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
#include "pdfdrawwidget.h"
#include "pdfdocumentbuilder.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>

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

PDFFormField* PDFForm::getFormFieldForWidget(PDFObjectReference widget)
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

void PDFFormField::modify(const std::function<void (PDFFormField*)>& functor)
{
    functor(this);

    for (const PDFFormFieldPointer& childField : m_childFields)
    {
        childField->modify(functor);
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
            QByteArray defaultAppearance;
            Qt::Alignment alignment = 0;

            if (PDFFormFieldText* parentTextField = dynamic_cast<PDFFormFieldText*>(parentField))
            {
                maxLengthDefault = parentTextField->getTextMaximalLength();
                defaultAppearance = parentTextField->getDefaultAppearance();
                alignment = parentTextField->getAlignment();
            }

            if (fieldDictionary->hasKey("DA"))
            {
                defaultAppearance = loader.readStringFromDictionary(fieldDictionary, "DA");
            }
            if (fieldDictionary->hasKey("Q"))
            {
                const PDFInteger quadding = loader.readIntegerFromDictionary(fieldDictionary, "Q", -1);
                switch (quadding)
                {
                    case 0:
                        alignment = Qt::AlignLeft;
                        break;

                    case 1:
                        alignment = Qt::AlignHCenter;
                        break;

                    case 2:
                        alignment = Qt::AlignRight;
                        break;

                    default:
                        break;
                }
            }
            alignment |= Qt::AlignVCenter;

            formFieldText->m_maxLength = loader.readIntegerFromDictionary(fieldDictionary, "MaxLen", maxLengthDefault);
            formFieldText->m_defaultAppearance = defaultAppearance;
            formFieldText->m_alignment = alignment;
            formFieldText->m_defaultStyle = loader.readTextStringFromDictionary(fieldDictionary, "DS", QString());
            formFieldText->m_richTextValue = loader.readTextStringFromDictionary(fieldDictionary, "RV", QString());
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

bool PDFFormField::setValue(const SetValueParameters& parameters)
{
    Q_UNUSED(parameters);

    // Default behaviour: return false, value cannot be set
    return false;
}

PDFFormWidget::PDFFormWidget(PDFObjectReference page, PDFObjectReference widget, PDFFormField* parentField, PDFAnnotationAdditionalActions actions) :
    m_page(page),
    m_widget(widget),
    m_parentField(parentField),
    m_actions(qMove(actions))
{

}

PDFFormWidget PDFFormWidget::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField)
{
    PDFObjectReference pageReference;
    PDFAnnotationAdditionalActions actions;
    if (const PDFDictionary* annotationDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference)))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        pageReference = loader.readReferenceFromDictionary(annotationDictionary, "P");
        actions = PDFAnnotationAdditionalActions::parse(storage, annotationDictionary->get("AA"), annotationDictionary->get("A"));
    }

    return PDFFormWidget(pageReference, reference, parentField, qMove(actions));
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

QByteArray PDFFormFieldButton::getOnAppearanceState(const PDFFormManager* formManager, const PDFFormWidget* widget)
{
    Q_ASSERT(formManager);
    Q_ASSERT(widget);

    const PDFDocument* document = formManager->getDocument();
    Q_ASSERT(document);

    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(widget->getWidget())))
    {
        PDFAppeareanceStreams streams = PDFAppeareanceStreams::parse(&document->getStorage(), dictionary->get("AP"));
        QByteArrayList states = streams.getAppearanceStates(PDFAppeareanceStreams::Appearance::Normal);

        for (const QByteArray& state : states)
        {
            if (!state.isEmpty() && state != "Off")
            {
                return state;
            }
        }
    }

    return QByteArray();
}

QByteArray PDFFormFieldButton::getOffAppearanceState(const PDFFormManager* formManager, const PDFFormWidget* widget)
{
    Q_UNUSED(formManager);
    Q_UNUSED(widget);

    // 'Off' value is specified by PDF 1.7 specification. It has always value 'Off'.
    // 'On' values can have different appearance states.
    return "Off";
}

