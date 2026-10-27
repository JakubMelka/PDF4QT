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

#include "pdfmcpschema.h"
#include "pdfmcpjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace pdf
{

/// Helpers of the schema
class PDFMcpSchemaHelper
{
public:
    PDFMcpSchemaHelper() = delete;

    static std::optional<PDFMcpSchema> fail(QString* errorMessage, const QString& message)
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return std::nullopt;
    }

    /// 2^63 - the first double above every qint64
    static constexpr double INTEGER_LIMIT = 9223372036854775808.0;

    /// \p value < \p bound exactly; a bound outside of qint64 (a schema of the
    /// parameters of a tool of Pdf4QtLibCore with the limits of qint64 - its
    /// maximum rounds to 2^63) is never converted to qint64
    static bool isIntegerBelow(qint64 value, double bound)
    {
        if (std::isnan(bound) || bound <= -INTEGER_LIMIT)
        {
            return false;
        }
        if (bound >= INTEGER_LIMIT)
        {
            return true;
        }
        return value < qint64(std::ceil(bound));
    }

    /// \p value > \p bound exactly (see isIntegerBelow)
    static bool isIntegerAbove(qint64 value, double bound)
    {
        if (std::isnan(bound) || bound >= INTEGER_LIMIT)
        {
            return false;
        }
        if (bound < -INTEGER_LIMIT)
        {
            return true;
        }
        return value > qint64(std::floor(bound));
    }

    /// Text of a bound (a whole bound without an exponent while it is exact)
    static QString formatBound(double bound)
    {
        if (std::abs(bound) <= 9007199254740992.0 && std::floor(bound) == bound)
        {
            return QString::number(qint64(bound));
        }
        return QString::number(bound, 'g', QLocale::FloatingPointShortest);
    }

    /// The message of a value out of its range: a missing bound is not named
    static QString getRangeMessage(const std::optional<double>& minimum, const std::optional<double>& maximum)
    {
        if (minimum && maximum)
        {
            return QStringLiteral("must be from %1 to %2").arg(formatBound(*minimum), formatBound(*maximum));
        }
        if (minimum)
        {
            return QStringLiteral("must be at least %1").arg(formatBound(*minimum));
        }
        return QStringLiteral("must be at most %1").arg(formatBound(maximum.value_or(0.0)));
    }
};

PDFMcpSchema::PDFMcpSchema(Type type, const QString& description) :
    m_type(type),
    m_description(description)
{

}

QStringList PDFMcpSchema::getAllowedKeywords()
{
    // Draft-07 and 2020-12; "title", "pattern" and "uniqueItems" are used by the
    // schemas of the parameters of the tools of Pdf4QtLibCore
    return { QStringLiteral("type"), QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("enum"),
             QStringLiteral("minimum"), QStringLiteral("maximum"), QStringLiteral("maxLength"), QStringLiteral("pattern"),
             QStringLiteral("items"), QStringLiteral("minItems"), QStringLiteral("maxItems"), QStringLiteral("uniqueItems"),
             QStringLiteral("properties"), QStringLiteral("required"), QStringLiteral("additionalProperties"),
             QStringLiteral("default"), QStringLiteral("readOnly"), QStringLiteral("writeOnly") };
}

const PDFMcpSchema* PDFMcpSchema::findProperty(const QString& name) const
{
    for (const auto& property : m_properties)
    {
        if (property.first == name)
        {
            return &property.second;
        }
    }
    return nullptr;
}

PDFMcpSchema* PDFMcpSchema::findProperty(const QString& name)
{
    for (auto& property : m_properties)
    {
        if (property.first == name)
        {
            return &property.second;
        }
    }
    return nullptr;
}

PDFMcpSchema& PDFMcpSchema::setItems(const PDFMcpSchema& items)
{
    m_items.clear();
    m_items.push_back(items);
    return *this;
}

PDFMcpSchema& PDFMcpSchema::addProperty(const QString& name, const PDFMcpSchema& schema, bool required)
{
    if (PDFMcpSchema* existing = findProperty(name))
    {
        *existing = schema;
    }
    else
    {
        m_properties.emplace_back(name, schema);
    }

    if (required && !m_required.contains(name))
    {
        m_required.append(name);
    }
    return *this;
}

