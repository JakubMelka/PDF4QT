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

#ifndef PDFDOCUMENTTOOL_INSERT_H
#define PDFDOCUMENTTOOL_INSERT_H

#include "pdfdocumenttool.h"
#include "pdfdocumentmerger.h"

#include <functional>
#include <optional>

namespace pdf
{

/// The shared core of Insert Pages and Add Image as Page (plan of step 3 of phase 2, 4.1, 5.7,
/// 5.8, Q3 a, R8, K57: Add Image is a preset of Insert - one place of the insertion, one way to
/// the merger): the pages go into the document of the tab by PDFDocumentMerger::insert, which
/// keeps its object numbers, /ID, encryption, signatures, output intents and declarations, so a
/// signed document is saved by an incremental update (Signature IncrementalSave).
///
/// Inputs: context.documents[0] is the target; the further documents and the images of the
/// context are the sources, ordered by the parameter "inputs" (the input ids separated by commas,
/// as Merge; empty = the documents, then the images, in the order of the context).
///
/// Place of the insertion ("position", "page"): before / after the page "page" (from 1), at the
/// start, at the end, or (Insert only) in place of the pages "replacePages" (388:17167: they are
/// removed by PDFDocumentPageOperations::removePages and the new pages go to the place of the
/// first of them, one operation - B20).
///
/// Permissions (Q4, R4): the target needs Assemble (bit 11, the catalog), each source the rule
/// Q2 of step 2 (PDFDocumentMerger::getPermission; its pages lose its restrictions). A certified
/// target (DocMDP, Q12, R10) is changed as for a copy: no level of /P allows a change of the pages,
/// so the result has no signature values, /Perms and /DSS and says so (data "signatureImpact"
/// "lost-copy-only", the fact "certification"); the caller offers only Save as a copy.
///
/// Result: Success - the changed document (Kind::Changed, PDFModifiedDocument::Reset); facts
/// "inserted" (Insert, 388:18134 "6 pages after page 12") or "added" (Add Image, 388:20121
/// "2 images after page 12"), "document" ("54 pages (was 48)"), when they happened "replaced",
/// "fieldsRenamed", "signatures", "tags", "conformance" (Q18: "Inserted pages were not checked for
/// PDF/A"), "certification", "leftOut"; data "insertedCount", "insertedPages" (numbers from 1),
/// "pageCount", "pageCountBefore", "replacedPages", "signatureCount", "signatureImpact" and the
/// counters of the merger. A source, whose copy failed during the run, is in leftOutInputs and the
/// result is partial (owner answer Q5 of step 2); no page left = Failed.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInsertBase : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolInsertBase)

public:
    /// Names of the shared parameters
    static constexpr const char* PARAMETER_POSITION = "position";
    static constexpr const char* PARAMETER_PAGE = "page";
    static constexpr const char* PARAMETER_INPUTS = "inputs";

    /// Identifiers of the places of the insertion (388:16103 Position)
    static constexpr const char* POSITION_BEFORE = "before";
    static constexpr const char* POSITION_AFTER = "after";
    static constexpr const char* POSITION_START = "start";
    static constexpr const char* POSITION_END = "end";
    static constexpr const char* POSITION_REPLACE = "replace";

    /// Most sources of one insertion (as the inputs of Merge)
    static constexpr int MAXIMUM_SOURCE_COUNT = 1000;

    /// Default margins of an image page in millimeters (388:18837 "Margins 10 mm", K35)
    static constexpr double DEFAULT_MARGINS_MILLIMETERS = 10.0;

    /// The place of an insertion in the target
    struct Placement
    {
        PDFInteger position = 0;                ///< Index of the page of the target, before which the pages go (before the replaced ones are removed)
        std::vector<PDFInteger> replacedPages;  ///< Replaced pages (indices from 0, sorted)
        PDFInteger neighborPage = 0;            ///< The page, whose size "Same as page N" takes (index from 0)
        QString positionId;                     ///< POSITION_*
        PDFInteger pageNumber = 1;              ///< The page of before / after (from 1)
    };

    /// The place of the insertion of the normalized parameters in \p target; std::nullopt and the
    /// translated reason for a page, which the document does not have, Replace without a page or
    /// with every page
    std::optional<Placement> resolvePlacement(const PDFDocument* target, const QVariantMap& parameters, QString* errorMessage) const;

    /// An input of the insertion (a source) in its order
    struct SourceInput
    {
        bool isImage = false;           ///< context.images[index], else context.documents[index] (index >= 1)
        size_t index = 0;               ///< Index in the vector of the context
        QString inputId;                ///< Id of the input
        QString name;                   ///< Name of the file
        PDFInteger pageCount = 0;       ///< Pages (frames of an image); 0 = it cannot be read
    };