bool PDFFormFieldButton::setValue(const SetValueParameters& parameters)
{
    // Do not allow to set value to push buttons
    if (getFlags().testFlag(PushButton))
    {
        return false;
    }

    // If form field is readonly, and scope is user (form field is changed by user,
    // not by calculated value), then we must not allow value change.
    if (getFlags().testFlag(ReadOnly) && parameters.scope == SetValueParameters::Scope::User)
    {
        return false;
    }

    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);
    Q_ASSERT(parameters.value.isName());

    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    QByteArray state = parameters.value.getString();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), parameters.value);

    // Change widget appearance states
    const bool isRadio = getFlags().testFlag(Radio);
    const bool isRadioInUnison = getFlags().testFlag(RadiosInUnison);
    const bool isSameValueForAllWidgets = !isRadio || isRadioInUnison;
    const bool isAllowedToCheckAllOff = !getFlags().testFlag(NoToggleToOff);
    bool hasWidgets = !m_widgets.empty();
    bool isAnyWidgetToggledOn = false;

    for (const PDFFormWidget& formWidget : getWidgets())
    {
        QByteArray onState = PDFFormFieldButton::getOnAppearanceState(parameters.formManager, &formWidget);

        // We set appearance to 'On' if following two conditions both hold:
        //  1) State equals to widget's "On" state
        //  2) Either we are setting value to invoking widget, or setting of same
        //     value to other widgets is allowed (it is a checkbox, or radio in unison)
        if (state == onState && (isSameValueForAllWidgets || formWidget.getWidget() == parameters.invokingWidget))
        {
            isAnyWidgetToggledOn = true;
            builder->setAnnotationAppearanceState(formWidget.getWidget(), onState);
        }
        else
        {
            QByteArray offState = PDFFormFieldButton::getOffAppearanceState(parameters.formManager, &formWidget);
            builder->setAnnotationAppearanceState(formWidget.getWidget(), offState);
        }
        parameters.modifier->markAnnotationsChanged();
    }

    // We must check, if correct value has been set. If form field has no widgets,
    // but has same qualified name, then no check is performed (just form field value is set
    // to same value in all form fields with same qualified name, according to the PDF specification.
    if (hasWidgets && !isAnyWidgetToggledOn && !isAllowedToCheckAllOff)
    {
        return false;
    }

    return true;
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

void PDFFormManager::modify(const std::function<void (PDFFormField*)>& functor) const
{
    for (const PDFFormFieldPointer& childField : m_form.getFormFields())
    {
        childField->modify(functor);
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

void PDFFormManager::setFormFieldValue(PDFFormField::SetValueParameters parameters)
{
    Q_ASSERT(parameters.invokingFormField);
    Q_ASSERT(parameters.invokingWidget.isValid());

    parameters.formManager = this;
    parameters.scope = PDFFormField::SetValueParameters::Scope::User;

    PDFDocumentModifier modifier(m_document);
    parameters.modifier = &modifier;

    if (parameters.invokingFormField->setValue(parameters))
    {
        // We must also set dependent fields with same name
        QString qualifiedFormFieldName = parameters.invokingFormField->getName(PDFFormField::NameType::FullyQualified);
        if (!qualifiedFormFieldName.isEmpty())
        {
            parameters.scope = PDFFormField::SetValueParameters::Scope::Internal;
            auto updateDependentField = [&parameters, &qualifiedFormFieldName](PDFFormField* formField)
            {
                if (parameters.invokingFormField == formField)
                {
                    // Do not update self
                    return;
                }

                if (qualifiedFormFieldName == formField->getName(PDFFormField::NameType::FullyQualified))
                {
                    formField->setValue(parameters);
                }
            };
            modify(updateDependentField);
        }

        if (modifier.finalize())
        {
            emit documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

QRectF PDFFormManager::getWidgetRectangle(const PDFFormWidget& widget) const
{
    if (const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(widget.getWidget())))
    {
        PDFDocumentDataLoaderDecorator loader(m_document);
        return loader.readRectangle(dictionary->get("Rect"), QRectF());
    }

    return QRectF();
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
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);

        // We try to set focus on editor
        if (event->button() == Qt::LeftButton)
        {
            setFocusToEditor(info.editor);
        }

        info.editor->mousePressEvent(widget, event);
        grabMouse(info, event);
    }
    else if (!isMouseGrabbed())
    {
        // Mouse is not grabbed, user clicked elsewhere, unfocus editor
        setFocusToEditor(nullptr);
    }
}

void PDFFormManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);
        info.editor->mouseReleaseEvent(widget, event);
        ungrabMouse(info, event);
    }
}

void PDFFormManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);
        info.editor->mouseMoveEvent(widget, event);

        // If mouse is grabbed, then event is accepted always (because
        // we get Press event, when we grabbed the mouse, then we will
        // wait for corresponding release event while all mouse move events
        // will be accepted, even if editor doesn't accept them.
        if (isMouseGrabbed())
        {
            event->accept();
        }
    }
}

void PDFFormManager::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);

    // We will accept mouse wheel events, if we are grabbing the mouse.
    // We do not want to zoom in/zoom out while grabbing.
    if (isMouseGrabbed())
    {
        event->accept();
    }
}

void PDFFormManager::grabMouse(const MouseEventInfo& info, QMouseEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // Double clicks doesn't grab the mouse
        return;
    }

    Q_ASSERT(event->type() == QEvent::MouseButtonPress);

    if (isMouseGrabbed())
    {
        // If mouse is already grabbed, then when new mouse button is pressed,
        // we just increase nesting level and accept the mouse event. We are
        // accepting all mouse events, if mouse is grabbed.
        ++m_mouseGrabInfo.mouseGrabNesting;
        event->accept();
    }
    else if (event->isAccepted())
    {
        // Event is accepted and we are not grabbing the mouse. We must start
        // grabbing the mouse.
        Q_ASSERT(m_mouseGrabInfo.mouseGrabNesting == 0);
        ++m_mouseGrabInfo.mouseGrabNesting;
        m_mouseGrabInfo.info = info;
    }
}

void PDFFormManager::ungrabMouse(const MouseEventInfo& info, QMouseEvent* event)
{
    Q_UNUSED(info);
    Q_ASSERT(event->type() == QEvent::MouseButtonRelease);

    if (isMouseGrabbed())
    {
        // Mouse is being grabbed, decrease nesting level. We must also accept
        // mouse release event, because mouse is being grabbed.
        --m_mouseGrabInfo.mouseGrabNesting;
        event->accept();

        if (!isMouseGrabbed())
        {
            m_mouseGrabInfo.info = MouseEventInfo();
        }
    }

    Q_ASSERT(m_mouseGrabInfo.mouseGrabNesting >= 0);
}

