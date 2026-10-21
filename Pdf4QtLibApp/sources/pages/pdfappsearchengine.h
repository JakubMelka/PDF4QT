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

#ifndef PDFAPPSEARCHENGINE_H
#define PDFAPPSEARCHENGINE_H

#include "pdfappglobal.h"
#include "pdfappcommand.h"
#include "pdfapphelpcatalog.h"
#include "pdfapproute.h"
#include "pdfrecentrepository.h"
#include "pdfsettingdefinition.h"

#include "pdfaccenthue.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFSearchQuery;

/// Group of the global search (Figma 285:3026, 941:7081), in the order of the results
enum class PDFAppSearchGroup
{
    Tools,      ///< Tools of the static catalog (PDFDocumentToolSearch)
    Commands,   ///< Commands of PDFAppCommandRegistry with the shortcuts of the user
    Settings,   ///< Settings of PDFSettingRegistry shown in Settings
    Documents,  ///< Recent and pinned files of PDFRecentRepository
    Help        ///< Topics and headings of the user manual, video tutorials
};

/// Identifiers and texts of the groups (the value of the route /search?group=)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchGroups
{
public:
    PDFAppSearchGroups() = delete;

    static constexpr size_t COUNT = 5;

    /// Groups in their order
    static const std::array<PDFAppSearchGroup, COUNT>& getGroups();

    /// Identifier of the group ("tools"), empty for an invalid value
    static QString getId(PDFAppSearchGroup group);

    /// Group of the identifier (std::nullopt for an unknown one, also for "all")
    static std::optional<PDFAppSearchGroup> findGroup(const QString& id);

    /// Translated title ("Tools": the chip of the filter, the header of the group in capitals)
    static QString getTitle(PDFAppSearchGroup group);

    /// Translated name inside of a sentence ("tools": "… results for “sign” in tools.")
    static QString getName(PDFAppSearchGroup group);
};

/// One result of the global search: the texts of its row and what its activation does
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchResult
{
public:
    /// What the activation of the result does
    enum class Action
    {
        Navigate,       ///< Navigates to \p route (tool, setting, manual)
        OpenUrl,        ///< Opens \p url by the system (video tutorial, https only)
        RunCommand,     ///< Runs the command \p commandId of the window
        OpenDocument    ///< Opens the file \p filePath
    };

    PDFAppSearchGroup group = PDFAppSearchGroup::Tools;

    /// Identity of the result in its group (tool id, command id, setting key,
    /// path, help route, video id): the current result is kept by it, when the
    /// results change under it
    QString key;

    /// Texts of the row: the title Semi Bold 13, the subtitle Regular 11 and the
    /// meta text at the trailing side ("Protect & Sign", "Ctrl+Shift+S", "Recent")
    QString title;
    QString subtitle;
    QString meta;

    /// Badge of the row (Tool icon 28): the icon and the hue of the category of a
    /// tool, Neutral for the other groups
    QString iconName;
    PDFAccentHue hue = PDFAccentHue::Neutral;

    /// The title is the name of a file (elided in the middle, the extension
    /// stays); the subtitle is data (a path, the text of the user)
    bool isFileName = false;
    bool isDataSubtitle = false;

    /// The result opens outside of the application (the icon external-link)
    bool isExternal = false;

    Action action = Action::Navigate;
    PDFAppRoute route;
    QUrl url;
    QString commandId;
    QString filePath;

    /// Score of the match (higher is better)
    int score = 0;

    /// Order of the result in its source (a tie of the score keeps it)
    int order = 0;
};

/// Results of one group: the best ones (at most the limit of the search) and the
/// count of all matches
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchGroupResults
{
public:
    PDFAppSearchGroup group = PDFAppSearchGroup::Tools;
    std::vector<PDFAppSearchResult> results;
    int totalCount = 0;
};

/// Results of the global search, grouped in the order of PDFAppSearchGroups
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchResults
{
public:
    PDFAppSearchResults();

    /// Text of the search for the user (sanitized: without invisible characters, at most 256 characters)
    QString text;

    /// The text has no word to search (empty, spaces, punctuation)
    bool isEmptyQuery = true;

    /// All groups, also the empty ones, in their order
    std::vector<PDFAppSearchGroupResults> groups;

    /// Count of all matches of all groups
    int getTotalCount() const;

    /// Count of all matches of the group
    int getCount(PDFAppSearchGroup group) const;

    /// Results of the group (nullptr for an invalid value)
    const PDFAppSearchGroupResults* getGroup(PDFAppSearchGroup group) const;

    /// Result of the group with the key (nullptr, if there is none)
    const PDFAppSearchResult* findResult(PDFAppSearchGroup group, const QString& key) const;
};

