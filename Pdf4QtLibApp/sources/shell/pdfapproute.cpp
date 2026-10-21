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

#include "pdfapproute.h"
#include "pdfdocumenttoolsearch.h"

#include <QStringDecoder>
#include <QUrl>

namespace pdf
{

PDFAppRoute PDFAppRoute::create(const QStringList& segments, const QList<QueryItem>& query, const QString& fragment)
{
    PDFAppRoute route;
    route.m_segments = segments;
    route.m_query = query;
    route.m_fragment = fragment;
    route.m_error = Error::None;
    route.validate();
    route.encode();
    if (!route.isValid() && route.m_text.isEmpty())
    {
        // The decoded path is shown to the user (Page Not Found)
        route.m_text = shorten(QLatin1Char('/') + segments.join(QLatin1Char('/')));
    }
    route.m_requestedText = shorten(route.m_text);
    return route;
}

PDFAppRoute PDFAppRoute::parse(const QString& text)
{
    PDFAppRoute route;
    route.m_requestedText = shorten(text);
    route.m_text = route.m_requestedText;

    if (text.isEmpty())
    {
        route.m_error = Error::Empty;
        return route;
    }

    if (text.size() > MAXIMUM_LENGTH)
    {
        route.m_error = Error::TooLong;
        return route;
    }

    if (!text.startsWith(QLatin1Char('/')))
    {
        route.m_error = Error::NotAbsolute;
        return route;
    }

    // Control characters are refused before decoding (a decoded one later)
    if (hasControlCharacter(text))
    {
        route.m_error = Error::InvalidCharacter;
        return route;
    }

    QStringView rest(text);
    QStringView fragmentText;
    QStringView queryText;
    bool hasFragment = false;
    bool hasQuery = false;

    const qsizetype fragmentIndex = rest.indexOf(QLatin1Char('#'));
    if (fragmentIndex >= 0)
    {
        fragmentText = rest.mid(fragmentIndex + 1);
        rest = rest.left(fragmentIndex);
        hasFragment = true;
    }

    const qsizetype queryIndex = rest.indexOf(QLatin1Char('?'));
    if (queryIndex >= 0)
    {
        queryText = rest.mid(queryIndex + 1);
        rest = rest.left(queryIndex);
        hasQuery = true;
    }

    // Path: "/" alone has no segment
    QStringView path = rest.mid(1);
    if (!path.isEmpty())
    {
        const QList<QStringView> parts = path.split(QLatin1Char('/'));
        if (parts.size() > MAXIMUM_SEGMENTS)
        {
            route.m_error = Error::TooManySegments;
            return route;
        }

        for (const QStringView& part : parts)
        {
            if (part.isEmpty())
            {
                route.m_error = Error::EmptySegment;
                return route;
            }

            QString segment;
            if (!decode(part, &segment))
            {
                route.m_error = Error::InvalidEncoding;
                return route;
            }
            route.m_segments << segment;
        }
    }

    if (hasQuery && !queryText.isEmpty())
    {
        const QList<QStringView> items = queryText.split(QLatin1Char('&'));
        if (items.size() > MAXIMUM_QUERY_ITEMS)
        {
            route.m_error = Error::InvalidQuery;
            return route;
        }

        for (const QStringView& item : items)
        {
            if (item.isEmpty())
            {
                route.m_error = Error::InvalidQuery;
                return route;
            }

            const qsizetype equalIndex = item.indexOf(QLatin1Char('='));
            const QStringView keyText = equalIndex >= 0 ? item.left(equalIndex) : item;
            const QStringView valueText = equalIndex >= 0 ? item.mid(equalIndex + 1) : QStringView();

            QString key;
            QString value;
            if (!decode(keyText, &key) || !decode(valueText, &value))
            {
                route.m_error = Error::InvalidEncoding;
                return route;
            }
            route.m_query.append(QueryItem(key, value));
        }
    }

    if (hasFragment)
    {
        QString fragment;
        if (!decode(fragmentText, &fragment))
        {
            route.m_error = Error::InvalidEncoding;
            return route;
        }
        route.m_fragment = fragment;
    }

    route.m_error = Error::None;
    route.validate();
    if (route.isValid())
    {
        // The text of a valid route is its normalized encoding
        route.encode();
    }
    return route;
}

void PDFAppRoute::validate()
{
    if (m_error != Error::None)
    {
        return;
    }

    if (m_segments.size() > MAXIMUM_SEGMENTS)
    {
        m_error = Error::TooManySegments;
        return;
    }

    for (const QString& segment : std::as_const(m_segments))
    {
        if (segment.isEmpty())
        {
            m_error = Error::EmptySegment;
            return;
        }

        if (segment == QLatin1String(".") || segment == QLatin1String(".."))
        {
            m_error = Error::DotSegment;
            return;
        }

        // A lone surrogate (a created route) has no UTF-8 encoding: its text
        // would not parse back to the same route
        if (!segment.isValidUtf16())
        {
            m_error = Error::InvalidEncoding;
            return;
        }

        // A slash inside of a segment would change the path, a control character
        // (also an encoded %00) never belongs to a route
        if (hasControlCharacter(segment) || segment.contains(QLatin1Char('/')) || segment.contains(QLatin1Char('\\')))
        {
            m_error = Error::InvalidCharacter;
            return;
        }
    }

    if (m_query.size() > MAXIMUM_QUERY_ITEMS)
    {
        m_error = Error::InvalidQuery;
        return;
    }

    QStringList keys;
    for (const QueryItem& item : std::as_const(m_query))
    {
        if (!isValidKey(item.first))
        {
            m_error = Error::InvalidQuery;
            return;
        }

        if (hasControlCharacter(item.second))
        {
            m_error = Error::InvalidCharacter;
            return;
        }

        if (!item.second.isValidUtf16())
        {
            m_error = Error::InvalidEncoding;
            return;
        }

        if (keys.contains(item.first))
        {
            m_error = Error::DuplicateQueryKey;
            return;
        }
        keys << item.first;
    }

    if (m_fragment.size() > MAXIMUM_FRAGMENT_LENGTH || hasControlCharacter(m_fragment) || !m_fragment.isValidUtf16())
    {
        m_error = Error::InvalidFragment;
        return;
    }
}

void PDFAppRoute::encode()
{
    if (m_error != Error::None)
    {
        return;
    }

    QString text;
    if (m_segments.isEmpty())
    {
        text = QStringLiteral("/");
    }

    for (const QString& segment : std::as_const(m_segments))
    {
        text += QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(segment));
    }

