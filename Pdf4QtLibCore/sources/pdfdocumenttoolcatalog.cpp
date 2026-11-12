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

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocument.h"

#include <QCoreApplication>

#include <algorithm>

namespace pdf
{

/// Helpers of the catalog
class PDFDocumentToolCatalogHelper
{
public:
    PDFDocumentToolCatalogHelper() = delete;

    /// Predicate of the tools of one category
    class CategoryPredicate
    {
    public:
        explicit CategoryPredicate(PDFDocumentToolCategory category) : m_category(category) { }

        bool operator()(const PDFDocumentToolCatalogEntry& tool) const { return tool.getCategory() == m_category; }

    private:
        PDFDocumentToolCategory m_category;
    };

    /// Subgroups of all categories in their order (tools.md 3, Podskupiny)
    static const std::vector<PDFDocumentToolSubgroupInfo>& getSubgroups()
    {
        static const std::vector<PDFDocumentToolSubgroupInfo> subgroups =
        {
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Pages, "combine-split", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Combine & split")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Pages, "arrange", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Arrange")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Pages, "page-size", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Page size")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Convert, "create", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Create")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Convert, "recognize", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Recognize")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Convert, "export", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Export")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Protect, "encryption", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Encryption")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Protect, "signatures", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Signatures")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Protect, "privacy", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Privacy")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Optimize, "file-size", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "File size")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Optimize, "color", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Color")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Review, "review", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Review")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Review, "print-production", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Print production")),
            PDFDocumentToolSubgroupInfo(PDFDocumentToolCategory::Review, "developer", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Developer")),
        };
        return subgroups;
    }
};

QString PDFDocumentToolCategoryInfo::getId() const
{
    return QString::fromLatin1(m_id);
}

QString PDFDocumentToolCategoryInfo::getTitle() const
{
    return QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_title);
}

QString PDFDocumentToolCategoryInfo::getChipText() const
{
    return QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_chip);
}

QString PDFDocumentToolCategoryInfo::getDescription() const
{
    return QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_description);
}

QString PDFDocumentToolCategoryInfo::getIconName() const
{
    return QString::fromLatin1(m_iconName);
}

int PDFDocumentToolCategoryInfo::getToolCount() const
{
    const std::vector<PDFDocumentToolCatalogEntry>& tools = PDFDocumentToolCatalog::getTools();
    return int(std::count_if(tools.cbegin(), tools.cend(), PDFDocumentToolCatalogHelper::CategoryPredicate(m_category)));
}

QString PDFDocumentToolSubgroupInfo::getId() const
{
    return QString::fromLatin1(m_id);
}

QString PDFDocumentToolSubgroupInfo::getTitle() const
{
    return QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_title);
}

QString PDFDocumentToolCatalogEntry::getId() const
{
    return QString::fromLatin1(m_id);
}

QString PDFDocumentToolCatalogEntry::getSubgroupId() const
{
    return QString::fromLatin1(m_subgroupId);
}

QString PDFDocumentToolCatalogEntry::getSubgroupTitle() const
{
    const PDFDocumentToolSubgroupInfo* subgroup = PDFDocumentToolCatalog::findSubgroup(m_category, m_subgroupId);
    return subgroup ? subgroup->getTitle() : QString();
}

QString PDFDocumentToolCatalogEntry::getTitle() const
{
    return QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_title);
}

QString PDFDocumentToolCatalogEntry::getSourceTitle() const
{
    return QString::fromUtf8(m_title);
}

QString PDFDocumentToolCatalogEntry::getDescription() const
{
    return QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_description);
}

QString PDFDocumentToolCatalogEntry::getSourceDescription() const
{
    return QString::fromUtf8(m_description);
}

QString PDFDocumentToolCatalogEntry::getIconName() const
{
    return QString::fromLatin1(m_iconName);
}

PDFDocumentToolHue PDFDocumentToolCatalogEntry::getHue() const
{
    const PDFDocumentToolCategoryInfo* category = PDFDocumentToolCatalog::getCategoryInfo(m_category);
    return category ? category->getHue() : PDFDocumentToolHue::Blue;
}

QStringList PDFDocumentToolCatalogEntry::getKeywords() const
{
    return PDFDocumentToolCatalog::splitKeywords(QString::fromUtf8(m_keywords));
}

QStringList PDFDocumentToolCatalogEntry::getTranslatedKeywords() const
{
    const QString source = QString::fromUtf8(m_keywords);
    const QString translated = QCoreApplication::translate(PDFDocumentToolCatalog::TRANSLATION_CONTEXT, m_keywords);
    if (translated == source)
    {
        return QStringList();
    }
    return PDFDocumentToolCatalog::splitKeywords(translated);
}

PDFDocumentToolInputKind PDFDocumentToolCatalogEntry::getInputKind() const
{
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    return data ? data->getInputKind() : PDFDocumentToolInputKind::One;
}

PDFDocumentToolResultKind PDFDocumentToolCatalogEntry::getResultKind() const
{
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    return data ? data->getResultKind() : PDFDocumentToolResultKind::ChangeDocument;
}

/// Kinds of the result of a tool, whose result depends on a parameter (a row per other kind, the
/// kind of the catalog first; plan of step 3 of phase 2, 4.2): Extract Pages one file (NewDocument)
/// or one file per page (Files). Manage Pages (plan of step 3b, 4.4): This document (the kind of
/// the catalog, ChangeDocument), One PDF (NewDocument), One PDF per group or item and One PDF per
/// page (Files).
class PDFDocumentToolResultKindsRow
{
public:
    const char* toolId;
    PDFDocumentToolResultKind otherKind;
};

static constexpr PDFDocumentToolResultKindsRow TOOL_OTHER_RESULT_KINDS[] =
{
    { "pages.extract", PDFDocumentToolResultKind::Files },
    { "pages.manage", PDFDocumentToolResultKind::NewDocument },
    { "pages.manage", PDFDocumentToolResultKind::Files },
};

