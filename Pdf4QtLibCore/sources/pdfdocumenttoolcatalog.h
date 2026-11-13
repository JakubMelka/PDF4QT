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
#include "pdfsecurityhandler.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>
#include <vector>

namespace pdf
{
class PDFDocument;

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

/// Input of a tool (newapp_tools.md 2, the data of the introduction page and of MCP)
enum class PDFDocumentToolInputKind
{
    None,       ///< No document (Blank PDF, Images to PDF, Manage Certificates)
    One,        ///< One document (the document of the tab)
    Multiple    ///< More documents (Merge, Compare)
};

/// Result of a tool (newapp_tools.md 2: the choices of the result dialog and the
/// parameter result of MCP)
enum class PDFDocumentToolResultKind
{
    ChangeDocument, ///< The document is changed (Keep in this tab / Save as a copy / Overwrite)
    NewDocument,    ///< A new PDF (Open in a new tab / Save as a file)
    Files,          ///< Files written by the tool (images, text, audio, several PDFs)
    Analysis        ///< Nothing is written, the tool shows information
};

/// How many inputs a tool takes (plan of step 3 of phase 2, K41): the documents and the
/// images of the context together (Merge 2 to 1000, Interleave exactly 2, Images to PDF 1 to
/// 1000 images); the list of the inputs of an own tab and the parameter documentIds of MCP
/// use it, a tool of one document has 1 to 1, a tool without an input 0 to 0
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInputCountRange
{
public:
    int minimum = 0;    ///< Fewest inputs
    int maximum = 0;    ///< Most inputs

    /// The count is inside of the range
    bool contains(int count) const { return count >= minimum && count <= maximum; }

    bool operator==(const PDFDocumentToolInputCountRange&) const = default;
};

/// External dependency of a tool, which may be missing at run time
enum class PDFDocumentToolExternalDependency
{
    None,
    Tesseract,  ///< The OCR engine and its language models
    Scanner     ///< A scanner of the system
};

/// Choice of the result dialog, which is preselected, and the default value of the
/// parameter result of MCP (newapp_tools.md 4, tools.md 9.3 point 11, Figma 296:2761)
enum class PDFDocumentToolPreselectedResult
{
    None,       ///< No choice: an analysis writes nothing
    Keep,       ///< Keep in this tab
    Copy,       ///< Save as a copy
    NewTab,     ///< Open in a new tab (a new document, Figma 378:3674)
    File        ///< Save as a file (the tool writes a new file only, or an export)
};

/// What the result of a tool does with the signatures of a signed document
/// (newapplication.md O31, plan chapter 4). A signature lives in one revision of
/// the file: only an incremental update keeps it valid.
enum class PDFDocumentToolSignatureImpact
{
    None,               ///< The open document is not changed
    IncrementalSave,    ///< The change can be saved as an incremental update of the signed file
    LostCopyOnly        ///< The tool rewrites the whole file, so the signatures stop being valid: only a copy is offered, MCP refuses keep and overwrite
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

    /// The tool with its own tab runs in the tab of a document too, when it is started over an
    /// open document (Manage Pages, plan of step 3b of phase 2, owner answer QB1 B): the panel
    /// Tools, Then continue with and the route /document/{id}/tool/{toolId} keep it in the tab of
    /// the document, where its output "This document" changes that document; only a start without
    /// a document (Home, Tools, an assembly file) opens its own tab. False for every other tool:
    /// a tool with its own tab always gets it (Merge), a tool without one always runs in the tab.
    bool hasDocumentHost() const;

    /// The card has the tag "Advanced" (Figma 272:12821: Set Page Boxes, the Print
    /// production and the Developer subgroups of Review & Inspect)
    bool isAdvanced() const { return m_isAdvanced; }

    /// Availability for a locked document (tools.md 4.7)
    PDFDocumentToolLockedAccess getLockedAccess() const { return m_lockedAccess; }

    // Extended data of phase 2 (step 6, plan chapter 3). The values come from the
    // table of the extended data of the catalog (PDFDocumentToolCatalog::getExtendedData);
    // a tool without a row has the defaults written at each getter.

    /// Input of the tool (default One)
    PDFDocumentToolInputKind getInputKind() const;

    /// Result of the tool (default ChangeDocument)
    PDFDocumentToolResultKind getResultKind() const;

    /// Every kind of the result, which the tool can have, getResultKind first: a tool, whose
    /// result depends on a parameter (Extract Pages: one file = NewDocument, one file per
    /// page = Files; plan of step 3 of phase 2, 4.2), has more (the values of the parameter
    /// result of MCP); PDFDocumentTool::getResultKind gives the kind of the parameters of a run
    std::vector<PDFDocumentToolResultKind> getPossibleResultKinds() const;