void PDFMcpSchema::removeProperty(const QString& name)
{
    for (auto it = m_properties.begin(); it != m_properties.end(); ++it)
    {
        if (it->first == name)
        {
            m_properties.erase(it);
            break;
        }
    }
    m_required.removeAll(name);
}

QString PDFMcpSchema::getTypeName(Type type)
{
    switch (type)
    {
        case Type::Object:
            return QStringLiteral("object");
        case Type::Array:
            return QStringLiteral("array");
        case Type::String:
            return QStringLiteral("string");
        case Type::Integer:
            return QStringLiteral("integer");
        case Type::Number:
            return QStringLiteral("number");
        case Type::Boolean:
            return QStringLiteral("boolean");
    }
    return QStringLiteral("object");
}

void PDFMcpSchema::write(PDFMcpJsonWriter& writer) const
{
    writer.beginObject();
    writer.writeMember(QLatin1StringView("type"), getTypeName(m_type));
    if (!m_title.isEmpty())
    {
        writer.writeMember(QLatin1StringView("title"), m_title);
    }
    if (!m_description.isEmpty())
    {
        writer.writeMember(QLatin1StringView("description"), m_description);
    }
    if (!m_enum.isEmpty())
    {
        writer.key(QLatin1StringView("enum"));
        writer.writeStringArray(m_enum);
    }

    const bool isInteger = m_type == Type::Integer;
    if (m_minimum)
    {
        writer.key(QLatin1StringView("minimum"));
        isInteger ? writer.writeInteger(qint64(*m_minimum)) : writer.writeNumber(*m_minimum);
    }
    if (m_maximum)
    {
        writer.key(QLatin1StringView("maximum"));
        isInteger ? writer.writeInteger(qint64(*m_maximum)) : writer.writeNumber(*m_maximum);
    }
    if (m_maximumLength)
    {
        writer.writeMember(QLatin1StringView("maxLength"), *m_maximumLength);
    }
    if (!m_pattern.isEmpty())
    {
        writer.writeMember(QLatin1StringView("pattern"), m_pattern);
    }
    if (const PDFMcpSchema* items = getItems())
    {
        writer.key(QLatin1StringView("items"));
        items->write(writer);
    }
    if (m_minimumItems)
    {
        writer.writeMember(QLatin1StringView("minItems"), *m_minimumItems);
    }
    if (m_maximumItems)
    {
        writer.writeMember(QLatin1StringView("maxItems"), *m_maximumItems);
    }
    if (m_uniqueItems)
    {
        writer.writeMember(QLatin1StringView("uniqueItems"), true);
    }
    if (m_type == Type::Object)
    {
        writer.key(QLatin1StringView("properties"));
        writer.beginObject();
        for (const auto& property : m_properties)
        {
            writer.key(QStringView(property.first));
            property.second.write(writer);
        }
        writer.endObject();

        if (!m_required.isEmpty())
        {
            writer.key(QLatin1StringView("required"));
            writer.writeStringArray(m_required);
        }
    }
    if (m_additionalProperties)
    {
        writer.writeMember(QLatin1StringView("additionalProperties"), *m_additionalProperties);
    }
    if (m_default)
    {
        writer.key(QLatin1StringView("default"));
        writer.writeQJson(*m_default);
    }
    if (m_readOnly)
    {
        writer.writeMember(QLatin1StringView("readOnly"), true);
    }
    if (m_writeOnly)
    {
        writer.writeMember(QLatin1StringView("writeOnly"), true);
    }
    writer.endObject();
}

QByteArray PDFMcpSchema::toJson() const
{
    PDFMcpJsonWriter writer;
    write(writer);
    return writer.take();
}

QJsonObject PDFMcpSchema::toQJson() const
{
    return QJsonDocument::fromJson(toJson()).object();
}

