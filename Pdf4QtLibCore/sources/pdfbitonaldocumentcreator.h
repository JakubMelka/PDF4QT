// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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
#include "pdfimage.h"
#include "pdfimageconversion.h"
#include "pdfoperationcontrol.h"
#include "pdfrenderer.h"

#include <QImage>

#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace pdf
{
class PDFPage;
class PDFProgress;
class PDFDocumentBuilder;
class PDFRasterizerPool;

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
                                       PDFProgress* progress);

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
    /// The result can be partial - an image, which cannot be decoded, or a page, which
    /// cannot be rendered completely, is left in its original form and the conversion
    /// continues. Use \p getFailedItemCount to find out, whether that has happened.
    /// A page is converted only when its rendering has reported no error, because an
    /// image of a page, whose content has been partially skipped, would replace the
    /// original content irreversibly.
    ///
    /// When only some pages of a tagged document are converted, the structure tree
    /// is pruned - the references to the marked content of the converted pages are
    /// removed from it, the elements, which have lost all their content, are removed
    /// as well, and the parent tree and the ID tree are updated accordingly.
    /// \param settings Inputs of the conversion
    bool createBitonalDocument(const Settings& settings);

    /// Returns the number of the items, which have been successfully converted by the
    /// last call of \p createBitonalDocument
    size_t getConvertedItemCount() const { return m_convertedItemCount; }

    /// Returns the number of the items, which should have been converted by the last
    /// call of \p createBitonalDocument, but which have failed and are left in their
    /// original form in the created document
    size_t getFailedItemCount() const { return m_failedItemCount; }

    /// Returns the created document and clears it. \sa createBitonalDocument
    PDFDocument takeBitonalDocument() { return qMove(m_bitonalDocument); }

    /// Returns the images of the document, which can be converted. Stencil masks are
    /// left out - they are already bitonal and they are painted using the current fill
    /// color, so converting them makes no sense.
    std::vector<PDFObjectReference> getConvertibleImages() const;

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
    /// cannot be decoded. This function reads the document only, so it can be called
    /// from a worker thread.
    /// \param reference Reference to the image object
    /// \param operationControl Operation control (can be nullptr)
    QImage getDecodedImage(PDFObjectReference reference, const PDFOperationControl* operationControl) const;

    /// Rasterizes the given pages and calls the processor for each rendered page
    /// image. Images are composited onto the white background and they are returned
    /// in the coordinate system of the page, i.e. the page rotation is not applied
    /// to them. The processor can be called from multiple threads simultaneously and
    /// it is not called at all for the pages, which have been skipped because the
    /// operation has been cancelled. A page, whose rendering has reported an error,
    /// is passed to the processor as a null image - the renderer skips the content
    /// it cannot process, so the image would not show the whole page.
    /// \param pageIndices Indices of the rendered pages
    /// \param pageSizeGetter Functor returning the size of the rendered page image
    /// \param pageImageProcessor Functor processing the rendered page image
    /// \param operationControl Operation control (can be nullptr)
    void renderPages(const std::vector<PDFInteger>& pageIndices,
                     const std::function<QSize(const PDFPage*)>& pageSizeGetter,
                     const std::function<void(PDFInteger, QImage)>& pageImageProcessor,
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
    /// when the page dimensions cannot be represented safely.
    /// \param page Page
    /// \param dpiResolution Resolution in dots per inch
    static QSize getPageImageSize(const PDFPage* page, int dpiResolution);

    /// Converts the image to the bitonal one. Returns a null image, if the conversion
    /// fails.
    /// \param image Image to be converted
    /// \param conversionMethod Conversion method
    /// \param threshold Manual threshold
    /// \param alphaMask Transparency of the converted image (can be a null image)
    /// \param operationControl Operation control (can be nullptr). A cancelled
    ///        conversion returns a null image.
    static QImage convertImageToBitonal(const QImage& image,
                                        PDFImageConversion::ConversionMethod conversionMethod,
                                        int threshold,
                                        QImage* alphaMask,
                                        const PDFOperationControl* operationControl);

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

    bool createBitonalDocumentFromImages(PDFDocumentBuilder& builder, const Settings& settings);
    bool createBitonalDocumentFromPages(PDFDocumentBuilder& builder, const Settings& settings);

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
    PDFProgress* m_progress;
    PDFDocument m_bitonalDocument;
    size_t m_convertedItemCount = 0;
    size_t m_failedItemCount = 0;
};

}   // namespace pdf

#endif // PDFBITONALDOCUMENTCREATOR_H
