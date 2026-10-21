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

#ifndef PDFAPPROUTE_H
#define PDFAPPROUTE_H

#include "pdfappglobal.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <utility>

namespace pdf
{

/// Route of a page of the application (newapp_navigation.md 2): an absolute path
/// of segments ("/settings/security"), query items ("?filter=pinned") and a
/// fragment ("#signature-verification"). A route is a link: the navigation, the
/// links of pages, the search and the documentation pass routes, never widgets.
///
/// A route is untrusted input (documentation, MCP, a typed link). parse() refuses
/// everything, which is not clearly a route of the application: a relative path,
/// empty segments ("//", a trailing slash), the segments "." and "..", invalid
/// percent-encoding or UTF-8, control characters (also an encoded %00), an
/// encoded slash inside of a segment, a malformed or repeated query key and an
/// overlong text. A refused route is not valid, it keeps the requested text for
/// Page Not Found (shortened to MAXIMUM_LENGTH).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRoute
{
public:
    /// Maximal length of the text of a route
    static constexpr qsizetype MAXIMUM_LENGTH = 2048;

    /// Maximal number of segments of the path
    static constexpr qsizetype MAXIMUM_SEGMENTS = 16;

    /// Maximal number of query items
    static constexpr qsizetype MAXIMUM_QUERY_ITEMS = 16;

    /// Maximal length of a query key
    static constexpr qsizetype MAXIMUM_KEY_LENGTH = 32;

    /// Maximal length of the fragment
    static constexpr qsizetype MAXIMUM_FRAGMENT_LENGTH = 128;

    /// Why a route is not valid
    enum class Error
    {
        None,               ///< The route is valid
        Empty,              ///< Empty text
        NotAbsolute,        ///< The path does not start by '/'
        TooLong,            ///< The text is longer than MAXIMUM_LENGTH
        InvalidEncoding,    ///< Invalid percent-encoding or UTF-8
        InvalidCharacter,   ///< A control character (also %00) or an encoded slash
        EmptySegment,       ///< "//" or a trailing slash
        DotSegment,         ///< A segment "." or ".."
        TooManySegments,    ///< More than MAXIMUM_SEGMENTS segments
        InvalidQuery,       ///< Malformed query (empty item, invalid key, too many items)
        DuplicateQueryKey,  ///< A query key is given twice
        InvalidFragment     ///< Fragment too long or with control characters
    };

    using QueryItem = std::pair<QString, QString>;

    /// Invalid route (Error::Empty)
    PDFAppRoute() = default;

    /// Route of the decoded segments, query items and fragment. The parts are
    /// checked as by parse (isValid tells the result).
    static PDFAppRoute create(const QStringList& segments, const QList<QueryItem>& query = QList<QueryItem>(), const QString& fragment = QString());

    /// Parses the text of a route (percent-encoded parts)
    static PDFAppRoute parse(const QString& text);

    bool isValid() const { return m_error == Error::None; }
    Error getError() const { return m_error; }

    /// Decoded segments of the path ("/" has no segment)
    const QStringList& getSegments() const { return m_segments; }

    /// Segment of the index, an empty string out of the range
    QString getSegment(qsizetype index) const;

    /// Decoded query items in their order
    const QList<QueryItem>& getQueryItems() const { return m_query; }

    bool hasQueryItem(const QString& key) const;

    /// Value of the query item, an empty string if there is none
    QString getQueryValue(const QString& key) const;

    /// Decoded fragment (without '#'), empty if there is none
    const QString& getFragment() const { return m_fragment; }

    /// Route with another fragment (empty = none)
    PDFAppRoute withFragment(const QString& fragment) const;

    /// Route of the same path without the query and the fragment
    PDFAppRoute withoutQuery() const;

    /// Decoded path ("/settings/security") for the user (Page Not Found)
    QString getPath() const;

    /// Text of the route (percent-encoded), the requested text for an invalid route
    QString toString() const;

    /// Text, which was requested (parse), or toString of a created route. It is
    /// shortened to MAXIMUM_LENGTH characters with an ellipsis.
    const QString& getRequestedText() const { return m_requestedText; }

    bool operator==(const PDFAppRoute& other) const;
    bool operator!=(const PDFAppRoute& other) const { return !(*this == other); }

private:
    void validate();
    void encode();

    static bool decode(QStringView text, QString* decoded);
    static int getHexadecimalValue(QChar character);
    static bool hasControlCharacter(const QString& text);
    static bool isValidKey(const QString& key);
    static QString shorten(const QString& text);

    QStringList m_segments;
    QList<QueryItem> m_query;
    QString m_fragment;
    QString m_requestedText;
    QString m_text;
    Error m_error = Error::Empty;
};

/// Builders of the routes of the application (newapp_navigation.md 3, 4.):
/// routes are never put together from strings elsewhere
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRoutes
{
public:
    PDFAppRoutes() = delete;

    static PDFAppRoute home();
    static PDFAppRoute open();

    /// Recent: the filter (files, folders, workspaces, pinned; empty = all), the
    /// sort (last-opened, name, folder; empty = the default last-opened) and the
    /// search text (empty = none); an empty part is not in the query
    static PDFAppRoute recent(const QString& filter = QString(), const QString& sort = QString(), const QString& searchText = QString());

    /// Catalog of the tools (category: pages, convert, edit, protect, optimize, review, pinned)
    static PDFAppRoute tools(const QString& category = QString());

    /// Introduction page of a tool of the catalog
    static PDFAppRoute tool(const QString& toolId);

    static PDFAppRoute toolCollections();
    static PDFAppRoute toolCollection(const QString& collectionId);
    static PDFAppRoute ai();

    /// What's New of the running version (empty) or of the version
    static PDFAppRoute whatsNew(const QString& version = QString());

    /// Release notes of the version (/whats-new/{version}/notes)
    static PDFAppRoute releaseNotes(const QString& version);
    static PDFAppRoute about(const QString& anchor = QString());
    /// Support: the topic of the video tutorials (for-you or a topic of
    /// PDFSupportVideoCatalog; empty = all) and the search text (empty = none)
    static PDFAppRoute support(const QString& topic = QString(), const QString& searchText = QString());
    static PDFAppRoute sponsor();
    static PDFAppRoute help(const QString& topic);

    /// Topic of the manual opened by the command Help (F1)
    static QString getDefaultHelpTopic() { return QStringLiteral("getting-started"); }

    /// Section of Settings (general when empty) with the anchor of a setting
    static PDFAppRoute settings(const QString& section = QString(), const QString& anchor = QString());

    /// Global search (Show all results) of the text, optionally in the group; the
    /// text is sanitized (PDFSearchQuery::sanitize), so the route is always valid
    static PDFAppRoute search(const QString& text, const QString& group = QString());

    /// Document tab (page = physical page from 1, 0 = none)
    static PDFAppRoute document(const QString& documentId, int page = 0);
    static PDFAppRoute documentProperties(const QString& documentId, const QString& section);
    static PDFAppRoute documentTool(const QString& documentId, const QString& toolId);

    /// Tool with its own tab
    static PDFAppRoute toolTab(const QString& toolId, const QString& instanceId);
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppRoute)

#endif // PDFAPPROUTE_H
