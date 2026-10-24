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

#include "pdfappoutlineeditor.h"
#include "pdfappannotationcontroller.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"

#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfsecurityhandler.h"

#include <limits>

namespace pdf
{

/// Values of the outline editor
class PDFAppOutlineEditorHelper
{
public:
    PDFAppOutlineEditorHelper() = delete;

    /// Dynamic property of PDFAppDocument: a new outline item waits for its title
    static constexpr const char* PENDING_PROPERTY = "pdf4qtOutlineCreationPending";

    /// The document allows the editing of its outline (no security handler =
    /// everything; the outline belongs to the assembly and to the modification)
    static bool isAllowed(const PDFDocument* document)
    {
        const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
        return document && (!securityHandler || securityHandler->isAllowed(PDFSecurityHandler::Permission::Assemble) ||
                            securityHandler->isAllowed(PDFSecurityHandler::Permission::Modify));
    }

    /// Path of the parent of the item (empty for a top level item)
    static PDFAppOutlineEditor::OutlinePath getParent(PDFAppOutlineEditor::OutlinePath path)
    {
        if (!path.empty())
        {
            path.pop_back();
        }
        return path;
    }
};

PDFAppOutlineEditor::PDFAppOutlineEditor(PDFAppDocumentView* view, QObject* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("outlineEditor"));
}

PDFAppOutlineEditor::~PDFAppOutlineEditor()
{
    // A pending creation leaves no trace (the document may live longer)
    if (m_isCreationPending)
    {
        setPendingProperty(false);
    }
    if (m_document)
    {
        m_document->disconnect(this);
    }
}

PDFAppDocumentView* PDFAppOutlineEditor::getView() const
{
    return m_view.data();
}

PDFAppDocument* PDFAppOutlineEditor::getDocument() const
{
    if (m_document)
    {
        return m_document.data();
    }
    return m_view ? m_view->getDocument() : nullptr;
}

void PDFAppOutlineEditor::setDocument(PDFAppDocument* document)
{
    if (m_document == document)
    {
        return;
    }

    if (m_isCreationPending)
    {
        cancelCreation();
    }
    if (m_document)
    {
        m_document->disconnect(this);
    }

    m_document = document;
    m_currentItem.clear();
    m_editableDocument = nullptr;
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppOutlineEditor::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppOutlineEditor::stateChanged);
        connect(document, &PDFAppDocument::lockChanged, this, &PDFAppOutlineEditor::stateChanged);
        connect(document, &PDFAppDocument::operationsChanged, this, &PDFAppOutlineEditor::stateChanged);
        connect(document, &QObject::destroyed, this, &PDFAppOutlineEditor::onDocumentDestroyed);
    }
    Q_EMIT stateChanged();
}

void PDFAppOutlineEditor::onDocumentChanged()
{
    // Another change of the document (undo, redo, a change of an annotation, a
    // reload) ends a creation, which waits for its title - its place is not known
    QPointer<PDFAppOutlineEditor> guard(this);
    if (m_isCreationPending)
    {
        cancelCreation();
        if (!guard)
        {
            return;
        }
    }
    Q_EMIT stateChanged();
}

void PDFAppOutlineEditor::onDocumentDestroyed()
{
    // The document is not whole any more: only the pointers are forgotten
    m_document = nullptr;
    m_isCreationPending = false;
    m_currentItem.clear();
    m_editableDocument = nullptr;
}

bool PDFAppOutlineEditor::isEditingAllowed() const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady() || document->isLocked())
    {
        return false;
    }

    const PDFDocumentPointer pdfDocument = document->getDocument();
    if (!PDFAppOutlineEditorHelper::isAllowed(pdfDocument.data()))
    {
        return false;
    }

    // The outline is read once per version of the outline, not at every commit: a
    // change of annotations or of form fields keeps the objects of the outline - an
    // object of another structure in the outline makes it read only by its type,
    // which these changes do not change (review RC-CD)
    if (m_editableDocument != document || m_editableVersion != document->getOutlineVersion())
    {
        m_editableDocument = document;
        m_editableVersion = document->getOutlineVersion();
        m_isOutlineEditable = PDFAppAnnotationOperations::isOutlineEditable(pdfDocument.data());
    }
    return m_isOutlineEditable;
}