std::optional<PDFMcpSchema> PDFMcpSchema::fromQJson(const QJsonObject& object, QString* errorMessage)
{
    const QStringList allowed = getAllowedKeywords();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
        {
            return PDFMcpSchemaHelper::fail(errorMessage, QStringLiteral("The keyword '%1' is not allowed").arg(it.key()));
        }
    }

    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    if (!typeValue.isString())
    {
        return PDFMcpSchemaHelper::fail(errorMessage, QStringLiteral("A schema needs a single type"));
    }

    const QString typeName = typeValue.toString();
    std::optional<Type> type;
    for (Type candidate : { Type::Object, Type::Array, Type::String, Type::Integer, Type::Number, Type::Boolean })
    {
        if (getTypeName(candidate) == typeName)
        {
            type = candidate;
        }
    }
    if (!type)
    {
        return PDFMcpSchemaHelper::fail(errorMessage, QStringLiteral("Unknown type '%1'").arg(typeName));
    }

    PDFMcpSchema schema(*type, object.value(QStringLiteral("description")).toString());
    schema.m_title = object.value(QStringLiteral("title")).toString();
    if (object.contains(QStringLiteral("enum")))
    {
        const QJsonArray values = object.value(QStringLiteral("enum")).toArray();
        for (const QJsonValue& value : values)
        {
            if (!value.isString())
            {
                return PDFMcpSchemaHelper::fail(errorMessage, QStringLiteral("Only string enumerations are supported"));
            }
            schema.m_enum.append(value.toString());
        }
    }
    if (object.contains(QStringLiteral("minimum")))
    {
        schema.m_minimum = object.value(QStringLiteral("minimum")).toDouble();
    }
    if (object.contains(QStringLiteral("maximum")))
    {
        schema.m_maximum = object.value(QStringLiteral("maximum")).toDouble();
    }
    if (object.contains(QStringLiteral("maxLength")))
    {
        schema.m_maximumLength = object.value(QStringLiteral("maxLength")).toInteger();
    }
    if (object.contains(QStringLiteral("pattern")))
    {
        schema.m_pattern = object.value(QStringLiteral("pattern")).toString();
        if (!QRegularExpression(schema.m_pattern).isValid())
        {
            return PDFMcpSchemaHelper::fail(errorMessage, QStringLiteral("Invalid pattern"));
        }
    }
    if (object.contains(QStringLiteral("items")))
    {
        std::optional<PDFMcpSchema> items = fromQJson(object.value(QStringLiteral("items")).toObject(), errorMessage);
        if (!items)
        {
            return std::nullopt;
        }
        schema.setItems(*items);
    }
    if (object.contains(QStringLiteral("minItems")))
    {
        schema.m_minimumItems = object.value(QStringLiteral("minItems")).toInteger();
    }
    if (object.contains(QStringLiteral("maxItems")))
    {
        schema.m_maximumItems = object.value(QStringLiteral("maxItems")).toInteger();
    }
    schema.m_uniqueItems = object.value(QStringLiteral("uniqueItems")).toBool(false);

    if (object.contains(QStringLiteral("properties")))
    {
        // The order of Qt (sorted keys) - the schemas of the core are written by Qt too
        const QJsonObject properties = object.value(QStringLiteral("properties")).toObject();
        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
        {
            std::optional<PDFMcpSchema> property = fromQJson(it.value().toObject(), errorMessage);
            if (!property)
            {
                return std::nullopt;
            }
            schema.m_properties.emplace_back(it.key(), *property);
        }
    }
    if (object.contains(QStringLiteral("required")))
    {
        const QJsonArray required = object.value(QStringLiteral("required")).toArray();
        for (const QJsonValue& value : required)
        {
            schema.m_required.append(value.toString());
        }
    }
    if (object.contains(QStringLiteral("additionalProperties")))
    {
        const QJsonValue value = object.value(QStringLiteral("additionalProperties"));
        if (!value.isBool())
        {
            return PDFMcpSchemaHelper::fail(errorMessage, QStringLiteral("additionalProperties must be a boolean"));
        }
        schema.m_additionalProperties = value.toBool();
    }
    if (object.contains(QStringLiteral("default")))
    {
        schema.m_default = object.value(QStringLiteral("default"));
    }
    schema.m_readOnly = object.value(QStringLiteral("readOnly")).toBool(false);
    schema.m_writeOnly = object.value(QStringLiteral("writeOnly")).toBool(false);
    return schema;
}

QString PDFMcpSchema::childPath(const QString& path, const QString& name)
{
    const QString safeName = PDFMcpJson::sanitizeString(name, PDFMcpJson::SanitizeFlag::RemoveBidi, 64);
    return path.isEmpty() ? safeName : path + QLatin1Char('.') + safeName;
}

