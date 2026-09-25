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

#include "pdfocrcompression.h"
#include "pdfdocument.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdfimage.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfimageconversion.h"
#include "pdfimageoptimizer.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"
#include "pdfpagecontentprocessor.h"

#include <QMutex>
#include <QThread>
#include <QJsonArray>
#include <QScopeGuard>

#include <set>
#include <map>
#include <cmath>
#include <atomic>
#include <limits>
#include <functional>
#include <unordered_set>

namespace pdf
{

// -------------------------------------------------------------------------
// PDFOCRCompressionSettings
// -------------------------------------------------------------------------

bool PDFOCRCompressionSettings::isExcluded(PDFObjectReference reference) const
{
    return std::find(excludedImages.begin(), excludedImages.end(), reference) != excludedImages.end();
}

QJsonObject PDFOCRCompressionSettings::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("mode")] = static_cast<int>(mode);
    object[QStringLiteral("bitonalEncoding")] = static_cast<int>(bitonalEncoding);
    object[QStringLiteral("thresholdMethod")] = static_cast<int>(thresholdMethod);
    object[QStringLiteral("manualThreshold")] = manualThreshold;
    object[QStringLiteral("compressSharedImages")] = compressSharedImages;
    object[QStringLiteral("downsample")] = downsample;
    object[QStringLiteral("downsampleDpi")] = downsampleDpi;
    object[QStringLiteral("jpegQuality")] = jpegQuality;
    return object;
}

PDFOCRCompressionSettings PDFOCRCompressionSettings::fromJson(const QJsonObject& object)
{
    PDFOCRCompressionSettings settings;

    const int mode = object.value(QStringLiteral("mode")).toInt(static_cast<int>(settings.mode));
    if (mode >= static_cast<int>(PDFOCRCompressionMode::Off) && mode <= static_cast<int>(PDFOCRCompressionMode::Custom))
    {
        settings.mode = static_cast<PDFOCRCompressionMode>(mode);
    }

    const int encoding = object.value(QStringLiteral("bitonalEncoding")).toInt(static_cast<int>(settings.bitonalEncoding));
    if (encoding >= static_cast<int>(PDFOCRBitonalEncoding::Smallest) && encoding <= static_cast<int>(PDFOCRBitonalEncoding::Flate))
    {
        settings.bitonalEncoding = static_cast<PDFOCRBitonalEncoding>(encoding);
    }

    const int method = object.value(QStringLiteral("thresholdMethod")).toInt(static_cast<int>(settings.thresholdMethod));
    if (method >= static_cast<int>(PDFOCRThresholdMethod::Automatic) && method <= static_cast<int>(PDFOCRThresholdMethod::Manual))
    {
        settings.thresholdMethod = static_cast<PDFOCRThresholdMethod>(method);
    }

    settings.manualThreshold = qBound(0, object.value(QStringLiteral("manualThreshold")).toInt(settings.manualThreshold), 255);
    settings.compressSharedImages = object.value(QStringLiteral("compressSharedImages")).toBool(settings.compressSharedImages);
    settings.downsample = object.value(QStringLiteral("downsample")).toBool(settings.downsample);
    settings.downsampleDpi = qBound(72, object.value(QStringLiteral("downsampleDpi")).toInt(settings.downsampleDpi), 2400);
    settings.jpegQuality = qBound(1, object.value(QStringLiteral("jpegQuality")).toInt(settings.jpegQuality), 100);
    return settings;
}

QStringList PDFOCRCompressionSettings::validate() const
{
    QStringList errors;
    if (manualThreshold < 0 || manualThreshold > 255)
    {
        errors << PDFTranslationContext::tr("Threshold of the conversion to black and white must be in the range 0-255.");
    }
    if (downsampleDpi < 72 || downsampleDpi > 2400)
    {
        errors << PDFTranslationContext::tr("Resolution of the downsampling must be in the range 72-2400 DPI.");
    }
    if (jpegQuality < 1 || jpegQuality > 100)
    {
        errors << PDFTranslationContext::tr("Quality of JPEG must be in the range 1-100.");
    }
    return errors;
}

QString PDFOCRCompressionSettings::getModeName(PDFOCRCompressionMode mode)
{
    switch (mode)
    {
        case PDFOCRCompressionMode::Off:
            return PDFTranslationContext::tr("Off");
        case PDFOCRCompressionMode::Lossless:
            return PDFTranslationContext::tr("Lossless");
        case PDFOCRCompressionMode::BitonalTextScans:
            return PDFTranslationContext::tr("Black and white text scans");
        case PDFOCRCompressionMode::Custom:
            return PDFTranslationContext::tr("Custom");
    }
    return QString();
}

QString PDFOCRCompressionSettings::getModeDescription(PDFOCRCompressionMode mode)
{
    switch (mode)
    {
        case PDFOCRCompressionMode::Off:
            return PDFTranslationContext::tr("The scanned images are not changed.");
        case PDFOCRCompressionMode::Lossless:
            return PDFTranslationContext::tr("Lossless: black and white images are encoded by JBIG2 (generic region) or CCITT G4, gray and color images by Flate, only if the result is smaller. The pages look exactly the same.");
        case PDFOCRCompressionMode::BitonalTextScans:
            return PDFTranslationContext::tr("LOSSY: gray and color scans of a text are converted to black and white and encoded by JBIG2 or CCITT G4. Pictures are compressed losslessly. Check the preview.");
        case PDFOCRCompressionMode::Custom:
            return PDFTranslationContext::tr("Possibly LOSSY: the image optimizer chooses the encoding (JPEG for photos) and may downsample the images. Check the preview.");
    }
    return QString();
}

