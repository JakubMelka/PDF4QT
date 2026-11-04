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

#include "pdfimageoptimizer.h"
#include "pdfocrtextlayerwriter.h"

#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfimageconversion.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"
#include "pdfconstants.h"

#include <QBuffer>
#include <QImageWriter>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

#include "pdfdbgheap.h"

namespace pdf
{
namespace imageoptimizer
{
constexpr int ANALYSIS_MAX_SIZE = 256;
constexpr int MAX_UNIQUE_COLOR_SAMPLES = 512;
constexpr int GRAYSCALE_TOLERANCE = 3;
constexpr int BLACK_AND_WHITE_TOLERANCE = 7; ///< One step of the colors quantized by the analysis
constexpr int BLACK_AND_WHITE_THRESHOLD = 128;
constexpr int TONES_BAND_HEIGHT = 256;

constexpr bool supportsJbig2Encoder()
{
    return true;
}

constexpr bool supportsCcittEncoder()
{
    return true;
}

QString readNameFromColorSpace(const PDFDocument* document, const PDFObject& object)
{
    if (!document)
    {
        return QString();
    }

    const PDFObject& resolved = document->getObject(object);
    if (resolved.isName())
    {
        return QString::fromLatin1(resolved.getString());
    }
    if (resolved.isArray())
    {
        const PDFArray* array = resolved.getArray();
        if (array && array->getCount() > 0)
        {
            const PDFObject& first = document->getObject(array->getItem(0));
            if (first.isName())
            {
                return QString::fromLatin1(first.getString());
            }
        }
    }

    return QString();
}

bool hasNonOpaquePixel(const QImage& image)
{
    if (!image.hasAlphaChannel())
    {
        return false;
    }

    // No memory for the copy: the image is taken as a transparent one, whose mask decides below
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
    {
        return true;
    }

    for (int y = 0; y < rgba.height(); ++y)
    {
        const uchar* line = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x)
        {
            if (line[3] != 255)
            {
                return true;
            }
            line += 4;
        }
    }

