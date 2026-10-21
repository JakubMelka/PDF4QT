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

#ifndef PDFDOCUMENTTOOLCATALOG_H
#define PDFDOCUMENTTOOLCATALOG_H

#include "pdfglobal.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>
#include <vector>

namespace pdf
{

/// Category of a tool of the catalog (GUI_overhaul/tools.md 3). The order is the
/// order of the catalog (organize, create or convert, edit, protect, optimize, review).
/// The identifier of a category (getCategoryId) is the value of the route
/// /tools?category= and the prefix of the identifiers of its tools.
enum class PDFDocumentToolCategory
{
    Pages,      ///< Organize Pages ("pages"), hue Amber
    Convert,    ///< Create & Convert ("convert"), hue Blue
    Edit,       ///< Edit ("edit"), hue Green
    Protect,    ///< Protect & Sign ("protect"), hue Red
    Optimize,   ///< Optimize ("optimize"), hue Violet
    Review      ///< Review & Inspect ("review"), hue Teal
};

/// Hue of a category (Figma variant Hue of Tool icon and Tool category header).
/// The names are the names of PDFAccentHue of Pdf4QtLibFluent (the core does not
/// depend on it): the user interface maps the value by a switch, the value
/// Neutral of the Fluent enumeration is never a hue of a tool.
enum class PDFDocumentToolHue
{
    Amber,
    Blue,
    Green,
    Red,
    Violet,
    Teal
};

/// Availability of a tool for a locked (read only) document (tools.md 4.7)
enum class PDFDocumentToolLockedAccess
{
    Available,      ///< The tool does not change the open document
    Unavailable,    ///< The tool changes the document, the card shows "Document is locked"
    NoDocument      ///< The tool does not work with an open document (Images to PDF, Blank PDF, Manage Certificates)
};

/// Category of the catalog: its texts, icon and hue (Figma Tool category header)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolCategoryInfo
{
public:
    constexpr PDFDocumentToolCategoryInfo(PDFDocumentToolCategory category,
                                          const char* id,
                                          const char* title,
                                          const char* chip,
                                          const char* description,
                                          const char* iconName,
                                          PDFDocumentToolHue hue) :
        m_category(category),
        m_id(id),
        m_title(title),
        m_chip(chip),
        m_description(description),
        m_iconName(iconName),
        m_hue(hue)
    {

    }

    PDFDocumentToolCategory getCategory() const { return m_category; }

    /// Identifier ("pages", "convert", ...) - the value of /tools?category=
    QString getId() const;

    /// Translated title ("Organize Pages")
    QString getTitle() const;

    /// Translated short label of the filter chip ("Pages")
    QString getChipText() const;

    /// Translated description of the category ("Merge, split, reorder and resize pages")
    QString getDescription() const;

    /// Name of the icon of Pdf4QtLibFluent/resources/icons ("pages")
    QString getIconName() const;

    PDFDocumentToolHue getHue() const { return m_hue; }

    /// Number of the tools of the category
    int getToolCount() const;

private:
    friend class PDFDocumentToolCatalog;

    PDFDocumentToolCategory m_category;
    const char* m_id;
    const char* m_title;
    const char* m_chip;
    const char* m_description;
    const char* m_iconName;
    PDFDocumentToolHue m_hue;
};

/// Subgroup of a category (tools.md 3, Podskupiny): only an ordering and a heading
/// inside of the category, no own hue or chip
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSubgroupInfo
{
public:
    constexpr PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory category, const char* id, const char* title) :
        m_category(category),
        m_id(id),
        m_title(title)
    {

    }

    PDFDocumentToolCategory getCategory() const { return m_category; }

    /// Identifier unique inside of the category ("combine-split")
    QString getId() const;

    /// Translated title ("Combine & split"). Figma shows the heading in capitals
    /// ("COMBINE & SPLIT"): the user interface sets QFont::AllUppercase, the text
    /// is not stored in capitals (the capitalization depends on the language).
    QString getTitle() const;

private:
    friend class PDFDocumentToolCatalog;

    PDFDocumentToolCategory m_category;
    const char* m_id;
    const char* m_title;
};