/// Inputs of a tool, which differ from the default of its input kind (K41 of the plan of step 3
/// of phase 2: the front and the back sides of Interleave; the images of Images to PDF, whose
/// input kind None says it opens no document; Manage Pages 1 to 1000 sources - one document is
/// arranged by it too, plan of step 3b, 1 point 7).
class PDFDocumentToolInputCountRow
{
public:
    const char* toolId;
    int minimum;
    int maximum;
};

static constexpr PDFDocumentToolInputCountRow TOOL_INPUT_COUNTS[] =
{
    { "pages.interleave", 2, 2 },
    { "convert.images-to-pdf", 1, PDFDocumentToolCatalog::MAXIMUM_INPUT_COUNT },
    { "pages.manage", 1, PDFDocumentToolCatalog::MAXIMUM_INPUT_COUNT },
};

/// Tools with their own tab, which run in the tab of a document, when they are started over
/// one (PDFDocumentToolCatalogEntry::hasDocumentHost; plan of step 3b of phase 2, QB1 B)
static constexpr const char* TOOL_DOCUMENT_HOSTS[] =
{
    "pages.manage",
};

/// Tools, whose files of the result are new PDF documents of the pages of the input document
/// (Split; plan of step 3 of phase 2, Q4)
static constexpr const char* TOOL_PDF_FILES_OF_INPUTS[] =
{
    "pages.split",
};

std::vector<PDFDocumentToolResultKind> PDFDocumentToolCatalogEntry::getPossibleResultKinds() const
{
    std::vector<PDFDocumentToolResultKind> kinds = { getResultKind() };
    for (const PDFDocumentToolResultKindsRow& row : TOOL_OTHER_RESULT_KINDS)
    {
        if (getId() == QLatin1String(row.toolId) && std::find(kinds.cbegin(), kinds.cend(), row.otherKind) == kinds.cend())
        {
            kinds.push_back(row.otherKind);
        }
    }
    return kinds;
}

PDFDocumentToolInputCountRange PDFDocumentToolCatalogEntry::getInputCountRange() const
{
    for (const PDFDocumentToolInputCountRow& row : TOOL_INPUT_COUNTS)
    {
        if (getId() == QLatin1String(row.toolId))
        {
            return PDFDocumentToolInputCountRange{ row.minimum, row.maximum };
        }
    }

    switch (getInputKind())
    {
        case PDFDocumentToolInputKind::None:
            return PDFDocumentToolInputCountRange{ 0, 0 };
        case PDFDocumentToolInputKind::One:
            return PDFDocumentToolInputCountRange{ 1, 1 };
        case PDFDocumentToolInputKind::Multiple:
            return PDFDocumentToolInputCountRange{ 2, PDFDocumentToolCatalog::MAXIMUM_INPUT_COUNT };
    }
    return PDFDocumentToolInputCountRange();
}

bool PDFDocumentToolCatalogEntry::hasDocumentHost() const
{
    if (!m_hasOwnTab)
    {
        return false;
    }

    for (const char* toolId : TOOL_DOCUMENT_HOSTS)
    {
        if (getId() == QLatin1String(toolId))
        {
            return true;
        }
    }
    return false;
}

bool PDFDocumentToolCatalogEntry::isNewDocumentOfInputs() const
{
    if (getInputKind() == PDFDocumentToolInputKind::None)
    {
        return false;
    }

    // The kind of the row of the catalog: a tool, whose output decides, answers by its
    // parameters (PDFDocumentTool::isNewDocumentOfInputs; Manage Pages, RB3 a of step 3b)
    if (getResultKind() == PDFDocumentToolResultKind::NewDocument)
    {
        return true;
    }
    for (const char* toolId : TOOL_PDF_FILES_OF_INPUTS)
    {
        if (getId() == QLatin1String(toolId))
        {
            return true;
        }
    }
    return false;
}

PDFDocumentToolExternalDependency PDFDocumentToolCatalogEntry::getExternalDependency() const
{
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    return data ? data->getExternalDependency() : PDFDocumentToolExternalDependency::None;
}

QString PDFDocumentToolCatalogEntry::getPdfToolCommand() const
{
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    return data ? data->getPdfToolCommand() : QString();
}

PDFDocumentToolPreselectedResult PDFDocumentToolCatalogEntry::getPreselectedResult() const
{
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    return data ? data->getPreselectedResult() : PDFDocumentToolPreselectedResult::None;
}

PDFDocumentToolSignatureImpact PDFDocumentToolCatalogEntry::getSignatureImpact() const
{
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    return data ? data->getSignatureImpact() : PDFDocumentToolSignatureImpact::None;
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolCatalogEntry::getRequiredPermissions() const
{
    std::vector<PDFSecurityHandler::Permission> permissions;
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    if (!data)
    {
        return permissions;
    }

    using Permission = PDFSecurityHandler::Permission;
    for (const Permission permission : { Permission::PrintLowResolution, Permission::Modify, Permission::CopyContent, Permission::ModifyInteractiveItems,
                                         Permission::ModifyFormFields, Permission::Accessibility, Permission::Assemble, Permission::PrintHighResolution })
    {
        if (data->getRequiredPermissionMask() & uint32_t(permission))
        {
            permissions.push_back(permission);
        }
    }
    return permissions;
}

bool PDFDocumentToolCatalogEntry::isPermitted(const PDFDocument* document) const
{
    if (!document)
    {
        return false;
    }

    const std::vector<PDFSecurityHandler::Permission> permissions = getRequiredPermissions();
    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (permissions.empty() || !securityHandler)
    {
        return true;
    }

    // The table of the revision of the handler (Table 22, SECURITY_TODO 31): the same rule as
    // the new application asks by (PDFAppDocumentPermissions)
    for (const PDFSecurityHandler::Permission permission : permissions)
    {
        if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, permission))
        {
            return false;
        }
    }
    return true;
}

QString PDFDocumentToolCatalogEntry::getHelpTopic() const
{
    QString topic = QStringLiteral("tool-") + getId();
    topic.replace(QLatin1Char('.'), QLatin1Char('-'));
    return topic;
}

/// The texts of the start page of a tool, which has its own (a row per tool)
class PDFDocumentToolStartTexts
{
public:
    const char* toolId;
    const char* resultSummary;
    const char* lockedNote;
};

