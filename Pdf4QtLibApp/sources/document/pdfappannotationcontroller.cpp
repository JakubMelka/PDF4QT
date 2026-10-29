// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfappannotationcontroller.h"
#include "pdfannotationstylerepository.h"
#include "pdfappcontext.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfappformcontroller.h"
#include "pdfappundostack.h"
#include "pdfdatabasenotifier.h"

#include "pdfannotationstyle.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfform.h"
#include "pdfsecurityhandler.h"
#include "pdfwidgetformmanager.h"

#include "pdfthememanager.h"

#include <QCryptographicHash>

namespace pdf
{

/// Values of the controller
class PDFAppAnnotationControllerHelper
{
public:
    PDFAppAnnotationControllerHelper() = delete;

    /// The document allows the permission (no security handler = everything)
    static bool isAllowed(const PDFDocument* document, PDFSecurityHandler::Permission permission)
    {
        const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
        return document && (!securityHandler || securityHandler->isAllowed(permission));
    }

    /// The object of the reference is a dictionary of the document (an annotation,
    /// which still exists)
    static bool exists(const PDFDocument* document, PDFObjectReference reference)
    {
        if (!document || !reference.isValid())
        {
            return false;
        }
        const PDFObject& object = document->getStorage().getObjectByReference(reference);
        return object.isDictionary();
    }

    /// Flag of the change of the kind
    static PDFModifiedDocument::ModificationFlag getFlag(PDFAppAnnotationChangeKind kind)
    {
        return kind == PDFAppAnnotationChangeKind::Form ? PDFModifiedDocument::FormField : PDFModifiedDocument::Annotation;
    }
};

PDFAppAnnotationController::PDFAppAnnotationController(PDFAppDocumentView* view) :
    BaseClass(view),
    m_view(view)
{
    setObjectName(QStringLiteral("annotationController"));
    if (view)
    {
        m_toolManager = view->getToolManager();
        m_annotationManager = view->getAnnotationManager();
        m_formManager = view->getFormManager();
    }
    connectEngine();

    PDFAppContext* context = view ? view->getContext() : nullptr;
    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppAnnotationController::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppAnnotationController::onEverythingChanged);
    }

    updateReadOnlyState();
}

PDFAppAnnotationController::~PDFAppAnnotationController()
{
    // The controller is deleted by the view before its managers (the managers keep
    // the delegated user interface, they are deleted right after the controller);
    // only the connections and the filter of the canvas are removed
    if (m_mergeSessionCanvas)
    {
        m_mergeSessionCanvas->removeEventFilter(this);
    }
    if (m_toolManager)
    {
        m_toolManager->disconnect(this);
    }
    if (m_annotationManager)
    {
        m_annotationManager->disconnect(this);
    }
    if (m_formManager)
    {
        m_formManager->disconnect(this);
    }
    if (m_document)
    {
        m_document->disconnect(this);
    }
}

