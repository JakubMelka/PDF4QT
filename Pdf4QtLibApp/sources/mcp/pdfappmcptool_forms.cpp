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

#include "pdfappmcptool_forms.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcphost.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappformcontroller.h"

#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfformfieldformat.h"
#include "pdfmcpjson.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>

namespace pdf
{

/// One value of forms.set_values
class PDFAppMcpFormValue
{
public:
    QString name;
    std::optional<QString> value;
    std::optional<bool> checked;
    std::optional<QStringList> selected;
};

/// Helpers of the tool forms
class PDFAppMcpFormsToolHelper
{
public:
    PDFAppMcpFormsToolHelper() = delete;

    /// Most fields of a form, which a call looks through
    static constexpr size_t MAXIMUM_FIELDS = 100000;

    static QString tr(const char* text, int count = -1)
    {
        return QCoreApplication::translate("pdf::PDFAppMcpFormsTool", text, nullptr, count);
    }

    /// Fields of the form in pre-order (the parents first), bounded
    static void collectFields(const PDFFormFields& fields, std::vector<PDFFormField*>* result, int depth)
    {
        if (depth > PDFFormField::MAX_FIELD_TREE_DEPTH)
        {
            return;
        }
        for (const PDFFormFieldPointer& field : fields)
        {
            if (!field || result->size() >= MAXIMUM_FIELDS)
            {
                continue;
            }
            result->push_back(field.data());
            collectFields(field->getChildFields(), result, depth + 1);
        }
    }

    /// Terminal fields (fields with a value: no child fields, or widgets)
    static std::vector<PDFFormField*> getTerminalFields(const PDFFormManager& manager)
    {
        std::vector<PDFFormField*> fields;
        collectFields(manager.getForm()->getFormFields(), &fields, 0);

        std::vector<PDFFormField*> terminal;
        for (PDFFormField* field : fields)
        {
            if (field->getChildFields().empty() || !field->getWidgets().empty())
            {
                terminal.push_back(field);
            }
        }
        return terminal;
    }

    static QString getName(const PDFFormField* field)
    {
        return field->getName(PDFFormField::NameType::FullyQualified);
    }

    static PDFObject createText(const QString& text)
    {
        PDFObjectFactory factory;
        factory << text;
        return factory.takeObject();
    }

    static QString readText(const PDFDocument* document, const PDFObject& object)
    {
        PDFDocumentDataLoaderDecorator loader(document);
        return loader.readTextString(object, QString());
    }

    /// Export values of the options of the buttons of a radio group (the on states
    /// may be indices into them; a widget field takes the options of its group)
    static QStringList getGroupOptions(const std::vector<const PDFFormFieldButton*>& group)
    {
        for (const PDFFormFieldButton* button : group)
        {
            if (!button->getOptions().isEmpty())
            {
                return button->getOptions();
            }
        }
        return QStringList();
    }

    /// The state name of a value of a radio group (the fields of one fully
    /// qualified name - the kids with their widgets are fields of the same name):
    /// an on state name or an export value of the options; \p widget gets the
    /// widget of the state and \p field its field
    static QByteArray findRadioState(const PDFFormManager& manager, const std::vector<const PDFFormFieldButton*>& group, const QString& value,
                                     PDFObjectReference* widget, const PDFFormFieldButton** field)
    {
        const QStringList options = getGroupOptions(group);
        for (const PDFFormFieldButton* button : group)
        {
            for (const PDFFormWidget& formWidget : button->getWidgets())
            {
                const QByteArray onState = PDFFormFieldButton::getOnAppearanceState(&manager, &formWidget);
                if (onState.isEmpty())
                {
                    continue;
                }

                bool isNumber = false;
                const int index = onState.toInt(&isNumber);
                const QString exportValue = isNumber && index >= 0 && index < options.size() ? options[index] : QString();
                if (QString::fromLatin1(onState) == value || (!exportValue.isEmpty() && exportValue == value))
                {
                    *widget = formWidget.getWidget();
                    *field = button;
                    return onState;
                }
            }
        }
        return QByteArray();
    }