    return false;
}

QImage extractAlphaMask(const QImage& image)
{
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
    {
        return QImage();
    }

    QImage mask(rgba.width(), rgba.height(), QImage::Format_Grayscale8);
    if (mask.isNull())
    {
        return QImage();
    }

    for (int y = 0; y < rgba.height(); ++y)
    {
        const uchar* src = rgba.constScanLine(y);
        uchar* dst = mask.scanLine(y);
        for (int x = 0; x < rgba.width(); ++x)
        {
            dst[x] = src[3];
            src += 4;
        }
    }

    return mask;
}

QSize computeTargetSize(const QSize& sourceSize,
                        const QPointF& minimalDpi,
                        int targetDpi,
                        PDFImageOptimizer::OptimizationGoal goal)
{
    if (targetDpi <= 0 || sourceSize.isEmpty())
    {
        return QSize();
    }

    const double threshold = (goal == PDFImageOptimizer::OptimizationGoal::PreferQuality) ? 1.15 : 1.0;
    const double minDpiX = minimalDpi.x();
    const double minDpiY = minimalDpi.y();

    const bool canDownsampleX = std::isfinite(minDpiX) && minDpiX > 0.0 && minDpiX > targetDpi * threshold;
    const bool canDownsampleY = std::isfinite(minDpiY) && minDpiY > 0.0 && minDpiY > targetDpi * threshold;

    if (!canDownsampleX && !canDownsampleY)
    {
        return QSize();
    }

    int newWidth = sourceSize.width();
    int newHeight = sourceSize.height();

    if (canDownsampleX)
    {
        const double scaleX = static_cast<double>(targetDpi) / minDpiX;
        newWidth = qMax(1, static_cast<int>(std::lround(sourceSize.width() * scaleX)));
    }
    if (canDownsampleY)
    {
        const double scaleY = static_cast<double>(targetDpi) / minDpiY;
        newHeight = qMax(1, static_cast<int>(std::lround(sourceSize.height() * scaleY)));
    }

    if (newWidth == sourceSize.width() && newHeight == sourceSize.height())
    {
        return QSize();
    }

    return QSize(newWidth, newHeight);
}

PDFImage::ImageCompression toImageCompression(PDFImageOptimizer::CompressionAlgorithm algorithm)
{
    switch (algorithm)
    {
        case PDFImageOptimizer::CompressionAlgorithm::Flate:
            return PDFImage::ImageCompression::Flate;
        case PDFImageOptimizer::CompressionAlgorithm::JPEG:
            return PDFImage::ImageCompression::JPEG;
        case PDFImageOptimizer::CompressionAlgorithm::JPEG2000:
            return PDFImage::ImageCompression::JPEG2000;
        case PDFImageOptimizer::CompressionAlgorithm::RunLength:
            return PDFImage::ImageCompression::RunLength;
        case PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4:
            return PDFImage::ImageCompression::CCITTGroup4;
        case PDFImageOptimizer::CompressionAlgorithm::JBIG2:
            return PDFImage::ImageCompression::JBIG2;
        case PDFImageOptimizer::CompressionAlgorithm::Auto:
            break;
    }

    return PDFImage::ImageCompression::Flate;
}

PDFImageOptimizer::CompressionProfile selectProfile(PDFImageOptimizer::ColorMode mode,
                                                    const PDFImageOptimizer::Settings& settings)
{
    switch (mode)
    {
        case PDFImageOptimizer::ColorMode::Color:
            return settings.colorProfile;
        case PDFImageOptimizer::ColorMode::Grayscale:
            return settings.grayProfile;
        case PDFImageOptimizer::ColorMode::Bitonal:
            return settings.bitonalProfile;
        case PDFImageOptimizer::ColorMode::Preserve:
        case PDFImageOptimizer::ColorMode::Auto:
            break;
    }

    return settings.colorProfile;
}

PDFImageOptimizer::ColorMode resolvePreservedColorMode(const PDFImageOptimizer::ImageInfo& info,
                                                       const PDFImageOptimizer::ImageAnalysis& analysis)
{
    // "Preserve" should keep the closest practical encoding class of the
    // original PDF image instead of routing everything through the color
    // profile. We therefore prefer original PDF metadata and fall back to
    // decoded-pixel analysis only when metadata is inconclusive.
    const QString colorSpaceName = info.colorSpaceName;

    // One bit of the file can mean other tones (/Decode, /Indexed): they would become black or white
    if (info.bitsPerComponent == 1 &&
        !analysis.hasTransparency &&
        analysis.kind == PDFImageOptimizer::ImageAnalysis::Kind::TextScan)
    {
        return PDFImageOptimizer::ColorMode::Bitonal;
    }

    if (colorSpaceName == COLOR_SPACE_NAME_DEVICE_GRAY ||
        colorSpaceName == COLOR_SPACE_NAME_CAL_GRAY)
    {
        return PDFImageOptimizer::ColorMode::Grayscale;
    }

    if (colorSpaceName == COLOR_SPACE_NAME_DEVICE_RGB ||
        colorSpaceName == COLOR_SPACE_NAME_DEVICE_CMYK ||
        colorSpaceName == COLOR_SPACE_NAME_ICCBASED ||
        colorSpaceName == COLOR_SPACE_NAME_INDEXED)
    {
        return PDFImageOptimizer::ColorMode::Color;
    }

    if (analysis.grayscale)
    {
        return PDFImageOptimizer::ColorMode::Grayscale;
    }

    return PDFImageOptimizer::ColorMode::Color;
}

QImage applyAlphaHandlingForPreview(const QImage& image, PDFImage::AlphaHandling alphaHandling)
{
    // Keep preview behavior aligned with the actual encoder. In particular,
    // "flatten to white" must be visible in the preview, otherwise the dialog
    // can suggest that transparency survives even when it will be removed.
    if (image.isNull() || !image.hasAlphaChannel() || alphaHandling == PDFImage::AlphaHandling::DropAlphaPreserveColors)
    {
        return image;
    }

    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
    {
        return image;
    }

    QImage flattened(rgba.size(), QImage::Format_RGB32);
    if (flattened.isNull())
    {
        return image;
    }

    for (int y = 0; y < rgba.height(); ++y)
    {
        const uchar* src = rgba.constScanLine(y);
        QRgb* dst = reinterpret_cast<QRgb*>(flattened.scanLine(y));

        for (int x = 0; x < rgba.width(); ++x)
        {
            const int alpha = src[3];
            const int red = (src[0] * alpha + 255 * (255 - alpha)) / 255;
            const int green = (src[1] * alpha + 255 * (255 - alpha)) / 255;
            const int blue = (src[2] * alpha + 255 * (255 - alpha)) / 255;
            dst[x] = qRgb(red, green, blue);
            src += 4;
        }
    }

    return flattened;
}

int adjustJpegQuality(int quality, PDFImageOptimizer::OptimizationGoal goal)
{
    if (goal == PDFImageOptimizer::OptimizationGoal::MinimumSize)
    {
        return qBound(20, quality - 10, 95);
    }

    return qBound(30, quality + 5, 100);
}

float adjustJpeg2000Rate(float rate, PDFImageOptimizer::OptimizationGoal goal)
{
    if (rate <= 0.0f)
    {
        return rate;
    }

    if (goal == PDFImageOptimizer::OptimizationGoal::MinimumSize)
    {
        return qMin(rate * 1.2f, 10.0f);
    }

    return qMax(rate * 0.85f, 0.1f);
}

PDFImageOptimizer::CompressionAlgorithm chooseAutoAlgorithm(const PDFImageOptimizer::ImageAnalysis& analysis,
                                                            PDFImageOptimizer::ColorMode colorMode)
{
    if (colorMode == PDFImageOptimizer::ColorMode::Bitonal)
    {
        // A generic region coded by JBIG2 is lossless and it is much smaller than the
        // Flate compression of a scanned page
        return PDFImageOptimizer::CompressionAlgorithm::JBIG2;
    }

    if (analysis.kind == PDFImageOptimizer::ImageAnalysis::Kind::Photo)
    {
        return PDFImageOptimizer::CompressionAlgorithm::JPEG;
    }

    return PDFImageOptimizer::CompressionAlgorithm::Flate;
}

PDFImageOptimizer::ColorMode resolveColorMode(const PDFImageOptimizer::Settings& settings,
                                              const PDFImageOptimizer::ImageAnalysis& analysis)
{
    if (!settings.autoMode)
    {
        return settings.colorMode == PDFImageOptimizer::ColorMode::Auto ? PDFImageOptimizer::ColorMode::Preserve : settings.colorMode;
    }

    if (settings.colorMode != PDFImageOptimizer::ColorMode::Auto)
    {
        return settings.colorMode;
    }

    if (analysis.kind == PDFImageOptimizer::ImageAnalysis::Kind::TextScan)
    {
        return PDFImageOptimizer::ColorMode::Bitonal;
    }

    if (analysis.grayscale)
    {
        return PDFImageOptimizer::ColorMode::Grayscale;
    }

    return PDFImageOptimizer::ColorMode::Color;
}

QImage applyColorMode(const QImage& image,
                      PDFImageOptimizer::ColorMode mode,
                      int monochromeThreshold)
{
    switch (mode)
    {
        case PDFImageOptimizer::ColorMode::Grayscale:
            return image.convertToFormat(QImage::Format_Grayscale8);
        case PDFImageOptimizer::ColorMode::Bitonal:
        {
            PDFImageConversion conversion;
            conversion.setImage(image);
            if (monochromeThreshold >= 0)
            {
                conversion.setConversionMethod(PDFImageConversion::ConversionMethod::Manual);
                conversion.setThreshold(qBound(0, monochromeThreshold, 255));
            }
            else
            {
                conversion.setConversionMethod(PDFImageConversion::ConversionMethod::Automatic);
            }
            if (conversion.convert())
            {
                return conversion.getConvertedImage();
            }
            return image.convertToFormat(QImage::Format_Grayscale8);
        }
        case PDFImageOptimizer::ColorMode::Preserve:
        case PDFImageOptimizer::ColorMode::Color:
        case PDFImageOptimizer::ColorMode::Auto:
            return image;
    }

    return image;
}

QImage scaleForPreview(const QImage& image, const QSize& targetSize, PDFImage::ResampleFilter filter)
{
    if (!targetSize.isValid() || targetSize.isEmpty() || targetSize == image.size())
    {
        return image;
    }

    Qt::TransformationMode mode = (filter == PDFImage::ResampleFilter::Nearest) ? Qt::FastTransformation : Qt::SmoothTransformation;
    return image.scaled(targetSize, Qt::IgnoreAspectRatio, mode);
}

QImage simulateJpegCompression(const QImage& image, int quality)
{
    QByteArray buffer;
    QBuffer device(&buffer);
    if (!device.open(QIODevice::WriteOnly))
    {
        return image;
    }

    QImageWriter writer(&device, "jpg");
    writer.setQuality(quality);
    if (!writer.write(image))
    {
        return image;
    }

    QImage decoded;
    decoded.loadFromData(buffer, "jpg");
    return decoded.isNull() ? image : decoded;
}

}   // namespace imageoptimizer

