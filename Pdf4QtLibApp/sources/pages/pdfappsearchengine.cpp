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

#include "pdfappsearchengine.h"
#include "pdfapplocale.h"
#include "pdfappcontext.h"
#include "pdfappsettingssection.h"
#include "pdfsupportvideocatalog.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsearch.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Helpers of the global search
class PDFAppSearchEngineHelper
{
public:
    PDFAppSearchEngineHelper() = delete;

    static constexpr const char* CONTEXT = "pdf::PDFAppSearchEngine";

    // Scores of the fields of the items of the groups other than Tools (the
    // tools use the same scale, PDFDocumentToolSearch)
    static constexpr int TITLE_EXACT = 100;
    static constexpr int TITLE_PREFIX = 90;
    static constexpr int TITLE_TYPO = 60;
    static constexpr int KEYWORD_EXACT = 50;
    static constexpr int KEYWORD_PREFIX = 45;
    static constexpr int KEYWORD_TYPO = 30;
    static constexpr int DESCRIPTION_EXACT = 20;
    static constexpr int DESCRIPTION_PREFIX = 18;
    static constexpr int TEXT_EXACT = 10;
    static constexpr int TEXT_PREFIX = 8;

    /// A heading is ranked just below a topic of the same quality
    static constexpr int HEADING_PENALTY = 5;

    /// Most headings of one topic among the results
    static constexpr int MAXIMUM_HEADINGS_PER_TOPIC = 2;

    static QString translate(const char* text)
    {
        return QCoreApplication::translate(CONTEXT, text);
    }

    /// The texts of the user interface and the English ones (once, when equal)
    static QStringList withSource(const QString& translated, const QString& source)
    {
        QStringList texts;
        if (!translated.isEmpty())
        {
            texts << translated;
        }
        if (!source.isEmpty() && source != translated)
        {
            texts << source;
        }
        return texts;
    }

    static PDFSearchField title(const QStringList& texts)
    {
        return PDFSearchField(texts, TITLE_EXACT, TITLE_PREFIX, TITLE_TYPO, true);
    }

    static PDFSearchField keywords(const QStringList& texts)
    {
        return PDFSearchField(texts, KEYWORD_EXACT, KEYWORD_PREFIX, KEYWORD_TYPO);
    }

    static PDFSearchField description(const QStringList& texts)
    {
        return PDFSearchField(texts, DESCRIPTION_EXACT, DESCRIPTION_PREFIX, 0);
    }

    static PDFSearchField text(const QStringList& texts)
    {
        return PDFSearchField(texts, TEXT_EXACT, TEXT_PREFIX, 0);
    }

    /// Order of the results of a group: the score, then the order of the source
    static bool isBetter(const PDFAppSearchResult& left, const PDFAppSearchResult& right)
    {
        if (left.score != right.score)
        {
            return left.score > right.score;
        }
        return left.order < right.order;
    }

    static bool isHeadingBetter(const std::pair<int, size_t>& left, const std::pair<int, size_t>& right)
    {
        return left.first > right.first;
    }

    static bool isInvisible(QChar::Category category)
    {
        return category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Separator_Line || category == QChar::Separator_Paragraph;
    }

    static QString toPercentEncoding(QStringView text)
    {
        QString result;
        for (const char byte : text.toUtf8())
        {
            result += QStringLiteral("%%1").arg(int(static_cast<unsigned char>(byte)), 2, 16, QLatin1Char('0')).toUpper();
        }
        return result;
    }

    /// Path of the folder of a file for the user
    static QString getFolderText(const PDFRecentFile& file)
    {
        QString folder = file.folder;
        if (folder.isEmpty())
        {
            folder = QFileInfo(file.path).path();
        }
        return QDir::toNativeSeparators(folder);
    }
};

const std::array<PDFAppSearchGroup, PDFAppSearchGroups::COUNT>& PDFAppSearchGroups::getGroups()
{
    static const std::array<PDFAppSearchGroup, COUNT> groups = { PDFAppSearchGroup::Tools, PDFAppSearchGroup::Commands, PDFAppSearchGroup::Settings,
                                                                 PDFAppSearchGroup::Documents, PDFAppSearchGroup::Help };
    return groups;
}

