//    Copyright (C) 2020-2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfform.h"
#include "pdfdocument.h"
#include "pdftexteditpseudowidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdocumentbuilder.h"
#include "pdfpainterutils.h"
#include "pdfdbgheap.h"

namespace pdf
{

/// "Pseudo" widget, which is emulating list box. It can contain scrollbar.
class PDFListBoxPseudowidget
{
public:
    explicit PDFListBoxPseudowidget(PDFFormField::FieldFlags flags);

    using Options = PDFFormFieldChoice::Options;

    inline bool isReadonly() const { return m_flags.testFlag(PDFFormField::ReadOnly); }
    inline bool isMultiSelect() const { return m_flags.testFlag(PDFFormField::MultiSelect); }
    inline bool isCommitOnSelectionChange() const { return m_flags.testFlag(PDFFormField::CommitOnSelectionChange); }

    void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    void keyPressEvent(QWidget* widget, QKeyEvent* event);

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param appearance Appearance
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param options Options selectable by list box
    /// \param topIndex Index of first visible item
    void setAppearance(const PDFAnnotationDefaultAppearance& appearance,
                       Qt::Alignment textAlignment,
                       QRectF rect,
                       const Options& options,
                       int topIndex,
                       std::set<int> selection);

    /// Draw text edit using given parameters
    /// \param parameters Parameters
    void draw(AnnotationDrawParameters& parameters, bool edit) const;

    /// Sets current selection. If commit on selection change flag
    /// is set, then contents of the widgets are commited. New selection
    /// is not set, if field is readonly and \p force is false
    /// \param selection New selection
    /// \param force Set selection even if readonly
    void setSelection(std::set<int> selection, bool force);

    /// Returns active item selection
    const std::set<int>& getSelection() const { return m_selection; }

    /// Returns top index
    int getTopItemIndex() const { return m_topIndex; }

    /// Set top item index
    /// \param index Top item index
    void setTopItemIndex(int index);

    /// Scrolls the list box in such a way, that index is visible
    /// \param index Index to scroll to
    void scrollTo(int index);

    /// Returns valid index from index (i.e. index which ranges from first
    /// row to the last one). If index itself is valid, then it is returned.
    /// \param index Index, from which we want to get valid one
    int getValidIndex(int index) const;

    /// Returns true, if index is valid
    bool isValidIndex(int index) const { return index == getValidIndex(index); }

    /// Returns number of rows in the viewport
    int getViewportRowCount() const { return qFloor(m_widgetRect.height() / m_lineSpacing); }

    /// Returns item index from widget position (i.e. transforms
    /// point in the widget to the index of item, which is under the point)
    /// \param point Widget point
    int getIndexFromWidgetPosition(const QPointF& point) const;

    /// Sets current item and updates selection based on keyboard modifiers
    /// \param index New index
    /// \param modifiers Keyboard modifiers
    void setCurrentItem(int index, Qt::KeyboardModifiers modifiers);

    /// Returns text of selected item text. If no item is selected,
    /// or multiple items are selected, then empty string is returned.
    /// \returns Selected text
    QString getSelectedItemText() const;

    /// Returns true, if single item is selected
    bool isSingleItemSelected() const { return m_selection.size() == 1; }

    /// Returns index of option, in the list box option list.
    /// If option is not found, then -1 is returned.
    /// \param option Option
    int findOption(const QString& option) const;

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param font Font
    /// \param fontColor Font color
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param options Options selectable by list box
    /// \param topIndex Index of first visible item
    void initialize(QFont font, QColor fontColor, Qt::Alignment textAlignment, QRectF rect,
                    const Options& options, int topIndex, std::set<int> selection);

private:
    int getStartItemIndex() const { return 0; }
    int getEndItemIndex() const { return m_options.empty() ? 0 : int(m_options.size() - 1); }

    int getSingleStep() const { return 1; }
    int getPageStep() const { return qMax(getViewportRowCount() - 1, getSingleStep()); }

    /// Returns true, if row with this index is visible in the widget
    /// (it is in viewport).
    /// \param index Index
    bool isVisible(int index) const;

    /// Moves current item by offset (negative is up, positive is down)
    /// \param offset Offset
    /// \param modifiers Keyboard modifiers
    void moveCurrentItemIndexByOffset(int offset, Qt::KeyboardModifiers modifiers) { setCurrentItem(m_currentIndex + offset, modifiers); }

    /// Returns true, if list box has continuous selection
    bool hasContinuousSelection() const;

    /// Creates transformation matrix, which transforms widget coordinates to page coordinates
    QTransform createListBoxTransformMatrix() const;

    PDFFormField::FieldFlags m_flags;