using namespace imageoptimizer;

QString PDFImageOptimizer::readFilterName(const PDFDocument* document, const PDFDictionary* dictionary)
{
    if (!document || !dictionary)
    {
        return QString();
    }

    PDFObject filters;
    if (dictionary->hasKey(PDF_STREAM_DICT_FILTER))
    {
        filters = document->getObject(dictionary->get(PDF_STREAM_DICT_FILTER));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER))
    {
        filters = document->getObject(dictionary->get(PDF_STREAM_DICT_FILE_FILTER));
    }

    if (filters.isName())
    {
        return QString::fromLatin1(filters.getString());
    }
    if (filters.isArray())
    {
        const PDFArray* array = filters.getArray();
        if (array && array->getCount() > 0)
        {
            const PDFObject& last = document->getObject(array->getItem(array->getCount() - 1));
            if (last.isName())
            {
                return QString::fromLatin1(last.getString());
            }
        }
    }

    return QString();
}

PDFDictionaryBuilder PDFImageOptimizer::mergeImageDictionary(const PDFDictionary& encoded, const PDFDictionary& original)
{
    // Entries describing the samples and the masks of the original image. DL is the decoded
    // length of the old samples. ColorTransform: producers (Skia of Chrome) write it next to
    // the filter, where it means nothing (it is a parameter of the filter): not carried over.
    static const std::unordered_set<QByteArray> replacedKeys =
    {
        "Type", "Subtype", "Width", "Height", "BitsPerComponent", "ColorSpace", "Filter",
        "DecodeParms", "DL", "Length", "Decode", "Mask", "SMask", "ImageMask", "SMaskInData", "ColorTransform"
    };

    PDFDictionaryBuilder merged(encoded);
    for (size_t i = 0; i < original.getCount(); ++i)
    {
        const PDFInplaceOrMemoryString& key = original.getKey(i);
        const QByteArray keyString = key.getString();
        if (replacedKeys.count(keyString) == 0 && !merged.hasKey(keyString))
        {
            merged.addEntry(key, PDFObject(original.getValue(i)));
        }
    }
    return merged;
}

PDFImageOptimizer::Settings PDFImageOptimizer::Settings::createDefault()
{
    Settings settings;
    settings.enabled = false;
    settings.autoMode = true;
    settings.colorMode = ColorMode::Auto;
    settings.goal = OptimizationGoal::PreferQuality;
    settings.keepOriginalIfLarger = true;
    settings.preserveTransparency = true;
    settings.colorProfile.targetDpi = 150;
    settings.grayProfile.targetDpi = 150;
    settings.bitonalProfile.targetDpi = 300;
    settings.colorProfile.jpegQuality = 85;
    settings.grayProfile.jpegQuality = 85;
    settings.colorProfile.enablePngPredictor = true;
    settings.grayProfile.enablePngPredictor = true;
    settings.bitonalProfile.enablePngPredictor = true;
    return settings;
}

