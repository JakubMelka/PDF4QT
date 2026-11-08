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

#include "pdfimagepagefactory.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfimage.h"
#include "pdfoperationcontrol.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QPointF>
#include <QTransform>

#include <cmath>

namespace pdf
{

/// What the header of a JPEG file says (ITU-T T.81, B.2; Adobe APP14 of the Adobe technical note 5116)
struct PDFImagePageFactory::JpegHeader
{
    int width = 0;
    int height = 0;
    int components = 0;
    int precision = 0;
    bool isSequentialOrProgressiveHuffman = false;  ///< SOF0, SOF1, SOF2 (DCTDecode, ISO 32000-2, 7.4.8)
    bool hasAdobeMarker = false;
    bool hasJfifDensity = false;
    int jfifUnits = 0;          ///< 1 = dots per inch, 2 = dots per centimeter
    int jfifDensityX = 0;
    int jfifDensityY = 0;
};

/// Helpers of the image pages
class PDFImagePageFactoryHelper
{
public:
    PDFImagePageFactoryHelper() = delete;

    /// A reader of the bytes. The allocation limit of Qt is one value of the process
    /// (QImageReader::setAllocationLimit is static, its default is ALLOCATION_LIMIT_MB): it is
    /// never written here - the functions run in workers at once, and a write would race with
    /// every reader of it; the pixel limit is checked before a frame is decoded.
    static void prepareReader(QImageReader* reader, const QByteArray& format)
    {
        reader->setAutoTransform(true);
        if (!format.isEmpty())
        {
            reader->setFormat(format);
            reader->setDecideFormatFromContent(true);
        }
    }

    /// The format is the one of a multi-page TIFF (every frame is a page, Q6)
    static bool isTiff(const QByteArray& format)
    {
        return format == "tif" || format == "tiff";
    }

    /// The format is JPEG
    static bool isJpeg(const QByteArray& format)
    {
        return format == "jpg" || format == "jpeg";
    }

    static quint16 readBigEndian16(const QByteArray& data, qsizetype offset)
    {
        return quint16((uchar(data[offset]) << 8) | uchar(data[offset + 1]));
    }

    static quint32 readBigEndian32(const QByteArray& data, qsizetype offset)
    {
        return (quint32(uchar(data[offset])) << 24) | (quint32(uchar(data[offset + 1])) << 16) | (quint32(uchar(data[offset + 2])) << 8) | quint32(uchar(data[offset + 3]));
    }

    static quint32 readLittleEndian32(const QByteArray& data, qsizetype offset)
    {
        return quint32(uchar(data[offset])) | (quint32(uchar(data[offset + 1])) << 8) | (quint32(uchar(data[offset + 2])) << 16) | (quint32(uchar(data[offset + 3])) << 24);
    }

    /// Where a point of the stored image goes, when the transformation of EXIF is applied
    /// (QImage: the mirror and the flip first, then the rotation by 90 degrees clockwise;
    /// coordinates 0 to 1 from the top left corner)
    static QPointF transformPoint(QPointF point, QImageIOHandler::Transformations transformation)
    {
        if (transformation == QImageIOHandler::TransformationRotate270)
        {
            // rotated270 of QImage: (u, v) -> (v, 1 - u)
            return QPointF(point.y(), 1.0 - point.x());
        }

        if (transformation.testFlag(QImageIOHandler::TransformationMirror))
        {
            point.setX(1.0 - point.x());
        }
        if (transformation.testFlag(QImageIOHandler::TransformationFlip))
        {
            point.setY(1.0 - point.y());
        }
        if (transformation.testFlag(QImageIOHandler::TransformationRotate90))
        {
            point = QPointF(1.0 - point.y(), point.x());
        }
        return point;
    }

    /// Bytes of a decoded frame are within the pixel limit
    static bool isWithinPixelLimit(QSize size)
    {
        return size.width() > 0 && size.height() > 0 && qint64(size.width()) * qint64(size.height()) <= PDFImagePageFactory::MAXIMUM_PIXEL_COUNT;
    }