PDFFormManager::MouseEventInfo PDFFormManager::getMouseEventInfo(QWidget* widget, QPoint point)
{
    MouseEventInfo result;

    if (isMouseGrabbed())
    {
        result = m_mouseGrabInfo.info;
        result.mousePosition = result.deviceToWidget.map(point);
        return result;
    }

    std::vector<PDFInteger> currentPages = m_proxy->getWidget()->getDrawWidget()->getCurrentPages();

    if (!m_annotationManager->hasAnyPageAnnotation(currentPages))
    {
        // All pages doesn't have annotation
        return result;
    }

    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        const PDFAnnotationManager::PageAnnotations& pageAnnotations = m_annotationManager->getPageAnnotations(snapshotItem.pageIndex);
        for (const PDFAnnotationManager::PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (pageAnnotation.annotation->isReplyTo())
            {
                // Annotation is reply to another annotation, do not interact with it
                continue;
            }

            if (pageAnnotation.annotation->getType() != AnnotationType::Widget)
            {
                // Annotation is not widget annotation (form field), do not interact with it
                continue;
            }

            QRectF annotationRect = pageAnnotation.annotation->getRectangle();
            QMatrix widgetToDevice = m_annotationManager->prepareTransformations(snapshotItem.pageToDeviceMatrix, widget, pageAnnotation.annotation->getEffectiveFlags(), m_document->getCatalog()->getPage(snapshotItem.pageIndex), annotationRect);

            QPainterPath path;
            path.addRect(annotationRect);
            path = widgetToDevice.map(path);

            if (path.contains(point))
            {
                if (PDFFormField* formField = getFormFieldForWidget(pageAnnotation.annotation->getSelfReference()))
                {
                    result.formField = formField;
                    result.deviceToWidget = widgetToDevice.inverted();
                    result.mousePosition = result.deviceToWidget.map(point);
                    result.editor = getEditor(formField);
                    return result;
                }
            }
        }
    }

    return result;
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

PDFFormFieldWidgetEditor* PDFFormManager::getEditor(const PDFFormField* formField) const
{
    for (PDFFormFieldWidgetEditor* editor : m_widgetEditors)
    {
        if (editor->getFormField() == formField)
        {
            return editor;
        }
    }

    return nullptr;
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

PDFFormFieldAbstractButtonEditor::PDFFormFieldAbstractButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent) :
    BaseClass(formManager, formWidget, parent)
{

}

void PDFFormFieldAbstractButtonEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
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

void PDFFormFieldAbstractButtonEditor::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
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

void PDFFormFieldAbstractButtonEditor::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        click();
        event->accept();
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

void PDFFormFieldCheckableButtonEditor::click()
{
    QByteArray newState;

    // First, check current state of form field
    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());
    QByteArray state = loader.readName(m_formWidget.getParent()->getValue());
    QByteArray onState = PDFFormFieldButton::getOnAppearanceState(m_formManager, &m_formWidget);
    if (state != onState)
    {
        newState = onState;
    }
    else
    {
        newState = PDFFormFieldButton::getOffAppearanceState(m_formManager, &m_formWidget);
    }

    // We have a new state, try to apply it to form field
    PDFFormField::SetValueParameters parameters;
    parameters.formManager = m_formManager;
    parameters.invokingWidget = m_formWidget.getWidget();
    parameters.invokingFormField = m_formWidget.getParent();
    parameters.scope = PDFFormField::SetValueParameters::Scope::User;
    parameters.value = PDFObject::createName(std::make_shared<PDFString>(qMove(newState)));
    m_formManager->setFormFieldValue(parameters);
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
    BaseClass(formManager, formWidget, parent),
    m_textEdit(formWidget.getParent()->getFlags())
{
    const PDFFormFieldText* parentField = dynamic_cast<const PDFFormFieldText*>(formWidget.getParent());
    Q_ASSERT(parentField);

    PDFDocumentDataLoaderDecorator loader(formManager->getDocument());

    QByteArray defaultAppearance = parentField->getDefaultAppearance();
    if (defaultAppearance.isEmpty())
    {
        defaultAppearance = formManager->getForm()->getDefaultAppearance().value_or(QByteArray());
    }
    Qt::Alignment alignment = parentField->getAlignment();
    if (!(alignment & Qt::AlignHorizontal_Mask))
    {
        switch (formManager->getForm()->getQuadding().value_or(0))
        {
            default:
            case 0:
                alignment |= Qt::AlignLeft;
                break;

            case 1:
                alignment |= Qt::AlignHCenter;
                break;

            case 2:
                alignment |= Qt::AlignRight;
                break;
        }
    }

    // Initialize text edit
    m_textEdit.setAppearance(PDFAnnotationDefaultAppearance::parse(defaultAppearance), alignment, m_formManager->getWidgetRectangle(formWidget), parentField->getTextMaximalLength());
    m_textEdit.setText(loader.readTextString(parentField->getValue(), QString()));
}