QString PDFAppSearchGroups::getId(PDFAppSearchGroup group)
{
    switch (group)
    {
        case PDFAppSearchGroup::Tools:
            return QStringLiteral("tools");
        case PDFAppSearchGroup::Commands:
            return QStringLiteral("commands");
        case PDFAppSearchGroup::Settings:
            return QStringLiteral("settings");
        case PDFAppSearchGroup::Documents:
            return QStringLiteral("documents");
        case PDFAppSearchGroup::Help:
            return QStringLiteral("help");
    }
    return QString();
}

std::optional<PDFAppSearchGroup> PDFAppSearchGroups::findGroup(const QString& id)
{
    for (const PDFAppSearchGroup group : getGroups())
    {
        if (getId(group) == id)
        {
            return group;
        }
    }
    return std::nullopt;
}

QString PDFAppSearchGroups::getTitle(PDFAppSearchGroup group)
{
    switch (group)
    {
        case PDFAppSearchGroup::Tools:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Tools"));
        case PDFAppSearchGroup::Commands:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Commands"));
        case PDFAppSearchGroup::Settings:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Settings"));
        case PDFAppSearchGroup::Documents:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Documents"));
        case PDFAppSearchGroup::Help:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Help"));
    }
    return QString();
}

QString PDFAppSearchGroups::getName(PDFAppSearchGroup group)
{
    switch (group)
    {
        case PDFAppSearchGroup::Tools:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "tools"));
        case PDFAppSearchGroup::Commands:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "commands"));
        case PDFAppSearchGroup::Settings:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "settings"));
        case PDFAppSearchGroup::Documents:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "documents"));
        case PDFAppSearchGroup::Help:
            return PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "help"));
    }
    return QString();
}

PDFAppSearchResults::PDFAppSearchResults()
{
    for (const PDFAppSearchGroup group : PDFAppSearchGroups::getGroups())
    {
        PDFAppSearchGroupResults groupResults;
        groupResults.group = group;
        groups.push_back(std::move(groupResults));
    }
}

int PDFAppSearchResults::getTotalCount() const
{
    int count = 0;
    for (const PDFAppSearchGroupResults& group : groups)
    {
        count += group.totalCount;
    }
    return count;
}

int PDFAppSearchResults::getCount(PDFAppSearchGroup group) const
{
    const PDFAppSearchGroupResults* groupResults = getGroup(group);
    return groupResults ? groupResults->totalCount : 0;
}

const PDFAppSearchGroupResults* PDFAppSearchResults::getGroup(PDFAppSearchGroup group) const
{
    for (const PDFAppSearchGroupResults& groupResults : groups)
    {
        if (groupResults.group == group)
        {
            return &groupResults;
        }
    }
    return nullptr;
}

const PDFAppSearchResult* PDFAppSearchResults::findResult(PDFAppSearchGroup group, const QString& key) const
{
    if (const PDFAppSearchGroupResults* groupResults = getGroup(group))
    {
        for (const PDFAppSearchResult& result : groupResults->results)
        {
            if (result.key == key)
            {
                return &result;
            }
        }
    }
    return nullptr;
}

PDFAppSearchData PDFAppSearchData::load(const PDFAppContext* context)
{
    PDFAppSearchData data;
    data.helpCatalog = getDefaultHelpCatalog();
    if (!context)
    {
        return data;
    }

    data.commandRegistry = context->getCommandRegistry();
    data.settingRegistry = context->getSettingRegistry();
    if (context->getDatabase())
    {
        PDFDatabaseError error;
        data.userShortcuts = context->getShortcuts().getShortcuts(&error);
        if (error.isError())
        {
            qWarning("The global search cannot read the shortcuts: %s", qPrintable(error.toString()));
        }
        data.recentFiles = context->getRecent().getFiles();
        data.prepareDocuments();
    }
    return data;
}

void PDFAppSearchData::prepareDocuments()
{
    documentFields.clear();
    documentFields.reserve(recentFiles.size());
    for (const PDFRecentFile& file : recentFiles)
    {
        documentFields.push_back(createDocumentFields(file));
    }
}

