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

#ifndef PDFIMAGECOMPRESSOR_H
#define PDFIMAGECOMPRESSOR_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfcms.h"
#include "pdfprogress.h"

#include <QImage>
#include <QPointF>

#include <optional>
#include <limits>
#include <memory>
#include <vector>
#include <functional>

namespace pdf
{
class PDFImage;
class PDFDocument;
class PDFFontCache;
class PDFICCProfileCache;
class PDFFontCacheShrinkGuard;
class PDFOperationControl;
class PDFOptionalContentActivity;

/// Helper responsible for gathering image statistics needed for compression.
///
/// The walks over the content of the pages (step 4 of phase 2, R4-4):
///  - collectImagePlacements, visitImages, visitImagesInParallel and collectImages (Compress,
///    Optimize Images, Grayscale, Black & White) and PDFDocumentImageList walk by
///    Environment::processPageImages without an activity of the layers: the images of every layer
///    are found, also of a layer hidden by /D or by the automatic states of /Export (ISO 32000-2,
///    8.11.4.4) - a changed document must not keep a color image, which a shown layer brings back.
///    Their optional content (/OC) is never changed. Inline images are reported by the walk, but the
///    changing walks skip them (they are a part of the content, R4-8). The walks follow the content
///    of the page, its forms and its tiling patterns, never the appearance streams of the
///    annotations (a signature field is a part of its signature, a stamp has its own /AP) and never
///    the glyphs of Type 3 fonts (the text is ignored).
///  - A color space named by a resource (an inline image by Table 91, an image XObject of a file,
///    which names a resource instead of a family) is resolved by the resources of the content, which
///    draws it (DrawnImage::colorSpace), so the decoding and the color of PDFDocumentImageList read
///    the same space as the page.
///  - Environment::processPage and processPagePlacements are the older walks of the OCR compression:
///    they skip the content hidden by the layers of an export (OCUsage::Export) and inline images.
class PDF4QTLIBCORESHARED_EXPORT PDFImageCompressor
{
public:
    struct ImageStatistics
    {
        PDFObjectReference reference;
        QImage image;
        QPointF minimalDpi = QPointF(std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity());
    };

    using ImageStatisticsList = std::vector<ImageStatistics>;

    /// Where an image XObject is drawn, known without decoding it: its largest size on
    /// the pages decides its resolution (the pixels are read when the image is decoded)
    struct ImagePlacement
    {
        PDFObjectReference reference;   ///< Reference of the image stream
        QPointF maximumInches;          ///< Largest length of the axes of the image on the pages, in inches (0 = degenerated on all pages)
        PDFInteger firstPageIndex = 0;  ///< First page, which draws the image
        PDFObject colorSpace;           ///< The declared color space of the first drawing, resolved (DrawnImage::colorSpace: a name of a resource of the content is replaced by its value) - the color of PDFDocumentImageList
    };

    /// Placements of the images, sorted by the reference
    using ImagePlacements = std::vector<ImagePlacement>;

    /// Image XObject drawn on a page: the image, its stream, its reference and its
    /// resolution in the user space of the page per axis (0, if the axis is degenerated)
    using ImageCallback = std::function<void(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference, QPointF dpi)>;

    /// Image XObject about to be drawn, nothing is decoded: its reference and the
    /// length of its axes on the page in inches (0, if the axis is degenerated)
    using PlacementCallback = std::function<void(PDFObjectReference reference, QPointF inches)>;

    /// Says, whether the image XObject of the reference is to be decoded
    using ImageFilter = std::function<bool(PDFObjectReference reference)>;

    /// One decoded image of visitImages. The image is the only reference to its pixels;
    /// when the callback does not keep it, it is released before the next image is decoded.
    using DecodedImageCallback = std::function<void(ImageStatistics statistics)>;

    /// An image drawn by the content of a page, an image XObject or an inline image, nothing
    /// decoded (step 4 of phase 2, PDFDocumentImageList). Valid only during the callback.
    class DrawnImage
    {
    public:
        PDFObjectReference reference;           ///< Reference of the image XObject; invalid for an inline image
        PDFObjectReference inlineContentStream; ///< An inline image: the page or the form XObject of its content (PDFPageContentProcessor::InlineImagePosition), invalid otherwise
        PDFInteger inlineIndex = -1;            ///< An inline image: the index of its BI in that content, -1 for an image XObject
        const PDFStream* stream = nullptr;      ///< The image stream (an inline image: a stream made of its dictionary and data)
        PDFObject colorSpace;                   ///< The declared color space resolved: a name of a resource of the content, which draws the image (an inline image, Table 91, or an image XObject naming a resource instead of a family), is replaced by its value; a family name and an array as they are (null = none). The walk reports the images of all layers (processPageImages without an activity)
        QPointF inches;                         ///< Length of the axes of the image on the page in inches (0 = degenerated)
    };