QString PDFOCRCompressionSettings::getBitonalEncodingName(PDFOCRBitonalEncoding encoding)
{
    switch (encoding)
    {
        case PDFOCRBitonalEncoding::Smallest:
            return PDFTranslationContext::tr("Smallest of JBIG2, CCITT G4 and Flate");
        case PDFOCRBitonalEncoding::JBIG2:
            return PDFTranslationContext::tr("JBIG2 (generic region)");
        case PDFOCRBitonalEncoding::CCITTGroup4:
            return PDFTranslationContext::tr("CCITT G4");
        case PDFOCRBitonalEncoding::Flate:
            return PDFTranslationContext::tr("Flate");
    }
    return QString();
}

QString PDFOCRCompressionSettings::getThresholdMethodName(PDFOCRThresholdMethod method)
{
    switch (method)
    {
        case PDFOCRThresholdMethod::Automatic:
            return PDFTranslationContext::tr("Automatic (Otsu)");
        case PDFOCRThresholdMethod::Adaptive:
            return PDFTranslationContext::tr("Adaptive");
        case PDFOCRThresholdMethod::Manual:
            return PDFTranslationContext::tr("Manual");
    }
    return QString();
}

// -------------------------------------------------------------------------
// PDFOCRCompressionImageResult, PDFOCRCompressionReport
// -------------------------------------------------------------------------

QString PDFOCRCompressionImageResult::getActionName(Action action)
{
    switch (action)
    {
        case Action::Compressed:
            return PDFTranslationContext::tr("compressed");
        case Action::KeptLarger:
            return PDFTranslationContext::tr("kept (the new encoding is not smaller)");
        case Action::SkippedShared:
            return PDFTranslationContext::tr("skipped (shared with a page, which is not written)");
        case Action::SkippedExcluded:
            return PDFTranslationContext::tr("skipped (excluded)");
        case Action::SkippedUnsupported:
            return PDFTranslationContext::tr("skipped (not supported)");
        case Action::Failed:
            return PDFTranslationContext::tr("failed");
    }
    return QString();
}

QString PDFOCRCompressionImageResult::getImageClassName(ImageClass imageClass)
{
    switch (imageClass)
    {
        case ImageClass::Bitonal:
            return PDFTranslationContext::tr("black and white");
        case ImageClass::TextScan:
            return PDFTranslationContext::tr("scan of a text");
        case ImageClass::Picture:
            return PDFTranslationContext::tr("picture");
        case ImageClass::Unknown:
            return PDFTranslationContext::tr("unknown");
    }
    return QString();
}

int PDFOCRCompressionReport::getCount(PDFOCRCompressionImageResult::Action action) const
{
    return int(std::count_if(images.begin(), images.end(), [action](const PDFOCRCompressionImageResult& image) { return image.action == action; }));
}

qint64 PDFOCRCompressionReport::getOriginalBytes() const
{
    qint64 bytes = 0;
    for (const PDFOCRCompressionImageResult& image : images)
    {
        if (image.action == PDFOCRCompressionImageResult::Action::Compressed)
        {
            bytes += image.originalBytes;
        }
    }
    return bytes;
}

qint64 PDFOCRCompressionReport::getNewBytes() const
{
    qint64 bytes = 0;
    for (const PDFOCRCompressionImageResult& image : images)
    {
        if (image.action == PDFOCRCompressionImageResult::Action::Compressed)
        {
            bytes += image.newBytes;
        }
    }
    return bytes;
}

