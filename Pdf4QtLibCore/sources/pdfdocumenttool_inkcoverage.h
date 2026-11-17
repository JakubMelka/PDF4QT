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

#ifndef PDFDOCUMENTTOOL_INKCOVERAGE_H
#define PDFDOCUMENTTOOL_INKCOVERAGE_H

#include "pdfdocumenttool.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdfoutputpreviewanalyzer.h"

#include <QJsonArray>
#include <QMutex>

namespace pdf
{

/// The pages of a run of Ink Coverage, which are finished, for a reader in another thread (plan of
/// step 7 of phase 2, 4.5: the rows of the table appear during the run, 478:11954; after Stop the
/// finished pages stay, 478:14194). The run adds a row of each measured page (the object of
/// DATA_PAGES, see PDFDocumentToolInkCoverage) from the threads of its pages; the GUI polls
/// getPages by its timer - no signal, no callback (CLAUDE.md, PDFProgressSink). Thread-safe (a
/// mutex). Ownership: the caller of PDFDocumentToolInkCoverage::measure owns it and keeps it alive
/// until measure has returned.
class PDF4QTLIBCORESHARED_EXPORT PDFInkCoverageCollector
{
public:
    PDFInkCoverageCollector() = default;

    PDFInkCoverageCollector(const PDFInkCoverageCollector&) = delete;
    PDFInkCoverageCollector& operator=(const PDFInkCoverageCollector&) = delete;

    /// The run starts: the inks (the object of DATA_INKS; the indices of the arrays of a row)
    void begin(QJsonArray inks);

    /// A page is measured (a row of DATA_PAGES); any thread
    void addPage(QJsonObject page);

    /// The inks of the run (empty before begin)
    QJsonArray getInks() const;

    /// The rows of the measured pages in the order of the pages (a snapshot)
    QJsonArray getPages() const;

    /// Number of the measured pages
    int getPageCount() const;

private:
    mutable QMutex m_mutex;
    QJsonArray m_inks;
    std::vector<QJsonObject> m_pages;
};

/// The tool Ink Coverage (review.ink-coverage, Figma 478:11760 …; plan of step 7 of phase 2, 4.5): the area
/// of every ink on the selected pages - measured in the process color space of the output (CMYK, the output
/// intent of the document by the settings; not in the space of the group of the page, B196), the spot colors
/// of every resource (PDFResourceWalk, B197), /None no ink (B198), the settings and the cancellation of the
/// calculator (B199). Pages in parallel (PDFExecutionPolicy) within the image budget of the raster pool; the
/// page as printing (owner answer Q7.8 a: the annotations with the Print flag, the layers by /Print); a page
/// with an error of the rendering is measured with a warning, only a page, which cannot be rendered, is left
/// out (Q7.9 b). The ratio is the ink inside of the box over the area of the box - TrimBox, else CropBox,
/// clipped by MediaBox, numerator and denominator of the same box (Q7.6 b, R7-8); the area in mm² is weighted
/// by the tint (K7.15) for the whole page. The total counts each page once (B163).
///
/// Parameters: "pages" (PageSelection), "resolution" (pixels of the longer side, 72 to
/// PDFOutputPreviewSettings::MAXIMUM_RESOLUTION, default 1920; R7-22 (5)), "format" (none, csv, text, html,
/// xml; default none).
///
/// Data: "pages" (rows: "page" from 1, per ink "area" mm² and "ratio" %), "inks" (name, isSpot), "total" per
/// ink, "peak" (page, %), "box" ("trim", "crop"), "spotsOverLimit", "renderingIssues" (page, message),
/// "unmeasuredPages". The runner gives the finished pages to the GUI while it runs (PDFProgressSink + a poll
/// of the GUI, no signal from the thread; plan 4.5).
///
/// The data in detail (P3a):
///  - "inks": [{ "name" (the name of the ink, Latin-1 of the PDF name), "displayName", "isSpot", "color"
///    ("#rrggbb" of the swatch, may be empty) }] - the process inks Cyan, Magenta, Yellow, Black, then the
///    spot inks of the pages in the order of the walk of their resources; the arrays "area" and "ratio"
///    of a row and of "total" follow this order;
///  - "pages": [{ "page" (from 1), "box" ("trim" / "crop"), "boxArea" (mm²), "pageArea" (mm² of the media
///    box), "area" [mm² per ink], "ratio" [% per ink], "coverage" (the sum of the ratios, %),
///    "maximumCoverage" (% of the darkest pixel) }] in the order of the pages;
///  - "total": { "area" [the sum of the pages, each page once - B163], "ratio" [the ink inside of the boxes
///    of all pages over their area, %], "boxArea", "pageArea" };
///  - "peak": { "page", "coverage" } - the highest total area coverage of a pixel and its page (null without
///    a measured page);
///  - "box": "trim", "crop", or "mixed" when the pages differ;
///  - "spotsOverLimit": [names], which have no separation (over 26 spot inks, 476:11703);
///  - "renderingIssues": [{ "page", "message" }] - the page is measured with a warning (Q7.9 b);
///  - "unmeasuredPages": [{ "page", "message" }] - the page cannot be rendered at all, it is not in the
///    totals (Q7.9 b);
///  - "pageCount" (measured pages), "resolution".
///
/// No permission (Q7.2: numbers, no content).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInkCoverage : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolInkCoverage)

public:
    static constexpr const char* TOOL_ID = "review.ink-coverage";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_RESOLUTION = "resolution";
    static constexpr const char* PARAMETER_FORMAT = PDFDocumentToolTableWriter::PARAMETER_FORMAT;

