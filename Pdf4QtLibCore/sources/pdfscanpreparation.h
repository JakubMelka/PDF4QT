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

#ifndef PDFSCANPREPARATION_H
#define PDFSCANPREPARATION_H

#include "pdfocrmodel.h"
#include "pdfdocument.h"

#include <QImage>
#include <QSizeF>
#include <QRectF>
#include <QTransform>

#include <map>
#include <array>
#include <vector>
#include <optional>

namespace pdf
{
class PDFPage;
class PDFOCRPagePreparer;
class PDFOperationControl;

/// Preparation of the scanned pages before the recognition (phases 4 and 5 of
/// OCR_PLAN.md): crop, split of the spreads of a book and permanent deskew. All
/// operations are lossless, no image is re-encoded:
///   - the crop is a new /CropBox (the MediaBox is kept, so the crop can be undone),
///   - the split creates a clone of the page dictionary, which shares the content
///     streams and the resources with the original page (the image stays in the file
///     once), each half has its own /CropBox,
///   - the deskew encloses the content of the page into two page specific streams
///     "q <rotation around the center of the crop box> cm" ... "Q".
///
/// Geometry of the plan is given in the visible coordinates of the source page:
/// the crop box rotated by /Rotate, origin in the top-left corner, y axis grows
/// downwards, unit is a point of the default user space.
class PDF4QTLIBCORESHARED_EXPORT PDFScanPreparation
{
public:
    /// Resolution of the rendering of the analysis
    static constexpr double AnalysisDpi = 100.0;

    /// Resolution of the stored mask of the ink (for the automatic crop)
    static constexpr double MaskDpi = 25.0;

    /// Maximal skew angle detected and corrected (degrees)
    static constexpr double MaximumDeskewAngle = 10.0;

    /// Orientation of the split of a spread
    enum class SplitOrientation
    {
        SideBySide,         ///< Two pages side by side (vertical split line)
        OneAboveAnother     ///< Two pages one above another (horizontal split line)
    };

    /// Candidate of the split line of a spread
    struct GutterCandidate
    {
        double position = 0.0;      ///< Position of the split line in visible points (x for side by side, y otherwise)
        double confidence = 0.0;    ///< 0-100
        bool isShadow = false;      ///< Dark shadow of the spine (otherwise a white gap)
    };

    /// Result of the analysis of a page
    struct PDF4QTLIBCORESHARED_EXPORT PageAnalysis
    {
        PDFInteger pageIndex = -1;
        PDFOCRPageContentClass contentClass = PDFOCRPageContentClass::Unknown;
        bool isScan = false;            ///< The page is a scan (image, possibly with text); deskew is offered by default
        bool hasOwnOCRLayer = false;    ///< Own OCR layer of PDF4QT
        bool hasText = false;           ///< Digital or foreign invisible text
        bool isTagged = false;          ///< The page is a part of the structure tree (/StructParents)
        bool hasAnnotations = false;
        bool hasUnbalancedContent = false; ///< Content with Q/ET/EMC without the opening operator (deskew is refused)

        QSizeF visibleSize;             ///< Size of the visible page in points

        double skewAngle = 0.0;         ///< Skew of the content in degrees, positive clockwise on the visible page
        double skewConfidence = 0.0;    ///< Confidence of the skew 0-100

        QRectF contentRect;             ///< Bounding rectangle of the ink (visible points), empty if none

        std::optional<GutterCandidate> gutterSideBySide;
        std::optional<GutterCandidate> gutterOneAboveAnother;

        /// Mask of the ink at MaskDpi (Format_Mono, 1 = ink), the dark regions connected
        /// with the border of the scan (edges of the scanner, shadows) are not ink
        QImage inkMask;

        QStringList notes;

        bool isValid() const { return pageIndex >= 0 && visibleSize.isValid(); }
    };

    /// Analyzes the page: skew, content rectangle, gutter candidates, class of the page
    /// \param preparer Preparer of the document (rendering environment)
    /// \param document Document
    /// \param pageIndex Page index
    /// \param operationControl Cancellation
    static PageAnalysis analyzePage(const PDFOCRPagePreparer& preparer, const PDFDocument* document, PDFInteger pageIndex, const PDFOperationControl* operationControl);

    /// Analyzes the rendered image of the visible page (skew, content, gutters)
    /// \param image Rendered visible page
    /// \param dpi Resolution of the image
    /// \param analysis Analysis to fill
    static void analyzeImage(const QImage& image, double dpi, PageAnalysis& analysis, const PDFOperationControl* operationControl);