static constexpr PDFDocumentToolStartTexts TOOL_START_TEXTS[] =
{
    { "optimize.compress", QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "A smaller PDF"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "a locked document cannot be compressed") },
};

QString PDFDocumentToolCatalogEntry::getResultSummary() const
{
    for (const PDFDocumentToolStartTexts& texts : TOOL_START_TEXTS)
    {
        if (getId() == QLatin1String(texts.toolId))
        {
            return QCoreApplication::translate("pdf::PDFDocumentToolCatalog", texts.resultSummary);
        }
    }

    switch (getResultKind())
    {
        case PDFDocumentToolResultKind::ChangeDocument:
            return QCoreApplication::translate("pdf::PDFDocumentToolCatalog", "A changed PDF");
        case PDFDocumentToolResultKind::NewDocument:
            return QCoreApplication::translate("pdf::PDFDocumentToolCatalog", "A new PDF");
        case PDFDocumentToolResultKind::Files:
            return QCoreApplication::translate("pdf::PDFDocumentToolCatalog", "Files");
        case PDFDocumentToolResultKind::Analysis:
            return QCoreApplication::translate("pdf::PDFDocumentToolCatalog", "A report");
    }
    return QString();
}

QString PDFDocumentToolCatalogEntry::getLockedNote() const
{
    for (const PDFDocumentToolStartTexts& texts : TOOL_START_TEXTS)
    {
        if (getId() == QLatin1String(texts.toolId))
        {
            return QCoreApplication::translate("pdf::PDFDocumentToolCatalog", texts.lockedNote);
        }
    }
    return getLockedAccess() == PDFDocumentToolLockedAccess::Unavailable ? QCoreApplication::translate("pdf::PDFDocumentToolCatalog", "a locked document cannot be changed by this tool")
                                                                         : QCoreApplication::translate("pdf::PDFDocumentToolCatalog", "a locked document can be used too");
}

QStringList PDFDocumentToolCatalogEntry::getDefaultSuccessors() const
{
    QStringList successors;
    const PDFDocumentToolExtendedData* data = PDFDocumentToolCatalog::findExtendedData(getId());
    if (!data)
    {
        return successors;
    }

    const QString id = getId();
    for (const QString& successor : PDFDocumentToolCatalog::splitKeywords(data->getSuccessorsText()))
    {
        if (successor != id && PDFDocumentToolCatalog::isValidToolId(successor) && !successors.contains(successor))
        {
            successors << successor;
            if (successors.size() >= PDFDocumentToolCatalog::MAXIMUM_DEFAULT_SUCCESSORS)
            {
                break;
            }
        }
    }
    return successors;
}

QString PDFDocumentToolExtendedData::getId() const
{
    return QString::fromLatin1(m_id);
}

QString PDFDocumentToolExtendedData::getPdfToolCommand() const
{
    return QString::fromLatin1(m_pdfToolCommand ? m_pdfToolCommand : "");
}

QString PDFDocumentToolExtendedData::getSuccessorsText() const
{
    return QString::fromLatin1(m_successors ? m_successors : "");
}

