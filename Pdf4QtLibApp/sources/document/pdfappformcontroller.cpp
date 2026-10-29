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

#include "pdfappformcontroller.h"
#include "pdfappannotationcontroller.h"
#include "pdfappdocument.h"
#include "pdfappdocumentactions.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgetsnapshot.h"

#include <QFocusEvent>
#include <QMouseEvent>
#include <QWidget>

namespace pdf
{

/// Values of the form controller
class PDFAppFormControllerHelper
{
public:
    PDFAppFormControllerHelper() = delete;

    /// An editor of the field holds its value until it is closed (a text field,
    /// which is not a password - a password is never written to the document -, a
    /// combo box, a list box); buttons are committed by their click
    static bool isPendingEditField(const PDFFormField* field)
    {
        if (!field)
        {
            return false;
        }

        switch (field->getFieldType())
        {
            case PDFFormField::FieldType::Text:
                return !field->getFlags().testFlag(PDFFormField::Password);

            case PDFFormField::FieldType::Choice:
                return true;

            default:
                return false;
        }
    }

    /// Index of the page, which holds the widget annotation (also an annotation
    /// without /P); PDFCatalog::INVALID_PAGE_INDEX, when it is on no page
    static size_t findPageIndex(const PDFDocument* document, const PDFFormWidget& widget)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageIndex = catalog->getPageIndexFromPageReference(widget.getPage());
        if (pageIndex != PDFCatalog::INVALID_PAGE_INDEX)
        {
            return pageIndex;
        }

        const size_t pageCount = catalog->getPageCount();
        for (size_t i = 0; i < pageCount; ++i)
        {
            const PDFPage* page = catalog->getPage(i);
            const std::vector<PDFObjectReference>& annotations = page->getAnnotations();
            if (std::find(annotations.cbegin(), annotations.cend(), widget.getWidget()) != annotations.cend())
            {
                return i;
            }
        }
        return PDFCatalog::INVALID_PAGE_INDEX;
    }

    /// Shift of an interval [low, high], which moves it into [visibleLow,
    /// visibleHigh]; an interval larger than the visible one shows its start
    static qreal getScrollDelta(qreal low, qreal high, qreal visibleLow, qreal visibleHigh)
    {
        if (high - low > visibleHigh - visibleLow || low < visibleLow)
        {
            return visibleLow - low;
        }
        if (high > visibleHigh)
        {
            return visibleHigh - high;
        }
        return 0.0;
    }

    /// The canvas loses the focus to another widget of the window (not to another
    /// window, a popup or a menu - the user comes back to the field)
    static bool isFocusLeavingCanvas(Qt::FocusReason reason)
    {
        switch (reason)
        {
            case Qt::ActiveWindowFocusReason:
            case Qt::PopupFocusReason:
            case Qt::MenuBarFocusReason:
                return false;

            default:
                return true;
        }
    }
};

PDFAppFormController::PDFAppFormController(PDFAppDocumentView* view) :
    BaseClass(view),
    m_view(view)
{
    setObjectName(QStringLiteral("formController"));

    if (view)
    {
        m_formManager = view->getFormManager();
        if (m_formManager)
        {
            // Tab goes through the fields in the order of the pages (entry Tabs), after the
            // last field the focus leaves the canvas (O20, no keyboard trap)
            m_formManager->setPageTabOrder(true);
        }
        if (PDFWidget* pdfWidget = view->getPdfWidget(); pdfWidget && pdfWidget->getDrawWidget())
        {
            m_drawWidget = pdfWidget->getDrawWidget()->getWidget();
        }

        connect(view, &PDFAppDocumentView::infoBarActionTriggered, this, &PDFAppFormController::onInfoBarActionTriggered);
        if (PDFAppAnnotationController* annotationController = view->getAnnotationController())
        {
            connect(annotationController, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppFormController::onEditingAllowedChanged);
        }
    }

    installFilters();
}

PDFAppFormController::~PDFAppFormController()
{
    if (m_drawWidget)
    {
        m_drawWidget->removeEventFilter(this);
    }
    if (m_view)
    {
        m_view->removeEventFilter(this);
        m_view->disconnect(this);
    }
    if (m_formManager)
    {
        m_formManager->disconnect(this);
    }
    if (m_document)
    {
        m_document->disconnect(this);

        // The edits of a destroyed editor are not pending any more
        m_document->setPendingEdits(false);
    }
}

void PDFAppFormController::installFilters()
{
    // Input of the canvas (focus changes of the editors, the keyboard, the loss of
    // the focus) and the language of the view (texts of the info bars)
    if (m_drawWidget)
    {
        m_drawWidget->installEventFilter(this);
    }
    if (m_view)
    {
        m_view->installEventFilter(this);
    }
}