std::vector<PDFSearchField> PDFAppSearchData::createDocumentFields(const PDFRecentFile& file)
{
    // The name of the file is the title, the folder the keywords of the item
    const QString name = file.name.isEmpty() ? QFileInfo(file.path).fileName() : file.name;
    std::vector<PDFSearchField> fields = { PDFAppSearchEngineHelper::title({ name }),
                                           PDFSearchField({ PDFAppSearchEngineHelper::getFolderText(file) }, PDFAppSearchEngineHelper::DESCRIPTION_EXACT, PDFAppSearchEngineHelper::DESCRIPTION_PREFIX, 0) };
    for (PDFSearchField& field : fields)
    {
        field.prepare();
    }
    return fields;
}

std::shared_ptr<const PDFAppHelpCatalog> PDFAppSearchData::getDefaultHelpCatalog()
{
    // Parsed once per process, shared with the help pages
    return PDFAppHelpCatalog::getBuiltin();
}

PDFAppSearchProvider::~PDFAppSearchProvider() = default;

int PDFAppSearchProvider::searchBest(const PDFSearchQuery& query, const PDFAppSearchData& data, int maximumResults, std::vector<PDFAppSearchResult>& results) const
{
    Q_UNUSED(maximumResults);

    const size_t count = results.size();
    search(query, data, results);
    return int(results.size() - count);
}

void PDFAppToolSearchProvider::search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const
{
    Q_UNUSED(data);

    int order = 0;
    for (const PDFDocumentToolSearchMatch& match : PDFDocumentToolSearch::search(query))
    {
        const PDFDocumentToolCatalogEntry* tool = match.tool;
        if (!tool)
        {
            continue;
        }

        const PDFDocumentToolCategoryInfo* category = PDFDocumentToolCatalog::getCategoryInfo(tool->getCategory());
        const QString categoryTitle = category ? category->getTitle() : QString();

        PDFAppSearchResult result;
        result.group = PDFAppSearchGroup::Tools;
        result.key = tool->getId();
        result.title = tool->getTitle();

        // The reason of a match by a keyword ("matches “signature”", Figma 285:3026),
        // otherwise the category of the tool
        const QString reason = match.field == PDFDocumentToolSearchMatch::Field::Keyword && !match.keyword.isEmpty()
                                   ? PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "matches “%1”")).arg(match.keyword)
                                   : categoryTitle;
        result.subtitle = reason.isEmpty() ? tool->getDescription() : PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "%1 · %2")).arg(tool->getDescription(), reason);
        result.meta = categoryTitle;
        result.iconName = tool->getIconName();
        result.hue = PDFAppSearchEngine::getToolHue(int(tool->getHue()));
        result.action = PDFAppSearchResult::Action::Navigate;
        result.route = PDFAppRoutes::tool(tool->getId());
        result.score = match.score;
        result.order = order++;
        results.push_back(std::move(result));
    }
}

void PDFAppCommandSearchProvider::search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const
{
    if (!data.commandRegistry)
    {
        return;
    }

    int order = 0;
    for (const PDFAppCommand* command : data.commandRegistry->getCommands())
    {
        if (!command)
        {
            continue;
        }

        const QString title = command->getTitle();
        const QString groupTitle = command->getGroupTitle();
        QStringList keywords = command->keywords;
        keywords << PDFAppSearchEngineHelper::withSource(groupTitle, QString::fromUtf8(command->group));

        const std::vector<PDFSearchField> fields = { PDFAppSearchEngineHelper::title(PDFAppSearchEngineHelper::withSource(title, QString::fromUtf8(command->title))),
                                                     PDFAppSearchEngineHelper::keywords(keywords) };
        const std::optional<PDFSearchFieldMatch> match = query.match(fields);
        if (!match)
        {
            ++order;
            continue;
        }

        PDFAppSearchResult result;
        result.group = PDFAppSearchGroup::Commands;
        result.key = command->id;
        result.title = title;
        result.subtitle = groupTitle;
        result.meta = data.commandRegistry->getShortcut(command->id, data.userShortcuts).toString(QKeySequence::NativeText);
        result.iconName = command->iconName.isEmpty() ? QStringLiteral("zap") : command->iconName;
        result.action = PDFAppSearchResult::Action::RunCommand;
        result.commandId = command->id;
        result.score = match->score;
        result.order = order++;
        results.push_back(std::move(result));
    }
}