    /// Called for every image drawn by the content of a page (DrawnImage)
    using DrawnImageCallback = std::function<void(const DrawnImage& image)>;

    /// Resources of the content processor for the collection of the images drawn
    /// on the pages of a document (optional content, font cache, color management).
    /// The environment can be used by more threads processing different pages at once.
    class PDF4QTLIBCORESHARED_EXPORT Environment
    {
    public:
        /// \param document Processed document
        /// \param cms Color management system decoding the images; if null, the color
        ///        management system with the default settings is used
        explicit Environment(const PDFDocument* document, PDFCMSPointer cms = PDFCMSPointer());
        ~Environment();

        Environment(const Environment&) = delete;
        Environment& operator=(const Environment&) = delete;

        /// Calls the callback for every image XObject drawn on the page by its content,
        /// by forms and by tiling patterns. Inline images and direct streams are skipped,
        /// they are part of the content and cannot be shared or replaced. The content hidden
        /// by the layers of an export (OCUsage::Export) is skipped too (the OCR compression);
        /// the walks of the changing tools use processPageImages (all layers, R4-4).
        /// \param pageIndex Index of the page
        /// \param callback Callback called for every drawn image
        /// \param operationControl Cancellation (optional)
        /// \param filter Images, which are decoded and reported (optional, default all);
        ///        the others are skipped without decoding them
        void processPage(PDFInteger pageIndex, const ImageCallback& callback, const PDFOperationControl* operationControl, const ImageFilter& filter = ImageFilter()) const;

        /// Calls the callback for every image XObject drawn on the page, without decoding any image;
        /// as processPage, the content hidden by the layers of an export is skipped
        /// \param pageIndex Index of the page
        /// \param callback Callback called for every drawn image
        /// \param operationControl Cancellation (optional)
        void processPagePlacements(PDFInteger pageIndex, const PlacementCallback& callback, const PDFOperationControl* operationControl) const;

        /// The collection options of step 4 of phase 2 (PDFDocumentImageList): calls the callback
        /// for every image drawn by the content of the page, by its forms and its tiling patterns,
        /// image XObjects and inline images alike, without decoding any of them. Glyphs of Type 3
        /// fonts are never visited (the text is ignored) and the appearances of the annotations
        /// are not a part of the content of the page (R4-4). A direct image stream without a
        /// reference, which is not an inline image, is skipped (ISO 32000-2, 7.3.8: streams are
        /// indirect objects).
        /// \param pageIndex Index of the page
        /// \param callback Callback called for every drawn image
        /// \param optionalContentActivity The layers: nullptr = every layer is visible (the images
        ///        of all layers, R4-4, R4-11); otherwise the content suppressed by its states is
        ///        skipped (the states of the tab, the hidden layers of R4-11)
        /// \param operationControl Cancellation (optional)
        void processPageImages(PDFInteger pageIndex, const DrawnImageCallback& callback, const PDFOptionalContentActivity* optionalContentActivity, const PDFOperationControl* operationControl) const;

        /// Returns the color management system decoding the images
        const PDFCMS* getCMS() const { return m_cms.data(); }

        /// Returns the processed document
        const PDFDocument* getDocument() const { return m_document; }

        /// Returns the cache of the fonts of the walk
        const PDFFontCache* getFontCache() const { return m_fontCache.get(); }

        /// Returns the layers of the walk of processPage and processPagePlacements (OCUsage::Export)
        const PDFOptionalContentActivity* getOptionalContentActivity() const { return m_optionalContentActivity.get(); }

    private:
        const PDFDocument* m_document;
        PDFCMSPointer m_cms;
        std::unique_ptr<PDFOptionalContentActivity> m_optionalContentActivity;
        std::unique_ptr<PDFFontCache> m_fontCache;
        std::unique_ptr<PDFICCProfileCache> m_iccProfileCache;
    };

