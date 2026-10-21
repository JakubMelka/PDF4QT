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

#ifndef PDFAPPROUTETABLE_H
#define PDFAPPROUTETABLE_H

#include "pdfappglobal.h"
#include "pdfapproute.h"

#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

class PDFAppPageFactory;

/// Kind of a tab of the window (newapp_navigation.md 3)
enum class PDFAppTabKind
{
    Home,       ///< The Home tab with the sidebar (one per window, not closable)
    NewTab,     ///< Tab New tab: only the route /open, without the sidebar
    Document,   ///< Tab of a document (step 3)
    Tool,       ///< Tool with its own tab (phase 2)
    Help        ///< The user manual /help/{topic}: at most one per window, closable, without the sidebar (owner decision O13)
};

/// Item of the navigation of the sidebar of the Home tab, which a route
/// highlights. The values are the positions of the items in the sidebar.
enum class PDFAppNavItem
{
    None = -1,
    Home = 0,
    Open,
    Recent,
    Tools,
    Ai,
    WhatsNew,
    About,
    Support,
    Sponsor,
    Settings
};

/// What a route shows: the subject Page Not Found names, when the route has no page
enum class PDFAppRouteSubject
{
    Page,               ///< A page with a title (Home, Settings, ...)
    Tool,               ///< A tool of the catalog (parameter toolId)
    ToolCollection,     ///< A collection of tools (parameter collectionId)
    SettingsSection,    ///< A section of Settings (parameter section)
    HelpTopic,          ///< A topic of the user manual (parameter topic)
    Version,            ///< Release notes of a version (parameter version)
    Document,           ///< A document tab (parameter documentId)
    Search              ///< Results of the global search
};

/// Values, which a parameter of a route (a segment or a query item) accepts
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRouteValues
{
public:
    enum class Kind
    {
        Text,           ///< Any text up to the maximal length (search text)
        Options,        ///< One of the listed values
        Number,         ///< A decimal number from 1 to 2^31 - 1, no sign, no leading zero
        Slug,           ///< Lowercase letters, digits, '.', '-', '_' (identifiers of tools, topics, panels)
        Version,        ///< Digits separated by dots, two to four parts ("1.6", "1.6.0.0")
        DocumentId      ///< Id of a document tab: 26 characters of base32 (128 bits of randomness)
    };

    PDFAppRouteValues() = default;

    static PDFAppRouteValues text(int maximumLength = 256);
    static PDFAppRouteValues options(const QStringList& values);
    static PDFAppRouteValues number();
    static PDFAppRouteValues slug();
    static PDFAppRouteValues version();
    static PDFAppRouteValues documentId();

    Kind getKind() const { return m_kind; }
    const QStringList& getOptions() const { return m_options; }

    /// The value is accepted
    bool accepts(const QString& value) const;

    /// Length of a document id
    static constexpr int DOCUMENT_ID_LENGTH = 26;

    /// Maximal length of a slug
    static constexpr int MAXIMUM_SLUG_LENGTH = 64;

private:
    Kind m_kind = Kind::Text;
    QStringList m_options;
    int m_maximumLength = 256;
};

/// Pattern of routes of the table: segments are literals or parameters
/// "{name}"; the parameters and the query keys have their accepted values.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRouteEntry
{
public:
    /// Text of the pattern ("/settings/{section}")
    QString pattern;

    /// Segments of the pattern
    QStringList segments;

    /// Tab, which shows the route
    PDFAppTabKind tabKind = PDFAppTabKind::Home;

    /// The route can be shown in a tab New tab (only /open)
    bool isNewTabRoute = false;

    /// Item of the navigation, which the route highlights
    PDFAppNavItem navItem = PDFAppNavItem::None;

    /// What the route shows and its title (source text in the context
    /// "pdf::PDFAppRouteTable", for Page Not Found and the window title)
    PDFAppRouteSubject subject = PDFAppRouteSubject::Page;
    const char* title = "";

    /// Accepted values of the parameters of the segments
    std::map<QString, PDFAppRouteValues> parameters;

    /// Accepted query keys and their values (a key, which is not here, is unknown)
    std::map<QString, PDFAppRouteValues> query;

    /// The route can have a fragment (anchor of a row of Settings, of a section)
    bool isFragmentAllowed = false;

    /// Redirect to these segments (the query and the fragment are kept), when
    /// not empty; with isRedirectOnlyWithoutQuery only a route without a query
    /// is redirected (/settings -> /settings/general, /settings?q= filters)
    QStringList redirect;
    bool isRedirectOnlyWithoutQuery = false;

    /// Factory of the page, nullptr = the page is not implemented yet (Page Not Found)
    std::shared_ptr<const PDFAppPageFactory> factory;

    /// Number of the literal segments (a literal match wins over a parameter)
    int getLiteralCount() const;

    /// Translated title
    QString getTitle() const;
};