/// Helpers of the optimization of the images
class PDFImageOptimizerHelper
{
public:
    PDFImageOptimizerHelper() = delete;

    /// A reporter of the caller used by more threads: one report at a time
    class LockedReporter : public PDFRenderErrorReporter
    {
    public:
        explicit LockedReporter(PDFRenderErrorReporter* reporter, QMutex* mutex) :
            m_reporter(reporter),
            m_mutex(mutex)
        {

        }

        virtual void reportRenderError(RenderErrorType type, QString message) override
        {
            if (m_reporter)
            {
                QMutexLocker locker(m_mutex);
                m_reporter->reportRenderError(type, std::move(message));
            }
        }

        virtual void reportRenderErrorOnce(RenderErrorType type, QString message) override
        {
            if (m_reporter)
            {
                QMutexLocker locker(m_mutex);
                m_reporter->reportRenderErrorOnce(type, std::move(message));
            }
        }

    private:
        PDFRenderErrorReporter* m_reporter;
        QMutex* m_mutex;
    };

    /// How the device colors of a document are shown, computed once per run
    struct DeviceColors
    {
        std::shared_ptr<const std::array<uchar, 256>> grayTable;
        bool isDeviceRGBShownExactly = true;
    };

    static DeviceColors createDeviceColors(const PDFCMS* cms)
    {
        DeviceColors colors;
        colors.grayTable = PDFImage::createGrayTable(cms);
        colors.isDeviceRGBShownExactly = PDFImage::isDeviceRGBShownExactly(cms);
        return colors;
    }

    /// Information about a decoded image of the document, with its analysis (computed
    /// once: it walks all pixels, the dialog of the images asks for each click)
    static PDFImageOptimizer::ImageInfo createImageInfo(const PDFDocument* document, PDFImageCompressor::ImageStatistics statistics, const DeviceColors& deviceColors);

    /// Tones of all pixels of an image. A sample would decide wrongly: a color of a thin
    /// line would be lost, a gray of a detail would become black or white.
    struct ImageTones
    {
        bool isGrayscale = true;
        bool isBlackAndWhite = true;
    };

    static ImageTones getImageTones(const QImage& image);

    static std::function<void()>& getTestMaskEncodeHook()
    {
        static std::function<void()> hook;
        return hook;
    }

    /// Encoding of one image, its pixels are not needed for the writing to the storage
    struct EncodedImage
    {
        bool keepOriginal = true;
        PDFObjectReference reference;
        PDFStream stream;
        std::optional<PDFStream> maskStream;
        std::optional<PDFObjectReference> existingMaskReference;
        int originalBytes = 0;
        int newBytes = 0;
    };

    /// Encodes one image. A kept image holds no stream: nothing but the image, which is
    /// written to the document, stays in the memory after this function
    static EncodedImage encodeImage(const PDFDocument* document,
                                    const PDFImageOptimizer::ImageInfo& info,
                                    const PDFImageOptimizer::Settings& settings,
                                    const PDFImageOptimizer::ImageOverrides& overrides,
                                    PDFRenderErrorReporter* reporter);
};

PDFImageOptimizerHelper::ImageTones PDFImageOptimizerHelper::getImageTones(const QImage& image)
{
    ImageTones tones;

    // Converted by bands: a large image is not held twice in the memory, and the
    // first colored pixel of a photograph ends the walk
    for (int top = 0; top < image.height(); top += TONES_BAND_HEIGHT)
    {
        const QImage band = image.copy(0, top, image.width(), qMin(TONES_BAND_HEIGHT, image.height() - top)).convertToFormat(QImage::Format_RGBA8888);
        if (band.isNull())
        {
            return ImageTones{ false, false };
        }

        for (int y = 0; y < band.height(); ++y)
        {
            const uchar* row = band.constScanLine(y);
            for (int x = 0; x < band.width(); ++x)
            {
                const int lowest = qMin(int(row[0]), qMin(int(row[1]), int(row[2])));
                const int highest = qMax(int(row[0]), qMax(int(row[1]), int(row[2])));

                if (highest - lowest > GRAYSCALE_TOLERANCE)
                {
                    return ImageTones{ false, false };
                }

                // A half transparent black gives other tones over the page, when the alpha is flattened
                if (row[3] != 255 || (highest > BLACK_AND_WHITE_TOLERANCE && lowest < 255 - BLACK_AND_WHITE_TOLERANCE))
                {
                    tones.isBlackAndWhite = false;
                }

                row += 4;
            }
        }
    }

    return tones;
}

