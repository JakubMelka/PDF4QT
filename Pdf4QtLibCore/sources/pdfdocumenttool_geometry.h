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

#ifndef PDFDOCUMENTTOOL_GEOMETRY_H
#define PDFDOCUMENTTOOL_GEOMETRY_H

#include "pdfdocumenttool.h"
#include "pdfpagegeometry.h"

#include <optional>
#include <vector>

namespace pdf
{

// The tools Crop Pages (pages.crop), Resize Pages (pages.resize) and Set Page Boxes (pages.boxes)
// of the core (package B of step 3a of phase 2, plan 4.1, 5.1-5.5, 7). They change the boxes (and
// Resize the content) of the pages of the open document by PDFPageGeometry: the numbers of the
// objects stay, the result is saved incrementally (Keep in this tab). A certified document
// (PDFDocMDPPermissions, Q12, R10) is said in "certified" of the data: no level of DocMDP allows a
// change of the pages, the caller offers only Save as a copy.

/// Crop Pages (pages.crop, 389:22382 ... 405:32319): equal margins, a custom box of the shown
/// reference page, or the removal of the crop (the crop box = the media box).
///
/// Result: Success - the changed document; facts "cropped" ("48 pages"), "pageSize" ("190 × 277
/// mm (was 210 × 297 mm)" of the reference page), "hiddenContent" ("Kept in the file", Remove:
/// "Visible again"), "pageBoxes" when the bleed, trim or art box of a page follows the crop box;
/// data "croppedCount", "croppedPages", "mode", "pageSizeBefore", "pageSizeAfter" (mm),
/// "certified"; NothingToDo - every page has the crop already; Failed - by validate.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolCropPages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolCropPages)

public:
    static constexpr const char* TOOL_ID = "pages.crop";

    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_MODE = "mode";
    static constexpr const char* PARAMETER_MARGIN = "margin";
    static constexpr const char* PARAMETER_LEFT = "left";
    static constexpr const char* PARAMETER_TOP = "top";
    static constexpr const char* PARAMETER_WIDTH = "width";
    static constexpr const char* PARAMETER_HEIGHT = "height";
    static constexpr const char* PARAMETER_REFERENCE_PAGE = "referencePage";

    static constexpr const char* MODE_EQUAL_MARGINS = "equal-margins";
    static constexpr const char* MODE_CUSTOM_BOX = "custom-box";
    static constexpr const char* MODE_REMOVE = "remove";

    /// The settings of the parameters
    static PDFPageGeometry::CropSettings getSettings(const QVariantMap& parameters);

    /// The reference page of the custom box (index from 0): the parameter referencePage (a page
    /// number of the panels; 0 = none), else the first page of \p pages
    static PDFInteger getReferencePage(const PDFDocument* document, const QVariantMap& parameters, const std::vector<PDFInteger>& pages);

    /// The custom box is inside of the shown reference page; else the translated reason, the
    /// largest value said (398:28158 "Left 20 mm + width 250 mm ends 60 mm outside the 210 mm
    /// wide page. The width can be 190 mm at most.")
    static QString checkCustomBox(const PDFPage* referencePage, const QRectF& boxMM);