/// Result of the matching of a route
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRouteMatch
{
public:
    enum class Result
    {
        Found,              ///< The route has a page
        Redirect,           ///< The route is redirected (getRedirectedRoute)
        InvalidRoute,       ///< The route itself is not valid (PDFAppRoute::getError)
        UnknownRoute,       ///< No pattern of the table matches
        InvalidParameter,   ///< A pattern matches, but a parameter, a query key or the fragment is not accepted
        NotImplemented      ///< The pattern has no page yet
    };

    Result result = Result::InvalidRoute;

    /// The matched entry (nullptr for InvalidRoute and UnknownRoute)
    const PDFAppRouteEntry* entry = nullptr;

    /// Values of the parameters of the pattern
    std::map<QString, QString> parameters;

    /// Name of the refused parameter or query key (InvalidParameter)
    QString refusedParameter;

    /// Target of a redirect
    PDFAppRoute redirectedRoute;

    bool isFound() const { return result == Result::Found; }

    /// Value of the parameter, empty if there is none
    QString getParameter(const QString& name) const;
};

/// Table of the routes of the application (newapp_navigation.md 3). It holds the
/// complete table of phase 1: every route is registered, a page, which does not
/// exist yet, has no factory and ends on Page Not Found. Later steps only set
/// the factories (setFactory).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRouteTable
{
public:
    PDFAppRouteTable() = default;

    /// Table with all routes of phase 1 (no factories)
    static PDFAppRouteTable createDefault();

    /// Adds the entry. \returns false for an invalid pattern or a pattern, which
    /// is already registered.
    bool addEntry(PDFAppRouteEntry entry);

    /// Sets the factory of the page of the pattern. \returns false for an unknown pattern.
    bool setFactory(const QString& pattern, std::shared_ptr<const PDFAppPageFactory> factory);

    /// Entry of the pattern, nullptr if there is none
    const PDFAppRouteEntry* findEntry(const QString& pattern) const;

    const std::vector<PDFAppRouteEntry>& getEntries() const { return m_entries; }

    /// Matches the route
    PDFAppRouteMatch match(const PDFAppRoute& route) const;

    /// Text, which names what the route shows (Page Not Found: "Tool “convert.epub”")
    QString describe(const PDFAppRoute& route) const;

    /// Text of the search offered by Page Not Found ("convert epub"), empty if
    /// there is nothing to search
    QString getSearchText(const PDFAppRoute& route) const;

    /// Maximal length of the text of the search offered by Page Not Found
    static constexpr int MAXIMUM_SEARCH_TEXT_LENGTH = 48;

private:
    static PDFAppRouteEntry createEntry(const QString& pattern, PDFAppTabKind tabKind, PDFAppNavItem navItem, PDFAppRouteSubject subject, const char* title);
    static QString toSearchWords(const QString& text);
    static QString getRouteParameter(const PDFAppRoute& route, const PDFAppRouteEntry& entry, const QString& name);
    void addDefaultEntry(PDFAppRouteEntry entry);
    static bool isParameterSegment(const QString& segment);
    static QString getParameterName(const QString& segment);

    std::vector<PDFAppRouteEntry> m_entries;
};

}   // namespace pdf

#endif // PDFAPPROUTETABLE_H