const std::vector<PDFDocumentToolExtendedData>& PDFDocumentToolCatalog::getExtendedData()
{
    // Extended data of phase 2 (plan of step 6, chapter 3), one row per tool in the
    // order of the catalog: the input and result kinds and the external dependency
    // from tools.md 5 ("Vstup -> výstup"), the command of PdfTool, which does the
    // same ("Dnes: ... CLI"; a command, which only lists something, is not the
    // tool), the default successors from Then continue with of the *-done frames of
    // Figma in the order of their choices (none for a tool without such a frame).
    // Decisions: Split, the exports and Manage Certificates write files (Files);
    // Manage Pages works over more documents and changes the open one; Scan to
    // PDF starts without a document and creates a new one (adding pages to an
    // open document is its option); Redact Content writes a new redacted copy;
    // Sign with Certificate and Add Timestamp write a new signed file and leave
    // the open document unchanged (tools.md 4.7, 9.3 point 11, 10.3 point 31).
    // The preselected choice of the result dialog, the signature impact and the
    // permissions differ from the defaults of the kind only where a row names them:
    // a copy for the tools, which cannot be taken back (tools.md 9.3 point 11, K1 of
    // the plan of the first batch), a whole rewrite for Compress, Optimize Structure
    // and Remove Hidden Data (O31). The permissions: every bit of the mask is needed, a
    // tool needs the bits of what it changes (owner 2026-11-06), ISO 32000-2, 7.6.4.2,
    // Table 22 (an owner password grants everything, PDFSecurityHandler::isAllowed):
    //  - bit 4 Modify: "Modify the contents of the document by operations other than those
    //    controlled by bits 6, 9, and 11" - Compress, Optimize Structure and Remove Hidden
    //    Data rewrite the content and the objects of the document;
    //  - bit 5 CopyContent: "Copy or otherwise extract text and graphics from the document" -
    //    Extract Text (bit 10, the extraction for accessibility, is deprecated in PDF 2.0);
    //  - bit 6 ModifyInteractiveItems: "Add or modify text annotations, fill in interactive
    //    form fields, and, if bit 4 is also set, create or modify interactive form fields
    //    (including signature fields)" - Remove External Links removes link annotations only;
    //    Remove Hidden Data needs it too, when its kinds remove annotations or fields
    //    (PDFDocumentToolSanitize::getInteractiveKinds, checked before its run);
    //  - bit 11 Assemble: "Assemble the document (insert, rotate, or delete pages and create
    //    document outline items or thumbnail images), even if bit 4 is clear" - Rotate Pages,
    //    Remove Pages and Merge (it inserts the pages of a document into another one); bit 4
    //    alone does not allow them. Merge needs also the owner password of a document, which
    //    restricts anything else, because its result is not encrypted (owner answer Q2 of
    //    step 2 of phase 2, PDFDocumentMerger::getPermission in its validation).
    // The tools of step 3 of phase 2 (plan, Q4 with R4): Split, Extract Pages and Interleave Pages
    // make new documents of the pages of their inputs - bit 11 and the rule of Merge
    // (PDFDocumentToolCatalogEntry::isNewDocumentOfInputs); Insert Pages and Add Image as Page
    // insert pages into the open document - bit 11 (their sources are checked as the inputs of
    // Merge by the tools); Crop Pages and Set Page Boxes change the boxes of the pages - bit 4
    // (bit 11 does not name them); Resize Pages - bit 4, and bit 6 for a run, which moves or
    // scales an annotation (checked by the tool); Images to PDF has no input document.
    // Manage Pages (plan of step 3b of phase 2, RB3 a): the row is its output This document in
    // the tab of a document (a change of its pages, Keep, an incremental update of a signed file,
    // bit 11); the other outputs make new documents of its inputs, their values come from the
    // tool by its parameters (PDFDocumentTool::getResultKind, getPreselectedResult,
    // getSignatureImpact, isNewDocumentOfInputs).
    using Input = PDFDocumentToolInputKind;
    using Result = PDFDocumentToolResultKind;
    using Dependency = PDFDocumentToolExternalDependency;
    using Preselected = PDFDocumentToolPreselectedResult;
    using Signature = PDFDocumentToolSignatureImpact;
    using Permission = PDFSecurityHandler::Permission;
    static const std::vector<PDFDocumentToolExtendedData> data =
    {
        PDFDocumentToolExtendedData("pages.merge", Input::Multiple, Result::NewDocument, Dependency::None, "unite", "edit.page-numbers,optimize.compress,protect.password", Preselected::NewTab, Signature::None, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.split", Input::One, Result::Files, Dependency::None, "separate", "", Preselected::File, Signature::None, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.extract", Input::One, Result::NewDocument, Dependency::None, "", "optimize.compress,protect.password", Preselected::NewTab, Signature::None, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.interleave", Input::Multiple, Result::NewDocument, Dependency::None, "", "convert.ocr,optimize.compress", Preselected::NewTab, Signature::None, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.manage", Input::Multiple, Result::ChangeDocument, Dependency::None, "", "edit.page-numbers,optimize.compress,protect.password", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.insert", Input::One, Result::ChangeDocument, Dependency::None, "", "edit.page-numbers,optimize.compress", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.add-image", Input::One, Result::ChangeDocument, Dependency::None, "", "convert.ocr,optimize.compress", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.remove", Input::One, Result::ChangeDocument, Dependency::None, "", "optimize.compress,edit.page-numbers,protect.password", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.rotate", Input::One, Result::ChangeDocument, Dependency::None, "", "convert.ocr,optimize.compress,pages.remove", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Assemble)),
        PDFDocumentToolExtendedData("pages.crop", Input::One, Result::ChangeDocument, Dependency::None, "", "pages.resize,optimize.compress", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Modify)),
        PDFDocumentToolExtendedData("pages.resize", Input::One, Result::ChangeDocument, Dependency::None, "", "optimize.compress,edit.page-numbers", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Modify)),
        PDFDocumentToolExtendedData("pages.boxes", Input::One, Result::ChangeDocument, Dependency::None, "", "review.output-preview", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::Modify)),
        PDFDocumentToolExtendedData("convert.images-to-pdf", Input::None, Result::NewDocument, Dependency::None, "", "optimize.images"),
        PDFDocumentToolExtendedData("convert.blank", Input::None, Result::NewDocument, Dependency::None, "", ""),
        PDFDocumentToolExtendedData("convert.scan", Input::None, Result::NewDocument, Dependency::Scanner, "", "optimize.compress"),
        PDFDocumentToolExtendedData("convert.ocr", Input::One, Result::ChangeDocument, Dependency::Tesseract, "ocr", "optimize.compress,convert.extract-text"),
        PDFDocumentToolExtendedData("convert.pdf-to-images", Input::One, Result::Files, Dependency::None, "render", ""),
        PDFDocumentToolExtendedData("convert.extract-text", Input::One, Result::Files, Dependency::None, "fetch-text", "", Preselected::File, Signature::None, uint32_t(Permission::CopyContent)),
        PDFDocumentToolExtendedData("convert.extract-images", Input::One, Result::Files, Dependency::None, "fetch-images", ""),
        PDFDocumentToolExtendedData("convert.audiobook", Input::One, Result::Files, Dependency::None, "audio-book", ""),
        PDFDocumentToolExtendedData("edit.content", Input::One, Result::ChangeDocument, Dependency::None, "", "edit.page-numbers,optimize.compress"),
        PDFDocumentToolExtendedData("edit.page-numbers", Input::One, Result::ChangeDocument, Dependency::None, "", "optimize.compress,protect.password"),
        PDFDocumentToolExtendedData("protect.password", Input::One, Result::ChangeDocument, Dependency::None, "encrypt", "", Preselected::Copy, Signature::IncrementalSave, 0),
        PDFDocumentToolExtendedData("protect.permissions", Input::One, Result::ChangeDocument, Dependency::None, "encrypt", "", Preselected::Copy, Signature::IncrementalSave, 0),
        PDFDocumentToolExtendedData("protect.certificate-encrypt", Input::One, Result::ChangeDocument, Dependency::None, "", "", Preselected::Copy, Signature::IncrementalSave, 0),
        PDFDocumentToolExtendedData("protect.unlock", Input::One, Result::ChangeDocument, Dependency::None, "decrypt", "", Preselected::Copy, Signature::IncrementalSave, 0),
        PDFDocumentToolExtendedData("protect.sign", Input::One, Result::NewDocument, Dependency::None, "", "", Preselected::File, Signature::None, 0),
        PDFDocumentToolExtendedData("protect.timestamp", Input::One, Result::NewDocument, Dependency::None, "", "", Preselected::File, Signature::None, 0),
        PDFDocumentToolExtendedData("protect.sign-by-hand", Input::One, Result::ChangeDocument, Dependency::None, "", "protect.password,optimize.compress"),
        PDFDocumentToolExtendedData("protect.certificates", Input::None, Result::Files, Dependency::None, "cert-store", ""),
        PDFDocumentToolExtendedData("protect.redact", Input::One, Result::NewDocument, Dependency::None, "redact", "protect.sanitize,protect.password,optimize.compress", Preselected::File, Signature::None, 0),
        PDFDocumentToolExtendedData("protect.sanitize", Input::One, Result::ChangeDocument, Dependency::None, "", "protect.password,optimize.compress,protect.sign", Preselected::Copy, Signature::LostCopyOnly, uint32_t(Permission::Modify)),
        PDFDocumentToolExtendedData("protect.remove-links", Input::One, Result::ChangeDocument, Dependency::None, "remove-external-links", "protect.sanitize,protect.password,optimize.compress", Preselected::Keep, Signature::IncrementalSave, uint32_t(Permission::ModifyInteractiveItems)),
        PDFDocumentToolExtendedData("optimize.compress", Input::One, Result::ChangeDocument, Dependency::None, "optimize", "protect.password,protect.sign,pages.merge", Preselected::Copy, Signature::LostCopyOnly, uint32_t(Permission::Modify)),
        PDFDocumentToolExtendedData("optimize.images", Input::One, Result::ChangeDocument, Dependency::None, "", "optimize.structure"),
        PDFDocumentToolExtendedData("optimize.structure", Input::One, Result::ChangeDocument, Dependency::None, "optimize", "optimize.images,optimize.compress,protect.password", Preselected::Keep, Signature::LostCopyOnly, uint32_t(Permission::Modify)),
        PDFDocumentToolExtendedData("optimize.bitonal", Input::One, Result::ChangeDocument, Dependency::None, "bitonal", "convert.ocr", Preselected::Copy, Signature::IncrementalSave, 0),
        PDFDocumentToolExtendedData("optimize.grayscale", Input::One, Result::ChangeDocument, Dependency::None, "", "optimize.compress"),
        PDFDocumentToolExtendedData("review.compare", Input::Multiple, Result::Analysis, Dependency::None, "diff", ""),
        PDFDocumentToolExtendedData("review.measure", Input::One, Result::Analysis, Dependency::None, "", ""),
        PDFDocumentToolExtendedData("review.output-preview", Input::One, Result::Analysis, Dependency::None, "", ""),
        PDFDocumentToolExtendedData("review.ink-coverage", Input::One, Result::Analysis, Dependency::None, "ink-coverage", ""),
        PDFDocumentToolExtendedData("review.soft-proof", Input::One, Result::Analysis, Dependency::None, "", ""),
        PDFDocumentToolExtendedData("review.statistics", Input::One, Result::Analysis, Dependency::None, "statistics", ""),
        PDFDocumentToolExtendedData("review.object-inspector", Input::One, Result::Analysis, Dependency::None, "", ""),
        PDFDocumentToolExtendedData("review.report", Input::One, Result::Analysis, Dependency::None, "info", ""),
    };
    return data;
}

