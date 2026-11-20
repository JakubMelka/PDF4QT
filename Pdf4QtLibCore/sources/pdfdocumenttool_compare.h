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

#ifndef PDFDOCUMENTTOOL_COMPARE_H
#define PDFDOCUMENTTOOL_COMPARE_H

#include "pdfdiff.h"
#include "pdfdiffreport.h"
#include "pdfdocumenttool.h"

#include <QMutex>

#include <memory>

namespace pdf
{

/// The whole result of a run of Compare for the GUI (plan of step 7 of phase 2, 4.9): the data of the result
/// hold at most MAXIMUM_DIFFERENCES differences (MCP), the view of the tab shows every one. The run stores its
/// PDFDiffResult here at its end (one thread); the GUI takes it after the run (its job and its result are
/// finished then - the runner of the application hands the result over by its queued signal; no signal, no
/// callback of the run). Thread-safe (a mutex). Ownership: the GUI creates it for each run, its run tool
/// (PDFAppToolRunEnvironment::runTool, a subclass of PDFDocumentToolCompare of the GUI as the run tool of Ink
/// Coverage) holds it by a std::shared_ptr until the job ends.
class PDF4QTLIBCORESHARED_EXPORT PDFDiffResultCollector
{
public:
    PDFDiffResultCollector() = default;

    PDFDiffResultCollector(const PDFDiffResultCollector&) = delete;
    PDFDiffResultCollector& operator=(const PDFDiffResultCollector&) = delete;

    /// The run stores its whole result (not filtered by the parameters include*)
    void setResult(PDFDiffResult result);

    /// The stored result, std::nullopt before the run ended (or after a failed run)
    std::optional<PDFDiffResult> getResult() const;

private:
    mutable QMutex m_mutex;
    std::optional<PDFDiffResult> m_result;
};

/// The tool Compare (review.compare, Figma 492:17871 …; plan of step 7 of phase 2, 4.9 with R7-6, R7-9, R7-15,
/// owner answers Q7.2, Q7.13, Q7.17): the differences of two documents - the original (the first input) and the
/// revised (the second input), exactly two inputs (PDFDocumentToolCatalogEntry::getInputCountRange {2, 2}). The
/// GUI shows them in its own tab (PDFAppToolGUICompare, PDFAppCompareView); MCP and PdfTool diff run it.
///
/// Parameters (the names are PARAMETER_*):
///   - "originalPages", "revisedPages" (PageSelection; the pages of each document - the field "1-24 of 24" of the
///     panel Documents, K7.3: the parser and the texts of Page selection);
///   - "compareTextAsVector" (Bool, default false): the texts are compared as vector graphics;
///   - "compareCharacters" (Bool, default false): the texts are compared by characters, not by words;
///   - "includeText", "includeVectorGraphics", "includeImages", "includeShading", "includePageMovement" (Bool,
///     default true): the five filters of the menu Filter (PDFDiffResult::filter) - the data, the XML and the
///     report hold the shown differences, the counts of every kind are always complete;
///   - "output" (Enum: summary, xml, report; default summary): summary = Analysis (the data), xml = Analysis
///     with the XML of the differences as its one output (PDFDiffResult::saveToXML with the rectangles),
///     report = NewDocument (the report PDF of PDFDiffReportBuilder::createReport - a new document of the
///     inputs, FileOnly: its file, Open the report in a new tab; 492:107143);
///   - "view" (Enum: side-by-side, original, revised, overlay; default side-by-side): the pages of the report
///     (output report only; the menu View).
///
/// Data of the result (DATA_*):
///   - "isSame" (no difference at all; a stopped comparison is the result Stopped, never "no differences" -
///     B173);
///   - "counts": { "total", "shown", "hidden" and per category CATEGORY_* and per change CHANGE_* } of the whole
///     comparison ("12 shown · 2 hidden", the counts of the menu Filter);
///   - "differences": [{ "index" (from 0 of the whole result), "kind" (getKindId), "category" (CATEGORY_*),
///     "change" (CHANGE_*), "originalPage", "revisedPage" (from 1, null = none), "description" (the translated
///     text of PDFDiffResult::getMessage without the text of the document), "originalText", "revisedText" (the
///     texts of a text difference - only when every input permits bit 5, see below), "originalRects",
///     "revisedRects" ([[x, y, width, height]] in points of their pages), "originalRectPages",
///     "revisedRectPages" (the page of each rectangle from 1: a text difference may go over the end of
///     a page), "isTextTooLarge" (true only for the text of a pair of pages over the budget, B191) }] - the
///     shown differences, at most MAXIMUM_DIFFERENCES, then "truncated" true;
///   - "pageSequence": [{ "originalPage", "revisedPage" }] (from 1, null = an added / removed page);
///   - "originalPageCount", "revisedPageCount" (the compared pages);
///   - "isTextOmitted": the texts of the differences were left out (an input without bit 5).
/// A comparison over the budget of the text (B191, R7-6) adds a fact "Text of pages N–M differs (too large to
/// compare word by word)" and a difference of the whole range.
///
/// Permissions (owner answer Q7.2 with R7-22 (6), Table 22): comparing the documents on the screen is reading
/// them; the text of the differences taken out of a document is an extraction - "output" xml needs bit 5
/// (CopyContent) of every input (getRequiredPermissions; the owner password offered, FC-6), the summary leaves the
/// texts out of an input without bit 5 ("isTextOmitted"); the report PDF is a new document of the inputs, not
/// encrypted: the rule of Merge (isNewDocumentOfInputs, PDFDocumentMerger::getPermission of every input in
/// validate - a restricted input needs its owner password, B161 / B210).
///
/// Hostile input (plan 6, 6.2): two untrusted documents never crash, hang or exhaust the memory - the page ranges
/// are checked before PDFClosedIntervalSet::unfold (B174, B192), a document without pages is an error (2.10), the
/// text comparison has its budget (B191, R7-6), PDFOperationControl in every long loop (B193).
///
/// Package P5 implements it; the skeleton has the parameters, the kinds, the permissions and a stub run (Failed).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolCompare : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolCompare)

public:
    static constexpr const char* TOOL_ID = "review.compare";