QString PDFAppOutlineEditor::getEditingRefusalReason() const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return tr("No document is shown.");
    }
    if (document->isLocked())
    {
        return tr("The document is locked. Unlock it to make changes.");
    }
    if (!PDFAppOutlineEditorHelper::isAllowed(document->getDocument().data()))
    {
        return tr("The document does not allow changes.");
    }
    if (!isEditingAllowed())
    {
        return tr("The outline of this document cannot be edited.");
    }
    return QString();
}

std::optional<PDFDestination> PDFAppOutlineEditor::getCurrentViewDestination() const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdfDocument = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    if (!pdfDocument || !proxy)
    {
        return std::nullopt;
    }

    const PDFInteger pageIndex = m_view->getCurrentPageIndex();
    const PDFCatalog* catalog = pdfDocument->getCatalog();
    if (pageIndex < 0 || pageIndex >= PDFInteger(catalog->getPageCount()))
    {
        return std::nullopt;
    }

    // The top edge of the view (in the middle, where a page lies in every layout);
    // the top of the page, when the view shows the area above it
    const PDFPage* page = catalog->getPage(size_t(pageIndex));
    const PDFReal top = getViewTop(m_view, pageIndex).value_or(page->getCropBox().bottom());

    constexpr PDFReal undefined = std::numeric_limits<PDFReal>::quiet_NaN();
    PDFDestination destination = PDFDestination::createXYZ(page->getPageReference(), undefined, top, undefined);
    destination.setPageIndex(pageIndex);
    return destination;
}

std::optional<PDFReal> PDFAppOutlineEditor::getViewTop(const PDFAppDocumentView* view, PDFInteger pageIndex)
{
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    PDFWidget* pdfWidget = view ? view->getPdfWidget() : nullptr;
    QWidget* drawWidget = pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr;
    if (!proxy || !drawWidget || pageIndex < 0)
    {
        return std::nullopt;
    }

    QPointF pagePoint;
    if (proxy->getPageUnderPoint(QPoint(drawWidget->width() / 2, 0), &pagePoint) == pageIndex && qIsFinite(pagePoint.y()))
    {
        return pagePoint.y();
    }
    return std::nullopt;
}

QString PDFAppOutlineEditor::getDefaultTitle()
{
    return tr("New item");
}

bool PDFAppOutlineEditor::isCreationPending(const PDFAppDocument* document)
{
    return document && document->property(PDFAppOutlineEditorHelper::PENDING_PROPERTY).toBool();
}

void PDFAppOutlineEditor::setPendingProperty(bool pending)
{
    if (PDFAppDocument* document = getDocument())
    {
        document->setProperty(PDFAppOutlineEditorHelper::PENDING_PROPERTY, pending ? QVariant(true) : QVariant());
    }
}

const PDFOutlineItem* PDFAppOutlineEditor::findItem(const OutlinePath& path) const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdfDocument = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    const QSharedPointer<PDFOutlineItem> root = pdfDocument ? pdfDocument->getCatalog()->getOutlineRootPtr() : QSharedPointer<PDFOutlineItem>();
    if (!pdfDocument)
    {
        return nullptr;
    }

    // The root without an outline (an empty path = the root)
    static const PDFOutlineItem emptyRoot;
    const PDFOutlineItem* item = root ? root.data() : &emptyRoot;
    for (size_t index : path)
    {
        if (index >= item->getChildCount())
        {
            return nullptr;
        }
        item = item->getChild(index);
    }
    return item;
}

PDFAppOutlinePanel* PDFAppOutlineEditor::findPanel() const
{
    PDFAppDocumentSidebar* sidebar = m_view ? m_view->getSidebar() : nullptr;
    return sidebar ? qobject_cast<PDFAppOutlinePanel*>(sidebar->findPanel(PDFAppDocumentPanelIds::outline())) : nullptr;
}

void PDFAppOutlineEditor::showStatusMessage(const QString& text) const
{
    if (m_view && m_view->getStatusBar() && !text.isEmpty())
    {
        m_view->getStatusBar()->showMessage(text, STATUS_MESSAGE_TIMEOUT);
    }
}