    /// Codes the samples losslessly (Flate with the PNG predictor, 16 bits become 8); the alpha
    /// channel, which is not opaque, is the soft mask (11.6.5.3). The stream of the image has
    /// the device color space of the samples, which is replaced by the profile of the frame.
    static void codeSamples(const QImage& image, PDFObject* codedImage, PDFObject* codedMask, int* components)
    {
        PDFImage::ImageEncodeOptions options;
        options.compression = PDFImage::ImageCompression::Flate;
        options.colorMode = PDFImage::ImageColorMode::Preserve;
        options.alphaHandling = PDFImage::AlphaHandling::DropAlphaPreserveColors;

        bool hasTransparency = false;
        if (image.hasAlphaChannel())
        {
            const QImage alpha = image.convertToFormat(QImage::Format_Alpha8);
            for (int y = 0; y < alpha.height() && !hasTransparency; ++y)
            {
                const uchar* line = alpha.constScanLine(y);
                for (int x = 0; x < alpha.width(); ++x)
                {
                    if (line[x] != 255)
                    {
                        hasTransparency = true;
                        break;
                    }
                }
            }
        }

        PDFStream stream = PDFImage::createStreamFromImage(image, options);
        const PDFObject& colorSpaceName = stream.getDictionary()->get("ColorSpace");
        *components = (colorSpaceName.isName() && colorSpaceName.getString() == "DeviceGray") ? 1 : 3;
        *codedImage = PDFObject::createStream(std::move(stream));

        *codedMask = PDFObject();
        if (hasTransparency)
        {
            QImage alpha = image.convertToFormat(QImage::Format_Alpha8);
            QImage mask(alpha.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < alpha.height(); ++y)
            {
                std::copy_n(alpha.constScanLine(y), alpha.width(), mask.scanLine(y));
            }

            PDFImage::ImageEncodeOptions maskOptions;
            maskOptions.compression = PDFImage::ImageCompression::Flate;
            maskOptions.colorMode = PDFImage::ImageColorMode::Grayscale;
            *codedMask = PDFObject::createStream(PDFImage::createStreamFromImage(mask, maskOptions));
        }
    }
};

bool PDFImagePageFactory::readJpegHeader(const QByteArray& data, JpegHeader* header)
{
    using Helper = PDFImagePageFactoryHelper;

    // Markers of ITU-T T.81, B.1.1.3: every segment up to the start of the scan has its length
    if (data.size() < 4 || uchar(data[0]) != 0xFF || uchar(data[1]) != 0xD8)
    {
        return false;
    }

    bool hasFrame = false;
    qsizetype position = 2;
    while (position + 4 <= data.size())
    {
        if (uchar(data[position]) != 0xFF)
        {
            return false;
        }
        const uchar marker = uchar(data[position + 1]);
        if (marker == 0xFF)
        {
            // A fill byte
            ++position;
            continue;
        }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
        {
            position += 2;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9)
        {
            break;
        }

        const quint16 length = Helper::readBigEndian16(data, position + 2);
        if (length < 2 || position + 2 + length > data.size())
        {
            return false;
        }
        const qsizetype segment = position + 4;
        const qsizetype segmentLength = length - 2;

        const bool isFrame = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isFrame)
        {
            if (segmentLength < 6 || hasFrame)
            {
                return false;
            }
            hasFrame = true;
            header->precision = uchar(data[segment]);
            header->height = Helper::readBigEndian16(data, segment + 1);
            header->width = Helper::readBigEndian16(data, segment + 3);
            header->components = uchar(data[segment + 5]);
            header->isSequentialOrProgressiveHuffman = marker == 0xC0 || marker == 0xC1 || marker == 0xC2;
        }
        else if (marker == 0xEE && segmentLength >= 12 && data.mid(segment, 5) == "Adobe")
        {
            header->hasAdobeMarker = true;
        }
        else if (marker == 0xE0 && segmentLength >= 12 && data.mid(segment, 5) == QByteArray("JFIF", 5))
        {
            header->jfifUnits = uchar(data[segment + 7]);
            header->jfifDensityX = Helper::readBigEndian16(data, segment + 8);
            header->jfifDensityY = Helper::readBigEndian16(data, segment + 10);
            header->hasJfifDensity = (header->jfifUnits == 1 || header->jfifUnits == 2) && header->jfifDensityX > 0 && header->jfifDensityY > 0;
        }

        position += 2 + length;
    }