    /// The sources of the context in the order of "inputs" (PDFDocumentToolMerge::resolveInputs over
    /// the context without the target); the reserved id BLANK_INPUT_ID ("blank") is the place of the
    /// blank pages of Insert and is skipped here (\p blankIndex: its place among the sources, -1 = none)
    static std::optional<std::vector<SourceInput>> resolveSources(const PDFDocumentToolContext& context, const QVariantMap& parameters, int* blankIndex, QString* errorMessage);

    /// Reserved input id of the blank pages in "inputs" (Insert)
    static constexpr const char* BLANK_INPUT_ID = "blank";

    /// The layout of an image page (5.8, K35): the size \p pageSize (empty = Fit to image),
    /// Auto orientation, the margins, the scaling, the anchor, the turn of the image
    static PDFImagePageLayout createImageLayout(QSizeF pageSize, double marginsMillimeters, PDFImagePageLayout::Scaling scaling, Qt::Alignment anchor, PageRotation turn);

    /// The size, which "Same as page N" takes: the media box of the page as it is shown (its
    /// rotation applied) - an image page has no /Rotate (5.8)
    static QSizeF getShownPageSize(const PDFDocument* document, PDFInteger pageIndex);

    /// Text of the place of the inserted pages for the facts: "after page 12", "before page 12",
    /// "at the beginning", "at the end", "in place of pages 13–14"
    static QString getPlacementText(const Placement& placement);

    /// The replaced pages (indices from 0, sorted) as the frames say them (388:17167 "13–14"):
    /// runs of two or more pages are ranges with an en dash, separated by ", " ("2, 5–9"). One
    /// text for the facts of the core and the texts of the GUI; \p formatNumber formats a number
    /// of a page from 1 (the GUI: the locale of the application), nullptr = ASCII digits
    static QString formatPageRuns(const std::vector<PDFInteger>& pageIndices, const std::function<QString(PDFInteger)>& formatNumber = nullptr);

    virtual PDFDocumentToolInputCountRange getInputCountRange() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The sources and the items of an insertion
    struct InsertionPlan
    {
        std::vector<PDFDocumentMerger::Source> sources;
        std::vector<PDFDocumentMerger::Item> items;
        std::vector<SourceInput> inputs;        ///< The inputs of the sources (one per source of a document or an image; blank sources have none)
        std::vector<int> inputOfSource;         ///< Index of the input of each source, -1 for the blank pages
        qint64 imageCount = 0;                  ///< Image sources
    };

protected:
    /// The plan of the tool for its parameters; std::nullopt and the translated reason for invalid ones
    virtual std::optional<InsertionPlan> createInsertionPlan(const PDFDocumentToolContext& context, const QVariantMap& parameters, const Placement& placement, QString* errorMessage) const = 0;

    /// The fact of the inserted pages ("inserted": "6 pages after page 12")
    virtual PDFDocumentToolFact createInsertedFact(const InsertionPlan& plan, qint64 insertedCount, const Placement& placement) const = 0;

    /// Suffix of the suggested name of the changed document ("_inserted")
    virtual QString getFileNameSuffix() const = 0;

    /// The shared parameters "position" and "page" (\p hasReplace: the choice Replace of Insert)
    static void addPlacementParameters(PDFDocumentToolParameterDefinitions* definitions, bool hasReplace);

    /// The shared parameter "inputs"
    static void addInputsParameter(PDFDocumentToolParameterDefinitions* definitions);
};

/// The tool Insert Pages (pages.insert, Figma 388:15487 ... 398:28602): pages of PDF files, open
/// documents, images (one page per frame, the layout of Add Image, K35) and blank pages, inserted
/// at a place of the document of the tab.
///
/// Parameters: the shared ones (position, page, inputs), "replacePages" (the pages of Replace,
/// the selection of Thumbnails), "sourcePages" (the ordered pages of each source in the order of
/// the inputs, separated by ";": PDFPageRangeList "1-5, 7, 10-, odd, even", empty or "all" = all
/// pages, K23), "blankPages" (count of blank pages, 0 to 10 000; their place is the id "blank" in
/// "inputs", else after the sources), "blankSize" (same-as-page = the media box and /Rotate of the
/// neighboring page, 388:16811 "As page 12"; or a named size), "blankOrientation" (of a named size).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolInsertPages : public PDFDocumentToolInsertBase
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolInsertPages)

