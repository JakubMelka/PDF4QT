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

#ifndef PDFMCPJSON_H
#define PDFMCPJSON_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcplimits.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QFlags>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFMcpJsonWriter;

// JSON layer of the MCP server (plan of step 7, 2.2). Every request body is
// checked by PDFMcpJsonScanner before QJsonDocument::fromJson parses it; values
// taken from a parsed request are read by PDFMcpJsonReader (strict) and every
// string goes through PDFMcpJson::sanitizeString; every output is written by
// PDFMcpJsonWriter (ordered, exact integers, shortest doubles). PDFJsonHelper of
// Pdf4QtLibCore is tolerant by design and is never used for untrusted input.

/// Kind of the id of a JSON-RPC message
enum class PDFMcpRequestIdKind
{
    None,       ///< No id (a notification, or no request at all)
    String,     ///< A string of at most PDFMcpLimits::MAXIMUM_ID_LENGTH characters
    Integer,    ///< An integer -?(0|[1-9][0-9]{0,18}), which fits qint64
    Invalid     ///< Any other id: null, bool, object, array, fraction, exponent, huge (-32600 without an id)
};

/// Id of a JSON-RPC request: echoed exactly in the response (a string by its
/// decoded value, an integer exactly - never through a double)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpRequestId
{
public:
    PDFMcpRequestId() = default;

    static PDFMcpRequestId createString(const QString& value);
    static PDFMcpRequestId createInteger(qint64 value);
    static PDFMcpRequestId createInvalid();

    /// Id of a parsed JSON value (a response of a client to an elicitation of the
    /// server): the same rules as the scanner, an integer given as a double is
    /// valid only when it is whole and exact
    static PDFMcpRequestId fromJson(const QJsonValue& value);

    PDFMcpRequestIdKind getKind() const { return m_kind; }
    bool isNone() const { return m_kind == PDFMcpRequestIdKind::None; }
    bool isValid() const { return m_kind == PDFMcpRequestIdKind::String || m_kind == PDFMcpRequestIdKind::Integer; }
    const QString& getString() const { return m_string; }
    qint64 getInteger() const { return m_integer; }

    /// Text of the id for logs of the tests and keys of maps ("s:<text>" / "i:<number>" / "")
    QString toKey() const;

    /// Writes the id as a value (null for None and Invalid)
    void write(PDFMcpJsonWriter& writer) const;

    /// JSON value of the id (null for None and Invalid)
    QJsonValue toJson() const;

    bool operator==(const PDFMcpRequestId&) const = default;

private:
    PDFMcpRequestIdKind m_kind = PDFMcpRequestIdKind::None;
    QString m_string;
    qint64 m_integer = 0;
};

/// Limits of one scan (PDFMcpLimits by default; the tests lower them)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpJsonScanLimits
{
public:
    qsizetype maximumSize = PDFMcpLimits::MAXIMUM_BODY_SIZE;
    int maximumDepth = PDFMcpLimits::MAXIMUM_JSON_DEPTH;
    int maximumObjectMembers = PDFMcpLimits::MAXIMUM_OBJECT_MEMBERS;
    int maximumArrayItems = PDFMcpLimits::MAXIMUM_ARRAY_ITEMS;
    qsizetype maximumStringSize = PDFMcpLimits::MAXIMUM_STRING_SIZE;
    int maximumNumberLength = PDFMcpLimits::MAXIMUM_NUMBER_LENGTH;
    int maximumIdLength = PDFMcpLimits::MAXIMUM_ID_LENGTH;
};

/// Result of a scan of a request body
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpJsonScanResult
{
public:
    /// Type of the top-level value
    enum class TopLevel
    {
        None,       ///< The scan failed before a value was found
        Object,
        Array,      ///< A batch (refused by the dispatcher, -32600)
        Other       ///< A string, number or literal
    };

    bool isValid() const { return errorCode == 0; }

    int errorCode = 0;              ///< 0, PDFMcpErrorCodes::PARSE_ERROR or INVALID_REQUEST
    QString errorMessage;           ///< English text of the error (for the JSON-RPC message)
    qsizetype errorOffset = -1;     ///< Byte offset of the error

    TopLevel topLevel = TopLevel::None;

    /// The top-level object has a member "id" (the id itself may be Invalid)
    bool hasId = false;
    PDFMcpRequestId id;

    /// A \u escape of a string gives a lone surrogate (recorded, not refused:
    /// PDFMcpJson::sanitizeString replaces it by U+FFFD)
    bool hasLoneSurrogate = false;

    // Statistics of the structure (the differential test compares them with the
    // tree of QJsonDocument)
    int maximumDepth = 0;
    qsizetype objectCount = 0;
    qsizetype arrayCount = 0;
    qsizetype memberCount = 0;      ///< Keys of all objects
    qsizetype stringCount = 0;      ///< String values (not keys)
    qsizetype numberCount = 0;
    qsizetype literalCount = 0;     ///< true, false, null
};

