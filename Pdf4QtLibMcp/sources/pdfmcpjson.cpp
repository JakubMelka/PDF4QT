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

#include "pdfmcpjson.h"
#include "pdfmcperrorcodes.h"

#include <QJsonDocument>
#include <QSet>

#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpRequestId
// ----------------------------------------------------------------------------

PDFMcpRequestId PDFMcpRequestId::createString(const QString& value)
{
    PDFMcpRequestId id;
    id.m_kind = PDFMcpRequestIdKind::String;
    id.m_string = value;
    return id;
}

PDFMcpRequestId PDFMcpRequestId::createInteger(qint64 value)
{
    PDFMcpRequestId id;
    id.m_kind = PDFMcpRequestIdKind::Integer;
    id.m_integer = value;
    return id;
}

PDFMcpRequestId PDFMcpRequestId::createInvalid()
{
    PDFMcpRequestId id;
    id.m_kind = PDFMcpRequestIdKind::Invalid;
    return id;
}

PDFMcpRequestId PDFMcpRequestId::fromJson(const QJsonValue& value)
{
    if (value.isUndefined())
    {
        return PDFMcpRequestId();
    }

    if (value.isString())
    {
        const QString text = value.toString();
        if (text.size() <= PDFMcpLimits::MAXIMUM_ID_LENGTH)
        {
            return createString(text);
        }
        return createInvalid();
    }

    if (value.isDouble())
    {
        // Whole and exact: the two defaults differ for a value, which is not a
        // whole number representable as qint64
        const qint64 first = value.toInteger(0);
        const qint64 second = value.toInteger(1);
        if (first == second)
        {
            return createInteger(first);
        }
    }

    return createInvalid();
}

QString PDFMcpRequestId::toKey() const
{
    switch (m_kind)
    {
        case PDFMcpRequestIdKind::String:
            return QStringLiteral("s:") + m_string;
        case PDFMcpRequestIdKind::Integer:
            return QStringLiteral("i:") + QString::number(m_integer);
        default:
            return QString();
    }
}

void PDFMcpRequestId::write(PDFMcpJsonWriter& writer) const
{
    switch (m_kind)
    {
        case PDFMcpRequestIdKind::String:
            writer.writeString(m_string);
            break;
        case PDFMcpRequestIdKind::Integer:
            writer.writeInteger(m_integer);
            break;
        default:
            writer.writeNull();
            break;
    }
}

QJsonValue PDFMcpRequestId::toJson() const
{
    switch (m_kind)
    {
        case PDFMcpRequestIdKind::String:
            return QJsonValue(m_string);
        case PDFMcpRequestIdKind::Integer:
            return QJsonValue(m_integer);
        default:
            return QJsonValue(QJsonValue::Null);
    }
}

// ----------------------------------------------------------------------------
// PDFMcpJsonScanner
// ----------------------------------------------------------------------------

/// One run of the scanner over a body (the state of the pass)
class PDFMcpJsonScannerContext
{
public:
    PDFMcpJsonScannerContext(QByteArrayView data, const PDFMcpJsonScanLimits& limits, PDFMcpJsonScanResult* result) :
        m_data(data.data()),
        m_size(data.size()),
        m_limits(limits),
        m_result(result)
    {

    }

    void scanDocument()
    {
        if (m_size > m_limits.maximumSize)
        {
            fail(PDFMcpErrorCodes::INVALID_REQUEST, "The request body is too large");
            return;
        }

        skipWhitespace();
        if (m_position >= m_size)
        {
            fail(PDFMcpErrorCodes::PARSE_ERROR, "The request body is empty");
            return;
        }

        const char first = m_data[m_position];
        if (first == '{')
        {
            m_result->topLevel = PDFMcpJsonScanResult::TopLevel::Object;
        }
        else if (first == '[')
        {
            m_result->topLevel = PDFMcpJsonScanResult::TopLevel::Array;
        }
        else
        {
            m_result->topLevel = PDFMcpJsonScanResult::TopLevel::Other;
        }

        if (!scanValue(0, true))
        {
            return;
        }

        skipWhitespace();
        if (m_position != m_size)
        {
            fail(PDFMcpErrorCodes::PARSE_ERROR, "Unexpected data after the JSON value");
        }
    }

private:
    /// Kind of a scanned value (the id of the request is checked by it)
    enum class ValueKind
    {
        Object,
        Array,
        String,
        Number,
        Literal
    };

    bool fail(int code, const char* message)
    {
        if (m_result->errorCode == 0)
        {
            m_result->errorCode = code;
            m_result->errorMessage = QString::fromLatin1(message);
            m_result->errorOffset = m_position;
            m_result->hasId = false;
            m_result->id = PDFMcpRequestId();
        }
        return false;
    }

    void skipWhitespace()
    {
        while (m_position < m_size)
        {
            const char c = m_data[m_position];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            {
                break;
            }
            ++m_position;
        }
    }