    /// Names of the parameters
    static constexpr const char* PARAMETER_ORIGINAL_PAGES = "originalPages";
    static constexpr const char* PARAMETER_REVISED_PAGES = "revisedPages";
    static constexpr const char* PARAMETER_COMPARE_TEXT_AS_VECTOR = "compareTextAsVector";
    static constexpr const char* PARAMETER_COMPARE_CHARACTERS = "compareCharacters";
    static constexpr const char* PARAMETER_INCLUDE_TEXT = "includeText";
    static constexpr const char* PARAMETER_INCLUDE_VECTOR_GRAPHICS = "includeVectorGraphics";
    static constexpr const char* PARAMETER_INCLUDE_IMAGES = "includeImages";
    static constexpr const char* PARAMETER_INCLUDE_SHADING = "includeShading";
    static constexpr const char* PARAMETER_INCLUDE_PAGE_MOVEMENT = "includePageMovement";
    static constexpr const char* PARAMETER_OUTPUT = "output";
    static constexpr const char* PARAMETER_VIEW = "view";

    /// Identifiers of the outputs
    static constexpr const char* OUTPUT_SUMMARY = "summary";
    static constexpr const char* OUTPUT_XML = "xml";
    static constexpr const char* OUTPUT_REPORT = "report";

    /// Identifiers of the views (PDFDiffReportView)
    static constexpr const char* VIEW_SIDE_BY_SIDE = "side-by-side";
    static constexpr const char* VIEW_ORIGINAL = "original";
    static constexpr const char* VIEW_REVISED = "revised";
    static constexpr const char* VIEW_OVERLAY = "overlay";