void PDFAppAnnotationController::connectEngine()
{
    // Delegated user interface of the engine (E3, E4, E7): no dialog and no menu
    // of the engine, the requests come to the controller
    if (m_toolManager)
    {
        m_toolManager->setUserInterfaceDelegated(true);

        // A creation tool stays active until Esc or another tool (tools.md 9.8)
        m_toolManager->setCreationToolsPersistent(true);

        // Rectangle = Square, freehand = Ink, as the specification intends (owner, O20)
        m_toolManager->setSpecificationShapeTypes(true);
        connect(m_toolManager.data(), &PDFToolManager::documentModified, this, &PDFAppAnnotationController::onToolDocumentModified);
        connect(m_toolManager.data(), &PDFToolManager::annotationCreationRequested, this, &PDFAppAnnotationController::onCreationRequested);
        connect(m_toolManager.data(), &PDFToolManager::activeToolChanged, this, &PDFAppAnnotationController::onActiveToolChanged);
        if (PDFSelectTextTool* selectTextTool = m_toolManager->getSelectTextTool())
        {
            selectTextTool->setAnnotationsSelectable(true);
        }
    }

    if (m_annotationManager)
    {
        m_annotationManager->setUserInterfaceDelegated(true);
        m_annotationManager->setDesignSelectionLook(true);

        // Links are selectable (their properties 1171:28262), a plain click follows them
        m_annotationManager->setLinksSelectable(true);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::documentModified, this, &PDFAppAnnotationController::onAnnotationDocumentModified);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::selectionChanged, this, &PDFAppAnnotationController::onEngineSelectionChanged);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::menuRequested, this, &PDFAppAnnotationController::onMenuRequested);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::popupRequested, this, &PDFAppAnnotationController::onPopupRequested);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::propertiesRequested, this, &PDFAppAnnotationController::onPropertiesRequested);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::geometryRequested, this, &PDFAppAnnotationController::onGeometryRequested);
        connect(m_annotationManager.data(), &PDFWidgetAnnotationManager::copyToPagesRequested, this, &PDFAppAnnotationController::onCopyToPagesRequested);
    }

    if (m_formManager)
    {
        connect(m_formManager.data(), &PDFWidgetFormManager::documentModified, this, &PDFAppAnnotationController::onFormDocumentModified);
    }
}

PDFAppDocumentView* PDFAppAnnotationController::getView() const
{
    return m_view.data();
}

PDFAppDocument* PDFAppAnnotationController::getDocument() const
{
    return m_document.data();
}

PDFToolManager* PDFAppAnnotationController::getToolManager() const
{
    return m_toolManager.data();
}

PDFWidgetAnnotationManager* PDFAppAnnotationController::getAnnotationManager() const
{
    return m_annotationManager.data();
}

PDFWidgetFormManager* PDFAppAnnotationController::getFormManager() const
{
    return m_formManager.data();
}

void PDFAppAnnotationController::setDocument(PDFAppDocument* document)
{
    if (m_document == document)
    {
        return;
    }

    if (m_document)
    {
        m_document->disconnect(this);
    }

    endMergeSession();
    m_document = document;
    m_knownVersion = document ? document->getVersion() : 0;
    m_lastSelection.clear();
    m_pendingDescription.clear();

    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppAnnotationController::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppAnnotationController::onDocumentStateChanged);
        connect(document, &PDFAppDocument::lockChanged, this, &PDFAppAnnotationController::onDocumentStateChanged);
    }
    updateReadOnlyState();
}

// ---------------------------------------------------------------------- commit

void PDFAppAnnotationController::setPendingDescription(const QString& description)
{
    m_pendingDescription = description.left(MAXIMUM_DESCRIPTION_LENGTH);
}

void PDFAppAnnotationController::beginMergeSession(const QString& key)
{
    if (m_mergeKey != key.left(1024))
    {
        endMergeSession();
        m_mergeKey = key.left(1024);

        // A press or a key on the canvas (a drag, a nudge, a click) is not a part of
        // the session: the canvas is watched while the session is open (the draw
        // widget of the view can be replaced by a change of the rendering engine)
        QWidget* canvas = getCanvasWidget();
        if (!m_mergeKey.isEmpty() && canvas)
        {
            canvas->installEventFilter(this);
            m_mergeSessionCanvas = canvas;
        }
    }

    // The engine changes up to endEngineChange join the session
    m_isMergeArmed = !m_mergeKey.isEmpty();
}

void PDFAppAnnotationController::beginEngineChange(const QString& description, const QString& mergeKey)
{
    m_savedDescription = m_isEngineChangeOpen ? m_savedDescription : m_pendingDescription;
    m_engineChangeCommitCounter = m_commitCounter;
    m_isEngineChangeOpen = true;
    setPendingDescription(description);
    if (!mergeKey.isEmpty())
    {
        beginMergeSession(mergeKey);
    }
}

void PDFAppAnnotationController::endEngineChange()
{
    // Nothing has been committed: the description does not stay for another
    // change (the description of an active creation tool is given back)
    if (m_isEngineChangeOpen && m_commitCounter == m_engineChangeCommitCounter)
    {
        m_pendingDescription = m_savedDescription;
    }
    m_savedDescription.clear();
    m_isEngineChangeOpen = false;
    m_isMergeArmed = false;
}