    /// Scans one value. \p isTopLevel: the value is the document itself (the
    /// members of a top-level object are searched for "id").
    bool scanValue(int depth, bool isTopLevel, ValueKind* kind = nullptr, QString* decodedString = nullptr, QByteArrayView* numberToken = nullptr)
    {
        if (m_position >= m_size)
        {
            return fail(PDFMcpErrorCodes::PARSE_ERROR, "Unexpected end of the JSON data");
        }

        const char c = m_data[m_position];
        switch (c)
        {
            case '{':
                if (kind)
                {
                    *kind = ValueKind::Object;
                }
                return scanObject(depth + 1, isTopLevel);

            case '[':
                if (kind)
                {
                    *kind = ValueKind::Array;
                }
                return scanArray(depth + 1);

            case '"':
            {
                if (kind)
                {
                    *kind = ValueKind::String;
                }
                ++m_result->stringCount;
                return scanString(decodedString);
            }

            case 't':
            case 'f':
            case 'n':
                if (kind)
                {
                    *kind = ValueKind::Literal;
                }
                ++m_result->literalCount;
                return scanLiteral();

            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                {
                    if (kind)
                    {
                        *kind = ValueKind::Number;
                    }
                    ++m_result->numberCount;
                    return scanNumber(numberToken);
                }
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid JSON value");
        }
    }

    bool enterContainer(int depth)
    {
        if (depth > m_limits.maximumDepth)
        {
            return fail(PDFMcpErrorCodes::INVALID_REQUEST, "The JSON data are nested too deeply");
        }
        m_result->maximumDepth = std::max(m_result->maximumDepth, depth);
        return true;
    }

    bool scanObject(int depth, bool isTopLevel)
    {
        if (!enterContainer(depth))
        {
            return false;
        }

        ++m_result->objectCount;
        ++m_position;   // '{'
        skipWhitespace();

        QSet<QString> keys;
        int memberCount = 0;
        if (m_position < m_size && m_data[m_position] == '}')
        {
            ++m_position;
            return true;
        }

        while (true)
        {
            skipWhitespace();
            if (m_position >= m_size || m_data[m_position] != '"')
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Expected a string key of an object member");
            }

            QString key;
            if (!scanString(&key))
            {
                return false;
            }

            ++memberCount;
            ++m_result->memberCount;
            if (memberCount > m_limits.maximumObjectMembers)
            {
                return fail(PDFMcpErrorCodes::INVALID_REQUEST, "An object has too many members");
            }

            if (keys.contains(key))
            {
                return fail(PDFMcpErrorCodes::INVALID_REQUEST, "duplicate key");
            }
            keys.insert(key);

            skipWhitespace();
            if (m_position >= m_size || m_data[m_position] != ':')
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Expected ':' after the key of an object member");
            }
            ++m_position;
            skipWhitespace();

            const bool isId = isTopLevel && key == QLatin1String("id");
            if (isId)
            {
                if (!scanId(depth))
                {
                    return false;
                }
            }
            else if (!scanValue(depth, false))
            {
                return false;
            }