PDFImageOptimizer::ImageInfo PDFImageOptimizerHelper::createImageInfo(const PDFDocument* document, PDFImageCompressor::ImageStatistics statistics, const DeviceColors& deviceColors)
{
    PDFImageOptimizer::ImageInfo info;
    info.reference = statistics.reference;
    info.image = std::move(statistics.image);
    info.minimalDpi = statistics.minimalDpi;
    info.pixelSize = info.image.size();
    info.hasTransparency = hasNonOpaquePixel(info.image);
    info.analysis = PDFImageOptimizer::analyzeImage(info.image);
    info.grayTable = deviceColors.grayTable;
    info.isDeviceRGBShownExactly = deviceColors.isDeviceRGBShownExactly;

    const PDFObject object = document->getObjectByReference(info.reference);
    if (object.isStream())
    {
        const PDFStream* stream = object.getStream();
        const PDFDictionary* dictionary = stream->getDictionary();

        info.originalBytes = stream->getContent() ? stream->getContent()->size() : 0;
        if (dictionary)
        {
            info.filterName = PDFImageOptimizer::readFilterName(document, dictionary);
            info.colorSpaceName = readNameFromColorSpace(document, dictionary->get("ColorSpace"));
            info.bitsPerComponent = PDFDocumentDataLoaderDecorator(document).readIntegerFromDictionary(dictionary, "BitsPerComponent", 8);
            info.isImageMask = PDFDocumentDataLoaderDecorator(document).readBooleanFromDictionary(dictionary, "ImageMask", false);
        }

        if (dictionary && dictionary->hasKey("SMask"))
        {
            const PDFObject& maskObject = document->getObject(dictionary->get("SMask"));
            if (maskObject.isStream())
            {
                info.hasSoftMask = true;
                const PDFStream* maskStream = maskObject.getStream();
                if (maskStream && maskStream->getContent())
                {
                    info.originalBytes += maskStream->getContent()->size();
                }
            }
        }
    }

    return info;
}

PDFImageOptimizerHelper::EncodedImage PDFImageOptimizerHelper::encodeImage(const PDFDocument* document,
                                                                           const PDFImageOptimizer::ImageInfo& info,
                                                                           const PDFImageOptimizer::Settings& settings,
                                                                           const PDFImageOptimizer::ImageOverrides& overrides,
                                                                           PDFRenderErrorReporter* reporter)
{
    EncodedImage encoded;
    encoded.reference = info.reference;
    encoded.originalBytes = info.originalBytes;

    auto overrideIt = overrides.find(info.reference);
    if (overrideIt != overrides.end() && !overrideIt->second.enabled)
    {
        return encoded;
    }

    if (info.isImageMask)
    {
        return encoded;
    }

    const PDFImageOptimizer::Settings& effectiveSettings = (overrideIt != overrides.end() && overrideIt->second.useCustomSettings) ? overrideIt->second.settings : settings;
    PDFImageOptimizer::ResolvedPlan plan = PDFImageOptimizer::resolvePlan(info, effectiveSettings);
    plan.originalBytes = info.originalBytes;

    // A decoded image holds the colors, which are shown: written as DeviceRGB under an output intent of
    // another RGB space it would go through that space again. The stream of the file stays.
    if (plan.resolvedColorMode == PDFImageOptimizer::ColorMode::Color && !info.isDeviceRGBShownExactly)
    {
        return encoded;
    }

    try
    {
        PDFStream stream = PDFImage::createStreamFromImage(info.image, plan.encodeOptions, reporter);
        encoded.stream = std::move(stream);
    }
    catch (const PDFException&)
    {
        return encoded;
    }

    encoded.newBytes = encoded.stream.getContent() ? encoded.stream.getContent()->size() : 0;

    if (plan.useSoftMask)
    {
        QImage mask = extractAlphaMask(info.image);

        // No memory for the mask: an image without it would lose its transparency, its stream stays
        if (mask.isNull())
        {
            encoded.stream = PDFStream();
            encoded.newBytes = 0;
            return encoded;
        }

        {
            PDFImage::ImageEncodeOptions maskOptions = plan.encodeOptions;
            maskOptions.compression = PDFImage::ImageCompression::Flate;
            maskOptions.colorMode = PDFImage::ImageColorMode::Grayscale;
            maskOptions.alphaHandling = PDFImage::AlphaHandling::FlattenToWhite;

            // The values of a soft mask are the alpha itself, no color management shows them
            maskOptions.grayTable = nullptr;

            try
            {
                if (const std::function<void()>& hook = getTestMaskEncodeHook())
                {
                    hook();
                }

                PDFStream maskStream = PDFImage::createStreamFromImage(mask, maskOptions, reporter);
                encoded.maskStream = std::move(maskStream);
                encoded.newBytes += encoded.maskStream->getContent() ? encoded.maskStream->getContent()->size() : 0;
            }
            catch (const PDFException&)
            {
                // An image without its mask would lose its transparency: its stream stays
                encoded.stream = PDFStream();
                encoded.maskStream.reset();
                encoded.newBytes = 0;
                return encoded;
            }
        }
    }

    if (effectiveSettings.keepOriginalIfLarger && encoded.originalBytes > 0 && encoded.newBytes >= encoded.originalBytes)
    {
        // The encoded image is not needed, only its size is reported
        encoded.keepOriginal = true;
        encoded.stream = PDFStream();
        encoded.maskStream.reset();
    }
    else
    {
        encoded.keepOriginal = false;

        const PDFObject object = document->getObjectByReference(info.reference);
        if (object.isStream())
        {
            const PDFStream* originalStream = object.getStream();
            const PDFDictionary* originalDict = originalStream->getDictionary();
            if (originalDict)
            {
                PDFDictionaryBuilder merged = PDFImageOptimizer::mergeImageDictionary(*encoded.stream.getDictionary(), *originalDict);
                if (originalDict->hasKey("SMask"))
                {
                    const PDFObject& maskObject = originalDict->get("SMask");
                    if (maskObject.isReference())
                    {
                        encoded.existingMaskReference = maskObject.getReference();
                    }
                }

                const QByteArray* content = encoded.stream.getContent();
                QByteArray contentCopy = content ? *content : QByteArray();
                encoded.stream = PDFStream(std::move(merged), std::move(contentCopy));
            }
        }
    }

    return encoded;
}

