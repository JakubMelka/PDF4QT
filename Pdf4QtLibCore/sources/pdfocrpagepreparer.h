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

#ifndef PDFOCRPAGEPREPARER_H
#define PDFOCRPAGEPREPARER_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"
#include "pdfocrengine.h"
#include "pdfocrconfiguration.h"
#include "pdfrenderer.h"

#include <QImage>

namespace pdf
{
class PDFCMS;
class PDFPage;
class PDFDocument;
class PDFFontCache;
class PDFOptionalContentActivity;
struct PDFMeshQualitySettings;

/// Prepares the pages for the recognition (chapter 6, 7.5 and 12.5 of the
/// OCR specification): analyzes the content of the page, renders the working
/// raster, applies the region masks and the preprocessing pipeline and
/// records the complete coordinate mapping.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRPagePreparer
{
public:
    explicit PDFOCRPagePreparer(const PDFDocument* document,
                                const PDFFontCache* fontCache,
                                const PDFCMS* cms,
                                const PDFOptionalContentActivity* optionalContentActivity,
                                const PDFMeshQualitySettings& meshQualitySettings,
                                RendererEngine rendererEngine);

    /// Decision of the existing text policy (chapter 6.2, INPUT-04)
    enum class PolicyDecision
    {
        Recognize,
        Skip,
        NeedsDecision
    };

    /// Evaluates the existing text policy for the page
    /// \param analysis Page analysis
    /// \param policy Policy
    /// \param hasInclusiveRegions Page has user defined inclusive regions
    /// \param reason Human readable reason of the decision
    /// \param regions Regions of the page (optional). An inclusive region overlapping
    ///        the existing text of the page is a collision, which the user must resolve
    ///        before the run (chapter 6.2): the decision is NeedsDecision.
    static PolicyDecision evaluateExistingTextPolicy(const PDFOCRPageAnalysis& analysis,
                                                     PDFOCRExistingTextPolicy policy,
                                                     bool hasInclusiveRegions,
                                                     QString* reason,
                                                     const std::vector<PDFOCRRegion>* regions = nullptr);

    /// Returns the inclusive regions overlapping the existing text of the page (chapter 6.2)
    static std::vector<const PDFOCRRegion*> getRegionsCollidingWithText(const PDFOCRPageAnalysis& analysis, const std::vector<PDFOCRRegion>& regions);

    /// Analyzes the page content (INPUT-01, INPUT-02, INPUT-03).
    PDFOCRPageAnalysis analyze(PDFInteger pageIndex, const PDFOperationControl* operationControl) const;

    /// Returns true, if the document is tagged (has structure tree)
    static bool isTaggedDocument(const PDFDocument* document);

    /// Returns true, if the document declares PDF/A or PDF/UA conformance in the XMP metadata
    static bool hasConformanceDeclaration(const PDFDocument* document, QStringList* declarations);

    /// Returns page label of the page (as displayed), or empty string
    static QString getPageLabel(const PDFDocument* document, PDFInteger pageIndex);

    /// Computes the fingerprint of the page content (excluding the own OCR layer),
    /// so it is stable when own layer is added, replaced or removed.
    static QByteArray computePageFingerprint(const PDFDocument* document, PDFInteger pageIndex);

    /// Computes the fingerprint of the document (from all page fingerprints)
    static QByteArray computeDocumentFingerprint(const PDFDocument* document);

    /// Returns features used for the OCR rasterization (IMAGE-02: no color effects,
    /// no annotations, clipped to the crop box).
    static PDFRenderer::Features getRasterizationFeatures();

    /// Returns size of the raster of the page (rotated crop box) at given resolution
    static QSize getRasterSize(const PDFPage* page, double dpi);

    /// Returns the resolution limited by the maximal pixel count and by the maximal
    /// dimension of the raster (limit of the engine, ARCH-02). If the requested
    /// resolution fits, it is returned unchanged.
    /// \param page Page
    /// \param dpi Requested resolution
    /// \param maximumPixels Maximal pixel count of the raster (0 = unlimited)
    /// \param maximumDimension Maximal width and height of the raster in pixels (0 = unlimited)
    static double getLimitedDpi(const PDFPage* page, double dpi, qint64 maximumPixels, int maximumDimension = 0);

    /// Estimates memory of the raster in bytes (32 bit pixels)
    static qint64 estimateRasterBytes(const PDFPage* page, double dpi);

    /// Default maximal pixel count of the raster
    static constexpr qint64 DefaultMaximumPixels = qint64(80) * 1000 * 1000;

    struct RasterResult
    {
        QImage image;
        PDFOCRPageGeometry geometry;
        PDFOCRError error;
    };

    /// Renders the page into the working raster (IMAGE-01, IMAGE-02, IMAGE-03).
    /// Masked rectangles (in canonical page space) are painted white.
    /// \param pageIndex Page index
    /// \param dpi Requested resolution
    /// \param maskedRectangles Rectangles to be masked (page space)
    /// \param maximumPixels Maximal pixel count of the raster
    /// \param operationControl Operation control
    /// \param maximumDimension Maximal width and height of the raster (limit of the engine, 0 = unlimited)
    RasterResult rasterize(PDFInteger pageIndex,
                           double dpi,
                           const std::vector<QRectF>& maskedRectangles,
                           qint64 maximumPixels,
                           const PDFOperationControl* operationControl,
                           int maximumDimension = 0) const;

    struct PreprocessResult
    {
        QImage image;
        PDFOCRPageGeometry geometry;
        PDFOCRError error;
    };

    /// Applies the preprocessing pipeline (IMAGE-04, IMAGE-05, IMAGE-06). The order
    /// of the pipeline is: orientation (manual/detected) -> deskew -> photometric
    /// filters. Every geometric step is recorded in the geometry.
    static PreprocessResult preprocess(const QImage& raster,
                                       const PDFOCRPageGeometry& geometry,
                                       const PDFOCRPreprocessing& preprocessing,
                                       const std::optional<PDFOCROrientation>& detectedOrientation,
                                       const PDFOperationControl* operationControl);

    /// Applies the region masks to the image (REGION-01, REGION-05): excluded regions
    /// are painted white, and if inclusive regions exist, everything outside of
    /// their union is painted white.
    /// \param image Image
    /// \param pageToImage Transformation from the canonical page space to the image space
    /// \param regions Regions
    static QImage maskRegions(QImage image, const QTransform& pageToImage, const std::vector<PDFOCRRegion>& regions);

    /// Returns rectangles in image space, which should be recognized. If no inclusive
    /// region exists, single rectangle covering the whole image is returned, with region
    /// identifier -1.
    static std::vector<std::pair<int, QRect>> getRecognitionRectangles(const QSize& imageSize,
                                                                        const QTransform& pageToImage,
                                                                        const std::vector<PDFOCRRegion>& regions);

    /// Returns true, if the image is blank (ratio of dark pixels is below the threshold)
    static bool isBlankImage(const QImage& image, double* inkRatio, const PDFOperationControl* operationControl);

    /// Minimal confidence (0-100) of the detected orientation, which is applied (IMAGE-04)
    static constexpr double MinimumOrientationConfidence = 10.0;

    /// Estimates the skew of the text in degrees with confidence 0-100. Positive
    /// angle means content rotated clockwise in the image (the sense of
    /// QTransform::rotate), the content is straightened by the rotation by the
    /// opposite angle. Only small angles (up to +-5 degrees) are estimated.
    static double estimateSkewAngle(const QImage& image, double* confidence, const PDFOperationControl* operationControl);

    /// Converts the raw output of the engine into the page result, transforming
    /// the geometry into the canonical page space (GEOM-02). Words overlapping
    /// excluded regions are flagged (REGION-05). Words duplicating already
    /// present words are skipped (REGION-05).
    /// \param result Page result
    /// \param output Engine output
    /// \param geometry Page geometry
    /// \param regionId Region identifier of the recognized rectangle (-1 = whole page)
    /// \param excludedRectangles Excluded rectangles (page space)
    /// \param outputToEngine Transformation from the space of the engine output into the
    ///        engine image space (identity, if the engine got the whole engine image; a
    ///        rotated or cropped sub-image of a region has its own transformation, REGION-02)
    static void appendOutput(PDFOCRPageResult& result,
                             const PDFOCRRecognitionOutput& output,
                             const PDFOCRPageGeometry& geometry,
                             int regionId,
                             const std::vector<QRectF>& excludedRectangles,
                             const QTransform& outputToEngine = QTransform());

    /// Recomputes the flag "overlapsExcludedRegion" of all words of the result from
    /// the current excluded regions of the result and from the rectangles of the
    /// unapplied redactions (REGION-05, PDF-13). Any intersection with a positive area
    /// counts. Called after every change of the regions or of the geometry.
    static void updateExcludedRegionFlags(PDFOCRPageResult& result);

    /// Returns true, if the rectangle intersects any of the rectangles with a positive
    /// area (a shared edge is not an intersection)
    static bool intersectsAny(const QRectF& rect, const std::vector<QRectF>& rectangles);

    /// Converts rectangle in the image space (y grows downwards) to the quad
    /// in the canonical page space.
    static PDFOCRQuad imageRectToPageQuad(const QRectF& rect, const QTransform& imageToPage);

    /// Converts polygon (bottom-left, bottom-right, top-right, top-left in
    /// image space) to the quad in canonical page space.
    static PDFOCRQuad imagePolygonToPageQuad(const QPolygonF& polygon, const QTransform& imageToPage);

    /// Converts image to 8-bit grayscale
    static QImage toGrayscale(const QImage& image);

    /// Applies 3x3 median filter (mild denoising)
    static QImage medianFilter(const QImage& grayscale, const PDFOperationControl* operationControl);

    /// Applies global Otsu binarization, returns 8-bit grayscale image with values 0/255
    static QImage otsuBinarization(const QImage& grayscale, const PDFOperationControl* operationControl);

    /// Returns true, if image has light text on dark background (mean luminance below 128)
    static bool isDarkBackground(const QImage& grayscale);

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFMeshQualitySettings& m_meshQualitySettings;
    RendererEngine m_rendererEngine;
};

}   // namespace pdf

#endif // PDFOCRPAGEPREPARER_H
