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

#ifndef PDFBITONALDOCUMENTCREATOR_H
#define PDFBITONALDOCUMENTCREATOR_H

#include "pdfdocument.h"
#include "pdfdocumentimagelist.h"
#include "pdfimage.h"
#include "pdfimageconversion.h"
#include "pdfoperationcontrol.h"
#include "pdfrenderer.h"
#include "pdfprogress.h"

#include <QImage>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace pdf
{
class PDFPage;
class PDFDocumentBuilder;
class PDFRasterizerPool;
class PDFCMSManager;
class PDFFontCache;
class PDFOptionalContentActivity;
struct PDFCMSSettings;

/// Everything a rasterizer pool of a conversion to black and white needs, owned together (step 4
/// of phase 2, package P3: the tool Convert to Black & White, PdfTool bitonal and the tests): the
/// layers by OCUsage::Export (ISO 32000-2, 8.11.4.4 - the layers of the converted pages are merged
/// as for an export, R4-10 a), its own CMS manager of \p cmsSettings (a value - a tool has no
/// manager of a view), the font and ICC caches, the mesh settings and the pool with the features
/// of PDFBitonalDocumentCreator::getPageRasterizationFeatures (the raster engine of QPainter, which
/// works in every thread and on every platform).
///
/// Ownership and threads: a value of the thread, which creates it (the run of a tool, a worker of
/// the GUI, PdfTool): it is created, used and destroyed there; the threads of the pool work with
/// it only inside of the calls of the pool. The document must outlive it and is only read.
class PDF4QTLIBCORESHARED_EXPORT PDFBitonalRasterizationEnvironment
{
public:
    /// \param document The document (read only, it outlives the environment)
    /// \param cmsSettings Color management of the rendering
    /// \param rasterizerCount Number of the rasterizers of the pool
    explicit PDFBitonalRasterizationEnvironment(const PDFDocument* document,
                                                const PDFCMSSettings& cmsSettings,
                                                int rasterizerCount);
    ~PDFBitonalRasterizationEnvironment();

    PDFBitonalRasterizationEnvironment(const PDFBitonalRasterizationEnvironment&) = delete;
    PDFBitonalRasterizationEnvironment& operator=(const PDFBitonalRasterizationEnvironment&) = delete;

    PDFRasterizerPool* getRasterizerPool() const { return m_rasterizerPool.get(); }

private:
    std::unique_ptr<PDFOptionalContentActivity> m_optionalContentActivity;
    std::unique_ptr<PDFCMSManager> m_cmsManager;
    std::unique_ptr<PDFFontCache> m_fontCache;
    std::unique_ptr<PDFICCProfileCache> m_iccProfileCache;
    PDFMeshQualitySettings m_meshQualitySettings;
    std::unique_ptr<PDFRasterizerPool> m_rasterizerPool;
};

/// Creates a bitonal (monochromatic) version of a document. Documents produced by
/// scanners often store a single scanned page as several images - for example a
/// background image and a text layer masked by a stencil mask - and converting such
/// images one by one cannot produce a reasonable bitonal page, because neither of
/// them is a picture of the page. For these documents the whole page composition
/// can be rasterized and converted instead.
///
/// This class performs the conversion itself, it does not depend on any user
/// interface. Rasterizing of the pages is delegated to a rasterizer pool, which the
/// caller provides - it is needed only by the page conversion source.
class PDF4QTLIBCORESHARED_EXPORT PDFBitonalDocumentCreator
{
public:
    /// \param document Source document
    /// \param rasterizerPool Pool used to rasterize the pages. It can be nullptr, when
    ///        only images are converted. It must be created with the features returned
    ///        by \p getPageRasterizationFeatures.
    /// \param progress Progress reporting (can be nullptr)
    explicit PDFBitonalDocumentCreator(const PDFDocument* document,
                                       PDFRasterizerPool* rasterizerPool,
                                       PDFProgressSink* progress);

    /// Source of the bitonal conversion
    enum class ConversionSource
    {
        Images, ///< Each image of the document is converted separately
        Pages   ///< Whole pages are rasterized and converted as a single image
    };

    /// Way, in which a single item (an image or a page) is converted
    enum class ItemMode
    {
        Algorithm,          ///< Converted using the selected conversion method
        AlgorithmInverted,  ///< Converted using the selected conversion method, the black and the white pixels are then swapped
        Original,           ///< Left as it is, the item is not touched at all
        FillBlack,          ///< Replaced by a black area
        FillWhite           ///< Replaced by a white area
    };

    /// Returns true, if the mode converts the item using the conversion method, i.e.
    /// the item has to be decoded or rasterized before it can be converted
    /// \param mode Mode of the item
    static bool isConversionMode(ItemMode mode) { return mode == ItemMode::Algorithm || mode == ItemMode::AlgorithmInverted; }

    /// Compression of the images of the created document. Only the algorithms, which
    /// suit a bitonal image, are offered - a lossy one would destroy the result of the
    /// conversion and it compresses a two-color image poorly anyway.
    enum class Compression
    {
        Auto,           ///< Compress by every algorithm below and keep the smallest result
        Flate,          ///< FlateDecode with the PNG predictor
        RunLength,      ///< RunLengthDecode
        CCITTGroup4,    ///< CCITTFaxDecode, pure two dimensional coding (K = -1)
        JBIG2           ///< JBIG2Decode, a single generic region
    };

    struct ItemInfo
    {
        PDFObjectReference imageReference;  ///< Valid, when images are converted
        PDFInteger pageIndex = -1;          ///< Valid, when pages are converted
        ItemMode mode = ItemMode::Algorithm;

        /// Returns true, if the item is going to be replaced in the converted
        /// document. Items, which are left as they are, are not touched at all.
        bool isContentReplaced() const { return mode != ItemMode::Original; }

        /// Returns true, if the item is replaced by a solid fill
        bool isFilled() const { return mode == ItemMode::FillBlack || mode == ItemMode::FillWhite; }

        /// Returns true, if the item is filled by the black color
        bool isFilledByBlack() const { return mode == ItemMode::FillBlack; }

        /// Returns true, if the black and the white pixels of the converted item are
        /// swapped after the conversion
        bool isInverted() const { return mode == ItemMode::AlgorithmInverted; }
    };

    /// All inputs of the conversion. The structure is copyable and it does not refer
    /// to anything, which can change while the conversion is running, so a user
    /// interface can create a snapshot of its state and hand it over to a worker.
    struct Settings
    {
        ConversionSource conversionSource = ConversionSource::Images;
        PDFImageConversion::ConversionMethod conversionMethod = PDFImageConversion::ConversionMethod::Automatic;
        int manualThreshold = 128;
        int dpiResolution = DEFAULT_DPI_RESOLUTION;
        Compression compression = Compression::Auto;
        std::vector<ItemInfo> items;
    };

    /// Creates the bitonal document. Returns true, if at least one item has been
    /// converted and the resulting document is valid. The created document is then
    /// available using the function \p takeBitonalDocument. This function does not
    /// touch anything but the document and the rasterizer pool, so it can be executed
    /// in a worker thread.
    ///
    /// The settings do not have to be normalized - an item with an invalid page index
    /// or image reference is ignored and an item requested more than once is converted
    /// only once (the mode of its last occurrence is used, even when that mode leaves
    /// the item untouched).
    ///
    /// The result can be partial - an image, which cannot be decoded, or a page, whose
    /// image cannot be created, is left in its original form and the conversion
    /// continues. Use \p getFailedItemCount to find out, whether that has happened.
    /// A page, whose rendering has reported errors, is converted as well (owner
    /// 14. 11. 2026: a rendering error never blocks the conversion); such pages are
    /// listed by \p getRenderingIssues, so the caller asks the user to check them.
    ///
    /// When only some pages of a tagged document are converted, the structure tree
    /// is pruned - the references to the marked content of the converted pages are
    /// removed from it, the elements, which have lost all their content, are removed
    /// as well, and the parent tree and the ID tree are updated accordingly.
    ///
    /// The created document keeps the version and the hash of the source data of the
    /// input (R4-9 of step 4 of phase 2); the conversion of images binds the OCR text
    /// layers of PDF4QT again (PDFOCRTextLayerWriter::rebindOptimizedDocument - only the
    /// samples of the images have changed, their placement not). A page converted as a
    /// whole loses its text layer with its content.
    ///
    /// The operation control cancels both sources (B58): between the images, inside of
    /// the decoding and the thresholding of an image, and in every wait and step of the
    /// rasterization. A cancelled conversion returns false and creates no document.
    /// \param settings Inputs of the conversion
    /// \param operationControl Operation control (can be nullptr)
    bool createBitonalDocument(const Settings& settings, const PDFOperationControl* operationControl = nullptr);

    /// Returns the number of the items, which have been successfully converted by the
    /// last call of \p createBitonalDocument
    size_t getConvertedItemCount() const { return m_convertedItemCount; }

    /// Returns the number of the items, which should have been converted by the last
    /// call of \p createBitonalDocument, but which have failed and are left in their
    /// original form in the created document
    size_t getFailedItemCount() const { return m_failedItemCount; }

    /// Why an item has not been converted (the facts of the result, Figma 307:6388 "Not
    /// converted: pages 17, 43, 44 (rendering errors)")
    enum class FailureReason
    {
        CannotBeRead,       ///< The image cannot be decoded (a damaged stream, an unknown filter)
        RenderingErrors,    ///< The image of the page could not be created (allocated); errors of the renderer alone never fail a page (getRenderingIssues)
        TooLarge,           ///< The image of the page would exceed the limits of the pixels (getPageImageSize), or the declared image would (isImageTooLarge)
        CannotBeEncoded,    ///< No encoder has accepted the black and white image
        InvalidPage         ///< The page has no usable media box or dictionary
    };

    /// An item, which has failed, and the reason
    struct FailedItem
    {
        ItemInfo item;
        FailureReason reason = FailureReason::CannotBeRead;
    };

    /// Returns the items, which have failed by the last call of \p createBitonalDocument, in
    /// the order of the pages (Pages) or of the items (Images)
    const std::vector<FailedItem>& getFailedItems() const { return m_failedItems; }

    /// Translated text of a reason ("rendering errors")
    static QString getFailureReasonText(FailureReason reason);

    /// A converted page, whose rendering has reported problems (owner 14. 11. 2026: the page is
    /// converted, the user is asked to check it - Rendering issues, as PDF to Images)
    struct RenderingIssue
    {
        PDFInteger pageIndex = -1;
        QStringList messages;       ///< Translated texts of the problems, each once (no level Information)
    };

    /// Returns the converted pages with rendering issues of the last call of \p createBitonalDocument
    /// (source Pages), in the order of the pages
    const std::vector<RenderingIssue>& getRenderingIssues() const { return m_renderingIssues; }

    /// How many converted items (and their soft masks are not counted) are encoded by each
    /// compression by the last call of \p createBitonalDocument (the automatic compression
    /// chooses per item; the fact "1.9 MB · JBIG2" of 307:6564)
    const std::map<Compression, size_t>& getUsedCompressions() const { return m_usedCompressions; }

    /// Name of a compression of a created image ("JBIG2", "CCITT G4", "Flate", "RunLength"; Auto
    /// has none). Not translated - the names of the filters.
    static QString getCompressionName(Compression compression);

    /// Compression of an image object created by \p createBitonalImageObject (by its filter),
    /// std::nullopt = none of the four
    static std::optional<Compression> getImageObjectCompression(const PDFObject& imageObject);

    /// Returns the created document and clears it. \sa createBitonalDocument
    PDFDocument takeBitonalDocument() { return qMove(m_bitonalDocument); }

    /// Returns the images of the document, which can be converted: the image XObjects drawn by
    /// the content of the pages, their forms and patterns (PDFDocumentImageList with the
    /// options of a change, B61 - an image only listed in /Resources is never drawn and never
    /// converted; the images of all layers, R4-4), in the order of their first drawing. Left
    /// out: inline images (they are kept, R4-8 - a change would rewrite the content), stencil
    /// masks (already bitonal and painted by the fill color) and images, which are also the
    /// mask of another image (the conversion would change the transparency of that image).
    std::vector<PDFObjectReference> getConvertibleImages() const;

    /// The item of the list of the images can be converted (the rule of getConvertibleImages)
    static bool isConvertibleImage(const PDFDocumentImageList::Item& item);

    /// What a preview of one item is made of (the panes ORIGINAL / BITONAL of the tool, 304:5102,
    /// and of the old dialog; R4-22: one implementation in the core). A value - copied into the
    /// worker.
    struct PreviewRequest
    {
        ConversionSource conversionSource = ConversionSource::Pages;
        ItemInfo item;
        PDFImageConversion::ConversionMethod conversionMethod = PDFImageConversion::ConversionMethod::Automatic;
        int manualThreshold = 128;
        int dpiResolution = DEFAULT_DPI_RESOLUTION;     ///< Pages: the resolution of the rendering (getPreviewDpiResolution)
        Compression compression = Compression::Auto;
        bool isEncoded = true;                          ///< The result is also encoded (its size and compression), as the conversion encodes it
        QImage originalImage;                           ///< A decoded image or a rendered page of an earlier preview of the same item and resolution (null = decoded or rendered now)
    };

    /// A preview of one item
    struct Preview
    {
        QImage originalImage;           ///< The decoded image or the rendered page (null = it failed)
        QImage resultImage;             ///< What the mode makes of it (null = it failed)
        int threshold = -1;             ///< The threshold used by the method (the automatic one computed), -1 = none (a fill, unchanged)
        qint64 encodedSize = -1;        ///< Bytes of the encoded result with its soft mask (-1 = not encoded, unchanged or failed)
        std::optional<Compression> compression;     ///< The compression of the encoded result
        std::optional<FailureReason> failure;       ///< Why the preview failed
    };

    /// Creates the preview of the item (a worker thread: it only reads the document and uses the
    /// pool). A cancelled preview is a failed one.
    Preview createPreview(const PreviewRequest& request, const PDFOperationControl* operationControl) const;

    /// The resolution of a preview of a page in a pane of \p paneSize pixels: the resolution of
    /// the conversion, lowered so the page is no larger than the pane (a pane cannot show more,
    /// a page at 600 dpi needs 140 MB); never above \p dpiResolution, never below the minimum
    static int getPreviewDpiResolution(const PDFPage* page, int dpiResolution, QSize paneSize);

    /// The result of the detection of one page
    struct BlankPageResult
    {
        PDFInteger pageIndex = -1;
        bool isExamined = false;    ///< The page has been rendered and analyzed (false: cancelled, a rendering error, a page, which does not exist)
        bool isBlank = false;       ///< The page is a scan of a blank sheet (detectBlankPage)
    };

    /// Examines the pages at BLANK_PAGE_DPI_RESOLUTION, whether they are scans of a blank sheet
    /// (B67: one implementation of the old dialog, PdfTool and the tool). The pages are rendered
    /// in parallel by the pool. A page, which cannot be rendered completely, is never blank (an
    /// unknown page must never be wiped out).
    /// \param pageIndices Pages to examine
    /// \param operationControl Cancellation (optional): the pages not examined have isExamined false
    /// \param progress A step per page (optional; the steps are serialized)
    /// \returns A result per page in the order of \p pageIndices
    std::vector<BlankPageResult> detectBlankPages(const std::vector<PDFInteger>& pageIndices,
                                                  const PDFOperationControl* operationControl,
                                                  PDFProgressSink* progress) const;

    /// Estimates the resolution of the document, so the rasterized pages do not lose
    /// the details of the scanned images. Resolution is estimated from the size of the
    /// images used on the pages. Because it is not known, which part of the page an
    /// image actually covers, the estimate is only used to raise the resolution above
    /// the default one - the returned value is never lower than the default resolution.
    int getEstimatedDpiResolution() const;

    /// Returns true, if the image is a stencil mask
    /// \param reference Reference to the image object
    bool isStencilMask(PDFObjectReference reference) const;

    /// Decodes an image of the document into a QImage, including its transparency,
    /// which is decoded into the alpha channel. Returns a null image, when the image
    /// cannot be decoded or when it is too large (isImageTooLarge: nothing is allocated).
    /// This function reads the document only, so it can be called from a worker thread.
    /// \param reference Reference to the image object
    /// \param operationControl Operation control (can be nullptr)
    QImage getDecodedImage(PDFObjectReference reference, const PDFOperationControl* operationControl) const;

    /// The declared image would need more than MAXIMUM_DECODED_IMAGE_BYTES of decoded pixels
    /// (/Width x /Height of 32 bit pixels counted in 64 bits, plan of step 4 of phase 2, chapter 6:
    /// a declaration of billions of pixels with a short stream is never decoded)
    /// \param reference Reference to the image object
    bool isImageTooLarge(PDFObjectReference reference) const;

    /// Most bytes of the decoded pixels of one image of the conversion of the images (32 bit
    /// pixels, plan of step 4 of phase 2, chapter 6: 2 GiB, as PDFImageOptimizer)
    static constexpr qint64 MAXIMUM_DECODED_IMAGE_BYTES = qint64(2) * 1024 * 1024 * 1024;

    /// Rasterizes the given pages and calls the processor for each rendered page
    /// image. Images are composited onto the white background and they are returned
    /// in the coordinate system of the page, i.e. the page rotation is not applied
    /// to them. The processor can be called from multiple threads simultaneously and
    /// it is not called at all for the pages, which have been skipped because the
    /// operation has been cancelled. A page, whose rendering has reported an error,
    /// is passed with its image (owner 14. 11. 2026: a rendering error never blocks the
    /// conversion; renderPageImages gives the errors too); a null image is a page, whose
    /// image could not be created.
    /// \param pageIndices Indices of the rendered pages
    /// \param pageSizeGetter Functor returning the size of the rendered page image
    /// \param pageImageProcessor Functor processing the rendered page image
    /// \param operationControl Operation control (can be nullptr)
    void renderPages(const std::vector<PDFInteger>& pageIndices,
                     const std::function<QSize(const PDFPage*)>& pageSizeGetter,
                     const std::function<void(PDFInteger, QImage)>& pageImageProcessor,
                     const PDFOperationControl* operationControl) const;

    /// As renderPages, the processor gets also the errors of the rendering of the page
    /// (PDFRenderedPageImage::getReportedErrors: every level except Information)
    void renderPageImages(const std::vector<PDFInteger>& pageIndices,
                          const std::function<QSize(const PDFPage*)>& pageSizeGetter,
                          const std::function<void(PDFInteger, QImage, const QList<PDFRenderError>&)>& pageImageProcessor,
                          const PDFOperationControl* operationControl) const;

    /// Rasterizes a single page into an image of a given size. \sa renderPages
    /// \param pageIndex Index of the rendered page
    /// \param size Size of the target image
    /// \param operationControl Operation control (can be nullptr)
    QImage renderPage(PDFInteger pageIndex, QSize size, const PDFOperationControl* operationControl) const;

    /// Returns the renderer features, with which the rasterizer pool used by this
    /// class must be created. Annotations are deliberately not rendered - they are
    /// kept as live annotation objects in the converted document, so rendering them
    /// into the page image would paint them twice.
    static PDFRenderer::Features getPageRasterizationFeatures();

    /// Calls the processor for every image used on a page, including the images used
    /// inside the form XObjects of the page. Resources are an inheritable attribute of
    /// the page tree, so the resolved ones are used, and every form is entered only
    /// once, so a recursive form of a damaged document cannot loop forever.
    /// \param page Page
    /// \param imageProcessor Called with the reference and the dictionary of the image
    void traversePageImages(const PDFPage* page,
                            const std::function<void(PDFObjectReference, const PDFDictionary*)>& imageProcessor) const;

    /// Returns size of the rasterized page image for a given resolution. The resolution
    /// is clamped into the range supported by this class - a page rasterized at an
    /// extreme resolution would need gigabytes of memory. Returns an invalid size
    /// when the page dimensions cannot be represented safely or when the image would
    /// exceed the limits of the pixels (B65, plan 4.4: MAXIMUM_PAGE_IMAGE_DIMENSION per side,
    /// MAXIMUM_PAGE_IMAGE_BYTES of 32 bit pixels).
    /// \param page Page
    /// \param dpiResolution Resolution in dots per inch
    static QSize getPageImageSize(const PDFPage* page, int dpiResolution);

    /// The size of the media box of the page in inches of the paper (/UserUnit, Table 31): the
    /// page image covers it, its pixels are the inches times the resolution
    static QSizeF getPageSizeInInches(const PDFPage* page);

    /// The error of a page, whose image at the resolution exceeds the limits of the pixels
    /// ("Page 12 would be 70,157 px high – lower the resolution"), empty = the page fits (a page
    /// with a degenerate media box has no error here: the conversion reports it as failed)
    /// \param page Page
    /// \param pageNumber Number of the page from 1 (the text)
    /// \param dpiResolution Resolution in dots per inch (clamped as in getPageImageSize)
    static QString getPageImageSizeError(const PDFPage* page, PDFInteger pageNumber, int dpiResolution);

    /// Longest side of the image of a page (the limit of QImage and of the image writers, plan 4.4)
    static constexpr int MAXIMUM_PAGE_IMAGE_DIMENSION = 32767;

    /// Most bytes of the image of a page (32 bit pixels; plan 4.4: 2 GiB)
    static constexpr qint64 MAXIMUM_PAGE_IMAGE_BYTES = qint64(2) * 1024 * 1024 * 1024;

    /// Converts the image to the bitonal one. Returns a null image, if the conversion
    /// fails.
    /// \param image Image to be converted
    /// \param conversionMethod Conversion method
    /// \param threshold Manual threshold
    /// \param alphaMask Transparency of the converted image (can be a null image)
    /// \param operationControl Operation control (can be nullptr). A cancelled
    ///        conversion returns a null image.
    /// \param usedThreshold The threshold, which the method has used (the automatic one
    ///        computed; the adaptive method gives its global part), can be nullptr
    static QImage convertImageToBitonal(const QImage& image,
                                        PDFImageConversion::ConversionMethod conversionMethod,
                                        int threshold,
                                        QImage* alphaMask,
                                        const PDFOperationControl* operationControl,
                                        int* usedThreshold = nullptr);

    /// Creates an image object (1 bit per component, DeviceGray) from a bitonal image.
    /// Returns a null object, when the image cannot be encoded.
    ///
    /// The automatic compression encodes the image by every algorithm and keeps the
    /// smallest result, so it costs several times more than a fixed one. Algorithms,
    /// which refuse the image, are skipped - the Flate coding accepts every image, so
    /// at least one candidate always remains.
    /// \param image Bitonal image
    /// \param compression Compression of the image data
    static PDFObject createBitonalImageObject(const QImage& image,
                                              Compression compression = Compression::Auto);

    /// Swaps the black and the white pixels of a bitonal image. Returns a null image,
    /// when the image is null or when it is not a bitonal one.
    /// \param image Bitonal image
    static QImage invertBitonalImage(QImage image);

    /// Result of the analysis, which decides, whether a page is a scan of a blank
    /// sheet of paper
    struct BlankPageInfo
    {
        bool isBlank = false;               ///< True, when the page carries no content
        uint64_t contentComponentCount = 0;  ///< Number of the found spots, which are large enough to be a content
        uint64_t inkPixelCount = 0;          ///< Number of the black pixels of the analyzed area. It is a lower estimate for a page, which obviously is not a blank one, because the counting stops as soon as that is decided.
        double inkRatio = 0.0;              ///< Ratio of the black pixels to all pixels of the analyzed area
    };

    /// Decides, whether a rasterized page is a scan of a blank sheet of paper. A scan
    /// of a blank page is never completely white - it carries the texture of the
    /// paper, dust of the scanner, specks of the toner and streaks of a dirty sensor -
    /// so the decision cannot be made by counting the black pixels. What separates a
    /// blank page from a page carrying nothing but a page number is the structure of
    /// the black pixels: dirt is a scatter of spots of a fraction of a millimeter,
    /// while even a single character is a connected spot of a millimeter or more.
    ///
    /// The border of the page is left out of the analysis - a scan of a blank page
    /// often has a black frame of the lid of the scanner, a shadow of the spine or a
    /// folded corner there, and neither of them is a content of the page.
    ///
    /// The page is thresholded by this function itself, using the automatic method,
    /// so the answer does not depend on the conversion method chosen by the user - a
    /// manual threshold can turn the paper itself black, which says nothing about the
    /// content of the page.
    /// \param pageImage Rasterized page
    /// \param dpiResolution Resolution, at which the page has been rasterized. The
    ///        analysis measures the spots in millimeters, so it needs to know it.
    /// \param operationControl Operation control (can be nullptr). A cancelled
    ///        analysis reports the page as a non-blank one.
    static BlankPageInfo detectBlankPage(const QImage& pageImage,
                                         int dpiResolution,
                                         const PDFOperationControl* operationControl);

    /// Creates the bitonal image, which a filled item is replaced by. A single sample
    /// is enough when the image has no soft mask, because the image is stretched over
    /// the whole area of the replaced item. When a soft mask is attached, the image
    /// must have the size of the mask - the soft mask is resampled to the dimensions
    /// of the image it belongs to, so a single sample would destroy it.
    /// \param size Size of the image
    /// \param isBlack True for the black fill, false for the white one
    static QImage createFillImage(QSize size, bool isBlack);

    /// Creates the image showing, how a filled item is going to look in the document.
    /// Transparent parts of the source image are not filled - they stay transparent
    /// and they are displayed as a blank paper.
    /// \param image Source image
    /// \param isBlack True for the black fill, false for the white one
    static QImage createFillPreviewImage(const QImage& image, bool isBlack);

    static constexpr int DEFAULT_DPI_RESOLUTION = 300;
    static constexpr int MAXIMUM_DPI_RESOLUTION = 600;
    static constexpr int MINIMUM_DPI_RESOLUTION = 24;

    /// Resolution, at which the pages are rasterized for the blank page detection.
    /// It is high enough for the strokes of a small character to survive the
    /// rasterization - a character of a two millimeter type is still sixteen pixels
    /// tall - and low enough that scanning a long document costs a fraction of the
    /// conversion itself. \sa detectBlankPage
    static constexpr int BLANK_PAGE_DPI_RESOLUTION = 200;

private:
    /// Decides, whether a bitonal page carries a content, see \p detectBlankPage. The
    /// black pixels are grouped into connected spots, the spots are classified by
    /// their size, and the page is a blank one, when no spot of the size of a content
    /// remains.
    /// \param image Bitonal image of the page
    /// \param dpiResolution Resolution, at which the page has been rasterized
    /// \param operationControl Operation control (can be nullptr)
    static BlankPageInfo analyzeBitonalPage(const QImage& image,
                                            int dpiResolution,
                                            const PDFOperationControl* operationControl);

    /// Width of the border of the page, which the blank page analysis ignores, in
    /// millimeters. \sa detectBlankPage
    static constexpr double BLANK_PAGE_BORDER_MM = 5.0;

    /// Minimal size of a spot, which is treated as a content of the page, in
    /// millimeters. A character of the smallest type, which is still readable, is
    /// about a millimeter and a half tall, while the dust and the specks of the
    /// toner stay far below it.
    static constexpr double BLANK_PAGE_CONTENT_SIZE_MM = 1.0;

    /// Minimal length of a thin spot, which is treated as a printed line - a rule of
    /// a table, an underline - in millimeters
    static constexpr double BLANK_PAGE_LINE_LENGTH_MM = 10.0;

    /// Maximal thickness of a streak of a dirty sensor of the scanner, in millimeters
    static constexpr double BLANK_PAGE_STREAK_THICKNESS_MM = 0.3;

    /// Fraction of the analyzed area, which a streak of a dirty sensor of the scanner
    /// spans. A streak runs across the whole page, a printed line does not.
    static constexpr double BLANK_PAGE_STREAK_LENGTH_RATIO = 0.9;

    /// Maximal ratio of the black pixels of the analyzed area of a blank page. Above
    /// it the page is not a blank one, whatever the structure of its black pixels is,
    /// and the analysis ends without grouping them - a page of a text has hundreds of
    /// thousands of spots, which are of no interest.
    static constexpr double BLANK_PAGE_MAXIMUM_INK_RATIO = 0.005;

    bool createBitonalDocumentFromImages(PDFDocumentBuilder& builder, const Settings& settings, const PDFOperationControl* operationControl, std::set<PDFObjectReference>* replacedImages);
    bool createBitonalDocumentFromPages(PDFDocumentBuilder& builder, const Settings& settings, const PDFOperationControl* operationControl);

    /// Records an item, which has failed
    void addFailedItem(const ItemInfo& item, FailureReason reason);

    /// Records the compression of a converted item
    void addUsedCompression(const PDFObject& imageObject);

    /// The image object of a converted image: the bitonal samples, the entries of the
    /// original image, which stay valid, and a soft mask of \p alphaMask (added to \p builder,
    /// when it is not null; otherwise only its size is counted into \p maskSize)
    /// \returns A null object, when it cannot be encoded
    PDFObject createConvertedImageObject(PDFObjectReference originalImage, const QImage& bitonalImage, const QImage& alphaMask,
                                         Compression compression, PDFDocumentBuilder* builder, qint64* maskSize) const;

    /// What the mode of the item makes of the decoded image or the rendered page \p image: the
    /// bitonal image (a converted one, inverted, a fill) and the alpha mask of an image
    /// \returns A null image, when it fails (or is cancelled)
    static QImage createItemImage(const QImage& image, ConversionSource source, ItemMode mode,
                                  PDFImageConversion::ConversionMethod conversionMethod, int threshold,
                                  QImage* alphaMask, int* usedThreshold, const PDFOperationControl* operationControl);

    /// Removes the structure tree of the document altogether
    void removeStructureTree(PDFDocumentBuilder& builder) const;

    /// Removes the references to the content of the converted pages from the structure
    /// tree, see \p createBitonalDocument. The whole tree is removed, when no element
    /// remains in it.
    /// \param builder Builder of the converted document
    /// \param convertedPages Pages, whose content has been replaced
    void pruneStructureTree(PDFDocumentBuilder& builder, const std::set<PDFObjectReference>& convertedPages) const;

    /// Starts the progress, when it is available
    /// \param stepCount Number of steps
    /// \param text Text displayed by the progress
    void startProgress(size_t stepCount, QString text);

    /// Performs one step of the progress, when it is available
    void stepProgress();

    /// Finishes the progress, when it is available
    void finishProgress();

    std::optional<PDFImage> getImageFromReference(PDFObjectReference reference) const;

    const PDFDocument* m_document;
    PDFRasterizerPool* m_rasterizerPool;
    PDFProgressSink* m_progress;

    /// Decoded ICC profiles of the color spaces of the images of the document
    PDFICCProfileCache m_iccProfileCache;
    PDFDocument m_bitonalDocument;
    size_t m_convertedItemCount = 0;
    size_t m_failedItemCount = 0;
    std::vector<FailedItem> m_failedItems;
    std::vector<RenderingIssue> m_renderingIssues;
    std::map<Compression, size_t> m_usedCompressions;
    QMutex m_progressMutex;     ///< Serializes the steps of the progress of the threads of the pool
};

}   // namespace pdf

#endif // PDFBITONALDOCUMENTCREATOR_H
