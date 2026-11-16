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

#ifndef PDFDOCUMENTTOOL_REDACT_H
#define PDFDOCUMENTTOOL_REDACT_H

#include "pdfdocumenttool.h"

#include <QPainterPath>

namespace pdf
{

class PDFDocumentTextIndex;
class PDFDocumentTextSearchMatch;
class PDFDocumentTextSearchOptions;
class PDFDocumentTextSearchResult;

/// Redact Content (protect.redact; plan of step 6 of phase 2, 4.3, 5.4, 6.8 with R6-5 ... R6-7, R6-9,
/// R6-14 ... R6-19, R6-24, R6-30; package P3 implements the run; PdfTool redact calls it - one
/// implementation). Every page is rebuilt (owner answer Q6.8 a): the page is compiled, the areas are
/// subtracted from the paths, the clips, the images (their pixels filled - B138), the shadings and the
/// meshes (B140, R6-18), the content outside the crop box is removed (Q6.11); text becomes curves.
/// Annotations, forms, the structure and the hidden data are not carried over. The new document is
/// written whole (no encryption, no earlier revision) and checked by PDFRedactionVerifier; a finding
/// means the copy is not created (Failed, owner answer Q6.9).
///
/// A new document of the pages of its input (the catalog isNewDocumentOfInputs, B139): a restricted
/// document needs its owner (the rule of Merge, PDFDocumentMerger::getRequiredPermissions; validate checks
/// it in the core too - R6-9, PdfTool and MCP). The layers: the states of the tab in the GUI, OCUsage::Export
/// for MCP and PdfTool (PDFDocumentToolRenderContext::layerStates, R6-15).
///
/// Parameters (the names are PARAMETER_*):
///   - useMarks (Bool, default true): the Redact annotations of the document are the areas (their
///     quadrilaterals united with Qt::WindingFill - R6-7, else /Rect);
///   - areas (String, the JSON [{ "page": n from 1, "rect": [x, y, width, height] }] in the user space of
///     the page; MCP; sensitive - R6-19);
///   - search (String, at most MAXIMUM_QUERY_LENGTH; MCP; sensitive - R6-19) with searchRegex,
///     searchCaseSensitive, searchWholeWords, searchAcrossLineBreaks (Bool, default false; K6.21: a line
///     end counts as a space): every match is an area (at most MAXIMUM_SEARCH_RESULT_COUNT);
///   - pages (PageSelection, default no page; MCP): whole pages - a blank page of the same size (R6-30);
///   - fillColor (Color, default #000000), noFill (Bool, default false): the fill of the areas (No fill =
///     the area stays transparent, Table 195 without /IC);
///   - keepTitle, keepMetadata, keepOutline (Bool, defaults true, true, false - 437:12794): /Info of direct
///     values of the standard keys only (R6-6); the title only with keepTitle.
/// At least one area of any source is needed (an error otherwise).
///
/// Result: a New document with writtenData = the verified bytes and the suggested name
/// "<name>_REDACTED.pdf"; the facts FACT_* (437:13887) and the data DATA_*. Never a redacted text, a
/// search term or an area in a fact, a message or the data.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolRedact : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolRedact)

public:
    static constexpr const char* TOOL_ID = "protect.redact";

    /// Names of the parameters
    static constexpr const char* PARAMETER_USE_MARKS = "useMarks";
    static constexpr const char* PARAMETER_AREAS = "areas";
    static constexpr const char* PARAMETER_SEARCH = "search";
    static constexpr const char* PARAMETER_SEARCH_REGEX = "searchRegex";
    static constexpr const char* PARAMETER_SEARCH_CASE_SENSITIVE = "searchCaseSensitive";
    static constexpr const char* PARAMETER_SEARCH_WHOLE_WORDS = "searchWholeWords";
    static constexpr const char* PARAMETER_SEARCH_ACROSS_LINE_BREAKS = "searchAcrossLineBreaks";
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_FILL_COLOR = "fillColor";
    static constexpr const char* PARAMETER_NO_FILL = "noFill";
    static constexpr const char* PARAMETER_KEEP_TITLE = "keepTitle";
    static constexpr const char* PARAMETER_KEEP_METADATA = "keepMetadata";
    static constexpr const char* PARAMETER_KEEP_OUTLINE = "keepOutline";

