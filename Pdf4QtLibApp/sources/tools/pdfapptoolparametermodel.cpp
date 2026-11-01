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

#include "pdfapptoolparametermodel.h"
#include "pdfappcontext.h"
#include "pdftoolparameterrepository.h"

#include <QColor>

namespace pdf
{

/// Store of a binder, which writes into the parameter model
class PDFAppToolParameterValueStore : public PDFAppValueStore
{
public:
    explicit PDFAppToolParameterValueStore(PDFAppToolParameterModel* model) :
        m_model(model)
    {

    }

    virtual bool isAvailable() const override
    {
        return !m_model.isNull();
    }

    virtual QVariant readValue(const QString& key) const override
    {
        if (!m_model)
        {
            return QVariant();
        }

        // The binder works with the types of a setting: a color is a text "#rrggbb"
        const QVariant value = m_model->getValue(key.section(QLatin1Char('.'), 1));
        return value.typeId() == QMetaType::QColor ? QVariant(value.value<QColor>().name(QColor::HexRgb)) : value;
    }

    virtual QString writeValue(const QString& key, const QVariant& value) override
    {
        return m_model ? m_model->setValue(key.section(QLatin1Char('.'), 1), value) : QString();
    }

private:
    QPointer<PDFAppToolParameterModel> m_model;
};

PDFAppToolParameterModel::PDFAppToolParameterModel(PDFAppContext* context, QString toolId, PDFDocumentToolParameterDefinitions definitions, QObject* parent) :
    BaseClass(parent),
    m_context(context),
    m_toolId(std::move(toolId)),
    m_definitions(std::move(definitions))
{
    loadLastValues();
}

PDFAppToolParameterModel::~PDFAppToolParameterModel() = default;

void PDFAppToolParameterModel::loadLastValues()
{
    m_values = PDFDocumentToolParameters::getDefaultValues(m_definitions);
    if (!m_context || !m_context->getDatabase())
    {
        return;
    }

    // The repository reads only what is stored (no Secret, no setStored(false)) and
    // validates it by the definitions
    const QVariantMap storedValues = PDFToolParameterRepository(m_context->getDatabase()).getValues(m_toolId, m_definitions);
    for (auto it = storedValues.cbegin(); it != storedValues.cend(); ++it)
    {
        m_values.insert(it.key(), it.value());
    }
}

QString PDFAppToolParameterModel::setValue(const QString& name, const QVariant& value)
{
    QVariantMap values;
    values.insert(name, value);
    return setValues(values);
}

QString PDFAppToolParameterModel::setValues(const QVariantMap& values)
{
    QVariantMap newValues = m_values;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(m_definitions, it.key());
        if (!definition)
        {
            return tr("Unknown parameter.");
        }

        QVariant normalizedValue;
        const QString error = definition->normalize(it.value(), &normalizedValue);
        if (!error.isEmpty())
        {
            return error;
        }
        newValues.insert(it.key(), normalizedValue);
    }

    if (newValues == m_values)
    {
        return QString();
    }