    /// How many inputs the tool takes (K41): by the input kind - None 0 to 0, One 1 to 1,
    /// Multiple 2 to MAXIMUM_INPUT_COUNT - unless the tool has its own range (Interleave
    /// exactly 2, Images to PDF 1 to MAXIMUM_INPUT_COUNT images)
    PDFDocumentToolInputCountRange getInputCountRange() const;

    /// The result is one or more new PDF documents made of the pages of the input documents
    /// (Merge, Extract Pages, Interleave Pages, Split): they are not encrypted, so they would
    /// lose every restriction of an input - the tool needs every permission of
    /// PDFDocumentMerger::getRequiredPermissions (owner answer Q2 of step 2 of phase 2, Q4 of
    /// step 3); a tool without an input document never. The value of the kind of the result of
    /// the catalog (getResultKind): a tool, whose output decides it, answers by its parameters
    /// (PDFDocumentTool::isNewDocumentOfInputs; Manage Pages: This document changes the open
    /// document - false here -, One PDF is a new document of the inputs; RB3 a of step 3b).
    bool isNewDocumentOfInputs() const;

    /// The run changes the pages of the open document - inserts, removes, rotates, crops, resizes
    /// them, writes their boxes or arranges them (Insert Pages, Add Image as Page, Remove Pages,
    /// Rotate Pages, Crop Pages, Resize Pages, Set Page Boxes, Manage Pages This document). No
    /// level of a certification allows it (ISO 32000-2, 12.8.2.2, Table 257): the run of such a
    /// tool removes the certification of a certified document (PDFDocMDPPermissions::
    /// isCertificationRemovedBy). The value of the row of the catalog; a tool, whose output
    /// decides it, answers by its parameters (PDFDocumentTool::isPageChange; Manage Pages: only
    /// This document changes the open document).
    bool isPageChange() const;

    /// External dependency (default None)
    PDFDocumentToolExternalDependency getExternalDependency() const;

    /// Command of PdfTool, which does the same ("optimize", "unite"; empty = none)
    QString getPdfToolCommand() const;

    /// Default successors of Then continue with (newapp_tools.md 3, O21): at most
    /// three identifiers of the catalog, never the tool itself, in their order
    /// (invalid and duplicate identifiers of the table are left out; default empty)
    QStringList getDefaultSuccessors() const;

    /// Choice of the result dialog, which is preselected (default None). The value of the row of
    /// the catalog; a tool, whose output decides it, answers by its parameters
    /// (PDFDocumentTool::getPreselectedResult, RB3 a of step 3b).
    PDFDocumentToolPreselectedResult getPreselectedResult() const;

    /// What the result does with the signatures of a signed document (default None). The value
    /// of the row of the catalog; by the parameters PDFDocumentTool::getSignatureImpact.
    PDFDocumentToolSignatureImpact getSignatureImpact() const;

    /// Permissions of an encrypted document, which the tool needs: every one of them
    /// must be granted (ISO 32000-2 Table 22: a tool needs the bits of what it changes;
    /// Rotate Pages: Assemble). A tool, whose options change more (Remove Hidden Data),
    /// checks the rest by itself before its run. Empty = the tool asks for no permission.
    std::vector<PDFSecurityHandler::Permission> getRequiredPermissions() const;

    /// The security handler of the document grants what the tool needs: a document
    /// without a handler grants everything, an owner too (nullptr grants nothing)
    bool isPermitted(const PDFDocument* document) const;

    /// Topic of the user manual of the tool: tool-<id with a dash instead of the dot>
    /// ("tool-optimize-compress", plan chapter 5)
    QString getHelpTopic() const;

    /// Row Result of At a glance of the start page ("A smaller PDF", 279:2054), by the
    /// kind of the result, when the tool has no row of its own
    QString getResultSummary() const;