    for (qsizetype i = 0; i < m_query.size(); ++i)
    {
        text += i == 0 ? QLatin1Char('?') : QLatin1Char('&');
        text += QString::fromLatin1(QUrl::toPercentEncoding(m_query[i].first));
        text += QLatin1Char('=');
        text += QString::fromLatin1(QUrl::toPercentEncoding(m_query[i].second));
    }

    if (!m_fragment.isEmpty())
    {
        text += QLatin1Char('#') + QString::fromLatin1(QUrl::toPercentEncoding(m_fragment));
    }

    // The encoded text must fit into the limit, so parse(toString()) gives the same route
    if (text.size() > MAXIMUM_LENGTH)
    {
        m_error = Error::TooLong;
        m_text = shorten(text);
        return;
    }

    m_text = text;
}

bool PDFAppRoute::decode(QStringView text, QString* decoded)
{
    QByteArray bytes;
    bytes.reserve(text.size());

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];
        if (character == QLatin1Char('%'))
        {
            // Exactly two hexadecimal digits follow
            if (i + 2 >= text.size())
            {
                return false;
            }

            const int high = getHexadecimalValue(text[i + 1]);
            const int low = getHexadecimalValue(text[i + 2]);
            if (high < 0 || low < 0)
            {
                return false;
            }

            bytes.append(char(high * 16 + low));
            i += 2;
        }
        else if (character.unicode() < 128)
        {
            bytes.append(char(character.unicode()));
        }
        else
        {
            // A raw character of a link (not encoded), surrogate pairs stay together
            qsizetype length = 1;
            if (character.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
            {
                length = 2;
            }
            else if (character.isSurrogate())
            {
                return false;
            }
            bytes.append(text.mid(i, length).toUtf8());
            i += length - 1;
        }
    }

    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString result = decoder.decode(bytes);
    if (decoder.hasError())
    {
        return false;
    }

    *decoded = std::move(result);
    return true;
}