    /// The changes of the pages, std::nullopt and the reason for invalid parameters
    static std::optional<std::vector<PDFPageGeometryChange>> createChanges(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

/// Resize Pages (pages.resize, 391:23146 ... 391:25948): a new sheet (a preset, the size of the
/// page, a custom size; the orientation), the margins, the anchor and the offset; the content is
/// scaled to fit (with the annotations, destinations, viewports, beads, patterns and layout
/// boxes, 5.4) or keeps its size (only the boxes move).
///
/// Result: Success - the changed document; facts "pages", "pageSize" ("A4 · 210 × 297 mm (was
/// Letter)"), "content" ("Scaled to 83 %, comments and fields too", "Not scaled"); data
/// "resizedCount", "resizedPages", "scalePercent" (the smallest), the counters of
/// PDFPageGeometryStatistics, "certified"; NothingToDo - no page changes; Failed - by validate.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolResizePages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolResizePages)

public:
    static constexpr const char* TOOL_ID = "pages.resize";

    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_PAGE_SIZE = "pageSize";
    static constexpr const char* PARAMETER_WIDTH = "width";
    static constexpr const char* PARAMETER_HEIGHT = "height";
    static constexpr const char* PARAMETER_ORIENTATION = "orientation";
    static constexpr const char* PARAMETER_CONTENT = "content";
    static constexpr const char* PARAMETER_ANCHOR = "anchor";
    static constexpr const char* PARAMETER_MARGIN_LEFT = "marginLeft";
    static constexpr const char* PARAMETER_MARGIN_TOP = "marginTop";
    static constexpr const char* PARAMETER_MARGIN_RIGHT = "marginRight";
    static constexpr const char* PARAMETER_MARGIN_BOTTOM = "marginBottom";
    static constexpr const char* PARAMETER_OFFSET_X = "offsetX";
    static constexpr const char* PARAMETER_OFFSET_Y = "offsetY";
    static constexpr const char* PARAMETER_PRESERVE_ASPECT_RATIO = "preserveAspectRatio";
    static constexpr const char* PARAMETER_SCALE_ANNOTATIONS = "scaleAnnotations";

    static constexpr const char* SIZE_KEEP = "keep";
    static constexpr const char* SIZE_CUSTOM = "custom";

    static constexpr const char* ORIENTATION_AUTO = "auto";
    static constexpr const char* ORIENTATION_PORTRAIT = "portrait";
    static constexpr const char* ORIENTATION_LANDSCAPE = "landscape";

    static constexpr const char* CONTENT_SCALE_TO_FIT = "scale-to-fit";
    static constexpr const char* CONTENT_KEEP_SIZE = "keep-size";

    /// The settings of the parameters
    static PDFPageGeometry::ResizeSettings getSettings(const QVariantMap& parameters);

    /// The changes of the pages, std::nullopt and the reason for invalid parameters (391:25272
    /// "The left and right margins together (240 mm) are wider than the A4 page (210 mm)…")
    static std::optional<std::vector<PDFPageGeometryChange>> createChanges(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    /// The text of a margin, which leaves no room (391:25272), of the page and the settings
    static QString getMarginsErrorText(const PDFPage* page, const QVariantMap& parameters);

    /// "A4", "Letter", ... of a size as shown in points (either orientation), empty = no preset
    static QString getPaperName(const QSizeF& sizeInPoints);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

/// Set Page Boxes (pages.boxes, 391:26688 ... 391:28535): a new box = the effective box Based on
/// + the margins (positive = larger), written to every box of Write to; the content is not
/// touched. A box outside of the media box is written as it is (the warning 391:27638; 14.11.2
/// reduces it when it is used).
///
/// Result: Success - the changed document; facts "pages", a fact per written box ("Bleed box" =
/// "216 × 303 mm (trim box + 3 mm)"), "content" ("Not changed"), "outsideMedia" when a box is
/// larger than the media box; data "changedCount", "changedPages", "basedOn", "writeTo",
/// "outsideMediaPages", "certified"; NothingToDo - the boxes have the values already.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSetPageBoxes : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSetPageBoxes)

public:
    static constexpr const char* TOOL_ID = "pages.boxes";

    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_BASED_ON = "basedOn";
    static constexpr const char* PARAMETER_WRITE_TO = "writeTo";
    static constexpr const char* PARAMETER_MARGIN_LEFT = "marginLeft";
    static constexpr const char* PARAMETER_MARGIN_TOP = "marginTop";
    static constexpr const char* PARAMETER_MARGIN_RIGHT = "marginRight";
    static constexpr const char* PARAMETER_MARGIN_BOTTOM = "marginBottom";

    /// The boxes of the text of writeTo ("bleed, trim"; the names of PDFPageGeometry::getBoxName,
    /// each once, in the order media, crop, bleed, trim, art); std::nullopt and the reason for an
    /// unknown name, empty for no box
    static std::optional<std::vector<PDFPageGeometry::Box>> parseWriteTo(const QString& text, QString* errorMessage);

    /// The text of writeTo of boxes
    static QString createWriteTo(const std::vector<PDFPageGeometry::Box>& boxes);

    /// Translated name of a box ("Bleed box") and its short name ("Bleed")
    static QString getBoxTitle(PDFPageGeometry::Box box);
    static QString getBoxShortTitle(PDFPageGeometry::Box box);

    /// The margins of the parameters (millimeters as shown)
    static QMarginsF getMargins(const QVariantMap& parameters);

    /// The new box of a page and whether it reaches outside of the media box (the new one, when
    /// Write to has the media box)
    static QRectF computeNewBox(const PDFPage* page, const QVariantMap& parameters, bool* isOutsideMedia);

    /// The largest margin (millimeters), which keeps the new box inside of the media box of the
    /// page ("reduce the margins to 8 mm or less", 391:27638); negative = none does
    static PDFReal getLargestMarginInsideMedia(const PDFPage* page, const QVariantMap& parameters);

    /// The changes of the pages, std::nullopt and the reason for invalid parameters
    static std::optional<std::vector<PDFPageGeometryChange>> createChanges(const PDFDocument* document, const QVariantMap& parameters, std::vector<PDFInteger>* outsideMediaPages, QString* errorMessage);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

/// The parts of the three tools, which the GUI and the tests share
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolGeometry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolGeometry)

public:
    PDFDocumentToolGeometry() = delete;

    /// Identifiers of the nine anchors (the values of the parameter anchor) and back
    static QString getAnchorName(Qt::Alignment anchor);
    static Qt::Alignment getAnchor(const QString& name);

    /// "190 × 277 mm" of a size in millimeters (whole millimeters, as the frames)
    static QString formatSizeMM(const QSizeF& sizeMM);

    /// Pages of the parameter pages (indices from 0), empty and the reason for none
    static std::vector<PDFInteger> getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    /// The document is certified: a change of its pages is only saved as a copy (Q12)
    static bool isCertified(const PDFDocument* document);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_GEOMETRY_H