PDFAppDocumentView* PDFAppFormController::getView() const
{
    return m_view.data();
}

PDFAppDocument* PDFAppFormController::getDocument() const
{
    return m_document.data();
}

PDFWidgetFormManager* PDFAppFormController::getFormManager() const
{
    return m_formManager.data();
}

PDFAppAnnotationController* PDFAppFormController::getAnnotationController() const
{
    return m_view ? m_view->getAnnotationController() : nullptr;
}

void PDFAppFormController::setDocument(PDFAppDocument* document)
{
    if (m_document == document)
    {
        return;
    }

    if (m_document)
    {
        m_document->disconnect(this);
        m_document->setPendingEdits(false);
    }

    m_document = document;
    m_focusedWidget = PDFObjectReference();
    m_isCalculatedBarDismissed = false;
    m_isXFABarDismissed = false;
    m_isInfoBarUpdateNeeded = true;

    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppFormController::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppFormController::onDocumentStateChanged);
        connect(document, &PDFAppDocument::pendingEditsCommitRequested, this, &PDFAppFormController::onPendingEditsCommitRequested);
    }
    scheduleUpdate();
}

// ---------------------------------------------------------------------- settings

PDFFormManager::FormAppearanceFlags PDFAppFormController::getSettingsFlags(const PDFSettingsRepository& settings)
{
    PDFFormManager::FormAppearanceFlags flags = PDFFormManager::None;
    flags.setFlag(PDFFormManager::HighlightFields, settings.getBool(PDFSettingKeys::formsHighlightFields()));
    flags.setFlag(PDFFormManager::HighlightRequiredFields, settings.getBool(PDFSettingKeys::formsHighlightRequiredFields()));
    flags.setFlag(PDFFormManager::EditReadOnlyFields, settings.getBool(PDFSettingKeys::formsAllowEditReadOnly()));
    return flags;
}

void PDFAppFormController::applySettings(const PDFSettingsRepository& settings)
{
    PDFWidgetFormManager* formManager = getFormManager();
    if (!formManager)
    {
        return;
    }

    // The read-only form of the annotation controller (a locked document, no
    // permission, an XFA form) is kept
    PDFFormManager::FormAppearanceFlags flags = getSettingsFlags(settings);
    const PDFAppAnnotationController* annotationController = getAnnotationController();
    flags.setFlag(PDFFormManager::ReadOnlyForm, annotationController && !annotationController->isFormFillingAllowed());
    if (formManager->getAppearanceFlags() == flags)
    {
        return;
    }

    // The editors are created again: an open editor is committed (one undo step)
    QPointer<PDFAppFormController> guard(this);
    formManager->setAppearanceFlags(flags);
    if (!guard)
    {
        return;
    }

    // The highlight of the fields is drawn with the page (not compiled into it)
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
    scheduleUpdate();
}

bool PDFAppFormController::isXFAForm(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    // The same rule as PDFForm::parse - an AcroForm with the key XFA is an XFA form
    const PDFDictionary* formDictionary = document->getDictionaryFromObject(document->getCatalog()->getFormObject());
    return formDictionary && !formDictionary->get("XFA").isNull();
}

// ---------------------------------------------------------------------- editors

PDFObjectReference PDFAppFormController::getFocusedWidget() const
{
    return m_formManager ? m_formManager->getFocusedWidget() : PDFObjectReference();
}

bool PDFAppFormController::hasPendingEdits() const
{
    const PDFWidgetFormManager* formManager = getFormManager();
    if (!formManager || !formManager->getDocument())
    {
        return false;
    }

    const PDFObjectReference widget = formManager->getFocusedWidget();
    return widget.isValid() && !formManager->isReadOnlyForm() && PDFAppFormControllerHelper::isPendingEditField(formManager->getFormFieldForWidget(widget));
}

bool PDFAppFormController::closeEditor()
{
    PDFWidgetFormManager* formManager = getFormManager();
    if (!formManager || !formManager->getFocusedWidget().isValid())
    {
        return false;
    }

    // The value is committed by the form manager (documentModified, the annotation
    // controller commits it): the slots may delete the tab and this controller
    QPointer<PDFAppFormController> guard(this);
    formManager->setFocusToEditor(nullptr);
    if (guard)
    {
        updatePendingEdits();
    }
    return true;
}

