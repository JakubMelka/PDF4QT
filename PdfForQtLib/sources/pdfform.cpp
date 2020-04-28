//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#include "pdfform.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>

namespace pdf
{

PDFForm PDFForm::parse(const PDFDocument* document, PDFObject object)
{
    PDFForm form;

    if (const PDFDictionary* formDictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);

        std::vector<PDFObjectReference> fieldRoots = loader.readReferenceArrayFromDictionary(formDictionary, "Fields");
        form.m_formFields.reserve(fieldRoots.size());
        for (const PDFObjectReference& fieldRootReference : fieldRoots)
        {
            form.m_formFields.emplace_back(PDFFormField::parse(&document->getStorage(), fieldRootReference, nullptr));
        }

        form.m_formType = FormType::AcroForm;
        form.m_needAppearances = loader.readBooleanFromDictionary(formDictionary, "NeedAppearances", false);
        form.m_signatureFlags = static_cast<SignatureFlags>(loader.readIntegerFromDictionary(formDictionary, "SigFlags", 0));
        form.m_calculationOrder = loader.readReferenceArrayFromDictionary(formDictionary, "CO");
        form.m_resources = formDictionary->get("DR");
        form.m_defaultAppearance = loader.readOptionalStringFromDictionary(formDictionary, "DA");
        form.m_quadding = loader.readOptionalIntegerFromDictionary(formDictionary, "Q");
        form.m_xfa = formDictionary->get("XFA");

        if (!form.m_xfa.isNull())
        {
            // Jakub Melka: handle XFA form
            form.m_formType = FormType::XFAForm;
        }

        // As post-processing, delete all form fields, which are nullptr (are incorrectly defined)
        form.m_formFields.erase(std::remove_if(form.m_formFields.begin(), form.m_formFields.end(), [](const auto& field){ return !field; }), form.m_formFields.end());
        form.updateWidgetToFormFieldMapping();

        // If we have form, then we must also look for 'rogue' form fields, which are
        // incorrectly not in the 'Fields' entry of this form. We do this by iterating
        // all pages, and their annotations and try to find these 'rogue' fields.
        bool rogueFieldFound = false;
        const size_t pageCount = document->getCatalog()->getPageCount();
        for (size_t i = 0; i < pageCount; ++i)
        {
            const PDFPage* page = document->getCatalog()->getPage(i);
            for (PDFObjectReference annotationReference : page->getAnnotations())
            {
                const PDFDictionary* annotationDictionary = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));

                if (form.m_widgetToFormField.count(annotationReference))
                {
                    // This widget/form field is already present
                    continue;
                }

                if (loader.readNameFromDictionary(annotationDictionary, "Subtype") == "Widget" &&
                    !annotationDictionary->hasKey("Kids"))
                {
                    rogueFieldFound = true;
                    form.m_formFields.emplace_back(PDFFormField::parse(&document->getStorage(), annotationReference, nullptr));
                }
            }
        }

        // As post-processing, delete all form fields, which are nullptr (are incorrectly defined)
        form.m_formFields.erase(std::remove_if(form.m_formFields.begin(), form.m_formFields.end(), [](const auto& field){ return !field; }), form.m_formFields.end());

        if (rogueFieldFound)
        {
            form.updateWidgetToFormFieldMapping();
        }
    }

    return form;
}

void PDFForm::updateWidgetToFormFieldMapping()
{
    m_widgetToFormField.clear();

    if (isAcroForm() || isXFAForm())
    {
        for (const PDFFormFieldPointer& formFieldPtr : getFormFields())
        {
            formFieldPtr->fillWidgetToFormFieldMapping(m_widgetToFormField);
        }
    }
}

const PDFFormField* PDFForm::getFormFieldForWidget(PDFObjectReference widget) const
{
    auto it = m_widgetToFormField.find(widget);
    if (it != m_widgetToFormField.cend())
    {
        return it->second;
    }

    return nullptr;
}