/// One linear pass over the raw bytes of a request body before QJsonDocument
/// parses it (plan 2.2). It checks the strict JSON grammar of RFC 8259 (no BOM,
/// no comments, no trailing garbage), valid UTF-8 (no overlong forms, no
/// encoded surrogates), the size, the depth, the members of an object and the
/// items of an array, duplicate keys (compared after the escapes are decoded),
/// the length of strings and number tokens, finite numbers, and it extracts the
/// raw token of the top-level "id". Allocation-light: only the keys of the open
/// objects are kept. Thread safe (no state).
///
/// Errors: invalid JSON or UTF-8 = PARSE_ERROR (-32700); a valid JSON over a
/// limit, a duplicate key or an invalid id = INVALID_REQUEST (-32600, the id of
/// the response is null).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpJsonScanner
{
public:
    PDFMcpJsonScanner() = default;
    explicit PDFMcpJsonScanner(const PDFMcpJsonScanLimits& limits) : m_limits(limits) { }

    PDFMcpJsonScanResult scan(QByteArrayView data) const;

    const PDFMcpJsonScanLimits& getLimits() const { return m_limits; }

private:
    PDFMcpJsonScanLimits m_limits;
};

/// Strings taken from requests and from documents
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpJson
{
public:
    PDFMcpJson() = delete;

    enum class SanitizeFlag
    {
        None = 0x00,
        KeepNewlines = 0x01,    ///< Tab, LF (and CR LF as LF) are kept; otherwise they become spaces
        RemoveBidi = 0x02,      ///< Bidirectional controls and overrides (U+061C, U+200E/F, U+202A-202E, U+2066-2069) are removed
        Trim = 0x04             ///< White space at the ends is removed
    };
    Q_DECLARE_FLAGS(SanitizeFlags, SanitizeFlag)

    /// Text of an untrusted string: lone surrogates replaced by U+FFFD, NUL and the
    /// C0 / C1 controls removed (except tab and newline with KeepNewlines), the
    /// bidirectional controls removed with RemoveBidi, at most \p maximumLength
    /// UTF-16 units (a surrogate pair is never cut; negative = no limit)
    static QString sanitizeString(const QString& text, SanitizeFlags flags = SanitizeFlag::RemoveBidi, qsizetype maximumLength = -1);

    /// The character is a bidirectional control or override
    static bool isBidiControl(char32_t character);

    /// Compact JSON of an object (keys sorted by Qt) - the canonical form of the
    /// arguments hashed by PDFMcpRequestState
    static QByteArray toCompactJson(const QJsonObject& object);

    /// Encodes data by base64url without padding (RFC 4648 5)
    static QByteArray toBase64Url(QByteArrayView data);

    /// Decodes base64url without padding; std::nullopt for any other character
    static std::optional<QByteArray> fromBase64Url(QByteArrayView text);
};

