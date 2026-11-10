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

#ifndef PDFDOCUMENTTOOL_MERGE_H
#define PDFDOCUMENTTOOL_MERGE_H

#include "pdfdocumenttool.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentpageplan.h"
#include "pdfimageoptimizer.h"
#include "pdfpagesizepresets.h"

#include <optional>

namespace pdf
{

/// The new document of a result of a tool, which builds it by PDFDocumentMerger (Merge, Blank
/// PDF, Images to PDF, Interleave Pages): written into memory (the size is measured by writing
/// it) and given to the result as a New document with its written bytes. Reentrant.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolNewDocument
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolNewDocument)

public:
    PDFDocumentToolNewDocument() = delete;

    /// Writes the document into memory; empty bytes and the translated reason for a failure or
    /// a result larger than PDFDocumentTool::MAXIMUM_RESULT_SIZE
    static QByteArray write(const PDFDocument* document, QString* errorMessage);

    /// Adds the New document with its written bytes to the result
    static void addToResult(PDFDocumentToolResult* result, PDFDocument document, QByteArray writtenData, const QString& suggestedFileName);

    /// A number of millimeters for a text: at most one decimal place, no trailing zero (K25 of
    /// step 2), in the default locale of the process
    static QString formatMillimeters(double millimeters);
};

/// The tool Merge (pages.merge, Figma 378:1049 ... 412:32654; plan of step 2 of phase 2,
/// chapters 5 to 7, R1-R10, owner answers Q1-Q7): the inputs (documents and images of the
/// context) joined into a new document by PDFDocumentMerger.
///
/// Inputs: the documents and the images of the context. The parameter "inputs" orders
/// them: the input ids (PDFDocumentToolInputDocument::inputId,
/// PDFDocumentToolInputImage::inputId) separated by commas, the first is the input A,
/// the second B, ... (getInputLetters). Empty "inputs" = the documents of the context in
/// their order, then its images (MCP: documentIds, owner answer Q11). Every input of the
/// context is in "inputs" exactly once.
///
/// The plan of the pages of the result ("plan", parsePlan): items separated by commas, each
/// "<letters><page>[-<page>][@<turn>][+]" - the input by its letters, its pages from 1 (a
/// range in either direction, "A6-1" is reversed), the turn clockwise relative to the
/// rotation of the page (@90, @180, @270), "+" a clone (a further occurrence of a page:
/// a copy of the page object and of its annotations, the content is shared; B4). Example
/// "A1-6,B1-9,C1,A3@90,A3+". A page used more than once needs "+" on every occurrence
/// after the first. Empty plan = every page of every input in the order of the inputs.
/// A page of an image input is a frame of the image (every frame of a multi-page TIFF).
///
/// Validation: at least MINIMUM_INPUT_COUNT inputs, at most MAXIMUM_INPUT_COUNT; every
/// input has a page; every PDF input permits the merge (PDFDocumentMerger::checkPermissions,
/// owner answer Q2: Assemble, and the owner password when anything else is restricted -
/// the result is not encrypted and would lose the restriction).
///
/// Result:
///   Success - a New document (open in a new tab / save as a file) with its written bytes
///   (PDFDocumentToolOutputDocument::writtenData, the size is measured by writing it) and
///   the suggested name "<name of the input A>_merged.pdf"; facts "pages" ("16 from 3
///   files"), "outline" ("3 document parts", "Joined outlines", "None"), "size", and when
///   it happened "fieldsRenamed", "destinationsRenamed", "signatures", "tags", "scripts",
///   "layers", "leftOut"; data "pageCount", "inputCount", "partCount", "outline",
///   "optimizeImages", "sizeAfter" and the counters of PDFDocumentMerger::Statistics.
///   An input, whose copy failed during the run (owner answer Q5), is in
///   PDFDocumentToolResult::leftOutInputs and the result is partial (the others merge);
///   no input left = Failed.
///   Stopped - canceled, nothing of the run is used.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolMerge : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolMerge)

