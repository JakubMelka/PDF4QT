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

#include "pdfapproutetable.h"
#include "pdfapppage.h"
#include "pdfappbuiltintoolcollections.h"

#include "pdfdocumenttoolcatalog.h"

#include <QCoreApplication>

#include <limits>

namespace pdf
{

PDFAppRouteValues PDFAppRouteValues::text(int maximumLength)
{
    PDFAppRouteValues values;
    values.m_kind = Kind::Text;
    values.m_maximumLength = qMax(0, maximumLength);
    return values;
}

PDFAppRouteValues PDFAppRouteValues::options(const QStringList& values)
{
    PDFAppRouteValues result;
    result.m_kind = Kind::Options;
    result.m_options = values;
    return result;
}

PDFAppRouteValues PDFAppRouteValues::number()
{
    PDFAppRouteValues values;
    values.m_kind = Kind::Number;
    return values;
}

PDFAppRouteValues PDFAppRouteValues::slug()
{
    PDFAppRouteValues values;
    values.m_kind = Kind::Slug;
    return values;
}

PDFAppRouteValues PDFAppRouteValues::version()
{
    PDFAppRouteValues values;
    values.m_kind = Kind::Version;
    return values;
}

PDFAppRouteValues PDFAppRouteValues::documentId()
{
    PDFAppRouteValues values;
    values.m_kind = Kind::DocumentId;
    return values;
}

bool PDFAppRouteValues::accepts(const QString& value) const
{
    switch (m_kind)
    {
        case Kind::Text:
            return value.size() <= m_maximumLength;

        case Kind::Options:
            return m_options.contains(value);

        case Kind::Number:
        {
            // 1 to 2^31 - 1 written plainly (no sign, no leading zero, no spaces)
            if (value.isEmpty() || value.size() > 10 || value.front() == QLatin1Char('0'))
            {
                return false;
            }
            for (const QChar character : value)
            {
                if (character.unicode() < u'0' || character.unicode() > u'9')
                {
                    return false;
                }
            }
            return value.toLongLong() <= std::numeric_limits<int>::max();
        }

        case Kind::Slug:
        {
            if (value.isEmpty() || value.size() > MAXIMUM_SLUG_LENGTH)
            {
                return false;
            }
            for (qsizetype i = 0; i < value.size(); ++i)
            {
                const char16_t code = value[i].unicode();
                const bool isAlphanumeric = (code >= u'a' && code <= u'z') || (code >= u'0' && code <= u'9');
                const bool isSeparator = code == u'.' || code == u'-' || code == u'_';
                if (!isAlphanumeric && !(isSeparator && i > 0))
                {
                    return false;
                }
            }
            return true;
        }

        case Kind::Version:
        {
            const QStringList parts = value.split(QLatin1Char('.'));
            if (parts.size() < 2 || parts.size() > 4)
            {
                return false;
            }
            for (const QString& part : parts)
            {
                if (part.isEmpty() || part.size() > 5)
                {
                    return false;
                }
                for (const QChar character : part)
                {
                    if (character.unicode() < u'0' || character.unicode() > u'9')
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        case Kind::DocumentId:
        {
            // RFC 4648 base32 alphabet in lowercase (routes are lowercase)
            if (value.size() != DOCUMENT_ID_LENGTH)
            {
                return false;
            }
            for (const QChar character : value)
            {
                const char16_t code = character.unicode();
                if (!((code >= u'a' && code <= u'z') || (code >= u'2' && code <= u'7')))
                {
                    return false;
                }
            }
            return true;
        }
    }

    return false;
}

int PDFAppRouteEntry::getLiteralCount() const
{
    int count = 0;
    for (const QString& segment : segments)
    {
        if (!segment.startsWith(QLatin1Char('{')))
        {
            ++count;
        }
    }
    return count;
}

QString PDFAppRouteEntry::getTitle() const
{
    return QCoreApplication::translate("pdf::PDFAppRouteTable", title ? title : "");
}

QString PDFAppRouteMatch::getParameter(const QString& name) const
{
    auto it = parameters.find(name);
    return it != parameters.end() ? it->second : QString();
}

bool PDFAppRouteTable::isParameterSegment(const QString& segment)
{
    return segment.size() > 2 && segment.startsWith(QLatin1Char('{')) && segment.endsWith(QLatin1Char('}'));
}

QString PDFAppRouteTable::getParameterName(const QString& segment)
{
    return segment.mid(1, segment.size() - 2);
}

bool PDFAppRouteTable::addEntry(PDFAppRouteEntry entry)
{
    if (!entry.pattern.startsWith(QLatin1Char('/')) || findEntry(entry.pattern))
    {
        return false;
    }

    QStringList segments;
    if (entry.pattern != QLatin1String("/"))
    {
        segments = entry.pattern.mid(1).split(QLatin1Char('/'));
    }

    for (const QString& segment : std::as_const(segments))
    {
        if (segment.isEmpty())
        {
            return false;
        }

        // Every parameter has its accepted values
        if (isParameterSegment(segment) && !entry.parameters.count(getParameterName(segment)))
        {
            return false;
        }
    }

    entry.segments = segments;
    m_entries.push_back(std::move(entry));
    return true;
}

bool PDFAppRouteTable::setFactory(const QString& pattern, std::shared_ptr<const PDFAppPageFactory> factory)
{
    for (PDFAppRouteEntry& entry : m_entries)
    {
        if (entry.pattern == pattern)
        {
            entry.factory = std::move(factory);
            return true;
        }
    }
    return false;
}

const PDFAppRouteEntry* PDFAppRouteTable::findEntry(const QString& pattern) const
{
    for (const PDFAppRouteEntry& entry : m_entries)
    {
        if (entry.pattern == pattern)
        {
            return &entry;
        }
    }
    return nullptr;
}

PDFAppRouteMatch PDFAppRouteTable::match(const PDFAppRoute& route) const
{
    PDFAppRouteMatch result;
    if (!route.isValid())
    {
        result.result = PDFAppRouteMatch::Result::InvalidRoute;
        return result;
    }

    // The pattern with most literal segments wins (/tools/collections over /tools/{toolId})
    const QStringList& segments = route.getSegments();
    const PDFAppRouteEntry* bestEntry = nullptr;
    int bestLiteralCount = -1;

    for (const PDFAppRouteEntry& entry : m_entries)
    {
        if (entry.segments.size() != segments.size())
        {
            continue;
        }

        bool isMatch = true;
        for (qsizetype i = 0; isMatch && i < segments.size(); ++i)
        {
            isMatch = isParameterSegment(entry.segments[i]) || entry.segments[i] == segments[i];
        }

        const int literalCount = entry.getLiteralCount();
        if (isMatch && literalCount > bestLiteralCount)
        {
            bestEntry = &entry;
            bestLiteralCount = literalCount;
        }
    }

    if (!bestEntry)
    {
        result.result = PDFAppRouteMatch::Result::UnknownRoute;
        return result;
    }

    result.entry = bestEntry;
    result.result = PDFAppRouteMatch::Result::InvalidParameter;

    for (qsizetype i = 0; i < segments.size(); ++i)
    {
        const QString& patternSegment = bestEntry->segments[i];
        if (!isParameterSegment(patternSegment))
        {
            continue;
        }

        const QString name = getParameterName(patternSegment);
        auto it = bestEntry->parameters.find(name);
        if (it == bestEntry->parameters.end() || !it->second.accepts(segments[i]))
        {
            result.refusedParameter = name;
            return result;
        }
        result.parameters[name] = segments[i];
    }

    for (const PDFAppRoute::QueryItem& item : route.getQueryItems())
    {
        auto it = bestEntry->query.find(item.first);
        if (it == bestEntry->query.end() || !it->second.accepts(item.second))
        {
            result.refusedParameter = item.first;
            return result;
        }
    }

    if (!route.getFragment().isEmpty() && !bestEntry->isFragmentAllowed)
    {
        result.refusedParameter = QStringLiteral("#");
        return result;
    }

    if (!bestEntry->redirect.isEmpty() && (!bestEntry->isRedirectOnlyWithoutQuery || route.getQueryItems().isEmpty()))
    {
        result.result = PDFAppRouteMatch::Result::Redirect;
        result.redirectedRoute = PDFAppRoute::create(bestEntry->redirect, route.getQueryItems(), route.getFragment());
        return result;
    }

    result.result = bestEntry->factory ? PDFAppRouteMatch::Result::Found : PDFAppRouteMatch::Result::NotImplemented;
    return result;
}

QString PDFAppRouteTable::describe(const PDFAppRoute& route) const
{
    const PDFAppRouteMatch routeMatch = match(route);
    const PDFAppRouteEntry* entry = routeMatch.entry;

    if (!route.isValid())
    {
        return QCoreApplication::translate("pdf::PDFAppRouteTable", "An invalid link");
    }

    if (!entry)
    {
        const QString last = route.getSegments().isEmpty() ? QString() : route.getSegments().back();
        return QCoreApplication::translate("pdf::PDFAppRouteTable", "Page “%1”").arg(last.left(MAXIMUM_SEARCH_TEXT_LENGTH));
    }

    switch (entry->subject)
    {
        case PDFAppRouteSubject::Page:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Page “%1”").arg(entry->getTitle());
        case PDFAppRouteSubject::Tool:
            // A tool of the catalog is named by its title (940:134845), an unknown id by itself
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Tool “%1”").arg(getToolTitle(route, *entry));
        case PDFAppRouteSubject::ToolCollection:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Tool collection “%1”").arg(getCollectionTitle(route, *entry));
        case PDFAppRouteSubject::SettingsSection:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Settings section “%1”").arg(getRouteParameter(route, *entry, QStringLiteral("section")));
        case PDFAppRouteSubject::HelpTopic:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Help topic “%1”").arg(getRouteParameter(route, *entry, QStringLiteral("topic")));
        case PDFAppRouteSubject::Version:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Release notes of version %1").arg(getRouteParameter(route, *entry, QStringLiteral("version")));
        case PDFAppRouteSubject::Document:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "A document, which is not open");
        case PDFAppRouteSubject::Search:
            return QCoreApplication::translate("pdf::PDFAppRouteTable", "Search results");
    }

    return entry->getTitle();
}

void PDFAppRouteTable::addDefaultEntry(PDFAppRouteEntry entry)
{
    const QString pattern = entry.pattern;
    if (!addEntry(std::move(entry)))
    {
        qWarning("Invalid route pattern %s", qPrintable(pattern));
    }
}

QString PDFAppRouteTable::getRouteParameter(const PDFAppRoute& route, const PDFAppRouteEntry& entry, const QString& name)
{
    const QString patternSegment = QLatin1Char('{') + name + QLatin1Char('}');
    for (qsizetype i = 0; i < entry.segments.size() && i < route.getSegments().size(); ++i)
    {
        if (entry.segments[i] == patternSegment)
        {
            return route.getSegments()[i].left(MAXIMUM_SEARCH_TEXT_LENGTH);
        }
    }
    return QString();
}

QString PDFAppRouteTable::getToolTitle(const PDFAppRoute& route, const PDFAppRouteEntry& entry)
{
    const QString toolId = getRouteParameter(route, entry, QStringLiteral("toolId"));
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    return tool ? tool->getTitle().left(MAXIMUM_SEARCH_TEXT_LENGTH) : toolId;
}

QString PDFAppRouteTable::getCollectionTitle(const PDFAppRoute& route, const PDFAppRouteEntry& entry)
{
    const QString collectionId = getRouteParameter(route, entry, QStringLiteral("collectionId"));
    const PDFAppBuiltinToolCollection* collection = PDFAppBuiltinToolCollections::findCollection(collectionId);
    return collection ? collection->getName().left(MAXIMUM_SEARCH_TEXT_LENGTH) : collectionId;
}

QString PDFAppRouteTable::toSearchWords(const QString& text)
{
    // Identifiers are searched as words: "convert.epub" -> "convert epub"
    QString result;
    for (const QChar character : text)
    {
        if (character == QLatin1Char('.') || character == QLatin1Char('-') || character == QLatin1Char('_') || !character.isPrint())
        {
            result += QLatin1Char(' ');
        }
        else
        {
            result += character;
        }
    }
    return result.simplified().left(MAXIMUM_SEARCH_TEXT_LENGTH).trimmed();
}

QString PDFAppRouteTable::getSearchText(const PDFAppRoute& route) const
{
    if (!route.isValid() || route.getSegments().isEmpty())
    {
        return QString();
    }

    const PDFAppRouteMatch routeMatch = match(route);
    const PDFAppRouteEntry* entry = routeMatch.entry;
    if (!entry)
    {
        return toSearchWords(route.getSegments().back());
    }

    switch (entry->subject)
    {
        case PDFAppRouteSubject::Page:
            return entry->getTitle();
        case PDFAppRouteSubject::Search:
            return route.getQueryValue(QStringLiteral("q")).simplified().left(MAXIMUM_SEARCH_TEXT_LENGTH).trimmed();
        case PDFAppRouteSubject::Document:
            return QString();
        case PDFAppRouteSubject::Tool:
            // Search for “Merge” (the title of a tool of the catalog)
            if (PDFDocumentToolCatalog::isValidToolId(getRouteParameter(route, *entry, QStringLiteral("toolId"))))
            {
                return getToolTitle(route, *entry);
            }
            return toSearchWords(route.getSegments().back());
        default:
            return toSearchWords(route.getSegments().back());
    }
}

PDFAppRouteEntry PDFAppRouteTable::createEntry(const QString& pattern, PDFAppTabKind tabKind, PDFAppNavItem navItem, PDFAppRouteSubject subject, const char* title)
{
    PDFAppRouteEntry entry;
    entry.pattern = pattern;
    entry.tabKind = tabKind;
    entry.navItem = navItem;
    entry.subject = subject;
    entry.title = title;
    return entry;
}

PDFAppRouteTable PDFAppRouteTable::createDefault()
{
    using Values = PDFAppRouteValues;
    using Tab = PDFAppTabKind;
    using Nav = PDFAppNavItem;
    using Subject = PDFAppRouteSubject;

    PDFAppRouteTable table;

    // The root is the Home page
    PDFAppRouteEntry root = createEntry(QStringLiteral("/"), Tab::Home, Nav::Home, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Home"));
    root.redirect = QStringList({ QStringLiteral("home") });
    table.addDefaultEntry(root);

    // Home tab (sidebar navigation)
    table.addDefaultEntry(createEntry(QStringLiteral("/home"), Tab::Home, Nav::Home, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Home")));

    PDFAppRouteEntry open = createEntry(QStringLiteral("/open"), Tab::Home, Nav::Open, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Open"));
    open.isNewTabRoute = true;
    table.addDefaultEntry(open);

    PDFAppRouteEntry recent = createEntry(QStringLiteral("/recent"), Tab::Home, Nav::Recent, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Recent"));
    recent.query[QStringLiteral("filter")] = Values::options({ QStringLiteral("files"), QStringLiteral("folders"), QStringLiteral("workspaces"), QStringLiteral("pinned") });
    recent.query[QStringLiteral("sort")] = Values::options({ QStringLiteral("last-opened"), QStringLiteral("name"), QStringLiteral("folder") });
    recent.query[QStringLiteral("q")] = Values::text();
    table.addDefaultEntry(recent);

    PDFAppRouteEntry tools = createEntry(QStringLiteral("/tools"), Tab::Home, Nav::Tools, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Tools"));
    tools.query[QStringLiteral("category")] = Values::options({ QStringLiteral("pages"), QStringLiteral("convert"), QStringLiteral("edit"), QStringLiteral("protect"),
                                                                QStringLiteral("optimize"), QStringLiteral("review"), QStringLiteral("pinned") });
    tools.query[QStringLiteral("q")] = Values::text();
    // The collection shown on the page (step 6, owner decision Q1): its route id
    tools.query[QStringLiteral("collection")] = Values::slug();
    table.addDefaultEntry(tools);

    // The list of the collections shows the first collection: the first built-in
    // collection, which cannot be deleted (plan of step 6, 4.4)
    PDFAppRouteEntry collections = createEntry(QStringLiteral("/tools/collections"), Tab::Home, Nav::Tools, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Tool collections"));
    if (!PDFAppBuiltinToolCollections::getCollections().empty())
    {
        collections.redirect = QStringList({ QStringLiteral("tools"), QStringLiteral("collections"), PDFAppBuiltinToolCollections::getCollections().front().getId() });
    }
    table.addDefaultEntry(collections);

    // A new collection, optionally with a tool added at once (New collection… of Add to collection)
    PDFAppRouteEntry collectionNew = createEntry(QStringLiteral("/tools/collections/new"), Tab::Home, Nav::Tools, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "New tool collection"));
    collectionNew.query[QStringLiteral("tool")] = Values::slug();
    table.addDefaultEntry(collectionNew);

    PDFAppRouteEntry collection = createEntry(QStringLiteral("/tools/collections/{collectionId}"), Tab::Home, Nav::Tools, Subject::ToolCollection, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Tool collection"));
    collection.parameters[QStringLiteral("collectionId")] = Values::slug();
    table.addDefaultEntry(collection);

    // A tool of the catalog has a page only, when its GUI is implemented (phase 2)
    PDFAppRouteEntry tool = createEntry(QStringLiteral("/tools/{toolId}"), Tab::Home, Nav::Tools, Subject::Tool, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Tool"));
    tool.parameters[QStringLiteral("toolId")] = Values::slug();
    table.addDefaultEntry(tool);

    // AI: the anchors of the rows of Connection / Permissions (/ai#read-documents, step 7)
    PDFAppRouteEntry ai = createEntry(QStringLiteral("/ai"), Tab::Home, Nav::Ai, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "AI"));
    ai.isFragmentAllowed = true;
    table.addDefaultEntry(ai);
    table.addDefaultEntry(createEntry(QStringLiteral("/whats-new"), Tab::Home, Nav::WhatsNew, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "What's New")));

    PDFAppRouteEntry whatsNewVersion = createEntry(QStringLiteral("/whats-new/{version}"), Tab::Home, Nav::WhatsNew, Subject::Version, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "What's New"));
    whatsNewVersion.parameters[QStringLiteral("version")] = Values::version();
    table.addDefaultEntry(whatsNewVersion);

    PDFAppRouteEntry whatsNewNotes = createEntry(QStringLiteral("/whats-new/{version}/notes"), Tab::Home, Nav::WhatsNew, Subject::Version, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Release notes"));
    whatsNewNotes.parameters[QStringLiteral("version")] = Values::version();
    table.addDefaultEntry(whatsNewNotes);

    PDFAppRouteEntry about = createEntry(QStringLiteral("/about"), Tab::Home, Nav::About, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "About"));
    about.isFragmentAllowed = true;
    table.addDefaultEntry(about);

    // Support: the filter of the video tutorials (topic of PDFSupportVideoCatalog or for-you, search text)
    PDFAppRouteEntry support = createEntry(QStringLiteral("/support"), Tab::Home, Nav::Support, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Support"));
    support.query[QStringLiteral("topic")] = Values::options({ QStringLiteral("for-you"), QStringLiteral("getting-started"), QStringLiteral("pages"), QStringLiteral("convert"),
                                                               QStringLiteral("forms"), QStringLiteral("security"), QStringLiteral("optimize"), QStringLiteral("review") });
    support.query[QStringLiteral("q")] = Values::text();
    table.addDefaultEntry(support);
    table.addDefaultEntry(createEntry(QStringLiteral("/sponsor"), Tab::Home, Nav::Sponsor, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Sponsor")));

    PDFAppRouteEntry help = createEntry(QStringLiteral("/help/{topic}"), Tab::Help, Nav::None, Subject::HelpTopic, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Help"));
    help.parameters[QStringLiteral("topic")] = Values::slug();
    help.isFragmentAllowed = true;
    table.addDefaultEntry(help);

    // Settings: /settings is the section General, /settings?q= filters the sections
    PDFAppRouteEntry settings = createEntry(QStringLiteral("/settings"), Tab::Home, Nav::Settings, Subject::Page, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Settings"));
    settings.query[QStringLiteral("q")] = Values::text();
    settings.redirect = QStringList({ QStringLiteral("settings"), QStringLiteral("general") });
    settings.isRedirectOnlyWithoutQuery = true;
    table.addDefaultEntry(settings);

    PDFAppRouteEntry settingsSection = createEntry(QStringLiteral("/settings/{section}"), Tab::Home, Nav::Settings, Subject::SettingsSection, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Settings"));
    settingsSection.parameters[QStringLiteral("section")] = Values::options({ QStringLiteral("general"), QStringLiteral("viewing"), QStringLiteral("color-modes"), QStringLiteral("editing"),
                                                                              QStringLiteral("security"), QStringLiteral("speech"), QStringLiteral("shortcuts"),
                                                                              QStringLiteral("color-management"), QStringLiteral("advanced"),
                                                                              // Storage below Advanced (step 5, owner decision Q1)
                                                                              QStringLiteral("storage") });
    settingsSection.isFragmentAllowed = true;
    table.addDefaultEntry(settingsSection);

    PDFAppRouteEntry search = createEntry(QStringLiteral("/search"), Tab::Home, Nav::None, Subject::Search, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Search"));
    search.query[QStringLiteral("q")] = Values::text();
    search.query[QStringLiteral("group")] = Values::options({ QStringLiteral("tools"), QStringLiteral("commands"), QStringLiteral("settings"), QStringLiteral("documents"), QStringLiteral("help") });
    table.addDefaultEntry(search);

    // Document tabs (step 3); the id is the id of an open document tab
    PDFAppRouteEntry document = createEntry(QStringLiteral("/document/{documentId}"), Tab::Document, Nav::None, Subject::Document, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Document"));
    document.parameters[QStringLiteral("documentId")] = Values::documentId();
    document.query[QStringLiteral("page")] = Values::number();
    document.query[QStringLiteral("panel")] = Values::slug();
    document.query[QStringLiteral("q")] = Values::text();
    table.addDefaultEntry(document);

    PDFAppRouteEntry properties = createEntry(QStringLiteral("/document/{documentId}/properties/{section}"), Tab::Document, Nav::None, Subject::Document, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Document properties"));
    properties.parameters[QStringLiteral("documentId")] = Values::documentId();
    properties.parameters[QStringLiteral("section")] = Values::options({ QStringLiteral("general"), QStringLiteral("security"), QStringLiteral("fonts"), QStringLiteral("display"), QStringLiteral("xmp") });
    table.addDefaultEntry(properties);

    PDFAppRouteEntry documentTool = createEntry(QStringLiteral("/document/{documentId}/tool/{toolId}"), Tab::Document, Nav::None, Subject::Tool, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Tool"));
    documentTool.parameters[QStringLiteral("documentId")] = Values::documentId();
    documentTool.parameters[QStringLiteral("toolId")] = Values::slug();
    table.addDefaultEntry(documentTool);

    // Tools with their own tab (phase 2)
    PDFAppRouteEntry toolTab = createEntry(QStringLiteral("/tool/{toolId}/{instanceId}"), Tab::Tool, Nav::None, Subject::Tool, QT_TRANSLATE_NOOP("pdf::PDFAppRouteTable", "Tool"));
    toolTab.parameters[QStringLiteral("toolId")] = Values::slug();
    toolTab.parameters[QStringLiteral("instanceId")] = Values::slug();
    table.addDefaultEntry(toolTab);

    return table;
}

}   // namespace pdf