const PDFDocumentToolExtendedData* PDFDocumentToolCatalog::findExtendedData(QStringView id)
{
    if (id.isEmpty() || id.size() > MAXIMUM_ID_LENGTH)
    {
        return nullptr;
    }

    for (const PDFDocumentToolExtendedData& data : getExtendedData())
    {
        if (data.m_id && id == QLatin1StringView(data.m_id))
        {
            return &data;
        }
    }
    return nullptr;
}

const std::vector<PDFDocumentToolCatalogEntry>& PDFDocumentToolCatalog::getTools()
{
    // The tools of GUI_overhaul/tools.md 4 and 5: identifiers, keywords, subgroups
    // and the availability for a locked document (4.7) from tools.md, titles,
    // descriptions, icons and the tag Advanced from the cards of Figma Tools
    // Overview 272:12821, own tabs from newapp_navigation.md 3.
    static const std::vector<PDFDocumentToolCatalogEntry> tools =
    {
    PDFDocumentToolCatalogEntry("pages.merge", PDFDocumentToolCategory::Pages, "combine-split",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Merge"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Combine several files into one PDF"),
        "layers",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "merge, join, combine, assemble, unite, united document, merge pdf, concatenate, connect, append, bind, glue, stitch, put together, add files, one file, binder, collate"),
        true, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("pages.split", PDFDocumentToolCategory::Pages, "combine-split",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Split"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Separate a document into several files"),
        "scissors",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "split, split pdf, separate, separate to multiple documents, regroup, divide, break apart, disassemble, unbind, burst, cut, chapters, by bookmarks, by outline, by size, every page, single pages, even odd, page ranges, parts, chunk"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("pages.extract", PDFDocumentToolCategory::Pages, "combine-split",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Extract Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Save selected pages as a new PDF"),
        "extract",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "extract pages, export pages, save pages as, pull out, page range, subset, copy pages, selected pages, take out, pick pages"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("pages.interleave", PDFDocumentToolCategory::Pages, "combine-split",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Interleave Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Mix front and back sides of a scan"),
        "repeat",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "interleave, regroup by alternating pages, alternate, mix, alternate mix, collate, zip, zipper, duplex scan, double sided, front and back, odd even merge, shuffle"),
        true, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("pages.manage", PDFDocumentToolCategory::Pages, "arrange",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Manage Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Reorder, group and arrange pages"),
        "grid",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "organize, manage, arrange, reorder, rearrange, resequence, move pages, sort, page order, thumbnails, group, duplicate, clone, reverse order, drag and drop, page sorter, reorganize pages, assembly, pagemaster, workspace"),
        // A locked document is a source of a new PDF; only its output This document is
        // Disabled (plan of step 3b of phase 2, 4.5)
        true, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("pages.insert", PDFDocumentToolCategory::Pages, "arrange",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Insert Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Add PDF pages, images or blank pages"),
        "page-insert",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "insert, insert pdf, insert pdf pages, insert empty page, add pages, append, import pages, blank page, empty page, replace pages, replace selection, paste pages, attach pages, add pdf"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("pages.add-image", PDFDocumentToolCategory::Pages, "arrange",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Add Image as Page"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Insert a picture as a new page"),
        "page-image",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "add image, image as page, insert image, picture page, photo page, scan page, add picture, jpg, png"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("pages.remove", PDFDocumentToolCategory::Pages, "arrange",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Remove Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Delete unwanted pages"),
        "trash",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "remove, delete, discard, drop pages, erase page, cut out, get rid of, trash, delete pages, remove selection"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("pages.rotate", PDFDocumentToolCategory::Pages, "arrange",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Rotate Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Turn pages in 90° steps"),
        "rotate-cw",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "rotate, turn, orientation, landscape, portrait, upside down, sideways, 90, 180, clockwise, counterclockwise, fix orientation, reset rotation"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("pages.crop", PDFDocumentToolCategory::Pages, "page-size",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Crop Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Trim margins or set a crop area"),
        "crop",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "crop, trim, cut margins, crop box, remove white margins, clip, shrink margins, cut edges, visible area"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("pages.resize", PDFDocumentToolCategory::Pages, "page-size",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Resize Pages"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Change paper size, margins and scale"),
        "expand",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "resize, page size, paper size, scale, a4, a3, a5, letter, legal, margins, fit to page, enlarge, shrink, change format, page geometry, add margins"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("pages.boxes", PDFDocumentToolCategory::Pages, "page-size",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Set Page Boxes"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Media, crop, bleed, trim and art box"),
        "crop-handles",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "page boxes, media box, crop box, bleed box, trim box, art box, bleed, prepress, mediabox, cropbox, trimbox"),
        false, true, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("convert.images-to-pdf", PDFDocumentToolCategory::Convert, "create",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Images to PDF"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Turn photos and scans into a PDF"),
        "image",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "images to pdf, jpg to pdf, jpeg to pdf, png to pdf, tiff to pdf, bmp, photo to pdf, picture, convert image, create pdf, pdf from images, photos"),
        true, false, PDFDocumentToolLockedAccess::NoDocument),
    PDFDocumentToolCatalogEntry("convert.blank", PDFDocumentToolCategory::Convert, "create",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Blank PDF"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Start a new document with empty pages"),
        "file",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "blank pdf, new pdf, empty document, new document, create pdf, empty page, insert empty page, from scratch, white page, start new"),
        true, false, PDFDocumentToolLockedAccess::NoDocument),
    PDFDocumentToolCatalogEntry("convert.scan", PDFDocumentToolCategory::Convert, "create",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Scan to PDF"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Scan paper pages from a scanner"),
        "scanner",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "scan, scan pages, scanner, acquire, twain, wia, sane, paper, digitize, document feeder, adf, flatbed, scan to pdf"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("convert.ocr", PDFDocumentToolCategory::Convert, "recognize",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Recognize Text (OCR)"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Make scanned pages searchable"),
        "ocr",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "ocr, recognize text, text recognition, searchable pdf, scanned, make searchable, image to text, tesseract, read text from image, selectable text, text layer, digitize"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("convert.pdf-to-images", PDFDocumentToolCategory::Convert, "export",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "PDF to Images"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Save pages as PNG, JPEG or TIFF"),
        "download",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "pdf to images, pdf to jpg, pdf to jpeg, pdf to png, pdf to tiff, render, render to images, rasterize, export images, convert to image, picture, page as image, thumbnails, bitmap"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("convert.extract-text", PDFDocumentToolCategory::Convert, "export",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Extract Text"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Save the document text as a text file"),
        "file-text",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "extract text, pdf to text, pdf to txt, export text, copy all text, copy text, select all, fetch text, plain text, get text, text file, content"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("convert.extract-images", PDFDocumentToolCategory::Convert, "export",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Extract Images"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Save embedded pictures to a folder"),
        "images",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "extract images, extract image, fetch images, save pictures, export embedded images, photos, get images, pull images, image extraction, original images, figures"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("convert.audiobook", PDFDocumentToolCategory::Convert, "export",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Create Audio Book"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Turn the document text into MP3"),
        "headphones",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "audiobook, audio book, create audio book, create text stream, mp3, text to speech, tts, narrate, listen, voice, export audio, speech, read to me"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("edit.content", PDFDocumentToolCategory::Edit, "",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Edit Content"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Edit or add text, images and graphics"),
        "edit",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "edit pdf, edit page content, editor toolbox, edit text, change text, modify, fix typo, correct, replace image, move object, delete object, edit content, vector graphics, add text, type on pdf, write on pdf, add image, insert picture, place logo, svg"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("edit.page-numbers", PDFDocumentToolCategory::Edit, "",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Add Page Numbers"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Number pages in a chosen style"),
        "hash",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "page numbers, insert page numbers, numbering, number pages, pagination, page x of y, roman numerals, footer number, folio"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.password", PDFDocumentToolCategory::Protect, "encryption",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Protect with Password"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Require a password to open the PDF"),
        "lock",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "password, protect, lock, secure, encrypt, encryption, encryption settings, encrypt pdf, aes, aes-256, open password, user password, password protect"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.permissions", PDFDocumentToolCategory::Protect, "encryption",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Restrict Permissions"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Limit printing, copying and editing"),
        "shield-lock",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "permissions, restrict, encryption, encrypt pdf, prevent printing, prevent copying, disable copy, no editing, owner password, read only, lock editing, rights"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.certificate-encrypt", PDFDocumentToolCategory::Protect, "encryption",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Encrypt with Certificate"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Only the certificate holder can open it"),
        "certificate-lock",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "certificate encryption, encryption, encrypt pdf, public key, pki, recipient, private key, digital id, encrypt for, certificate security"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.unlock", PDFDocumentToolCategory::Protect, "encryption",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Remove Password"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Remove encryption you have access to"),
        "unlock",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "remove password, unlock, decrypt, encryption, remove security, remove restrictions, remove encryption, unprotect, open locked"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.sign", PDFDocumentToolCategory::Protect, "signatures",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Sign with Certificate"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Add a digital signature"),
        "sign-certificate",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "sign, sign document, sign digitally with certificate, signature, digital signature, certificate, pkcs12, pfx, p12, pades, digital id, electronic signature, esign, approve, qualified signature"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.timestamp", PDFDocumentToolCategory::Protect, "signatures",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Add Timestamp"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Prove the document existed at a time"),
        "timestamp",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "timestamp, time stamp, sign document, tsa, rfc 3161, trusted time, document timestamp, proof of existence, long term"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.sign-by-hand", PDFDocumentToolCategory::Protect, "signatures",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Sign by Hand"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Draw, type or insert a signature"),
        "signature",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "draw signature, sign electronically, signature creator, sign document, handwritten, sign by hand, fill and sign, fill & sign, initials, e-sign, electronic signature, autograph, signature image, tick, cross"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.certificates", PDFDocumentToolCategory::Protect, "signatures",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Manage Certificates"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Create and trust digital IDs"),
        "certificate",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "certificates, certificates manager, digital id, self-signed, create certificate, trusted certificates, keystore, pfx, p12, key, certificate store"),
        true, false, PDFDocumentToolLockedAccess::NoDocument),
    PDFDocumentToolCatalogEntry("protect.redact", PDFDocumentToolCategory::Protect, "privacy",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Redact Content"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Permanently remove sensitive content"),
        "redact",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "redact, create redacted document, redact rectangle, redact text, redact page, black out, blackout, censor, remove sensitive, hide text, gdpr, anonymize, obscure, cover up, personal data, erase text"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.sanitize", PDFDocumentToolCategory::Protect, "privacy",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Remove Hidden Data"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Strip metadata, comments and more"),
        "hidden-data",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "sanitize, sanitise, remove hidden data, remove hidden information, hidden data, remove metadata, strip metadata, clean, privacy, remove comments, remove author, scrub, remove ocr text, document info, exif"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("protect.remove-links", PDFDocumentToolCategory::Protect, "privacy",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Remove External Links"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Delete links that leave the document"),
        "link-off",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "remove links, external links, url, hyperlinks, web links, strip links, phishing, tracking links, disable links"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("optimize.compress", PDFDocumentToolCategory::Optimize, "file-size",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Compress"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Reduce the file size in one step"),
        "minimize-2",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "compress, compress pdf, reduce size, shrink, smaller, make smaller, downsize, optimize, minimize, file size, email size, mb, too large, lighten, reduce"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("optimize.images", PDFDocumentToolCategory::Optimize, "file-size",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Optimize Images"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Downsample and recompress images"),
        "optimize-images",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "optimize images, image optimization settings, downsample, resample, recompress, jpeg quality, dpi, image compression, jpeg2000, reduce image size, resolution, 150 dpi"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("optimize.structure", PDFDocumentToolCategory::Optimize, "file-size",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Optimize Structure"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Clean up and recompress PDF objects"),
        "optimize-structure",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "optimize structure, optimize, clean up, remove unused objects, merge identical objects, recompress, flate, garbage collect, lossless, object streams"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("optimize.bitonal", PDFDocumentToolCategory::Optimize, "color",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Convert to Black & White"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Make scans 1-bit and much smaller"),
        "droplet",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "black and white, bitonal, create bitonal document, bitonal images, monochrome, 1-bit, b&w, bw, fax, jbig2, ccitt, threshold, scan cleanup, two colors, lineart"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("optimize.grayscale", PDFDocumentToolCategory::Optimize, "color",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Convert Images to Grayscale"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Remove color from images"),
        "sun",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "grayscale, greyscale, convert to grayscale, gray, grey, remove color, desaturate, black and white photo, colorless, save ink"),
        false, false, PDFDocumentToolLockedAccess::Unavailable),
    PDFDocumentToolCatalogEntry("review.compare", PDFDocumentToolCategory::Review, "review",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Compare"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Find differences between two PDFs"),
        "compare",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "compare, compare pdfs, compare report, diff, differences, changes, versions, revision, side by side, overlay, what changed, redline, track changes, before after"),
        true, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.measure", PDFDocumentToolCategory::Review, "review",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Measure"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Distances, areas, perimeters, angles"),
        "ruler",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "measure, dimensions, distance, length, area, perimeter, angle, scale, ruler, dimension, calibrate, floor plan, drawing, takeoff, blueprint, cad"),
        false, false, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.output-preview", PDFDocumentToolCategory::Review, "print-production",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Output Preview"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Separations, overprint, color warnings"),
        "separations",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "output preview, separations, overprint, spot colors, cmyk, prepress, rich black, color warnings, plates, pantone, print preview, transparency"),
        false, true, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.ink-coverage", PDFDocumentToolCategory::Review, "print-production",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Ink Coverage"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Ink usage of every page"),
        "ink-coverage",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "ink coverage, ink usage, toner, tac, total area coverage, cmyk coverage, print cost, inks, ink consumption"),
        false, true, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.soft-proof", PDFDocumentToolCategory::Review, "print-production",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Soft Proofing"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Simulate print colors, check gamut"),
        "soft-proof",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "soft proof, soft proofing, gamut, gamut check, gamut checking, icc profile, color profile, cmyk simulation, proof colors, out of gamut, color management"),
        false, true, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.statistics", PDFDocumentToolCategory::Review, "developer",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Document Statistics"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "What takes up space in the file"),
        "bar-chart-2",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "statistics, object statistics, space usage, audit, what takes space, file size analysis, object count, size breakdown, why so big"),
        false, true, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.object-inspector", PDFDocumentToolCategory::Review, "developer",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Object Inspector"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Browse the internal PDF objects"),
        "object",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "object inspector, internal structure, objects, streams, dictionary, debug, pdf syntax, cos, raw, developer, low level, xref"),
        false, true, PDFDocumentToolLockedAccess::Available),
    PDFDocumentToolCatalogEntry("review.report", PDFDocumentToolCategory::Review, "developer",
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Document Report"),
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Structure, scripts, destinations as a report"),
        "report",
        //: Comma separated search keywords of the tool, lower case. They are never shown, English keywords are searched too.
        QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "report, document info, javascript, scripts, named destinations, structure tree, tags, tagged pdf, xmp, metadata, xml export, analyze, audit"),
        false, true, PDFDocumentToolLockedAccess::Available)
    };

    return tools;
}