QString PDFOCRCompressionReport::formatBytes(qint64 bytes)
{
    if (bytes >= qint64(1024) * 1024)
    {
        return PDFTranslationContext::tr("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return PDFTranslationContext::tr("%1 kB").arg(double(bytes) / 1024.0, 0, 'f', 1);
}

QString PDFOCRCompressionReport::getSummary() const
{
    using Action = PDFOCRCompressionImageResult::Action;

    QStringList parts;
    parts << PDFTranslationContext::tr("%1 compressed").arg(getCount(Action::Compressed));
    if (const int count = getCount(Action::KeptLarger))
    {
        parts << PDFTranslationContext::tr("%1 kept (larger)").arg(count);
    }
    if (const int count = getCount(Action::SkippedShared))
    {
        parts << PDFTranslationContext::tr("%1 skipped (shared)").arg(count);
    }
    if (const int count = getCount(Action::SkippedExcluded))
    {
        parts << PDFTranslationContext::tr("%1 excluded").arg(count);
    }
    if (const int count = getCount(Action::SkippedUnsupported))
    {
        parts << PDFTranslationContext::tr("%1 not supported").arg(count);
    }
    if (const int count = getCount(Action::Failed))
    {
        parts << PDFTranslationContext::tr("%1 failed").arg(count);
    }

    QString summary = PDFTranslationContext::tr("Images: %1.").arg(parts.join(QStringLiteral(", ")));
    if (isChanged())
    {
        summary += QChar(' ') + PDFTranslationContext::tr("Size %1 -> %2.").arg(formatBytes(getOriginalBytes()), formatBytes(getNewBytes()));
    }
    if (versionRaised)
    {
        summary += QChar(' ') + PDFTranslationContext::tr("The version of the document was raised to PDF 1.4 (JBIG2).");
    }
    return summary;
}

// -------------------------------------------------------------------------
// PDFOCRImageCompressor
// -------------------------------------------------------------------------

namespace
{

using ImageResult = PDFOCRCompressionImageResult;

/// Image, which was encoded
struct EncodedImage
{
    ImageResult result;
    PDFStream stream;
    QImage preview;
    double encodedDpi = 0.0;
    bool usesJbig2 = false;
};

QString getFilterName(const PDFDocument* document, const PDFDictionary* dictionary)
{
    const PDFObject& filter = document->getObject(dictionary->get("Filter"));
    if (filter.isName())
    {
        return QString::fromLatin1(filter.getString());
    }
    if (filter.isArray() && filter.getArray()->getCount() > 0)
    {
        const PDFObject& last = document->getObject(filter.getArray()->getItem(filter.getArray()->getCount() - 1));
        if (last.isName())
        {
            return QString::fromLatin1(last.getString());
        }
    }
    return QString();
}

QString getEncodingName(PDFImage::ImageCompression compression)
{
    switch (compression)
    {
        case PDFImage::ImageCompression::Flate:
            return QStringLiteral("Flate");
        case PDFImage::ImageCompression::JPEG:
            return QStringLiteral("JPEG");
        case PDFImage::ImageCompression::JPEG2000:
            return QStringLiteral("JPEG 2000");
        case PDFImage::ImageCompression::RunLength:
            return QStringLiteral("RunLength");
        case PDFImage::ImageCompression::CCITTGroup4:
            return QStringLiteral("CCITT G4");
        case PDFImage::ImageCompression::JBIG2:
            return QStringLiteral("JBIG2");
    }
    return QString();
}

/// Encodes a black and white image (samples 0 or 255) by the encoding of the settings.
/// Returns false, if no encoding succeeded.
bool encodeBitonal(const QImage& bitonal, PDFOCRBitonalEncoding encoding, PDFStream* stream, QString* encodingName, bool* usesJbig2)
{
    std::vector<PDFImage::ImageCompression> candidates;
    switch (encoding)
    {
        case PDFOCRBitonalEncoding::Smallest:
            candidates = { PDFImage::ImageCompression::JBIG2, PDFImage::ImageCompression::CCITTGroup4, PDFImage::ImageCompression::Flate };
            break;
        case PDFOCRBitonalEncoding::JBIG2:
            candidates = { PDFImage::ImageCompression::JBIG2 };
            break;
        case PDFOCRBitonalEncoding::CCITTGroup4:
            candidates = { PDFImage::ImageCompression::CCITTGroup4 };
            break;
        case PDFOCRBitonalEncoding::Flate:
            candidates = { PDFImage::ImageCompression::Flate };
            break;
    }

    PDFRenderErrorReporterDummy reporter;
    bool hasResult = false;
    qint64 bestSize = std::numeric_limits<qint64>::max();

    for (PDFImage::ImageCompression compression : candidates)
    {
        PDFImage::ImageEncodeOptions options;
        options.compression = compression;
        options.colorMode = PDFImage::ImageColorMode::Monochrome;
        options.monochromeThreshold = 128;
        options.enablePngPredictor = false;

        try
        {
            PDFStream candidate = PDFImage::createStreamFromImage(bitonal, options, &reporter);
            const qint64 size = candidate.getContent() ? candidate.getContent()->size() : std::numeric_limits<qint64>::max();

            // The encoder falls back to Flate, when the algorithm cannot encode the
            // image; the stream is taken as it is, its filter tells the truth
            if (size < bestSize)
            {
                bestSize = size;
                *stream = std::move(candidate);
                *encodingName = getEncodingName(compression);
                *usesJbig2 = compression == PDFImage::ImageCompression::JBIG2;
                hasResult = true;
            }
        }
        catch (const PDFException&)
        {
            // Other candidates are tried
        }
    }

    if (hasResult && stream->getDictionary())
    {
        // The real filter of the stream (an encoder can fall back to Flate)
        const PDFObject& filter = stream->getDictionary()->get("Filter");
        if (filter.isName())
        {
            const QByteArray name = filter.getString();
            *usesJbig2 = name == "JBIG2Decode";
            if (name == "CCITTFaxDecode")
            {
                *encodingName = QStringLiteral("CCITT G4");
            }
            else if (name == "JBIG2Decode")
            {
                *encodingName = QStringLiteral("JBIG2");
            }
            else if (name == "FlateDecode")
            {
                *encodingName = QStringLiteral("Flate");
            }
        }
    }

    return hasResult;
}

/// Returns true, if all pixels of the image are black or white
bool isBitonalImage(const QImage& image)
{
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < gray.height(); ++y)
    {
        const uchar* row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
        {
            if (row[x] != 0 && row[x] != 255)
            {
                return false;
            }
        }
    }
    return true;
}

/// Compression of the images of a document, shared by the compression and by the preview
class ImageCompressionJob
{
public:
    ImageCompressionJob(const PDFDocument* document, const PDFOCRCompressionSettings& settings, const std::set<PDFInteger>& processedPages, bool keepPreview) :
        m_document(document),
        m_settings(settings),
        m_processedPages(processedPages),
        m_keepPreview(keepPreview),
        m_usage(PDFOCRImageCompressor::getImageUsage(document))
    {

    }

    /// Processes an image drawn on the page
    void processImage(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference, PDFInteger pageIndex, double dpi, const PDFCMS* cms, const PDFOperationControl* operationControl);

    std::map<PDFObjectReference, EncodedImage> takeImages() { return std::move(m_images); }

    int getMaximumConcurrentImages() const { return m_maximumConcurrentImages.load(); }

private:
    /// Checks the image, which cannot be re-encoded. Returns the reason, or empty string.
    std::optional<ImageResult::Action> checkImage(const PDFImage& image, const PDFDictionary* dictionary, PDFObjectReference reference, QString* message) const;

    /// Encodes the decoded image. Returns false, if the image is not re-encoded.
    bool encode(const QImage& decoded, const PDFImage& image, const PDFDictionary* dictionary, double dpi, EncodedImage& encoded) const;

    const PDFDocument* m_document;
    PDFOCRCompressionSettings m_settings;
    std::set<PDFInteger> m_processedPages;
    bool m_keepPreview;
    std::map<PDFObjectReference, std::vector<PDFInteger>> m_usage;

    QMutex m_mutex;
    std::map<PDFObjectReference, EncodedImage> m_images;
    std::set<PDFObjectReference> m_inProgress;
    std::map<PDFObjectReference, std::vector<PDFInteger>> m_pendingPages;
    std::atomic<int> m_concurrentImages = 0;
    std::atomic<int> m_maximumConcurrentImages = 0;
};

std::optional<ImageResult::Action> ImageCompressionJob::checkImage(const PDFImage& image, const PDFDictionary* dictionary, PDFObjectReference reference, QString* message) const
{
    PDFDocumentDataLoaderDecorator loader(m_document);

    if (m_settings.isExcluded(reference))
    {
        return ImageResult::Action::SkippedExcluded;
    }

    // The image drawn on a page, which is not written, is left as it is by default
    auto usageIt = m_usage.find(reference);
    if (usageIt != m_usage.end() && !m_settings.compressSharedImages)
    {
        for (PDFInteger page : usageIt->second)
        {
            if (!m_processedPages.count(page))
            {
                *message = PDFTranslationContext::tr("The image is drawn also on the page %1.").arg(page + 1);
                return ImageResult::Action::SkippedShared;
            }
        }
    }

    if (loader.readBooleanFromDictionary(dictionary, "ImageMask", false) || image.getImageData().getMaskingType() == PDFImageData::MaskingType::ImageMask)
    {
        *message = PDFTranslationContext::tr("Stencil masks are not re-encoded.");
        return ImageResult::Action::SkippedUnsupported;
    }

    if (dictionary->hasKey("SMask") || dictionary->hasKey("Mask") || dictionary->hasKey("SMaskInData") ||
        image.getImageData().getMaskingType() != PDFImageData::MaskingType::None)
    {
        *message = PDFTranslationContext::tr("Images with transparency are not re-encoded.");
        return ImageResult::Action::SkippedUnsupported;
    }

    const PDFAbstractColorSpace* colorSpace = image.getColorSpace().data();
    if (!colorSpace)
    {
        *message = PDFTranslationContext::tr("The color space of the image is not known.");
        return ImageResult::Action::SkippedUnsupported;
    }

    return std::nullopt;
}

bool ImageCompressionJob::encode(const QImage& decoded, const PDFImage& image, const PDFDictionary* dictionary, double dpi, EncodedImage& encoded) const
{
    const PDFAbstractColorSpace* colorSpace = image.getColorSpace().data();
    const PDFAbstractColorSpace::ColorSpace colorSpaceType = colorSpace->getColorSpace();
    const bool isDeviceGray = colorSpaceType == PDFAbstractColorSpace::ColorSpace::DeviceGray;
    const bool isDeviceRGB = colorSpaceType == PDFAbstractColorSpace::ColorSpace::DeviceRGB;
    const int bitsPerComponent = int(image.getImageData().getBitsPerComponent());
    const QString filter = encoded.result.originalFilter;
    const bool isLossySource = filter == QLatin1String("DCTDecode") || filter == QLatin1String("JPXDecode");

    // Classification of the content
    const bool bitonal = (isDeviceGray && bitsPerComponent == 1) || isBitonalImage(decoded);
    if (bitonal)
    {
        encoded.result.imageClass = ImageResult::ImageClass::Bitonal;
    }
    else if (PDFOCRImageCompressor::isTextScan(decoded))
    {
        encoded.result.imageClass = ImageResult::ImageClass::TextScan;
    }
    else
    {
        encoded.result.imageClass = ImageResult::ImageClass::Picture;
    }

    // Custom mode: the image optimizer decides the encoding
    if (m_settings.mode == PDFOCRCompressionMode::Custom)
    {
        PDFImageOptimizer::Settings optimizerSettings = PDFImageOptimizer::Settings::createDefault();
        optimizerSettings.enabled = true;
        optimizerSettings.autoMode = true;
        optimizerSettings.keepOriginalIfLarger = true;
        optimizerSettings.preserveTransparency = false;
        for (PDFImageOptimizer::CompressionProfile* profile : { &optimizerSettings.colorProfile, &optimizerSettings.grayProfile })
        {
            profile->targetDpi = m_settings.downsample ? m_settings.downsampleDpi : 0;
            profile->jpegQuality = m_settings.jpegQuality;
        }
        optimizerSettings.bitonalProfile.targetDpi = m_settings.downsample ? m_settings.downsampleDpi : 0;
        switch (m_settings.bitonalEncoding)
        {
            case PDFOCRBitonalEncoding::Smallest:
            case PDFOCRBitonalEncoding::JBIG2:
                optimizerSettings.bitonalProfile.algorithm = PDFImageOptimizer::CompressionAlgorithm::JBIG2;
                break;
            case PDFOCRBitonalEncoding::CCITTGroup4:
                optimizerSettings.bitonalProfile.algorithm = PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4;
                break;
            case PDFOCRBitonalEncoding::Flate:
                optimizerSettings.bitonalProfile.algorithm = PDFImageOptimizer::CompressionAlgorithm::Flate;
                break;
        }

        PDFImageOptimizer::ImageInfo info;
        info.image = decoded;
        info.minimalDpi = QPointF(dpi, dpi);
        info.pixelSize = decoded.size();
        info.bitsPerComponent = bitsPerComponent;
        info.filterName = filter;
        info.originalBytes = int(qMin<qint64>(encoded.result.originalBytes, std::numeric_limits<int>::max()));

        const PDFImageOptimizer::ResolvedPlan plan = PDFImageOptimizer::resolvePlan(info, optimizerSettings);
        PDFRenderErrorReporterDummy reporter;
        encoded.stream = PDFImage::createStreamFromImage(decoded, plan.encodeOptions, &reporter);
        encoded.result.encoding = getEncodingName(plan.encodeOptions.compression);
        encoded.usesJbig2 = plan.encodeOptions.compression == PDFImage::ImageCompression::JBIG2;
        if (m_keepPreview)
        {
            encoded.preview = PDFImageOptimizer::createPreviewImage(info, plan, true);
        }
        return true;
    }

    // Black and white images: lossless re-encoding of the 1-bit samples
    if (bitonal)
    {
        if (!encodeBitonal(decoded, m_settings.bitonalEncoding, &encoded.stream, &encoded.result.encoding, &encoded.usesJbig2))
        {
            return false;
        }
        if (m_keepPreview)
        {
            encoded.preview = decoded.convertToFormat(QImage::Format_Grayscale8);
        }
        return true;
    }

    // Scans of a text converted to black and white (lossy, explicit choice of the user)
    if (m_settings.mode == PDFOCRCompressionMode::BitonalTextScans && encoded.result.imageClass == ImageResult::ImageClass::TextScan)
    {
        const QImage converted = PDFOCRImageCompressor::toBitonal(decoded, m_settings, nullptr);
        if (converted.isNull() || !encodeBitonal(converted, m_settings.bitonalEncoding, &encoded.stream, &encoded.result.encoding, &encoded.usesJbig2))
        {
            return false;
        }
        if (m_keepPreview)
        {
            encoded.preview = converted;
        }
        return true;
    }

    // Lossless re-encoding of gray and color images. The samples of the device color
    // spaces are exact; other color spaces (ICC, Indexed, CMYK, ...) and the lossy
    // sources (JPEG) would not be smaller or would change the colors.
    if (isLossySource)
    {
        encoded.result.message = PDFTranslationContext::tr("The source is a lossy JPEG image, a lossless encoding would be larger.");
        return false;
    }
    if (!(isDeviceGray || isDeviceRGB) || bitsPerComponent != 8)
    {
        encoded.result.message = PDFTranslationContext::tr("Only 8-bit DeviceGray and DeviceRGB images are re-encoded losslessly.");
        return false;
    }
    if (!image.getImageData().getDecode().empty())
    {
        encoded.result.message = PDFTranslationContext::tr("Images with a decode array are not re-encoded losslessly.");
        return false;
    }

    Q_UNUSED(dictionary);

    PDFImage::ImageEncodeOptions options;
    options.compression = PDFImage::ImageCompression::Flate;
    options.colorMode = isDeviceGray ? PDFImage::ImageColorMode::Grayscale : PDFImage::ImageColorMode::Color;
    options.enablePngPredictor = true;
    PDFRenderErrorReporterDummy reporter;
    encoded.stream = PDFImage::createStreamFromImage(decoded, options, &reporter);
    encoded.result.encoding = getEncodingName(options.compression);
    if (m_keepPreview)
    {
        encoded.preview = decoded;
    }
    return true;
}

void ImageCompressionJob::processImage(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference, PDFInteger pageIndex, double dpi, const PDFCMS* cms, const PDFOperationControl* operationControl)
{
    const PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
    if (!reference.isValid() || !dictionary)
    {
        // Inline images and direct streams are not shared objects, they are part of the content
        return;
    }

    {
        QMutexLocker lock(&m_mutex);

        auto it = m_images.find(reference);
        if (it != m_images.end())
        {
            EncodedImage& existing = it->second;
            if (std::find(existing.result.pages.begin(), existing.result.pages.end(), pageIndex) == existing.result.pages.end())
            {
                existing.result.pages.push_back(pageIndex);
            }

            // A downsampled image is encoded again, if it is drawn larger elsewhere
            // (a lower resolution); otherwise the first encoding is final
            const bool needsReencoding = m_settings.mode == PDFOCRCompressionMode::Custom && m_settings.downsample &&
                                         existing.result.action == ImageResult::Action::Compressed && dpi > 0.0 && dpi < existing.encodedDpi * 0.99;
            if (!needsReencoding)
            {
                return;
            }
        }

        if (m_inProgress.count(reference))
        {
            // The image is being encoded by another page right now, the page is recorded
            // and added to the pages of the image, when its encoding is stored
            m_pendingPages[reference].push_back(pageIndex);
            return;
        }
        m_inProgress.insert(reference);
    }

    auto inProgressGuard = qScopeGuard([&]()
    {
        QMutexLocker lock(&m_mutex);
        m_inProgress.erase(reference);
    });

    EncodedImage encoded;
    encoded.result.reference = reference;
    encoded.result.pages = { pageIndex };
    encoded.result.pixelSize = QSize(int(image.getImageData().getWidth()), int(image.getImageData().getHeight()));
    encoded.result.originalFilter = getFilterName(m_document, dictionary);
    encoded.result.originalBytes = stream->getContent() ? stream->getContent()->size() : 0;
    encoded.encodedDpi = dpi;

    QString message;
    if (std::optional<ImageResult::Action> action = checkImage(image, dictionary, reference, &message))
    {
        encoded.result.action = *action;
        encoded.result.message = message;
    }
    else
    {
        // Number of the images decoded at the same time (diagnostics of the memory)
        const int concurrent = ++m_concurrentImages;
        int maximum = m_maximumConcurrentImages.load();
        while (concurrent > maximum && !m_maximumConcurrentImages.compare_exchange_weak(maximum, concurrent))
        {
        }
        auto concurrentGuard = qScopeGuard([this]() { --m_concurrentImages; });

        try
        {
            PDFRenderErrorReporterDummy reporter;
            const QImage decoded = image.getImage(cms, &reporter, operationControl);
            if (decoded.isNull())
            {
                encoded.result.action = ImageResult::Action::Failed;
                encoded.result.message = PDFTranslationContext::tr("The image cannot be decoded.");
            }
            else if (!encode(decoded, image, dictionary, dpi, encoded))
            {
                encoded.result.action = ImageResult::Action::SkippedUnsupported;
                encoded.preview = QImage();
            }
            else
            {
                encoded.result.newBytes = encoded.stream.getContent() ? encoded.stream.getContent()->size() : 0;
                if (encoded.result.newBytes <= 0 || encoded.result.newBytes >= encoded.result.originalBytes)
                {
                    encoded.result.action = ImageResult::Action::KeptLarger;
                    encoded.stream = PDFStream();
                }
                else
                {
                    encoded.result.action = ImageResult::Action::Compressed;

                    // The other entries of the image dictionary (Interpolate, Intent, OC,
                    // Metadata, StructParent, ...) are kept, the entries describing the
                    // samples are replaced by the new encoding
                    static const std::unordered_set<QByteArray> replacedKeys =
                    {
                        "Type", "Subtype", "Width", "Height", "BitsPerComponent", "ColorSpace", "Filter",
                        "DecodeParms", "Length", "Decode", "Mask", "SMask", "ImageMask", "SMaskInData"
                    };

                    PDFDictionary merged = *encoded.stream.getDictionary();
                    for (size_t i = 0; i < dictionary->getCount(); ++i)
                    {
                        const QByteArray key = dictionary->getKey(i).getString();
                        if (!replacedKeys.count(key) && !merged.hasKey(key))
                        {
                            merged.addEntry(dictionary->getKey(i), PDFObject(dictionary->getValue(i)));
                        }
                    }
                    const QByteArray* content = encoded.stream.getContent();
                    encoded.stream = PDFStream(std::move(merged), content ? QByteArray(*content) : QByteArray());
                }
            }

        }
        catch (const PDFException& exception)
        {
            encoded.result.action = ImageResult::Action::Failed;
            encoded.result.message = exception.getMessage();
            encoded.stream = PDFStream();
        }
    }

    // The decoded image is released here, only the encoded stream is kept (memory)
    QMutexLocker lock(&m_mutex);
    auto it = m_images.find(reference);
    if (it != m_images.end())
    {
        // Re-encoding at a lower resolution: the pages of the image are kept
        encoded.result.pages = it->second.result.pages;
    }

    // Pages, which have drawn the image during its encoding
    auto pendingIt = m_pendingPages.find(reference);
    if (pendingIt != m_pendingPages.end())
    {
        for (PDFInteger page : pendingIt->second)
        {
            if (std::find(encoded.result.pages.begin(), encoded.result.pages.end(), page) == encoded.result.pages.end())
            {
                encoded.result.pages.push_back(page);
            }
        }
        m_pendingPages.erase(pendingIt);
    }

    if (it != m_images.end())
    {
        it->second = std::move(encoded);
    }
    else
    {
        m_images.emplace(reference, std::move(encoded));
    }
}

/// Content processor, which only reports the image XObjects drawn on the page
class ImageCollectorProcessor : public PDFPageContentProcessor
{
public:
    using Callback = std::function<void(const PDFImage&, const PDFStream*, PDFObjectReference, double)>;

    ImageCollectorProcessor(const PDFPage* page,
                            const PDFDocument* document,
                            const PDFFontCache* fontCache,
                            const PDFCMS* cms,
                            const PDFOptionalContentActivity* optionalContentActivity,
                            const PDFMeshQualitySettings& meshQualitySettings,
                            Callback callback) :
        PDFPageContentProcessor(page, document, fontCache, cms, optionalContentActivity, QTransform(), meshQualitySettings),
        m_callback(std::move(callback))
    {

    }

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override
    {
        switch (kind)
        {
            case ContentKind::Images:
            case ContentKind::Tiling:
            case ContentKind::Forms:
                return false;

            default:
                return true;
        }
    }

    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override
    {
        if (!isContentSuppressed() && !isProcessingCancelled())
        {
            // Resolution of the image in the user space of the page (smaller of the axes)
            const QTransform ctm = getGraphicState()->getCurrentTransformationMatrix();
            const double widthInches = std::hypot(ctm.m11(), ctm.m12()) * PDF_POINT_TO_INCH;
            const double heightInches = std::hypot(ctm.m21(), ctm.m22()) * PDF_POINT_TO_INCH;
            double dpi = 0.0;
            if (widthInches > 0.0 && heightInches > 0.0)
            {
                dpi = qMin(image.getImageData().getWidth() / widthInches, image.getImageData().getHeight() / heightInches);
            }
            m_callback(image, stream, reference, dpi);
        }
        return true;
    }

private:
    Callback m_callback;
};

/// Runs the processing of the images of the pages
void processPages(const PDFDocument* document, const std::vector<PDFInteger>& pages, ImageCompressionJob& job, qint64 memoryBudget, const PDFOperationControl* operationControl)
{
    PDFOptionalContentActivity optionalContentActivity(document, OCUsage::Export, nullptr);
    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);
    auto fontCacheGuard = qScopeGuard([&fontCache]() { fontCache.setCacheShrinkEnabled(nullptr, true); });

    // A decoded scan of an A4 page at 300 DPI in color has about 35 MB, the encoders
    // need further copies; the number of pages processed at once is bounded by the budget
    constexpr qint64 PAGE_MEMORY_ESTIMATE = qint64(128) << 20;
    const int concurrency = int(qBound<qint64>(1, memoryBudget / PAGE_MEMORY_ESTIMATE, qint64(qMax(1, QThread::idealThreadCount()))));

    const PDFCatalog* catalog = document->getCatalog();
    for (size_t start = 0; start < pages.size(); start += size_t(concurrency))
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        const size_t end = qMin(pages.size(), start + size_t(concurrency));
        auto processPage = [&](PDFInteger pageIndex)
        {
            const PDFPage* page = pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
            if (!page || PDFOperationControl::isOperationCancelled(operationControl))
            {
                return;
            }

            ImageCollectorProcessor processor(page, document, &fontCache, &cms, &optionalContentActivity, meshQualitySettings,
                                              [&](const PDFImage& image, const PDFStream* stream, PDFObjectReference reference, double dpi)
            {
                job.processImage(image, stream, reference, pageIndex, dpi, &cms, operationControl);
            });
            processor.setOperationControl(operationControl);
            processor.processContents();
        };

        if (concurrency > 1)
        {
            PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pages.begin() + start, pages.begin() + end, processPage);
        }
        else
        {
            std::for_each(pages.begin() + start, pages.begin() + end, processPage);
        }
    }
}

