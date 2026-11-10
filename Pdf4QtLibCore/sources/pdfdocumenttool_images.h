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

#ifndef PDFDOCUMENTTOOL_IMAGES_H
#define PDFDOCUMENTTOOL_IMAGES_H

#include "pdfdocumenttool.h"
#include "pdfdocumentpageplan.h"
#include "pdfimagepagefactory.h"
#include "pdfpagesizepresets.h"

#include <QMarginsF>

#include <map>
#include <optional>

namespace pdf
{

/// The tool Images to PDF (convert.images-to-pdf; Figma 336:1976 ... 348:29711, 356:9165,
/// 356:9556; plan of step 3 of phase 2, 4.1, 5.8, K14-K17, K34, Q10, Q13 / R16): a new document,
/// one page per image (every frame of a multi-page TIFF), each image laid out on its page by
/// PDFImagePageLayout and written by PDFDocumentMerger (the images are never decoded twice: JPEG
/// with its bytes, the others losslessly, PDFImagePageFactory).
///
/// Inputs: the images of the context (PDFDocumentToolContext::images, at least 1, at most
/// MAXIMUM_INPUT_COUNT; MCP gives them by imagePaths, Q10). "inputs" orders them by their input
/// ids as the parameter of Merge (empty = their order in the context).
///
/// The page (336:2639, 356:9556): pageSize = a named size of PDFPageSizePresets (A4, A3, A5,
/// Letter, Legal), custom (width x height in millimeters, 3 to 14 400 points each) or
/// fit-to-image (the page is the image by its resolution and the margins around it);
/// orientation auto (the orientation of each image after its turn and crop), portrait or
/// landscape; margins in millimeters on every side, less than half of the shorter side of a
/// named or custom page; imageScaling fit, fill, stretch, original-size; anchor one of the nine
/// cells (top-left ... bottom-right; never mirrored).
///
/// "adjustments" (panel only, never stored, not of MCP): the turn and the crop of the images
/// (336:3159), items separated by ";" - "<letters>[@<turn>][#<left>,<top>,<right>,<bottom>]",
/// the input by its letters in the order of "inputs", the turn clockwise (90, 180, 270), the
/// crop margins in pixels of the shown image (the orientation of EXIF applied, its first frame);
/// a crop, which leaves less than one pixel, is limited to one pixel ("Apply to: All images").
///
/// optimizeImages: off (the bytes of the images unchanged), prefer-quality, minimum-size (the
/// presets of Merge, K17 / K24 of step 2).
///
/// Result: Success - a New document with its written bytes (R2 with "Open in this tab", R16)
/// and the suggested name "<base name of the first image>.pdf"; facts "pages", "size" (and
/// "images" when optimized); data "pageCount", "imageCount", "pageSize", "orientation",
/// "imageScaling", "anchor", "marginsMillimeters", "sizeAfter". An image, which cannot be read
/// during the run, is left out (leftOutInputs, as Merge); none left = Failed. Stopped = nothing
/// is kept.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolImagesToPdf : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolImagesToPdf)

public:
    static constexpr const char* TOOL_ID = "convert.images-to-pdf";

    /// Names of the parameters
    static constexpr const char* PARAMETER_INPUTS = "inputs";
    static constexpr const char* PARAMETER_ADJUSTMENTS = "adjustments";
    static constexpr const char* PARAMETER_PAGE_SIZE = "pageSize";
    static constexpr const char* PARAMETER_WIDTH = "width";
    static constexpr const char* PARAMETER_HEIGHT = "height";
    static constexpr const char* PARAMETER_ORIENTATION = "orientation";
    static constexpr const char* PARAMETER_MARGINS = "margins";
    static constexpr const char* PARAMETER_IMAGE_SCALING = "imageScaling";
    static constexpr const char* PARAMETER_ANCHOR = "anchor";
    static constexpr const char* PARAMETER_OPTIMIZE_IMAGES = "optimizeImages";

    /// Identifiers of the sizes (336:2639)
    static constexpr const char* SIZE_FIT_TO_IMAGE = "fit-to-image";
    static constexpr const char* SIZE_A4 = PDFPageSizePresets::A4;
    static constexpr const char* SIZE_A3 = PDFPageSizePresets::A3;
    static constexpr const char* SIZE_A5 = PDFPageSizePresets::A5;
    static constexpr const char* SIZE_LETTER = PDFPageSizePresets::LETTER;
    static constexpr const char* SIZE_LEGAL = PDFPageSizePresets::LEGAL;
    static constexpr const char* SIZE_CUSTOM = "custom";

    /// Identifiers of the orientations (336:1976)
    static constexpr const char* ORIENTATION_AUTO = "auto";
    static constexpr const char* ORIENTATION_PORTRAIT = "portrait";
    static constexpr const char* ORIENTATION_LANDSCAPE = "landscape";

    /// Identifiers of the scaling of the image (356:9556)
    static constexpr const char* SCALING_FIT = "fit";
    static constexpr const char* SCALING_FILL = "fill";
    static constexpr const char* SCALING_STRETCH = "stretch";
    static constexpr const char* SCALING_ORIGINAL_SIZE = "original-size";

    /// Identifiers of the anchors (356:9556, K14: the nine cells of PDFAnchorGrid)
    static constexpr const char* ANCHOR_TOP_LEFT = "top-left";
    static constexpr const char* ANCHOR_TOP_CENTER = "top-center";
    static constexpr const char* ANCHOR_TOP_RIGHT = "top-right";
    static constexpr const char* ANCHOR_MIDDLE_LEFT = "middle-left";
    static constexpr const char* ANCHOR_MIDDLE_CENTER = "middle-center";
    static constexpr const char* ANCHOR_MIDDLE_RIGHT = "middle-right";
    static constexpr const char* ANCHOR_BOTTOM_LEFT = "bottom-left";
    static constexpr const char* ANCHOR_BOTTOM_CENTER = "bottom-center";
    static constexpr const char* ANCHOR_BOTTOM_RIGHT = "bottom-right";