std::vector<const PDFDocumentToolCatalogEntry*> PDFDocumentToolCatalog::getTools(PDFDocumentToolCategory category)
{
    std::vector<const PDFDocumentToolCatalogEntry*> result;
    for (const PDFDocumentToolCatalogEntry& tool : getTools())
    {
        if (tool.getCategory() == category)
        {
            result.push_back(&tool);
        }
    }
    return result;
}

int PDFDocumentToolCatalog::getToolCount()
{
    return int(getTools().size());
}

const PDFDocumentToolCatalogEntry* PDFDocumentToolCatalog::findTool(QStringView id)
{
    const int index = getToolIndex(id);
    return index >= 0 ? &getTools()[size_t(index)] : nullptr;
}

bool PDFDocumentToolCatalog::isValidToolId(QStringView id)
{
    return getToolIndex(id) >= 0;
}

int PDFDocumentToolCatalog::getToolIndex(QStringView id)
{
    if (id.isEmpty() || id.size() > MAXIMUM_ID_LENGTH)
    {
        return -1;
    }

    const std::vector<PDFDocumentToolCatalogEntry>& tools = getTools();
    for (size_t i = 0; i < tools.size(); ++i)
    {
        if (id == QLatin1StringView(tools[i].m_id))
        {
            return int(i);
        }
    }

    return -1;
}