bool PDFAppOutlineEditor::createItemAtCurrentView()
{
    if (!m_view || !isEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }

    QPointer<PDFAppOutlineEditor> guard(this);
    if (m_isCreationPending)
    {
        cancelCreation();
        if (!guard)
        {
            return false;
        }
    }

    // An open inline rename of the panel is written first (the temporary item
    // would close its editor and its title would be lost; its commit would end
    // the new creation)
    if (QPointer<PDFAppOutlinePanel> panel = findPanel())
    {
        panel->finishInlineEditing();
        if (!guard || !m_view)
        {
            return false;
        }
        if (!isEditingAllowed())
        {
            showStatusMessage(getEditingRefusalReason());
            return false;
        }
    }

    const std::optional<PDFDestination> destination = getCurrentViewDestination();
    if (!destination)
    {
        return false;
    }

    // After the current item, at the end without one
    OutlinePath parent;
    size_t index = 0;
    if (hasCurrentItem())
    {
        parent = PDFAppOutlineEditorHelper::getParent(m_currentItem);
        index = m_currentItem.back() + 1;
    }
    else if (const PDFOutlineItem* root = findItem({}))
    {
        index = root->getChildCount();
    }

    QPointer<PDFAppDocumentView> view = m_view;
    m_previousPanelId = view->getCurrentPanelId();
    m_isCreationPending = true;
    m_pendingParent = parent;
    m_pendingIndex = index;
    m_pendingDestination = *destination;
    setPendingProperty(true);

    if (!view->isSidebarVisible())
    {
        view->setSidebarVisible(true);
    }
    const bool isShown = view && view->showPanel(PDFAppDocumentPanelIds::outline());
    if (!guard)
    {
        return false;
    }

    PDFAppOutlinePanel* panel = isShown ? findPanel() : nullptr;
    if (!panel || !m_isCreationPending)
    {
        cancelCreation();
        return false;
    }

    panel->beginPendingItem();
    if (!guard)
    {
        return false;
    }
    Q_EMIT stateChanged();
    return true;
}

bool PDFAppOutlineEditor::finishCreation(const QString& title)
{
    if (!m_isCreationPending)
    {
        return false;
    }

    QString text = PDFAppAnnotationOperations::sanitizeOutlineTitle(title);
    if (text.isEmpty())
    {
        text = getDefaultTitle();
    }

    // The creation ends before the commit (the change of the document is not a
    // change, which cancels it)
    const OutlinePath parent = m_pendingParent;
    const size_t index = m_pendingIndex;
    const PDFDestination destination = m_pendingDestination;
    m_isCreationPending = false;
    setPendingProperty(false);

    PDFAppDocument* document = getDocument();
    const PDFOutlineItem* parentItem = findItem(parent);
    if (!document || !parentItem)
    {
        // The temporary item of the panel goes away (cancelCreation works only
        // with a pending creation)
        m_isCreationPending = true;
        cancelCreation();
        return false;
    }

    OutlinePath newItem = parent;
    newItem.push_back(qMin(index, parentItem->getChildCount()));
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::insertOutlineItem(document->getDocument(), parent, index, text, destination);

    QPointer<PDFAppOutlineEditor> guard(this);
    const bool isCommitted = commit(result, tr("New outline item"), newItem);
    if (!guard)
    {
        return isCommitted;
    }

    if (!isCommitted)
    {
        // The temporary item of the panel goes away
        m_isCreationPending = true;
        cancelCreation();
    }
    return isCommitted;
}

void PDFAppOutlineEditor::cancelCreation()
{
    if (!m_isCreationPending)
    {
        return;
    }

    m_isCreationPending = false;
    setPendingProperty(false);

    QPointer<PDFAppOutlineEditor> guard(this);
    if (QPointer<PDFAppOutlinePanel> panel = findPanel())
    {
        panel->endPendingItem();
        if (!guard)
        {
            return;
        }
    }

    // A document without an outline: the panel shown before the creation
    const PDFOutlineItem* root = findItem({});
    if (m_view && (!root || root->getChildCount() == 0) && !m_previousPanelId.isEmpty() && m_previousPanelId != PDFAppDocumentPanelIds::outline())
    {
        m_view->showPanel(m_previousPanelId);
        if (!guard)
        {
            return;
        }
    }
    Q_EMIT stateChanged();
}