    /// Default pixels of the longer side of a page
    static constexpr int DEFAULT_RESOLUTION = 1920;

    /// Fewest pixels of the longer side of a page
    static constexpr int MINIMUM_RESOLUTION = 72;

    /// Keys of the data of the result
    static constexpr const char* DATA_PAGES = "pages";
    static constexpr const char* DATA_INKS = "inks";
    static constexpr const char* DATA_TOTAL = "total";
    static constexpr const char* DATA_PEAK = "peak";
    static constexpr const char* DATA_BOX = "box";
    static constexpr const char* DATA_SPOTS_OVER_LIMIT = "spotsOverLimit";
    static constexpr const char* DATA_RENDERING_ISSUES = "renderingIssues";
    static constexpr const char* DATA_UNMEASURED_PAGES = "unmeasuredPages";

    static constexpr const char* DATA_PAGE_COUNT = "pageCount";
    static constexpr const char* DATA_RESOLUTION = "resolution";

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The run of execute with a collector of the finished pages (the GUI runs it in its own job
    /// and polls the collector; execute passes nullptr). The pages are measured in parallel
    /// (PDFExecutionPolicy) in batches, whose estimated bitmaps fit into the budget of the images
    /// (PDFRasterizerPool::getDefaultImageDataBudget; a page larger than the budget alone). Stopped
    /// after a cancellation (the collector keeps the finished pages).
    /// \param context The context (one document)
    /// \param parameters The normalized parameters
    /// \param control Cancellation (optional)
    /// \param progress Progress, one step per page (optional)
    /// \param collector The finished pages (optional)
    static PDFDocumentToolResult measure(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress, PDFInkCoverageCollector* collector);

    /// The file of an export of the data of a result (the table page × ink with the Total row): the
    /// columns "page", then per ink "inkN-area" (mm²) and "inkN-ratio" (%) with the name of the ink
    /// in the title (N from 1 - the element names of XML never come from the document, B179); the
    /// row Total. Text and HTML by the formatter of \p options, CSV and XML in the C locale.
    static PDFDocumentToolOutputFile writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage);

    /// The model of the export of writeOutput (PdfTool writes it in its console style)
    static PDFDocumentToolTableSection createTables(const QJsonObject& data);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_INKCOVERAGE_H