            skipWhitespace();
            if (m_position >= m_size)
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Unexpected end of the JSON data in an object");
            }

            const char c = m_data[m_position++];
            if (c == '}')
            {
                return true;
            }
            if (c != ',')
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Expected ',' or '}' in an object");
            }
        }
    }

    /// Value of the member "id" of the top-level object (plan 2.2)
    bool scanId(int depth)
    {
        ValueKind kind = ValueKind::Literal;
        QString text;
        QByteArrayView token;
        if (!scanValue(depth, false, &kind, &text, &token))
        {
            return false;
        }

        m_result->hasId = true;
        switch (kind)
        {
            case ValueKind::String:
                if (text.size() <= m_limits.maximumIdLength)
                {
                    m_result->id = PDFMcpRequestId::createString(text);
                    return true;
                }
                break;

            case ValueKind::Number:
            {
                // -?(0|[1-9][0-9]{0,18}), which fits qint64
                const qsizetype digitsStart = (!token.isEmpty() && token[0] == '-') ? 1 : 0;
                const qsizetype digits = token.size() - digitsStart;
                bool isIntegerToken = digits >= 1 && digits <= 19;
                for (qsizetype i = digitsStart; isIntegerToken && i < token.size(); ++i)
                {
                    isIntegerToken = token[i] >= '0' && token[i] <= '9';
                }
                if (isIntegerToken && digits > 1 && token[digitsStart] == '0')
                {
                    isIntegerToken = false;
                }

                if (isIntegerToken)
                {
                    qint64 value = 0;
                    const std::from_chars_result result = std::from_chars(token.data(), token.data() + token.size(), value);
                    if (result.ec == std::errc() && result.ptr == token.data() + token.size())
                    {
                        m_result->id = PDFMcpRequestId::createInteger(value);
                        return true;
                    }
                }
                break;
            }

            default:
                break;
        }

        // The id is invalid: -32600 with the id null, but the scan goes on (a
        // syntax error of the rest is reported instead)
        m_result->id = PDFMcpRequestId::createInvalid();
        return true;
    }

    bool scanArray(int depth)
    {
        if (!enterContainer(depth))
        {
            return false;
        }

        ++m_result->arrayCount;
        ++m_position;   // '['
        skipWhitespace();

        if (m_position < m_size && m_data[m_position] == ']')
        {
            ++m_position;
            return true;
        }

        int itemCount = 0;
        while (true)
        {
            skipWhitespace();
            if (!scanValue(depth, false))
            {
                return false;
            }

            ++itemCount;
            if (itemCount > m_limits.maximumArrayItems)
            {
                return fail(PDFMcpErrorCodes::INVALID_REQUEST, "An array has too many items");
            }

            skipWhitespace();
            if (m_position >= m_size)
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Unexpected end of the JSON data in an array");
            }

            const char c = m_data[m_position++];
            if (c == ']')
            {
                return true;
            }
            if (c != ',')
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Expected ',' or ']' in an array");
            }
        }
    }

    static int hexValue(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    /// Reads the 4 hex digits of a \u escape at the position (after "\u")
    bool readHex4(char16_t* value)
    {
        if (m_size - m_position < 4)
        {
            return fail(PDFMcpErrorCodes::PARSE_ERROR, "Incomplete \\u escape");
        }

        int result = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int digit = hexValue(m_data[m_position + i]);
            if (digit < 0)
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid \\u escape");
            }
            result = result * 16 + digit;
        }

        m_position += 4;
        *value = char16_t(result);
        return true;
    }

    static void appendCodePoint(QString* text, char32_t codePoint)
    {
        if (!text)
        {
            return;
        }

        if (QChar::requiresSurrogates(codePoint))
        {
            text->append(QChar(QChar::highSurrogate(codePoint)));
            text->append(QChar(QChar::lowSurrogate(codePoint)));
        }
        else
        {
            text->append(QChar(char16_t(codePoint)));
        }
    }

    /// Scans a string at '"'; \p decoded gets its text (nullptr = not needed)
    bool scanString(QString* decoded)
    {
        const qsizetype start = ++m_position;   // after '"'
        while (true)
        {
            if (m_position >= m_size)
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Unterminated string");
            }

            if (m_position - start > m_limits.maximumStringSize)
            {
                return fail(PDFMcpErrorCodes::INVALID_REQUEST, "A string is too long");
            }

            const unsigned char c = static_cast<unsigned char>(m_data[m_position]);
            if (c == '"')
            {
                ++m_position;
                return true;
            }

            if (c < 0x20)
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Control character in a string");
            }

            if (c == '\\')
            {
                ++m_position;
                if (m_position >= m_size)
                {
                    return fail(PDFMcpErrorCodes::PARSE_ERROR, "Unterminated escape");
                }

                const char escape = m_data[m_position++];
                switch (escape)
                {
                    case '"':
                        appendCodePoint(decoded, U'"');
                        break;
                    case '\\':
                        appendCodePoint(decoded, U'\\');
                        break;
                    case '/':
                        appendCodePoint(decoded, U'/');
                        break;
                    case 'b':
                        appendCodePoint(decoded, U'\b');
                        break;
                    case 'f':
                        appendCodePoint(decoded, U'\f');
                        break;
                    case 'n':
                        appendCodePoint(decoded, U'\n');
                        break;
                    case 'r':
                        appendCodePoint(decoded, U'\r');
                        break;
                    case 't':
                        appendCodePoint(decoded, U'\t');
                        break;
                    case 'u':
                    {
                        char16_t unit = 0;
                        if (!readHex4(&unit))
                        {
                            return false;
                        }

                        if (QChar::isHighSurrogate(unit))
                        {
                            // A pair is \uD8xx\uDCxx; anything else is a lone surrogate
                            if (m_size - m_position >= 6 && m_data[m_position] == '\\' && m_data[m_position + 1] == 'u')
                            {
                                const qsizetype saved = m_position;
                                m_position += 2;
                                char16_t low = 0;
                                if (!readHex4(&low))
                                {
                                    return false;
                                }

                                if (QChar::isLowSurrogate(low))
                                {
                                    appendCodePoint(decoded, QChar::surrogateToUcs4(unit, low));
                                    break;
                                }

                                // The second escape is scanned again as its own character
                                m_position = saved;
                            }

                            m_result->hasLoneSurrogate = true;
                            if (decoded)
                            {
                                decoded->append(QChar(unit));
                            }
                        }
                        else if (QChar::isLowSurrogate(unit))
                        {
                            m_result->hasLoneSurrogate = true;
                            if (decoded)
                            {
                                decoded->append(QChar(unit));
                            }
                        }
                        else
                        {
                            appendCodePoint(decoded, unit);
                        }
                        break;
                    }
                    default:
                        return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid escape in a string");
                }
                continue;
            }

            if (c < 0x80)
            {
                appendCodePoint(decoded, c);
                ++m_position;
                continue;
            }

            // UTF-8 sequence of 2 - 4 bytes: no overlong form, no encoded
            // surrogate, at most U+10FFFF (RFC 3629)
            int length = 0;
            char32_t codePoint = 0;
            char32_t minimum = 0;
            if (c >= 0xC2 && c <= 0xDF)
            {
                length = 2;
                codePoint = c & 0x1F;
                minimum = 0x80;
            }
            else if (c >= 0xE0 && c <= 0xEF)
            {
                length = 3;
                codePoint = c & 0x0F;
                minimum = 0x800;
            }
            else if (c >= 0xF0 && c <= 0xF4)
            {
                length = 4;
                codePoint = c & 0x07;
                minimum = 0x10000;
            }
            else
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid UTF-8");
            }

            if (m_size - m_position < length)
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid UTF-8");
            }

            for (int i = 1; i < length; ++i)
            {
                const unsigned char continuation = static_cast<unsigned char>(m_data[m_position + i]);
                if ((continuation & 0xC0) != 0x80)
                {
                    return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid UTF-8");
                }
                codePoint = (codePoint << 6) | (continuation & 0x3F);
            }

            if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid UTF-8");
            }

            appendCodePoint(decoded, codePoint);
            m_position += length;
        }
    }

    bool scanNumber(QByteArrayView* token)
    {
        const qsizetype start = m_position;

        qsizetype integerDigits = 0;
        qsizetype leadingFractionZeros = 0;
        bool isIntegerZero = false;

        if (m_data[m_position] == '-')
        {
            ++m_position;
        }

        if (!isDigitAt(m_position))
        {
            return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid number");
        }

        if (m_data[m_position] == '0')
        {
            isIntegerZero = true;
            ++m_position;
            if (isDigitAt(m_position))
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid number (leading zero)");
            }
        }
        else
        {
            while (isDigitAt(m_position))
            {
                ++m_position;
                ++integerDigits;
            }
        }

        if (m_position < m_size && m_data[m_position] == '.')
        {
            ++m_position;
            if (!isDigitAt(m_position))
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid number (fraction)");
            }

            bool isLeading = isIntegerZero;
            while (isDigitAt(m_position))
            {
                if (isLeading && m_data[m_position] == '0')
                {
                    ++leadingFractionZeros;
                }
                else
                {
                    isLeading = false;
                }
                ++m_position;
            }
        }

        qint64 exponent = 0;
        if (m_position < m_size && (m_data[m_position] == 'e' || m_data[m_position] == 'E'))
        {
            ++m_position;
            bool isNegative = false;
            if (m_position < m_size && (m_data[m_position] == '+' || m_data[m_position] == '-'))
            {
                isNegative = m_data[m_position] == '-';
                ++m_position;
            }

            if (!isDigitAt(m_position))
            {
                return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid number (exponent)");
            }

            while (isDigitAt(m_position))
            {
                if (exponent < 1000000)
                {
                    exponent = exponent * 10 + (m_data[m_position] - '0');
                }
                ++m_position;
            }

            if (isNegative)
            {
                exponent = -exponent;
            }
        }

        const qsizetype length = m_position - start;
        if (length > m_limits.maximumNumberLength)
        {
            return fail(PDFMcpErrorCodes::INVALID_REQUEST, "A number is too long");
        }

        // A number, whose magnitude overflows a double, is refused (an underflow
        // is zero, as everywhere)
        double value = 0.0;
        const std::from_chars_result result = std::from_chars(m_data + start, m_data + m_position, value);
        if (result.ec == std::errc::result_out_of_range)
        {
            const qint64 magnitude = isIntegerZero ? exponent - leadingFractionZeros : exponent + integerDigits;
            if (magnitude > 0)
            {
                return fail(PDFMcpErrorCodes::INVALID_REQUEST, "A number is out of range");
            }
        }
        else if (result.ec != std::errc() || !std::isfinite(value))
        {
            return fail(PDFMcpErrorCodes::INVALID_REQUEST, "A number is out of range");
        }

        if (token)
        {
            *token = QByteArrayView(m_data + start, length);
        }
        return true;
    }

    bool isDigitAt(qsizetype position) const
    {
        return position < m_size && m_data[position] >= '0' && m_data[position] <= '9';
    }

    bool scanLiteral()
    {
        static constexpr const char* LITERALS[] = { "true", "false", "null" };
        for (const char* literal : LITERALS)
        {
            const qsizetype length = qsizetype(qstrlen(literal));
            if (m_size - m_position >= length && QByteArrayView(m_data + m_position, length) == QByteArrayView(literal, length))
            {
                m_position += length;
                return true;
            }
        }
        return fail(PDFMcpErrorCodes::PARSE_ERROR, "Invalid literal");
    }

    const char* m_data = nullptr;
    qsizetype m_size = 0;
    qsizetype m_position = 0;
    const PDFMcpJsonScanLimits& m_limits;
    PDFMcpJsonScanResult* m_result = nullptr;
};