/// Strict typed accessor of a parsed JSON object with the paths of its errors
/// ("items[2].quads: expected an array of 8 numbers"). A child reader shares the
/// list of errors of its parent. A missing optional value gives std::nullopt
/// without an error; a value of a wrong type, out of the range or too long gives
/// std::nullopt and an error.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpJsonReader
{
public:
    /// Most errors, which are collected
    static constexpr int MAXIMUM_ERRORS = 16;

    explicit PDFMcpJsonReader(const QJsonObject& object, const QString& path = QString());

    const QJsonObject& getObject() const { return m_object; }
    const QString& getPath() const { return m_path; }

    bool has(QLatin1StringView key) const;

    /// String of at most \p maximumLength UTF-16 units, sanitized by \p flags
    std::optional<QString> getString(QLatin1StringView key, qsizetype maximumLength, bool required,
                                     PDFMcpJson::SanitizeFlags flags = PDFMcpJson::SanitizeFlag::RemoveBidi);

    /// Integer in [minimum, maximum]: a whole number, also 2.0, never 1.5;
    /// compared numerically (no rounding through a double for big values)
    std::optional<qint64> getInteger(QLatin1StringView key, qint64 minimum, qint64 maximum, bool required);

    /// Finite number in [minimum, maximum]
    std::optional<double> getNumber(QLatin1StringView key, double minimum, double maximum, bool required);

    std::optional<bool> getBool(QLatin1StringView key, bool required);

    /// String, which is one of \p values (exact)
    std::optional<QString> getEnum(QLatin1StringView key, const QStringList& values, bool required);

    /// Array of at most \p maximumItems items
    std::optional<QJsonArray> getArray(QLatin1StringView key, qsizetype maximumItems, bool required);

    std::optional<QJsonObject> getObject(QLatin1StringView key, bool required);

    /// Rectangle {left, bottom, right, top} of finite numbers (any order of the
    /// edges is normalized, an empty rectangle is valid)
    std::optional<QRectF> getRect(QLatin1StringView key, bool required);

    /// Quadrilateral of 8 finite numbers (QuadPoints order) at the path
    std::optional<std::array<double, 8>> getQuad(const QJsonValue& value, const QString& path);

    /// Reader of a child object (an empty reader with an error, when it is not an object)
    PDFMcpJsonReader child(QLatin1StringView key, bool required);

    /// Reader of an object, which is an item of an array at \p path
    PDFMcpJsonReader item(const QJsonValue& value, const QString& path);

    /// Records an error of the key ("<path>.<key>: <message>")
    void addError(QLatin1StringView key, const QString& message);

    /// Records an error of a full path
    void addPathError(const QString& path, const QString& message);

    /// Keys of the object, which are not in \p knownKeys, are errors ("unknown property")
    void checkUnknownKeys(const QStringList& knownKeys);

    bool hasErrors() const;
    QStringList getErrors() const;

    /// The errors joined by "; "
    QString getErrorText() const;

    /// Path of a key of this object ("items[2].quads")
    QString getKeyPath(QLatin1StringView key) const;

private:
    PDFMcpJsonReader(const QJsonObject& object, const QString& path, std::shared_ptr<QStringList> errors);

    QJsonObject m_object;
    QString m_path;
    std::shared_ptr<QStringList> m_errors;
};

/// Ordered streaming writer of JSON (all output of the server, plan 2.2): the
/// members are written in the order of the calls (deterministic schemas,
/// "action" first), strings escaped by RFC 8259 (a lone surrogate as its \u
/// escape, so a string id is echoed exactly), integers exactly, doubles by the
/// shortest round trip form (std::to_chars; a non-finite double is null).
/// The writer checks its own nesting; a misuse is a bug of the server and is
/// asserted.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpJsonWriter
{
public:
    PDFMcpJsonWriter() = default;

    void reserve(qsizetype size) { m_data.reserve(size); }

    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    /// Key of the next member of the current object
    void key(QStringView name);
    void key(QLatin1StringView name);

    void writeString(QStringView text);
    void writeString(QLatin1StringView text);
    void writeInteger(qint64 value);
    void writeNumber(double value);
    void writeBool(bool value);
    void writeNull();

    /// A complete serialized JSON value (a cached schema)
    void writeRaw(QByteArrayView json);

    /// Serialized members "a":1,"b":2 inserted into the current object (empty = nothing)
    void writeRawMembers(QByteArrayView members);

    /// A string value of base64 of the data, encoded directly into the output
    void writeBase64(QByteArrayView data);

    /// A value of Qt (objects with their keys sorted by Qt)
    void writeQJson(const QJsonValue& value);

    // Members of the current object
    void writeMember(QLatin1StringView name, QStringView text) { key(name); writeString(text); }
    void writeMember(QLatin1StringView name, QLatin1StringView text) { key(name); writeString(text); }
    void writeMember(QLatin1StringView name, const char* text) { key(name); writeString(QLatin1StringView(text)); }
    void writeMember(QLatin1StringView name, qint64 value) { key(name); writeInteger(value); }
    void writeMember(QLatin1StringView name, int value) { key(name); writeInteger(value); }
    void writeMember(QLatin1StringView name, double value) { key(name); writeNumber(value); }
    void writeMember(QLatin1StringView name, bool value) { key(name); writeBool(value); }

    /// Array of strings
    void writeStringArray(const QStringList& values);

    /// The output is one complete value
    bool isComplete() const { return m_stack.empty() && m_hasValue; }

    const QByteArray& getData() const { return m_data; }
    QByteArray take();

    /// Escaped text of a string value including the quotes
    static QByteArray quote(QStringView text);

    /// Shortest round trip text of a double ("null" when it is not finite)
    static QByteArray formatNumber(double value);

private:
    enum class Scope
    {
        Object,
        Array
    };

    void beginValue();
    void appendEscaped(QStringView text);

    QByteArray m_data;
    std::vector<Scope> m_stack;
    std::vector<bool> m_hasItems;
    bool m_expectValue = false;     ///< A key was written in an object
    bool m_hasValue = false;        ///< The top-level value was written
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFMcpJson::SanitizeFlags)

}   // namespace pdf

#endif // PDFMCPJSON_H