QWidget* PDFAppAnnotationController::getCanvasWidget() const
{
    PDFWidget* widget = m_view ? m_view->getPdfWidget() : nullptr;
    return widget && widget->getDrawWidget() ? widget->getDrawWidget()->getWidget() : nullptr;
}

bool PDFAppAnnotationController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_mergeSessionCanvas.data())
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            case QEvent::KeyPress:
            case QEvent::TouchBegin:
            case QEvent::TabletPress:
                endMergeSession();
                break;

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppAnnotationController::endMergeSession()
{
    if (m_mergeSessionCanvas)
    {
        m_mergeSessionCanvas->removeEventFilter(this);
        m_mergeSessionCanvas = nullptr;
    }

    // Only a session of the controller is ended (a merge of another source - a
    // direct commit of the model, MCP - is not touched)
    m_isMergeArmed = false;
    if (m_mergeKey.isEmpty())
    {
        return;
    }

    m_mergeKey.clear();
    if (m_document)
    {
        m_document->closeUndoMerge();
    }
}

QString PDFAppAnnotationController::createMergeKey(const QString& property, const std::vector<PDFObjectReference>& references, quint64 counter)
{
    // The references are hashed, so a key of a large selection stays short
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const PDFObjectReference& reference : references)
    {
        const QByteArray item = QByteArray::number(reference.objectNumber) + ' ' + QByteArray::number(reference.generation) + ';';
        hash.addData(item);
    }
    return property.left(64) + QLatin1Char(':') + QString::fromLatin1(hash.result().toHex().left(16)) + QLatin1Char(':') + QString::number(counter);
}

QString PDFAppAnnotationController::getDefaultDescription(PDFAppAnnotationChangeKind kind)
{
    switch (kind)
    {
        case PDFAppAnnotationChangeKind::Tool:
            return tr("Add annotation");
        case PDFAppAnnotationChangeKind::Annotation:
            return tr("Edit annotations");
        case PDFAppAnnotationChangeKind::Form:
            return tr("Fill form field");
    }
    return tr("Edit annotations");
}

bool PDFAppAnnotationController::commit(const PDFModifiedDocument& modified, PDFAppAnnotationChangeKind kind, const QString& description)
{
    return commitEngineChange(modified, kind, description, false);
}