void PDFAppSettingSearchProvider::search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const
{
    if (!data.settingRegistry)
    {
        return;
    }

    int order = 0;
    for (const PDFSettingDefinition* definition : data.settingRegistry->getDefinitions())
    {
        // Internal state (no section) is not searched
        if (!definition || !definition->isVisible())
        {
            continue;
        }

        const QString title = definition->getTitle();
        const QString sectionTitle = PDFAppSearchEngine::getSettingsSectionTitle(definition->getSection());
        QStringList keywords = definition->getKeywords();
        keywords << PDFAppSearchEngineHelper::withSource(definition->getGroupTitle(), definition->getGroup());
        keywords << sectionTitle;

        const std::vector<PDFSearchField> fields = {
            PDFAppSearchEngineHelper::title(PDFAppSearchEngineHelper::withSource(title, QString::fromUtf8(definition->getTitleSource()))),
            PDFAppSearchEngineHelper::keywords(keywords),
            PDFAppSearchEngineHelper::description(PDFAppSearchEngineHelper::withSource(definition->getDescription(), QString::fromUtf8(definition->getDescriptionSource())))
        };
        const std::optional<PDFSearchFieldMatch> match = query.match(fields);
        if (!match)
        {
            ++order;
            continue;
        }

        // "Settings › Security", with the description "Settings › Security · treat signature warnings as errors"
        PDFAppSearchResult result;
        result.group = PDFAppSearchGroup::Settings;
        result.key = definition->getKey();
        result.title = title.isEmpty() ? definition->getKey() : title;
        result.subtitle = PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Settings › %1")).arg(sectionTitle);
        const QString description = definition->getDescription();
        if (!description.isEmpty())
        {
            result.subtitle = PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "%1 · %2")).arg(result.subtitle, description);
        }
        result.iconName = QStringLiteral("settings");
        result.action = PDFAppSearchResult::Action::Navigate;
        result.route = PDFAppRoutes::settings(definition->getSection(), definition->getAnchor());
        result.score = match->score;
        result.order = order++;
        results.push_back(std::move(result));
    }

    // The rows of Settings › Storage are not settings (step 5, package D): the
    // database file, the export and the import of the settings
    struct StorageRow
    {
        const char* anchor;
        const char* title;
        const char* keywords;
    };
    static constexpr StorageRow storageRows[] =
    {
        { "location", QT_TRANSLATE_NOOP("pdf::PDFAppStorageSettingsSection", "Location"), "database file folder path storage" },
        { "state", QT_TRANSLATE_NOOP("pdf::PDFAppStorageSettingsSection", "State"), "portable memory read only database storage" },
        { "export", QT_TRANSLATE_NOOP("pdf::PDFAppStorageSettingsSection", "Export settings"), "backup export save copy storage" },
        { "import", QT_TRANSLATE_NOOP("pdf::PDFAppStorageSettingsSection", "Import settings"), "backup import restore storage" }
    };

    const QString storageTitle = PDFAppSearchEngine::getSettingsSectionTitle(QStringLiteral("storage"));
    for (const StorageRow& row : storageRows)
    {
        const QString title = QCoreApplication::translate("pdf::PDFAppStorageSettingsSection", row.title);
        QStringList keywords = QString::fromLatin1(row.keywords).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        keywords << storageTitle;
        const std::vector<PDFSearchField> fields = {
            PDFAppSearchEngineHelper::title(PDFAppSearchEngineHelper::withSource(title, QString::fromUtf8(row.title))),
            PDFAppSearchEngineHelper::keywords(keywords)
        };
        const std::optional<PDFSearchFieldMatch> match = query.match(fields);
        if (!match)
        {
            ++order;
            continue;
        }

        PDFAppSearchResult result;
        result.group = PDFAppSearchGroup::Settings;
        result.key = QStringLiteral("storage.") + QString::fromLatin1(row.anchor);
        result.title = title;
        result.subtitle = PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Settings › %1")).arg(storageTitle);
        result.iconName = QStringLiteral("settings");
        result.action = PDFAppSearchResult::Action::Navigate;
        result.route = PDFAppRoutes::settings(QStringLiteral("storage"), QString::fromLatin1(row.anchor));
        result.score = match->score;
        result.order = order++;
        results.push_back(std::move(result));
    }
}