/// One tool of the static catalog (GUI_overhaul/tools.md 4 and 5, Figma Tools
/// Overview 272:12821). Data only, no implementation: the tools themselves come
/// in phase 2 (PDFDocumentTool, newapp_tools.md). All texts are in American
/// English in the table and translated by the getters (context
/// "pdf::PDFDocumentToolCatalog").
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolCatalogEntry
{
public:
    constexpr PDFDocumentToolCatalogEntry(const char* id,
                                          PDFDocumentToolCategory category,
                                          const char* subgroupId,
                                          const char* title,
                                          const char* description,
                                          const char* iconName,
                                          const char* keywords,
                                          bool hasOwnTab,
                                          bool isAdvanced,
                                          PDFDocumentToolLockedAccess lockedAccess) :
        m_id(id),
        m_category(category),
        m_subgroupId(subgroupId),
        m_title(title),
        m_description(description),
        m_iconName(iconName),
        m_keywords(keywords),
        m_hasOwnTab(hasOwnTab),
        m_isAdvanced(isAdvanced),
        m_lockedAccess(lockedAccess)
    {

    }

    /// Stable identifier "<category>.<name>" ("pages.merge", "optimize.compress").
    /// It is stored in the database (tool_pin, tool_usage, ...), used in the routes
    /// (/tools/{toolId}), in the What's New files and in MCP - never change it.
    QString getId() const;

    PDFDocumentToolCategory getCategory() const { return m_category; }

    /// Identifier of the subgroup inside of the category (empty for Edit, which has none)
    QString getSubgroupId() const;

    /// Translated title of the subgroup (empty for Edit)
    QString getSubgroupTitle() const;

    /// Translated title of the tool ("Merge")
    QString getTitle() const;

    /// Title in American English (search always matches the English texts)
    QString getSourceTitle() const;

    /// Translated description of the card ("Combine several files into one PDF")
    QString getDescription() const;

    /// Description in American English
    QString getSourceDescription() const;

    /// Name of the icon of Pdf4QtLibFluent/resources/icons, as on the card of
    /// Figma 272:12821 ("layers" for Merge)
    QString getIconName() const;

    /// Hue of the tool = hue of its category
    PDFDocumentToolHue getHue() const;

    /// Search keywords in English, lower case (tools.md 5, rules in tools.md 2).
    /// They are never shown, only as the reason of a match of the search.
    QStringList getKeywords() const;

    /// Keywords in the language of the user interface (the translation of the comma
    /// separated list). Empty, when there is no translation. The search uses
    /// both lists (English always).
    QStringList getTranslatedKeywords() const;

    /// The tool has its own tab (newapp_navigation.md 3: Merge, Interleave Pages,
    /// Manage Pages, Compare, Manage Certificates, Images to PDF); the others run
    /// in the tab of the document
    bool hasOwnTab() const { return m_hasOwnTab; }

    /// The card has the tag "Advanced" (Figma 272:12821: Set Page Boxes, the Print
    /// production and the Developer subgroups of Review & Inspect)
    bool isAdvanced() const { return m_isAdvanced; }

    /// Availability for a locked document (tools.md 4.7)
    PDFDocumentToolLockedAccess getLockedAccess() const { return m_lockedAccess; }

private:
    friend class PDFDocumentToolCatalog;

    const char* m_id;
    PDFDocumentToolCategory m_category;
    const char* m_subgroupId;
    const char* m_title;
    const char* m_description;
    const char* m_iconName;
    const char* m_keywords;
    bool m_hasOwnTab;
    bool m_isAdvanced;
    PDFDocumentToolLockedAccess m_lockedAccess;
};

/// Static catalog of the 46 tools of phase 2 (tools.md, Figma Tools Overview
/// 272:12821). It exists already in phase 1: the catalog page (step 6), the
/// global search, What's New (New tools), Support (tool ids of the videos) and
/// the documentation of MCP use it. The data are constant, every function is
/// thread safe and never fails: an unknown identifier (a row of the database
/// written by another version, a hostile route) gives nullptr / false.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolCatalog
{
public:
    PDFDocumentToolCatalog() = delete;

    /// Longest identifier of a tool or of a category, which is looked up at all
    static constexpr int MAXIMUM_ID_LENGTH = 64;

    /// All tools in the order of the catalog (category, subgroup, position)
    static const std::vector<PDFDocumentToolCatalogEntry>& getTools();

    /// Tools of one category in the order of the catalog
    static std::vector<const PDFDocumentToolCatalogEntry*> getTools(PDFDocumentToolCategory category);

    /// Number of the tools (46)
    static int getToolCount();

    /// Tool by its identifier, nullptr for an unknown one (exact, case sensitive)
    static const PDFDocumentToolCatalogEntry* findTool(QStringView id);

    /// The identifier is a tool of the catalog
    static bool isValidToolId(QStringView id);

    /// Position of the tool in the catalog (0 to getToolCount() - 1), -1 for an unknown one.
    /// Sorting by it keeps the order of the catalog (ties of the suggestions, tools.md 9.3).
    static int getToolIndex(QStringView id);

    /// Categories in the order of the catalog
    static const std::vector<PDFDocumentToolCategoryInfo>& getCategories();

    /// Category by its value (never nullptr for a value of the enumeration;
    /// an invalid value gives nullptr)
    static const PDFDocumentToolCategoryInfo* getCategoryInfo(PDFDocumentToolCategory category);

    /// Category by its identifier ("pages"), std::nullopt for an unknown one
    static std::optional<PDFDocumentToolCategory> findCategory(QStringView id);

    /// Identifier of the category ("pages"), empty for an invalid value
    static QString getCategoryId(PDFDocumentToolCategory category);

    /// Subgroups of the category in their order (empty for Edit)
    static std::vector<const PDFDocumentToolSubgroupInfo*> getSubgroups(PDFDocumentToolCategory category);

    /// Name of the hue as in Figma and in PDFAccentHue ("Amber"), empty for an invalid value
    static QString getHueName(PDFDocumentToolHue hue);

    /// Translation context of the texts of the catalog
    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFDocumentToolCatalog";

private:
    friend class PDFDocumentToolCatalogEntry;
    friend class PDFDocumentToolSubgroupInfo;

    static const PDFDocumentToolSubgroupInfo* findSubgroup(PDFDocumentToolCategory category, const char* id);
    static QStringList splitKeywords(const QString& keywords);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLCATALOG_H
