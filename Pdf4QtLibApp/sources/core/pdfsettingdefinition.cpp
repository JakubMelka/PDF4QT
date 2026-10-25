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

#include "pdfsettingdefinition.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace pdf
{

QString PDFSettingOption::getTitle() const
{
    if (isTranslated)
    {
        return title;
    }
    return QCoreApplication::translate(PDFSettingDefinition::TRANSLATION_CONTEXT, title.toUtf8().constData());
}

PDFSettingOptionsProvider::~PDFSettingOptionsProvider() = default;

PDFSettingDefinition PDFSettingDefinition::create(QString key, Type type, QVariant defaultValue)
{
    PDFSettingDefinition definition;
    definition.m_key = std::move(key);
    definition.m_type = type;
    definition.m_defaultValue = std::move(defaultValue);
    return definition;
}

PDFSettingDefinition PDFSettingDefinition::createBool(QString key, bool defaultValue)
{
    return create(std::move(key), Type::Bool, QVariant(defaultValue));
}

PDFSettingDefinition PDFSettingDefinition::createInteger(QString key, qint64 defaultValue, qint64 minimum, qint64 maximum)
{
    PDFSettingDefinition definition = create(std::move(key), Type::Integer, QVariant(defaultValue));
    definition.m_minimum = QVariant(qMin(minimum, maximum));
    definition.m_maximum = QVariant(qMax(minimum, maximum));
    return definition;
}

PDFSettingDefinition PDFSettingDefinition::createReal(QString key, double defaultValue, double minimum, double maximum)
{
    PDFSettingDefinition definition = create(std::move(key), Type::Real, QVariant(defaultValue));
    definition.m_minimum = QVariant(qMin(minimum, maximum));
    definition.m_maximum = QVariant(qMax(minimum, maximum));
    return definition;
}

PDFSettingDefinition PDFSettingDefinition::createString(QString key, QString defaultValue, qsizetype maximumLength)
{
    PDFSettingDefinition definition = create(std::move(key), Type::String, QVariant(std::move(defaultValue)));
    definition.m_maximumLength = qMax(qsizetype(0), maximumLength);
    return definition;
}

PDFSettingDefinition PDFSettingDefinition::createOption(QString key, QString defaultValue, std::vector<PDFSettingOption> options)
{
    PDFSettingDefinition definition = create(std::move(key), Type::Option, QVariant(std::move(defaultValue)));
    definition.m_options = std::move(options);
    return definition;
}

PDFSettingDefinition PDFSettingDefinition::createOption(QString key, QString defaultValue, std::shared_ptr<const PDFSettingOptionsProvider> provider)
{
    PDFSettingDefinition definition = create(std::move(key), Type::Option, QVariant(std::move(defaultValue)));
    definition.m_provider = std::move(provider);
    return definition;
}

PDFSettingDefinition& PDFSettingDefinition::setSection(QString section, QString group)
{
    m_section = std::move(section);
    m_group = std::move(group);
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setAnchor(QString anchor)
{
    m_anchor = std::move(anchor);
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setTitle(const char* title)
{
    m_title = title ? title : "";
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setDescription(const char* description)
{
    m_description = description ? description : "";
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setKeywords(QStringList keywords)
{
    m_keywords = std::move(keywords);
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setUnit(const char* unit)
{
    m_unit = unit ? unit : "";
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setDecimals(int decimals)
{
    // A real number never has more decimals than a double can show
    m_decimals = decimals < 0 ? -1 : qMin(decimals, 6);
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setControl(Control control)
{
    m_control = control;
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setEnabledWhen(QString key, QVariant value)
{
    m_enabledWhenKey = std::move(key);
    m_enabledWhenValue = std::move(value);
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setTag(const char* tag)
{
    m_tag = tag ? tag : "";
    return *this;
}

PDFSettingDefinition& PDFSettingDefinition::setCommitOnActivation()
{
    m_isCommittedOnActivation = true;
    return *this;
}

QString PDFSettingDefinition::getUnit() const
{
    return *m_unit ? QCoreApplication::translate(TRANSLATION_CONTEXT, m_unit) : QString();
}

int PDFSettingDefinition::getDecimals() const
{
    if (m_type != Type::Real)
    {
        return 0;
    }
    return m_decimals >= 0 ? m_decimals : 2;
}

QString PDFSettingDefinition::getTag() const
{
    return *m_tag ? QCoreApplication::translate(TRANSLATION_CONTEXT, m_tag) : QString();
}

QString PDFSettingDefinition::getTitle() const
{
    return QCoreApplication::translate(TRANSLATION_CONTEXT, m_title);
}

QString PDFSettingDefinition::getDescription() const
{
    return QCoreApplication::translate(TRANSLATION_CONTEXT, m_description);
}

QString PDFSettingDefinition::getGroupTitle() const
{
    return QCoreApplication::translate(TRANSLATION_CONTEXT, m_group.toUtf8().constData());
}

std::vector<PDFSettingOption> PDFSettingDefinition::getOptions() const
{
    if (m_provider)
    {
        return m_provider->getOptions(*this);
    }
    return m_options;
}

std::optional<QVariant> PDFSettingDefinition::normalize(const QVariant& value) const
{
    if (!value.isValid() || value.isNull())
    {
        return std::nullopt;
    }

    const int typeId = value.typeId();
    const bool isIntegerType = typeId == QMetaType::Int || typeId == QMetaType::UInt || typeId == QMetaType::LongLong ||
                               typeId == QMetaType::ULongLong || typeId == QMetaType::Long || typeId == QMetaType::ULong ||
                               typeId == QMetaType::Short || typeId == QMetaType::UShort;
    const bool isRealType = typeId == QMetaType::Double || typeId == QMetaType::Float;
    const bool isStringType = typeId == QMetaType::QString;

    switch (m_type)
    {
        case Type::Bool:
        {
            if (typeId == QMetaType::Bool)
            {
                return QVariant(value.toBool());
            }
            if (isIntegerType)
            {
                // Stored as 0 / 1, anything else is not a valid value
                const qint64 integer = value.toLongLong();
                if (integer == 0 || integer == 1)
                {
                    return QVariant(integer == 1);
                }
            }
            return std::nullopt;
        }

        case Type::Integer:
        {
            qint64 integer = 0;
            if (isIntegerType)
            {
                if (typeId == QMetaType::ULongLong || typeId == QMetaType::ULong)
                {
                    if (value.toULongLong() > qulonglong(std::numeric_limits<qint64>::max()))
                    {
                        return std::nullopt;
                    }
                }
                integer = value.toLongLong();
            }
            else if (isRealType)
            {
                // A real number with an integral value is accepted (for example from JSON)
                const double real = value.toDouble();
                if (!std::isfinite(real) || std::floor(real) != real || std::abs(real) > 9.0e15)
                {
                    return std::nullopt;
                }
                integer = qint64(real);
            }
            else
            {
                return std::nullopt;
            }

            if (integer < m_minimum.toLongLong() || integer > m_maximum.toLongLong())
            {
                return std::nullopt;
            }
            return QVariant(integer);
        }

        case Type::Real:
        {
            if (!isIntegerType && !isRealType)
            {
                return std::nullopt;
            }

            const double real = value.toDouble();
            if (!std::isfinite(real) || real < m_minimum.toDouble() || real > m_maximum.toDouble())
            {
                return std::nullopt;
            }
            return QVariant(real);
        }

        case Type::String:
        {
            if (!isStringType)
            {
                return std::nullopt;
            }

            const QString text = value.toString();
            if (text.size() > m_maximumLength)
            {
                return std::nullopt;
            }
            return QVariant(text);
        }

        case Type::Option:
        {
            if (!isStringType)
            {
                return std::nullopt;
            }

            const QString id = value.toString();
            for (const PDFSettingOption& option : getOptions())
            {
                if (option.id == id)
                {
                    return QVariant(id);
                }
            }
            return std::nullopt;
        }
    }

    return std::nullopt;
}

QVariant PDFSettingDefinition::validate(const QVariant& storedValue) const
{
    std::optional<QVariant> value = normalize(storedValue);
    if (value)
    {
        return *value;
    }
    return m_defaultValue;
}

bool PDFSettingDefinition::isValidKey(const QString& key)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z][A-Za-z0-9]*\\.[a-z][A-Za-z0-9]*$"));
    return key.size() <= 128 && expression.match(key).hasMatch();
}

bool PDFSettingDefinition::isValid() const
{
    if (!isValidKey(m_key))
    {
        return false;
    }

    // The default value must be a valid value (an option provider may not offer
    // the default on every system, so only its type is checked)
    if (m_type == Type::Option && m_provider)
    {
        return m_defaultValue.typeId() == QMetaType::QString;
    }

    std::optional<QVariant> value = normalize(m_defaultValue);
    return value.has_value();
}

bool PDFSettingRegistry::registerDefinition(PDFSettingDefinition definition)
{
    if (!definition.isValid() || m_index.count(definition.getKey()))
    {
        return false;
    }

    m_index[definition.getKey()] = m_definitions.size();
    m_definitions.push_back(std::move(definition));
    return true;
}

const PDFSettingDefinition* PDFSettingRegistry::findDefinition(const QString& key) const
{
    auto it = m_index.find(key);
    if (it == m_index.end() || it->second >= m_definitions.size())
    {
        return nullptr;
    }
    return &m_definitions[it->second];
}

std::vector<const PDFSettingDefinition*> PDFSettingRegistry::getDefinitions() const
{
    std::vector<const PDFSettingDefinition*> result;
    result.reserve(m_definitions.size());
    for (const PDFSettingDefinition& definition : m_definitions)
    {
        result.push_back(&definition);
    }
    return result;
}

std::vector<const PDFSettingDefinition*> PDFSettingRegistry::getSectionDefinitions(const QString& section) const
{
    std::vector<const PDFSettingDefinition*> result;
    if (section.isEmpty())
    {
        return result;
    }

    for (const PDFSettingDefinition& definition : m_definitions)
    {
        if (definition.getSection() == section)
        {
            result.push_back(&definition);
        }
    }
    return result;
}

QStringList PDFSettingRegistry::getSections() const
{
    QStringList sections;
    for (const PDFSettingDefinition& definition : m_definitions)
    {
        if (!definition.getSection().isEmpty() && !sections.contains(definition.getSection()))
        {
            sections << definition.getSection();
        }
    }
    return sections;
}

}   // namespace pdf