PDFTextEditPseudowidget::PDFTextEditPseudowidget(PDFFormField::FieldFlags flags) :
    m_flags(flags),
    m_selectionStart(0),
    m_selectionEnd(0),
    m_positionCursor(0),
    m_maxTextLength(0)
{
    m_textLayout.setCacheEnabled(true);
    m_passwordReplacementCharacter = QApplication::style()->styleHint(QStyle::SH_LineEdit_PasswordCharacter);
}

void PDFTextEditPseudowidget::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    /*
       We will support following key sequences:
            Delete
            Cut,
            Copy,
            Paste,
            SelectAll,
            MoveToNextChar,
            MoveToPreviousChar,
            MoveToNextWord,
            MoveToPreviousWord,
            MoveToNextLine,
            MoveToPreviousLine,
            MoveToStartOfLine,
            MoveToEndOfLine,
            MoveToStartOfBlock,
            MoveToEndOfBlock,
            MoveToStartOfDocument,
            MoveToEndOfDocument,
            SelectNextChar,
            SelectPreviousChar,
            SelectNextWord,
            SelectPreviousWord,
            SelectNextLine,
            SelectPreviousLine,
            SelectStartOfLine,
            SelectEndOfLine,
            SelectStartOfBlock,
            SelectEndOfBlock,
            SelectStartOfDocument,
            SelectEndOfDocument,
            DeleteStartOfWord,
            DeleteEndOfWord,
            DeleteEndOfLine,
            Deselect,
            DeleteCompleteLine,
            Backspace,
     * */

    event->accept();

    if (event == QKeySequence::Delete)
    {
        performDelete();
    }
    else if (event == QKeySequence::Cut)
    {
        performCut();
    }
    else if (event == QKeySequence::Copy)
    {
        performCopy();
    }
    else if (event == QKeySequence::Paste)
    {
        performPaste();
    }
    else if (event == QKeySequence::SelectAll)
    {
        setSelection(0, getTextLength());
    }
    else if (event == QKeySequence::MoveToNextChar)
    {
        setCursorPosition(getCursorCharacterForward(), false);
    }
    else if (event == QKeySequence::MoveToPreviousChar)
    {
        setCursorPosition(getCursorCharacterBackward(), false);
    }
    else if (event == QKeySequence::MoveToNextWord)
    {
        setCursorPosition(getCursorWordForward(), false);
    }
    else if (event == QKeySequence::MoveToPreviousWord)
    {
        setCursorPosition(getCursorWordBackward(), false);
    }
    else if (event == QKeySequence::MoveToNextLine)
    {
        setCursorPosition(getCursorNextLine(), false);
    }
    else if (event == QKeySequence::MoveToPreviousLine)
    {
        setCursorPosition(getCursorPreviousLine(), false);
    }
    else if (event == QKeySequence::MoveToStartOfLine || event == QKeySequence::MoveToStartOfBlock)
    {
        setCursorPosition(getCursorLineStart(), false);
    }
    else if (event == QKeySequence::MoveToEndOfLine || event == QKeySequence::MoveToEndOfBlock)
    {
        setCursorPosition(getCursorLineEnd(), false);
    }
    else if (event == QKeySequence::MoveToStartOfDocument)
    {
        setCursorPosition(getCursorDocumentStart(), false);
    }
    else if (event == QKeySequence::MoveToEndOfDocument)
    {
        setCursorPosition(getCursorDocumentEnd(), false);
    }
    else if (event == QKeySequence::SelectNextChar)
    {
        setCursorPosition(getCursorCharacterForward(), true);
    }
    else if (event == QKeySequence::SelectPreviousChar)
    {
        setCursorPosition(getCursorCharacterBackward(), true);
    }
    else if (event == QKeySequence::SelectNextWord)
    {
        setCursorPosition(getCursorWordForward(), true);
    }
    else if (event == QKeySequence::SelectPreviousWord)
    {
        setCursorPosition(getCursorWordBackward(), true);
    }
    else if (event == QKeySequence::SelectNextLine)
    {
        setCursorPosition(getCursorNextLine(), true);
    }
    else if (event == QKeySequence::SelectPreviousLine)
    {
        setCursorPosition(getCursorPreviousLine(), true);
    }
    else if (event == QKeySequence::SelectStartOfLine || event == QKeySequence::SelectStartOfBlock)
    {
        setCursorPosition(getCursorLineStart(), true);
    }
    else if (event == QKeySequence::SelectEndOfLine || event == QKeySequence::SelectEndOfBlock)
    {
        setCursorPosition(getCursorLineEnd(), true);
    }
    else if (event == QKeySequence::SelectStartOfDocument)
    {
        setCursorPosition(getCursorDocumentStart(), true);
    }
    else if (event == QKeySequence::SelectEndOfDocument)
    {
        setCursorPosition(getCursorDocumentEnd(), true);
    }
    else if (event == QKeySequence::DeleteStartOfWord)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorWordBackward(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::DeleteEndOfWord)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorWordForward(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::DeleteEndOfLine)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorLineEnd(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::Deselect)
    {
        clearSelection();
    }
    else if (event == QKeySequence::DeleteCompleteLine)
    {
        if (!isReadonly())
        {
            m_selectionStart = getCurrentLineTextStart();
            m_selectionEnd = getCurrentLineTextEnd();
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::Backspace)
    {
        performBackspace();
    }
    else if (event->key() == Qt::Key_Direction_L)
    {
        QTextOption option = m_textLayout.textOption();
        option.setTextDirection(Qt::LeftToRight);
        m_textLayout.setTextOption(qMove(option));
        updateTextLayout();
    }
    else if (event->key() == Qt::Key_Direction_R)
    {
        QTextOption option = m_textLayout.textOption();
        option.setTextDirection(Qt::LeftToRight);
        m_textLayout.setTextOption(qMove(option));
        updateTextLayout();
    }
    else if (event->key() == Qt::Key_Up)
    {
        setCursorPosition(getCursorLineUp(), event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Down)
    {
        setCursorPosition(getCursorLineDown(), event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Left)
    {
        const int position = (event->modifiers().testFlag(Qt::ControlModifier)) ? getCursorWordBackward() : getCursorCharacterBackward();
        setCursorPosition(position, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Right)
    {
        const int position = (event->modifiers().testFlag(Qt::ControlModifier)) ? getCursorWordForward() : getCursorCharacterForward();
        setCursorPosition(position, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else
    {
        QString text = event->text();
        if (!text.isEmpty())
        {
            performInsertText(text);
        }
        else
        {
            event->ignore();
        }
    }
}

void PDFTextEditPseudowidget::setSelection(int startPosition, int selectionLength)
{
    if (selectionLength > 0)
    {
        // We are selecting to the right
        m_selectionStart = startPosition;
        m_selectionEnd = qMin(startPosition + selectionLength, getTextLength());
        m_positionCursor = m_selectionEnd;
    }
    else if (selectionLength < 0)
    {
        // We are selecting to the left
        m_selectionStart = qMax(startPosition + selectionLength, 0);
        m_selectionEnd = startPosition;
        m_positionCursor = m_selectionStart;
    }
    else
    {
        // Clear the selection
        m_selectionStart = 0;
        m_selectionEnd = 0;
        m_positionCursor = startPosition;
    }
}

void PDFTextEditPseudowidget::setCursorPosition(int position, bool select)
{
    if (select)
    {
        const bool isTextSelected = this->isTextSelected();
        const bool isCursorAtStartOfSelection = isTextSelected && m_selectionStart == m_positionCursor;
        const bool isCursorAtEndOfSelection = isTextSelected && m_selectionEnd == m_positionCursor;

        // Do we have selected text, and cursor is at the end of selected text?
        // In this case, we must preserve start of the selection (we are manipulating
        // with the end of selection.
        if (isCursorAtEndOfSelection)
        {
            m_selectionStart = qMin(m_selectionStart, position);
            m_selectionEnd = qMax(m_selectionStart, position);
        }
        else if (isCursorAtStartOfSelection)
        {
            // We must preserve end of the text selection, because we are manipulating
            // with start of text selection.
            m_selectionStart = qMin(m_selectionEnd, position);
            m_selectionEnd = qMax(m_selectionEnd, position);
        }
        else
        {
            // Otherwise we are manipulating with cursor
            m_selectionStart = qMin(m_positionCursor, position);
            m_selectionEnd = qMax(m_positionCursor, position);
        }
    }

    // Why we are clearing text selection, even if we doesn't have it?
    // We can have, for example m_selectionStart == m_selectionEnd == 3,
    // and we want to have it both zero.
    if (!select || !isTextSelected())
    {
        clearSelection();
    }

    m_positionCursor = position;
}

void PDFTextEditPseudowidget::setText(const QString& text)
{
    clearSelection();
    m_editText = text;
    setCursorPosition(getPositionEnd(), false);
    updateTextLayout();
}

void PDFTextEditPseudowidget::setAppearance(const PDFAnnotationDefaultAppearance& appearance, Qt::Alignment textAlignment, QRectF rect, int maxTextLength)
{
    // Set appearance
    qreal fontSize = appearance.getFontSize();
    if (qFuzzyIsNull(fontSize))
    {
        fontSize = rect.height();
    }

    QFont font(appearance.getFontName());
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPixelSize(qCeil(fontSize));
    m_textLayout.setFont(font);

    QTextOption option = m_textLayout.textOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(textAlignment);
    option.setUseDesignMetrics(true);
    m_textLayout.setTextOption(option);

    m_textColor = appearance.getFontColor();
    if (!m_textColor.isValid())
    {
        m_textColor = Qt::black;
    }

    m_maxTextLength = maxTextLength;
    m_widgetRect = rect;
}

void PDFTextEditPseudowidget::performCut()
{
    if (isReadonly())
    {
        return;
    }

    performCopy();
    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performCopy()
{
    if (isTextSelected() && !isPassword())
    {
        QApplication::clipboard()->setText(getSelectedText(), QClipboard::Clipboard);
    }
}

void PDFTextEditPseudowidget::performPaste()
{
    // We always insert text, even if it is empty. Because we want
    // to erase selected text.
    performInsertText(QApplication::clipboard()->text(QClipboard::Clipboard));
}

void PDFTextEditPseudowidget::performClear()
{
    if (isReadonly())
    {
        return;
    }

    performSelectAll();
    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performSelectAll()
{
    m_selectionStart = getPositionStart();
    m_selectionEnd = getPositionEnd();
}

void PDFTextEditPseudowidget::performBackspace()
{
    if (isReadonly())
    {
        return;
    }

    // If we have selection, then delete selected text. If we do not have
    // selection, then we delete previous character.
    if (!isTextSelected())
    {
        setCursorPosition(m_textLayout.previousCursorPosition(m_positionCursor, QTextLayout::SkipCharacters), true);
    }

    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performDelete()
{
    if (isReadonly())
    {
        return;
    }

    // If we have selection, then delete selected text. If we do not have
    // selection, then we delete previous character.
    if (!isTextSelected())
    {
        setCursorPosition(m_textLayout.nextCursorPosition(m_positionCursor, QTextLayout::SkipCharacters), true);
    }

    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performRemoveSelectedText()
{
    m_editText.remove(m_selectionStart, getSelectionLength());
    setCursorPosition(m_selectionStart, false);
    clearSelection();
    updateTextLayout();
}

void PDFTextEditPseudowidget::performInsertText(const QString& text)
{
    if (isReadonly())
    {
        return;
    }

    // Insert text at the cursor
    performRemoveSelectedText();
    m_editText.insert(m_positionCursor, text);
    setCursorPosition(m_positionCursor + text.length(), false);
    updateTextLayout();
}

void PDFTextEditPseudowidget::updateTextLayout()
{
    // Prepare display text
    if (isPassword())
    {
        m_displayText.resize(m_editText.length(), m_passwordReplacementCharacter);
    }
    else
    {
        m_displayText = m_editText;
    }

    // Perform text layout
    m_textLayout.clearLayout();
    m_textLayout.setText(m_displayText);
    m_textLayout.beginLayout();

    QPointF textLinePosition = m_widgetRect.topLeft();

    while (true)
    {
        QTextLine textLine = m_textLayout.createLine();
        if (!textLine.isValid())
        {
            // We are finished with layout
            break;
        }

        textLinePosition.ry() += textLine.leading();
        textLine.setLineWidth(m_widgetRect.width());
        textLine.setPosition(textLinePosition);
        textLinePosition.ry() += textLine.height();
    }
    m_textLayout.endLayout();

    // Check length
    if (m_maxTextLength > 0)
    {
        int length = 0;
        int currentPos = 0;

        while (currentPos <= m_editText.length())
        {
            currentPos = m_textLayout.nextCursorPosition(currentPos, QTextLayout::SkipCharacters);
            ++length;

            if (length == m_maxTextLength)
            {
                break;
            }
        }

        if (currentPos < m_editText.length())
        {
            m_editText = m_editText.left(currentPos);
            m_positionCursor = qBound(getPositionStart(), m_positionCursor, getPositionEnd());
            updateTextLayout();
        }
    }
}

int PDFTextEditPseudowidget::getSingleStepForward() const
{
    // If direction is right-to-left, then move backward (because
    // text is painted from right to left.
    return (m_textLayout.textOption().textDirection() == Qt::RightToLeft) ? -1 : 1;
}

int PDFTextEditPseudowidget::getNextPrevCursorPosition(int referencePosition, int steps, QTextLayout::CursorMode mode) const
{
    int cursor = referencePosition;

    if (steps > 0)
    {
        for (int i = 0; i < steps; ++i)
        {
            cursor = m_textLayout.nextCursorPosition(cursor, mode);
        }
    }
    else if (steps < 0)
    {
        for (int i = 0; i < steps; ++i)
        {
            cursor = m_textLayout.previousCursorPosition(cursor, mode);
        }
    }

    return cursor;
}

int PDFTextEditPseudowidget::getCurrentLineTextStart() const
{
    return m_textLayout.lineForTextPosition(m_positionCursor).textStart();
}

int PDFTextEditPseudowidget::getCurrentLineTextEnd() const
{
    QTextLine textLine = m_textLayout.lineForTextPosition(m_positionCursor);
    return textLine.textStart() + textLine.textLength();
}

int PDFTextEditPseudowidget::getCursorLineUp() const
{
    QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
    const int lineIndex = line.lineNumber() - 1;

    if (lineIndex >= 0)
    {
        QTextLine upLine = m_textLayout.lineAt(lineIndex);
        return upLine.xToCursor(line.cursorToX(m_positionCursor), QTextLine::CursorBetweenCharacters);
    }

    return m_positionCursor;
}

int PDFTextEditPseudowidget::getCursorLineDown() const
{
    QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
    const int lineIndex = line.lineNumber() + 1;

    if (lineIndex < m_textLayout.lineCount())
    {
        QTextLine downLine = m_textLayout.lineAt(lineIndex);
        return downLine.xToCursor(line.cursorToX(m_positionCursor), QTextLine::CursorBetweenCharacters);
    }

    return m_positionCursor;
}

}   // namespace pdf