void PDFAppDocumentSearchProvider::search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const
{
    searchBest(query, data, std::numeric_limits<int>::max(), results);
}

int PDFAppDocumentSearchProvider::searchBest(const PDFSearchQuery& query, const PDFAppSearchData& data, int maximumResults, std::vector<PDFAppSearchResult>& results) const
{
    // The matches first (score, index), the results of the best ones only; the
    // prepared fields of the data (normalized once per loading of the data) are
    // used, data filled by hand are prepared here
    const bool isPrepared = data.documentFields.size() == data.recentFiles.size();
    std::vector<std::pair<int, size_t>> matches;
    for (size_t i = 0; i < data.recentFiles.size(); ++i)
    {
        const PDFRecentFile& file = data.recentFiles[i];
        if (file.path.isEmpty())
        {
            continue;
        }

        const std::vector<PDFSearchField> createdFields = isPrepared ? std::vector<PDFSearchField>() : PDFAppSearchData::createDocumentFields(file);
        const std::vector<PDFSearchField>& fields = isPrepared ? data.documentFields[i] : createdFields;
        if (const std::optional<PDFSearchFieldMatch> match = query.match(fields))
        {
            matches.emplace_back(match->score, i);
        }
    }

    // The order of Recent (the most recently opened first) decides a tie
    std::stable_sort(matches.begin(), matches.end(), &PDFAppSearchEngineHelper::isHeadingBetter);
    const int totalCount = int(matches.size());
    if (maximumResults >= 0 && matches.size() > size_t(maximumResults))
    {
        matches.resize(size_t(maximumResults));
    }

    const QDateTime now = data.now.isValid() ? data.now : QDateTime::currentDateTime();
    for (const std::pair<int, size_t>& match : matches)
    {
        const PDFRecentFile& file = data.recentFiles[match.second];
        const QString folder = PDFAppSearchEngineHelper::getFolderText(file);

        PDFAppSearchResult result;
        result.group = PDFAppSearchGroup::Documents;
        result.key = file.path;
        result.title = file.name.isEmpty() ? QFileInfo(file.path).fileName() : file.name;
        const QString opened = PDFAppSearchEngine::getOpenedText(file.lastOpened, now);
        result.subtitle = opened.isEmpty() ? folder : PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "%1 · %2")).arg(folder, opened);
        result.meta = file.pinned ? PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Pinned"))
                                  : PDFAppSearchEngineHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Recent"));
        result.iconName = QStringLiteral("file-text");
        result.isFileName = true;
        result.isDataSubtitle = true;
        result.action = PDFAppSearchResult::Action::OpenDocument;
        result.filePath = file.path;
        result.score = match.first;
        result.order = int(match.second);
        results.push_back(std::move(result));
    }
    return totalCount;
}