void PDFFormField::fillWidgetToFormFieldMapping(PDFWidgetToFormFieldMapping& mapping)
{
    for (const auto& childField : m_childFields)
    {
        childField->fillWidgetToFormFieldMapping(mapping);
    }

    for (const PDFFormWidget& formWidget : m_widgets)
    {
        mapping[formWidget.getWidget()] = formWidget.getParent();
    }
}

void PDFFormField::reloadValue(const PDFObjectStorage* storage, PDFObject parentValue)
{
    Q_ASSERT(storage);

    if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(getSelfReference())))
    {
        m_value = fieldDictionary->hasKey("V") ? fieldDictionary->get("V") : parentValue;
    }

    for (const PDFFormFieldPointer& childField : m_childFields)
    {
        childField->reloadValue(storage, m_value);
    }
}

void PDFFormField::apply(const std::function<void (const PDFFormField*)>& functor) const
{
    functor(this);

    for (const PDFFormFieldPointer& childField : m_childFields)
    {
        childField->apply(functor);
    }
}

PDFFormFieldPointer PDFFormField::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField)
{
    PDFFormFieldPointer result;

    if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference)))
    {
        PDFFormField* formField = nullptr;
        PDFFormFieldButton* formFieldButton = nullptr;
        PDFFormFieldText* formFieldText = nullptr;
        PDFFormFieldChoice* formFieldChoice = nullptr;
        PDFFormFieldSignature* formFieldSignature = nullptr;

        constexpr const std::array<std::pair<const char*, FieldType>, 4> fieldTypes = {
            std::pair<const char*, FieldType>{ "Btn", FieldType::Button },
            std::pair<const char*, FieldType>{ "Tx", FieldType::Text },
            std::pair<const char*, FieldType>{ "Ch", FieldType::Choice },
            std::pair<const char*, FieldType>{ "Sig", FieldType::Signature }
        };

        PDFDocumentDataLoaderDecorator loader(storage);
        FieldType formFieldType = parentField ? parentField->getFieldType() : FieldType::Invalid;
        if (fieldDictionary->hasKey("FT"))
        {
            formFieldType = loader.readEnumByName(fieldDictionary->get("FT"), fieldTypes.begin(), fieldTypes.end(), FieldType::Invalid);
        }

        switch (formFieldType)
        {
            case FieldType::Invalid:
                formField = new PDFFormField();
                break;

            case FieldType::Button:
                formFieldButton = new PDFFormFieldButton();
                formField = formFieldButton;
                break;

            case FieldType::Text:
                formFieldText = new PDFFormFieldText();
                formField = formFieldText;
                break;

            case FieldType::Choice:
                formFieldChoice = new PDFFormFieldChoice();
                formField = formFieldChoice;
                break;

            case FieldType::Signature:
                formFieldSignature = new PDFFormFieldSignature();
                formField = formFieldSignature;
                break;

            default:
                Q_ASSERT(false);
                break;
        }
        result.reset(formField);

        PDFObject parentV = parentField ? parentField->getValue() : PDFObject();
        PDFObject parentDV = parentField ? parentField->getDefaultValue() : PDFObject();
        FieldFlags parentFlags = parentField ? parentField->getFlags() : None;

        formField->m_selfReference = reference;
        formField->m_fieldType = formFieldType;
        formField->m_parentField = parentField;
        formField->m_fieldNames[Partial] = loader.readTextStringFromDictionary(fieldDictionary, "T", QString());
        formField->m_fieldNames[UserCaption] = loader.readTextStringFromDictionary(fieldDictionary, "TU", QString());
        formField->m_fieldNames[Export] = loader.readTextStringFromDictionary(fieldDictionary, "TM", QString());
        formField->m_fieldFlags = fieldDictionary->hasKey("Ff") ? static_cast<FieldFlags>(loader.readIntegerFromDictionary(fieldDictionary, "Ff", 0)) : parentFlags;
        formField->m_value = fieldDictionary->hasKey("V") ? fieldDictionary->get("V") : parentV;
        formField->m_defaultValue = fieldDictionary->hasKey("DV") ? fieldDictionary->get("DV") : parentDV;
        formField->m_additionalActions = PDFAnnotationAdditionalActions::parse(storage, fieldDictionary->get("AA"), fieldDictionary->get("A"));

        // Generate fully qualified name. If partial name is empty, then fully qualified name
        // is generated from parent fully qualified name (i.e. it is same as parent's name).
        // This is according the PDF specification 1.7.
        QStringList names;
        if (parentField)
        {
            names << parentField->getName(FullyQualified);
        }
        names << formField->m_fieldNames[Partial];
        names.removeAll(QString());

        formField->m_fieldNames[FullyQualified] = names.join(".");

        std::vector<PDFObjectReference> kids = loader.readReferenceArrayFromDictionary(fieldDictionary, "Kids");
        if (kids.empty())
        {
            // This means, that field's dictionary is merged with annotation's dictionary,
            // so, we will add pointer to self to the form widgets. But we must test, if we
            // really have merged annotation's dictionary - we test it by checking for 'Subtype'
            // presence.
            if (loader.readNameFromDictionary(fieldDictionary, "Subtype") == "Widget")
            {
                formField->m_widgets.emplace_back(PDFFormWidget::parse(storage, reference, formField));
            }
        }
        else
        {
            // Otherwise we must scan all references, and determine, if kid is another form field,
            // or it is a widget annotation. Widget annotations has required field 'Subtype', which
            // has value 'Widget', form field has required field 'Parent' (for non-root fields).

            for (const PDFObjectReference& kid : kids)
            {
                if (const PDFDictionary* childDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(kid)))
                {
                    const bool isWidget = loader.readNameFromDictionary(childDictionary, "Subtype") == "Widget";
                    const bool isField = loader.readReferenceFromDictionary(childDictionary, "Parent").isValid();

                    if (isField)
                    {
                        // This is form field (potentially merged with widget)
                        formField->m_childFields.emplace_back(PDFFormField::parse(storage, kid, formField));
                    }
                    else if (isWidget)
                    {
                        // This is pure widget (with no form field)
                        formField->m_widgets.emplace_back(PDFFormWidget::parse(storage, kid, formField));
                    }
                }
            }

        }

        if (formFieldButton)
        {
            formFieldButton->m_options = loader.readTextStringList(fieldDictionary->get("Opt"));
        }

        if (formFieldText)
        {
            PDFInteger maxLengthDefault = 0;

            if (PDFFormFieldText* parentTextField = dynamic_cast<PDFFormFieldText*>(parentField))
            {
                maxLengthDefault = parentTextField->getTextMaximalLength();
            }

            formFieldText->m_maxLength = loader.readIntegerFromDictionary(fieldDictionary, "MaxLen", maxLengthDefault);
        }

        if (formFieldChoice)
        {
            // Parse options - it is array of options values. Option value can be either a single text
            // string, or array of two values - export value and text to be displayed.
            PDFObject options = storage->getObject(fieldDictionary->get("Opt"));
            if (options.isArray())
            {
                const PDFArray* optionsArray = options.getArray();
                formFieldChoice->m_options.reserve(optionsArray->getCount());
                for (size_t i = 0; i < optionsArray->getCount(); ++i)
                {
                    PDFFormFieldChoice::Option option;

                    PDFObject optionItemObject = storage->getObject(optionsArray->getItem(i));
                    if (optionItemObject.isArray())
                    {
                        QStringList stringList = loader.readTextStringList(optionItemObject);
                        if (stringList.size() == 2)
                        {
                            option.exportString = stringList[0];
                            option.userString = stringList[1];
                        }
                    }
                    else
                    {
                        option.userString = loader.readTextString(optionItemObject, QString());
                        option.exportString = option.userString;
                    }

                    formFieldChoice->m_options.emplace_back(qMove(option));
                }
            }

            formFieldChoice->m_topIndex = loader.readIntegerFromDictionary(fieldDictionary, "TI", 0);
        }
    }

    return result;
}