    return hasFrame && header->width > 0 && header->height > 0;
}

double PDFImagePageFactory::readFileResolution(const QByteArray& data, const QByteArray& format, const QImage& image, bool isX)
{
    using Helper = PDFImagePageFactoryHelper;

    // The resolution of a file, which says one; 0 for a file, which does not. The decoders
    // of Qt leave the default of QImage (the resolution of the screen) for a file without
    // it, so the formats with a header known here read it from the header.
    if (Helper::isJpeg(format))
    {
        JpegHeader header;
        if (readJpegHeader(data, &header) && header.hasJfifDensity)
        {
            const double density = isX ? header.jfifDensityX : header.jfifDensityY;
            return header.jfifUnits == 1 ? density : density * 2.54;
        }
        return 0.0;
    }

    if (format == "png")
    {
        // pHYs (PNG, 11.3.5.3): pixels per unit, the unit 1 is the meter
        qsizetype position = 8;
        while (position + 12 <= data.size())
        {
            const quint32 length = Helper::readBigEndian32(data, position);
            const QByteArray type = data.mid(position + 4, 4);
            if (qint64(position) + 12 + qint64(length) > data.size())
            {
                break;
            }
            if (type == "pHYs" && length >= 9)
            {
                const quint32 x = Helper::readBigEndian32(data, position + 8);
                const quint32 y = Helper::readBigEndian32(data, position + 12);
                const uchar unit = uchar(data[position + 16]);
                return unit == 1 ? double(isX ? x : y) * 0.0254 : 0.0;
            }
            if (type == "IDAT" || type == "IEND")
            {
                break;
            }
            position += 12 + qsizetype(length);
        }
        return 0.0;
    }

    if (format == "bmp")
    {
        // BITMAPINFOHEADER: biXPelsPerMeter at 38, biYPelsPerMeter at 42 (0 = not given)
        if (data.size() >= 46)
        {
            return double(Helper::readLittleEndian32(data, isX ? 38 : 42)) * 0.0254;
        }
        return 0.0;
    }

    if (format == "gif")
    {
        return 0.0;
    }

    // TIFF, WebP and the others: the resolution of Qt, unless it is the default of QImage
    const QImage defaultImage(1, 1, QImage::Format_RGB32);
    const int dotsPerMeter = isX ? image.dotsPerMeterX() : image.dotsPerMeterY();
    const int defaultDotsPerMeter = isX ? defaultImage.dotsPerMeterX() : defaultImage.dotsPerMeterY();
    return dotsPerMeter == defaultDotsPerMeter ? 0.0 : double(dotsPerMeter) * 0.0254;
}

PDFImagePageFactory::ImageInfo PDFImagePageFactory::readInfo(const QByteArray& data, const QByteArray& format)
{
    using Helper = PDFImagePageFactoryHelper;

    ImageInfo info;
    if (data.isEmpty())
    {
        info.error = tr("The image has no data.");
        return info;
    }

    QByteArray bytes = data;
    QBuffer buffer(&bytes);
    buffer.open(QBuffer::ReadOnly);
    QImageReader reader(&buffer);
    Helper::prepareReader(&reader, format);
    if (!reader.canRead())
    {
        info.error = tr("The image cannot be read: %1").arg(reader.errorString());
        return info;
    }

    info.format = reader.format();
    const QSize size = reader.size();
    if (size.isValid() && !Helper::isWithinPixelLimit(size))
    {
        info.error = tr("The image is too large (%1 × %2 pixels).").arg(size.width()).arg(size.height());
        return info;
    }

    if (Helper::isTiff(info.format))
    {
        // A reader of the known format counts the directories of the file (a reader, which has
        // decided its format from the content, says 1)
        QByteArray countBytes = data;
        QBuffer countBuffer(&countBytes);
        countBuffer.open(QBuffer::ReadOnly);
        QImageReader countReader(&countBuffer, info.format);
        const int count = countReader.imageCount();
        if (count > MAXIMUM_FRAME_COUNT)
        {
            info.error = tr("The image has more than %Ln pages.", nullptr, MAXIMUM_FRAME_COUNT);
            return info;
        }
        info.frameCount = qMax(count, 1);
    }
    else
    {
        // The first frame of an animation (5.3 of the plan)
        info.frameCount = 1;
    }
    return info;
}