void PDFAppOutlineEditor::setCurrentItem(const OutlinePath& path)
{
    if (m_currentItem == path)
    {
        return;
    }
    m_currentItem = path;

    // The panel sets the current item from a signal of the selection model of its
    // view: stateChanged goes from the event loop, so its receivers may delete the
    // tab (with the view) while Qt is not on the stack (wrap-up item 10)
    if (!m_isCurrentItemChangePending)
    {
        m_isCurrentItemChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppOutlineEditor::onCurrentItemChangeQueued, Qt::QueuedConnection);
    }
}

void PDFAppOutlineEditor::onCurrentItemChangeQueued()
{
    if (m_isCurrentItemChangePending)
    {
        m_isCurrentItemChangePending = false;
        Q_EMIT stateChanged();
    }
}

bool PDFAppOutlineEditor::hasCurrentItem() const
{
    return !m_currentItem.empty() && findItem(m_currentItem);
}

bool PDFAppOutlineEditor::canMoveUp() const
{
    return isEditingAllowed() && hasCurrentItem() && m_currentItem.back() > 0;
}

bool PDFAppOutlineEditor::canMoveDown() const
{
    const PDFOutlineItem* parent = hasCurrentItem() ? findItem(PDFAppOutlineEditorHelper::getParent(m_currentItem)) : nullptr;
    return isEditingAllowed() && parent && m_currentItem.back() + 1 < parent->getChildCount();
}

bool PDFAppOutlineEditor::canIndent() const
{
    return isEditingAllowed() && hasCurrentItem() && m_currentItem.back() > 0 && m_currentItem.size() < PDFAppAnnotationOperations::MAXIMUM_OUTLINE_DEPTH;
}

bool PDFAppOutlineEditor::canOutdent() const
{
    return isEditingAllowed() && hasCurrentItem() && m_currentItem.size() > 1;
}

bool PDFAppOutlineEditor::isCurrentItemBold() const
{
    const PDFOutlineItem* item = m_currentItem.empty() ? nullptr : findItem(m_currentItem);
    return item && item->isFontBold();
}

bool PDFAppOutlineEditor::isCurrentItemItalic() const
{
    const PDFOutlineItem* item = m_currentItem.empty() ? nullptr : findItem(m_currentItem);
    return item && item->isFontItalics();
}

bool PDFAppOutlineEditor::renameCurrentItem()
{
    if (!m_view || !isEditingAllowed() || !hasCurrentItem())
    {
        return false;
    }

    // The inline editor of the panel
    const OutlinePath item = m_currentItem;
    QPointer<PDFAppOutlineEditor> guard(this);
    if (!m_view->isSidebarVisible())
    {
        m_view->setSidebarVisible(true);
    }
    const bool isShown = m_view && m_view->showPanel(PDFAppDocumentPanelIds::outline());
    if (!guard || !isShown)
    {
        return false;
    }

    PDFAppOutlinePanel* panel = findPanel();
    return panel && panel->editItem(item);
}

bool PDFAppOutlineEditor::deleteCurrentItem()
{
    return hasCurrentItem() && deleteItem(m_currentItem);
}

bool PDFAppOutlineEditor::moveCurrentItemUp()
{
    if (!canMoveUp())
    {
        return false;
    }
    return moveItem(m_currentItem, PDFAppOutlineEditorHelper::getParent(m_currentItem), m_currentItem.back() - 1);
}

bool PDFAppOutlineEditor::moveCurrentItemDown()
{
    if (!canMoveDown())
    {
        return false;
    }

    // The index is of the children before the move
    return moveItem(m_currentItem, PDFAppOutlineEditorHelper::getParent(m_currentItem), m_currentItem.back() + 2);
}

bool PDFAppOutlineEditor::indentCurrentItem()
{
    if (!canIndent())
    {
        return false;
    }

    // The last child of the previous item
    OutlinePath newParent = m_currentItem;
    newParent.back() -= 1;
    const PDFOutlineItem* parentItem = findItem(newParent);
    if (!parentItem)
    {
        return false;
    }
    return moveItem(m_currentItem, newParent, parentItem->getChildCount());
}

bool PDFAppOutlineEditor::outdentCurrentItem()
{
    if (!canOutdent())
    {
        return false;
    }

    // After its parent
    const OutlinePath parent = PDFAppOutlineEditorHelper::getParent(m_currentItem);
    return moveItem(m_currentItem, PDFAppOutlineEditorHelper::getParent(parent), parent.back() + 1);
}

bool PDFAppOutlineEditor::setCurrentItemTargetToCurrentView()
{
    return hasCurrentItem() && setItemTargetToCurrentView(m_currentItem);
}