public:
    static constexpr const char* TOOL_ID = "pages.insert";

    static constexpr const char* PARAMETER_REPLACE_PAGES = "replacePages";
    static constexpr const char* PARAMETER_SOURCE_PAGES = "sourcePages";
    static constexpr const char* PARAMETER_BLANK_PAGES = "blankPages";
    static constexpr const char* PARAMETER_BLANK_SIZE = "blankSize";
    static constexpr const char* PARAMETER_BLANK_ORIENTATION = "blankOrientation";

    /// Identifiers of the sizes of the blank pages
    static constexpr const char* SIZE_SAME_AS_PAGE = "same-as-page";
    static constexpr const char* ORIENTATION_PORTRAIT = "portrait";
    static constexpr const char* ORIENTATION_LANDSCAPE = "landscape";

    /// Most blank pages of one insertion (as Blank PDF)
    static constexpr qint64 MAXIMUM_BLANK_PAGES = 10000;

    /// Longest text of "sourcePages"
    static constexpr int MAXIMUM_SOURCE_PAGES_LENGTH = 65536;

    /// The page ranges of "sourcePages" by the order of the inputs (an empty or missing part = all
    /// pages); std::nullopt and the translated reason ("Annex_A.pdf: Page 60 does not exist ...")
    static std::optional<std::vector<std::vector<PDFInteger>>> parseSourcePages(const QString& text, const std::vector<SourceInput>& inputs, QString* errorMessage);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;

protected:
    virtual std::optional<InsertionPlan> createInsertionPlan(const PDFDocumentToolContext& context, const QVariantMap& parameters, const Placement& placement, QString* errorMessage) const override;
    virtual PDFDocumentToolFact createInsertedFact(const InsertionPlan& plan, qint64 insertedCount, const Placement& placement) const override;
    virtual QString getFileNameSuffix() const override;
};

/// The tool Add Image as Page (pages.add-image, Figma 388:18837 ... 388:20121): every image (each
/// frame of a multi-page TIFF) becomes one page at a place of the document of the tab, laid out by
/// PDFImagePageLayout (5.8): "pageSize" (same-as-page, fit-to-image, a4, a3, letter, custom with
/// "width" x "height" in millimeters), "margins" (millimeters, at most half of the shorter side),
/// "imageScaling" (fit, fill, stretch, original-size, K15), "anchor" (9 places, never mirrored),
/// "turns" (the turns of the images clockwise in the order of the inputs: "90,0,270"). The
/// orientation follows each image (388:18837 "Orientation follows each image").
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolAddImage : public PDFDocumentToolInsertBase
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolAddImage)

public:
    static constexpr const char* TOOL_ID = "pages.add-image";

    static constexpr const char* PARAMETER_PAGE_SIZE = "pageSize";
    static constexpr const char* PARAMETER_WIDTH = "width";
    static constexpr const char* PARAMETER_HEIGHT = "height";
    static constexpr const char* PARAMETER_MARGINS = "margins";
    static constexpr const char* PARAMETER_IMAGE_SCALING = "imageScaling";
    static constexpr const char* PARAMETER_ANCHOR = "anchor";
    static constexpr const char* PARAMETER_TURNS = "turns";

    /// Identifiers of the sizes of the page (388:19607 Page)
    static constexpr const char* SIZE_SAME_AS_PAGE = "same-as-page";
    static constexpr const char* SIZE_FIT_TO_IMAGE = "fit-to-image";
    static constexpr const char* SIZE_CUSTOM = "custom";

    /// Identifiers of the scaling (356:9556 Image, K15)
    static constexpr const char* SCALING_FIT = "fit";
    static constexpr const char* SCALING_FILL = "fill";
    static constexpr const char* SCALING_STRETCH = "stretch";
    static constexpr const char* SCALING_ORIGINAL_SIZE = "original-size";

    /// Identifiers of the anchors (391:23821)
    static constexpr const char* ANCHOR_MIDDLE_CENTER = "middle-center";

    /// The scaling of an identifier, std::nullopt for an unknown one
    static std::optional<PDFImagePageLayout::Scaling> getScaling(const QString& id);

    /// The anchor of an identifier ("top-left" ... "bottom-right"), std::nullopt for an unknown one
    static std::optional<Qt::Alignment> getAnchor(const QString& id);

    /// Identifiers of all anchors in the order of the grid (rows top to bottom)
    static QStringList getAnchorIds();

    /// The turns of "turns" for \p count images ("90,0,270"; missing = no turn); std::nullopt and
    /// the reason for a value, which is not 0, 90, 180 or 270
    static std::optional<std::vector<PageRotation>> parseTurns(const QString& text, size_t count, QString* errorMessage);

    /// Text of turns, which parseTurns reads back
    static QString createTurns(const std::vector<PageRotation>& turns);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolInputCountRange getInputCountRange() const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;

protected:
    virtual std::optional<InsertionPlan> createInsertionPlan(const PDFDocumentToolContext& context, const QVariantMap& parameters, const Placement& placement, QString* errorMessage) const override;
    virtual PDFDocumentToolFact createInsertedFact(const InsertionPlan& plan, qint64 insertedCount, const Placement& placement) const override;
    virtual QString getFileNameSuffix() const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_INSERT_H