void PDFImageOptimizer::setTestMaskEncodeHook(std::function<void()> hook)
{
    PDFImageOptimizerHelper::getTestMaskEncodeHook() = std::move(hook);
}

std::vector<PDFImageOptimizer::ImageInfo> PDFImageOptimizer::collectImageInfos(const PDFDocument* document, const PDFOperationControl* operationControl)
{
    std::vector<ImageInfo> results;
    if (!document)
    {
        return results;
    }

    PDFImageCompressor compressor;
    PDFImageCompressor::Environment environment(document);
    const PDFImageOptimizerHelper::DeviceColors deviceColors = PDFImageOptimizerHelper::createDeviceColors(environment.getCMS());
    PDFImageCompressor::ImageStatisticsList stats = compressor.collectImages(environment, operationControl);
    results.reserve(stats.size());

    for (PDFImageCompressor::ImageStatistics& stat : stats)
    {
        results.push_back(PDFImageOptimizerHelper::createImageInfo(document, std::move(stat), deviceColors));
    }

    return results;
}

PDFImageOptimizer::ImageAnalysis PDFImageOptimizer::analyzeImage(const QImage& image)
{
    ImageAnalysis analysis;
    if (image.isNull())
    {
        return analysis;
    }

    const PDFImageOptimizerHelper::ImageTones tones = PDFImageOptimizerHelper::getImageTones(image);
    analysis.grayscale = tones.isGrayscale;

    QImage sample = image;
    if (qMax(image.width(), image.height()) > ANALYSIS_MAX_SIZE)
    {
        sample = image.scaled(ANALYSIS_MAX_SIZE, ANALYSIS_MAX_SIZE, Qt::KeepAspectRatio, Qt::FastTransformation);
    }

    QImage rgba = sample.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
    {
        return analysis;
    }

    std::unordered_set<int> uniqueColors;
    uniqueColors.reserve(256);

    const int width = rgba.width();
    const int height = rgba.height();
    const int pixelCount = width * height;

    double sum = 0.0;
    double sumSq = 0.0;
    int edgeCount = 0;

    for (int y = 0; y < height; ++y)
    {
        const uchar* row = rgba.constScanLine(y);
        const uchar* prevRow = y > 0 ? rgba.constScanLine(y - 1) : nullptr;
        for (int x = 0; x < width; ++x)
        {
            const int r = row[0];
            const int g = row[1];
            const int b = row[2];
            const int a = row[3];

            if (a != 255)
            {
                analysis.hasTransparency = true;
            }

            const int quantized = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
            if (uniqueColors.size() < MAX_UNIQUE_COLOR_SAMPLES)
            {
                uniqueColors.insert(quantized);
            }

            const int luma = qGray(r, g, b);
            sum += luma;
            sumSq += static_cast<double>(luma) * luma;

            if (prevRow && x > 0)
            {
                const int leftLuma = qGray(row[-4], row[-3], row[-2]);
                const int upLuma = qGray(prevRow[0], prevRow[1], prevRow[2]);
                const int gradient = std::abs(luma - leftLuma) + std::abs(luma - upLuma);
                if (gradient > 50)
                {
                    ++edgeCount;
                }
            }

            row += 4;
            if (prevRow)
            {
                prevRow += 4;
            }
        }
    }

    analysis.uniqueColors = static_cast<int>(uniqueColors.size());

    if (pixelCount > 0)
    {
        const double mean = sum / pixelCount;
        analysis.variance = (sumSq / pixelCount) - (mean * mean);
        analysis.edgeDensity = static_cast<double>(edgeCount) / pixelCount;
    }

    // Only a black and white image can be written by one bit: an image of one or two other
    // tones (a light gray panel) would become black or disappear
    if (tones.isBlackAndWhite)
    {
        analysis.kind = ImageAnalysis::Kind::TextScan;
    }
    else if (analysis.uniqueColors <= 32 || (analysis.edgeDensity > 0.22 && analysis.variance < 600.0))
    {
        analysis.kind = ImageAnalysis::Kind::LineArt;
    }
    else
    {
        analysis.kind = ImageAnalysis::Kind::Photo;
    }

    return analysis;
}