PDFFormWidget::PDFFormWidget(PDFObjectReference widget, PDFFormField* parentField, PDFAnnotationAdditionalActions actions) :
    m_widget(widget),
    m_parentField(parentField),
    m_actions(qMove(actions))
{

}

PDFFormWidget PDFFormWidget::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField)
{
    PDFAnnotationAdditionalActions actions;
    if (const PDFDictionary* annotationDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference)))
    {
        actions = PDFAnnotationAdditionalActions::parse(storage, annotationDictionary->get("AA"), annotationDictionary->get("A"));
    }

    return PDFFormWidget(reference, parentField, qMove(actions));
}

PDFFormFieldButton::ButtonType PDFFormFieldButton::getButtonType() const
{
    if (m_fieldFlags.testFlag(PushButton))
    {
        return ButtonType::PushButton;
    }
    else if (m_fieldFlags.testFlag(Radio))
    {
        return ButtonType::RadioButton;
    }

    return ButtonType::CheckBox;
}

PDFFormManager::PDFFormManager(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(parent),
    m_proxy(proxy),
    m_annotationManager(nullptr),
    m_document(nullptr),
    m_flags(getDefaultApperanceFlags()),
    m_focusedEditor(nullptr)
{
    Q_ASSERT(proxy);
}

PDFFormManager::~PDFFormManager()
{

}

PDFAnnotationManager* PDFFormManager::getAnnotationManager() const
{
    return m_annotationManager;
}

void PDFFormManager::setAnnotationManager(PDFAnnotationManager* annotationManager)
{
    m_annotationManager = annotationManager;
}

const PDFDocument* PDFFormManager::getDocument() const
{
    return m_document;
}

void PDFFormManager::setDocument(const PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        m_document = document;

        if (document.hasReset())
        {
            if (m_document)
            {
                m_form = PDFForm::parse(m_document, m_document->getCatalog()->getFormObject());
            }
            else
            {
                // Clean the form
                m_form = PDFForm();
            }

            updateFormWidgetEditors();
        }
        else if (document.hasFlag(PDFModifiedDocument::FormField))
        {
            // Just update field values
            updateFieldValues();
        }
    }
}

