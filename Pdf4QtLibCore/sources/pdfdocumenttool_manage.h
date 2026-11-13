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

#ifndef PDFDOCUMENTTOOL_MANAGE_H
#define PDFDOCUMENTTOOL_MANAGE_H

#include "pdfdocumenttool.h"
#include "pdfdocumentassembly.h"
#include "pdfdocumenttool_split.h"

#include <QCoreApplication>
#include <QDate>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace pdf
{

/// The tool Manage Pages (pages.manage; plan of step 3b of phase 2, 4.4, 4.5, 7, RB3 a, RB9,
/// QB1, QB4, QB7; package M1, the output This document by PDFDocumentMerger::arrange of M2).
/// The skeleton declares it and defines the values of its outputs (RB3 a); package M1 defines
/// the parameters, the validation and the run.
///
/// Parameters (plan 9.3, fixed):
///  - "inputs" (panel only) - the order of the inputs of the context by their ids, as Merge;
///  - "assembly" (PDFDocumentToolParameterType::Value of PDFDocumentAssembly, panel only, never
///    stored; RB7) - the assembly of the panels;
///  - "output" - OUTPUT_DOCUMENT (This document: the tab of a document only, QB7 b - the change of
///    the document of input 0 by arrange, kind ChangeDocument), OUTPUT_ONE_PDF (NewDocument),
///    OUTPUT_PER_GROUP and OUTPUT_PER_PAGE (Files written by the file sink of the context);
///  - "outline" (document-parts, join, none - QB3), "optimizeImages" (off, quality, size), and the
///    output files of Split (PDFDocumentToolFileOutput: folder, fileName, existingFiles);
///  - MCP only (never of the panels): "assemblyPath" (the application reads the file and gives the
///    assembly, Q9 b) and "pages" (with documentId: an ordered PDFPageRangeList naming every page of
///    the document at least once - a repetition is a clone, a missing page is an error listing the
///    missing pages, RB9 / QB4; removal is only pages.remove).
///
/// Values of the outputs (RB3 a, the catalog row is This document): getResultKind,
/// getPreselectedResult (Keep / Open in a new tab / Save as a file), getSignatureImpact
/// (IncrementalSave only for This document) and isNewDocumentOfInputs (every output except This
/// document: the new documents are not encrypted, the rule Q2 of step 2).
///
/// No preview (RB3 g: the canvas draws the model, a run of arrange over 10 000 pages for a preview
/// would be waste). Reentrant as every tool.
///
/// The run (package M1): the sources of the assembly are the inputs of the context by their ids
/// (PDFDocumentAssembly, contract; the target is the first document); a context document without
/// an id (the MCP bridge of assemblyPath) is the PDF source of its index among the PDF sources of
/// the assembly. "inputs" is only the order of the panel and is not read. One PDF = one merge of the plan (PDFDocumentAssembly::toPlan) with
/// its parts (QB3: PDFDocumentMerger::Settings::parts); per group and per page = the same merge,
/// then a file of each item or page by PDFDocumentToolFileOutput::writeFiles (plan 1 point 3: the
/// splitter keeps no order, the pages of a file are ascending in the merged document); This
/// document = PDFDocumentMerger::arrange with the page objects of the version of the assembly
/// (RB2), a certified document loses its certification (Q12, as Rotate). Facts of This document:
/// "pages", "order", "rotated", "cropped", "cloned", "inserted", "removed", "tags" (removed with
/// removed pages / "The reading order of the tags is not changed", QB6), "clonedTags" ("Cloned
/// pages are not tagged", RB13), "signedRevisions" (RB5 1), "removedSignature" (RB5 5),
/// "usageRights" (/Perms /UR3), "conformance" (Q18), then the facts of the arrangement:
/// "documentParts" ("Document parts removed": the document had parts and the result has none -
/// another order, or a damaged hierarchy; with the order kept removePages repairs them, 14.12),
/// "signatureFields" ("Signature fields are not cloned", RB1), "lockedFields" ("Fields locked by a
/// signature are not cloned", 12.7.5.5, 12.8.2.4), "fieldsRenamed", "signatures", "certification";
/// nothing changed = NothingToDo. The crop of the assembly is in points, the run divides it by
/// /UserUnit of its page (Table 31) for the merger.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolManage : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolManage)

public:
    static constexpr const char* TOOL_ID = "pages.manage";

    /// Names of the parameters (plan 9.3)
    static constexpr const char* PARAMETER_INPUTS = "inputs";
    static constexpr const char* PARAMETER_ASSEMBLY = "assembly";
    static constexpr const char* PARAMETER_OUTPUT = "output";
    static constexpr const char* PARAMETER_OUTLINE = "outline";
    static constexpr const char* PARAMETER_OPTIMIZE_IMAGES = "optimizeImages";
    static constexpr const char* PARAMETER_ASSEMBLY_PATH = "assemblyPath";
    static constexpr const char* PARAMETER_PAGES = "pages";

    /// Identifiers of the outputs (396:26763; This document of the document host)
    static constexpr const char* OUTPUT_DOCUMENT = "document";
    static constexpr const char* OUTPUT_ONE_PDF = "one-pdf";
    static constexpr const char* OUTPUT_PER_GROUP = "per-group";
    static constexpr const char* OUTPUT_PER_PAGE = "per-page";

    /// Identifiers of the outline (396:26763, QB3)
    static constexpr const char* OUTLINE_DOCUMENT_PARTS = "document-parts";
    static constexpr const char* OUTLINE_JOIN = "join";
    static constexpr const char* OUTLINE_NONE = "none";

    /// Identifiers of the images (396:26763)
    static constexpr const char* IMAGES_OFF = "off";
    static constexpr const char* IMAGES_QUALITY = "quality";
    static constexpr const char* IMAGES_SIZE = "size";

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;

    /// By "output" (RB3 a): This document ChangeDocument, One PDF NewDocument, per group and per page Files
    virtual PDFDocumentToolResultKind getResultKind(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolPreselectedResult getPreselectedResult(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolSignatureImpact getSignatureImpact(const QVariantMap& parameters) const override;
    virtual bool isNewDocumentOfInputs(const QVariantMap& parameters) const override;

    /// This document changes the pages of the open document (a certified one loses its
    /// certification); the new documents of the other outputs leave it as it is
    virtual bool isPageChange(const QVariantMap& parameters) const override;

    /// No preview (RB3 g)
    virtual bool hasPreview() const override { return false; }

    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// MCP: "pages" belongs to This document, "assemblyPath" to the new documents; the panel
    /// parameters are refused ("inputs" always, "assembly" unless "assemblyPath" is given - the
    /// bridge hands over the assembly it has read of the file)
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;

    /// MCP document mode (QB4, RB9): "pages" of the document of the context becomes the assembly
    /// (createAssemblyOfPages) with the output This document; nothing is added for invalid pages
    /// (validate names the error)
    virtual QVariantMap completeGivenParameters(const PDFDocumentToolContext& context, const QVariantMap& given) const override;

    /// The output of the parameters (OUTPUT_DOCUMENT, when it is missing - the catalog row)
    static QString getOutput(const QVariantMap& parameters);

    /// The assembly of the parameters (an empty one, when it is missing or of another type)
    static PDFDocumentAssembly getAssembly(const QVariantMap& parameters);

    /// The assembly of the pages of a document in a new order (QB4, RB9): an ordered
    /// PDFPageRangeList ("3, 1-2, 5-"), which names every page at least once - a further occurrence
    /// is a clone (RB4: the first occurrence is the original); std::nullopt and the reason (the
    /// missing pages listed; removal is pages.remove) for an invalid text
    static std::optional<PDFDocumentAssembly> createAssemblyOfPages(const PDFDocument* document, const QString& name, const QString& pages, QString* errorMessage);

    /// The files of the outputs per group (a file per item) and per page, with their names by the
    /// template of "fileName" (the panel Output files shows them, 396:26763): "#" the number of the
    /// file, "@" the position of its first page, "%" and {source_*} its first source, {group_name}
    /// the name of the item (PDFDocumentAssembly::getItemName), {group_index} the number of the item
    /// (B26). The pages of a file are its positions in the result (from 0). false and the reason
    /// (383:9088) for an invalid template, equal names or more than 10 000 files.
    static bool createFiles(const PDFDocumentAssembly& assembly, const QVariantMap& parameters, const QDate& date, std::vector<PDFDocumentToolFileOutput::File>* files, QString* errorMessage);

    /// Longest text of "pages"
    static constexpr int MAXIMUM_PAGES_LENGTH = 65536;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_MANAGE_H