PDFImageOptimizer::ResolvedPlan PDFImageOptimizer::resolvePlan(const ImageInfo& info,
                                                              const Settings& settings)
{
    ResolvedPlan plan;
    plan.originalBytes = info.originalBytes;
    ImageAnalysis analysis = info.analysis ? *info.analysis : analyzeImage(info.image);

    // The analysis looks at a sample of a large image, it may miss a few transparent pixels
    analysis.hasTransparency = analysis.hasTransparency || info.hasTransparency;

    ColorMode resolvedMode = resolveColorMode(settings, analysis);
    if (resolvedMode == ColorMode::Preserve)
    {
        resolvedMode = resolvePreservedColorMode(info, analysis);
    }
    plan.resolvedColorMode = resolvedMode;

    const CompressionProfile profile = selectProfile(resolvedMode, settings);
    plan.targetSize = computeTargetSize(info.image.size(), info.minimalDpi, profile.targetDpi, settings.goal);

    CompressionAlgorithm algorithm = profile.algorithm;
    if (algorithm == CompressionAlgorithm::Auto)
    {
        if (settings.autoMode)
        {
            algorithm = chooseAutoAlgorithm(analysis, resolvedMode);
        }
        else
        {
            algorithm = (resolvedMode == ColorMode::Bitonal) ? CompressionAlgorithm::JBIG2 : CompressionAlgorithm::JPEG;
        }
    }

    if (resolvedMode == ColorMode::Bitonal &&
        (algorithm == CompressionAlgorithm::JPEG || algorithm == CompressionAlgorithm::JPEG2000))
    {
        algorithm = CompressionAlgorithm::Flate;
        plan.hadUnsupportedCompression = true;
    }

    if ((algorithm == CompressionAlgorithm::JBIG2 && !supportsJbig2Encoder()) ||
        (algorithm == CompressionAlgorithm::CCITTGroup4 && !supportsCcittEncoder()))
    {
        algorithm = CompressionAlgorithm::Flate;
        plan.hadUnsupportedCompression = true;
    }

    plan.algorithm = algorithm;
    plan.useSoftMask = settings.preserveTransparency && analysis.hasTransparency;

    PDFImage::ImageEncodeOptions options;
    options.compression = toImageCompression(algorithm);
    options.colorMode = PDFImage::ImageColorMode::Preserve;
    options.targetSize = plan.targetSize;
    options.jpegQuality = settings.exactJpegQuality ? qBound(0, profile.jpegQuality, 100) : adjustJpegQuality(profile.jpegQuality, settings.goal);
    options.jpeg2000Rate = adjustJpeg2000Rate(profile.jpeg2000Rate, settings.goal);
    options.monochromeThreshold = profile.monochromeThreshold;

    // All tones lie at the ends: a threshold from the histogram could fall inside of the black
    if (options.monochromeThreshold < 0 && analysis.kind == ImageAnalysis::Kind::TextScan)
    {
        options.monochromeThreshold = BLACK_AND_WHITE_THRESHOLD;
    }

    options.enablePngPredictor = profile.enablePngPredictor;
    options.resampleFilter = profile.resampleFilter;
    options.alphaHandling = plan.useSoftMask ? PDFImage::AlphaHandling::DropAlphaPreserveColors : PDFImage::AlphaHandling::FlattenToWhite;
    options.grayTable = info.grayTable;

    switch (resolvedMode)
    {
        case ColorMode::Color:
            options.colorMode = PDFImage::ImageColorMode::Color;
            break;
        case ColorMode::Preserve:
            options.colorMode = PDFImage::ImageColorMode::Preserve;
            break;
        case ColorMode::Grayscale:
            options.colorMode = PDFImage::ImageColorMode::Grayscale;
            break;
        case ColorMode::Bitonal:
            options.colorMode = PDFImage::ImageColorMode::Monochrome;
            if (options.monochromeThreshold >= 0)
            {
                options.monochromeThreshold = qBound(0, options.monochromeThreshold, 255);
            }
            break;
        case ColorMode::Auto:
            options.colorMode = PDFImage::ImageColorMode::Preserve;
            break;
    }

    plan.encodeOptions = options;
    return plan;
}

QImage PDFImageOptimizer::createPreviewImage(const ImageInfo& info, const ResolvedPlan& plan, bool simulateCompression)
{
    if (info.image.isNull())
    {
        return QImage();
    }

    QImage image = scaleForPreview(info.image, plan.targetSize, plan.encodeOptions.resampleFilter);
    image = applyAlphaHandlingForPreview(image, plan.encodeOptions.alphaHandling);
    image = applyColorMode(image, plan.resolvedColorMode, plan.encodeOptions.monochromeThreshold);

    if (simulateCompression && plan.algorithm == CompressionAlgorithm::JPEG)
    {
        image = simulateJpegCompression(image, plan.encodeOptions.jpegQuality);
    }

    return image;
}

int PDFImageOptimizer::estimateEncodedBytes(const ImageInfo& info,
                                            const ResolvedPlan& plan,
                                            PDFRenderErrorReporter* reporter)
{
    if (info.image.isNull())
    {
        return 0;
    }

    PDFRenderErrorReporterDummy dummyReporter;
    if (!reporter)
    {
        reporter = &dummyReporter;
    }

    int totalBytes = 0;
    try
    {
        PDFStream stream = PDFImage::createStreamFromImage(info.image, plan.encodeOptions, reporter);
        totalBytes += stream.getContent() ? stream.getContent()->size() : 0;
    }
    catch (const PDFException&)
    {
        return 0;
    }

    if (plan.useSoftMask)
    {
        QImage mask = extractAlphaMask(info.image);
        if (!mask.isNull())
        {
            PDFImage::ImageEncodeOptions maskOptions = plan.encodeOptions;
            maskOptions.compression = PDFImage::ImageCompression::Flate;
            maskOptions.colorMode = PDFImage::ImageColorMode::Grayscale;
            maskOptions.alphaHandling = PDFImage::AlphaHandling::FlattenToWhite;
            maskOptions.grayTable = nullptr;

            try
            {
                PDFStream maskStream = PDFImage::createStreamFromImage(mask, maskOptions, reporter);
                totalBytes += maskStream.getContent() ? maskStream.getContent()->size() : 0;
            }
            catch (const PDFException&)
            {
                return totalBytes;
            }
        }
    }

    return totalBytes;
}

