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

#ifndef PDFIMAGEPAGEFACTORY_H
#define PDFIMAGEPAGEFACTORY_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfpage.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QImageIOHandler>
#include <QMarginsF>
#include <QRectF>
#include <QSizeF>

#include <optional>
#include <vector>

class QImageReader;

namespace pdf
{

class PDFDocumentBuilder;
class PDFOperationControl;

/// Layout of an image on its page (plan of step 3 of phase 2, 4.3, 5.8, 9.3, K14-K16, K35): Add
/// Image as Page, Images to PDF, the image sources of Insert Pages and Manage Pages. A value.
///
///  - The image: the shown image (the orientation of EXIF applied), its part \p clip (0 to 1
///    from its top left corner, empty = all), turned by \p turn clockwise (the turn of the user
///    turns the image, not the page: no /Rotate, 5.8) - the "laid out image".
///  - The page: \p pageSize (a named size of PDFPageSizePresets, a custom one, the size of the
///    neighboring page - the tool resolves it) turned by \p orientation: Auto = the orientation
///    of the laid out image (a square image keeps \p pageSize), Portrait = the width is not
///    larger than the height, Landscape = the width is not smaller. An empty \p pageSize is
///    Fit to image: the laid out image by its resolution and the margins around it, the
///    orientation does not apply.
///  - The area = the page without \p margins (left, top, right, bottom as the page is shown).
///    \p scaling: Fit = the whole image in the area, the ratio kept; Fill = the whole area, the
///    ratio kept, the rest clipped; Stretch = the area; OriginalSize = the size by the
///    resolution, a larger image clipped. The content is clipped by the area (ISO 32000-2,
///    8.5.4: re W n). Fit to image places the image as Fit (Stretch stays).
///  - \p anchor: one horizontal (AlignLeft, AlignHCenter, AlignRight) and one vertical (AlignTop,
///    AlignVCenter, AlignBottom) flag; a missing one is the center. It belongs to the page: it
///    is never mirrored for right-to-left.
///  - The page side stays within PDFImagePageFactory::MINIMUM_SIDE and MAXIMUM_SIDE, the ratio
///    kept (getBoundedSize, R15 of step 2).
///  - getPageSize and getImageRect are the one rule of the page (PDFImagePageFactory::createPage)
///    and of the preview (PDFAppImagePagePreview draws by them).
class PDF4QTLIBCORESHARED_EXPORT PDFImagePageLayout
{
public:
    /// Orientation of the page
    enum class Orientation
    {
        Auto,       ///< The orientation of the image
        Portrait,
        Landscape
    };

    /// How the image fills the area inside of the margins (356:9556)
    enum class Scaling
    {
        Fit,            ///< Fit, keep ratio: the whole image, the ratio kept
        Fill,           ///< Fill, keep ratio: the whole area, the ratio kept, the rest clipped
        Stretch,        ///< The whole area, the ratio not kept
        OriginalSize    ///< The size by the resolution of the image
    };

    QSizeF pageSize;                                ///< Size of the page in points; empty = by the image (Fit to image)
    Orientation orientation = Orientation::Auto;    ///< Orientation of the page
    QMarginsF margins;                              ///< Margins of the page in points
    Scaling scaling = Scaling::Stretch;             ///< How the image fills the area
    Qt::Alignment anchor = Qt::AlignCenter;         ///< Where the image lies in the area
    QRectF clip;                                    ///< Part of the shown image (0 to 1 from its top left corner), empty = all
    PageRotation turn = PageRotation::None;         ///< Turn of the image clockwise

    bool operator==(const PDFImagePageLayout&) const = default;

    /// The layout of the image pages of today (Merge, PageMaster): the part \p imageClip of the
    /// image fills the whole page of \p stretchedPageSize (empty = by the resolution of the
    /// image); the orientation keeps the given size
    static PDFImagePageLayout createStretched(QSizeF stretchedPageSize, QRectF imageClip);

    /// The clip, which is used: \p clip within the image, the whole image for an empty or
    /// invalid one
    QRectF getEffectiveClip() const;

    /// Size of the laid out image (the clip, turned) in points
    /// \param imageSize Size of the shown image by its resolution in points (PDFImagePageFactory::getPageSize)
    QSizeF getLaidOutImageSize(QSizeF imageSize) const;

    /// Size of the page in points
    /// \param imageSize Size of the shown image by its resolution in points (PDFImagePageFactory::getPageSize)
    QSizeF getPageSize(QSizeF imageSize) const;

    /// Area of the image on the page (the page without the margins), from the top left corner
    /// of the page in points; empty when the margins leave no room
    /// \param laidOutPageSize Size of the page (getPageSize)
    QRectF getArea(QSizeF laidOutPageSize) const;

    /// Rectangle of the laid out image on the page in points, from the top left corner of the
    /// page; it may be larger than the area (Fill, OriginalSize), the area clips it. Empty when
    /// the margins leave no room or the image has no size.
    /// \param laidOutPageSize Size of the page (getPageSize)
    /// \param imageSize Size of the shown image by its resolution in points
    QRectF getImageRect(QSizeF laidOutPageSize, QSizeF imageSize) const;