    /// Limits (plan 7.2)
    static constexpr int MAXIMUM_MARK_COUNT = 10000;                ///< Redact annotations of a document
    static constexpr int MAXIMUM_QUADRILATERAL_COUNT = 1000;        ///< Quadrilaterals of one annotation
    static constexpr int MAXIMUM_AREA_COUNT = 10000;                ///< Areas of the parameter areas
    static constexpr int MAXIMUM_SEARCH_RESULT_COUNT = 10000;       ///< Matches of the search
    static constexpr int MAXIMUM_QUERY_LENGTH = 1024;               ///< Characters of the search term
    static constexpr int MAXIMUM_AREA_TEXT_LENGTH = 200;            ///< Characters of one area in the JSON: its keys, four numbers of full precision (17 digits, a sign, an exponent), white space

    /// Characters of the JSON of areas: fits MAXIMUM_AREA_COUNT areas (ASan D1 of step 6 - the areas of every page
    /// of a large document were refused by the length before their count was checked); a structured String
    /// (PDFDocumentToolParameterDefinition::setStructuredData), so MCP stays bounded
    static constexpr int MAXIMUM_AREAS_LENGTH = MAXIMUM_AREA_COUNT * MAXIMUM_AREA_TEXT_LENGTH;
    static_assert(MAXIMUM_AREAS_LENGTH <= PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH);

    /// Names of the data of the result (MCP)
    static constexpr const char* DATA_AREA_COUNT = "areaCount";     ///< The redacted areas
    static constexpr const char* DATA_PAGE_COUNT = "pageCount";     ///< The pages with an area
    static constexpr const char* DATA_VERIFIED = "verified";        ///< The check after the write passed (always true of a Success)
    static constexpr const char* DATA_WARNINGS = "warnings";        ///< The texts of the warnings (Q6.10, Q6.19), never a redacted word

    /// Names of the facts of the result dialog (437:13887)
    static constexpr const char* FACT_REMOVED = "removed";          ///< "12 areas on 5 pages"
    static constexpr const char* FACT_FILL = "fill";                ///< "Black", "No fill"
    static constexpr const char* FACT_KEPT = "kept";                ///< "Title, metadata"
    static constexpr const char* FACT_SIGNATURES = "signatures";    ///< "Not carried over"
    static constexpr const char* FACT_CONFORMANCE = "conformance";  ///< "PDF/A and PDF/UA are not declared by the copy"
    static constexpr const char* FACT_LAYERS = "layers";            ///< "Hidden layers were not carried over" (R6-15)
    static constexpr const char* FACT_PAGE_PROBLEMS = "pageProblems"; ///< "Page 12: some content could not be drawn" (B143)

    PDFDocumentToolRedact() = default;

    /// The search of the redaction (the parameter search; the search panel of the GUI uses it too):
    /// PDFDocumentTextSearch::find with at most MAXIMUM_SEARCH_RESULT_COUNT matches and the option
    /// Across line breaks (K6.21): without it a match, whose characters do not all lie on one line (one
    /// page, block and line; the index joins the lines of a block by a space), is not a match; with it a
    /// plain term is searched as createLineBreakPattern (a user's regular expression as it is). The soft
    /// hyphens are always ignored.
    /// \param index The text index of the document
    /// \param query The searched text (or a regular expression by \p options)
    /// \param options Case, whole words, regular expression
    /// \param isAcrossLineBreaks A match may continue on the next line
    /// \param control Cancellation (may be null)
    static PDFDocumentTextSearchResult findMatches(const PDFDocumentTextIndex& index, const QString& query, const PDFDocumentTextSearchOptions& options,
                                                   bool isAcrossLineBreaks, const PDFOperationControl* control);

    /// The regular expression of a plain term searched across line breaks (K6.21): each run of white
    /// space of the term becomes \s+, the rest is escaped ("Jane  Novak" -> "Jane\s+Novak")
    static QString createLineBreakPattern(const QString& query);

    /// The area of a match: its quadrilaterals in the default user space of its page
    static QPainterPath getMatchArea(const PDFDocumentTextSearchMatch& match);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_REDACT_H