PDFFormManager::FormAppearanceFlags PDFFormManager::getAppearanceFlags() const
{
    return m_flags;
}

void PDFFormManager::setAppearanceFlags(FormAppearanceFlags flags)
{
    m_flags = flags;
}

bool PDFFormManager::hasFormFieldWidgetText(PDFObjectReference widgetAnnotation) const
{
    if (const PDFFormField* formField = getFormFieldForWidget(widgetAnnotation))
    {
        switch (formField->getFieldType())
        {
            case PDFFormField::FieldType::Text:
                return true;

            case PDFFormField::FieldType::Choice:
            {
                PDFFormField::FieldFlags flags = formField->getFlags();
                return flags.testFlag(PDFFormField::Combo) && flags.testFlag(PDFFormField::Edit);
            }

            default:
                break;
        }
    }

    return false;
}

PDFFormWidgets PDFFormManager::getWidgets() const
{
    PDFFormWidgets result;

    auto functor = [&result](const PDFFormField* formField)
    {
        const PDFFormWidgets& widgets = formField->getWidgets();
        result.insert(result.cend(), widgets.cbegin(), widgets.cend());
    };
    apply(functor);

    return result;
}

void PDFFormManager::apply(const std::function<void (const PDFFormField*)>& functor) const
{
    for (const PDFFormFieldPointer& childField : m_form.getFormFields())
    {
        childField->apply(functor);
    }
}

void PDFFormManager::setFocusToEditor(PDFFormFieldWidgetEditor* editor)
{
    if (m_focusedEditor != editor)
    {
        if (m_focusedEditor)
        {
            m_focusedEditor->setFocus(false);
        }

        m_focusedEditor = editor;

        if (m_focusedEditor)
        {
            m_focusedEditor->setFocus(true);
        }

        // Request repaint, because focus changed
        Q_ASSERT(m_proxy);
        m_proxy->repaintNeeded();
    }
}