bool PDFAppAnnotationController::commitEngineChange(const PDFModifiedDocument& modified, PDFAppAnnotationChangeKind kind, const QString& description, bool isFromEngine)
{
    QString text = description.left(MAXIMUM_DESCRIPTION_LENGTH);
    if (text.isEmpty())
    {
        text = m_pendingDescription;
    }
    if (text.isEmpty())
    {
        text = getDefaultDescription(kind);
    }
    m_pendingDescription.clear();
    ++m_commitCounter;

    // The document of the change is shared (the engine creates it by
    // PDFDocumentModifier); a change without a shared document cannot be an undo step
    const PDFDocumentPointer newDocument = modified;
    PDFAppDocument* document = m_document.data();
    if (!document || !document->isReady() || newDocument.isNull())
    {
        return false;
    }

    // The change must come from the version, which the engine shows; the document
    // refuses a locked document, a blocking operation and a stale version
    QString refusal;
    if (document->isPagedXFA())
    {
        refusal = PDFAppDocument::getPagedXFARefusalText();
    }
    else if (document->isLocked())
    {
        refusal = tr("The document is locked. Unlock it to make changes.");
    }
    else if (document->isBlockedByOperation())
    {
        refusal = tr("The document cannot be changed while an operation is running.");
    }
    else if (kind == PDFAppAnnotationChangeKind::Form ? !isFormFillingAllowed() : !isAnnotationEditingAllowed())
    {
        // The permissions of the document (O8) are checked for every change, not
        // only by the user interface: a dialog or a popup opened before a reload
        // of a restricted file, or an operation of another caller, cannot bypass them
        refusal = tr("The document does not allow changes.");
    }
    else if (isFromEngine && !isEngineDocumentCurrent())
    {
        // The engine has made the change from a version, which it shows, but
        // which is not the current one (the view has not got the new version
        // yet): the change would discard the newer version
        refusal = tr("The change cannot be made.");
    }

    PDFModifiedDocument::ModificationFlags flags = modified.getFlags();
    flags |= PDFAppAnnotationControllerHelper::getFlag(kind);
    const PDFModifiedDocument change(newDocument, modified.getOptionalContentActivity(), flags);

    // Only the engine changes of the annotations made while the session is armed
    // belong to it (a live color / width of the bar); a new annotation, a form
    // field, a reply, a command or a change of a panel is a new step, which ends
    // the session
    const bool isMerged = kind == PDFAppAnnotationChangeKind::Annotation && isFromEngine && m_isMergeArmed && !m_mergeKey.isEmpty();
    const QString mergeKey = isMerged ? m_mergeKey : QString();

    QPointer<PDFAppAnnotationController> guard(this);
    bool isCommitted = false;
    if (refusal.isEmpty())
    {
        if (!isMerged)
        {
            endMergeSession();
        }
        m_isCommitting = true;
        isCommitted = document->commit(change, text, document->getVersion(), PDFAppChangeSource::User, mergeKey);
        if (!guard)
        {
            return isCommitted;
        }
        m_isCommitting = false;
        if (!isCommitted)
        {
            refusal = tr("The change cannot be made.");
        }
    }

    if (!isCommitted)
    {
        // The engine keeps the document of the model (it waits for setDocument):
        // the refused change is not shown
        showStatusMessage(refusal);
        Q_EMIT changeRefused(refusal);
        return false;
    }

    Q_EMIT changeCommitted(kind);
    return true;
}

bool PDFAppAnnotationController::isEngineDocumentCurrent() const
{
    // The engine works on the document of the view (its proxy)
    const PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    const PDFDocument* current = m_document ? m_document->getDocument().data() : nullptr;
    return !proxy || proxy->getDocument() == current;
}

void PDFAppAnnotationController::onToolDocumentModified(PDFModifiedDocument document)
{
    commitEngineChange(document, PDFAppAnnotationChangeKind::Tool, QString(), true);
}

void PDFAppAnnotationController::onAnnotationDocumentModified(PDFModifiedDocument document)
{
    commitEngineChange(document, PDFAppAnnotationChangeKind::Annotation, QString(), true);
}

void PDFAppAnnotationController::onFormDocumentModified(PDFModifiedDocument document)
{
    commitEngineChange(document, PDFAppAnnotationChangeKind::Form, QString(), true);
}

void PDFAppAnnotationController::showStatusMessage(const QString& text)
{
    if (m_view && m_view->getStatusBar() && !text.isEmpty())
    {
        m_view->getStatusBar()->showMessage(text, STATUS_MESSAGE_TIMEOUT);
    }
}

// ---------------------------------------------------------------------- state

bool PDFAppAnnotationController::isAnnotationEditingAllowed() const
{
    const PDFAppDocument* document = m_document.data();
    if (!document || !document->isEditable())
    {
        return false;
    }
    return PDFAppAnnotationControllerHelper::isAllowed(document->getDocument().data(), PDFSecurityHandler::Permission::ModifyInteractiveItems);
}

bool PDFAppAnnotationController::isFormFillingAllowed() const
{
    const PDFAppDocument* document = m_document.data();
    if (!document || !document->isEditable())
    {
        return false;
    }

    // Bit 9 allows the filling of the form also without bit 6 (PDF 2.0, Table 22);
    // an XFA form is shown, but not filled (step 4, package D)
    const PDFDocument* pdfDocument = document->getDocument().data();
    if (PDFAppFormController::isXFAForm(pdfDocument))
    {
        return false;
    }
    return PDFAppAnnotationControllerHelper::isAllowed(pdfDocument, PDFSecurityHandler::Permission::ModifyFormFields) ||
           PDFAppAnnotationControllerHelper::isAllowed(pdfDocument, PDFSecurityHandler::Permission::ModifyInteractiveItems);
}

