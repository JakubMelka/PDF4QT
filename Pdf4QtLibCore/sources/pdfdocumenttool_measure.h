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

#ifndef PDFDOCUMENTTOOL_MEASURE_H
#define PDFDOCUMENTTOOL_MEASURE_H

#include "pdfdocumenttool.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdfmeasure.h"
#include "pdfmeasurewriter.h"

#include <QJsonArray>
#include <QJsonObject>

#include <optional>
#include <vector>

namespace pdf
{

/// The tool Measure (review.measure, Figma 471:5490 …; plan of step 7 of phase 2, 4.8 with R7-12, owner answers
/// Q7.4, Q7.5, Q7.20 and FM-9 cancelled). In the GUI the user draws the measurements on the pages of the document
/// tab (PDFAppMeasureOverlay); this core tool lists, exports and adds them for the GUI, MCP and PdfTool.
///
/// Parameters (the names are PARAMETER_*):
///   - "mode" (Enum: list, csv, add; default list): list = Analysis (the data), csv = Analysis with the CSV
///     file as its one output, add = ChangeDocument (the measurements become annotations; no result dialog,
///     10.3 point 35; one step of Undo);
///   - "pages" (PageSelection): the pages, whose existing measurement annotations and viewports are listed
///     (list, csv); add ignores it (each measurement names its page);
///   - "measurements" (String, structured JSON, at most MAXIMUM_MEASUREMENT_COUNT): the new measurements
///     [{ "page": n from 1, "kind": "distance" | "perimeter" | "area" | "angle", "points": [[x, y], …] in points of
///     the default user space of the page (the origin at its lower left corner; at most MAXIMUM_POINT_COUNT),
///     "scale": { "paperValue", "paperUnit", "realValue", "realUnit", "name" } (optional; omitted = 1:1, a point
///     inside a viewport of the page with a measure takes the scale of the viewport - the last one wins,
///     ISO 32000-2, 12.9.2) }]; list and csv compute and include them (the temporary measurements of the GUI),
///     add writes them (PDFMeasureWriter). The measurements may lie on different pages, each one on one page
///     (FM-9 cancelled);
///   - "lengthUnit", "areaUnit", "angleUnit" (Enum: the ids of PDFMeasureUnits; the panel Units 473:8597): the
///     units of the values (the plan named one parameter "units": three enumerations give MCP a schema of
///     their values);
///   - "fontFamily", "fontSize", "bold", "italic", "textColor", "backgroundColor" (the panel Labels; add only).
///
/// Data of the result (DATA_*):
///   - "measurements": [{ "page" (from 1), "kind" (KIND_*), "value" (number in "unit"), "unit" (a unit id),
///     "text" (the shown text of the value, the C locale), "scale" (PDFMeasureScale::getRatioText), "source"
///     ("new" = of the parameter, "annotation" = an existing measurement annotation of the document, also of
///     another producer - FM-4), "annotation" ("12 0 R" of an existing one), "viewport" (the name of the viewport,
///     whose scale applies; may be empty) }] in the order of the pages; at most MAXIMUM_LISTED_MEASUREMENTS
///     (10 000, plan 6.2) with "truncated";
///   - "viewports": [{ "page", "name", "bbox": [x1, y1, x2, y2], "ratio" (/R), "subtype" ("RL" / "GEO") }] - a
///     viewport of /Measure /GEO is listed and ignored for the measurements (FM-7, NOTES.txt);
///   - "added" (add: the number of the written annotations), "failed" (add: [{ "index" (from 0 in
///     "measurements"), "reason" }] - 473:90376 a partial failure).
/// The CSV (writeOutput) has the six columns Page; Type; Value; Unit; Text; Scale (owner answer Q7.20, Figma
/// 473:91038 corrected by the designer), the value with a decimal point (the C locale), every measurement of the
/// data (the new and the existing ones) - PDFDocumentToolTableWriter (RFC 4180, formula escape of the texts of the
/// document, B182).
///
/// Permissions (owner answer Q7.2): listing and exporting the measurements is reading the document; adding the
/// annotations needs bit 6 (ModifyInteractiveItems, Table 22), the owner password offered (FM-2). A certified
/// document of P = 1 or 2 allows no new annotation (12.8.2.2, Table 257): add is refused by validate. A locked
/// tab has only the temporary measurements and the CSV (checked by the application).
///
/// The opacities of the labels (the panel Labels 471:5490: Opacity of the text color 100 %, of the background
/// color 10 %) are the parameters "textOpacity" and "backgroundOpacity" in percent: a color parameter keeps no
/// alpha. The panel keeps its own state in the panel-only parameters "kind" (the 8 kinds of the toolbar,
/// 471:6294), "storage" (temporary / annotations, 471:7608) and "rememberScale" (Remember the scale per document,
/// 473:9500) - the last values of the tool, never in the schema of MCP.
///
/// The GUI uses the static functions directly (no run: the list, the export and the annotations of the open
/// tab are immediate - as Redact writes its marks): parseMeasurements, createData, addAnnotations.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureItem
{
public:
    PDFInteger pageIndex = -1;              ///< The page (from 0)
    PDFMeasureKind kind = PDFMeasureKind::Distance;
    std::vector<QPointF> points;            ///< Normalized points (PDFMeasureGeometry::normalizePoints) in the default user space of the page
    std::optional<PDFMeasureScale> scale;   ///< The scale chosen for it; std::nullopt = 1:1. A viewport of the page with a measure wins (12.9.2).
};

class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolMeasure : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolMeasure)