    static QStringList getRadioValues(const PDFFormManager& manager, const std::vector<const PDFFormFieldButton*>& group)
    {
        QStringList values;
        const QStringList options = getGroupOptions(group);
        for (const PDFFormFieldButton* button : group)
        {
            for (const PDFFormWidget& formWidget : button->getWidgets())
            {
                const QByteArray onState = PDFFormFieldButton::getOnAppearanceState(&manager, &formWidget);
                bool isNumber = false;
                const int index = onState.toInt(&isNumber);
                values << (isNumber && index >= 0 && index < options.size() ? options[index] : QString::fromLatin1(onState));
            }
        }
        values.removeAll(QString());
        values.removeDuplicates();
        return values;
    }

    /// Index of an option of a choice field by its export or user string (-1 = none)
    static int findOption(const PDFFormFieldChoice* choice, const QString& value)
    {
        const PDFFormFieldChoice::Options& options = choice->getOptions();
        for (size_t i = 0; i < options.size(); ++i)
        {
            if (options[i].exportString == value || options[i].userString == value)
            {
                return int(i);
            }
        }
        return -1;
    }

    static QString getOptionExport(const PDFFormFieldChoice* choice, int index)
    {
        const PDFFormFieldChoice::Options& options = choice->getOptions();
        const PDFFormFieldChoice::Option& option = options[size_t(index)];
        return option.exportString.isEmpty() ? option.userString : option.exportString;
    }

    /// The text of a text field committed as the input of the user (plan 3.7): the
    /// native keystroke function of the Acrobat form library of the field
    /// (AFNumber_Keystroke, AFDate_KeystrokeEx, AFSpecial_Keystroke, ...) accepts
    /// and normalizes it as the editor of the form field does it. The text of the
    /// user ("1234,5" of a field with a decimal comma) and the stored value
    /// ("1234.5") are both accepted. Returns std::nullopt for a rejected text.
    static std::optional<QString> commitText(PDFFormManager& manager, const PDFFormField* field, const QString& text)
    {
        if (field->getWidgets().empty())
        {
            return text;
        }

        const PDFFormFieldFormat keystroke = PDFFormFieldFormat::parse(manager.getAction(PDFAnnotationAdditionalActions::FormFieldModified, &field->getWidgets().front()));
        if (!keystroke.isKeystroke())
        {
            return text;
        }

        if (std::optional<QString> committed = keystroke.commit(text))
        {
            return committed;
        }
        return keystroke.commit(keystroke.getEditText(text));
    }
};

QStringList PDFAppMcpFormsTool::matchFieldNames(const QStringList& names, const QString& name)
{
    if (names.contains(name))
    {
        return { name };
    }

    QStringList matches;
    const QString suffix = QLatin1Char('.') + name;
    for (const QString& fieldName : names)
    {
        if (!name.isEmpty() && fieldName.endsWith(suffix))
        {
            matches << fieldName;
        }
    }
    return matches;
}

/// forms.set_values and forms.reset on a snapshot (a headless form manager)
class PDFAppMcpFormsJob : public PDFAppMcpChangeJob
{
public:
    PDFAppMcpFormsJob(PDFAppMcpChangeContext context, bool isReset, std::vector<PDFAppMcpFormValue> values, QStringList resetNames) :
        PDFAppMcpChangeJob(std::move(context)),
        m_isReset(isReset),
        m_values(std::move(values)),
        m_resetNames(std::move(resetNames))
    {

    }

protected:
    virtual PDFAppMcpChange build() override
    {
        PDFAppMcpChange change;
        const PDFDocumentPointer document = getSnapshot().document;
        if (document.isNull())
        {
            change.errorText = QStringLiteral("The document is not available.");
            return change;
        }

        try
        {
            // A form manager of Core without widgets (the worker thread owns it)
            PDFFormManager manager(nullptr);
            manager.setAppearanceFlags(PDFFormManager::None);
            manager.setDocument(PDFModifiedDocument(document, nullptr, PDFModifiedDocument::Reset));
            if (!manager.hasAcroForm())
            {
                change.errorText = QStringLiteral("The document has no form, which can be filled.");
                return change;
            }
            return m_isReset ? buildReset(manager, document) : buildSetValues(manager, document);
        }
        catch (const PDFException& exception)
        {
            change.errorText = QStringLiteral("The form could not be filled: %1").arg(exception.getMessage());
            return change;
        }
    }

    virtual std::shared_ptr<PDFAppMcpChangeJob> createRerun(PDFAppMcpChangeContext context) const override
    {
        return std::make_shared<PDFAppMcpFormsJob>(std::move(context), m_isReset, m_values, m_resetNames);
    }

private:
    /// The terminal fields of each fully qualified name
    using FieldsByName = QHash<QString, std::vector<PDFFormField*>>;