int PDFAppRoute::getHexadecimalValue(QChar character)
{
    const char16_t code = character.unicode();
    if (code >= u'0' && code <= u'9')
    {
        return code - u'0';
    }
    if (code >= u'a' && code <= u'f')
    {
        return code - u'a' + 10;
    }
    if (code >= u'A' && code <= u'F')
    {
        return code - u'A' + 10;
    }
    return -1;
}

bool PDFAppRoute::hasControlCharacter(const QString& text)
{
    for (const QChar character : text)
    {
        const char16_t code = character.unicode();
        if (code < 0x20 || (code >= 0x7F && code <= 0x9F) || code == 0x2028 || code == 0x2029)
        {
            return true;
        }
    }
    return false;
}

bool PDFAppRoute::isValidKey(const QString& key)
{
    if (key.isEmpty() || key.size() > MAXIMUM_KEY_LENGTH)
    {
        return false;
    }

    for (qsizetype i = 0; i < key.size(); ++i)
    {
        const char16_t code = key[i].unicode();
        const bool isLetter = (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z');
        const bool isDigit = code >= u'0' && code <= u'9';
        if (!(isLetter || (i > 0 && (isDigit || code == u'-' || code == u'_'))))
        {
            return false;
        }
    }
    return true;
}

QString PDFAppRoute::shorten(const QString& text)
{
    if (text.size() <= MAXIMUM_LENGTH)
    {
        return text;
    }

    QString result = text.left(MAXIMUM_LENGTH);
    if (result.back().isHighSurrogate())
    {
        result.chop(1);
    }
    return result + QChar(0x2026);
}

QString PDFAppRoute::getSegment(qsizetype index) const
{
    return m_segments.value(index);
}

bool PDFAppRoute::hasQueryItem(const QString& key) const
{
    for (const QueryItem& item : m_query)
    {
        if (item.first == key)
        {
            return true;
        }
    }
    return false;
}

QString PDFAppRoute::getQueryValue(const QString& key) const
{
    for (const QueryItem& item : m_query)
    {
        if (item.first == key)
        {
            return item.second;
        }
    }
    return QString();
}

PDFAppRoute PDFAppRoute::withFragment(const QString& fragment) const
{
    if (!isValid())
    {
        return *this;
    }
    return create(m_segments, m_query, fragment);
}

PDFAppRoute PDFAppRoute::withoutQuery() const
{
    if (!isValid())
    {
        return *this;
    }
    return create(m_segments);
}

QString PDFAppRoute::getPath() const
{
    if (!isValid())
    {
        return m_requestedText;
    }
    return QLatin1Char('/') + m_segments.join(QLatin1Char('/'));
}

QString PDFAppRoute::toString() const
{
    return m_text;
}

bool PDFAppRoute::operator==(const PDFAppRoute& other) const
{
    if (isValid() != other.isValid())
    {
        return false;
    }

    if (!isValid())
    {
        return m_error == other.m_error && m_requestedText == other.m_requestedText;
    }

    return m_segments == other.m_segments && m_query == other.m_query && m_fragment == other.m_fragment;
}

PDFAppRoute PDFAppRoutes::home()
{
    return PDFAppRoute::create({ QStringLiteral("home") });
}

PDFAppRoute PDFAppRoutes::open()
{
    return PDFAppRoute::create({ QStringLiteral("open") });
}

PDFAppRoute PDFAppRoutes::recent(const QString& filter, const QString& sort, const QString& searchText)
{
    QList<PDFAppRoute::QueryItem> query;
    if (!filter.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("filter"), filter));
    }
    if (!sort.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("sort"), sort));
    }
    if (!searchText.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("q"), searchText));
    }
    return PDFAppRoute::create({ QStringLiteral("recent") }, query);
}