PDFMcpJsonScanResult PDFMcpJsonScanner::scan(QByteArrayView data) const
{
    PDFMcpJsonScanResult result;
    PDFMcpJsonScannerContext context(data, m_limits, &result);
    context.scanDocument();

    if (result.isValid() && result.id.getKind() == PDFMcpRequestIdKind::Invalid)
    {
        result.errorCode = PDFMcpErrorCodes::INVALID_REQUEST;
        result.errorMessage = QStringLiteral("Invalid id: a string of at most %1 characters or an integer is required").arg(m_limits.maximumIdLength);
    }
    if (!result.isValid())
    {
        result.id = PDFMcpRequestId();
    }
    return result;
}

// ----------------------------------------------------------------------------
// PDFMcpJson
// ----------------------------------------------------------------------------

/// Helpers of PDFMcpJson
class PDFMcpJsonHelper
{
public:
    PDFMcpJsonHelper() = delete;

    /// Appends the part, when the result stays within the limit (negative = none)
    static bool appendLimited(QString* result, qsizetype maximumLength, QStringView part)
    {
        if (maximumLength >= 0 && result->size() + part.size() > maximumLength)
        {
            return false;
        }
        result->append(part);
        return true;
    }
};

bool PDFMcpJson::isBidiControl(char32_t character)
{
    return character == 0x061C ||
           character == 0x200E || character == 0x200F ||
           (character >= 0x202A && character <= 0x202E) ||
           (character >= 0x2066 && character <= 0x2069);
}

