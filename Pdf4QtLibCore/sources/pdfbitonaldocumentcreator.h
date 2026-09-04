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
        Algorithm,  ///< Converted using the selected conversion method
        Original,   ///< Left as it is, the item is not touched at all
        FillBlack,  ///< Replaced by a black area
        FillWhite   ///< Replaced by a white area
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
        std::vector<ItemInfo> items;
    };

    /// Creates the bitonal document. Returns true, if at least one item has been
    /// converted and the resulting document is valid. The created document is then
    /// available using the function \p takeBitonalDocument. This function does not
    /// touch anything but the document and the rasterizer pool, so it can be executed
    /// in a worker thread.
    ///
    /// The settings do not have to be normalized - an item with an invalid page index
    /// is ignored and a page requested more than once is converted only once (the mode
    /// of its last occurrence is used).
    ///
    /// The result can be partial - an image, which cannot be decoded, or a page, which
    /// cannot be rendered, is left in its original form and the conversion continues.
    /// Use \p getFailedItemCount to find out, whether that has happened.
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
    /// operation has been cancelled.
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
    /// extreme resolution would need gigabytes of memory.
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

    /// Creates an image object (1 bit per component, DeviceGray) from a bitonal image
    /// \param image Bitonal image
    static PDFObject createBitonalImageObject(const QImage& image);

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

private:
    bool createBitonalDocumentFromImages(PDFDocumentBuilder& builder, const Settings& settings);
    bool createBitonalDocumentFromPages(PDFDocumentBuilder& builder, const Settings& settings);

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
