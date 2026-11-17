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

#ifndef PDFDOCUMENTTOOL_OUTPUTPREVIEW_H
#define PDFDOCUMENTTOOL_OUTPUTPREVIEW_H

#include "pdfdocumenttool.h"
#include "pdfoutputpreviewanalyzer.h"

namespace pdf
{

/// The tool Output Preview for MCP and PdfTool (review.output-preview; plan of step 7 of phase 2, 4.6, 7.1):
/// the measurements of PDFOutputPreviewAnalyzer of one page - no picture (MCP has its own render). The GUI
/// shows the canvas of PDFAppToolGUIOutputPreview over the same analyzer; it has only Close (10.3 point 34).
///
/// Parameters: "page" (from 1), "resolution" (72 to PDFOutputPreviewSettings::MAXIMUM_RESOLUTION, default
/// 1920), "mode" (PDFOutputPreviewAnalyzer::getModeId), "coverageLimit" (% , default 300), "richBlackLimit"
/// (% of K, default 95 - Q7.7), "simulateSeparations" (Bool, true), "simulatePaperColor" (Bool, false),
/// "paperColor" (Color, white), "inactiveInks" (String: the names of the unchecked inks, comma separated), the
/// content kinds "images", "text", "vectors", "shadings", "patterns" (Bool, true).
///
/// Data: "inks" (name, isSpot, isActive, area mm²), "spotLimitReached", "pageArea", "maximumCoverage",
/// "areaOverCoverageLimit", "richBlackArea", "renderingIssues".
///
/// The data in detail (P3a): "page" (from 1); "inks" [{ "name", "displayName", "isSpot", "isActive", "isMeasured"
/// (false for a spot ink over the limit of 26), "color" ("#rrggbb"), "area" (mm² of the whole page weighted by
/// the tint), "ratio" (% of the box) }]; "spotLimitReached"; "pageArea" (mm² of the box of the ratios: TrimBox,
/// else CropBox - Q7.6), "box" ("trim" / "crop"), "mediaArea" (mm² of the media box); "maximumCoverage" (%);
/// "areaOverCoverageLimit", "richBlackArea" (mm²); "coverageLimit", "richBlackLimit" (the limits used, %);
/// "renderingIssues" [messages] - the page is measured with a warning (476:11358). A page, which cannot be
/// rendered at all, fails the run.
///
/// No permission (Q7.2).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolOutputPreview : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolOutputPreview)

public:
    static constexpr const char* TOOL_ID = "review.output-preview";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGE = "page";
    static constexpr const char* PARAMETER_RESOLUTION = "resolution";
    static constexpr const char* PARAMETER_MODE = "mode";
    static constexpr const char* PARAMETER_COVERAGE_LIMIT = "coverageLimit";
    static constexpr const char* PARAMETER_RICH_BLACK_LIMIT = "richBlackLimit";
    static constexpr const char* PARAMETER_SIMULATE_SEPARATIONS = "simulateSeparations";
    static constexpr const char* PARAMETER_SIMULATE_PAPER_COLOR = "simulatePaperColor";
    static constexpr const char* PARAMETER_PAPER_COLOR = "paperColor";
    static constexpr const char* PARAMETER_INACTIVE_INKS = "inactiveInks";
    static constexpr const char* PARAMETER_IMAGES = "images";
    static constexpr const char* PARAMETER_TEXT = "text";
    static constexpr const char* PARAMETER_VECTORS = "vectors";
    static constexpr const char* PARAMETER_SHADINGS = "shadings";
    static constexpr const char* PARAMETER_PATTERNS = "patterns";

    /// Keys of the data of the result
    static constexpr const char* DATA_INKS = "inks";
    static constexpr const char* DATA_SPOT_LIMIT_REACHED = "spotLimitReached";
    static constexpr const char* DATA_PAGE_AREA = "pageArea";
    static constexpr const char* DATA_MAXIMUM_COVERAGE = "maximumCoverage";
    static constexpr const char* DATA_AREA_OVER_COVERAGE_LIMIT = "areaOverCoverageLimit";
    static constexpr const char* DATA_RICH_BLACK_AREA = "richBlackArea";
    static constexpr const char* DATA_RENDERING_ISSUES = "renderingIssues";

    /// The settings of the analyzer of the normalized parameters (the GUI and the tool alike): the
    /// values of the parameters only - the color management and the rendering are the caller's
    /// (PDFOutputPreviewSettings::create of the context of a run, the settings of the tab of the GUI).
    /// "inactiveInks" is split by commas, the names trimmed (an empty name is left out).
    static PDFOutputPreviewSettings createSettings(const QVariantMap& parameters);

    /// The data of a result of the analyzer (see the class)
    static QJsonObject createData(const PDFOutputPreviewResult& result, const PDFOutputPreviewSettings& settings);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_OUTPUTPREVIEW_H
