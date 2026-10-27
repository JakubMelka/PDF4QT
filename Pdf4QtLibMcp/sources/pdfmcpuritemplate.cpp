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

#include "pdfmcpuritemplate.h"

#include <QByteArray>
#include <QStringDecoder>

namespace pdf
{

/// Helpers of the URI templates
class PDFMcpUriTemplateHelper
{
public:
    PDFMcpUriTemplateHelper() = delete;

    static bool isNameCharacter(QChar c)
    {
        const char16_t unit = c.unicode();
        return (unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') || unit == u'_';
    }

    /// Value of a variable from the raw text of the URI (std::nullopt = invalid)
    static std::optional<QString> decodeValue(QStringView raw)
    {
        if (raw.isEmpty())
        {
            return std::nullopt;
        }

        for (const QChar c : raw)
        {
            const char16_t unit = c.unicode();
            if (unit == u'/' || unit == u'?' || unit == u'#' || unit < 0x21 || unit == 0x7F || unit > 0x7E)
            {
                return std::nullopt;
            }
        }

        // Percent escapes must be complete
        for (qsizetype i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == u'%')
            {
                if (i + 2 >= raw.size() || !isHex(raw[i + 1]) || !isHex(raw[i + 2]))
                {
                    return std::nullopt;
                }
                i += 2;
            }
        }

        const QByteArray bytes = QByteArray::fromPercentEncoding(raw.toLatin1());
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString value = decoder.decode(bytes);
        if (decoder.hasError() || value.isEmpty())
        {
            return std::nullopt;
        }

        for (const QChar c : value)
        {
            if (c.unicode() < 0x20 || (c.unicode() >= 0x7F && c.unicode() <= 0x9F) || c == u'/')
            {
                return std::nullopt;
            }
        }
        return value;
    }

    static bool isHex(QChar c)
    {
        const char16_t unit = c.unicode();
        return (unit >= u'0' && unit <= u'9') || (unit >= u'a' && unit <= u'f') || (unit >= u'A' && unit <= u'F');
    }
};

PDFMcpUriTemplate::PDFMcpUriTemplate(const QString& text) :
    m_text(text)
{
    qsizetype position = 0;
    while (position < text.size())
    {
        const qsizetype open = text.indexOf(u'{', position);
        const qsizetype close = text.indexOf(u'}', position);
        if (close >= 0 && (open < 0 || close < open))
        {
            return;
        }

        if (open < 0)
        {
            m_parts.push_back({ false, text.mid(position) });
            break;
        }

        if (open > position)
        {
            m_parts.push_back({ false, text.mid(position, open - position) });
        }

        if (close < 0)
        {
            return;
        }

        const QString name = text.mid(open + 1, close - open - 1);
        if (name.isEmpty())
        {
            return;
        }
        for (const QChar c : name)
        {
            if (!PDFMcpUriTemplateHelper::isNameCharacter(c))
            {
                return;
            }
        }

        if (!m_parts.empty() && m_parts.back().isVariable)
        {
            // Two adjacent variables cannot be matched
            return;
        }
        m_parts.push_back({ true, name });
        position = close + 1;
    }

    m_isValid = !m_parts.empty();
}

QStringList PDFMcpUriTemplate::getVariables() const
{
    QStringList variables;
    for (const Part& part : m_parts)
    {
        if (part.isVariable)
        {
            variables << part.text;
        }
    }
    return variables;
}

std::optional<QMap<QString, QString>> PDFMcpUriTemplate::match(const QString& uri) const
{
    if (!m_isValid || uri.size() > MAXIMUM_URI_LENGTH)
    {
        return std::nullopt;
    }

    QMap<QString, QString> values;
    qsizetype position = 0;
    for (size_t i = 0; i < m_parts.size(); ++i)
    {
        const Part& part = m_parts[i];
        if (!part.isVariable)
        {
            if (QStringView(uri).mid(position).startsWith(part.text) == false)
            {
                return std::nullopt;
            }
            position += part.text.size();
            continue;
        }

        // The value ends at the next literal (or at the end of the URI)
        qsizetype end = uri.size();
        if (i + 1 < m_parts.size())
        {
            end = uri.indexOf(m_parts[i + 1].text, position + 1);
            if (end < 0)
            {
                return std::nullopt;
            }
        }

        std::optional<QString> value = PDFMcpUriTemplateHelper::decodeValue(QStringView(uri).mid(position, end - position));
        if (!value)
        {
            return std::nullopt;
        }
        values.insert(part.text, *value);
        position = end;
    }

    if (position != uri.size())
    {
        return std::nullopt;
    }
    return values;
}

QString PDFMcpUriTemplate::expand(const QMap<QString, QString>& values) const
{
    QString uri;
    for (const Part& part : m_parts)
    {
        if (part.isVariable)
        {
            uri += QString::fromLatin1(values.value(part.text).toUtf8().toPercentEncoding());
        }
        else
        {
            uri += part.text;
        }
    }
    return uri;
}

}   // namespace pdf