    /// Categories of the differences (the five filters)
    static constexpr const char* CATEGORY_TEXT = "text";
    static constexpr const char* CATEGORY_VECTOR_GRAPHICS = "vectorGraphics";
    static constexpr const char* CATEGORY_IMAGES = "images";
    static constexpr const char* CATEGORY_SHADING = "shading";
    static constexpr const char* CATEGORY_PAGE_MOVEMENT = "pageMovement";

    /// Changes of the differences (the signs − + ≠ ↔ of the list, 492:19314)
    static constexpr const char* CHANGE_REMOVED = "removed";
    static constexpr const char* CHANGE_ADDED = "added";
    static constexpr const char* CHANGE_REPLACED = "replaced";
    static constexpr const char* CHANGE_MOVED = "moved";

    /// Keys of the data of the result
    static constexpr const char* DATA_IS_SAME = "isSame";
    static constexpr const char* DATA_COUNTS = "counts";
    static constexpr const char* DATA_DIFFERENCES = "differences";
    static constexpr const char* DATA_TRUNCATED = "truncated";
    static constexpr const char* DATA_PAGE_SEQUENCE = "pageSequence";
    static constexpr const char* DATA_ORIGINAL_PAGE_COUNT = "originalPageCount";
    static constexpr const char* DATA_REVISED_PAGE_COUNT = "revisedPageCount";
    static constexpr const char* DATA_IS_TEXT_OMITTED = "isTextOmitted";

    /// Most differences in the data (plan 4.9, 7.1)
    static constexpr int MAXIMUM_DIFFERENCES = 1000;

    /// The id of a type of a difference ("page-moved", "text-replaced" …; stable, the values of "kind") and its
    /// category and change (CATEGORY_*, CHANGE_*)
    static QString getKindId(PDFDiffResult::Type type);
    static QString getCategoryId(PDFDiffResult::Type type);
    static QString getChangeId(PDFDiffResult::Type type);

    /// The translated title of the kind of a difference of the data ("Text replaced"; empty for an
    /// unknown id) - PdfTool diff lists the data by it
    static QString getKindDescription(const QString& kindId);

    /// The view of the id of the parameter view (side-by-side for an unknown id)
    static PDFDiffReportView getView(const QString& id);

    /// The options of PDFDiff::compare of the normalized parameters
    static PDFDiff::Options getOptions(const QVariantMap& parameters);

    /// The result filtered by the parameters include*
    static PDFDiffResult filter(const PDFDiffResult& result, const QVariantMap& parameters);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;

    /// output report = NewDocument (FileOnly, Save as a file preselected, no impact on the open documents);
    /// summary, xml = Analysis
    virtual PDFDocumentToolResultKind getResultKind(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolPreselectedResult getPreselectedResult(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolSignatureImpact getSignatureImpact(const QVariantMap& parameters) const override;

    /// output report: the rule of Merge (the report is not encrypted)
    virtual bool isNewDocumentOfInputs(const QVariantMap& parameters) const override;

    /// output xml: bit 5 (CopyContent) - the texts of the differences are extracted; summary and report none
    /// (the report is checked by the rule of Merge)
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const override;

    /// The base checks, and for output report the rule of Merge of every input (PDFDocumentMerger::getPermission)
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;

    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The run of execute with a collector of the whole result (the GUI runs it by its run tool; execute passes
    /// nullptr). The two inputs are compared by PDFDiff::compare in this thread (no nested job, R7-15).
    /// \param context The context (two documents: the original, the revised)
    /// \param parameters The normalized parameters
    /// \param control Cancellation (optional)
    /// \param progress Progress (optional)
    /// \param collector The whole result (optional)
    static PDFDocumentToolResult compare(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress, PDFDiffResultCollector* collector);

    /// The data of a result (see the class): the shown differences of \p result filtered by \p parameters, at
    /// most \p maximumDifferences; the texts only with \p isTextIncluded
    static QJsonObject createData(const PDFDiffResult& result, const QVariantMap& parameters, int maximumDifferences, bool isTextIncluded);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_COMPARE_H