const std::vector<PDFDocumentToolCategoryInfo>& PDFDocumentToolCatalog::getCategories()
{
    // Titles, descriptions, icons and hues of the headers of the categories of
    // Figma 272:12821, chips from tools.md 3
    static const std::vector<PDFDocumentToolCategoryInfo> categories =
    {
        PDFDocumentToolCategoryInfo(PDFDocumentToolCategory::Pages, "pages",
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Organize Pages"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Pages"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Merge, split, reorder and resize pages"),
                                    "pages", PDFDocumentToolHue::Amber),
        PDFDocumentToolCategoryInfo(PDFDocumentToolCategory::Convert, "convert",
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Create & Convert"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Convert"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Create PDFs and get content out of them"),
                                    "repeat", PDFDocumentToolHue::Blue),
        PDFDocumentToolCategoryInfo(PDFDocumentToolCategory::Edit, "edit",
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Edit"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Edit"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Change the content of pages"),
                                    "edit", PDFDocumentToolHue::Green),
        PDFDocumentToolCategoryInfo(PDFDocumentToolCategory::Protect, "protect",
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Protect & Sign"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Protect & Sign"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Protect, sign and remove sensitive data"),
                                    "shield", PDFDocumentToolHue::Red),
        PDFDocumentToolCategoryInfo(PDFDocumentToolCategory::Optimize, "optimize",
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Optimize"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Optimize"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Make files smaller, cleaner and healthier"),
                                    "minimize-2", PDFDocumentToolHue::Violet),
        PDFDocumentToolCategoryInfo(PDFDocumentToolCategory::Review, "review",
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Review & Inspect"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Review"),
                                    QT_TRANSLATE_NOOP("pdf::PDFDocumentToolCatalog", "Compare, measure, proof and look inside"),
                                    "columns", PDFDocumentToolHue::Teal),
    };

    return categories;
}

