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

#ifndef PDFIMAGESCALING_H
#define PDFIMAGESCALING_H

#include "pdfglobal.h"

#include <QImage>
#include <QSize>
#include <QTransform>

#include <map>
#include <tuple>

namespace pdf
{

/// Downscaling of the images for the display. Raster paint engines resample an image
/// from a 2x2 neighbourhood only, so when an image is shrunk more than twice, most of
/// the source pixels are never sampled at all. Thin strokes of a bitonal scan then
/// either land on a sampling point and stay dark, or fall between the sampling points
/// and wash out. Images must therefore be downscaled to the target resolution before
/// they are drawn, and bitonal images need a downscaler of their own, which averages
/// the ink coverage over the whole area of the destination pixel.
///
/// All functions of this class are pure and reentrant - they can be called from any
/// thread, including the worker threads of the rasterizer pool.
class PDF4QTLIBCORESHARED_EXPORT PDFImageScaling
{
public:
    // This class is a collection of the algorithms, it is never instantiated
    PDFImageScaling() = delete;
    PDFImageScaling(const PDFImageScaling&) = delete;
    PDFImageScaling& operator=(const PDFImageScaling&) = delete;

    /// Kind of the image from the point of view of the downscaling. Bitonal images are
    /// downscaled by the ink coverage downscaler, all other images by the box filter
    /// of Qt.
    enum class ImageType
    {
        Generic,        ///< Regular image, downscaled by the box filter of Qt
        Monochrome,     ///< Image of one bit per pixel with a color table of two entries
        StencilMask     ///< Image mask - a single opaque color over a binary alpha channel
    };

    /// Exponent of the coverage transfer (stem darkening) curve. The ink coverage of a
    /// destination pixel is remapped by the power function before it is converted to a
    /// color. An exponent below 1.0 lifts the partial coverage, so thin strokes do not
    /// wash out to a pale gray, and it keeps the endpoints intact - a fully covered
    /// pixel stays a full ink and an uncovered one stays a blank paper.
    static constexpr PDFReal DEFAULT_INK_GAMMA = 0.65;

    /// Determines the type of the image. It is an expensive function for the images with
    /// an alpha channel (it can scan all the pixels of the image), so determine the type
    /// once, when the image is created, and not each time the image is drawn.
    /// \param image Image
    static ImageType getImageType(const QImage& image);

    /// Returns true, if the image type is a bitonal one
    /// \param imageType Image type
    static bool isBitonal(ImageType imageType) { return imageType != ImageType::Generic; }

    /// Returns true, if an image should be downscaled to the resolution of the paint device
    /// before it is drawn onto it. Bitonal images are downscaled always, when they are drawn
    /// onto a raster paint device - the paint engines cannot shrink them without losing their
    /// thin strokes, so for them it is a matter of the correctness and not of the quality.
    /// Other images are downscaled only, when the smooth images are requested. A bitonal
    /// image is not downscaled for a paint device, which is not a raster one (a pdf writer,
    /// a printer, a picture) - such a device stores the image into a document, and its
    /// resolution (72 dpi of a pdf writer) is not the resolution of the final output.
    /// \param paintDevice Paint device, onto which the image is drawn
    /// \param imageType Type of the image \sa getImageType
    /// \param isSmoothImagesEnabled Are the smooth images requested? \sa PDFRenderer::SmoothImages
    static bool isDownscalingEnabled(const QPaintDevice* paintDevice, ImageType imageType, bool isSmoothImagesEnabled);

    /// Returns the size, to which an image of the given size should be downscaled before it
    /// is drawn using the given transform, or an invalid size, when the image should be
    /// drawn as it is (it is being enlarged, or the transform is skewed, degenerate or not
    /// affine). The transform must be the device transform of the painter and not its world
    /// transform - the world transform is in the logical pixels, so on a display with a
    /// device pixel ratio above one it would downscale the image to a half (or a third, ...)
    /// of the resolution, which the paint device really has, and the paint device would then
    /// have to enlarge it back. \sa QPainter::deviceTransform
    /// \param imageSize Size of the source image
    /// \param deviceTransform Transform, which maps the unit square of the image to the
    ///        real pixels of the paint device
    static QSize getDownscaledSize(QSize imageSize, const QTransform& deviceTransform);

    /// Downscales the image to the target size. Bitonal images are downscaled by the ink
    /// coverage downscaler \p scaleDownBitonal, all other images by the box filter of Qt.
    /// A null image is returned, when the target size is invalid.
    /// \param image Source image
    /// \param targetSize Target size
    /// \param imageType Type of the source image \sa getImageType
    static QImage scaleDown(const QImage& image, QSize targetSize, ImageType imageType);

    /// Downscales a bitonal image by an exact area averaging of the ink coverage and
    /// remaps the coverage by the stem darkening curve, so thin strokes remain visible.
    /// A null image is returned, when the image cannot be downscaled this way - then the
    /// caller should fall back to the box filter of Qt.
    /// \param image Source image
    /// \param targetSize Target size (must not be greater than the size of the image)
    /// \param imageType Type of the source image (must be a bitonal one)
    /// \param inkGamma Exponent of the coverage transfer curve \sa DEFAULT_INK_GAMMA
    static QImage scaleDownBitonal(const QImage& image,
                                   QSize targetSize,
                                   ImageType imageType,
                                   PDFReal inkGamma = DEFAULT_INK_GAMMA);