QString PDFAppAnnotationController::getEditingRefusalReason() const
{
    const PDFAppDocument* document = m_document.data();
    if (!document || !document->isReady())
    {
        return tr("No document is shown.");
    }
    if (document->isPagedXFA())
    {
        return PDFAppDocument::getPagedXFARefusalText();
    }
    if (document->isLocked())
    {
        return tr("The document is locked. Unlock it to make changes.");
    }
    if (!isAnnotationEditingAllowed())
    {
        return tr("The document does not allow changes.");
    }
    return QString();
}

void PDFAppAnnotationController::updateReadOnlyState()
{
    const bool isAnnotationEditingAllowed = this->isAnnotationEditingAllowed();
    const bool isFormFillingAllowed = this->isFormFillingAllowed();

    if (m_annotationManager && m_annotationManager->isReadOnly() == isAnnotationEditingAllowed)
    {
        m_annotationManager->setReadOnly(!isAnnotationEditingAllowed);
    }

    if (m_formManager)
    {
        PDFFormManager::FormAppearanceFlags flags = m_formManager->getAppearanceFlags();
        const PDFFormManager::FormAppearanceFlags oldFlags = flags;
        flags.setFlag(PDFFormManager::ReadOnlyForm, !isFormFillingAllowed);
        if (flags != oldFlags)
        {
            m_formManager->setAppearanceFlags(flags);
        }
    }

    // A creation tool ends, when the annotations cannot be changed (O20)
    QPointer<PDFAppAnnotationController> guard(this);
    if (!isAnnotationEditingAllowed && isCreationTool(getActiveTool()))
    {
        endActiveTool();
        if (!guard)
        {
            return;
        }
    }

    if (!m_isStateKnown || m_isAnnotationEditingAllowed != isAnnotationEditingAllowed || m_isFormFillingAllowed != isFormFillingAllowed)
    {
        const bool isFirst = !m_isStateKnown;
        m_isStateKnown = true;
        m_isAnnotationEditingAllowed = isAnnotationEditingAllowed;
        m_isFormFillingAllowed = isFormFillingAllowed;
        if (!isFirst)
        {
            Q_EMIT editingAllowedChanged();
        }
    }
}

void PDFAppAnnotationController::onDocumentStateChanged()
{
    updateReadOnlyState();
}

void PDFAppAnnotationController::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);

    // Permissions follow the document (a reload), the merge session ends with a
    // change, which is not of the session (undo, redo, reload)
    const bool isOwnCommit = m_isCommitting;
    if (!isOwnCommit)
    {
        endMergeSession();
    }

    // The view gives the engine the new version before this slot: the selection
    // changes of the engine up to here are not the selection of the user
    m_knownVersion = m_document ? m_document->getVersion() : 0;

    QPointer<PDFAppAnnotationController> guard(this);
    updateReadOnlyState();
    if (!guard)
    {
        return;
    }

    // Undo / redo (also a Reset change of another source): the selection of the
    // annotations, which still exist, is restored. A loaded or reloaded document
    // (its undo stack is cleared) may have other objects under the references.
    if (!isOwnCommit)
    {
        const PDFAppUndoStack* stack = m_document ? m_document->getUndoStack() : nullptr;
        if (stack && (stack->canUndo() || stack->canRedo()))
        {
            restoreSelection();
        }
        else
        {
            m_lastSelection.clear();
        }
    }
}

void PDFAppAnnotationController::restoreSelection()
{
    if (!m_annotationManager || !m_document || m_lastSelection.empty())
    {
        return;
    }

    const PDFDocumentPointer document = m_document->getDocument();
    std::vector<PDFObjectReference> selection;
    for (const PDFObjectReference& reference : m_lastSelection)
    {
        if (PDFAppAnnotationControllerHelper::exists(document.data(), reference))
        {
            selection.push_back(reference);
        }
    }

    if (selection != m_annotationManager->getSelectedAnnotations())
    {
        m_annotationManager->setSelectedAnnotations(selection);
    }
}