    /// Collects all image XObjects from the document and computes their
    /// analysis data required for compression decisions. All decoded images are
    /// returned, so the memory grows with their number: a caller, which only needs
    /// them one after another, uses collectImagePlacements and visitImages.
    /// \param document Processed document
    /// \param operationControl Cancellation (optional), a canceled collection returns
    ///        the images decoded so far
    /// \param progress Progress of the walk over the pages, then of the decoding (optional)
    /// \return Collected statistics for every unique image reference
    ImageStatisticsList collectImages(const PDFDocument* document,
                                      const PDFOperationControl* operationControl = nullptr,
                                      PDFProgressSink* progress = nullptr) const;

    /// As collectImages, in the given environment
    ImageStatisticsList collectImages(const Environment& environment,
                                      const PDFOperationControl* operationControl = nullptr,
                                      PDFProgressSink* progress = nullptr) const;

    /// First pass over the pages (in the threads of PDFExecutionPolicy): finds the image XObjects
    /// drawn by the document and their placements, the result does not depend on the threads.
    /// Nothing is decoded, the memory does not depend on the images.
    /// \param environment Environment of the processed document
    /// \param operationControl Cancellation, checked per page and per operator (optional)
    /// \param progress One step per page (optional)
    /// \return Placements sorted by the reference; incomplete, when canceled
    ImagePlacements collectImagePlacements(const Environment& environment,
                                           const PDFOperationControl* operationControl = nullptr,
                                           PDFProgressSink* progress = nullptr) const;

    /// Second pass in the threads of PDFExecutionPolicy (Scope::Page): the pages, which draw an image for
    /// the first time, are processed at once, each task holds a decoded image only while the callback
    /// works on it, nothing collects them: the peak is about the threads x the largest image, never the
    /// sum of all images. The strategy Single thread (Settings › Advanced › Multithreading strategy)
    /// decodes strictly one image at a time in the calling thread. One thread of the pool stays free for the
    /// pages of the canvas. An image is decoded only on its first page, so its result does not depend
    /// on the threads; an image, which gives no pixels there, is tried on its other pages afterwards
    /// (one thread, as visitImages).
    /// \param environment Environment of the processed document
    /// \param placements Result of collectImagePlacements
    /// \param callback Called for each image, by more threads at once in any order: it is thread-safe,
    ///        it must not let a PDFException out (see visitImages)
    /// \param operationControl Cancellation, checked before each image (optional)
    void visitImagesInParallel(const Environment& environment,
                               const ImagePlacements& placements,
                               const DecodedImageCallback& callback,
                               const PDFOperationControl* operationControl) const;

    /// Second pass: decodes the images one at a time and hands each of them to the
    /// callback, with its resolution taken from the placements, before the next image
    /// is decoded. Only pages, which draw an image for the first time, are processed.
    /// An image, which cannot be decoded on its first page, is tried on its other
    /// pages; when it never gives pixels, the callback gets it with a null image,
    /// unless its data cannot be read at all (then it is not reported, as by collectImages).
    /// Runs inside of the content processor: the callback must not let a PDFException
    /// out, the processor would report it as an error of the page and go on.
    /// \param environment Environment of the processed document
    /// \param placements Result of collectImagePlacements
    /// \param callback Called for each image, in the order of the pages
    /// \param operationControl Cancellation, checked before each image (optional)
    void visitImages(const Environment& environment,
                     const ImagePlacements& placements,
                     const DecodedImageCallback& callback,
                     const PDFOperationControl* operationControl = nullptr) const;

    /// Test only: replaces each decoded image (an image, whose pixels could not be allocated - a null
    /// image - cannot be caused otherwise). Set it before a run, an empty function removes it.
    using TestDecodeHook = std::function<QImage(PDFObjectReference reference, QImage image)>;
    static void setTestDecodeHook(TestDecodeHook hook);

    /// Returns the smaller of the resolutions of an axis; values, which are not
    /// positive or finite, are ignored
    static double updateAxisDpi(double currentValue, double candidate);

private:
    class ImageCollectorProcessor;

    /// Decodes one image: nullopt when nothing is left of it (canceled, no pixels)
    static std::optional<ImageStatistics> decodeImage(const Environment& environment, const PDFImage& image, PDFObjectReference reference, const ImagePlacement& placement, const PDFOperationControl* operationControl);
};

}   // namespace pdf

#endif // PDFIMAGECOMPRESSOR_H