bool PDFFormManager::focusNextPrevFormField(bool next)
{
    if (m_widgetEditors.empty())
    {
        return false;
    }

    std::vector<PDFFormFieldWidgetEditor*>::const_iterator newFocusIterator = m_widgetEditors.cend();

    if (!m_focusedEditor)
    {
        // We are setting a new focus
        if (next)
        {
            newFocusIterator = m_widgetEditors.cbegin();
        }
        else
        {
            newFocusIterator = std::prev(m_widgetEditors.cend());
        }
    }
    else
    {
        std::vector<PDFFormFieldWidgetEditor*>::const_iterator it = std::find(m_widgetEditors.cbegin(), m_widgetEditors.cend(), m_focusedEditor);
        Q_ASSERT(it != m_widgetEditors.cend());

        if (next)
        {
            newFocusIterator = std::next(it);
        }
        else if (it != m_widgetEditors.cbegin())
        {
            newFocusIterator = std::prev(it);
        }
    }

    if (newFocusIterator != m_widgetEditors.cend())
    {
        setFocusToEditor(*newFocusIterator);
        return true;
    }
    else
    {
        // Jakub Melka: We must remove focus out of editor, because
        setFocusToEditor(nullptr);
    }

    return false;
}

bool PDFFormManager::isFocused(PDFObjectReference widget) const
{
    if (m_focusedEditor)
    {
        return m_focusedEditor->getWidgetAnnotation() == widget;
    }

    return false;
}

const PDFAction* PDFFormManager::getAction(PDFAnnotationAdditionalActions::Action actionType, const PDFFormWidget* widget)
{
    if (const PDFAction* action = widget->getAction(actionType))
    {
        return action;
    }

    for (const PDFFormField* formField = widget->getParent(); formField; formField = formField->getParentField())
    {
        if (const PDFAction* action = formField->getAction(actionType))
        {
            return action;
        }
    }

    return nullptr;
}

void PDFFormManager::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    if (m_focusedEditor)
    {
        m_focusedEditor->keyPressEvent(widget, event);
    }
}

void PDFFormManager::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    if (m_focusedEditor)
    {
        m_focusedEditor->keyReleaseEvent(widget, event);
    }
}

void PDFFormManager::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormManager::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

const std::optional<QCursor>& PDFFormManager::getCursor() const
{
    static const std::optional<QCursor> dummy;
    return dummy;
}