    /// Relative tolerance of the orthogonality test of the mapped unit vectors
    static constexpr qreal ORTHOGONALITY_TOLERANCE = 1e-6;

    /// Maximal fraction of the ink in the image, for which the stem darkening curve is
    /// applied. An image, which is covered by the ink from a larger part, is not an ink
    /// on a paper (it is a bitonal photograph, a filled seal, ...), and biasing such an
    /// image towards one of its two colors would be arbitrary.
    static constexpr PDFReal MAXIMUM_INK_FRACTION_FOR_DARKENING = 0.4;

    /// Size of the lookup table of the coverage transfer curve
    static constexpr int COVERAGE_LUT_SIZE = 1024;
};

/// Cache of the images downscaled for the drawing of the precompiled pages. A precompiled
/// page is deliberately independent on the zoom, so the downscaled variants of its images
/// cannot be stored in it - they are stored here instead, keyed by the identity of the
/// source image and by the target size.
///
/// The cache is organized into the drawing passes. One drawing pass corresponds to a single
/// repaint of the widget. A pass can draw the same page more than once and at more than one
/// zoom (the magnifier tool draws the pages a second time, magnified), and all the images
/// used anywhere in the pass survive the whole pass. Images, which have not been used for
/// the last \p MAXIMUM_PASS_AGE passes, are discarded at the end of a pass.
///
/// This class is not thread safe. All drawing of the cached precompiled pages happens on the
/// GUI thread, and the Blend2D backend copies the images before it hands them to its worker
/// threads, so no synchronization is needed. Do not share a single cache between the threads -
/// a page drawn on a worker thread (the rasterizer pool) must use no cache at all.
class PDF4QTLIBCORESHARED_EXPORT PDFScaledImageCache
{
public:
    explicit PDFScaledImageCache() = default;

    // The cache holds the images of a single drawing target, it is never copied
    PDFScaledImageCache(const PDFScaledImageCache&) = delete;
    PDFScaledImageCache& operator=(const PDFScaledImageCache&) = delete;

    /// Begins a drawing pass. The calls can be nested - only the outermost pair of the
    /// begin/end calls actually starts and finishes the pass.
    void beginDrawingPass();

    /// Finishes the drawing pass and discards the images, which have not been used
    /// recently enough. \sa beginDrawingPass
    void endDrawingPass();

    /// Returns the image downscaled to the target size. The image is taken from the cache,
    /// if it is stored there; otherwise it is scaled and stored. When no drawing pass is
    /// active, the image is scaled and returned without being stored - such a caller would
    /// never get a cache hit anyway and its images would just linger in the cache.
    /// \param image Source image
    /// \param targetSize Target size
    /// \param imageType Type of the source image \sa PDFImageScaling::getImageType
    QImage getScaledImage(const QImage& image, QSize targetSize, PDFImageScaling::ImageType imageType);

    /// Discards all the stored images
    void clear();

    /// Returns the estimate of the memory consumed by the stored images. This memory is
    /// not a part of the memory consumption estimate of the precompiled pages and it is
    /// not counted into the limit of their cache.
    qint64 getMemoryConsumptionEstimate() const { return m_memoryConsumptionEstimate; }

    /// Returns the number of the stored images
    size_t getImageCount() const { return m_entries.size(); }

    /// Guard of the drawing pass \sa beginDrawingPass
    class DrawingPassGuard
    {
    public:
        explicit inline DrawingPassGuard(PDFScaledImageCache* cache) :
            m_cache(cache)
        {
            m_cache->beginDrawingPass();
        }

        inline ~DrawingPassGuard()
        {
            m_cache->endDrawingPass();
        }

        DrawingPassGuard(const DrawingPassGuard&) = delete;
        DrawingPassGuard& operator=(const DrawingPassGuard&) = delete;

    private:
        PDFScaledImageCache* m_cache;
    };

    /// An image survives, when it has been used in one of the last (age + 1) passes. A zero
    /// would discard everything, which the current pass has not touched, and a partial
    /// repaint (the exposed rectangle can cover a single page only) would then throw away
    /// the images of the pages, which have not been repainted.
    static constexpr quint64 MAXIMUM_PASS_AGE = 2;

    /// Images smaller than this are not stored - scaling them costs microseconds, and an
    /// image inside a tiling pattern is decoded once per tile, so it would otherwise fill
    /// the cache with thousands of entries of an identical content.
    static constexpr int MINIMUM_SOURCE_PIXELS = 64 * 64;

private:
    struct Key
    {
        qint64 imageCacheKey = 0;
        int width = 0;
        int height = 0;

        bool operator<(const Key& other) const
        {
            return std::tie(imageCacheKey, width, height) < std::tie(other.imageCacheKey, other.width, other.height);
        }
    };

    struct Entry
    {
        QImage image;
        quint64 lastUsedPassId = 0;
        qint64 memoryConsumption = 0;
    };

    std::map<Key, Entry> m_entries;
    quint64 m_currentPassId = 0;
    int m_passDepth = 0;
    qint64 m_memoryConsumptionEstimate = 0;
};

}   // namespace pdf

#endif // PDFIMAGESCALING_H