const PDFDocumentToolCategoryInfo* PDFDocumentToolCatalog::getCategoryInfo(PDFDocumentToolCategory category)
{
    for (const PDFDocumentToolCategoryInfo& info : getCategories())
    {
        if (info.getCategory() == category)
        {
            return &info;
        }
    }
    return nullptr;
}

std::optional<PDFDocumentToolCategory> PDFDocumentToolCatalog::findCategory(QStringView id)
{
    if (id.isEmpty() || id.size() > MAXIMUM_ID_LENGTH)
    {
        return std::nullopt;
    }

    for (const PDFDocumentToolCategoryInfo& info : getCategories())
    {
        if (id == QLatin1StringView(info.m_id))
        {
            return info.getCategory();
        }
    }
    return std::nullopt;
}

QString PDFDocumentToolCatalog::getCategoryId(PDFDocumentToolCategory category)
{
    const PDFDocumentToolCategoryInfo* info = getCategoryInfo(category);
    return info ? info->getId() : QString();
}

std::vector<const PDFDocumentToolSubgroupInfo*> PDFDocumentToolCatalog::getSubgroups(PDFDocumentToolCategory category)
{
    std::vector<const PDFDocumentToolSubgroupInfo*> result;
    for (const PDFDocumentToolSubgroupInfo& subgroup : PDFDocumentToolCatalogHelper::getSubgroups())
    {
        if (subgroup.getCategory() == category)
        {
            result.push_back(&subgroup);
        }
    }
    return result;
}

const PDFDocumentToolSubgroupInfo* PDFDocumentToolCatalog::findSubgroup(PDFDocumentToolCategory category, const char* id)
{
    if (!id || !*id)
    {
        return nullptr;
    }

    for (const PDFDocumentToolSubgroupInfo& subgroup : PDFDocumentToolCatalogHelper::getSubgroups())
    {
        if (subgroup.getCategory() == category && qstrcmp(subgroup.m_id, id) == 0)
        {
            return &subgroup;
        }
    }
    return nullptr;
}

QString PDFDocumentToolCatalog::getHueName(PDFDocumentToolHue hue)
{
    switch (hue)
    {
        case PDFDocumentToolHue::Amber:
            return QStringLiteral("Amber");
        case PDFDocumentToolHue::Blue:
            return QStringLiteral("Blue");
        case PDFDocumentToolHue::Green:
            return QStringLiteral("Green");
        case PDFDocumentToolHue::Red:
            return QStringLiteral("Red");
        case PDFDocumentToolHue::Violet:
            return QStringLiteral("Violet");
        case PDFDocumentToolHue::Teal:
            return QStringLiteral("Teal");
    }

    return QString();
}

QStringList PDFDocumentToolCatalog::splitKeywords(const QString& keywords)
{
    QStringList result;
    for (const QString& keyword : keywords.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString trimmed = keyword.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed))
        {
            result << trimmed;
        }
    }
    return result;
}

}   // namespace pdf