    /// Where a point of the shown image (0 to 1 from its top left corner) is in the rectangle
    /// of the laid out image (0 to 1 from its top left corner): the clip and the turn. A point
    /// outside of the clip is outside of 0 to 1.
    QPointF mapShownImagePoint(QPointF point) const;
};

/// Pages made of images (plan of step 2 of phase 2, 5.3, R15, owner answer Q6): the one
/// place, which turns an image file into a page. Merge and the old PageMaster use it,
/// Images to PDF and Add Image as Page will add a layout on the page.
///
///  - Every format of QImageReader; the first frame of an animated image, every frame of
///    a multi-page TIFF (at most MAXIMUM_FRAME_COUNT). A frame, whose header has more pixels
///    than MAXIMUM_PIXEL_COUNT, is refused before it is decoded, and Qt decodes under its
///    allocation limit (SECURITY_TODO 2); an already decoded image (the clipboard, PageMaster)
///    has the same limit of its pixels. A frame waiting for its page is compacted (coded,
///    its pixels freed); the frames of a file are read by one reader (readFrames).
///  - The size of the page by the resolution of the image (Q6): pixels / dpi x 72 points.
///    A resolution, which the file does not give, or which is 0, negative, NaN or out of
///    72 to 2400 dpi, is 300 dpi. A page side out of 3 to 14 400 points is scaled into the
///    limits keeping the aspect ratio (no /UserUnit).
///  - The orientation of EXIF is applied (QImageReader::setAutoTransform).
///  - A JPEG (DCTDecode, ISO 32000-2, 7.4.8) is written with its bytes unchanged: Gray,
///    RGB or CMYK, an Adobe CMYK (APP14) with /Decode [1 0 1 0 1 0 1 0]; the orientation
///    of EXIF is then the matrix of the page content. Another image is coded losslessly
///    (Flate with the PNG predictor, PDFImage::createStreamFromImage), 16 bits become 8,
///    the alpha channel becomes a soft mask /SMask (11.6.5.3).
///  - An ICC profile of the image (QColorSpace::iccProfile) with the number of the
///    components of the samples is written as ICCBased with /N (8.6.5.5), else the
///    device color space of the samples.
///
/// Every function is reentrant (only its arguments), usable from any thread.
class PDF4QTLIBCORESHARED_EXPORT PDFImagePageFactory
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFImagePageFactory)