void PDFAppHelpSearchProvider::search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const
{
    using Helper = PDFAppSearchEngineHelper;

    int order = 0;
    if (data.helpCatalog)
    {
        for (const PDFAppHelpTopic& topic : data.helpCatalog->getTopics())
        {
            const PDFAppHelpCategory* category = PDFAppHelpCatalog::findCategory(topic.categoryId);
            const QString categoryTitle = category ? category->getTitle() : QString();

            const std::vector<PDFSearchField> fields = { Helper::title({ topic.title }), Helper::keywords(topic.keywords),
                                                         Helper::description({ topic.summary }), Helper::text({ topic.markdown }) };
            if (const std::optional<PDFSearchFieldMatch> match = query.match(fields))
            {
                // "User manual › Getting started"
                PDFAppSearchResult result;
                result.group = PDFAppSearchGroup::Help;
                result.key = PDFAppHelpCatalog::getRoute(topic.id);
                result.title = topic.title;
                result.subtitle = categoryTitle.isEmpty() ? Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "User manual"))
                                                          : Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "User manual › %1")).arg(categoryTitle);
                result.meta = Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Manual"));
                result.iconName = QStringLiteral("book-open");
                result.route = PDFAppRoute::parse(result.key);
                result.score = match->score;
                result.order = order++;
                if (result.route.isValid())
                {
                    results.push_back(std::move(result));
                }
            }

            // The best headings of the topic lead to their anchors
            std::vector<std::pair<int, size_t>> headings;
            for (size_t i = 0; i < topic.headings.size(); ++i)
            {
                const PDFAppMarkdownHeading& heading = topic.headings[i];
                if (heading.anchor.isEmpty() || heading.text.isEmpty() || heading.text == topic.title)
                {
                    continue;
                }

                if (const std::optional<PDFSearchFieldMatch> match = query.match({ Helper::title({ heading.text }) }))
                {
                    headings.emplace_back(match->score - Helper::HEADING_PENALTY, i);
                }
            }
            std::stable_sort(headings.begin(), headings.end(), &Helper::isHeadingBetter);
            if (headings.size() > size_t(Helper::MAXIMUM_HEADINGS_PER_TOPIC))
            {
                headings.resize(size_t(Helper::MAXIMUM_HEADINGS_PER_TOPIC));
            }

            for (const std::pair<int, size_t>& item : headings)
            {
                const PDFAppMarkdownHeading& heading = topic.headings[item.second];

                // "User manual › Getting started › Search everything"
                PDFAppSearchResult result;
                result.group = PDFAppSearchGroup::Help;
                result.key = PDFAppHelpCatalog::getRoute(topic.id, heading.anchor);
                result.title = heading.text;
                result.subtitle = categoryTitle.isEmpty() ? Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "User manual › %1")).arg(topic.title)
                                                          : Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "User manual › %1 › %2")).arg(categoryTitle, topic.title);
                result.meta = Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Manual"));
                result.iconName = QStringLiteral("book-open");
                result.route = PDFAppRoute::parse(result.key);
                result.score = item.first;
                result.order = order++;
                if (result.route.isValid())
                {
                    results.push_back(std::move(result));
                }
            }
        }
    }

    if (!data.includeVideos)
    {
        return;
    }

    // Video tutorials (YouTube, opened by the system)
    for (const PDFSupportVideo& video : PDFSupportVideoCatalog::getVideos())
    {
        const QUrl link = video.getLink();
        if (!PDFSupportVideoCatalog::isAllowedLink(link))
        {
            continue;
        }

        QStringList tags = video.getTags();
        for (const QString& tag : video.getSourceTags())
        {
            if (!tags.contains(tag))
            {
                tags << tag;
            }
        }

        const std::vector<PDFSearchField> fields = { Helper::title(Helper::withSource(video.getTitle(), video.getSourceTitle())), Helper::keywords(tags),
                                                     Helper::description(Helper::withSource(video.getDescription(), video.getSourceDescription())) };
        const std::optional<PDFSearchFieldMatch> match = query.match(fields);
        if (!match)
        {
            continue;
        }

        PDFAppSearchResult result;
        result.group = PDFAppSearchGroup::Help;
        result.key = QStringLiteral("video:%1").arg(video.getId());
        result.title = video.getTitle();
        result.subtitle = Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Video tutorial · %1")).arg(video.getDuration());
        result.meta = Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "YouTube"));
        result.iconName = QStringLiteral("play-circle");
        result.isExternal = true;
        result.action = PDFAppSearchResult::Action::OpenUrl;
        result.url = link;
        result.score = match->score;
        result.order = order++;
        results.push_back(std::move(result));
    }
}

PDFAppSearchEngine::PDFAppSearchEngine()
{
    m_providers.push_back(std::make_unique<PDFAppToolSearchProvider>());
    m_providers.push_back(std::make_unique<PDFAppCommandSearchProvider>());
    m_providers.push_back(std::make_unique<PDFAppSettingSearchProvider>());
    m_providers.push_back(std::make_unique<PDFAppDocumentSearchProvider>());
    m_providers.push_back(std::make_unique<PDFAppHelpSearchProvider>());
}

PDFAppSearchEngine::~PDFAppSearchEngine() = default;