double PDFImagePageFactory::getResolution(double dotsPerInch)
{
    if (!std::isfinite(dotsPerInch) || dotsPerInch < MINIMUM_RESOLUTION || dotsPerInch > MAXIMUM_RESOLUTION)
    {
        return DEFAULT_RESOLUTION;
    }
    return dotsPerInch;
}

QSizeF PDFImagePageFactory::getPageSize(QSize pixels, double resolutionX, double resolutionY)
{
    if (pixels.isEmpty())
    {
        return QSizeF();
    }

    double width = double(pixels.width()) / getResolution(resolutionX) * 72.0;
    double height = double(pixels.height()) / getResolution(resolutionY) * 72.0;

    // Into the limits, the aspect ratio kept (R15): a larger side shrinks first, then a smaller
    // side grows; an extreme aspect ratio, which cannot keep both, keeps the larger limit
    const double larger = qMax(width, height);
    if (larger > MAXIMUM_SIDE)
    {
        const double scale = MAXIMUM_SIDE / larger;
        width *= scale;
        height *= scale;
    }
    const double smaller = qMin(width, height);
    if (smaller < MINIMUM_SIDE)
    {
        const double scale = qMin(MINIMUM_SIDE / smaller, MAXIMUM_SIDE / qMax(width, height));
        width *= scale;
        height *= scale;
    }
    width = qBound(MINIMUM_SIDE, width, MAXIMUM_SIDE);
    height = qBound(MINIMUM_SIDE, height, MAXIMUM_SIDE);
    return QSizeF(width, height);
}

QSizeF PDFImagePageFactory::getPageSize(const Frame& frame)
{
    return getPageSize(frame.size.isValid() ? frame.size : frame.image.size(), frame.resolutionX, frame.resolutionY);
}

void PDFImagePageFactory::compact(Frame* frame)
{
    if (frame->image.isNull())
    {
        return;
    }

    frame->size = frame->image.size();
    if (frame->jpegData.isEmpty() && frame->codedImage.isNull())
    {
        PDFImagePageFactoryHelper::codeSamples(frame->image, &frame->codedImage, &frame->codedMask, &frame->codedComponents);
    }
    frame->image = QImage();
}

std::optional<PDFImagePageFactory::Frame> PDFImagePageFactory::createFrame(const QImage& image, double dotsPerInch, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;

    if (image.isNull())
    {
        error = tr("The image is empty.");
        return std::nullopt;
    }
    if (!PDFImagePageFactoryHelper::isWithinPixelLimit(image.size()))
    {
        error = tr("The image is too large (%1 × %2 pixels).").arg(image.width()).arg(image.height());
        return std::nullopt;
    }

    Frame frame;
    frame.image = image;
    frame.size = image.size();
    frame.resolutionX = getResolution(dotsPerInch);
    frame.resolutionY = frame.resolutionX;
    if (image.colorSpace().isValid())
    {
        frame.iccProfile = image.colorSpace().iccProfile();
    }
    return frame;
}

std::optional<PDFImagePageFactory::Frame> PDFImagePageFactory::readFrame(const QByteArray& data, const QByteArray& format, int frameIndex, double dotsPerInch, QString* errorMessage)
{
    using Helper = PDFImagePageFactoryHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;

    const ImageInfo info = readInfo(data, format);
    if (info.frameCount <= 0)
    {
        error = info.error;
        return std::nullopt;
    }
    if (frameIndex < 0 || frameIndex >= info.frameCount)
    {
        error = tr("The image has no page %1.").arg(frameIndex + 1);
        return std::nullopt;
    }

    QByteArray bytes = data;
    QBuffer buffer(&bytes);
    buffer.open(QBuffer::ReadOnly);
    QImageReader reader(&buffer);
    Helper::prepareReader(&reader, info.format);
    return decodeFrame(&reader, data, info.format, frameIndex, dotsPerInch, &error);
}