void PDFFormManager::updateFormWidgetEditors()
{
    setFocusToEditor(nullptr);
    qDeleteAll(m_widgetEditors);
    m_widgetEditors.clear();

    for (PDFFormWidget widget : getWidgets())
    {
        const PDFFormField* formField = widget.getParent();
        switch (formField->getFieldType())
        {
            case PDFFormField::FieldType::Button:
            {
                Q_ASSERT(dynamic_cast<const PDFFormFieldButton*>(formField));
                const PDFFormFieldButton* formFieldButton = static_cast<const PDFFormFieldButton*>(formField);
                switch (formFieldButton->getButtonType())
                {
                    case PDFFormFieldButton::ButtonType::PushButton:
                    {
                        m_widgetEditors.push_back(new PDFFormFieldPushButtonEditor(this, widget, this));
                        break;
                    }

                    case PDFFormFieldButton::ButtonType::RadioButton:
                    case PDFFormFieldButton::ButtonType::CheckBox:
                    {
                        m_widgetEditors.push_back(new PDFFormFieldCheckableButtonEditor(this, widget, this));
                        break;
                    }

                    default:
                        Q_ASSERT(false);
                        break;
                }

                break;
            }

            case PDFFormField::FieldType::Text:
            {
                m_widgetEditors.push_back(new PDFFormFieldTextBoxEditor(this, widget, this));
                break;
            }

            case PDFFormField::FieldType::Choice:
            {
                Q_ASSERT(dynamic_cast<const PDFFormFieldChoice*>(formField));
                const PDFFormFieldChoice* formFieldChoice = static_cast<const PDFFormFieldChoice*>(formField);
                if (formFieldChoice->isComboBox())
                {
                    m_widgetEditors.push_back(new PDFFormFieldComboBoxEditor(this, widget, this));
                }
                else if (formFieldChoice->isListBox())
                {
                    m_widgetEditors.push_back(new PDFFormFieldListBoxEditor(this, widget, this));
                }
                else
                {
                    // Uknown field choice
                    Q_ASSERT(false);
                }

                break;
            }

            case PDFFormField::FieldType::Signature:
                // Signature fields doesn't have editor
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }
}

void PDFFormManager::updateFieldValues()
{
    if (m_document)
    {
        for (const PDFFormFieldPointer& childField : m_form.getFormFields())
        {
            childField->reloadValue(&m_document->getStorage(), PDFObject());
        }
    }
}

PDFFormFieldWidgetEditor::PDFFormFieldWidgetEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(parent),
    m_formManager(formManager),
    m_formWidget(formWidget),
    m_hasFocus(false)
{
    Q_ASSERT(m_formManager);
}

void PDFFormFieldWidgetEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::setFocus(bool hasFocus)
{
    m_hasFocus = hasFocus;
}

void PDFFormFieldWidgetEditor::performKeypadNavigation(QWidget* widget, QKeyEvent* event)
{
    int key = event->key();

    const bool isLeft = key == Qt::Key_Left;
    const bool isRight = key == Qt::Key_Right;
    const bool isUp = key == Qt::Key_Up;
    const bool isDown = key == Qt::Key_Down;
    const bool isHorizontal = isLeft || isRight;

    Qt::NavigationMode navigationMode = Qt::NavigationModeKeypadDirectional;
#ifdef QT_KEYPAD_NAVIGATION
    navigationMode = QApplication::navigationMode();
#endif

    switch (navigationMode)
    {
        case Qt::NavigationModeKeypadTabOrder:
        {
            // According the Qt's documentation, Up/Down arrows are used
            // to change focus. So, if user pressed Left/Right, we must
            // ignore this event.
            if (isHorizontal)
            {
                return;
            }
            break;
        }

        case Qt::NavigationModeKeypadDirectional:
            // Default behaviour
            break;

        default:
            // Nothing happens
            return;
    }

    bool next = false;
    if (isHorizontal)
    {
        switch (widget->layoutDirection())
        {
            case Qt::LeftToRight:
            case Qt::LayoutDirectionAuto:
                next = isRight;
                break;

            case Qt::RightToLeft:
                next = isLeft;
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }
    else
    {
        // Vertical
        next = isDown;
    }

    m_formManager->focusNextPrevFormField(next);
}

PDFFormFieldPushButtonEditor::PDFFormFieldPushButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(formManager, formWidget, parent)
{

}

void PDFFormFieldPushButtonEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        {
            click();
            event->accept();
            break;
        }

        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            performKeypadNavigation(widget, event);
            break;
        }

        default:
            break;
    }
}

void PDFFormFieldPushButtonEditor::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    switch (event->key())
    {
        case Qt::Key_Select:
        case Qt::Key_Space:
        {
            click();
            event->accept();
            break;
        }

        default:
            break;
    }
}

void PDFFormFieldPushButtonEditor::click()
{
    if (const PDFAction* action = m_formManager->getAction(PDFAnnotationAdditionalActions::MousePressed, getFormWidget()))
    {
        emit m_formManager->actionTriggered(action);
    }
    else if (const PDFAction* action = m_formManager->getAction(PDFAnnotationAdditionalActions::Default, getFormWidget()))
    {
        emit m_formManager->actionTriggered(action);
    }
}

PDFFormFieldCheckableButtonEditor::PDFFormFieldCheckableButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(formManager, formWidget, parent)
{

}

PDFFormFieldComboBoxEditor::PDFFormFieldComboBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(formManager, formWidget, parent)
{

}

PDFFormFieldListBoxEditor::PDFFormFieldListBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(formManager, formWidget, parent)
{

}

PDFFormFieldTextBoxEditor::PDFFormFieldTextBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(formManager, formWidget, parent)
{

}

}   // namespace pdf