public:
    static constexpr const char* TOOL_ID = "review.measure";

    /// Names of the parameters
    static constexpr const char* PARAMETER_MODE = "mode";
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_MEASUREMENTS = "measurements";
    static constexpr const char* PARAMETER_LENGTH_UNIT = "lengthUnit";
    static constexpr const char* PARAMETER_AREA_UNIT = "areaUnit";
    static constexpr const char* PARAMETER_ANGLE_UNIT = "angleUnit";
    static constexpr const char* PARAMETER_FONT_FAMILY = "fontFamily";
    static constexpr const char* PARAMETER_FONT_SIZE = "fontSize";
    static constexpr const char* PARAMETER_BOLD = "bold";
    static constexpr const char* PARAMETER_ITALIC = "italic";
    static constexpr const char* PARAMETER_TEXT_COLOR = "textColor";
    static constexpr const char* PARAMETER_BACKGROUND_COLOR = "backgroundColor";
    static constexpr const char* PARAMETER_TEXT_OPACITY = "textOpacity";
    static constexpr const char* PARAMETER_BACKGROUND_OPACITY = "backgroundOpacity";

    /// The panel-only parameters of the GUI (see above)
    static constexpr const char* PARAMETER_KIND = "kind";
    static constexpr const char* PARAMETER_STORAGE = "storage";
    static constexpr const char* PARAMETER_REMEMBER_SCALE = "rememberScale";

    /// The drawing kinds of the toolbar (471:6294; PARAMETER_KIND)
    static constexpr const char* DRAW_HORIZONTAL = "horizontal";
    static constexpr const char* DRAW_VERTICAL = "vertical";
    static constexpr const char* DRAW_LINEAR = "linear";
    static constexpr const char* DRAW_PERIMETER = "perimeter";
    static constexpr const char* DRAW_RECTANGLE_PERIMETER = "rectangle-perimeter";
    static constexpr const char* DRAW_AREA = "area";
    static constexpr const char* DRAW_RECTANGLE_AREA = "rectangle-area";
    static constexpr const char* DRAW_ANGLE = "angle";

    /// Where new measurements go (471:7608; PARAMETER_STORAGE)
    static constexpr const char* STORAGE_TEMPORARY = "temporary";
    static constexpr const char* STORAGE_ANNOTATIONS = "annotations";

    /// The defaults of the panel Labels (471:5490): size 10 pt, black text 100 %, black background 10 %
    static constexpr int DEFAULT_TEXT_OPACITY = 100;
    static constexpr int DEFAULT_BACKGROUND_OPACITY = 10;

    /// Identifiers of the modes
    static constexpr const char* MODE_LIST = "list";
    static constexpr const char* MODE_CSV = "csv";
    static constexpr const char* MODE_ADD = "add";

    /// Identifiers of the kinds of the measurements (PDFMeasureKind)
    static constexpr const char* KIND_DISTANCE = "distance";
    static constexpr const char* KIND_PERIMETER = "perimeter";
    static constexpr const char* KIND_AREA = "area";
    static constexpr const char* KIND_ANGLE = "angle";

    /// Keys of the data of the result
    static constexpr const char* DATA_MEASUREMENTS = "measurements";
    static constexpr const char* DATA_VIEWPORTS = "viewports";
    static constexpr const char* DATA_TRUNCATED = "truncated";
    static constexpr const char* DATA_ADDED = "added";
    static constexpr const char* DATA_FAILED = "failed";

    /// Limits (plan 6.2): the measurements of one call (MCP), the points of one measurement, the listed ones
    static constexpr int MAXIMUM_MEASUREMENT_COUNT = 500;
    static constexpr int MAXIMUM_POINT_COUNT = 200;
    static constexpr int MAXIMUM_LISTED_MEASUREMENTS = 10000;

    /// Characters of the JSON of the measurements (MAXIMUM_MEASUREMENT_COUNT measurements of many points)
    static constexpr int MAXIMUM_MEASUREMENTS_LENGTH = PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH;

    /// The id of a kind (KIND_*) and back (std::nullopt for an unknown id)
    static QString getKindId(PDFMeasureKind kind);
    static std::optional<PDFMeasureKind> getKind(const QString& id);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;

    /// mode add = ChangeDocument (Keep, an incremental update of a signed file); list, csv = Analysis
    virtual PDFDocumentToolResultKind getResultKind(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolPreselectedResult getPreselectedResult(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolSignatureImpact getSignatureImpact(const QVariantMap& parameters) const override;

    /// mode add = bit 6 (ModifyInteractiveItems, Table 22); list, csv none
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const override;

    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The CSV of the data of a result (the GUI exports its list without a new run; Export CSV, 473:91038): the
    /// columns Page; Type; Value; Unit; Text; Scale (Q7.20), the C locale. \p parameters give the format (csv).
    static PDFDocumentToolOutputFile writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage);

    /// The table of the CSV of the data (the columns of Q7.20)
    static PDFDocumentToolTable createTable(const QJsonObject& data);

    /// Reads the measurements of the parameter (untrusted input: a text, which is not a JSON array, more than
    /// MAXIMUM_MEASUREMENT_COUNT items, an item, which is not an object, a page outside of the document, an
    /// unknown kind, points of a wrong count or not finite, a point outside of the MediaBox of its page - review
    /// RS7CCORE L4, an invalid scale) - std::nullopt and the translated reason naming the measurement
    /// ("Measurement 3: …"). An empty text is no measurement. The GUI never calls it: its overlay keeps the points
    /// on the MediaBox of the page (PDFAppMeasureOverlay::addPoint).
    static std::optional<std::vector<PDFMeasureItem>> parseMeasurements(const QString& text, const PDFDocument* document, QString* errorMessage);

    /// The JSON of the parameter "measurements" of the items (the page from 1)
    static QString toJson(const std::vector<PDFMeasureItem>& items);

    /// The scale of a measurement on its page: the scale of the viewport of its first point (12.9.2; the
    /// name of the viewport into \p viewportName), else its own scale, else 1:1
    static PDFMeasureScale getEffectiveScale(const PDFDocument* document, const PDFMeasureItem& item, QString* viewportName);

    /// The data of list and csv (see the class): the new measurements \p items (shown in \p units) and the
    /// measurement annotations and the viewports of \p pageIndices, in the order of the pages; at most
    /// MAXIMUM_LISTED_MEASUREMENTS with "truncated". The annotations are read with the reading limit of the
    /// vertices (PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT - other producers write more than this tool,
    /// review RS7CCORE L6). Any thread; \p control (optional) stops the reading - the caller asks it and drops a
    /// canceled result (review RS7CCORE L9).
    static QJsonObject createData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const std::vector<PDFMeasureItem>& items, const PDFMeasureDisplayUnits& units,
                                  const PDFOperationControl* control);

    /// As createData, with the measurement annotations of \p pageIndices read before (PDFMeasureAnnotationInfo::readPages
    /// of the caller, \p isAnnotationsTruncated its truncation): the GUI exports the annotations of its list, which it
    /// has read in a worker thread, never again in the GUI thread
    static QJsonObject createData(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const std::vector<PDFMeasureAnnotationInfo>& annotations, bool isAnnotationsTruncated,
                                  const std::vector<PDFMeasureItem>& items, const PDFMeasureDisplayUnits& units, const PDFOperationControl* control);

    /// The measurements \p items as measurement annotations of the document (PDFMeasureWriter, one builder - one
    /// step of Undo of the caller). \p added gets the count of the written ones, \p failed the items, which
    /// could not be written ({ "index", "reason" }). std::nullopt when none was written (the reason of the first
    /// failure in \p errorMessage). A document, which declares PDF/A (getConformanceDeclarations, as the Signature
    /// tool), gets opaque annotations (PDFMeasureAnnotationStyle::isOpaqueRequired, owner 2026-11-19); its
    /// declaration stays.
    static std::optional<PDFDocument> addAnnotations(const PDFDocument* document, const std::vector<PDFMeasureItem>& items, const PDFMeasureDisplayUnits& units, const PDFMeasureAnnotationStyle& style,
                                                     int* added, QJsonArray* failed, QString* errorMessage);

    /// The units of the parameters (lengthUnit, areaUnit, angleUnit)
    static PDFMeasureDisplayUnits getUnits(const QVariantMap& parameters);

    /// The style of the labels of the parameters (fontFamily … backgroundOpacity) and the author
    static PDFMeasureAnnotationStyle getStyle(const QVariantMap& parameters, const QString& author);

    /// The measurement annotations of the document can be added by the certification (12.8.2.2, Table 257):
    /// not certified, or P = 3 (annotations allowed)
    static bool isAnnotationAllowedByCertification(const PDFDocument* document);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_MEASURE_H