void PDFAppFormController::ensureFocusedFieldVisible()
{
    PDFWidgetFormManager* formManager = getFormManager();
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    QWidget* drawWidget = m_drawWidget.data();
    const PDFDocument* document = formManager ? formManager->getDocument() : nullptr;
    if (!formManager || !proxy || !drawWidget || !document)
    {
        return;
    }

    const PDFObjectReference widgetReference = formManager->getFocusedWidget();
    const PDFFormField* field = formManager->getFormFieldForWidget(widgetReference);
    if (!field)
    {
        return;
    }

    const PDFFormWidget* formWidget = nullptr;
    for (const PDFFormWidget& widget : field->getWidgets())
    {
        if (widget.getWidget() == widgetReference)
        {
            formWidget = &widget;
            break;
        }
    }
    if (!formWidget)
    {
        return;
    }

    const size_t pageIndex = PDFAppFormControllerHelper::findPageIndex(document, *formWidget);
    const QRectF rectangle = formManager->getWidgetRectangle(*formWidget);
    if (pageIndex == PDFCatalog::INVALID_PAGE_INDEX || !rectangle.isValid())
    {
        return;
    }

    // A page, which is not shown (another block of a single page layout, a page
    // far away), is shown first
    PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const PDFInteger page = PDFInteger(pageIndex);
    if (!snapshot.hasPage(page))
    {
        proxy->goToPageAndEnsureVisible(page, rectangle);
        snapshot = proxy->getSnapshot();
    }

    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(page);
    if (!item)
    {
        return;
    }

    // The whole field with a margin (a smaller one in a small window)
    const QRectF fieldRect = item->pageToDeviceMatrix.mapRect(rectangle);
    const QRectF displayedRect = QRectF(drawWidget->rect());
    const qreal margin = qMin(qreal(FIELD_VISIBLE_MARGIN), qMin(displayedRect.width(), displayedRect.height()) / 4.0);
    const QRectF visibleRect = displayedRect.adjusted(margin, margin, -margin, -margin);
    const qreal dx = PDFAppFormControllerHelper::getScrollDelta(fieldRect.left(), fieldRect.right(), visibleRect.left(), visibleRect.right());
    const qreal dy = PDFAppFormControllerHelper::getScrollDelta(fieldRect.top(), fieldRect.bottom(), visibleRect.top(), visibleRect.bottom());
    const QPoint offset(qRound(dx), qRound(dy));
    if (!offset.isNull())
    {
        proxy->scrollByPixels(offset);
    }
}

// ---------------------------------------------------------------------- events