    /// Returns the size of the visible page in points
    static QSizeF getVisibleSize(const PDFPage* page);

    /// Returns the transformation from the canonical page space to the visible coordinates
    static QTransform getPageToVisible(const PDFPage* page);

    /// Returns the two halves of the split in visible coordinates
    /// \param visibleSize Size of the visible page
    /// \param orientation Orientation of the split
    /// \param position Position of the split line (visible points)
    /// \param gutterWidth Width of the gap removed around the split line (points)
    static std::array<QRectF, 2> computeSplit(QSizeF visibleSize, SplitOrientation orientation, double position, double gutterWidth);

    /// Returns the crop rectangle enclosing the ink inside the region after the deskew,
    /// enlarged by the margin (visible points). Returns the region, if there is no ink.
    /// \param analysis Analysis of the page (ink mask)
    /// \param region Region of the visible page (empty = whole page)
    /// \param deskewAngle Skew corrected by the deskew (degrees, see PageAnalysis::skewAngle)
    /// \param margin Margin around the content (points)
    static QRectF computeContentCrop(const PageAnalysis& analysis, const QRectF& region, double deskewAngle, double margin);

    /// Output page: a part of a source page with its crop and deskew
    struct OutputPage
    {
        PDFInteger sourcePageIndex = -1;

        /// Visible rectangle of the source page (empty = the current crop box of the page)
        QRectF visibleRect;

        /// Skew corrected by the rotation of the content (degrees, positive clockwise
        /// on the visible page, i.e. PageAnalysis::skewAngle); 0 = no deskew
        double deskewAngle = 0.0;

        /// Region of the source page, which is the output page before its crop: the half
        /// of a split spread, empty = the whole visible page. The deskew rotates the
        /// content around the center of the region (the same center is used by
        /// computeContentCrop), so the crop can be computed before the rotation.
        QRectF regionRect;

        bool operator==(const OutputPage&) const = default;
    };

    /// What happens with a changed page, which has the own OCR layer (decided by the user)
    enum class LayerAction
    {
        Skip,           ///< The page is not changed
        RemoveLayer     ///< The OCR layer is removed and the page is changed (recognize it again)
    };

    struct Plan
    {
        /// Output pages in their order. Every source page must have at least one output
        /// page and the source pages must stay in their order (the halves of a spread can
        /// be swapped, for example for a book read from the right to the left).
        std::vector<OutputPage> pages;

        /// Decisions for the changed pages with the own OCR layer (missing = Skip)
        std::map<PDFInteger, LayerAction> layerActions;
    };

    /// Creates the plan, which does not change anything (one output page per page)
    static Plan createIdentityPlan(const PDFDocument* document);

    /// Returns true, if the output pages of the source page change the page
    static bool isSourceChanged(const PDFDocument* document, const Plan& plan, PDFInteger sourcePageIndex);

    /// Returns the changed source pages, which have the own OCR layer (the user is asked)
    static std::vector<PDFInteger> getChangedPagesWithOCRLayer(const PDFDocument* document, const Plan& plan);

    struct PDF4QTLIBCORESHARED_EXPORT Result
    {
        PDFDocumentPointer document;
        QString errorMessage;
        QStringList warnings;
        std::vector<PDFInteger> removedLayers;  ///< Source pages, whose OCR layer was removed
        std::vector<PDFInteger> skippedPages;   ///< Changed source pages left unchanged (OCR layer)
        int croppedPages = 0;
        int deskewedPages = 0;
        int splitPages = 0;                     ///< Source pages split into more pages
        int pageCount = 0;                      ///< Number of the pages of the result

        bool isSuccess() const { return errorMessage.isEmpty(); }

        /// Human readable summary
        QString getSummary() const;
    };

    /// Applies the plan. The source document is not modified. Returns an empty
    /// document (no error), if the plan does not change anything.
    static Result apply(const PDFDocument* document, const Plan& plan, const PDFOperationControl* operationControl);

    /// Returns the matrix of the deskew in the canonical page space (the "cm" of the
    /// wrapper): the rotation of the content by the opposite of the skew around the
    /// center of the region
    /// \param region Region of the output page (canonical page space)
    /// \param deskewAngle Skew of the content (degrees, positive clockwise on the visible page)
    static QTransform getDeskewMatrix(const QRectF& region, double deskewAngle);
};

}   // namespace pdf

#endif // PDFSCANPREPARATION_H