bool PDFImagePageFactory::readFrames(const QByteArray& data, const QByteArray& format, const std::vector<int>& frameIndices, double dotsPerInch, const PDFOperationControl* control, std::vector<Frame>* frames, QString* errorMessage)
{
    using Helper = PDFImagePageFactoryHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    frames->clear();

    const ImageInfo info = readInfo(data, format);
    if (info.frameCount <= 0)
    {
        error = info.error;
        return false;
    }

    // One reader for every frame: a frame of a multi-page TIFF is found from the frame before it,
    // not from the first directory of the file (a new reader for each frame and its count of the
    // directories made the frames of a large TIFF quadratic)
    QByteArray bytes = data;
    QBuffer buffer(&bytes);
    buffer.open(QBuffer::ReadOnly);
    QImageReader reader(&buffer);
    Helper::prepareReader(&reader, info.format);

    // The TIFF plugin counts its directories at the first jump from the position of the device:
    // counted now, at the start of the file (after a frame is read, the position is in its data)
    if (Helper::isTiff(info.format))
    {
        reader.imageCount();
    }

    frames->reserve(frameIndices.size());
    int previousIndex = -1;
    for (const int frameIndex : frameIndices)
    {
        if (PDFOperationControl::isOperationCancelled(control))
        {
            frames->clear();
            return false;
        }
        if (frameIndex <= previousIndex || frameIndex >= info.frameCount)
        {
            error = tr("The image has no page %1.").arg(frameIndex + 1);
            frames->clear();
            return false;
        }
        previousIndex = frameIndex;

        std::optional<Frame> frame = decodeFrame(&reader, data, info.format, frameIndex, dotsPerInch, &error);
        if (!frame)
        {
            frames->clear();
            return false;
        }
        compact(&frame.value());
        frames->push_back(std::move(*frame));
    }
    return true;
}

std::optional<PDFImagePageFactory::Frame> PDFImagePageFactory::decodeFrame(QImageReader* imageReader, const QByteArray& data, const QByteArray& format, int frameIndex, double dotsPerInch, QString* errorMessage)
{
    using Helper = PDFImagePageFactoryHelper;

    QImageReader& reader = *imageReader;
    QString& error = *errorMessage;
    // The first frame is where a new reader is (an animation says -1 before its first read)
    if (frameIndex < 0 || (frameIndex > 0 && reader.currentImageNumber() != frameIndex && !reader.jumpToImage(frameIndex)))
    {
        error = tr("The page %1 of the image cannot be read.").arg(frameIndex + 1);
        return std::nullopt;
    }

    // The header first (the size), then its orientation of EXIF
    const QSize size = reader.size();
    const QImageIOHandler::Transformations transformation = reader.transformation();
    if (size.isValid() && !Helper::isWithinPixelLimit(size))
    {
        error = tr("The image is too large (%1 × %2 pixels).").arg(size.width()).arg(size.height());
        return std::nullopt;
    }

    QImage image = reader.read();
    if (image.isNull())
    {
        error = tr("The image cannot be read: %1").arg(reader.errorString());
        return std::nullopt;
    }
    if (!Helper::isWithinPixelLimit(image.size()))
    {
        error = tr("The image is too large (%1 × %2 pixels).").arg(image.width()).arg(image.height());
        return std::nullopt;
    }

    Frame frame;
    frame.image = std::move(image);
    frame.size = frame.image.size();
    if (frame.image.colorSpace().isValid())
    {
        frame.iccProfile = frame.image.colorSpace().iccProfile();
    }

    // The resolution given by the caller, else the one of the file (R15)
    const double givenResolution = (std::isfinite(dotsPerInch) && dotsPerInch > 0.0) ? dotsPerInch : 0.0;
    double resolutionX = givenResolution > 0.0 ? givenResolution : readFileResolution(data, format, frame.image, true);
    double resolutionY = givenResolution > 0.0 ? givenResolution : readFileResolution(data, format, frame.image, false);
    if (transformation.testFlag(QImageIOHandler::TransformationRotate90))
    {
        std::swap(resolutionX, resolutionY);
    }
    frame.resolutionX = getResolution(resolutionX);
    frame.resolutionY = getResolution(resolutionY);

    // A JPEG keeps its bytes (no new coding, 5.3): only the coding DCTDecode reads
    JpegHeader header;
    if (Helper::isJpeg(format) && frameIndex == 0 && readJpegHeader(data, &header) &&
        header.precision == 8 && header.isSequentialOrProgressiveHuffman &&
        (header.components == 1 || header.components == 3 || header.components == 4))
    {
        const QSize storedSize(header.width, header.height);
        const QSize shownSize = transformation.testFlag(QImageIOHandler::TransformationRotate90) ? storedSize.transposed() : storedSize;
        if (shownSize == frame.image.size())
        {
            frame.jpegData = data;
            frame.jpegSize = storedSize;
            frame.jpegComponents = header.components;
            frame.isAdobeCmyk = header.components == 4 && header.hasAdobeMarker;
            frame.transformation = transformation;
        }
    }

    return frame;
}

