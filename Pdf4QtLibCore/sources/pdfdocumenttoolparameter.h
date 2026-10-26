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

#ifndef PDFDOCUMENTTOOLPARAMETER_H
#define PDFDOCUMENTTOOLPARAMETER_H

#include "pdfglobal.h"

#include <QColor>
#include <QFlags>
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace pdf
{

class PDFDocument;

// Parameters of the tools of phase 2 (newapp_tools.md 2, plan of step 6,
// chapter 3). PROVISIONAL: the interface is minimal and may change until the
// pilot tool Compress of phase 2 is implemented. Everything here is pure data
// and pure functions (no user interface, no global state), usable from any
// thread; the texts of the errors are translated (context
// "pdf::PDFDocumentToolParameterDefinition").

/// Type of a parameter of a tool. The canonical value of each type (what
/// PDFDocumentToolParameterDefinition::normalize returns and what a tool gets):
///   Bool - bool; Integer - qint64; Real - double (finite);
///   String, Enum (the id of the option), FilePath, FolderPath,
///   FileNameTemplate, Secret - QString;
///   Color - QColor (opaque, written as "#rrggbb" in JSON and in the database);
///   PageSelection - QVariantMap { "pages": QString, "filters": QStringList }
///   (PDFDocumentToolPageSelection::toVariant).
enum class PDFDocumentToolParameterType
{
    Bool,
    Integer,
    Real,
    String,
    Enum,
    PageSelection,
    Color,
    FilePath,
    FolderPath,
    FileNameTemplate,
    Secret              ///< A password or a key: never stored, never echoed (JSON Schema writeOnly)
};

/// Option of a parameter of the type Enum
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolParameterOption
{
public:
    QString id;             ///< Value of the option (JSON, database): [a-z0-9] with '-' / '_' inside, at most 64 characters
    QString title;          ///< Translated title shown in the user interface
    QString description;    ///< Translated description (optional)
    QString iconName;       ///< Icon of Pdf4QtLibFluent/resources/icons (optional)
};

/// Selection of the pages of a parameter of the type PageSelection: the range
/// syntax of PDFClosedIntervalSet::parse / PDFPageRange (pages from 1, "1-3, 5,
/// 8-"; empty = all pages) and the filters of the component Page selection.
/// Filters of one kind are joined (Even + Odd = every page), the two kinds narrow
/// each other (Odd + Landscape = odd landscape pages). A page is landscape, when
/// its visible box (the crop box rotated by /Rotate) is wider than high.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolPageSelection
{
public:
    enum class Filter
    {
        None = 0x00,
        Even = 0x01,
        Odd = 0x02,
        Portrait = 0x04,
        Landscape = 0x08
    };
    Q_DECLARE_FLAGS(Filters, Filter)

    /// Longest page range text
    static constexpr int MAXIMUM_PAGES_LENGTH = 4096;

    /// Highest page number of a range, which is checked without a document
    static constexpr PDFInteger MAXIMUM_PAGE_NUMBER = 1'000'000'000;

    QString pages;          ///< Range text, empty = all pages
    Filters filters;        ///< Filters of the component Page selection

    bool operator==(const PDFDocumentToolPageSelection& other) const { return pages == other.pages && filters == other.filters; }

    /// Canonical value { "pages": text, "filters": [ "even", "odd", "portrait", "landscape" ] }
    QVariant toVariant() const;

    /// Selection from a canonical value, from a JSON object converted to a variant
    /// or from a plain range text (no filters). Untrusted input: a wrong type, an
    /// unknown key or filter, a text longer than MAXIMUM_PAGES_LENGTH or a range,
    /// which is not valid for pages 1 to MAXIMUM_PAGE_NUMBER, gives std::nullopt
    /// and the translated reason in \p errorMessage.
    static std::optional<PDFDocumentToolPageSelection> fromVariant(const QVariant& value, QString* errorMessage);

    /// Indices of the pages (from 0) of a document with the pages of the given
    /// visible sizes, in the order of the document. A range outside of the pages,
    /// a selection without any page gives an empty list and the translated reason.
    std::vector<PDFInteger> getPageIndices(const std::vector<QSizeF>& pageSizes, QString* errorMessage) const;

    /// Indices of the pages (from 0) of the document (its visible page sizes)
    std::vector<PDFInteger> getPageIndices(const PDFDocument* document, QString* errorMessage) const;

    /// Names of the filters in JSON ("even", "odd", "portrait", "landscape")
    static QStringList getFilterNames();
};

/// Error of a parameter (validation): the name of the parameter (empty = the
/// parameters as a whole or the context) and the translated message
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolParameterError
{
public:
    QString parameter;
    QString message;

    bool operator==(const PDFDocumentToolParameterError&) const = default;
};

/// Definition of one parameter of a tool (newapp_tools.md 2, Parametry). One
/// definition gives the JSON Schema of MCP, the validation with translated
/// messages (the same for the user interface, MCP and later PdfTool) and the
/// storage of the last value (PDFToolParameterRepository of Pdf4QtLibApp; a
/// Secret is never stored). PROVISIONAL until the pilot tool Compress.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolParameterDefinition
{
public:
    /// Longest name of a parameter
    static constexpr int MAXIMUM_NAME_LENGTH = 64;

    /// Longest text of String / FileNameTemplate (default and upper bound of setMaximumLength)
    static constexpr int MAXIMUM_TEXT_LENGTH = 65536;

    /// Longest path of FilePath / FolderPath
    static constexpr int MAXIMUM_PATH_LENGTH = 4096;

    /// Longest Secret
    static constexpr int MAXIMUM_SECRET_LENGTH = 1024;

    /// Most options of an Enum
    static constexpr int MAXIMUM_OPTION_COUNT = 256;

    /// Range of Integer, which is exact in JSON (doubles of the clients)
    static constexpr qint64 MAXIMUM_SAFE_INTEGER = 9007199254740991;

    /// Translation context of the messages
    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFDocumentToolParameterDefinition";

    PDFDocumentToolParameterDefinition() = default;

    static PDFDocumentToolParameterDefinition createBool(const QString& name, const QString& title, const QString& description, bool defaultValue);
    static PDFDocumentToolParameterDefinition createInteger(const QString& name, const QString& title, const QString& description, qint64 defaultValue, qint64 minimum, qint64 maximum);
    static PDFDocumentToolParameterDefinition createReal(const QString& name, const QString& title, const QString& description, double defaultValue, double minimum, double maximum);
    static PDFDocumentToolParameterDefinition createString(const QString& name, const QString& title, const QString& description, const QString& defaultValue, int maximumLength = 1024);
    static PDFDocumentToolParameterDefinition createEnum(const QString& name, const QString& title, const QString& description, std::vector<PDFDocumentToolParameterOption> options, const QString& defaultId);
    static PDFDocumentToolParameterDefinition createPageSelection(const QString& name, const QString& title, const QString& description, const PDFDocumentToolPageSelection& defaultValue = PDFDocumentToolPageSelection());
    static PDFDocumentToolParameterDefinition createColor(const QString& name, const QString& title, const QString& description, const QColor& defaultValue);
    static PDFDocumentToolParameterDefinition createFilePath(const QString& name, const QString& title, const QString& description, const QString& defaultValue = QString());
    static PDFDocumentToolParameterDefinition createFolderPath(const QString& name, const QString& title, const QString& description, const QString& defaultValue = QString());
    static PDFDocumentToolParameterDefinition createFileNameTemplate(const QString& name, const QString& title, const QString& description, const QString& defaultValue);
    static PDFDocumentToolParameterDefinition createSecret(const QString& name, const QString& title, const QString& description);

    /// A value must be given (a missing value is an error, not the default; an
    /// empty text of a text type is missing)
    PDFDocumentToolParameterDefinition& setRequired(bool required) { m_required = required; return *this; }

    /// Longest text of a text type (bounded by the limit of the type)
    PDFDocumentToolParameterDefinition& setMaximumLength(int maximumLength);

    const QString& getName() const { return m_name; }
    PDFDocumentToolParameterType getType() const { return m_type; }
    const QString& getTitle() const { return m_title; }
    const QString& getDescription() const { return m_description; }
    const QVariant& getDefaultValue() const { return m_defaultValue; }
    bool isRequired() const { return m_required; }
    qint64 getIntegerMinimum() const { return m_integerMinimum; }
    qint64 getIntegerMaximum() const { return m_integerMaximum; }
    double getRealMinimum() const { return m_realMinimum; }
    double getRealMaximum() const { return m_realMaximum; }
    int getMaximumLength() const { return m_maximumLength; }
    const std::vector<PDFDocumentToolParameterOption>& getOptions() const { return m_options; }

    /// The value is stored as the last value of the tool (all types except Secret)
    bool isStored() const { return m_type != PDFDocumentToolParameterType::Secret; }

    /// The definition itself is valid: a valid name, minimum <= default <= maximum,
    /// valid unique option ids with the default among them, a valid default value
    /// (unless the parameter is required). \p errorMessage gets the reason (not translated, for developers).
    bool isValid(QString* errorMessage = nullptr) const;

    /// Canonical value of the untrusted \p value (QVariant from the user interface,
    /// from JSON or from the database). An invalid QVariant (missing value) gives the
    /// default of an optional parameter and an error of a required one. Returns
    /// the translated error, empty on success.
    QString normalize(const QVariant& value, QVariant* normalizedValue) const;

    /// JSON Schema of the parameter (a property of the object of the parameters)
    QJsonObject toJsonSchema() const;

    /// JSON value of a canonical value (Color "#rrggbb", PageSelection object;
    /// a Secret gives null - it is never echoed)
    QJsonValue toJsonValue(const QVariant& value) const;

    /// Value written into the database (integer / real / text; invalid for a Secret)
    QVariant toStorageValue(const QVariant& value) const;

    /// Canonical value of a value read from the database; std::nullopt for a
    /// hostile or foreign value (the caller uses the default)
    std::optional<QVariant> fromStorageValue(const QVariant& storedValue) const;

    /// Name is a valid name of a parameter: an ASCII letter followed by ASCII
    /// letters, digits and '_', at most MAXIMUM_NAME_LENGTH characters
    static bool isValidName(const QString& name);

    /// Id is a valid id of an option: lower case ASCII letters and digits, '-' and
    /// '_' inside, at most MAXIMUM_NAME_LENGTH characters
    static bool isValidOptionId(const QString& id);

    /// Untrusted text for a message: control and format characters removed, at
    /// most \p maximumLength characters (elided by "…")
    static QString sanitizeForMessage(const QString& text, int maximumLength = 32);

private:
    PDFDocumentToolParameterDefinition(const QString& name, PDFDocumentToolParameterType type, const QString& title, const QString& description);

    QString getLabel() const;
    QString normalizeText(const QVariant& value, QVariant* normalizedValue) const;
    int getTypeMaximumLength() const;

    QString m_name;
    PDFDocumentToolParameterType m_type = PDFDocumentToolParameterType::String;
    QString m_title;
    QString m_description;
    QVariant m_defaultValue;
    bool m_required = false;
    qint64 m_integerMinimum = 0;
    qint64 m_integerMaximum = 0;
    double m_realMinimum = 0.0;
    double m_realMaximum = 0.0;
    int m_maximumLength = 0;
    std::vector<PDFDocumentToolParameterOption> m_options;
};

using PDFDocumentToolParameterDefinitions = std::vector<PDFDocumentToolParameterDefinition>;

/// The parameters of a tool as a whole: validation of a set of untrusted values,
/// the JSON Schema of the object of the parameters (inputSchema of an action of
/// MCP, newapp_mcp.md 6) and the defaults. PROVISIONAL until the pilot tool Compress.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolParameters
{
public:
    PDFDocumentToolParameters() = delete;

    /// Most values of a set (untrusted input with more keys is refused)
    static constexpr int MAXIMUM_VALUE_COUNT = 256;

    /// Definitions are valid as a set: each one valid, unique names, at most
    /// MAXIMUM_VALUE_COUNT. \p errorMessage gets the reason (not translated).
    static bool isValid(const PDFDocumentToolParameterDefinitions& definitions, QString* errorMessage = nullptr);

    /// Canonical values of the untrusted \p values: every parameter present (the
    /// defaults of the missing optional ones). An unknown key, a missing required
    /// value or an invalid value is an error; on any error \p normalizedValues is
    /// left empty. Returns the errors (empty = valid).
    static std::vector<PDFDocumentToolParameterError> normalize(const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values, QVariantMap* normalizedValues);

    /// The same for a JSON object (MCP)
    static std::vector<PDFDocumentToolParameterError> normalize(const PDFDocumentToolParameterDefinitions& definitions, const QJsonObject& values, QVariantMap* normalizedValues);

    /// Default values of the optional parameters (required ones are left out)
    static QVariantMap getDefaultValues(const PDFDocumentToolParameterDefinitions& definitions);

    /// JSON Schema of the object of the parameters: "type": "object", the
    /// properties in the order of the definitions, "required" and
    /// "additionalProperties": false - never "oneOf" / "anyOf" / "allOf" at the top level
    static QJsonObject toJsonSchema(const PDFDocumentToolParameterDefinitions& definitions);

    /// JSON object of canonical values (Secrets left out)
    static QJsonObject toJson(const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values);

    /// Definition of the name, nullptr = none
    static const PDFDocumentToolParameterDefinition* find(const PDFDocumentToolParameterDefinitions& definitions, const QString& name);
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFDocumentToolPageSelection::Filters)

#endif // PDFDOCUMENTTOOLPARAMETER_H