void PDFAppAnnotationController::applyThemeColors(PDFViewColors& colors)
{
    // Selection drawing of 1015:17319 / 1017:17699 / 1018:18079 (step 4 Figma map 3)
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    colors.annotationSelection = manager->getColor(PDFThemeColors::BgAccent);
    colors.annotationHandle = manager->getColor(PDFThemeColors::BgSurface);
    colors.annotationPoint = manager->getColor(PDFThemeColors::BgAccent);
    colors.annotationPointActive = manager->getColor(PDFThemeColors::BgSurface);
    colors.rubberBand = manager->getColor(PDFThemeColors::BgAccent);
    colors.snapMarker = manager->getColor(PDFThemeColors::BgAccent);
    // cross of the pick tools of the shapes and of the link targets (the C key: whole canvas)
    colors.pickCross = manager->getColor(PDFThemeColors::BgAccent);
    // lock-badge 1026:21991: icon/lock in icon/muted on bg/surface, the outline and the
    // dashed frame of a locked annotation border/strong
    colors.lockBadge = manager->getColor(PDFThemeColors::IconMuted);
    colors.lockBadgeBorder = manager->getColor(PDFThemeColors::BorderStrong);

    // drag-tooltip 1017:18217
    colors.infoText = manager->getColor(PDFThemeColors::TextSecondary);
    colors.infoBackground = manager->getColor(PDFThemeColors::BgSurface);
    colors.infoBorder = manager->getColor(PDFThemeColors::BorderDefault);
    colors.infoShadow = manager->getColor(PDFThemeColors::EffectShadow);
}

// ---------------------------------------------------------------------- tools

PDFWidgetTool* PDFAppAnnotationController::getActiveTool() const
{
    return m_toolManager ? m_toolManager->getActiveTool() : nullptr;
}

void PDFAppAnnotationController::addTool(PDFWidgetTool* tool)
{
    if (m_toolManager && tool)
    {
        m_toolManager->addTool(tool);
    }
}

bool PDFAppAnnotationController::activateTool(PDFWidgetTool* tool)
{
    if (!m_toolManager)
    {
        return false;
    }
    if (!tool)
    {
        endActiveTool();
        return true;
    }
    if (isCreationTool(tool) && !isAnnotationEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }

    // A slot of the change of the active tool may delete the view (the tab)
    QPointer<PDFAppAnnotationController> self(this);
    m_toolManager->setActiveTool(tool);
    return self && m_toolManager && m_toolManager->getActiveTool() == tool;
}

void PDFAppAnnotationController::endActiveTool()
{
    // The chosen mode of the canvas (Select text or Hand) comes back
    if (m_view && m_view->getToolManager() == m_toolManager.data())
    {
        m_view->activateDefaultCanvasTool();
    }
    else if (m_toolManager)
    {
        m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
    }
}

bool PDFAppAnnotationController::isCreationTool(const PDFWidgetTool* tool) const
{
    if (!tool || !m_toolManager)
    {
        return false;
    }

    for (int i = 0; i < PDFToolManager::ToolEnd; ++i)
    {
        if (m_toolManager->getPredefinedTool(static_cast<PDFToolManager::PredefinedTools>(i)) == tool)
        {
            return false;
        }
    }
    return true;
}

void PDFAppAnnotationController::onActiveToolChanged(PDFWidgetTool* tool)
{
    endMergeSession();
    m_pendingDescription.clear();
    Q_EMIT activeToolChanged(tool);
}

void PDFAppAnnotationController::reloadToolStyles()
{
    if (!m_toolManager)
    {
        return;
    }

    // A style changed by another instance (or by the bar of another tab); the
    // tools are children of the view (also through its tool manager)
    if (m_view)
    {
        for (PDFWidgetTool* tool : m_view->findChildren<PDFWidgetTool*>())
        {
            if (PDFAnnotationStyleManager* styleManager = tool->getStyleManager())
            {
                styleManager->reloadStyle();
            }
        }
    }
}