QString PDFMcpJson::sanitizeString(const QString& text, SanitizeFlags flags, qsizetype maximumLength)
{
    QString result;
    result.reserve(maximumLength >= 0 ? std::min(maximumLength, text.size()) : text.size());

    const bool keepNewlines = flags.testFlag(SanitizeFlag::KeepNewlines);
    const bool removeBidi = flags.testFlag(SanitizeFlag::RemoveBidi);

    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size; ++i)
    {
        const QChar c = text[i];
        const char16_t unit = c.unicode();

        if (QChar::isHighSurrogate(unit))
        {
            if (i + 1 < size && QChar::isLowSurrogate(text[i + 1].unicode()))
            {
                if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(text).mid(i, 2)))
                {
                    break;
                }
                ++i;
                continue;
            }

            if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(u"�")))
            {
                break;
            }
            continue;
        }

        if (QChar::isLowSurrogate(unit))
        {
            if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(u"�")))
            {
                break;
            }
            continue;
        }

        if (unit == u'\r')
        {
            if (keepNewlines)
            {
                if (i + 1 < size && text[i + 1] == u'\n')
                {
                    continue;
                }
                if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(u"\n")))
                {
                    break;
                }
            }
            else if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(u" ")))
            {
                break;
            }
            continue;
        }

        if (unit == u'\t' || unit == u'\n')
        {
            const char16_t replacement[1] = { keepNewlines ? unit : u' ' };
            if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(replacement, 1)))
            {
                break;
            }
            continue;
        }

        if (unit < 0x20 || (unit >= 0x7F && unit <= 0x9F))
        {
            continue;
        }

        if (removeBidi && isBidiControl(unit))
        {
            continue;
        }

        if (!PDFMcpJsonHelper::appendLimited(&result, maximumLength, QStringView(&c, 1)))
        {
            break;
        }
    }

    if (flags.testFlag(SanitizeFlag::Trim))
    {
        result = result.trimmed();
    }
    return result;
}

QByteArray PDFMcpJson::toCompactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray PDFMcpJson::toBase64Url(QByteArrayView data)
{
    return data.toByteArray().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

std::optional<QByteArray> PDFMcpJson::fromBase64Url(QByteArrayView text)
{
    for (const char c : text)
    {
        const bool isValid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!isValid)
        {
            return std::nullopt;
        }
    }

    // A length of 4n + 1 cannot be a valid encoding
    if (text.size() % 4 == 1)
    {
        return std::nullopt;
    }

    QByteArray::FromBase64Result result = QByteArray::fromBase64Encoding(text.toByteArray(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!result)
    {
        return std::nullopt;
    }
    return result.decoded;
}

// ----------------------------------------------------------------------------
// PDFMcpJsonReader
// ----------------------------------------------------------------------------

/// Helpers of the reader
class PDFMcpJsonReaderHelper
{
public:
    PDFMcpJsonReaderHelper() = delete;

    /// Largest magnitude of a coordinate of a rectangle or a quadrilateral
    static constexpr double MAXIMUM_COORDINATE = 1.0e9;

    static QString typeName(const QJsonValue& value)
    {
        switch (value.type())
        {
            case QJsonValue::Null:
                return QStringLiteral("null");
            case QJsonValue::Bool:
                return QStringLiteral("a boolean");
            case QJsonValue::Double:
                return QStringLiteral("a number");
            case QJsonValue::String:
                return QStringLiteral("a string");
            case QJsonValue::Array:
                return QStringLiteral("an array");
            case QJsonValue::Object:
                return QStringLiteral("an object");
            default:
                return QStringLiteral("nothing");
        }
    }

    static std::optional<double> coordinate(const QJsonValue& value)
    {
        if (!value.isDouble())
        {
            return std::nullopt;
        }
        const double number = value.toDouble();
        if (!std::isfinite(number) || std::abs(number) > MAXIMUM_COORDINATE)
        {
            return std::nullopt;
        }
        return number;
    }
};

PDFMcpJsonReader::PDFMcpJsonReader(const QJsonObject& object, const QString& path) :
    m_object(object),
    m_path(path),
    m_errors(std::make_shared<QStringList>())
{

}

PDFMcpJsonReader::PDFMcpJsonReader(const QJsonObject& object, const QString& path, std::shared_ptr<QStringList> errors) :
    m_object(object),
    m_path(path),
    m_errors(std::move(errors))
{

}

bool PDFMcpJsonReader::has(QLatin1StringView key) const
{
    return m_object.contains(key);
}

QString PDFMcpJsonReader::getKeyPath(QLatin1StringView key) const
{
    if (m_path.isEmpty())
    {
        return QString(key);
    }
    return m_path + QLatin1Char('.') + QString(key);
}

void PDFMcpJsonReader::addError(QLatin1StringView key, const QString& message)
{
    addPathError(getKeyPath(key), message);
}

void PDFMcpJsonReader::addPathError(const QString& path, const QString& message)
{
    if (m_errors->size() < MAXIMUM_ERRORS)
    {
        m_errors->append(path.isEmpty() ? message : QStringLiteral("%1: %2").arg(path, message));
    }
}

bool PDFMcpJsonReader::hasErrors() const
{
    return !m_errors->isEmpty();
}

QStringList PDFMcpJsonReader::getErrors() const
{
    return *m_errors;
}

QString PDFMcpJsonReader::getErrorText() const
{
    return m_errors->join(QStringLiteral("; "));
}

std::optional<QString> PDFMcpJsonReader::getString(QLatin1StringView key, qsizetype maximumLength, bool required, PDFMcpJson::SanitizeFlags flags)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isString())
    {
        addError(key, QStringLiteral("expected a string, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return std::nullopt;
    }

    const QString text = value.toString();
    if (maximumLength >= 0 && text.size() > maximumLength)
    {
        addError(key, QStringLiteral("is longer than %1 characters").arg(maximumLength));
        return std::nullopt;
    }

    return PDFMcpJson::sanitizeString(text, flags, maximumLength);
}

std::optional<qint64> PDFMcpJsonReader::getInteger(QLatin1StringView key, qint64 minimum, qint64 maximum, bool required)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isDouble())
    {
        addError(key, QStringLiteral("expected an integer, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return std::nullopt;
    }

    const qint64 first = value.toInteger(0);
    const qint64 second = value.toInteger(1);
    if (first != second)
    {
        addError(key, QStringLiteral("expected an integer from %1 to %2").arg(minimum).arg(maximum));
        return std::nullopt;
    }

    if (first < minimum || first > maximum)
    {
        addError(key, QStringLiteral("must be from %1 to %2").arg(minimum).arg(maximum));
        return std::nullopt;
    }
    return first;
}

std::optional<double> PDFMcpJsonReader::getNumber(QLatin1StringView key, double minimum, double maximum, bool required)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isDouble())
    {
        addError(key, QStringLiteral("expected a number, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return std::nullopt;
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum)
    {
        addError(key, QStringLiteral("must be from %1 to %2").arg(minimum).arg(maximum));
        return std::nullopt;
    }
    return number;
}

std::optional<bool> PDFMcpJsonReader::getBool(QLatin1StringView key, bool required)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isBool())
    {
        addError(key, QStringLiteral("expected a boolean, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return std::nullopt;
    }
    return value.toBool();
}

std::optional<QString> PDFMcpJsonReader::getEnum(QLatin1StringView key, const QStringList& values, bool required)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isString() || !values.contains(value.toString()))
    {
        addError(key, QStringLiteral("must be one of: %1").arg(values.join(QStringLiteral(", "))));
        return std::nullopt;
    }
    return value.toString();
}