    PDFAppMcpChange buildSetValues(PDFFormManager& manager, const PDFDocumentPointer& document)
    {
        using Helper = PDFAppMcpFormsToolHelper;

        PDFAppMcpChange change;
        const std::vector<PDFFormField*> fields = Helper::getTerminalFields(manager);
        // The fields of each fully qualified name (kids of a field with their
        // widgets have the name of the field), in the order of the form
        QStringList uniqueNames;
        FieldsByName fieldsByName;
        for (PDFFormField* field : fields)
        {
            const QString name = Helper::getName(field);
            std::vector<PDFFormField*>& sameName = fieldsByName[name];
            if (sameName.empty())
            {
                uniqueNames << name;
            }
            sameName.push_back(field);
        }

        // The names: exact, a unique suffix, a choice of the client
        bool isDeclined = false;
        const std::optional<QJsonObject> answer = PDFAppMcpChoice::getAnswer(getCall()->getRequest(), &isDeclined);
        if (isDeclined)
        {
            change.errorText = QStringLiteral("The user declined to choose the form fields. Pass the fully qualified names of the fields.");
            return change;
        }

        std::vector<QString> resolved(m_values.size());
        QJsonArray errors;
        QStringList fallback;
        for (size_t i = 0; i < m_values.size(); ++i)
        {
            const QString& name = m_values[i].name;
            const QStringList matches = PDFAppMcpFormsTool::matchFieldNames(uniqueNames, name);
            if (matches.isEmpty())
            {
                QJsonObject error;
                error.insert(QStringLiteral("name"), name);
                error.insert(QStringLiteral("message"), QStringLiteral("No field has this name; call document.get_form_fields for the names."));
                errors.append(error);
                continue;
            }
            if (matches.size() == 1)
            {
                resolved[i] = matches.front();
                continue;
            }

            const QString property = QStringLiteral("field%1").arg(i + 1);
            const QString chosen = answer ? answer->value(property).toString() : QString();
            if (matches.contains(chosen))
            {
                resolved[i] = chosen;
                continue;
            }

            PDFAppMcpChoice::Property choice;
            choice.name = property;
            choice.title = QStringLiteral("Field for “%1”").arg(PDFAppMcpArguments::sanitizeDocumentText(name, 64));
            for (const QString& match : matches.mid(0, 100))
            {
                choice.options.emplace_back(match, match);
            }
            change.choices.push_back(std::move(choice));
            fallback << QStringLiteral("\"%1\" matches %2").arg(name, matches.mid(0, 20).join(QStringLiteral(", ")));
        }

        if (!change.choices.empty())
        {
            change.choiceMessage = QStringLiteral("Several form fields match the names. Choose the fields to fill.");
            change.choiceFallback = QStringLiteral("Several form fields match the names: %1. Pass the fully qualified names.").arg(fallback.join(QStringLiteral("; ")));
            return change;
        }

        PDFDocumentModifier modifier(document.data());
        modifier.getBuilder()->setFormManager(&manager);

        QJsonArray changed;
        QJsonArray unchanged;
        std::vector<PDFObjectReference> widgets;
        for (size_t i = 0; i < m_values.size(); ++i)
        {
            if (resolved[i].isEmpty())
            {
                continue;
            }

            const PDFAppMcpFormValue& value = m_values[i];
            PDFFormField* field = fieldsByName.value(resolved[i]).front();
            QString message;
            bool isUnchanged = false;
            if (!setValue(manager, document.data(), &modifier, fieldsByName, field, value, &message, &isUnchanged))
            {
                QJsonObject error;
                error.insert(QStringLiteral("name"), resolved[i]);
                error.insert(QStringLiteral("message"), message);
                errors.append(error);
                continue;
            }

            if (isUnchanged)
            {
                unchanged.append(resolved[i]);
                continue;
            }

            changed.append(resolved[i]);
            for (const PDFFormWidget& widget : field->getWidgets())
            {
                widgets.push_back(widget.getWidget());
            }
        }

        change.output.emplace_back(QStringLiteral("changed"), changed);
        change.output.emplace_back(QStringLiteral("unchanged"), unchanged);
        change.output.emplace_back(QStringLiteral("errors"), errors);
        change.output.emplace_back(QStringLiteral("note"), QStringLiteral("JavaScript is not run in PDF4QT, so calculated fields are not recomputed."));
        if (changed.isEmpty())
        {
            return change;
        }

        if (!modifier.finalize() || modifier.getDocument().isNull())
        {
            change.errorText = QStringLiteral("The values could not be written into the form.");
            return change;
        }

        const int count = int(changed.size());
        QStringList details;
        for (const QJsonValue& name : changed)
        {
            if (details.size() < 3)
            {
                details << PDFAppMcpArguments::sanitizeDocumentText(name.toString(), 60);
            }
        }

        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("form-fill");
        row.text = (count == 1 ? Helper::tr("Fill %n form field", count) : Helper::tr("Fill %n form fields", count));
        row.detail = details.join(QStringLiteral(", "));
        change.rows.push_back(row);
        change.changeCount = count;
        change.summary = row.text;
        change.modified = PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags() | PDFModifiedDocument::FormField);
        change.undoDescription = (count == 1 ? Helper::tr("%1 filled %n form field", count) : Helper::tr("%1 filled %n form fields", count)).arg(getContext().authorName);
        change.activityDetail.insert(QStringLiteral("count"), count);
        return change;
    }