PDFObject PDFImagePageFactory::createColorSpace(PDFDocumentBuilder* builder, const QByteArray& iccProfile, int components)
{
    const char* deviceName = components == 1 ? "DeviceGray" : (components == 4 ? "DeviceCMYK" : "DeviceRGB");
    if (iccProfile.isEmpty())
    {
        return PDFObject::createName(deviceName);
    }

    // The profile must describe the samples (8.6.5.5: /N is the number of the components)
    const QColorSpace colorSpace = QColorSpace::fromIccProfile(iccProfile);
    int profileComponents = 0;
    if (colorSpace.isValid())
    {
        switch (colorSpace.colorModel())
        {
            case QColorSpace::ColorModel::Gray:
                profileComponents = 1;
                break;
            case QColorSpace::ColorModel::Rgb:
                profileComponents = 3;
                break;
            case QColorSpace::ColorModel::Cmyk:
                profileComponents = 4;
                break;
            default:
                break;
        }
    }
    if (profileComponents != components)
    {
        return PDFObject::createName(deviceName);
    }

    PDFDictionaryBuilder profileDictionary;
    profileDictionary.addEntry(PDFInplaceOrMemoryString("N"), PDFObject::createInteger(components));
    profileDictionary.addEntry(PDFInplaceOrMemoryString("Alternate"), PDFObject::createName(deviceName));
    profileDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(iccProfile.size()));
    QByteArray profileData = iccProfile;
    const PDFObjectReference profile = builder->addObject(PDFObject::createStream(PDFStream(std::move(profileDictionary), std::move(profileData))));

    PDFArrayBuilder array;
    array.appendItem(PDFObject::createName("ICCBased"));
    array.appendItem(PDFObject::createReference(profile));
    return PDFObject::createArray(std::move(array));
}