void PDFAppAnnotationController::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);
    if (topic == PDFAnnotationStyleRepository::getTopic())
    {
        reloadToolStyles();
    }
}

void PDFAppAnnotationController::onEverythingChanged()
{
    reloadToolStyles();
}

// ---------------------------------------------------------------------- selection

std::vector<PDFObjectReference> PDFAppAnnotationController::getSelectedAnnotations() const
{
    return m_annotationManager ? m_annotationManager->getSelectedAnnotations() : std::vector<PDFObjectReference>();
}

bool PDFAppAnnotationController::hasAnnotationSelection() const
{
    return m_annotationManager && m_annotationManager->hasSelection();
}

void PDFAppAnnotationController::selectAnnotations(const std::vector<PDFObjectReference>& references)
{
    if (!m_annotationManager)
    {
        return;
    }

    // Only the annotations of the current document (a panel may hold stale rows)
    const PDFDocumentPointer document = m_document ? m_document->getDocument() : PDFDocumentPointer();
    std::vector<PDFObjectReference> selection;
    for (const PDFObjectReference& reference : references)
    {
        if (PDFAppAnnotationControllerHelper::exists(document.data(), reference))
        {
            selection.push_back(reference);
        }
    }
    m_annotationManager->setSelectedAnnotations(selection);
}

void PDFAppAnnotationController::clearAnnotationSelection()
{
    if (m_annotationManager)
    {
        m_annotationManager->clearSelection();
    }
}

void PDFAppAnnotationController::copySelection()
{
    if (m_annotationManager && m_annotationManager->hasSelection())
    {
        m_annotationManager->copySelectedAnnotations();
    }
}

void PDFAppAnnotationController::cutSelection()
{
    if (m_annotationManager && m_annotationManager->hasSelection())
    {
        endMergeSession();
        QPointer<PDFAppAnnotationController> guard(this);
        beginEngineChange(tr("Cut annotations"));
        m_annotationManager->cutSelectedAnnotations();
        if (guard)
        {
            endEngineChange();
        }
    }
}

bool PDFAppAnnotationController::canPaste() const
{
    return m_annotationManager && isAnnotationEditingAllowed() && m_annotationManager->canPasteAnnotations();
}

void PDFAppAnnotationController::paste(bool keepPosition)
{
    if (canPaste())
    {
        endMergeSession();
        QPointer<PDFAppAnnotationController> guard(this);
        beginEngineChange(tr("Paste annotations"));
        m_annotationManager->pasteAnnotations(std::nullopt, keepPosition);
        if (guard)
        {
            endEngineChange();
        }
    }
}

void PDFAppAnnotationController::deleteSelection()
{
    if (m_annotationManager && m_annotationManager->hasSelection())
    {
        endMergeSession();
        QPointer<PDFAppAnnotationController> guard(this);
        beginEngineChange(tr("Delete annotations"));
        m_annotationManager->deleteSelectedAnnotations();
        if (guard)
        {
            endEngineChange();
        }
    }
}

void PDFAppAnnotationController::selectAll()
{
    if (m_annotationManager)
    {
        m_annotationManager->selectAllAnnotations();
    }
}

void PDFAppAnnotationController::onEngineSelectionChanged()
{
    // A live change of the bar belongs to one selection
    endMergeSession();

    // A selection changed by a version, which the controller has not seen yet
    // (a Reset change of undo / redo clears the selection of the engine) is
    // not remembered: restoreSelection gives the selection of the user back
    if (!m_document || m_document->getVersion() == m_knownVersion)
    {
        m_lastSelection = getSelectedAnnotations();
    }

    // The engine is still emitting its signal (and it uses itself after it, also
    // inside of its mouse handlers): the signal of the controller comes from the
    // event loop, so a slot may delete the tab or the window. Several changes of
    // one turn of the event loop are one signal; the receivers read the current
    // selection.
    if (!m_isSelectionChangePending)
    {
        m_isSelectionChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onSelectionChangedQueued, Qt::QueuedConnection);
    }
}