void collectXObjectImages(const PDFDocument* document, const PDFObject& resourcesObject, PDFInteger pageIndex, std::set<PDFObjectReference>& visitedForms, std::map<PDFObjectReference, std::vector<PDFInteger>>& usage)
{
    const PDFDictionary* resources = document->getDictionaryFromObject(resourcesObject);
    const PDFDictionary* xobjects = resources ? document->getDictionaryFromObject(resources->get("XObject")) : nullptr;
    if (!xobjects)
    {
        return;
    }

    for (size_t i = 0; i < xobjects->getCount(); ++i)
    {
        const PDFObject& value = xobjects->getValue(i);
        if (!value.isReference())
        {
            continue;
        }

        const PDFObjectReference reference = value.getReference();
        const PDFObject& object = document->getObjectByReference(reference);
        if (!object.isStream())
        {
            continue;
        }

        const PDFDictionary* dictionary = object.getStream()->getDictionary();
        const PDFObject& subtype = document->getObject(dictionary->get("Subtype"));
        if (!subtype.isName())
        {
            continue;
        }

        if (subtype.getString() == "Image")
        {
            std::vector<PDFInteger>& pages = usage[reference];
            if (pages.empty() || pages.back() != pageIndex)
            {
                pages.push_back(pageIndex);
            }
        }
        else if (subtype.getString() == "Form" && visitedForms.insert(reference).second)
        {
            collectXObjectImages(document, dictionary->get("Resources"), pageIndex, visitedForms, usage);
        }
    }
}

} // namespace