PDFObjectReference PDFImagePageFactory::createPage(PDFDocumentBuilder* builder, const Frame& frame, QSizeF pageSize, QRectF clip)
{
    using Helper = PDFImagePageFactoryHelper;

    // A compacted frame has no pixels, but its coded image or its JPEG
    if (frame.image.isNull() && frame.codedImage.isNull() && frame.jpegData.isEmpty())
    {
        throw PDFException(tr("The image is empty."));
    }

    if (pageSize.isEmpty())
    {
        pageSize = getPageSize(frame);
    }
    if (!clip.isValid() || clip.isEmpty())
    {
        clip = QRectF(0.0, 0.0, 1.0, 1.0);
    }

    // The image XObject
    PDFObjectReference imageReference;
    if (!frame.jpegData.isEmpty())
    {
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(frame.jpegSize.width()));
        dictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(frame.jpegSize.height()));
        dictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
        dictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), createColorSpace(builder, frame.iccProfile, frame.jpegComponents));
        if (frame.isAdobeCmyk)
        {
            // Adobe writes the samples of CMYK inverted (5.3 of the plan)
            PDFArrayBuilder decode;
            for (int i = 0; i < 4; ++i)
            {
                decode.appendItem(PDFObject::createInteger(1));
                decode.appendItem(PDFObject::createInteger(0));
            }
            dictionary.addEntry(PDFInplaceOrMemoryString("Decode"), PDFObject::createArray(std::move(decode)));
        }
        dictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("DCTDecode"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(frame.jpegData.size()));
        QByteArray content = frame.jpegData;
        imageReference = builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
    }
    else
    {
        // Lossless coding; the alpha channel is the soft mask (11.6.5.3); a compacted frame was
        // coded already
        PDFObject codedImage = frame.codedImage;
        PDFObject codedMask = frame.codedMask;
        int components = frame.codedComponents;
        if (codedImage.isNull())
        {
            Helper::codeSamples(frame.image, &codedImage, &codedMask, &components);
        }

        const PDFStream* stream = codedImage.getStream();
        PDFDictionaryBuilder dictionary(*stream->getDictionary());
        dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), createColorSpace(builder, frame.iccProfile, components));
        if (!codedMask.isNull())
        {
            const PDFObjectReference maskReference = builder->addObject(codedMask);
            dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(maskReference));
        }

        QByteArray content = *stream->getContent();
        imageReference = builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content))));
    }

    // The matrix: the unit square of the image (its bottom left corner is the origin) to the
    // page, through the orientation of EXIF of the stored JPEG and the clip of the shown image
    const double width = pageSize.width();
    const double height = pageSize.height();
    auto mapImagePoint = [&](QPointF imagePoint)
    {
        const QPointF stored(imagePoint.x(), 1.0 - imagePoint.y());
        const QPointF shown = Helper::transformPoint(stored, frame.jpegData.isEmpty() ? QImageIOHandler::TransformationNone : frame.transformation);
        const double x = (shown.x() - clip.left()) / clip.width() * width;
        const double y = height - (shown.y() - clip.top()) / clip.height() * height;
        return QPointF(x, y);
    };
    const QPointF origin = mapImagePoint(QPointF(0.0, 0.0));
    const QPointF xAxis = mapImagePoint(QPointF(1.0, 0.0)) - origin;
    const QPointF yAxis = mapImagePoint(QPointF(0.0, 1.0)) - origin;

    auto number = [](double value)
    {
        return PDFDocumentWriter::getRealNumberText(std::abs(value) < 1e-9 ? 0.0 : value);
    };

    QByteArray content;
    content += "q 0 0 " + number(width) + " " + number(height) + " re W n ";
    content += number(xAxis.x()) + " " + number(xAxis.y()) + " " + number(yAxis.x()) + " " + number(yAxis.y()) + " " + number(origin.x()) + " " + number(origin.y()) + " cm /Im0 Do Q";

    PDFDictionaryBuilder contentDictionary;
    contentDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    const PDFObjectReference contentReference = builder->addObject(PDFObject::createStream(PDFStream(std::move(contentDictionary), std::move(content))));

    PDFDictionaryBuilder xObjects;
    xObjects.addEntry(PDFInplaceOrMemoryString("Im0"), PDFObject::createReference(imageReference));
    PDFDictionaryBuilder resources;
    resources.addEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xObjects)));

    PDFArrayBuilder mediaBox;
    mediaBox.appendItem(PDFObject::createInteger(0));
    mediaBox.appendItem(PDFObject::createInteger(0));
    mediaBox.appendItem(PDFObject::createReal(width));
    mediaBox.appendItem(PDFObject::createReal(height));

    PDFDictionaryBuilder page;
    page.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Page"));
    page.addEntry(PDFInplaceOrMemoryString("MediaBox"), PDFObject::createArray(std::move(mediaBox)));
    page.addEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
    page.addEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentReference));
    return builder->addObject(PDFObject::createDictionary(std::move(page)));
}

}   // namespace pdf