    bool setValue(PDFFormManager& manager, const PDFDocument* document, PDFDocumentModifier* modifier, const FieldsByName& fieldsByName, PDFFormField* field,
                  const PDFAppMcpFormValue& value, QString* message, bool* isUnchanged)
    {
        using Helper = PDFAppMcpFormsToolHelper;

        if (manager.isReadOnly(field))
        {
            *message = QStringLiteral("The field is read only.");
            return false;
        }

        PDFFormField::SetValueParameters parameters;
        parameters.invokingFormField = field;
        parameters.modifier = modifier;
        parameters.formManager = &manager;
        parameters.scope = PDFFormField::SetValueParameters::Scope::User;
        if (!field->getWidgets().empty())
        {
            parameters.invokingWidget = field->getWidgets().front().getWidget();
        }

        switch (field->getFieldType())
        {
            case PDFFormField::FieldType::Text:
            {
                if (!value.value)
                {
                    *message = QStringLiteral("A text field needs value (a text).");
                    return false;
                }
                if (field->getFlags().testFlag(PDFFormField::Password))
                {
                    // A password is never written into the document (the editor of
                    // the field does not store it either)
                    *message = QStringLiteral("A password field is never written into the document.");
                    return false;
                }

                const PDFFormFieldText* textField = dynamic_cast<const PDFFormFieldText*>(field);
                QString text = PDFAppMcpArguments::sanitizeDocumentText(*value.value, PDFAppMcpToolSet::MAXIMUM_TEXT_LENGTH);
                if (!field->getFlags().testFlag(PDFFormField::Multiline))
                {
                    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
                }

                // The format of the field accepts and normalizes the text as the
                // input of the user (JavaScript is not run, O16)
                const std::optional<QString> committedText = Helper::commitText(manager, field, text);
                if (!committedText)
                {
                    *message = QStringLiteral("\"%1\" does not match the format of the field (a number, a date, a time, a ZIP code, a phone number or a mask).")
                                   .arg(PDFAppMcpArguments::sanitizeDocumentText(text, 64));
                    return false;
                }
                text = *committedText;

                if (textField && textField->getTextMaximalLength() > 0 && text.size() > textField->getTextMaximalLength())
                {
                    *message = QStringLiteral("The text is longer than the %1 characters of the field.").arg(textField->getTextMaximalLength());
                    return false;
                }
                if (Helper::readText(document, field->getValue()) == text)
                {
                    *isUnchanged = true;
                    return true;
                }
                parameters.value = Helper::createText(text);
                break;
            }

            case PDFFormField::FieldType::Button:
            {
                const PDFFormFieldButton* button = dynamic_cast<const PDFFormFieldButton*>(field);
                if (!button)
                {
                    *message = QStringLiteral("The field cannot be filled.");
                    return false;
                }

                switch (button->getButtonType())
                {
                    case PDFFormFieldButton::ButtonType::PushButton:
                        *message = QStringLiteral("A push button has no value.");
                        return false;

                    case PDFFormFieldButton::ButtonType::CheckBox:
                    {
                        if (!value.checked)
                        {
                            *message = QStringLiteral("A check box needs checked (true or false).");
                            return false;
                        }
                        QByteArray state = "Off";
                        if (*value.checked)
                        {
                            state = button->getWidgets().empty() ? QByteArray("Yes") : PDFFormFieldButton::getOnAppearanceState(&manager, &button->getWidgets().front());
                            if (state.isEmpty())
                            {
                                state = "Yes";
                            }
                        }
                        if (field->getValue().isName() && field->getValue().getString() == state)
                        {
                            *isUnchanged = true;
                            return true;
                        }
                        if (!field->getValue().isName() && state == "Off")
                        {
                            *isUnchanged = true;
                            return true;
                        }
                        parameters.value = PDFObject::createName(state);
                        break;
                    }

                    case PDFFormFieldButton::ButtonType::RadioButton:
                    {
                        // The buttons of a group may be kids with the same fully
                        // qualified name (fields merged with their widgets)
                        std::vector<const PDFFormFieldButton*> group;
                        const std::vector<PDFFormField*> sameName = fieldsByName.value(Helper::getName(field));
                        for (PDFFormField* other : sameName)
                        {
                            if (const PDFFormFieldButton* otherButton = dynamic_cast<const PDFFormFieldButton*>(other))
                            {
                                group.push_back(otherButton);
                            }
                        }

                        if (!value.value)
                        {
                            *message = QStringLiteral("A radio group needs value, one of: %1.").arg(Helper::getRadioValues(manager, group).join(QStringLiteral(", ")));
                            return false;
                        }
                        PDFObjectReference widget;
                        const PDFFormFieldButton* stateField = nullptr;
                        const QByteArray state = Helper::findRadioState(manager, group, *value.value, &widget, &stateField);
                        if (state.isEmpty() || !stateField)
                        {
                            *message = QStringLiteral("\"%1\" is not a value of the radio group; the values are: %2.")
                                           .arg(PDFAppMcpArguments::sanitizeDocumentText(*value.value, 64), Helper::getRadioValues(manager, group).join(QStringLiteral(", ")));
                            return false;
                        }

                        // The field of the widget of the state is the invoking field
                        for (PDFFormField* other : sameName)
                        {
                            if (other == stateField)
                            {
                                field = other;
                                parameters.invokingFormField = other;
                            }
                        }
                        if (field->getValue().isName() && field->getValue().getString() == state)
                        {
                            *isUnchanged = true;
                            return true;
                        }
                        parameters.value = PDFObject::createName(state);
                        parameters.invokingWidget = widget;
                        break;
                    }
                }
                break;
            }

            case PDFFormField::FieldType::Choice:
            {
                const PDFFormFieldChoice* choice = dynamic_cast<const PDFFormFieldChoice*>(field);
                if (!choice)
                {
                    *message = QStringLiteral("The field cannot be filled.");
                    return false;
                }

                if (choice->getFlags().testFlag(PDFFormField::MultiSelect) && choice->isListBox())
                {
                    if (!value.selected)
                    {
                        *message = QStringLiteral("A list with multiple selection needs selected (a list of values).");
                        return false;
                    }

                    PDFObjectFactory factory;
                    factory.beginArray();
                    std::vector<PDFInteger> indices;
                    for (const QString& selectedValue : *value.selected)
                    {
                        const int index = Helper::findOption(choice, selectedValue);
                        if (index < 0)
                        {
                            *message = QStringLiteral("\"%1\" is not an option of the list.").arg(PDFAppMcpArguments::sanitizeDocumentText(selectedValue, 64));
                            return false;
                        }
                        if (std::find(indices.begin(), indices.end(), PDFInteger(index)) == indices.end())
                        {
                            indices.push_back(index);
                            factory << Helper::getOptionExport(choice, index);
                        }
                    }
                    factory.endArray();
                    std::sort(indices.begin(), indices.end());
                    parameters.value = factory.takeObject();
                    parameters.listboxChoices = indices;
                    parameters.listboxTopIndex = indices.empty() ? 0 : indices.front();
                    break;
                }

                if (!value.value)
                {
                    *message = QStringLiteral("A choice field needs value (one of its options).");
                    return false;
                }

                const int index = Helper::findOption(choice, *value.value);
                QString exportValue;
                if (index >= 0)
                {
                    exportValue = Helper::getOptionExport(choice, index);
                    if (choice->isListBox())
                    {
                        parameters.listboxChoices = { PDFInteger(index) };
                        parameters.listboxTopIndex = index;
                    }
                }
                else if (choice->isEditableComboBox())
                {
                    exportValue = PDFAppMcpArguments::sanitizeDocumentText(*value.value, PDFAppMcpToolSet::MAXIMUM_TEXT_LENGTH);
                }
                else
                {
                    *message = QStringLiteral("\"%1\" is not an option of the field.").arg(PDFAppMcpArguments::sanitizeDocumentText(*value.value, 64));
                    return false;
                }

                if (Helper::readText(document, field->getValue()) == exportValue)
                {
                    *isUnchanged = true;
                    return true;
                }
                parameters.value = Helper::createText(exportValue);
                break;
            }

            case PDFFormField::FieldType::Signature:
                *message = QStringLiteral("A signature field cannot be filled by an assistant.");
                return false;

            case PDFFormField::FieldType::Invalid:
                *message = QStringLiteral("The field cannot be filled.");
                return false;
        }

        if (!parameters.invokingWidget.isValid() && field->getFieldType() != PDFFormField::FieldType::Text && field->getFieldType() != PDFFormField::FieldType::Choice)
        {
            *message = QStringLiteral("The field has no widget on a page.");
            return false;
        }

        if (!field->setValue(parameters))
        {
            *message = QStringLiteral("The value could not be set.");
            return false;
        }

        // The fields of the same fully qualified name get the value too (as the
        // form manager does it for the user)
        const QString name = Helper::getName(field);
        parameters.scope = PDFFormField::SetValueParameters::Scope::Internal;
        const std::vector<PDFFormField*> sameName = fieldsByName.value(name);
        for (PDFFormField* other : sameName)
        {
            if (other != field)
            {
                other->setValue(parameters);
            }
        }
        return true;
    }