    /// Most images (the catalog, PDFDocumentToolCatalog::MAXIMUM_INPUT_COUNT)
    static constexpr int MAXIMUM_INPUT_COUNT = 1000;

    /// Longest text of "inputs" and of "adjustments"
    static constexpr int MAXIMUM_INPUTS_LENGTH = 65536;
    static constexpr int MAXIMUM_ADJUSTMENTS_LENGTH = 262144;

    /// Default and limits of the margins in millimeters (336:1976: 10 mm)
    static constexpr double DEFAULT_MARGINS_MILLIMETERS = 10.0;
    static constexpr double MAXIMUM_MARGINS_MILLIMETERS = 1000.0;

    /// Default custom size in millimeters (356:9556)
    static constexpr double DEFAULT_WIDTH_MILLIMETERS = 210.0;
    static constexpr double DEFAULT_HEIGHT_MILLIMETERS = 297.0;

    /// The turn and the crop of one image (pixels of the shown image)
    struct Adjustment
    {
        PageRotation turn = PageRotation::None;
        QMargins crop;

        bool operator==(const Adjustment&) const = default;
        bool isEmpty() const { return turn == PageRotation::None && crop.isNull(); }
    };

    /// An image of the run in its order
    struct Input
    {
        size_t index = 0;           ///< Index in PDFDocumentToolContext::images
        QString inputId;
        QString name;
        PDFImagePageFactory::ImageInfo info;
    };

    /// Parses "adjustments" for the given number of inputs (input index -> adjustment);
    /// std::nullopt and the translated reason for an invalid text
    static std::optional<std::map<int, Adjustment>> parseAdjustments(const QString& text, int inputCount, QString* errorMessage);

    /// Text of the adjustments, which parseAdjustments reads back (an empty adjustment is left out)
    static QString createAdjustments(const std::map<int, Adjustment>& adjustments);

    /// The clip of the layout (0 to 1) of crop margins in pixels of an image of \p shownSize;
    /// a crop, which leaves less than one pixel, keeps one pixel; empty = the whole image
    static QRectF getClip(const QMargins& crop, QSize shownSize);

    /// The anchor of its identifier (Qt::AlignCenter for an unknown one) and back
    static Qt::Alignment getAnchor(const QString& anchorId);
    static QString getAnchorId(Qt::Alignment anchor);

    /// The layout of the normalized parameters without the image adjustments (an empty page size
    /// for fit to image); std::nullopt, the translated reason and the name of the parameter
    /// (pageSize, width, margins) for an invalid size or margins
    static std::optional<PDFImagePageLayout> createLayout(const QVariantMap& parameters, QString* errorMessage, QString* errorParameter);

    /// The images of the context in the order of "inputs" (as PDFDocumentToolMerge::resolveInputs);
    /// std::nullopt and the translated reason
    static std::optional<std::vector<Input>> resolveInputs(const PDFDocumentToolContext& context, const QVariantMap& parameters, QString* errorMessage);

    /// Suggested name of the result: "<base name of the first image>.pdf", "Images.pdf" without a name
    static QString getSuggestedFileName(const QString& firstInputName);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

/// The tool Interleave Pages (pages.interleave; Figma 385:13263 ... 385:14973; plan of step 3
/// of phase 2, 5.7, Q8, K28, K29, K31, K47): two scans - the front sides and the back sides -
/// joined into one new document by the plan PDFDocumentPagePlan::interleave and the merge of
/// Merge (the same permissions, the same keys of the new document, the same partial result).
///
/// Inputs: exactly two PDF documents, the front sides first ("inputs" orders them as Merge).
/// backSides: reversed (the back side i is the page n - i + 1, a stack turned over) or in-order;
/// extraPages: at-the-end (the sides without a pair follow in their order) or leave-out.
/// The result has no outline and no document parts (Q8); bookmarks of the scans are a fact.
///
/// Result: as Merge - a New document "<name of the front sides>_interleaved.pdf"; facts
/// "pages" ("12 · 6 front and 6 back sides"), "backSides" ("Reversed order", "In order"),
/// "extraPages" when some side has no pair, "bookmarks" when a scan has bookmarks, "size";
/// data "pageCount", "frontPageCount", "backPageCount", "unpairedCount", "backSides",
/// "extraPages", "sizeAfter".
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInterleave : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolInterleave)

public:
    static constexpr const char* TOOL_ID = "pages.interleave";

    /// Names of the parameters
    static constexpr const char* PARAMETER_INPUTS = "inputs";
    static constexpr const char* PARAMETER_BACK_SIDES = "backSides";
    static constexpr const char* PARAMETER_EXTRA_PAGES = "extraPages";

    /// Identifiers of the values (385:13970)
    static constexpr const char* BACK_SIDES_REVERSED = "reversed";
    static constexpr const char* BACK_SIDES_IN_ORDER = "in-order";
    static constexpr const char* EXTRA_PAGES_AT_THE_END = "at-the-end";
    static constexpr const char* EXTRA_PAGES_LEAVE_OUT = "leave-out";

    /// The settings of the plan of the normalized parameters
    static PDFDocumentPagePlan::InterleaveSettings createSettings(const QVariantMap& parameters);

    /// Suggested name of the result: "<base name of the front sides>_interleaved.pdf"
    static QString getSuggestedFileName(const QString& frontName);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_IMAGES_H