std::map<PDFObjectReference, std::vector<PDFInteger>> PDFOCRImageCompressor::getImageUsage(const PDFDocument* document)
{
    std::map<PDFObjectReference, std::vector<PDFInteger>> usage;
    if (!document)
    {
        return usage;
    }

    const PDFCatalog* catalog = document->getCatalog();
    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        if (const PDFPage* page = catalog->getPage(pageIndex))
        {
            std::set<PDFObjectReference> visitedForms;
            collectXObjectImages(document, page->getResources(), PDFInteger(pageIndex), visitedForms, usage);
        }
    }

    return usage;
}

PDFDocument PDFOCRImageCompressor::compress(const PDFDocument* document,
                                            const std::vector<PDFInteger>& pages,
                                            const PDFOCRCompressionSettings& settings,
                                            qint64 memoryBudget,
                                            const PDFOperationControl* operationControl,
                                            PDFOCRCompressionReport* report)
{
    if (!document)
    {
        return PDFDocument();
    }

    PDFObjectStorage storage = document->getStorage();
    PDFVersion version = document->getInfo()->version;

    if (settings.isEnabled() && !pages.empty())
    {
        const std::set<PDFInteger> processedPages(pages.begin(), pages.end());
        ImageCompressionJob job(document, settings, processedPages, false);
        processPages(document, std::vector<PDFInteger>(processedPages.begin(), processedPages.end()), job, memoryBudget, operationControl);

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return PDFDocument(std::move(storage), version, document->getSourceDataHash());
        }

        bool usesJbig2 = false;
        std::map<PDFObjectReference, EncodedImage> images = job.takeImages();
        for (auto& [reference, encoded] : images)
        {
            if (encoded.result.action == ImageResult::Action::Compressed)
            {
                storage.setObject(reference, PDFObject::createStream(std::make_shared<PDFStream>(std::move(encoded.stream))));
                usesJbig2 = usesJbig2 || encoded.usesJbig2;
            }

            std::sort(encoded.result.pages.begin(), encoded.result.pages.end());
            if (report)
            {
                report->images.push_back(std::move(encoded.result));
            }
        }

        if (report)
        {
            report->maximumConcurrentImages = job.getMaximumConcurrentImages();
        }

        // JBIG2Decode is a filter of PDF 1.4
        if (usesJbig2 && (version.major < 1 || (version.major == 1 && version.minor < 4)))
        {
            version = PDFVersion(1, 4);
            if (report)
            {
                report->versionRaised = true;
            }
        }
    }

    return PDFDocument(std::move(storage), version, document->getSourceDataHash());
}