bool PDFAppOutlineEditor::setCurrentItemBold(bool bold)
{
    return hasCurrentItem() && setItemStyle(m_currentItem, bold, isCurrentItemItalic());
}

bool PDFAppOutlineEditor::setCurrentItemItalic(bool italic)
{
    return hasCurrentItem() && setItemStyle(m_currentItem, isCurrentItemBold(), italic);
}

bool PDFAppOutlineEditor::renameItem(const OutlinePath& item, const QString& title)
{
    PDFAppDocument* document = getDocument();
    if (!document || !isEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }
    return commit(PDFAppAnnotationOperations::renameOutlineItem(document->getDocument(), item, title), tr("Rename outline item"), item);
}

bool PDFAppOutlineEditor::deleteItem(const OutlinePath& item)
{
    PDFAppDocument* document = getDocument();
    const PDFOutlineItem* parent = item.empty() ? nullptr : findItem(PDFAppOutlineEditorHelper::getParent(item));
    if (!document || !isEditingAllowed() || !parent || item.back() >= parent->getChildCount())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }

    // The next item, else the previous one, else the parent becomes current
    OutlinePath selected = item;
    if (item.back() + 1 >= parent->getChildCount())
    {
        if (item.back() > 0)
        {
            selected.back() -= 1;
        }
        else
        {
            selected.pop_back();
        }
    }
    return commit(PDFAppAnnotationOperations::removeOutlineItem(document->getDocument(), item), tr("Delete outline item"), selected);
}

bool PDFAppOutlineEditor::moveItem(const OutlinePath& item, const OutlinePath& newParent, size_t index)
{
    PDFAppDocument* document = getDocument();
    if (!document || !isEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }

    OutlinePath newPath;
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::moveOutlineItem(document->getDocument(), item, newParent, index, &newPath);

    QString description = tr("Move outline item");
    if (newPath.size() > item.size())
    {
        description = tr("Indent outline item");
    }
    else if (newPath.size() < item.size())
    {
        description = tr("Outdent outline item");
    }
    return commit(result, description, newPath);
}

bool PDFAppOutlineEditor::setItemTargetToCurrentView(const OutlinePath& item)
{
    PDFAppDocument* document = getDocument();
    const std::optional<PDFDestination> destination = getCurrentViewDestination();
    if (!document || !destination || !isEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }
    return commit(PDFAppAnnotationOperations::setOutlineItemDestination(document->getDocument(), item, *destination), tr("Set outline item target"), item);
}

bool PDFAppOutlineEditor::setItemStyle(const OutlinePath& item, bool bold, bool italic)
{
    PDFAppDocument* document = getDocument();
    if (!document || !isEditingAllowed())
    {
        showStatusMessage(getEditingRefusalReason());
        return false;
    }
    return commit(PDFAppAnnotationOperations::setOutlineItemStyle(document->getDocument(), item, bold, italic), tr("Change outline item style"), item);
}

bool PDFAppOutlineEditor::commit(const PDFAppAnnotationResult& result, const QString& description, const OutlinePath& selectedItem)
{
    PDFAppDocument* document = getDocument();
    if (!result || !document)
    {
        return false;
    }

    // A live change of the annotation bar is not merged over this step
    if (PDFAppAnnotationController* controller = m_view ? m_view->getAnnotationController() : nullptr)
    {
        controller->endMergeSession();
    }

    // The panel selects the changed item, when it shows the new outline
    QPointer<PDFAppOutlineEditor> guard(this);
    QPointer<PDFAppOutlinePanel> panel = findPanel();
    if (panel)
    {
        panel->setItemToSelect(selectedItem);
    }

    const bool isCommitted = document->commit(*result, description, document->getVersion(), PDFAppChangeSource::User);
    if (!guard)
    {
        return isCommitted;
    }
    if (panel)
    {
        panel->setItemToSelect({});
    }

    if (!isCommitted)
    {
        showStatusMessage(document->isLocked() ? tr("The document is locked. Unlock it to make changes.") : tr("The change cannot be made."));
        return false;
    }

    m_currentItem = selectedItem;
    Q_EMIT outlineChanged();
    if (!guard)
    {
        return true;
    }
    Q_EMIT stateChanged();
    return true;
}

}   // namespace pdf