bool PDFAppFormController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_drawWidget)
    {
        switch (event->type())
        {
            case QEvent::KeyPress:
                // Tab, Shift+Tab and the arrows of the buttons move the focus by the
                // keyboard: the field is scrolled into view by the update
                m_isKeyboardInput = true;
                scheduleUpdate();
                break;

            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            {
                // A press outside of the open editor closes it: the text tool takes
                // the presses on the page before the form manager, which would close
                // the editor itself (the filter sees the press before them)
                m_isKeyboardInput = false;
                const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
                PDFWidgetFormManager* formManager = getFormManager();
                if (formManager && formManager->getFocusedWidget().isValid() && !formManager->isEditorAt(m_drawWidget.data(), mouseEvent->position().toPoint()))
                {
                    QPointer<PDFAppFormController> guard(this);
                    closeEditor();
                    if (!guard)
                    {
                        return false;
                    }
                }
                scheduleUpdate();
                break;
            }

            case QEvent::KeyRelease:
            case QEvent::MouseButtonRelease:
                scheduleUpdate();
                break;

            case QEvent::FocusOut:
            {
                // The user works elsewhere in the window: the field is committed (a
                // popup, a menu or another window bring the user back to it)
                const QFocusEvent* focusEvent = static_cast<const QFocusEvent*>(event);
                if (PDFAppFormControllerHelper::isFocusLeavingCanvas(focusEvent->reason()))
                {
                    QPointer<PDFAppFormController> guard(this);
                    closeEditor();
                    if (!guard)
                    {
                        return false;
                    }
                }
                scheduleUpdate();
                break;
            }

            default:
                break;
        }
    }
    else if (watched == m_view && event->type() == QEvent::LanguageChange)
    {
        m_isInfoBarUpdateNeeded = true;
        scheduleUpdate();
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppFormController::scheduleUpdate()
{
    // One update after the event (the engine changes the focus of its editors while
    // it handles the event, which the filter sees before it)
    if (!m_isUpdateScheduled)
    {
        m_isUpdateScheduled = true;
        QMetaObject::invokeMethod(this, &PDFAppFormController::performUpdate, Qt::QueuedConnection);
    }
}

void PDFAppFormController::performUpdate()
{
    m_isUpdateScheduled = false;

    // A field focused by the keyboard is scrolled into view
    const PDFObjectReference focusedWidget = getFocusedWidget();
    const bool isFocusChangedByKeyboard = m_isKeyboardInput && focusedWidget.isValid() && focusedWidget != m_focusedWidget;
    m_focusedWidget = focusedWidget;
    m_isKeyboardInput = false;
    if (isFocusChangedByKeyboard)
    {
        ensureFocusedFieldVisible();
    }

    QPointer<PDFAppFormController> guard(this);
    updatePendingEdits();
    if (guard && m_isInfoBarUpdateNeeded)
    {
        updateInfoBars();
    }
}

void PDFAppFormController::updatePendingEdits()
{
    PDFAppDocument* document = m_document.data();
    if (!document || m_isUpdatingPendingEdits)
    {
        return;
    }

    const bool hasPendingEdits = this->hasPendingEdits();
    if (document->hasPendingEdits() == hasPendingEdits)
    {
        return;
    }

    // Save and Undo follow the pending edits: the window reads the commands again
    document->setPendingEdits(hasPendingEdits);
    if (PDFAppDocumentTab* tab = m_view ? m_view->getTab() : nullptr)
    {
        m_isUpdatingPendingEdits = true;
        QPointer<PDFAppFormController> guard(this);
        Q_EMIT tab->commandStatesChanged();
        if (guard)
        {
            m_isUpdatingPendingEdits = false;
        }
    }
}

void PDFAppFormController::updateInfoBars()
{
    m_isInfoBarUpdateNeeded = false;

    PDFAppDocumentView* view = m_view.data();
    if (!view)
    {
        return;
    }

    const PDFDocument* document = m_document && m_document->isReady() ? m_document->getDocument().data() : nullptr;
    const bool isXFA = isXFAForm(document);

    // An XFA form is shown, but not filled
    if (isXFA && !m_isXFABarDismissed)
    {
        PDFAppInfoBarRequest request;
        request.id = getXFABarId();
        request.tone = PDFAppInfoBarTone::Information;
        request.title = tr("This form cannot be filled in");
        // A dynamic form is shown with the pages of its layout (step 8): the shown
        // document is a display version, the file keeps the original PDF
        request.text = m_document->isPagedXFA() ? tr("PDF4QT shows the pages of this XFA form as laid out. The document cannot be edited; Save As keeps the original file.")
                                                : tr("PDF4QT shows XFA forms, but it does not fill them in.");
        const PDFAppInfoBarRequest* shown = view->findInfoBar(request.id);
        if (!shown || !(*shown == request))
        {
            view->showInfoBar(request);
        }
    }
    else if (view->findInfoBar(getXFABarId()))
    {
        view->hideInfoBar(getXFABarId());
    }

    // Fields calculated by scripts are unlocked (JavaScript is not run, O16) - only
    // a form, which can be filled; the bar says also, that scripts are not run, so
    // the general bar of the scripts is not needed beside it
    const PDFWidgetFormManager* formManager = getFormManager();
    const PDFAppAnnotationController* annotationController = getAnnotationController();
    const bool hasCalculatedFields = document && !isXFA && formManager && formManager->getDocument() == document &&
                                     annotationController && annotationController->isFormFillingAllowed() && formManager->hasUnlockedCalculatedFields();
    if (hasCalculatedFields && !m_isCalculatedBarDismissed)
    {
        PDFAppInfoBarRequest request;
        request.id = getCalculatedFieldsBarId();
        request.tone = PDFAppInfoBarTone::Information;
        request.title = tr("Calculated fields are not updated");
        request.text = tr("Scripts are not run, so the calculated fields are unlocked. Fill in their values yourself.");
        const PDFAppInfoBarRequest* shown = view->findInfoBar(request.id);
        if (!shown || !(*shown == request))
        {
            view->showInfoBar(request);
        }
        view->hideInfoBar(PDFAppDocumentActionHandler::getScriptsBarId());
    }
    else if (view->findInfoBar(getCalculatedFieldsBarId()))
    {
        view->hideInfoBar(getCalculatedFieldsBarId());
    }
}

// ---------------------------------------------------------------------- slots

void PDFAppFormController::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // A new form (open, reload) may have other calculated fields; the editors of
    // the form manager are created again by every reset
    if (modified.hasReset())
    {
        m_isInfoBarUpdateNeeded = true;
    }
    scheduleUpdate();
}