    Options m_options;
    Qt::Alignment m_textAlignment = Qt::Alignment();
    int m_topIndex = 0;
    int m_currentIndex = 0;
    std::set<int> m_selection;
    QFont m_font;
    PDFReal m_lineSpacing = 0.0;
    QRectF m_widgetRect;
    QColor m_textColor;
};

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
        form.m_signatureFlags = static_cast<SignatureFlags>(static_cast<int32_t>(loader.readIntegerFromDictionary(formDictionary, "SigFlags", 0)));
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

Qt::Alignment PDFForm::getDefaultAlignment() const
{
    Qt::Alignment alignment = Qt::AlignVCenter;

    switch (getQuadding().value_or(0))
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

    return alignment;
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

void PDFForm::apply(const std::function<void (const PDFFormField*)>& functor) const
{
    for (const PDFFormFieldPointer& childField : getFormFields())
    {
        childField->apply(functor);
    }
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
        formField->m_fieldFlags = fieldDictionary->hasKey("Ff") ? static_cast<FieldFlags>(static_cast<int32_t>(loader.readIntegerFromDictionary(fieldDictionary, "Ff", 0))) : parentFlags;
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
            Qt::Alignment alignment = Qt::Alignment();

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
            formFieldChoice->m_selection = fieldDictionary->get("I");
        }

        if (formFieldSignature)
        {
            formFieldSignature->m_signature = PDFSignature::parse(storage, fieldDictionary->get("V"));
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

void PDFFormField::resetValue(const ResetValueParameters& parameters)
{
    Q_UNUSED(parameters);

    // Default behaviour: do nothing
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

void PDFFormFieldButton::resetValue(const ResetValueParameters& parameters)
{
    // Do not allow to reset value of push buttons
    if (getFlags().testFlag(PushButton))
    {
        return;
    }

    Q_ASSERT(parameters.modifier);
    Q_ASSERT(parameters.formManager);

    PDFObject defaultValue = getDefaultValue();
    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), defaultValue);

    PDFDocumentDataLoaderDecorator loader(parameters.formManager->getDocument());
    QByteArray defaultState = loader.readString(defaultValue);

    for (const PDFFormWidget& formWidget : getWidgets())
    {
        QByteArray onState = PDFFormFieldButton::getOnAppearanceState(parameters.formManager, &formWidget);
        if (defaultState == onState)
        {
            builder->setAnnotationAppearanceState(formWidget.getWidget(), onState);
        }
        else
        {
            QByteArray offState = PDFFormFieldButton::getOffAppearanceState(parameters.formManager, &formWidget);
            builder->setAnnotationAppearanceState(formWidget.getWidget(), offState);
        }
        parameters.modifier->markAnnotationsChanged();
    }
}

PDFFormManager::PDFFormManager(QObject* parent) :
    BaseClass(parent),
    m_annotationManager(nullptr),
    m_document(nullptr),
    m_flags(getDefaultApperanceFlags()),
    m_isCommitDisabled(false)
{

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
        PDFTemporaryValueChange change(&m_isCommitDisabled, true);
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

        m_xfaEngine.setDocument(document, &m_form);
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
    m_form.apply(functor);
}

void PDFFormManager::modify(const std::function<void (PDFFormField*)>& functor) const
{
    for (const PDFFormFieldPointer& childField : m_form.getFormFields())
    {
        childField->modify(functor);
    }
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
    if (!m_document)
    {
        // This can happen, when we are closing the document and some editor is opened
        return;
    }

    Q_ASSERT(parameters.invokingFormField);
    Q_ASSERT(parameters.invokingWidget.isValid());

    parameters.formManager = this;
    parameters.scope = PDFFormField::SetValueParameters::Scope::User;

    PDFDocumentModifier modifier(m_document);
    modifier.getBuilder()->setFormManager(this);
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
            Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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

void PDFFormManager::drawXFAForm(const QTransform& pagePointToDevicePointMatrix,
                                 const PDFPage* page,
                                 QList<PDFRenderError>& errors,
                                 QPainter* painter)
{
    if (hasXFAForm())
    {
        m_xfaEngine.draw(pagePointToDevicePointMatrix, page, errors, painter);
    }
}

void PDFFormManager::performPaging()
{
    if (!hasXFAForm() || !m_document)
    {
        return;
    }

    std::vector<QSizeF> pageSizes = m_xfaEngine.getPageSizes();

    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageCount = catalog->getPageCount();
    std::vector<QSizeF> oldPageSizes;
    oldPageSizes.reserve(pageCount);

    for (size_t i = 0; i < pageCount; ++i)
    {
        oldPageSizes.push_back(catalog->getPage(i)->getMediaBox().size());
    }

    bool changed = pageSizes.size() != oldPageSizes.size();
    if (!changed)
    {
        for (size_t i = 0; i < pageCount; ++i)
        {
            changed = changed ||
                      !qFuzzyCompare(pageSizes[i].width(), oldPageSizes[i].width()) ||
                      !qFuzzyCompare(pageSizes[i].height(), oldPageSizes[i].height());
        }
    }

    if (changed && !oldPageSizes.empty())
    {
        PDFDocumentModifier modifier(m_document);
        modifier.getBuilder()->setFormManager(this);
        modifier.markReset();

        PDFDocumentBuilder* builder = modifier.getBuilder();
        builder->flattenPageTree();
        std::vector<PDFObjectReference> pages = builder->getPages();

        // Do we have more pages?
        if (pages.size() > pageSizes.size())
        {
            pages.resize(pageSizes.size());
        }

        // Do we have less pages?
        while (pages.size() < pageSizes.size())
        {
            std::vector<pdf::PDFObjectReference> references = pdf::PDFDocumentBuilder::createReferencesFromObjects(builder->copyFrom(pdf::PDFDocumentBuilder::createObjectsFromReferences({ pages.front() }), *builder->getStorage(), true));
            Q_ASSERT(references.size() == 1);
            pages.push_back(references.front());
        }

        Q_ASSERT(pages.size() == pageSizes.size());
        for (size_t i = 0; i < pages.size(); ++i)
        {
            builder->setPageMediaBox(pages[i], QRectF(QPointF(0, 0), pageSizes[i]));
        }

        builder->setPages(std::move(pages));

        if (modifier.finalize())
        {
            Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
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

void PDFFormManager::performResetAction(const PDFActionResetForm* action)
{
    Q_ASSERT(action);
    Q_ASSERT(m_document);

    PDFDocumentModifier modifier(m_document);
    modifier.getBuilder()->setFormManager(this);

    auto resetFieldValue = [this, action, &modifier](PDFFormField* formField)
    {
        const PDFFormAction::FieldList& fieldList = action->getFieldList();

        // Akceptujeme form field dle daného filtru?
        bool accept = false;
        bool isInFieldList = std::find(fieldList.fieldReferences.cbegin(), fieldList.fieldReferences.cend(), formField->getSelfReference()) != fieldList.fieldReferences.cend() ||
                             fieldList.qualifiedNames.contains(formField->getName(PDFFormField::NameType::FullyQualified));
        switch (action->getFieldScope())
        {
            case PDFFormAction::FieldScope::All:
                accept = true;
                break;

            case PDFFormAction::FieldScope::Include:
                accept = isInFieldList;
                break;

            case PDFFormAction::FieldScope::Exclude:
                accept = !isInFieldList;
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        if (accept)
        {
            PDFFormField::ResetValueParameters parameters;
            parameters.formManager = this;
            parameters.modifier = &modifier;
            formField->resetValue(parameters);
        }
    };
    modify(resetFieldValue);

    if (modifier.finalize())
    {
        Q_EMIT documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

bool PDFFormFieldText::setValue(const SetValueParameters& parameters)
{
    // If form field is readonly, and scope is user (form field is changed by user,
    // not by calculated value), then we must not allow value change.
    if (getFlags().testFlag(ReadOnly) && parameters.scope == SetValueParameters::Scope::User)
    {
        return false;
    }

    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), parameters.value);
    m_value = parameters.value;

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }

    return true;
}

void PDFFormFieldText::resetValue(const ResetValueParameters& parameters)
{
    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFObject defaultValue = getDefaultValue();
    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), defaultValue);
    m_value = defaultValue;

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }
}

bool PDFFormFieldChoice::setValue(const SetValueParameters& parameters)
{
    // If form field is readonly, and scope is user (form field is changed by user,
    // not by calculated value), then we must not allow value change.
    if (getFlags().testFlag(ReadOnly) && parameters.scope == SetValueParameters::Scope::User)
    {
        return false;
    }

    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), parameters.value);

    if (isListBox())
    {
        // Listbox has special values, which must be set
        builder->setFormFieldChoiceTopIndex(getSelfReference(), parameters.listboxTopIndex);
        builder->setFormFieldChoiceIndices(getSelfReference(), parameters.listboxChoices);
    }

    m_value = parameters.value;
    m_topIndex = parameters.listboxTopIndex;

    PDFObjectFactory objectFactory;
    objectFactory << parameters.listboxChoices;
    m_selection = objectFactory.takeObject();

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }

    return true;
}

void PDFFormFieldChoice::resetValue(const PDFFormField::ResetValueParameters& parameters)
{
    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFObject defaultValue = getDefaultValue();
    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), defaultValue);
    m_value = defaultValue;
    m_selection = PDFObject();

    if (isListBox())
    {
        // Listbox has special values, which must be set
        builder->setFormFieldChoiceTopIndex(getSelfReference(), 0);
        builder->setFormFieldChoiceIndices(getSelfReference(), { });
    }

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }
}

void PDFFormFieldChoice::reloadValue(const PDFObjectStorage* storage, PDFObject parentValue)
{
    BaseClass::reloadValue(storage, parentValue);

    if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(getSelfReference())))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        m_topIndex = loader.readIntegerFromDictionary(fieldDictionary, "TI", 0);
        m_selection = fieldDictionary->get("I");
    }
}

}   // namespace pdf