    /// End of the note of the start page ("a locked document cannot be compressed", 277:2099)
    QString getLockedNote() const;

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

/// Extended data of one tool (phase 2 preparation, step 6): a row of the table
/// PDFDocumentToolCatalog::getExtendedData. Data only.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolExtendedData
{
public:
    /// Row of a tool with the defaults of its result kind: the preselected result and
    /// the signature impact follow the kind, no permission is needed
    constexpr PDFDocumentToolExtendedData(const char* id,
                                          PDFDocumentToolInputKind inputKind,
                                          PDFDocumentToolResultKind resultKind,
                                          PDFDocumentToolExternalDependency externalDependency,
                                          const char* pdfToolCommand,
                                          const char* successors) :
        PDFDocumentToolExtendedData(id, inputKind, resultKind, externalDependency, pdfToolCommand, successors,
                                    getDefaultPreselectedResult(resultKind), getDefaultSignatureImpact(resultKind), 0)
    {

    }

    /// \param requiredPermissions Mask of the values of PDFSecurityHandler::Permission,
    ///        every one of them must be granted (0 = none needed)
    constexpr PDFDocumentToolExtendedData(const char* id,
                                          PDFDocumentToolInputKind inputKind,
                                          PDFDocumentToolResultKind resultKind,
                                          PDFDocumentToolExternalDependency externalDependency,
                                          const char* pdfToolCommand,
                                          const char* successors,
                                          PDFDocumentToolPreselectedResult preselectedResult,
                                          PDFDocumentToolSignatureImpact signatureImpact,
                                          uint32_t requiredPermissions) :
        m_id(id),
        m_inputKind(inputKind),
        m_resultKind(resultKind),
        m_externalDependency(externalDependency),
        m_pdfToolCommand(pdfToolCommand),
        m_successors(successors),
        m_preselectedResult(preselectedResult),
        m_signatureImpact(signatureImpact),
        m_requiredPermissions(requiredPermissions)
    {

    }

    /// Preselected result of a tool of the kind, which does not differ (tools.md 9.3 point 11)
    static constexpr PDFDocumentToolPreselectedResult getDefaultPreselectedResult(PDFDocumentToolResultKind kind)
    {
        switch (kind)
        {
            case PDFDocumentToolResultKind::ChangeDocument:
                return PDFDocumentToolPreselectedResult::Keep;
            case PDFDocumentToolResultKind::NewDocument:
                return PDFDocumentToolPreselectedResult::NewTab;
            case PDFDocumentToolResultKind::Files:
                return PDFDocumentToolPreselectedResult::File;
            case PDFDocumentToolResultKind::Analysis:
                break;
        }
        return PDFDocumentToolPreselectedResult::None;
    }

    /// Signature impact of a tool of the kind: only a change of the open document
    /// touches its signatures, an incremental update keeps them
    static constexpr PDFDocumentToolSignatureImpact getDefaultSignatureImpact(PDFDocumentToolResultKind kind)
    {
        return kind == PDFDocumentToolResultKind::ChangeDocument ? PDFDocumentToolSignatureImpact::IncrementalSave : PDFDocumentToolSignatureImpact::None;
    }

    /// Identifier of the tool of the catalog
    QString getId() const;

    PDFDocumentToolInputKind getInputKind() const { return m_inputKind; }
    PDFDocumentToolResultKind getResultKind() const { return m_resultKind; }
    PDFDocumentToolExternalDependency getExternalDependency() const { return m_externalDependency; }
    PDFDocumentToolPreselectedResult getPreselectedResult() const { return m_preselectedResult; }
    PDFDocumentToolSignatureImpact getSignatureImpact() const { return m_signatureImpact; }

    /// Mask of the values of PDFSecurityHandler::Permission, every one of them must be granted
    uint32_t getRequiredPermissionMask() const { return m_requiredPermissions; }

    /// Command of PdfTool (empty = none)
    QString getPdfToolCommand() const;

    /// Comma separated identifiers of the default successors, as written in the table
    QString getSuccessorsText() const;

private:
    friend class PDFDocumentToolCatalog;

    const char* m_id;
    PDFDocumentToolInputKind m_inputKind;
    PDFDocumentToolResultKind m_resultKind;
    PDFDocumentToolExternalDependency m_externalDependency;
    const char* m_pdfToolCommand;
    const char* m_successors;
    PDFDocumentToolPreselectedResult m_preselectedResult;
    PDFDocumentToolSignatureImpact m_signatureImpact;
    uint32_t m_requiredPermissions;
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

    /// Most default successors of a tool (O21)
    static constexpr int MAXIMUM_DEFAULT_SUCCESSORS = 3;

    /// Most inputs of a tool of more inputs (Merge, Images to PDF; chapter 6 of the plan of
    /// step 2 of phase 2: PDFDocumentToolMerge::MAXIMUM_INPUT_COUNT)
    static constexpr int MAXIMUM_INPUT_COUNT = 1000;

    /// Table of the extended data of the tools (input and result kinds, external
    /// dependency, command of PdfTool, default successors), at most one row per
    /// tool; a tool without a row has the defaults of the getters of
    /// PDFDocumentToolCatalogEntry. Every tool of the catalog has its row (step 6, package D).
    static const std::vector<PDFDocumentToolExtendedData>& getExtendedData();

    /// Row of the extended data of the tool, nullptr = none (defaults)
    static const PDFDocumentToolExtendedData* findExtendedData(QStringView id);

private:
    friend class PDFDocumentToolCatalogEntry;
    friend class PDFDocumentToolSubgroupInfo;

    static const PDFDocumentToolSubgroupInfo* findSubgroup(PDFDocumentToolCategory category, const char* id);
    static QStringList splitKeywords(const QString& keywords);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLCATALOG_H