public:
    PDFImagePageFactory() = delete;

    /// Allocation limit of the decoding of one frame: the default of Qt (QImageReader::allocationLimit
    /// is one static value of the process - never set by a worker, the write would race with the
    /// decoders of other threads); the pixel limit of it is checked before a frame is decoded
    static constexpr int ALLOCATION_LIMIT_MB = 256;

    /// Most pixels of an image, which is decoded already (4 bytes a pixel in the allocation limit)
    static constexpr qint64 MAXIMUM_PIXEL_COUNT = qint64(ALLOCATION_LIMIT_MB) * 1024 * 1024 / 4;

    /// Most frames of a multi-page TIFF
    static constexpr int MAXIMUM_FRAME_COUNT = 10000;

    /// Resolution of an image, which does not give a valid one
    static constexpr double DEFAULT_RESOLUTION = 300.0;

    /// Range of a valid resolution given by an image (dpi)
    static constexpr double MINIMUM_RESOLUTION = 72.0;
    static constexpr double MAXIMUM_RESOLUTION = 2400.0;

    /// Range of a side of a page (points, PDF Reference 1.7, Table C.1)
    static constexpr double MINIMUM_SIDE = 3.0;
    static constexpr double MAXIMUM_SIDE = 14400.0;

    /// What an image file is
    struct ImageInfo
    {
        int frameCount = 0;     ///< Pages of the image (0 = it cannot be read)
        QByteArray format;      ///< Format of QImageReader
        QString error;          ///< Translated reason, why it cannot be read
        QSize size;             ///< Pixels of the first frame as it is shown (the orientation of EXIF applied) by its header; invalid = the format does not say it
    };

    /// Reads the header of an image file: its format and the number of its pages
    /// \param data Bytes of the file
    /// \param format Format, empty = by the content
    static ImageInfo readInfo(const QByteArray& data, const QByteArray& format);

    /// One frame of an image, ready to be written
    struct Frame
    {
        QImage image;                       ///< Decoded frame as it is shown (the orientation of EXIF applied); null after compact
        QSize size;                         ///< Pixels of the frame as it is shown (kept by compact)
        PDFObject codedImage;               ///< The samples coded by compact (a stream of the image XObject without its color space); null = not coded
        PDFObject codedMask;                ///< The soft mask coded by compact (a stream); null = none
        int codedComponents = 0;            ///< Components of the coded samples (1 or 3)
        QByteArray jpegData;                ///< Bytes of a JPEG written unchanged (empty = \p image is coded)
        QSize jpegSize;                     ///< Pixels of the JPEG as they are stored
        int jpegComponents = 0;             ///< Components of the JPEG (1, 3, 4)
        bool isAdobeCmyk = false;           ///< A JPEG with Adobe APP14 and four components (inverted samples)
        QImageIOHandler::Transformations transformation = QImageIOHandler::TransformationNone;  ///< Orientation of EXIF of the stored JPEG
        double resolutionX = DEFAULT_RESOLUTION;    ///< Resolution in dpi (valid by the rules of the class)
        double resolutionY = DEFAULT_RESOLUTION;
        QByteArray iccProfile;              ///< ICC profile of the image, may be empty
    };

    /// Decodes a frame of an image file
    /// \param data Bytes of the file
    /// \param format Format, empty = by the content
    /// \param frameIndex Index of the frame (from 0)
    /// \param dotsPerInch Resolution given by the caller, 0 = the resolution of the file
    /// \param errorMessage Translated reason of a failure
    static std::optional<Frame> readFrame(const QByteArray& data, const QByteArray& format, int frameIndex, double dotsPerInch, QString* errorMessage);

    /// Decodes the frames of an image file in their order by one reader and compacts each of them
    /// (a merge holds many frames): a frame of a multi-page TIFF is found from the frame before
    /// it, not from the first page of the file again
    /// \param data Bytes of the file
    /// \param format Format, empty = by the content
    /// \param frameIndices Indices of the frames (from 0), ascending, each once
    /// \param dotsPerInch Resolution given by the caller, 0 = the resolution of the file
    /// \param control Cancellation (optional), checked before each frame
    /// \param frames The compacted frames in the order of \p frameIndices
    /// \param errorMessage Translated reason of the first frame, which cannot be read
    /// \returns false for a frame, which cannot be read, or a cancellation (\p frames is empty then)
    static bool readFrames(const QByteArray& data, const QByteArray& format, const std::vector<int>& frameIndices, double dotsPerInch,
                           const PDFOperationControl* control, std::vector<Frame>* frames, QString* errorMessage);

    /// A frame of a decoded image (the clipboard, PageMaster); its resolution is the one of an
    /// image without it, unless \p dotsPerInch is valid
    static std::optional<Frame> createFrame(const QImage& image, double dotsPerInch, QString* errorMessage);

    /// The resolution used for \p dotsPerInch (300 for an invalid one, R15)
    static double getResolution(double dotsPerInch);

    /// Size of the page of an image of the given pixels and resolutions, in points, within
    /// MINIMUM_SIDE and MAXIMUM_SIDE (the aspect ratio kept)
    static QSizeF getPageSize(QSize pixels, double resolutionX, double resolutionY);

    /// Size of the page of a frame (getPageSize of its shown pixels)
    static QSizeF getPageSize(const Frame& frame);

    /// A size within MINIMUM_SIDE and MAXIMUM_SIDE with its aspect ratio kept (R15 of step 2;
    /// B30): a larger side shrinks first, then a smaller side grows; an extreme aspect ratio,
    /// which cannot keep both, keeps the larger limit. A side, which is not finite or not
    /// positive, is MINIMUM_SIDE.
    static QSizeF getBoundedSize(QSizeF size);

    /// Codes the samples of a frame now and frees its decoded pixels: a frame, which waits for its
    /// page (a merge of many images decodes every image before it makes the pages), holds the
    /// coded image - the size it has in the result - instead of its pixels. A JPEG written
    /// unchanged needs only its bytes. createPage makes the same page of a compacted frame.
    static void compact(Frame* frame);

    /// Creates a page of the image: a page dictionary, which is not in the page tree (the
    /// caller adds it and sets its /Parent), with the image XObject and the content
    /// stream. The part \p clip of the image fills the whole page
    /// (PDFImagePageLayout::createStretched).
    /// \param builder Builder of the document
    /// \param frame The frame
    /// \param pageSize Size of the page in points; an empty size = the clip by the resolution
    /// \param clip Part of the shown image in the coordinates 0 to 1 from its top left corner; an empty one = all
    static PDFObjectReference createPage(PDFDocumentBuilder* builder, const Frame& frame, QSizeF pageSize, QRectF clip);

    /// Creates a page of the image placed by a layout (as the overload above: a page
    /// dictionary, which is not in the page tree; the page has no /Rotate). The merger calls it
    /// for every image page (PDFDocumentMerger::Item::imageLayout). The content draws the image
    /// clipped by the area of the layout (q re W n cm Do Q). Throws PDFException for an empty
    /// frame and for margins, which leave no room for the image.
    /// \param builder Builder of the document
    /// \param frame The frame
    /// \param layout Layout of the image on the page
    static PDFObjectReference createPage(PDFDocumentBuilder* builder, const Frame& frame, const PDFImagePageLayout& layout);

private:
    struct JpegHeader;

    static bool readJpegHeader(const QByteArray& data, JpegHeader* header);
    static std::optional<Frame> decodeFrame(QImageReader* reader, const QByteArray& data, const QByteArray& format, int frameIndex, double dotsPerInch, QString* errorMessage);
    static double readFileResolution(const QByteArray& data, const QByteArray& format, const QImage& image, bool isX);
    static PDFObject createColorSpace(PDFDocumentBuilder* builder, const QByteArray& iccProfile, int components);
};

}   // namespace pdf

#endif // PDFIMAGEPAGEFACTORY_H