std::vector<PDFOCRImageCompressor::Preview> PDFOCRImageCompressor::createPreview(const PDFDocument* document,
                                                                                 PDFInteger pageIndex,
                                                                                 const std::vector<PDFInteger>& processedPages,
                                                                                 const PDFOCRCompressionSettings& settings,
                                                                                 const PDFOperationControl* operationControl)
{
    std::vector<Preview> previews;
    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return previews;
    }

    // Decoded originals are collected by a separate pass (the job releases them)
    std::map<PDFObjectReference, QImage> originals;
    {
        PDFOptionalContentActivity optionalContentActivity(document, OCUsage::Export, nullptr);
        PDFCMSGeneric cms;
        PDFMeshQualitySettings meshQualitySettings;
        PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
        fontCache.setDocument(modifiedDocument);
        fontCache.setCacheShrinkEnabled(nullptr, false);
        auto fontCacheGuard = qScopeGuard([&fontCache]() { fontCache.setCacheShrinkEnabled(nullptr, true); });

        std::set<PDFInteger> processed(processedPages.begin(), processedPages.end());
        processed.insert(pageIndex);
        PDFOCRCompressionSettings previewSettings = settings;
        if (previewSettings.mode == PDFOCRCompressionMode::Off)
        {
            previewSettings.mode = PDFOCRCompressionMode::Lossless;
        }
        ImageCompressionJob job(document, previewSettings, processed, true);

        ImageCollectorProcessor processor(document->getCatalog()->getPage(pageIndex), document, &fontCache, &cms, &optionalContentActivity, meshQualitySettings,
                                          [&](const PDFImage& image, const PDFStream* stream, PDFObjectReference reference, double dpi)
        {
            if (reference.isValid() && !originals.count(reference))
            {
                PDFRenderErrorReporterDummy reporter;
                originals[reference] = image.getImage(&cms, &reporter, operationControl);
            }
            job.processImage(image, stream, reference, pageIndex, dpi, &cms, operationControl);
        });
        processor.setOperationControl(operationControl);
        processor.processContents();

        std::map<PDFObjectReference, EncodedImage> images = job.takeImages();
        for (auto& [reference, encoded] : images)
        {
            Preview preview;
            preview.result = std::move(encoded.result);
            preview.original = originals[reference];
            preview.compressed = preview.result.action == ImageResult::Action::Compressed ? std::move(encoded.preview) : QImage();
            previews.push_back(std::move(preview));
        }
    }

    return previews;
}