public:
    static constexpr const char* TOOL_ID = "pages.merge";

    /// Names of the parameters
    static constexpr const char* PARAMETER_INPUTS = "inputs";
    static constexpr const char* PARAMETER_PLAN = "plan";
    static constexpr const char* PARAMETER_OUTLINE = "outline";
    static constexpr const char* PARAMETER_OPTIMIZE_IMAGES = "optimizeImages";

    /// Identifiers of the modes of the outline (378:1879, menu Outline)
    static constexpr const char* OUTLINE_DOCUMENT_PARTS = "document-parts";
    static constexpr const char* OUTLINE_JOIN = "join-outlines";
    static constexpr const char* OUTLINE_NONE = "no-outline";

    /// Identifiers of the optimization of the images (378:1879, menu Optimize; K24: the
    /// presets Balanced and Smaller file of Compress)
    static constexpr const char* OPTIMIZE_OFF = "off";
    static constexpr const char* OPTIMIZE_PREFER_QUALITY = "prefer-quality";
    static constexpr const char* OPTIMIZE_MINIMUM_SIZE = "minimum-size";

    /// Fewest inputs of a merge (551:149074, K22)
    static constexpr int MINIMUM_INPUT_COUNT = 2;

    /// Most inputs of a merge (chapter 6; MCP has at most 100 documentIds)
    static constexpr int MAXIMUM_INPUT_COUNT = 1000;

    /// Longest text of the parameter "inputs"
    static constexpr int MAXIMUM_INPUTS_LENGTH = 65536;

    /// Longest plan (a plan of 50 000 pages sorted by their numbers, K13, has about 400 000 characters)
    static constexpr int MAXIMUM_PLAN_LENGTH = PDFDocumentPagePlan::MAXIMUM_TEXT_LENGTH;

    /// One page of the result in the plan (the general plan of the pages, plan of step 3 of
    /// phase 2, K44: Merge uses its input, page, turn and clone)
    using PlanItem = PDFDocumentPagePlan::Item;

    /// An input of the merge in its order
    struct Input
    {
        bool isImage = false;       ///< The input is PDFDocumentToolContext::images[index], else documents[index]
        size_t index = 0;           ///< Index in the vector of the context
        QString inputId;            ///< Id of the input (may be empty)
        QString name;               ///< Name of the file
        PDFInteger pageCount = 0;   ///< Pages (frames of an image); 0 = it cannot be read
    };

    /// Letters of the input of the index: A to Z, then AA, AB, ... (R20); empty for a negative index
    static QString getInputLetters(int inputIndex);

    /// Index of the input of its letters (upper case A-Z), -1 for an invalid text
    static int getInputIndex(QStringView letters);

    /// The inputs of the context in the order of the parameter "inputs". std::nullopt and
    /// the translated reason for an id, which is not in the context, an id used twice, an
    /// input of the context missing in the list, or more than MAXIMUM_INPUT_COUNT inputs.
    /// The page counts of the images are read from their bytes (PDFImagePageFactory).
    static std::optional<std::vector<Input>> resolveInputs(const PDFDocumentToolContext& context, const QVariantMap& parameters, QString* errorMessage);

    /// Parses the plan for inputs of the given page counts (see the class). std::nullopt
    /// and the translated reason for an invalid plan, a page out of range, a page twice
    /// without "+", a text longer than MAXIMUM_PLAN_LENGTH.
    static std::optional<std::vector<PlanItem>> parsePlan(const QString& plan, const std::vector<PDFInteger>& pageCounts, QString* errorMessage);

    /// Text of a plan, which parsePlan reads back: consecutive pages of an input with the
    /// same turn and clone flag become a range ("A1-6"), a reversed run a reversed range
    static QString createPlan(const std::vector<PlanItem>& items);

    /// The plan of an empty parameter: every page of every input in their order
    static std::vector<PlanItem> createDefaultPlan(const std::vector<PDFInteger>& pageCounts);

    /// Settings of the merger by the normalized parameters (the outline)
    static PDFDocumentMerger::Settings createSettings(const QVariantMap& parameters);

    /// Optimization of the images of the result by the normalized parameters: Prefer quality =
    /// the preset Balanced of Compress, Minimum size = Smaller file (K24); std::nullopt = Off
    static std::optional<PDFImageOptimizer::Settings> createImageSettings(const QVariantMap& parameters);

    /// Suggested name of the result: "<base name of the first input>_merged.pdf",
    /// "merged.pdf" without a name
    static QString getSuggestedFileName(const QString& firstInputName);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