PDFAppSearchResults PDFAppSearchEngine::search(const QString& text, const PDFAppSearchData& data) const
{
    const PDFSearchQuery query(text);

    PDFAppSearchResults results;
    results.text = query.getText();
    results.isEmptyQuery = query.isEmpty();
    if (query.isEmpty())
    {
        return results;
    }

    for (PDFAppSearchGroupResults& groupResults : results.groups)
    {
        std::vector<PDFAppSearchResult> found;
        int totalCount = 0;
        for (const std::unique_ptr<PDFAppSearchProvider>& provider : m_providers)
        {
            if (provider && provider->getGroup() == groupResults.group)
            {
                totalCount += provider->searchBest(query, data, MAXIMUM_GROUP_RESULTS, found);
            }
        }

        std::stable_sort(found.begin(), found.end(), &PDFAppSearchEngineHelper::isBetter);
        groupResults.totalCount = totalCount;
        if (found.size() > size_t(MAXIMUM_GROUP_RESULTS))
        {
            found.resize(size_t(MAXIMUM_GROUP_RESULTS));
        }
        groupResults.results = std::move(found);
    }
    return results;
}

QString PDFAppSearchEngine::getSettingsSectionTitle(const QString& section)
{
    // Sections of Settings: the registry of the page (step 5), also Storage
    if (const PDFAppSettingsSectionInfo* info = PDFAppSettingsSectionRegistry::findSection(section))
    {
        return info->getTitle();
    }
    return section;
}

QString PDFAppSearchEngine::getOpenedText(qint64 lastOpened, const QDateTime& now)
{
    using Helper = PDFAppSearchEngineHelper;

    if (lastOpened <= 0 || !now.isValid())
    {
        return QString();
    }

    const QDate date = QDateTime::fromMSecsSinceEpoch(lastOpened, now.timeZone()).date();
    const QDate today = now.date();
    if (!date.isValid())
    {
        return QString();
    }

    if (date == today)
    {
        return Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Opened today"));
    }
    if (date == today.addDays(-1))
    {
        return Helper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchEngine", "Opened yesterday"));
    }

    // O17: the language of the user interface, not the locale of the system
    if (date.year() == today.year())
    {
        return PDFAppLocale::formatShortDate(date);
    }
    return PDFAppLocale::formatDate(date);
}

QString PDFAppSearchEngine::getVisibleText(const QString& text, qsizetype maximumLength)
{
    using Helper = PDFAppSearchEngineHelper;

    if (maximumLength <= 0)
    {
        return QString();
    }

    QString result;
    qsizetype characters = 0;
    bool isCut = false;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (characters >= maximumLength)
        {
            isCut = true;
            break;
        }

        const QChar character = text[i];
        if (character.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
        {
            const char32_t codePoint = QChar::surrogateToUcs4(character, text[i + 1]);
            const QStringView pair = QStringView(text).mid(i, 2);
            result += Helper::isInvisible(QChar::category(codePoint)) ? Helper::toPercentEncoding(pair) : pair.toString();
            ++i;
        }
        else if (character.isSurrogate())
        {
            result += QChar(QChar::ReplacementCharacter);
        }
        else
        {
            result += Helper::isInvisible(character.category()) ? Helper::toPercentEncoding(QStringView(text).mid(i, 1)) : QString(character);
        }
        ++characters;
    }

    if (isCut)
    {
        result += QChar(0x2026);
    }
    return result;
}

PDFAccentHue PDFAppSearchEngine::getToolHue(int documentToolHue)
{
    switch (static_cast<PDFDocumentToolHue>(documentToolHue))
    {
        case PDFDocumentToolHue::Amber:
            return PDFAccentHue::Amber;
        case PDFDocumentToolHue::Blue:
            return PDFAccentHue::Blue;
        case PDFDocumentToolHue::Green:
            return PDFAccentHue::Green;
        case PDFDocumentToolHue::Red:
            return PDFAccentHue::Red;
        case PDFDocumentToolHue::Violet:
            return PDFAccentHue::Violet;
        case PDFDocumentToolHue::Teal:
            return PDFAccentHue::Teal;
    }
    return PDFAccentHue::Neutral;
}

}   // namespace pdf