bool PDFOCRImageCompressor::isTextScan(const QImage& image)
{
    if (image.isNull())
    {
        return false;
    }

    constexpr int SAMPLE_SIZE = 1000;
    QImage sample = image;
    if (qMax(image.width(), image.height()) > SAMPLE_SIZE)
    {
        sample = image.scaled(SAMPLE_SIZE, SAMPLE_SIZE, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    sample = sample.convertToFormat(QImage::Format_RGB32);

    qint64 total = 0;
    qint64 colored = 0;
    qint64 dark = 0;
    qint64 light = 0;
    qint64 middle = 0;

    for (int y = 0; y < sample.height(); ++y)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x)
        {
            const QRgb pixel = row[x];
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            const int chroma = qMax(r, qMax(g, b)) - qMin(r, qMin(g, b));
            const int gray = qGray(pixel);

            ++total;
            if (chroma > 48)
            {
                ++colored;
            }
            if (gray < 96)
            {
                ++dark;
            }
            else if (gray > 160)
            {
                ++light;
            }
            else
            {
                ++middle;
            }
        }
    }

    if (total == 0)
    {
        return false;
    }

    // Paper dominates, there is some ink, only a few mid tones (antialiased edges of
    // the glyphs) and almost no color. Photos have a lot of mid tones.
    const double coloredRatio = double(colored) / total;
    const double darkRatio = double(dark) / total;
    const double lightRatio = double(light) / total;
    const double middleRatio = double(middle) / total;
    return coloredRatio < 0.05 && middleRatio < 0.15 && darkRatio > 0.002 && lightRatio > 0.5;
}

QImage PDFOCRImageCompressor::toBitonal(const QImage& image, const PDFOCRCompressionSettings& settings, const PDFOperationControl* operationControl)
{
    PDFImageConversion conversion;
    conversion.setImage(image.convertToFormat(QImage::Format_ARGB32));
    conversion.setAlphaMode(PDFImageConversion::AlphaMode::Composite);
    conversion.setOperationControl(operationControl);

    switch (settings.thresholdMethod)
    {
        case PDFOCRThresholdMethod::Automatic:
            conversion.setConversionMethod(PDFImageConversion::ConversionMethod::Automatic);
            break;
        case PDFOCRThresholdMethod::Adaptive:
            conversion.setConversionMethod(PDFImageConversion::ConversionMethod::Adaptive);
            break;
        case PDFOCRThresholdMethod::Manual:
            conversion.setConversionMethod(PDFImageConversion::ConversionMethod::Manual);
            conversion.setThreshold(settings.manualThreshold);
            break;
    }

    if (!conversion.convert())
    {
        return QImage();
    }

    return conversion.getConvertedImage().convertToFormat(QImage::Format_Grayscale8);
}

}   // namespace pdf