/// What the search looks into. The application fills it from the context
/// (\p load: the registries, the shortcuts of the user and the files of Recent read
/// from the database now); the tests fill it by hand. Nothing is kept longer than
/// one showing of a page or of the popup (newapp_storage.md 1).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchData
{
public:
    const PDFAppCommandRegistry* commandRegistry = nullptr;
    PDFAppCommandRegistry::UserShortcuts userShortcuts;
    const PDFSettingRegistry* settingRegistry = nullptr;
    std::vector<PDFRecentFile> recentFiles;
    std::shared_ptr<const PDFAppHelpCatalog> helpCatalog;

    /// Video tutorials of PDFSupportVideoCatalog are searched (group Help)
    bool includeVideos = true;

    /// Current time for "Opened yesterday" (invalid = the clock of the system)
    QDateTime now;

    /// Data of the context (a null context gives the static catalogs only)
    static PDFAppSearchData load(const PDFAppContext* context);

    /// The user manual of the resources, loaded once (the resources do not change)
    static std::shared_ptr<const PDFAppHelpCatalog> getDefaultHelpCatalog();
};

/// Provider of the results of one group
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchProvider
{
public:
    PDFAppSearchProvider() = default;
    virtual ~PDFAppSearchProvider();

    PDFAppSearchProvider(const PDFAppSearchProvider&) = delete;
    PDFAppSearchProvider& operator=(const PDFAppSearchProvider&) = delete;

    /// Group of the results of the provider
    virtual PDFAppSearchGroup getGroup() const = 0;

    /// Appends the matches of the query (not sorted, the engine sorts them)
    virtual void search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const = 0;

    /// Appends at least the \p maximumResults best matches (a provider of many
    /// items creates only them) and returns the count of all matches. The default
    /// appends every match of \p search.
    virtual int searchBest(const PDFSearchQuery& query, const PDFAppSearchData& data, int maximumResults, std::vector<PDFAppSearchResult>& results) const;
};

/// Tools of the static catalog: route /tools/{toolId} (Page Not Found until the
/// tool has its page, phase 2), the reason of a match by a keyword
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolSearchProvider : public PDFAppSearchProvider
{
public:
    virtual PDFAppSearchGroup getGroup() const override { return PDFAppSearchGroup::Tools; }
    virtual void search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const override;
};

/// Commands of the registry: the effective shortcut of the user is the meta text,
/// the activation runs the command of the window
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommandSearchProvider : public PDFAppSearchProvider
{
public:
    virtual PDFAppSearchGroup getGroup() const override { return PDFAppSearchGroup::Commands; }
    virtual void search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const override;
};

/// Settings shown in Settings: route /settings/{section}#{anchor}
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingSearchProvider : public PDFAppSearchProvider
{
public:
    virtual PDFAppSearchGroup getGroup() const override { return PDFAppSearchGroup::Settings; }
    virtual void search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const override;
};

/// Recent and pinned files by their names and folders: the activation opens the
/// file. The list may be long (pinned files are never pruned): the results are
/// created only for the best matches.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSearchProvider : public PDFAppSearchProvider
{
public:
    virtual PDFAppSearchGroup getGroup() const override { return PDFAppSearchGroup::Documents; }
    virtual void search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const override;
    virtual int searchBest(const PDFSearchQuery& query, const PDFAppSearchData& data, int maximumResults, std::vector<PDFAppSearchResult>& results) const override;
};

/// The user manual (topics, their headings - route /help/{topic}#{anchor}, which
/// opens in the Help tab, O13 - and the text of the topics) and the video tutorials
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpSearchProvider : public PDFAppSearchProvider
{
public:
    virtual PDFAppSearchGroup getGroup() const override { return PDFAppSearchGroup::Help; }
    virtual void search(const PDFSearchQuery& query, const PDFAppSearchData& data, std::vector<PDFAppSearchResult>& results) const override;
};

/// The global search (Ctrl+K, newapplication.md step 2): the providers of the five
/// groups, the ranking (exact title > title prefix > word of the title > keyword >
/// description, one typo of a word of 4 characters and more, case and diacritics
/// ignored) and the counts of the groups. Pure: no widgets, no database - the data
/// are given by PDFAppSearchData.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchEngine
{
public:
    /// Most results kept of one group (the count of the group counts all of them)
    static constexpr int MAXIMUM_GROUP_RESULTS = 500;

    /// Engine with the built-in providers
    PDFAppSearchEngine();
    ~PDFAppSearchEngine();

    PDFAppSearchEngine(const PDFAppSearchEngine&) = delete;
    PDFAppSearchEngine& operator=(const PDFAppSearchEngine&) = delete;

    /// Searches the text (untrusted) in the data
    PDFAppSearchResults search(const QString& text, const PDFAppSearchData& data) const;

    /// Translated title of a section of Settings ("Security"), the identifier for an unknown section
    static QString getSettingsSectionTitle(const QString& section);

    /// "Opened today", "Opened yesterday", "Sep 28" (this year), "Sep 12, 2026";
    /// empty for an unknown time (0)
    static QString getOpenedText(qint64 lastOpened, const QDateTime& now);

    /// The text of the user as it is shown: invisible characters (bidirectional
    /// overrides, zero width characters, separators of lines) percent-encoded, a
    /// lone surrogate the replacement character, at most \p maximumLength
    /// characters with an ellipsis
    static QString getVisibleText(const QString& text, qsizetype maximumLength);

    /// Hue of the category of a tool of the catalog (Fluent PDFAccentHue)
    static PDFAccentHue getToolHue(int documentToolHue);

private:
    std::vector<std::unique_ptr<PDFAppSearchProvider>> m_providers;
};

}   // namespace pdf

#endif // PDFAPPSEARCHENGINE_H