PDFDocument PDFImageOptimizer::optimize(const PDFDocument* document,
                                        const Settings& settings,
                                        const ImageOverrides& overrides,
                                        PDFProgress* progress,
                                        PDFRenderErrorReporter* reporter,
                                        std::vector<ImageResult>* results,
                                        const PDFOperationControl* operationControl,
                                        PDFProgress* analysisProgress) const
{
    if (results)
    {
        results->clear();
    }

    if (!document || !settings.enabled)
    {
        if (!document)
        {
            return PDFDocument();
        }

        PDFObjectStorage storage = document->getStorage();
        return PDFDocument(std::move(storage), document->getInfo()->version, document->getSourceDataHash());
    }

    const auto createCopy = [document]()
    {
        PDFObjectStorage storage = document->getStorage();
        return PDFDocument(std::move(storage), document->getInfo()->version, document->getSourceDataHash());
    };

    // The first pass decodes nothing: it finds the images and the resolutions they have
    // on the pages. The second one decodes and encodes them in the threads of the policy,
    // a task holds its decoded image only while it works on it.
    PDFImageCompressor compressor;
    PDFImageCompressor::Environment environment(document);
    const PDFImageCompressor::ImagePlacements placements = compressor.collectImagePlacements(environment, operationControl, analysisProgress);
    if (placements.empty() || PDFOperationControl::isOperationCancelled(operationControl))
    {
        return createCopy();
    }

    if (progress)
    {
        ProgressStartupInfo info;
        info.showDialog = true;
        info.text = PDFTranslationContext::tr("Optimizing images...");
        progress->start(placements.size(), std::move(info));
    }

    std::vector<PDFImageOptimizerHelper::EncodedImage> encodedResults;
    encodedResults.reserve(placements.size());

    // The callers give a reporter of one thread and a progress serves one thread at a time
    QMutex resultMutex;
    PDFImageOptimizerHelper::LockedReporter lockedReporter(reporter, &resultMutex);
    const PDFImageOptimizerHelper::DeviceColors deviceColors = PDFImageOptimizerHelper::createDeviceColors(environment.getCMS());

    const PDFImageCompressor::DecodedImageCallback encodeImage = [&](PDFImageCompressor::ImageStatistics statistics)
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        const ImageInfo info = PDFImageOptimizerHelper::createImageInfo(document, std::move(statistics), deviceColors);
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        PDFImageOptimizerHelper::EncodedImage encoded = PDFImageOptimizerHelper::encodeImage(document, info, settings, overrides, &lockedReporter);

        // A canceled run has no results: images still in the hands of other threads say nothing
        QMutexLocker locker(&resultMutex);
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }
        encodedResults.push_back(std::move(encoded));
        if (progress)
        {
            progress->step();
        }
    };

    compressor.visitImagesInParallel(environment, placements, encodeImage, operationControl);

    if (progress)
    {
        progress->finish();
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        return createCopy();
    }

    // The objects are written in the order of the references, new objects of the
    // masks get their numbers independently on the order of the pages
    std::sort(encodedResults.begin(), encodedResults.end(), [](const PDFImageOptimizerHelper::EncodedImage& left, const PDFImageOptimizerHelper::EncodedImage& right) { return left.reference < right.reference; });

    PDFObjectStorage storage = document->getStorage();
    std::set<PDFObjectReference> replacedObjects;

    if (results)
    {
        results->reserve(encodedResults.size());
    }

    for (const PDFImageOptimizerHelper::EncodedImage& encoded : encodedResults)
    {
        if (encoded.keepOriginal)
        {
            if (results)
            {
                results->push_back(ImageResult{ encoded.reference, true, encoded.originalBytes, encoded.newBytes, QString() });
            }
            continue;
        }

        PDFStream stream = encoded.stream;

        if (encoded.maskStream)
        {
            PDFObjectReference maskReference;
            if (encoded.existingMaskReference)
            {
                maskReference = *encoded.existingMaskReference;
                storage.setObject(maskReference, PDFObject::createStream(PDFStream(*encoded.maskStream)));
            }
            else
            {
                maskReference = storage.addObject(PDFObject::createStream(PDFStream(*encoded.maskStream)));
            }

            PDFDictionaryBuilder updatedDictionary(*stream.getDictionary());
            updatedDictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(maskReference));
            stream = PDFStream(std::move(updatedDictionary), QByteArray(*stream.getContent()));
        }

        storage.setObject(encoded.reference, PDFObject::createStream(PDFStream(std::move(stream))));
        replacedObjects.insert(encoded.reference);

        if (results)
        {
            results->push_back(ImageResult{ encoded.reference, false, encoded.originalBytes, encoded.newBytes, QString() });
        }
    }

    PDFDocument optimizedDocument(std::move(storage), document->getInfo()->version, document->getSourceDataHash());

    // Only the encoding of the images was changed, their placement not: the OCR text
    // layers of PDF4QT stay valid and are bound to the new revision of the pages
    return PDFOCRTextLayerWriter::rebindOptimizedDocument(document, std::move(optimizedDocument), replacedObjects, nullptr);
}

}   // namespace pdf