    m_undoStack.push_back(m_values);
    if (m_undoStack.size() > size_t(MAXIMUM_HISTORY))
    {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
    applyValues(newValues);
    return QString();
}

QString PDFAppToolParameterModel::setFollowingValue(const QString& name, const QVariant& value)
{
    const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(m_definitions, name);
    if (!definition)
    {
        return tr("Unknown parameter.");
    }

    QVariant normalizedValue;
    const QString error = definition->normalize(value, &normalizedValue);
    if (error.isEmpty() && normalizedValue != m_values.value(name))
    {
        QVariantMap newValues = m_values;
        newValues.insert(name, normalizedValue);
        applyValues(newValues);
    }
    return error;
}

std::vector<PDFDocumentToolParameterError> PDFAppToolParameterModel::getErrors() const
{
    QVariantMap normalizedValues;
    return PDFDocumentToolParameters::normalize(m_definitions, m_values, &normalizedValues);
}

void PDFAppToolParameterModel::undo()
{
    if (m_undoStack.empty())
    {
        return;
    }

    QVariantMap values = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    m_redoStack.push_back(m_values);
    applyValues(values);
}

void PDFAppToolParameterModel::redo()
{
    if (m_redoStack.empty())
    {
        return;
    }

    QVariantMap values = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    m_undoStack.push_back(m_values);
    applyValues(values);
}

void PDFAppToolParameterModel::applyValues(const QVariantMap& values)
{
    const QVariantMap oldValues = m_values;
    m_values = values;

    // The slots may delete the model
    QPointer<PDFAppToolParameterModel> guard(this);
    Q_EMIT undoStateChanged();
    if (!guard)
    {
        return;
    }

    // A copy: the definitions die with the model
    const PDFDocumentToolParameterDefinitions definitions = m_definitions;
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        const QString name = definition.getName();
        if (oldValues.value(name) != values.value(name))
        {
            Q_EMIT changed(name);
            if (!guard)
            {
                return;
            }
        }
    }
}

PDFDatabaseError PDFAppToolParameterModel::storeLastValues()
{
    if (!m_context || !m_context->getDatabase())
    {
        return PDFDatabaseError();
    }

    return PDFToolParameterRepository(m_context->getDatabase()).setValues(m_toolId, m_definitions, m_values);
}

void PDFAppToolParameterModel::updateDefinitionTexts(PDFDocumentToolParameterDefinitions definitions)
{
    m_definitions = std::move(definitions);
}

std::optional<PDFSettingDefinition> PDFAppToolParameterModel::toSettingDefinition(const QString& name) const
{
    const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(m_definitions, name);
    const QString key = QStringLiteral("tool.") + name;
    if (!definition || !PDFSettingDefinition::isValidKey(key))
    {
        return std::nullopt;
    }

    const QVariant defaultValue = definition->getDefaultValue();
    switch (definition->getType())
    {
        case PDFDocumentToolParameterType::Bool:
            return PDFSettingDefinition::createBool(key, defaultValue.toBool());

        case PDFDocumentToolParameterType::Integer:
            return PDFSettingDefinition::createInteger(key, defaultValue.toLongLong(), definition->getIntegerMinimum(), definition->getIntegerMaximum());

        case PDFDocumentToolParameterType::Real:
            return PDFSettingDefinition::createReal(key, defaultValue.toDouble(), definition->getRealMinimum(), definition->getRealMaximum());

        case PDFDocumentToolParameterType::Enum:
        {
            std::vector<PDFSettingOption> options;
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                PDFSettingOption settingOption;
                settingOption.id = option.id;
                settingOption.title = option.title;
                settingOption.isTranslated = true;
                options.push_back(std::move(settingOption));
            }
            return PDFSettingDefinition::createOption(key, defaultValue.toString(), std::move(options));
        }

        case PDFDocumentToolParameterType::Color:
        {
            PDFSettingDefinition setting = PDFSettingDefinition::createString(key, defaultValue.value<QColor>().name(QColor::HexRgb), 7);
            setting.setControl(PDFSettingDefinition::Control::Color);
            return setting;
        }

        case PDFDocumentToolParameterType::String:
        case PDFDocumentToolParameterType::FilePath:
        case PDFDocumentToolParameterType::FolderPath:
        case PDFDocumentToolParameterType::FileNameTemplate:
            return PDFSettingDefinition::createString(key, defaultValue.toString(), definition->getMaximumLength());

        case PDFDocumentToolParameterType::PageSelection:
        case PDFDocumentToolParameterType::Secret:
            break;
    }
    return std::nullopt;
}

std::shared_ptr<PDFAppValueStore> PDFAppToolParameterModel::createValueStore()
{
    return std::make_shared<PDFAppToolParameterValueStore>(this);
}

}   // namespace pdf

#include "moc_pdfapptoolparametermodel.cpp"