void PDFMcpSchema::addError(const QString& path, const QString& message, QStringList* errors, int maximumErrors) const
{
    if (errors && errors->size() < maximumErrors)
    {
        errors->append(path.isEmpty() ? message : QStringLiteral("%1: %2").arg(path, message));
    }
}

bool PDFMcpSchema::validate(const QJsonValue& value, const QString& path, QStringList* errors, int maximumErrors) const
{
    switch (m_type)
    {
        case Type::Object:
        {
            if (!value.isObject())
            {
                addError(path, QStringLiteral("expected an object"), errors, maximumErrors);
                return false;
            }

            bool isValid = true;
            const QJsonObject object = value.toObject();
            for (const QString& name : m_required)
            {
                if (!object.contains(name))
                {
                    addError(childPath(path, name), QStringLiteral("is required"), errors, maximumErrors);
                    isValid = false;
                }
            }

            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            {
                if (const PDFMcpSchema* property = findProperty(it.key()))
                {
                    isValid = property->validate(it.value(), childPath(path, it.key()), errors, maximumErrors) && isValid;
                }
                else if (m_additionalProperties.has_value() && !*m_additionalProperties)
                {
                    addError(childPath(path, it.key()), QStringLiteral("unknown property"), errors, maximumErrors);
                    isValid = false;
                }
            }
            return isValid;
        }

        case Type::Array:
        {
            if (!value.isArray())
            {
                addError(path, QStringLiteral("expected an array"), errors, maximumErrors);
                return false;
            }

            const QJsonArray array = value.toArray();
            bool isValid = true;
            if (m_minimumItems && array.size() < *m_minimumItems)
            {
                addError(path, QStringLiteral("needs at least %1 items").arg(*m_minimumItems), errors, maximumErrors);
                isValid = false;
            }
            if (m_maximumItems && array.size() > *m_maximumItems)
            {
                addError(path, QStringLiteral("has more than %1 items").arg(*m_maximumItems), errors, maximumErrors);
                return false;
            }

            if (const PDFMcpSchema* items = getItems())
            {
                for (qsizetype i = 0; i < array.size(); ++i)
                {
                    if (!items->validate(array[i], QStringLiteral("%1[%2]").arg(path).arg(i), errors, maximumErrors))
                    {
                        isValid = false;
                        if (errors && errors->size() >= maximumErrors)
                        {
                            break;
                        }
                    }
                }
            }

            // Linear: the canonical compact JSON of each item in a set (keys of an
            // object are sorted, 1 and 1.0 are one number). A pairwise comparison
            // of an array of 100 000 items ran in the GUI thread for minutes.
            if (m_uniqueItems && isValid)
            {
                QSet<QByteArray> items;
                items.reserve(array.size());
                for (const QJsonValue& item : array)
                {
                    const QByteArray key = QJsonDocument(QJsonArray({ item })).toJson(QJsonDocument::Compact);
                    if (items.contains(key))
                    {
                        addError(path, QStringLiteral("has duplicate items"), errors, maximumErrors);
                        return false;
                    }
                    items.insert(key);
                }
            }
            return isValid;
        }

        case Type::String:
        {
            if (!value.isString())
            {
                addError(path, QStringLiteral("expected a string"), errors, maximumErrors);
                return false;
            }

            const QString text = value.toString();
            if (m_maximumLength)
            {
                // Characters are code points (JSON Schema)
                qsizetype length = 0;
                for (qsizetype i = 0; i < text.size(); ++i)
                {
                    if (!(text[i].isLowSurrogate() && i > 0 && text[i - 1].isHighSurrogate()))
                    {
                        ++length;
                    }
                }
                if (length > *m_maximumLength)
                {
                    addError(path, QStringLiteral("is longer than %1 characters").arg(*m_maximumLength), errors, maximumErrors);
                    return false;
                }
            }

            if (!m_enum.isEmpty() && !m_enum.contains(text))
            {
                addError(path, QStringLiteral("must be one of: %1").arg(m_enum.join(QStringLiteral(", "))), errors, maximumErrors);
                return false;
            }

            if (!m_pattern.isEmpty() && !QRegularExpression(m_pattern).match(text).hasMatch())
            {
                addError(path, QStringLiteral("does not match the pattern %1").arg(m_pattern), errors, maximumErrors);
                return false;
            }
            return true;
        }

        case Type::Integer:
        case Type::Number:
        {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
            {
                addError(path, m_type == Type::Integer ? QStringLiteral("expected an integer") : QStringLiteral("expected a number"), errors, maximumErrors);
                return false;
            }

            const double number = value.toDouble();
            if (m_type == Type::Integer)
            {
                const qint64 first = value.toInteger(0);
                const qint64 second = value.toInteger(1);
                if (first != second)
                {
                    addError(path, QStringLiteral("expected an integer"), errors, maximumErrors);
                    return false;
                }

                // Exact comparison of integers (no rounding of big values)
                if ((m_minimum && PDFMcpSchemaHelper::isIntegerBelow(first, *m_minimum)) || (m_maximum && PDFMcpSchemaHelper::isIntegerAbove(first, *m_maximum)))
                {
                    addError(path, PDFMcpSchemaHelper::getRangeMessage(m_minimum, m_maximum), errors, maximumErrors);
                    return false;
                }
                return true;
            }

            if ((m_minimum && number < *m_minimum) || (m_maximum && number > *m_maximum))
            {
                addError(path, PDFMcpSchemaHelper::getRangeMessage(m_minimum, m_maximum), errors, maximumErrors);
                return false;
            }
            return true;
        }

        case Type::Boolean:
            if (!value.isBool())
            {
                addError(path, QStringLiteral("expected a boolean"), errors, maximumErrors);
                return false;
            }
            return true;
    }

    return false;
}