void PDFAppAnnotationController::onSelectionChangedQueued()
{
    if (!m_isSelectionChangePending)
    {
        return;
    }
    m_isSelectionChangePending = false;
    Q_EMIT selectionChanged();
}

// ---------------------------------------------------------------------- requests

void PDFAppAnnotationController::requestProperties(PDFObjectReference annotation)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onPropertiesRequestedQueued, Qt::QueuedConnection, annotation);
}

void PDFAppAnnotationController::requestComment(PDFObjectReference annotation, bool compose)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onCommentRequestedQueued, Qt::QueuedConnection, annotation, compose);
}

void PDFAppAnnotationController::requestReply(PDFObjectReference annotation)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onReplyRequestedQueued, Qt::QueuedConnection, annotation);
}

void PDFAppAnnotationController::requestGeometry(PDFObjectReference annotation)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onGeometryRequestedQueued, Qt::QueuedConnection, annotation);
}

void PDFAppAnnotationController::requestCopyToPages()
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onCopyToPagesRequestedQueued, Qt::QueuedConnection);
}

void PDFAppAnnotationController::requestMenu(const PDFAnnotationMenuRequest& request)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onMenuRequestedQueued, Qt::QueuedConnection, request);
}

void PDFAppAnnotationController::onCreationRequested(const PDFAnnotationCreationRequest& request)
{
    // From a mouse handler of the tool: the dialog runs from the event loop
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onCreationRequestedQueued, Qt::QueuedConnection, request);
}

void PDFAppAnnotationController::onMenuRequested(const PDFAnnotationMenuRequest& request)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onMenuRequestedQueued, Qt::QueuedConnection, request);
}

void PDFAppAnnotationController::onPopupRequested(PDFObjectReference annotation, PDFObjectReference page, QPoint globalPosition)
{
    QMetaObject::invokeMethod(this, &PDFAppAnnotationController::onPopupRequestedQueued, Qt::QueuedConnection, annotation, page, globalPosition);
}

void PDFAppAnnotationController::onPropertiesRequested(PDFObjectReference annotation)
{
    requestProperties(annotation);
}

void PDFAppAnnotationController::onGeometryRequested(PDFObjectReference annotation)
{
    requestGeometry(annotation);
}

void PDFAppAnnotationController::onCopyToPagesRequested()
{
    requestCopyToPages();
}

void PDFAppAnnotationController::onCreationRequestedQueued(const PDFAnnotationCreationRequest& request)
{
    if (!isAnnotationEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return;
    }
    Q_EMIT creationRequested(request);
}

void PDFAppAnnotationController::onMenuRequestedQueued(const PDFAnnotationMenuRequest& request)
{
    Q_EMIT menuRequested(request);
}

void PDFAppAnnotationController::onPopupRequestedQueued(PDFObjectReference annotation, PDFObjectReference page, QPoint globalPosition)
{
    Q_EMIT popupRequested(annotation, page, globalPosition);
}

void PDFAppAnnotationController::onPropertiesRequestedQueued(PDFObjectReference annotation)
{
    endMergeSession();
    Q_EMIT propertiesRequested(annotation);
}

void PDFAppAnnotationController::onGeometryRequestedQueued(PDFObjectReference annotation)
{
    endMergeSession();
    Q_EMIT geometryRequested(annotation);
}

void PDFAppAnnotationController::onCopyToPagesRequestedQueued()
{
    endMergeSession();
    Q_EMIT copyToPagesRequested();
}

void PDFAppAnnotationController::onCommentRequestedQueued(PDFObjectReference annotation, bool compose)
{
    endMergeSession();
    Q_EMIT commentRequested(annotation, compose);
}

void PDFAppAnnotationController::onReplyRequestedQueued(PDFObjectReference annotation)
{
    endMergeSession();
    Q_EMIT replyRequested(annotation);
}

}   // namespace pdf