    PDFAppMcpChange buildReset(PDFFormManager& manager, const PDFDocumentPointer& document)
    {
        using Helper = PDFAppMcpFormsToolHelper;

        PDFAppMcpChange change;
        const std::vector<PDFFormField*> fields = Helper::getTerminalFields(manager);

        std::vector<PDFFormField*> targets;
        if (m_resetNames.isEmpty())
        {
            targets = fields;
        }
        else
        {
            QStringList names;
            for (const PDFFormField* field : fields)
            {
                names << Helper::getName(field);
            }
            for (const QString& name : m_resetNames)
            {
                QStringList uniqueNames = names;
                uniqueNames.removeDuplicates();
                const QStringList matches = PDFAppMcpFormsTool::matchFieldNames(uniqueNames, name);
                if (matches.size() != 1)
                {
                    change.errorText = matches.isEmpty() ? QStringLiteral("names: no field has the name \"%1\"; call document.get_form_fields for the names.").arg(PDFAppMcpArguments::sanitizeDocumentText(name, 64))
                                                         : QStringLiteral("names: \"%1\" matches several fields (%2); pass the fully qualified name.").arg(PDFAppMcpArguments::sanitizeDocumentText(name, 64), matches.mid(0, 20).join(QStringLiteral(", ")));
                    return change;
                }
                for (qsizetype index = 0; index < names.size(); ++index)
                {
                    if (names[index] == matches.front())
                    {
                        targets.push_back(fields[size_t(index)]);
                    }
                }
            }
        }

        PDFDocumentModifier modifier(document.data());
        modifier.getBuilder()->setFormManager(&manager);
        PDFFormField::ResetValueParameters parameters;
        parameters.modifier = &modifier;
        parameters.formManager = &manager;

        // A reset of fields at their defaults is not performed (isResetEffective)
        int count = 0;
        for (PDFFormField* field : targets)
        {
            if (field->getFieldType() == PDFFormField::FieldType::Signature || manager.isReadOnly(field) || field->getValue() == field->getDefaultValue())
            {
                continue;
            }
            field->resetValue(parameters);
            ++count;
        }

        change.output.emplace_back(QStringLiteral("reset"), count > 0);
        if (count == 0)
        {
            return change;
        }

        if (!modifier.finalize() || modifier.getDocument().isNull())
        {
            change.errorText = QStringLiteral("The form could not be reset.");
            return change;
        }

        PDFAppMcpChangeRow row;
        row.iconName = QStringLiteral("form-fill");
        row.text = (count == 1 ? Helper::tr("Reset %n form field", count) : Helper::tr("Reset %n form fields", count));
        change.rows.push_back(row);
        change.changeCount = 1;
        change.summary = row.text;
        change.modified = PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags() | PDFModifiedDocument::FormField);
        change.undoDescription = Helper::tr("%1 reset the form").arg(getContext().authorName);
        change.activityDetail.insert(QStringLiteral("count"), count);
        return change;
    }

    bool m_isReset = false;
    std::vector<PDFAppMcpFormValue> m_values;
    QStringList m_resetNames;
};

void PDFAppMcpFormsTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    PDFAppMcpHost* host = invocation.host;
    const std::shared_ptr<PDFMcpCall>& call = invocation.call;
    const QString actionId = invocation.getActionId();

    PDFAppDocument* document = PDFAppMcpChangeJob::prepareDocument(invocation);
    if (!document)
    {
        return;
    }

    // A read only form (O20 locked - refused above, the permission to fill the
    // form, XFA) is never filled by an assistant (plan 3.7)
    const PDFDocument* pdfDocument = document->getDocument().data();
    if (PDFAppFormController::isXFAForm(pdfDocument))
    {
        host->finishWithError(call, actionId, document, QStringLiteral("The form of the document is an XFA form, which PDF4QT shows read only. It cannot be filled."), PDFMcpActivityStatus::Failed);
        return;
    }

    const PDFSecurityHandler* securityHandler = pdfDocument ? pdfDocument->getStorage().getSecurityHandler() : nullptr;
    if (securityHandler && !securityHandler->isAllowed(PDFSecurityHandler::Permission::ModifyFormFields) &&
        !securityHandler->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems))
    {
        host->finishWithError(call, actionId, document, QStringLiteral("The document does not allow filling its form (its security settings)."), PDFMcpActivityStatus::Failed);
        return;
    }

    const QJsonObject& arguments = invocation.actionArguments;
    PDFAppMcpChangeContext context = PDFAppMcpChangeJob::createContext(invocation, document);
    if (invocation.actionName == QLatin1String("reset"))
    {
        QStringList names;
        for (const QJsonValue& value : arguments.value(QStringLiteral("names")).toArray())
        {
            names << PDFMcpJson::sanitizeString(value.toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 1024);
        }
        host->startJob(std::make_shared<PDFAppMcpFormsJob>(std::move(context), true, std::vector<PDFAppMcpFormValue>(), names));
        return;
    }

    std::vector<PDFAppMcpFormValue> values;
    QString error;
    const QJsonArray array = arguments.value(QStringLiteral("values")).toArray();
    for (qsizetype i = 0; i < array.size(); ++i)
    {
        const QJsonObject object = array.at(i).toObject();
        PDFAppMcpFormValue value;
        value.name = PDFMcpJson::sanitizeString(object.value(QStringLiteral("name")).toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 1024);
        if (object.contains(QStringLiteral("value")))
        {
            value.value = object.value(QStringLiteral("value")).toString();
        }
        if (object.contains(QStringLiteral("checked")))
        {
            value.checked = object.value(QStringLiteral("checked")).toBool();
        }
        if (object.contains(QStringLiteral("selected")))
        {
            QStringList selected;
            for (const QJsonValue& item : object.value(QStringLiteral("selected")).toArray())
            {
                selected << item.toString();
            }
            value.selected = selected;
        }

        const int kinds = (value.value ? 1 : 0) + (value.checked ? 1 : 0) + (value.selected ? 1 : 0);
        if (value.name.isEmpty() || kinds != 1)
        {
            error = QStringLiteral("values[%1]: give the name and exactly one of value, checked, selected").arg(i);
            break;
        }
        values.push_back(std::move(value));
    }

    if (error.isEmpty() && values.empty())
    {
        error = QStringLiteral("values: at least one value is needed");
    }
    if (!error.isEmpty())
    {
        host->finishWithError(call, actionId, document, QStringLiteral("Invalid arguments: %1. See pdf4qt://docs/forms.md").arg(error), PDFMcpActivityStatus::Failed);
        return;
    }

    host->startJob(std::make_shared<PDFAppMcpFormsJob>(std::move(context), false, std::move(values), QStringList()));
}

}   // namespace pdf
