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

#ifndef PDFMCPSCHEMA_H
#define PDFMCPSCHEMA_H

#include "pdf4qtlibmcp_export.h"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace pdf
{

class PDFMcpJsonWriter;

/// JSON Schema of a tool input, a tool output, an elicitation or a parameter
/// (plan of step 7, 3.0): a tree of nodes, whose keywords are written in a
/// fixed order (type, description, enum, minimum, maximum, maxLength, pattern,
/// items, minItems, maxItems, uniqueItems, properties, required,
/// additionalProperties, default, readOnly, writeOnly) and whose properties keep
/// the order of addProperty - the bytes are deterministic (prompt caching) and a
/// model sees "action" first. Only keywords valid in draft-07 and 2020-12 are
/// used; never oneOf / anyOf / allOf / $ref / const and never a list of types.
///
/// The same tree validates the arguments of a call (validate): the errors name
/// the JSON path of the value ("items[2].quads: expected an array").
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSchema
{
public:
    enum class Type
    {
        Object,
        Array,
        String,
        Integer,
        Number,
        Boolean
    };

    PDFMcpSchema() = default;
    explicit PDFMcpSchema(Type type, const QString& description = QString());

    /// Keywords, which a schema of the server may contain (the contract test
    /// checks every schema against this list)
    static QStringList getAllowedKeywords();

    Type getType() const { return m_type; }
    const QString& getTitle() const { return m_title; }
    const QString& getDescription() const { return m_description; }
    const QStringList& getEnum() const { return m_enum; }
    const std::vector<std::pair<QString, PDFMcpSchema>>& getProperties() const { return m_properties; }
    const QStringList& getRequired() const { return m_required; }
    const PDFMcpSchema* getItems() const { return m_items.empty() ? nullptr : &m_items.front(); }

    /// Property of the name, nullptr = none
    const PDFMcpSchema* findProperty(const QString& name) const;
    PDFMcpSchema* findProperty(const QString& name);

    PDFMcpSchema& setTitle(const QString& title) { m_title = title; return *this; }
    PDFMcpSchema& setDescription(const QString& description) { m_description = description; return *this; }
    PDFMcpSchema& setEnum(const QStringList& values) { m_enum = values; return *this; }
    PDFMcpSchema& setMinimum(double minimum) { m_minimum = minimum; return *this; }
    PDFMcpSchema& setMaximum(double maximum) { m_maximum = maximum; return *this; }
    PDFMcpSchema& setMaximumLength(qint64 maximumLength) { m_maximumLength = maximumLength; return *this; }
    PDFMcpSchema& setPattern(const QString& pattern) { m_pattern = pattern; return *this; }
    PDFMcpSchema& setItems(const PDFMcpSchema& items);
    PDFMcpSchema& setMinimumItems(qint64 minimumItems) { m_minimumItems = minimumItems; return *this; }
    PDFMcpSchema& setMaximumItems(qint64 maximumItems) { m_maximumItems = maximumItems; return *this; }
    PDFMcpSchema& setUniqueItems(bool uniqueItems) { m_uniqueItems = uniqueItems; return *this; }
    PDFMcpSchema& setAdditionalProperties(bool additionalProperties) { m_additionalProperties = additionalProperties; return *this; }
    PDFMcpSchema& setDefault(const QJsonValue& value) { m_default = value; return *this; }
    PDFMcpSchema& setReadOnly(bool readOnly) { m_readOnly = readOnly; return *this; }
    PDFMcpSchema& setWriteOnly(bool writeOnly) { m_writeOnly = writeOnly; return *this; }

    /// Adds (or replaces) a property of an object; \p required adds it to "required"
    PDFMcpSchema& addProperty(const QString& name, const PDFMcpSchema& schema, bool required = false);

    /// Removes a property (and its entry of "required")
    void removeProperty(const QString& name);

    /// Writes the schema as a JSON object
    void write(PDFMcpJsonWriter& writer) const;

    /// Serialized schema (compact, deterministic)
    QByteArray toJson() const;

    /// The schema as a Qt object (for comparisons; the order is lost)
    QJsonObject toQJson() const;

    /// Schema of a Qt object (the schemas of Pdf4QtLibCore:
    /// PDFDocumentToolParameters::toJsonSchema): only the allowed keywords and a
    /// single type are accepted; std::nullopt and the reason in \p errorMessage
    /// otherwise
    static std::optional<PDFMcpSchema> fromQJson(const QJsonObject& object, QString* errorMessage);

    /// Validates the value. \p path is the path of the value ("arguments"; empty =
    /// the root); errors are appended to \p errors (at most \p maximumErrors).
    /// \returns true when the value is valid.
    bool validate(const QJsonValue& value, const QString& path, QStringList* errors, int maximumErrors = 8) const;

private:
    static QString getTypeName(Type type);
    static QString childPath(const QString& path, const QString& name);
    void addError(const QString& path, const QString& message, QStringList* errors, int maximumErrors) const;

    Type m_type = Type::Object;
    QString m_title;
    QString m_description;
    QStringList m_enum;
    std::optional<double> m_minimum;
    std::optional<double> m_maximum;
    std::optional<qint64> m_maximumLength;
    QString m_pattern;
    std::vector<PDFMcpSchema> m_items;     ///< Zero or one schema of the items
    std::optional<qint64> m_minimumItems;
    std::optional<qint64> m_maximumItems;
    bool m_uniqueItems = false;
    std::vector<std::pair<QString, PDFMcpSchema>> m_properties;
    QStringList m_required;
    std::optional<bool> m_additionalProperties;
    std::optional<QJsonValue> m_default;
    bool m_readOnly = false;
    bool m_writeOnly = false;
};

/// Factory of the nodes of PDFMcpSchema (the vocabulary of the tool contracts)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSchemaBuilder
{
public:
    PDFMcpSchemaBuilder() = delete;

    /// Object; \p isClosed = "additionalProperties": false (every input object)
    static PDFMcpSchema object(const QString& description = QString(), bool isClosed = true);

    /// String of at most \p maximumLength characters (0 = no limit)
    static PDFMcpSchema string(const QString& description, qint64 maximumLength = 0);

    /// String, which is one of \p values
    static PDFMcpSchema enumeration(const QString& description, const QStringList& values);

    static PDFMcpSchema integer(const QString& description, qint64 minimum, qint64 maximum);
    static PDFMcpSchema number(const QString& description, double minimum, double maximum);
    static PDFMcpSchema boolean(const QString& description);

    /// Array of the items, \p minimumItems / \p maximumItems (negative = none)
    static PDFMcpSchema array(const QString& description, const PDFMcpSchema& items, qint64 minimumItems = -1, qint64 maximumItems = -1);

    /// Integer of an output (no bounds)
    static PDFMcpSchema outputInteger(const QString& description);

    /// Number of an output (no bounds)
    static PDFMcpSchema outputNumber(const QString& description);
};

}   // namespace pdf

#endif // PDFMCPSCHEMA_H