PDFAppRoute PDFAppRoutes::tools(const QString& category)
{
    QList<PDFAppRoute::QueryItem> query;
    if (!category.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("category"), category));
    }
    return PDFAppRoute::create({ QStringLiteral("tools") }, query);
}

PDFAppRoute PDFAppRoutes::tool(const QString& toolId)
{
    return PDFAppRoute::create({ QStringLiteral("tools"), toolId });
}

PDFAppRoute PDFAppRoutes::toolCollections()
{
    return PDFAppRoute::create({ QStringLiteral("tools"), QStringLiteral("collections") });
}

PDFAppRoute PDFAppRoutes::toolCollection(const QString& collectionId)
{
    return PDFAppRoute::create({ QStringLiteral("tools"), QStringLiteral("collections"), collectionId });
}

PDFAppRoute PDFAppRoutes::ai()
{
    return PDFAppRoute::create({ QStringLiteral("ai") });
}

PDFAppRoute PDFAppRoutes::whatsNew(const QString& version)
{
    if (version.isEmpty())
    {
        return PDFAppRoute::create({ QStringLiteral("whats-new") });
    }
    return PDFAppRoute::create({ QStringLiteral("whats-new"), version });
}

PDFAppRoute PDFAppRoutes::releaseNotes(const QString& version)
{
    return PDFAppRoute::create({ QStringLiteral("whats-new"), version, QStringLiteral("notes") });
}

PDFAppRoute PDFAppRoutes::about(const QString& anchor)
{
    return PDFAppRoute::create({ QStringLiteral("about") }, QList<PDFAppRoute::QueryItem>(), anchor);
}

PDFAppRoute PDFAppRoutes::support(const QString& topic, const QString& searchText)
{
    QList<PDFAppRoute::QueryItem> query;
    if (!topic.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("topic"), topic));
    }
    if (!searchText.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("q"), searchText));
    }
    return PDFAppRoute::create({ QStringLiteral("support") }, query);
}

PDFAppRoute PDFAppRoutes::sponsor()
{
    return PDFAppRoute::create({ QStringLiteral("sponsor") });
}

PDFAppRoute PDFAppRoutes::help(const QString& topic)
{
    return PDFAppRoute::create({ QStringLiteral("help"), topic });
}

PDFAppRoute PDFAppRoutes::settings(const QString& section, const QString& anchor)
{
    return PDFAppRoute::create({ QStringLiteral("settings"), section.isEmpty() ? QStringLiteral("general") : section }, QList<PDFAppRoute::QueryItem>(), anchor);
}

PDFAppRoute PDFAppRoutes::search(const QString& text, const QString& group)
{
    // The text of a field is sanitized as the page does it: a control character or a
    // lone surrogate (a field cut at its maximum length) would make the route invalid
    // (Page Not Found), a longer text would be refused by the route table
    QList<PDFAppRoute::QueryItem> query;
    query.append(PDFAppRoute::QueryItem(QStringLiteral("q"), PDFSearchQuery::sanitize(text)));
    if (!group.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("group"), group));
    }
    return PDFAppRoute::create({ QStringLiteral("search") }, query);
}

PDFAppRoute PDFAppRoutes::document(const QString& documentId, int page)
{
    QList<PDFAppRoute::QueryItem> query;
    if (page > 0)
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("page"), QString::number(page)));
    }
    return PDFAppRoute::create({ QStringLiteral("document"), documentId }, query);
}

PDFAppRoute PDFAppRoutes::documentProperties(const QString& documentId, const QString& section)
{
    return PDFAppRoute::create({ QStringLiteral("document"), documentId, QStringLiteral("properties"), section });
}

PDFAppRoute PDFAppRoutes::documentTool(const QString& documentId, const QString& toolId)
{
    return PDFAppRoute::create({ QStringLiteral("document"), documentId, QStringLiteral("tool"), toolId });
}

PDFAppRoute PDFAppRoutes::toolTab(const QString& toolId, const QString& instanceId)
{
    return PDFAppRoute::create({ QStringLiteral("tool"), toolId, instanceId });
}

}   // namespace pdf