std::optional<QJsonArray> PDFMcpJsonReader::getArray(QLatin1StringView key, qsizetype maximumItems, bool required)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isArray())
    {
        addError(key, QStringLiteral("expected an array, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return std::nullopt;
    }

    const QJsonArray array = value.toArray();
    if (maximumItems >= 0 && array.size() > maximumItems)
    {
        addError(key, QStringLiteral("has more than %1 items").arg(maximumItems));
        return std::nullopt;
    }
    return array;
}

std::optional<QJsonObject> PDFMcpJsonReader::getObject(QLatin1StringView key, bool required)
{
    const QJsonValue value = m_object.value(key);
    if (value.isUndefined())
    {
        if (required)
        {
            addError(key, QStringLiteral("is required"));
        }
        return std::nullopt;
    }

    if (!value.isObject())
    {
        addError(key, QStringLiteral("expected an object, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return std::nullopt;
    }
    return value.toObject();
}

std::optional<QRectF> PDFMcpJsonReader::getRect(QLatin1StringView key, bool required)
{
    std::optional<QJsonObject> object = getObject(key, required);
    if (!object)
    {
        return std::nullopt;
    }

    const QStringList names = { QStringLiteral("left"), QStringLiteral("bottom"), QStringLiteral("right"), QStringLiteral("top") };
    double values[4] = { };
    bool ok = true;
    for (int i = 0; i < 4; ++i)
    {
        const std::optional<double> coordinate = PDFMcpJsonReaderHelper::coordinate(object->value(names[i]));
        if (!coordinate)
        {
            addPathError(getKeyPath(key) + QLatin1Char('.') + names[i], QStringLiteral("expected a finite number"));
            ok = false;
            continue;
        }
        values[i] = *coordinate;
    }

    for (auto it = object->constBegin(); it != object->constEnd(); ++it)
    {
        if (!names.contains(it.key()))
        {
            addPathError(getKeyPath(key) + QLatin1Char('.') + PDFMcpJson::sanitizeString(it.key(), PDFMcpJson::SanitizeFlag::RemoveBidi, 64), QStringLiteral("unknown property"));
            ok = false;
        }
    }

    if (!ok)
    {
        return std::nullopt;
    }

    return QRectF(QPointF(values[0], values[1]), QPointF(values[2], values[3])).normalized();
}

std::optional<std::array<double, 8>> PDFMcpJsonReader::getQuad(const QJsonValue& value, const QString& path)
{
    if (!value.isArray() || value.toArray().size() != 8)
    {
        addPathError(path, QStringLiteral("expected an array of 8 numbers"));
        return std::nullopt;
    }

    const QJsonArray array = value.toArray();
    std::array<double, 8> quad = { };
    for (int i = 0; i < 8; ++i)
    {
        const std::optional<double> coordinate = PDFMcpJsonReaderHelper::coordinate(array[i]);
        if (!coordinate)
        {
            addPathError(path, QStringLiteral("expected an array of 8 finite numbers"));
            return std::nullopt;
        }
        quad[size_t(i)] = *coordinate;
    }
    return quad;
}

PDFMcpJsonReader PDFMcpJsonReader::child(QLatin1StringView key, bool required)
{
    std::optional<QJsonObject> object = getObject(key, required);
    return PDFMcpJsonReader(object.value_or(QJsonObject()), getKeyPath(key), m_errors);
}

PDFMcpJsonReader PDFMcpJsonReader::item(const QJsonValue& value, const QString& path)
{
    if (!value.isObject())
    {
        addPathError(path, QStringLiteral("expected an object, got %1").arg(PDFMcpJsonReaderHelper::typeName(value)));
        return PDFMcpJsonReader(QJsonObject(), path, m_errors);
    }
    return PDFMcpJsonReader(value.toObject(), path, m_errors);
}

void PDFMcpJsonReader::checkUnknownKeys(const QStringList& knownKeys)
{
    for (auto it = m_object.constBegin(); it != m_object.constEnd(); ++it)
    {
        if (!knownKeys.contains(it.key()))
        {
            const QString name = PDFMcpJson::sanitizeString(it.key(), PDFMcpJson::SanitizeFlag::RemoveBidi, 64);
            addPathError(m_path.isEmpty() ? name : m_path + QLatin1Char('.') + name, QStringLiteral("unknown property"));
        }
    }
}

// ----------------------------------------------------------------------------
// PDFMcpJsonWriter
// ----------------------------------------------------------------------------

void PDFMcpJsonWriter::beginValue()
{
    if (m_stack.empty())
    {
        Q_ASSERT(!m_hasValue);
        m_hasValue = true;
        return;
    }

    if (m_stack.back() == Scope::Array)
    {
        if (m_hasItems.back())
        {
            m_data.append(',');
        }
        m_hasItems.back() = true;
        return;
    }

    // An object: the key has been written
    Q_ASSERT(m_expectValue);
    m_expectValue = false;
}

void PDFMcpJsonWriter::beginObject()
{
    beginValue();
    m_data.append('{');
    m_stack.push_back(Scope::Object);
    m_hasItems.push_back(false);
}

void PDFMcpJsonWriter::endObject()
{
    Q_ASSERT(!m_stack.empty() && m_stack.back() == Scope::Object && !m_expectValue);
    m_data.append('}');
    m_stack.pop_back();
    m_hasItems.pop_back();
}

void PDFMcpJsonWriter::beginArray()
{
    beginValue();
    m_data.append('[');
    m_stack.push_back(Scope::Array);
    m_hasItems.push_back(false);
}

void PDFMcpJsonWriter::endArray()
{
    Q_ASSERT(!m_stack.empty() && m_stack.back() == Scope::Array);
    m_data.append(']');
    m_stack.pop_back();
    m_hasItems.pop_back();
}

void PDFMcpJsonWriter::key(QStringView name)
{
    Q_ASSERT(!m_stack.empty() && m_stack.back() == Scope::Object && !m_expectValue);
    if (m_hasItems.back())
    {
        m_data.append(',');
    }
    m_hasItems.back() = true;
    m_data.append('"');
    appendEscaped(name);
    m_data.append("\":");
    m_expectValue = true;
}

void PDFMcpJsonWriter::key(QLatin1StringView name)
{
    key(QStringView(QString(name)));
}

void PDFMcpJsonWriter::writeString(QStringView text)
{
    beginValue();
    m_data.append('"');
    appendEscaped(text);
    m_data.append('"');
}

void PDFMcpJsonWriter::writeString(QLatin1StringView text)
{
    writeString(QStringView(QString(text)));
}

void PDFMcpJsonWriter::writeInteger(qint64 value)
{
    beginValue();
    char buffer[32];
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    m_data.append(buffer, result.ptr - buffer);
}

void PDFMcpJsonWriter::writeNumber(double value)
{
    beginValue();
    m_data.append(formatNumber(value));
}

void PDFMcpJsonWriter::writeBool(bool value)
{
    beginValue();
    m_data.append(value ? "true" : "false");
}

void PDFMcpJsonWriter::writeNull()
{
    beginValue();
    m_data.append("null");
}

void PDFMcpJsonWriter::writeRaw(QByteArrayView json)
{
    beginValue();
    m_data.append(json);
}

void PDFMcpJsonWriter::writeRawMembers(QByteArrayView members)
{
    Q_ASSERT(!m_stack.empty() && m_stack.back() == Scope::Object && !m_expectValue);
    if (members.isEmpty())
    {
        return;
    }

    if (m_hasItems.back())
    {
        m_data.append(',');
    }
    m_hasItems.back() = true;
    m_data.append(members);
}

void PDFMcpJsonWriter::writeBase64(QByteArrayView data)
{
    static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    beginValue();
    const qsizetype encodedSize = ((data.size() + 2) / 3) * 4;
    const qsizetype start = m_data.size();
    m_data.resize(start + encodedSize + 2);
    char* output = m_data.data() + start;
    *output++ = '"';

    const unsigned char* input = reinterpret_cast<const unsigned char*>(data.data());
    const qsizetype size = data.size();
    qsizetype i = 0;
    for (; i + 2 < size; i += 3)
    {
        const quint32 triple = (quint32(input[i]) << 16) | (quint32(input[i + 1]) << 8) | quint32(input[i + 2]);
        *output++ = ALPHABET[(triple >> 18) & 0x3F];
        *output++ = ALPHABET[(triple >> 12) & 0x3F];
        *output++ = ALPHABET[(triple >> 6) & 0x3F];
        *output++ = ALPHABET[triple & 0x3F];
    }

    const qsizetype remaining = size - i;
    if (remaining == 1)
    {
        const quint32 triple = quint32(input[i]) << 16;
        *output++ = ALPHABET[(triple >> 18) & 0x3F];
        *output++ = ALPHABET[(triple >> 12) & 0x3F];
        *output++ = '=';
        *output++ = '=';
    }
    else if (remaining == 2)
    {
        const quint32 triple = (quint32(input[i]) << 16) | (quint32(input[i + 1]) << 8);
        *output++ = ALPHABET[(triple >> 18) & 0x3F];
        *output++ = ALPHABET[(triple >> 12) & 0x3F];
        *output++ = ALPHABET[(triple >> 6) & 0x3F];
        *output++ = '=';
    }
    *output++ = '"';
    Q_ASSERT(output == m_data.data() + m_data.size());
}

void PDFMcpJsonWriter::writeQJson(const QJsonValue& value)
{
    switch (value.type())
    {
        case QJsonValue::Bool:
            writeBool(value.toBool());
            break;

        case QJsonValue::Double:
        {
            const qint64 first = value.toInteger(0);
            const qint64 second = value.toInteger(1);
            if (first == second)
            {
                writeInteger(first);
            }
            else
            {
                writeNumber(value.toDouble());
            }
            break;
        }

        case QJsonValue::String:
            writeString(value.toString());
            break;

        case QJsonValue::Array:
        {
            beginArray();
            const QJsonArray array = value.toArray();
            for (const QJsonValue& item : array)
            {
                writeQJson(item);
            }
            endArray();
            break;
        }

        case QJsonValue::Object:
        {
            beginObject();
            const QJsonObject object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            {
                key(QStringView(it.key()));
                writeQJson(it.value());
            }
            endObject();
            break;
        }

        default:
            writeNull();
            break;
    }
}

void PDFMcpJsonWriter::writeStringArray(const QStringList& values)
{
    beginArray();
    for (const QString& value : values)
    {
        writeString(value);
    }
    endArray();
}

QByteArray PDFMcpJsonWriter::take()
{
    QByteArray data = std::move(m_data);
    m_data = QByteArray();
    m_stack.clear();
    m_hasItems.clear();
    m_expectValue = false;
    m_hasValue = false;
    return data;
}

void PDFMcpJsonWriter::appendEscaped(QStringView text)
{
    static constexpr char HEX[] = "0123456789abcdef";

    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size; ++i)
    {
        const char16_t unit = text[i].unicode();
        if (unit < 0x80)
        {
            switch (unit)
            {
                case u'"':
                    m_data.append("\\\"");
                    break;
                case u'\\':
                    m_data.append("\\\\");
                    break;
                case u'\b':
                    m_data.append("\\b");
                    break;
                case u'\f':
                    m_data.append("\\f");
                    break;
                case u'\n':
                    m_data.append("\\n");
                    break;
                case u'\r':
                    m_data.append("\\r");
                    break;
                case u'\t':
                    m_data.append("\\t");
                    break;
                default:
                    if (unit < 0x20)
                    {
                        const char escape[] = { '\\', 'u', '0', '0', HEX[(unit >> 4) & 0xF], HEX[unit & 0xF] };
                        m_data.append(escape, 6);
                    }
                    else
                    {
                        m_data.append(char(unit));
                    }
                    break;
            }
            continue;
        }

        char32_t codePoint = unit;
        if (QChar::isHighSurrogate(unit) && i + 1 < size && QChar::isLowSurrogate(text[i + 1].unicode()))
        {
            codePoint = QChar::surrogateToUcs4(unit, text[i + 1].unicode());
            ++i;
        }
        else if (QChar::isSurrogate(unit))
        {
            // A lone surrogate is written as its escape (exact echo of an id)
            const char escape[] = { '\\', 'u', HEX[(unit >> 12) & 0xF], HEX[(unit >> 8) & 0xF], HEX[(unit >> 4) & 0xF], HEX[unit & 0xF] };
            m_data.append(escape, 6);
            continue;
        }

        if (codePoint < 0x800)
        {
            m_data.append(char(0xC0 | (codePoint >> 6)));
            m_data.append(char(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint < 0x10000)
        {
            m_data.append(char(0xE0 | (codePoint >> 12)));
            m_data.append(char(0x80 | ((codePoint >> 6) & 0x3F)));
            m_data.append(char(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            m_data.append(char(0xF0 | (codePoint >> 18)));
            m_data.append(char(0x80 | ((codePoint >> 12) & 0x3F)));
            m_data.append(char(0x80 | ((codePoint >> 6) & 0x3F)));
            m_data.append(char(0x80 | (codePoint & 0x3F)));
        }
    }
}

QByteArray PDFMcpJsonWriter::quote(QStringView text)
{
    PDFMcpJsonWriter writer;
    writer.writeString(text);
    return writer.take();
}

QByteArray PDFMcpJsonWriter::formatNumber(double value)
{
    if (!std::isfinite(value))
    {
        return QByteArrayLiteral("null");
    }

    char buffer[64];
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc())
    {
        return QByteArrayLiteral("null");
    }
    return QByteArray(buffer, result.ptr - buffer);
}

}   // namespace pdf