PDFMcpSchema PDFMcpSchemaBuilder::object(const QString& description, bool isClosed)
{
    PDFMcpSchema schema(PDFMcpSchema::Type::Object, description);
    if (isClosed)
    {
        schema.setAdditionalProperties(false);
    }
    return schema;
}

PDFMcpSchema PDFMcpSchemaBuilder::string(const QString& description, qint64 maximumLength)
{
    PDFMcpSchema schema(PDFMcpSchema::Type::String, description);
    if (maximumLength > 0)
    {
        schema.setMaximumLength(maximumLength);
    }
    return schema;
}

PDFMcpSchema PDFMcpSchemaBuilder::enumeration(const QString& description, const QStringList& values)
{
    PDFMcpSchema schema(PDFMcpSchema::Type::String, description);
    schema.setEnum(values);
    return schema;
}

PDFMcpSchema PDFMcpSchemaBuilder::integer(const QString& description, qint64 minimum, qint64 maximum)
{
    PDFMcpSchema schema(PDFMcpSchema::Type::Integer, description);
    schema.setMinimum(double(minimum));
    schema.setMaximum(double(maximum));
    return schema;
}

PDFMcpSchema PDFMcpSchemaBuilder::number(const QString& description, double minimum, double maximum)
{
    PDFMcpSchema schema(PDFMcpSchema::Type::Number, description);
    schema.setMinimum(minimum);
    schema.setMaximum(maximum);
    return schema;
}

PDFMcpSchema PDFMcpSchemaBuilder::boolean(const QString& description)
{
    return PDFMcpSchema(PDFMcpSchema::Type::Boolean, description);
}

PDFMcpSchema PDFMcpSchemaBuilder::array(const QString& description, const PDFMcpSchema& items, qint64 minimumItems, qint64 maximumItems)
{
    PDFMcpSchema schema(PDFMcpSchema::Type::Array, description);
    schema.setItems(items);
    if (minimumItems >= 0)
    {
        schema.setMinimumItems(minimumItems);
    }
    if (maximumItems >= 0)
    {
        schema.setMaximumItems(maximumItems);
    }
    return schema;
}

PDFMcpSchema PDFMcpSchemaBuilder::outputInteger(const QString& description)
{
    return PDFMcpSchema(PDFMcpSchema::Type::Integer, description);
}

PDFMcpSchema PDFMcpSchemaBuilder::outputNumber(const QString& description)
{
    return PDFMcpSchema(PDFMcpSchema::Type::Number, description);
}

}   // namespace pdf