/// The tool Blank PDF (convert.blank, Figma 337:1976, 337:2221, 356:9975; plan of step 2
/// of phase 2, 5.4, K25-K27, Q14): a new document of empty pages of one size, made by
/// PDFDocumentMerger from blank sources. It has its own tab ("New document") and no
/// result dialog: the document opens in the place of the tab.
///
/// Sizes (points, exact; Portrait): A4 595.276 x 841.890 (210 x 297 mm), A3 841.890 x
/// 1190.551, A5 419.528 x 595.276, Letter 612 x 792, Legal 612 x 1008, 16:9 540 x 960
/// (Landscape 960 x 540 - the GUI switches to Landscape when 16:9 is chosen, Q14; a 16:9
/// size given without an orientation is Landscape, completeGivenParameters, Q22);
/// Landscape swaps the width and the height of a named size. Custom: "width" x
/// "height" in millimeters (mm x 72 / 25.4 points), each side 3 to 14 400 points (K26,
/// PDF Reference 1.7, Table C.1); the orientation of a custom size is given by its sides.
///
/// Result:
///   Success - a New document with its written bytes and the name "Untitled.pdf"; facts
///   "pages" and "pageSize"; data "pageCount", "pageSize", "widthPoints", "heightPoints",
///   "orientation";
///   Failed - a page count or a side outside of the limits (by validate).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolBlank : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolBlank)

public:
    static constexpr const char* TOOL_ID = "convert.blank";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGE_COUNT = "pageCount";
    static constexpr const char* PARAMETER_PAGE_SIZE = "pageSize";
    static constexpr const char* PARAMETER_WIDTH = "width";
    static constexpr const char* PARAMETER_HEIGHT = "height";
    static constexpr const char* PARAMETER_ORIENTATION = "orientation";

    /// Identifiers of the sizes (337:2221): the named sizes of PDFPageSizePresets and Custom
    static constexpr const char* SIZE_A4 = PDFPageSizePresets::A4;
    static constexpr const char* SIZE_A3 = PDFPageSizePresets::A3;
    static constexpr const char* SIZE_A5 = PDFPageSizePresets::A5;
    static constexpr const char* SIZE_LETTER = PDFPageSizePresets::LETTER;
    static constexpr const char* SIZE_LEGAL = PDFPageSizePresets::LEGAL;
    static constexpr const char* SIZE_16X9 = PDFPageSizePresets::SIZE_16X9;
    static constexpr const char* SIZE_CUSTOM = "custom";

    /// Identifiers of the orientations
    static constexpr const char* ORIENTATION_PORTRAIT = "portrait";
    static constexpr const char* ORIENTATION_LANDSCAPE = "landscape";

    /// Limits of the page count (K26)
    static constexpr qint64 MINIMUM_PAGE_COUNT = 1;
    static constexpr qint64 MAXIMUM_PAGE_COUNT = 10000;
    static constexpr qint64 DEFAULT_PAGE_COUNT = 1;

    /// Limits of a side of a page in points (K26, PDFPageSizePresets)
    static constexpr double MINIMUM_SIDE_POINTS = PDFPageSizePresets::MINIMUM_SIDE_POINTS;
    static constexpr double MAXIMUM_SIDE_POINTS = PDFPageSizePresets::MAXIMUM_SIDE_POINTS;

    /// Limits of a side of a custom size in millimeters (3 and 14 400 points)
    static constexpr double MINIMUM_SIDE_MILLIMETERS = PDFPageSizePresets::MINIMUM_SIDE_MILLIMETERS;
    static constexpr double MAXIMUM_SIDE_MILLIMETERS = PDFPageSizePresets::MAXIMUM_SIDE_MILLIMETERS;

    /// Default custom size in millimeters (A4)
    static constexpr double DEFAULT_WIDTH_MILLIMETERS = 210.0;
    static constexpr double DEFAULT_HEIGHT_MILLIMETERS = 297.0;

    /// Portrait size of a named size in points, std::nullopt for custom or an unknown id
    /// (PDFPageSizePresets::getSize)
    static std::optional<QSizeF> getNamedSize(const QString& sizeId);

    /// Points of millimeters (mm x 72 / 25.4, PDFPageSizePresets::getPoints)
    static double getPoints(double millimeters);

    /// The side in points is a valid side of a page (MINIMUM_SIDE_POINTS to MAXIMUM_SIDE_POINTS,
    /// finite; PDFPageSizePresets::isValidSide)
    static bool isValidSide(double points);

    /// Size of the pages of the normalized parameters in points; an empty size and the
    /// translated reason for a side outside of the limits
    static QSizeF getPageSize(const QVariantMap& parameters, QString* errorMessage);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual QVariantMap completeGivenParameters(const QVariantMap& given) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_MERGE_H