void PDFAppFormController::onDocumentStateChanged()
{
    m_isInfoBarUpdateNeeded = true;
    scheduleUpdate();
}

void PDFAppFormController::onEditingAllowedChanged()
{
    // A locked document has no unlocked calculated fields (the form is read only);
    // the fields of a read-only form are not highlighted
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
    m_isInfoBarUpdateNeeded = true;
    scheduleUpdate();
}

void PDFAppFormController::onPendingEditsCommitRequested()
{
    // Undo, redo, save, lock, close: the open editor is committed first
    closeEditor();
}

QString PDFAppFormController::getResetFormDescription()
{
    return tr("Reset form");
}

bool PDFAppFormController::isResetEffective(const PDFFormManager* formManager, const PDFActionResetForm* action)
{
    const PDFDocument* document = formManager ? formManager->getDocument() : nullptr;
    if (!document || !action || formManager->isReadOnlyForm())
    {
        return false;
    }

    // The fields of the scope as PDFFormManager::performResetAction takes them
    // (every field of the tree, the parents first)
    const PDFObjectStorage& storage = document->getStorage();
    PDFDocumentDataLoaderDecorator loader(document);
    const PDFFormAction::FieldList& fieldList = action->getFieldList();
    std::vector<const PDFFormField*> stack;
    for (const PDFFormFieldPointer& field : formManager->getForm()->getFormFields())
    {
        stack.push_back(field.data());
    }

    while (!stack.empty())
    {
        const PDFFormField* field = stack.back();
        stack.pop_back();
        if (!field)
        {
            continue;
        }
        for (const PDFFormFieldPointer& child : field->getChildFields())
        {
            stack.push_back(child.data());
        }

        const bool isInFieldList = std::find(fieldList.fieldReferences.cbegin(), fieldList.fieldReferences.cend(), field->getSelfReference()) != fieldList.fieldReferences.cend() ||
                                   fieldList.qualifiedNames.contains(field->getName(PDFFormField::NameType::FullyQualified));
        const bool accepted = action->getFieldScope() == PDFFormAction::FieldScope::All ||
                              (action->getFieldScope() == PDFFormAction::FieldScope::Include && isInFieldList) ||
                              (action->getFieldScope() == PDFFormAction::FieldScope::Exclude && !isInFieldList);
        if (!accepted)
        {
            continue;
        }

        const PDFObject& value = storage.getObject(field->getValue());
        const PDFObject& defaultValue = storage.getObject(field->getDefaultValue());
        switch (field->getFieldType())
        {
            case PDFFormField::FieldType::Button:
            {
                if (field->getFlags().testFlag(PDFFormField::PushButton))
                {
                    break;
                }
                if (value != defaultValue)
                {
                    return true;
                }

                // The appearance states, which the reset writes
                const QByteArray defaultState = loader.readString(field->getDefaultValue());
                for (const PDFFormWidget& widget : field->getWidgets())
                {
                    const QByteArray onState = PDFFormFieldButton::getOnAppearanceState(formManager, &widget);
                    const QByteArray state = defaultState == onState ? onState : PDFFormFieldButton::getOffAppearanceState(formManager, &widget);
                    const PDFDictionary* annotation = storage.getDictionaryFromObject(storage.getObjectByReference(widget.getWidget()));
                    if (!annotation || loader.readNameFromDictionary(annotation, "AS") != state)
                    {
                        return true;
                    }
                }
                break;
            }

            case PDFFormField::FieldType::Text:
            {
                if (value != defaultValue)
                {
                    return true;
                }
                break;
            }

            case PDFFormField::FieldType::Choice:
            {
                if (value != defaultValue)
                {
                    return true;
                }

                const PDFFormFieldChoice* choice = dynamic_cast<const PDFFormFieldChoice*>(field);
                if (choice && choice->isListBox())
                {
                    const PDFObject& selection = storage.getObject(choice->getSelection());
                    if (choice->getTopIndex() != 0 || (selection.isArray() ? selection.getArray()->getCount() > 0 : !selection.isNull()))
                    {
                        return true;
                    }
                }
                break;
            }

            default:
                // Other fields are not reset (PDFFormField::resetValue)
                break;
        }
    }
    return false;
}

void PDFAppFormController::onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId)
{
    // The close button of a bar (an empty action): it stays closed for the document
    if (!actionId.isEmpty())
    {
        return;
    }

    if (infoBarId == getCalculatedFieldsBarId())
    {
        m_isCalculatedBarDismissed = true;
    }
    else if (infoBarId == getXFABarId())
    {
        m_isXFABarDismissed = true;
    }
}

}   // namespace pdf
