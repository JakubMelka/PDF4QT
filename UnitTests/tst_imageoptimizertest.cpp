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

#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfimage.h"
#include "pdfimageconversion.h"
#include "pdfimageoptimizer.h"
#include "pdfbitonaldocumentcreator.h"
#include "pdfpage.h"
#include "pdfconstants.h"

#include <QtTest>
#include <QColor>
#include <QImage>

#include <memory>
#include <optional>
#include <random>
#include <tuple>

class ImageOptimizerTest : public QObject
{
    Q_OBJECT

private slots:
    void test_bitonal_conversion_otsu();
    void test_bitonal_conversion_manual();
    void test_bitonal_conversion_color_space_of_methods();
    void test_bitonal_conversion_composites_alpha();
    void test_bitonal_conversion_alpha_mask();
    void test_bitonal_conversion_static_alpha_mask();
    void test_bitonal_conversion_alpha_mode_ignore();
    void test_bitonal_conversion_alpha_adaptive_and_dither();
    void test_bitonal_conversion_constant_images();
    void test_bitonal_conversion_is_cancellable();
    void test_bitonal_conversion_allocation_failure();
    void test_bitonal_creator_normalizes_page_items();
    void test_bitonal_creator_normalizes_image_items();
    void test_bitonal_creator_converts_image();
    void test_bitonal_creator_fill_respects_transparency();
    void test_bitonal_creator_original_mode_converts_nothing();
    void test_bitonal_creator_skips_undecodable_image();
    void test_image_analysis_classification();
    void test_optimizer_keeps_original_if_larger();
    void test_optimizer_skips_disabled_override();
    void test_optimizer_preserve_keeps_bitonal_encoding();
    void test_optimizer_preserves_smask();
    void test_optimizer_reduces_size_for_photo();
    void test_image_stream_bitonal_filters_roundtrip();
    void test_image_stream_bitonal_filters_convert_color_images();
    void test_optimizer_bitonal_algorithms();
    void test_bitonal_creator_uses_jbig2();
    void test_image_stream_jbig2_falls_back_for_large_images();

private:
    static QImage createLineArtImage(int size);
    static QImage createTextScanImage(int size);
    static QImage createPhotoImage(int size);
    static QImage createAlphaImage(int size);
    static QImage createMaskedForegroundImage(int size);
    static QImage extractAlphaMask(const QImage& image);
    static pdf::PDFDocument createDocumentWithImage(const QImage& image,
                                                    bool addSoftMask,
                                                    pdf::PDFImage::ImageCompression compression);

    /// Replaces the data of the image by a garbage, which its filter cannot decode.
    /// The dictionary of the image (its size, color space and filter) stays valid.
    static pdf::PDFDocument damageImageData(const pdf::PDFDocument& document, pdf::PDFObjectReference imageReference);
};

QImage ImageOptimizerTest::createLineArtImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::white);

    for (int x = 4; x < size - 4; ++x)
    {
        image.setPixel(x, 4, qRgb(0, 0, 0));
        image.setPixel(x, size - 5, qRgb(0, 0, 0));
    }

    for (int y = 4; y < size - 4; ++y)
    {
        image.setPixel(4, y, qRgb(0, 0, 0));
        image.setPixel(size - 5, y, qRgb(0, 0, 0));
    }

    for (int i = 8; i < size - 8; i += 4)
    {
        image.setPixel(i, i, qRgb(0, 0, 0));
        image.setPixel(size - 1 - i, i, qRgb(0, 0, 0));
    }

    for (int x = 8; x < size - 8; x += 6)
    {
        image.setPixel(x, size / 2, qRgb(200, 0, 0));
    }

    return image;
}

QImage ImageOptimizerTest::createTextScanImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::white);

    for (int y = 10; y < size - 10; y += 8)
    {
        for (int x = 8; x < size - 8; ++x)
        {
            image.setPixel(x, y, qRgb(0, 0, 0));
        }
    }

    return image;
}

QImage ImageOptimizerTest::createPhotoImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int r = dist(rng);
            const int g = dist(rng);
            const int b = dist(rng);
            image.setPixel(x, y, qRgb(r, g, b));
        }
    }

    return image;
}

QImage ImageOptimizerTest::createAlphaImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32);

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int alpha = static_cast<int>((255.0 * x) / qMax(1, size - 1));
            image.setPixel(x, y, qRgba(30, 120, 200, alpha));
        }
    }

    return image;
}

QImage ImageOptimizerTest::createMaskedForegroundImage(int size)
{
    // Foreground layer of a two-layer (MRC) scan. Color values of the transparent
    // pixels are just a leftover of the lossy compression - they are almost black,
    // but they are never painted. Only the opaque right half is a real content.
    QImage image(size, size, QImage::Format_ARGB32);

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            if (x < size / 2)
            {
                image.setPixel(x, y, qRgba(10, 10, 10, 0));
            }
            else
            {
                const bool isBlack = y >= size / 2;
                image.setPixel(x, y, isBlack ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255));
            }
        }
    }

    return image;
}

QImage ImageOptimizerTest::extractAlphaMask(const QImage& image)
{
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QImage mask(rgba.size(), QImage::Format_Grayscale8);

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

pdf::PDFDocument ImageOptimizerTest::damageImageData(const pdf::PDFDocument& document, pdf::PDFObjectReference imageReference)
{
    const pdf::PDFObject& imageObject = document.getObjectByReference(imageReference);
    Q_ASSERT(imageObject.isStream());

    pdf::PDFDictionary dictionary = *imageObject.getStream()->getDictionary();
    QByteArray garbage("this is not a compressed image stream at all");
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(garbage.size()));

    pdf::PDFDocumentBuilder builder(&document);
    builder.setObject(imageReference, pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), std::move(garbage))));
    return builder.build();
}

pdf::PDFDocument ImageOptimizerTest::createDocumentWithImage(const QImage& image,
                                                             bool addSoftMask,
                                                             pdf::PDFImage::ImageCompression compression)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference pageRef = builder.appendPage(QRectF(0, 0, 200, 200));

    pdf::PDFImage::ImageEncodeOptions options;
    options.compression = compression;
    options.colorMode = pdf::PDFImage::ImageColorMode::Preserve;
    options.enablePngPredictor = true;
    options.alphaHandling = addSoftMask ? pdf::PDFImage::AlphaHandling::DropAlphaPreserveColors
                                        : pdf::PDFImage::AlphaHandling::FlattenToWhite;

    pdf::PDFStream imageStream = pdf::PDFImage::createStreamFromImage(image, options);

    if (addSoftMask)
    {
        QImage maskImage = extractAlphaMask(image);
        pdf::PDFImage::ImageEncodeOptions maskOptions;
        maskOptions.compression = pdf::PDFImage::ImageCompression::Flate;
        maskOptions.colorMode = pdf::PDFImage::ImageColorMode::Grayscale;
        maskOptions.enablePngPredictor = true;
        maskOptions.alphaHandling = pdf::PDFImage::AlphaHandling::FlattenToWhite;

        pdf::PDFStream maskStream = pdf::PDFImage::createStreamFromImage(maskImage, maskOptions);
        pdf::PDFObjectReference maskRef = builder.addObject(
            pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(maskStream)));

        pdf::PDFDictionary dict = *imageStream.getDictionary();
        dict.setEntry(pdf::PDFInplaceOrMemoryString("SMask"), pdf::PDFObject::createReference(maskRef));
        const QByteArray* content = imageStream.getContent();
        QByteArray contentDereferenced = content ? *content : QByteArray();
        imageStream = pdf::PDFStream(std::move(dict), std::move(contentDereferenced));
    }

    pdf::PDFObjectReference imageRef = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(imageStream)));

    QByteArray content("q 200 0 0 200 0 0 cm /Im1 Do Q");
    pdf::PDFDictionary contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                         pdf::PDFObject::createInteger(content.size()));
    pdf::PDFStream contentStream(std::move(contentDict), std::move(content));
    pdf::PDFObjectReference contentRef = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(contentStream)));

    pdf::PDFDictionary xObject;
    xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

    pdf::PDFDictionary resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"),
                       pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(xObject))));

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources))));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentRef));

    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));

    return builder.build();
}

void ImageOptimizerTest::test_bitonal_conversion_otsu()
{
    QImage image(64, 64, QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y)
    {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
        {
            row[x] = static_cast<uchar>((255 * x) / qMax(1, image.width() - 1));
        }
    }

    pdf::PDFImageConversion conversion;
    conversion.setImage(image);
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);

    QVERIFY(conversion.convert());
    QImage result = conversion.getConvertedImage();
    QVERIFY(!result.isNull());
    QCOMPARE(result.format(), QImage::Format_Mono);

    const int threshold = conversion.getThreshold();
    QVERIFY(threshold >= 0 && threshold <= 255);
}

void ImageOptimizerTest::test_bitonal_conversion_manual()
{
    QImage image(2, 1, QImage::Format_Grayscale8);
    image.setPixelColor(0, 0, QColor(50, 50, 50));
    image.setPixelColor(1, 0, QColor(250, 250, 250));

    pdf::PDFImageConversion conversion;
    conversion.setImage(image);
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Manual);
    conversion.setThreshold(200);

    QVERIFY(conversion.convert());
    QImage result = conversion.getConvertedImage();
    QCOMPARE(result.format(), QImage::Format_Mono);
    QCOMPARE(result.pixelIndex(0, 0), 0u);
    QCOMPARE(result.pixelIndex(1, 0), 1u);
}

void ImageOptimizerTest::test_bitonal_conversion_color_space_of_methods()
{
    // Global thresholding works with the HSL lightness, while adaptive thresholding
    // and dithering work with the grayscale (luminance) values. For saturated colors
    // these two representations differ substantially - pure red has the lightness 128
    // but the luminance 87, pure blue has the lightness 128 but the luminance 39. This
    // test pins down, which representation each of the methods uses, so the conversion
    // of opaque color images cannot silently change.
    QImage image(4, 1, QImage::Format_ARGB32);
    image.setPixel(0, 0, qRgb(255, 0, 0));
    image.setPixel(1, 0, qRgb(255, 0, 0));
    image.setPixel(2, 0, qRgb(0, 0, 255));
    image.setPixel(3, 0, qRgb(0, 0, 255));

    {
        // Lightness of both colors is 128, so no pixel is below the threshold
        pdf::PDFImageConversion conversion;
        conversion.setImage(image);
        conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Manual);
        conversion.setThreshold(128);

        QVERIFY(conversion.convert());

        QImage result = conversion.getConvertedImage();
        QCOMPARE(result.format(), QImage::Format_Mono);

        for (int x = 0; x < image.width(); ++x)
        {
            QCOMPARE(result.pixelIndex(x, 0), 1);
        }
    }

    {
        // In the grayscale representation the blue pixels are darker than the local
        // average of the whole image, while the red ones are brighter
        pdf::PDFImageConversion conversion;
        conversion.setImage(image);
        conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Adaptive);

        QVERIFY(conversion.convert());

        QImage result = conversion.getConvertedImage();
        QCOMPARE(result.format(), QImage::Format_Mono);
        QCOMPARE(result.pixelIndex(0, 0), 1);
        QCOMPARE(result.pixelIndex(1, 0), 1);
        QCOMPARE(result.pixelIndex(2, 0), 0);
        QCOMPARE(result.pixelIndex(3, 0), 0);
    }

    {
        // The first pixel does not receive any diffused error yet, so its result is
        // given purely by the threshold - the luminance of the red pixel is below it,
        // although its lightness would be above it
        pdf::PDFImageConversion conversion;
        conversion.setImage(image);
        conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Dither);
        conversion.setThreshold(100);

        QVERIFY(conversion.convert());

        QImage result = conversion.getConvertedImage();
        QCOMPARE(result.format(), QImage::Format_Mono);
        QCOMPARE(result.pixelIndex(0, 0), 0);
    }
}

void ImageOptimizerTest::test_bitonal_conversion_composites_alpha()
{
    constexpr int size = 8;
    QImage image = createMaskedForegroundImage(size);

    pdf::PDFImageConversion conversion;
    conversion.setImage(image);
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);
    conversion.setAlphaMode(pdf::PDFImageConversion::AlphaMode::Composite);

    QVERIFY(conversion.convert());

    QImage result = conversion.getConvertedImage();
    QCOMPARE(result.format(), QImage::Format_Mono);
    QCOMPARE(result.size(), image.size());

    // Transparent pixels are not painted at all - they must become white, even
    // though their color values are almost black.
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size / 2; ++x)
        {
            QCOMPARE(result.pixelIndex(x, y), 1);
        }
    }

    // Opaque pixels are thresholded normally
    QCOMPARE(result.pixelIndex(size / 2, 0), 1);
    QCOMPARE(result.pixelIndex(size - 1, size - 1), 0);
}

void ImageOptimizerTest::test_bitonal_conversion_alpha_mask()
{
    constexpr int size = 8;
    QImage image = createMaskedForegroundImage(size);

    pdf::PDFImageConversion conversion;
    conversion.setImage(image);
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);

    QVERIFY(conversion.convert());
    QVERIFY(conversion.hasConvertedAlphaMask());

    QImage mask = conversion.getConvertedAlphaMask();
    QCOMPARE(mask.format(), QImage::Format_Mono);
    QCOMPARE(mask.size(), image.size());

    // Set sample of the mask means an opaque pixel
    QCOMPARE(mask.pixelIndex(0, 0), 0);
    QCOMPARE(mask.pixelIndex(size / 2 - 1, size - 1), 0);
    QCOMPARE(mask.pixelIndex(size / 2, 0), 1);
    QCOMPARE(mask.pixelIndex(size - 1, size - 1), 1);

    // Fully opaque image does not produce any mask
    QImage opaqueImage(4, 4, QImage::Format_RGB32);
    opaqueImage.fill(Qt::white);

    conversion.setImage(opaqueImage);
    QVERIFY(conversion.convert());
    QVERIFY(!conversion.hasConvertedAlphaMask());
    QVERIFY(conversion.getConvertedAlphaMask().isNull());
}

void ImageOptimizerTest::test_bitonal_conversion_static_alpha_mask()
{
    constexpr int size = 8;
    QImage image = createMaskedForegroundImage(size);

    // The mask created without the conversion must agree with the one created by it,
    // otherwise an item replaced by a solid fill would have a different transparency
    // than the same item converted by the algorithm.
    pdf::PDFImageConversion conversion;
    conversion.setImage(image);
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);
    QVERIFY(conversion.convert());

    std::optional<QImage> maskResult = pdf::PDFImageConversion::createAlphaMask(image);
    QVERIFY(maskResult.has_value());
    const QImage staticMask = *maskResult;
    QCOMPARE(staticMask.format(), QImage::Format_Mono);
    QCOMPARE(staticMask.size(), image.size());
    QCOMPARE(staticMask, conversion.getConvertedAlphaMask());

    // Set sample of the mask means an opaque pixel
    QCOMPARE(staticMask.pixelIndex(0, 0), 0);
    QCOMPARE(staticMask.pixelIndex(size / 2, 0), 1);

    // Neither a fully opaque image nor an image without the alpha channel has a mask.
    // That is a legitimate result, not a failure - the optional holds a null image.
    QImage opaqueImage(size, size, QImage::Format_ARGB32);
    opaqueImage.fill(Qt::white);
    maskResult = pdf::PDFImageConversion::createAlphaMask(opaqueImage);
    QVERIFY(maskResult.has_value());
    QVERIFY(maskResult->isNull());

    QImage imageWithoutAlpha(size, size, QImage::Format_RGB32);
    imageWithoutAlpha.fill(Qt::red);
    maskResult = pdf::PDFImageConversion::createAlphaMask(imageWithoutAlpha);
    QVERIFY(maskResult.has_value());
    QVERIFY(maskResult->isNull());

    maskResult = pdf::PDFImageConversion::createAlphaMask(QImage());
    QVERIFY(maskResult.has_value());
    QVERIFY(maskResult->isNull());
}

void ImageOptimizerTest::test_bitonal_conversion_allocation_failure()
{
    // The bitonal images are written by direct access to their scan lines, so an image,
    // which could not be allocated, must be recognized before anything is written into
    // it. An image of a billion by a billion pixels cannot be allocated, and Qt refuses
    // it without touching the memory.
    QVERIFY(pdf::PDFImageConversion::createBitonalImage(QSize(1 << 30, 1 << 30)).isNull());
    QVERIFY(pdf::PDFImageConversion::createBitonalImage(QSize(0, 0)).isNull());

    // A regular image is allocated and it is black
    const QImage image = pdf::PDFImageConversion::createBitonalImage(QSize(11, 3));
    QVERIFY(!image.isNull());
    QCOMPARE(image.format(), QImage::Format_Mono);
    QCOMPARE(image.size(), QSize(11, 3));

    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            QCOMPARE(image.pixelIndex(x, y), 0);
        }
    }
}

void ImageOptimizerTest::test_bitonal_conversion_alpha_mode_ignore()
{
    constexpr int size = 8;
    QImage image = createMaskedForegroundImage(size);

    pdf::PDFImageConversion conversion;
    conversion.setImage(image);
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Manual);
    conversion.setThreshold(128);
    conversion.setAlphaMode(pdf::PDFImageConversion::AlphaMode::Ignore);

    QVERIFY(conversion.convert());

    QImage result = conversion.getConvertedImage();
    QCOMPARE(result.format(), QImage::Format_Mono);

    // Alpha channel is ignored, so the dark color values of the transparent
    // pixels are thresholded as if they were a real content.
    QCOMPARE(result.pixelIndex(0, 0), 0);
    QVERIFY(!conversion.hasConvertedAlphaMask());
}

void ImageOptimizerTest::test_bitonal_conversion_alpha_adaptive_and_dither()
{
    constexpr int size = 8;
    QImage image = createMaskedForegroundImage(size);

    const pdf::PDFImageConversion::ConversionMethod methods[] =
    {
        pdf::PDFImageConversion::ConversionMethod::Adaptive,
        pdf::PDFImageConversion::ConversionMethod::Dither
    };

    for (pdf::PDFImageConversion::ConversionMethod method : methods)
    {
        pdf::PDFImageConversion conversion;
        conversion.setImage(image);
        conversion.setConversionMethod(method);

        QVERIFY(conversion.convert());

        QImage result = conversion.getConvertedImage();
        QCOMPARE(result.format(), QImage::Format_Mono);
        QCOMPARE(result.size(), image.size());

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size / 2; ++x)
            {
                QCOMPARE(result.pixelIndex(x, y), 1);
            }
        }
    }
}

void ImageOptimizerTest::test_bitonal_conversion_constant_images()
{
    constexpr int size = 8;

    // A constant image has a single intensity, so the inter-class variance of Otsu's
    // method is zero for every threshold and the method has no answer. It must not
    // fall back to the threshold zero, which would turn a black page into a white one.
    auto convertConstantImage = [](QRgb color) -> QImage
    {
        QImage image(size, size, QImage::Format_ARGB32);
        image.fill(color);

        pdf::PDFImageConversion conversion;
        conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);
        conversion.setImage(image);

        if (!conversion.convert())
        {
            return QImage();
        }

        return conversion.getConvertedImage();
    };

    const QImage black = convertConstantImage(qRgb(0, 0, 0));
    QVERIFY(!black.isNull());
    QCOMPARE(black.pixelIndex(0, 0), 0);
    QCOMPARE(black.pixelIndex(size - 1, size - 1), 0);

    const QImage white = convertConstantImage(qRgb(255, 255, 255));
    QVERIFY(!white.isNull());
    QCOMPARE(white.pixelIndex(0, 0), 1);
    QCOMPARE(white.pixelIndex(size - 1, size - 1), 1);

    // A uniform dark gray is below the default threshold, a uniform light gray above it
    const QImage darkGray = convertConstantImage(qRgb(64, 64, 64));
    QVERIFY(!darkGray.isNull());
    QCOMPARE(darkGray.pixelIndex(0, 0), 0);

    const QImage lightGray = convertConstantImage(qRgb(200, 200, 200));
    QVERIFY(!lightGray.isNull());
    QCOMPARE(lightGray.pixelIndex(0, 0), 1);
}

void ImageOptimizerTest::test_bitonal_conversion_is_cancellable()
{
    /// Operation control, which reports the operation as cancelled from the beginning
    class CancelledOperationControl : public pdf::PDFOperationControl
    {
    public:
        virtual bool isOperationCancelled() const override { return true; }
    };

    CancelledOperationControl operationControl;

    pdf::PDFImageConversion conversion;
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);
    conversion.setOperationControl(&operationControl);
    conversion.setImage(createLineArtImage(16));

    // A cancelled conversion produces nothing, so its caller cannot use a partial result
    QVERIFY(!conversion.convert());
    QVERIFY(conversion.getConvertedImage().isNull());
    QVERIFY(conversion.getConvertedAlphaMask().isNull());
}

static const pdf::PDFDictionary* getPageBitonalImageOfDocument(const pdf::PDFDocument& document);
static const pdf::PDFDictionary* getFirstPageImageDictionary(const pdf::PDFDocument& document);

void ImageOptimizerTest::test_bitonal_creator_normalizes_page_items()
{
    constexpr int size = 16;
    pdf::PDFDocument document = createDocumentWithImage(createLineArtImage(size), false, pdf::PDFImage::ImageCompression::Flate);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));

    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Pages;

    // An index of a page, which does not exist, must be dropped before anything is
    // rendered - it would index the vector of the page images out of its bounds
    pdf::PDFBitonalDocumentCreator::ItemInfo invalidItem;
    invalidItem.pageIndex = 42;
    invalidItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite;
    settings.items.push_back(invalidItem);

    pdf::PDFBitonalDocumentCreator::ItemInfo negativeItem;
    negativeItem.pageIndex = -1;
    negativeItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite;
    settings.items.push_back(negativeItem);

    // Only the invalid items are present, so there is nothing to be converted
    QVERIFY(!creator.createBitonalDocument(settings));

    // The same page requested twice must be converted once only - two tasks rendering
    // it in parallel would write into the same slot at the same time
    pdf::PDFBitonalDocumentCreator::ItemInfo duplicatedItem;
    duplicatedItem.pageIndex = 0;
    duplicatedItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillBlack;
    settings.items.push_back(duplicatedItem);
    settings.items.push_back(duplicatedItem);

    QVERIFY(creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    QCOMPARE(bitonalDocument.getCatalog()->getPageCount(), size_t(1));

    const pdf::PDFPage* page = bitonalDocument.getCatalog()->getPage(0);
    QVERIFY(page);

    const pdf::PDFDictionary* resources = bitonalDocument.getDictionaryFromObject(page->getResources());
    QVERIFY(resources);

    const pdf::PDFDictionary* xObjects = bitonalDocument.getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xObjects);

    // The page content has been replaced by the single filled image
    QCOMPARE(xObjects->getCount(), size_t(1));
    QVERIFY(xObjects->hasKey("BitonalImage"));

    // The mode of the last occurrence wins even when it leaves the page untouched -
    // a later item cancels the earlier request
    pdf::PDFBitonalDocumentCreator::ItemInfo originalItem;
    originalItem.pageIndex = 0;
    originalItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::Original;
    settings.items.push_back(originalItem);

    QVERIFY(!creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(0));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    // ... and the other way round, the fill following the untouched page fills it
    settings.items.push_back(duplicatedItem);
    QVERIFY(creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));
    QVERIFY(getPageBitonalImageOfDocument(creator.takeBitonalDocument()));
}

void ImageOptimizerTest::test_bitonal_creator_normalizes_image_items()
{
    constexpr int size = 16;
    pdf::PDFDocument document = createDocumentWithImage(createLineArtImage(size), false, pdf::PDFImage::ImageCompression::Flate);
    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

    const pdf::PDFObjectReference imageReference = creator.getConvertibleImages().front();

    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;

    // An item without an image is ignored
    settings.items.push_back(pdf::PDFBitonalDocumentCreator::ItemInfo());
    QVERIFY(!creator.createBitonalDocument(settings));

    // The same image requested twice is converted once, the mode of the last occurrence
    // wins - a later untouched item cancels the earlier fill
    pdf::PDFBitonalDocumentCreator::ItemInfo fillItem;
    fillItem.imageReference = imageReference;
    fillItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillBlack;

    pdf::PDFBitonalDocumentCreator::ItemInfo originalItem;
    originalItem.imageReference = imageReference;
    originalItem.mode = pdf::PDFBitonalDocumentCreator::ItemMode::Original;

    settings.items.push_back(fillItem);
    settings.items.push_back(originalItem);
    QVERIFY(!creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(0));

    settings.items.push_back(fillItem);
    settings.items.push_back(fillItem);
    QVERIFY(creator.createBitonalDocument(settings));
    QCOMPARE(creator.getConvertedItemCount(), size_t(1));
    QCOMPARE(creator.getFailedItemCount(), size_t(0));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    const pdf::PDFDictionary* imageDictionary = getFirstPageImageDictionary(bitonalDocument);
    QVERIFY(imageDictionary);

    pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0), 1);
}

/// Returns the dictionary of the image XObject placed on the first page of the
/// document. The image is looked up through the page resources, so the test does not
/// depend on the object numbering of the converted document.
/// Returns the dictionary of the image, which the page conversion has placed onto
/// the first page of the document
static const pdf::PDFDictionary* getPageBitonalImageOfDocument(const pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    if (!page)
    {
        return nullptr;
    }

    const pdf::PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
    if (!resources)
    {
        return nullptr;
    }

    const pdf::PDFDictionary* xObjects = document.getDictionaryFromObject(resources->get("XObject"));
    if (!xObjects)
    {
        return nullptr;
    }

    return document.getDictionaryFromObject(xObjects->get("BitonalImage"));
}

static const pdf::PDFDictionary* getFirstPageImageDictionary(const pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);
    if (!page)
    {
        return nullptr;
    }

    const pdf::PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
    if (!resources)
    {
        return nullptr;
    }

    const pdf::PDFDictionary* xObjects = document.getDictionaryFromObject(resources->get("XObject"));
    if (!xObjects)
    {
        return nullptr;
    }

    return document.getDictionaryFromObject(xObjects->get("Im1"));
}

void ImageOptimizerTest::test_bitonal_creator_converts_image()
{
    constexpr int size = 32;
    pdf::PDFDocument document = createDocumentWithImage(createLineArtImage(size), false, pdf::PDFImage::ImageCompression::Flate);

    // The rasterizer pool is needed by the page conversion only
    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

    const std::vector<pdf::PDFObjectReference> images = creator.getConvertibleImages();
    QCOMPARE(images.size(), size_t(1));

    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;
    settings.conversionMethod = pdf::PDFImageConversion::ConversionMethod::Automatic;

    pdf::PDFBitonalDocumentCreator::ItemInfo item;
    item.imageReference = images.front();
    settings.items.push_back(item);

    QVERIFY(creator.createBitonalDocument(settings));

    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
    QCOMPARE(bitonalDocument.getCatalog()->getPageCount(), size_t(1));

    const pdf::PDFDictionary* imageDictionary = getFirstPageImageDictionary(bitonalDocument);
    QVERIFY(imageDictionary);

    // The converted image keeps its resolution and it is bitonal now
    pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0), 1);
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Width", 0), size);
    QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Height", 0), size);

    // A fully opaque image does not need a soft mask
    QVERIFY(!imageDictionary->hasKey("SMask"));
}

void ImageOptimizerTest::test_bitonal_creator_fill_respects_transparency()
{
    constexpr int size = 32;

    // A transparent image keeps its transparency, so the fill covers only the visible
    // part of it. The fill image must have the size of the mask - the soft mask is
    // resampled to the dimensions of the image it belongs to.
    {
        pdf::PDFDocument document = createDocumentWithImage(createMaskedForegroundImage(size), true, pdf::PDFImage::ImageCompression::Flate);
        pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

        const std::vector<pdf::PDFObjectReference> images = creator.getConvertibleImages();
        QCOMPARE(images.size(), size_t(1));

        pdf::PDFBitonalDocumentCreator::Settings settings;
        settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;

        pdf::PDFBitonalDocumentCreator::ItemInfo item;
        item.imageReference = images.front();
        item.mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillBlack;
        settings.items.push_back(item);

        QVERIFY(creator.createBitonalDocument(settings));

        const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
        const pdf::PDFDictionary* imageDictionary = getFirstPageImageDictionary(bitonalDocument);
        QVERIFY(imageDictionary);

        pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0), 1);
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Width", 0), size);
        QVERIFY(imageDictionary->hasKey("SMask"));
    }

    // An opaque image has nothing to be masked, so a single sample stretched over the
    // whole area of the original image is enough
    {
        pdf::PDFDocument document = createDocumentWithImage(createLineArtImage(size), false, pdf::PDFImage::ImageCompression::Flate);
        pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

        pdf::PDFBitonalDocumentCreator::Settings settings;
        settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;

        pdf::PDFBitonalDocumentCreator::ItemInfo item;
        item.imageReference = creator.getConvertibleImages().front();
        item.mode = pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite;
        settings.items.push_back(item);

        QVERIFY(creator.createBitonalDocument(settings));

        const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();
        const pdf::PDFDictionary* imageDictionary = getFirstPageImageDictionary(bitonalDocument);
        QVERIFY(imageDictionary);

        pdf::PDFDocumentDataLoaderDecorator loader(&bitonalDocument);
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0), 1);
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Width", 0), 1);
        QCOMPARE(loader.readIntegerFromDictionary(imageDictionary, "Height", 0), 1);
        QVERIFY(!imageDictionary->hasKey("SMask"));
    }
}

void ImageOptimizerTest::test_bitonal_creator_original_mode_converts_nothing()
{
    constexpr int size = 32;
    pdf::PDFDocument document = createDocumentWithImage(createLineArtImage(size), false, pdf::PDFImage::ImageCompression::Flate);
    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;

    pdf::PDFBitonalDocumentCreator::ItemInfo item;
    item.imageReference = creator.getConvertibleImages().front();
    item.mode = pdf::PDFBitonalDocumentCreator::ItemMode::Original;
    settings.items.push_back(item);

    // Nothing has been converted, so there is no document to be accepted
    QVERIFY(!creator.createBitonalDocument(settings));
    QCOMPARE(creator.takeBitonalDocument().getCatalog()->getPageCount(), size_t(0));
}

void ImageOptimizerTest::test_bitonal_creator_skips_undecodable_image()
{
    constexpr int size = 32;
    const pdf::PDFDocument validDocument = createDocumentWithImage(createMaskedForegroundImage(size), true, pdf::PDFImage::ImageCompression::Flate);
    pdf::PDFBitonalDocumentCreator validCreator(&validDocument, nullptr, nullptr);
    const std::vector<pdf::PDFObjectReference> images = validCreator.getConvertibleImages();
    QCOMPARE(images.size(), size_t(1));

    const pdf::PDFDocument document = damageImageData(validDocument, images.front());
    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);

    // The image is still listed - its dictionary is valid - but it cannot be decoded
    QCOMPARE(creator.getConvertibleImages(), images);
    QVERIFY(creator.getDecodedImage(images.front(), nullptr).isNull());

    // Neither the algorithm nor the fills can replace the image - the fill has to keep
    // the transparency of the image, which is a part of the image, which cannot be
    // decoded. The image is left as it is and it is counted as failed, so a user
    // interface must not offer these modes for it.
    for (const pdf::PDFBitonalDocumentCreator::ItemMode mode : { pdf::PDFBitonalDocumentCreator::ItemMode::Algorithm,
                                                                 pdf::PDFBitonalDocumentCreator::ItemMode::FillBlack,
                                                                 pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite })
    {
        pdf::PDFBitonalDocumentCreator::Settings settings;
        settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;

        pdf::PDFBitonalDocumentCreator::ItemInfo item;
        item.imageReference = images.front();
        item.mode = mode;
        settings.items.push_back(item);

        QVERIFY(!creator.createBitonalDocument(settings));
        QCOMPARE(creator.getConvertedItemCount(), size_t(0));
        QCOMPARE(creator.getFailedItemCount(), size_t(1));
    }
}

void ImageOptimizerTest::test_image_analysis_classification()
{
    QImage lineArt = createLineArtImage(64);
    QImage textScan = createTextScanImage(64);
    QImage photo = createPhotoImage(64);

    pdf::PDFImageOptimizer::ImageAnalysis lineAnalysis = pdf::PDFImageOptimizer::analyzeImage(lineArt);
    pdf::PDFImageOptimizer::ImageAnalysis textAnalysis = pdf::PDFImageOptimizer::analyzeImage(textScan);
    pdf::PDFImageOptimizer::ImageAnalysis photoAnalysis = pdf::PDFImageOptimizer::analyzeImage(photo);

    QCOMPARE(lineAnalysis.kind, pdf::PDFImageOptimizer::ImageAnalysis::Kind::LineArt);
    QCOMPARE(textAnalysis.kind, pdf::PDFImageOptimizer::ImageAnalysis::Kind::TextScan);
    QCOMPARE(photoAnalysis.kind, pdf::PDFImageOptimizer::ImageAnalysis::Kind::Photo);
}

void ImageOptimizerTest::test_optimizer_keeps_original_if_larger()
{
    QImage lineArt = createLineArtImage(256);
    pdf::PDFDocument document = createDocumentWithImage(lineArt, false, pdf::PDFImage::ImageCompression::RunLength);

    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(infos.size(), 1u);

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Preserve;
    settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality;
    settings.keepOriginalIfLarger = true;
    settings.preserveTransparency = true;
    settings.colorProfile.algorithm = pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG;
    settings.colorProfile.targetDpi = 0;
    settings.colorProfile.jpegQuality = 100;

    pdf::PDFImageOptimizer optimizer;
    pdf::PDFDocument optimized = optimizer.optimize(&document, settings);

    const pdf::PDFObject& imageObject = optimized.getObjectByReference(infos[0].reference);
    QVERIFY(imageObject.isStream());

    const pdf::PDFStream* stream = imageObject.getStream();
    const pdf::PDFDictionary* dictionary = stream->getDictionary();
    QVERIFY(dictionary);

    const pdf::PDFObject& filterObject = optimized.getObject(dictionary->get(pdf::PDF_STREAM_DICT_FILTER));
    QVERIFY(filterObject.isName());
    QCOMPARE(QString::fromLatin1(filterObject.getString()), QString("RunLengthDecode"));
}

void ImageOptimizerTest::test_optimizer_skips_disabled_override()
{
    QImage textScan = createTextScanImage(128);
    pdf::PDFDocument document = createDocumentWithImage(textScan, false, pdf::PDFImage::ImageCompression::Flate);

    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(infos.size(), 1u);

    const pdf::PDFObject& originalImageObject = document.getObjectByReference(infos[0].reference);
    QVERIFY(originalImageObject.isStream());
    const pdf::PDFStream* originalStream = originalImageObject.getStream();
    QVERIFY(originalStream);

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = true;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Auto;
    settings.keepOriginalIfLarger = false;

    pdf::PDFImageOptimizer::ImageOverrides overrides;
    pdf::PDFImageOptimizer::ImageOverride imageOverride;
    imageOverride.enabled = false;
    overrides.emplace(infos[0].reference, imageOverride);

    pdf::PDFImageOptimizer optimizer;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    pdf::PDFDocument optimized = optimizer.optimize(&document, settings, overrides, nullptr, nullptr, &results);

    QCOMPARE(results.size(), 1u);
    QVERIFY(results[0].keptOriginal);

    const pdf::PDFObject& optimizedImageObject = optimized.getObjectByReference(infos[0].reference);
    QVERIFY(optimizedImageObject.isStream());
    const pdf::PDFStream* optimizedStream = optimizedImageObject.getStream();
    QVERIFY(optimizedStream);

    QCOMPARE(*optimizedStream->getContent(), *originalStream->getContent());
}

void ImageOptimizerTest::test_optimizer_preserve_keeps_bitonal_encoding()
{
    QImage bitonal(64, 64, QImage::Format_Mono);
    bitonal.fill(1);

    for (int y = 8; y < 56; ++y)
    {
        for (int x = 8; x < 56; ++x)
        {
            if (((x / 8) + (y / 8)) % 2 == 0)
            {
                bitonal.setPixel(x, y, 0);
            }
        }
    }

    pdf::PDFDocument document = createDocumentWithImage(bitonal, false, pdf::PDFImage::ImageCompression::Flate);

    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(infos.size(), 1u);
    QCOMPARE(infos[0].bitsPerComponent, 1);

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Preserve;
    settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality;
    settings.keepOriginalIfLarger = false;
    settings.preserveTransparency = true;
    settings.colorProfile.algorithm = pdf::PDFImageOptimizer::CompressionAlgorithm::Flate;
    settings.colorProfile.targetDpi = 0;

    pdf::PDFImageOptimizer optimizer;
    pdf::PDFDocument optimized = optimizer.optimize(&document, settings);

    const pdf::PDFObject& imageObject = optimized.getObjectByReference(infos[0].reference);
    QVERIFY(imageObject.isStream());

    const pdf::PDFStream* stream = imageObject.getStream();
    const pdf::PDFDictionary* dictionary = stream->getDictionary();
    QVERIFY(dictionary);

    QCOMPARE(optimized.getObject(dictionary->get("BitsPerComponent")).getInteger(), pdf::PDFInteger(1));

    const pdf::PDFObject& colorSpaceObject = optimized.getObject(dictionary->get("ColorSpace"));
    QVERIFY(colorSpaceObject.isName());
    QCOMPARE(QString::fromLatin1(colorSpaceObject.getString()), QString("DeviceGray"));
}

void ImageOptimizerTest::test_optimizer_preserves_smask()
{
    QImage alphaImage = createAlphaImage(64);
    pdf::PDFDocument document = createDocumentWithImage(alphaImage, true, pdf::PDFImage::ImageCompression::Flate);

    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(infos.size(), 1u);

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Preserve;
    settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality;
    settings.keepOriginalIfLarger = false;
    settings.preserveTransparency = true;
    settings.colorProfile.algorithm = pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG;
    settings.colorProfile.targetDpi = 0;
    settings.colorProfile.jpegQuality = 70;

    pdf::PDFImageOptimizer optimizer;
    pdf::PDFDocument optimized = optimizer.optimize(&document, settings);

    const pdf::PDFObject& imageObject = optimized.getObjectByReference(infos[0].reference);
    QVERIFY(imageObject.isStream());

    const pdf::PDFStream* stream = imageObject.getStream();
    const pdf::PDFDictionary* dictionary = stream->getDictionary();
    QVERIFY(dictionary);
    QVERIFY(dictionary->hasKey("SMask"));

    const pdf::PDFObject& maskObject = optimized.getObject(dictionary->get("SMask"));
    QVERIFY(maskObject.isStream());
}

void ImageOptimizerTest::test_optimizer_reduces_size_for_photo()
{
    QImage photo = createPhotoImage(128);
    pdf::PDFDocument document = createDocumentWithImage(photo, false, pdf::PDFImage::ImageCompression::Flate);

    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(infos.size(), 1u);

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Preserve;
    settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize;
    settings.keepOriginalIfLarger = true;
    settings.preserveTransparency = true;
    settings.colorProfile.algorithm = pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG;
    settings.colorProfile.targetDpi = 0;
    settings.colorProfile.jpegQuality = 35;

    pdf::PDFImageOptimizer optimizer;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    pdf::PDFDocument optimized = optimizer.optimize(&document, settings, {}, nullptr, nullptr, &results);

    QCOMPARE(results.size(), 1u);
    QVERIFY(!results[0].keptOriginal);
    QVERIFY(results[0].newBytes > 0);
    QVERIFY(results[0].originalBytes > 0);
    QVERIFY(results[0].newBytes < results[0].originalBytes);

    const pdf::PDFObject& imageObject = optimized.getObjectByReference(infos[0].reference);
    QVERIFY(imageObject.isStream());
}

/// Returns true, if the pixel of the decoded image is black
static bool isBlackPixel(const QImage& image, int x, int y)
{
    return qGray(image.pixel(x, y)) < 128;
}

/// Reports the errors of the encoder into a list
class ErrorCollector : public pdf::PDFRenderErrorReporter
{
public:
    virtual void reportRenderError(pdf::RenderErrorType type, QString message) override
    {
        Q_UNUSED(type);
        messages << message;
    }

    virtual void reportRenderErrorOnce(pdf::RenderErrorType type, QString message) override
    {
        reportRenderError(type, message);
    }

    QStringList messages;
};

void ImageOptimizerTest::test_image_stream_bitonal_filters_roundtrip()
{
    // A monochrome image written by the fax and JBIG2 filters is decoded back exactly,
    // because both codings are lossless. The width is not a multiple of eight, so the
    // padding of the rows is exercised as well.
    QImage bitonal(93, 41, QImage::Format_Mono);
    bitonal.fill(1);

    for (int y = 0; y < bitonal.height(); ++y)
    {
        for (int x = 0; x < bitonal.width(); ++x)
        {
            if ((x * 7 + y * 3) % 11 < 4 || (x / 8 + y / 8) % 3 == 0)
            {
                bitonal.setPixel(x, y, 0);
            }
        }
    }

    for (const pdf::PDFImage::ImageCompression compression : { pdf::PDFImage::ImageCompression::CCITTGroup4, pdf::PDFImage::ImageCompression::JBIG2 })
    {
        const bool isCCITT = compression == pdf::PDFImage::ImageCompression::CCITTGroup4;

        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = compression;
        options.colorMode = pdf::PDFImage::ImageColorMode::Preserve;

        ErrorCollector errorCollector;
        pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(bitonal, options, &errorCollector);
        QCOMPARE(errorCollector.messages, QStringList());

        const pdf::PDFDictionary* dictionary = stream.getDictionary();
        QVERIFY(dictionary);
        QVERIFY(dictionary->get("Filter").isName());
        QCOMPARE(QString::fromLatin1(dictionary->get("Filter").getString()), QString(isCCITT ? "CCITTFaxDecode" : "JBIG2Decode"));
        QCOMPARE(dictionary->get("BitsPerComponent").getInteger(), pdf::PDFInteger(1));
        QCOMPARE(QString::fromLatin1(dictionary->get("ColorSpace").getString()), QString("DeviceGray"));
        QCOMPARE(dictionary->get("Width").getInteger(), pdf::PDFInteger(93));
        QCOMPARE(dictionary->get("Height").getInteger(), pdf::PDFInteger(41));
        QCOMPARE(dictionary->get("Length").getInteger(), pdf::PDFInteger(stream.getContent()->size()));

        if (isCCITT)
        {
            // The parameters of the fax filter - pure two dimensional coding and the size
            const pdf::PDFDictionary* decodeParms = dictionary->get("DecodeParms").getDictionary();
            QVERIFY(decodeParms);
            QCOMPARE(decodeParms->get("K").getInteger(), pdf::PDFInteger(-1));
            QCOMPARE(decodeParms->get("Columns").getInteger(), pdf::PDFInteger(93));
            QCOMPARE(decodeParms->get("Rows").getInteger(), pdf::PDFInteger(41));
            QVERIFY(!decodeParms->hasKey("BlackIs1"));
        }
        else
        {
            // No global segments are used by the JBIG2 stream
            QVERIFY(!dictionary->hasKey("DecodeParms"));
        }

        pdf::PDFDocument document = createDocumentWithImage(bitonal, false, compression);
        std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
        QCOMPARE(infos.size(), 1u);
        QCOMPARE(infos[0].bitsPerComponent, 1);
        QCOMPARE(infos[0].image.size(), bitonal.size());

        for (int y = 0; y < bitonal.height(); ++y)
        {
            for (int x = 0; x < bitonal.width(); ++x)
            {
                const bool isSourceBlack = bitonal.pixelIndex(x, y) == 0;
                if (isBlackPixel(infos[0].image, x, y) != isSourceBlack)
                {
                    QFAIL(qPrintable(QString("Pixel [%1, %2] of the %3 image differs.").arg(x).arg(y).arg(isCCITT ? "CCITT" : "JBIG2")));
                }
            }
        }
    }
}

void ImageOptimizerTest::test_image_stream_bitonal_filters_convert_color_images()
{
    // The fax and JBIG2 filters encode only bitonal images, so a color image is
    // converted to monochrome and the conversion is reported
    const QImage image = createTextScanImage(64);

    for (const pdf::PDFImage::ImageCompression compression : { pdf::PDFImage::ImageCompression::CCITTGroup4, pdf::PDFImage::ImageCompression::JBIG2 })
    {
        for (const pdf::PDFImage::ImageColorMode colorMode : { pdf::PDFImage::ImageColorMode::Preserve, pdf::PDFImage::ImageColorMode::Color, pdf::PDFImage::ImageColorMode::Grayscale })
        {
            pdf::PDFImage::ImageEncodeOptions options;
            options.compression = compression;
            options.colorMode = colorMode;

            ErrorCollector errorCollector;
            pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(image, options, &errorCollector);
            QCOMPARE(errorCollector.messages.size(), 1);
            QVERIFY(errorCollector.messages.front().contains("monochrome"));

            const pdf::PDFDictionary* dictionary = stream.getDictionary();
            QVERIFY(dictionary);
            QCOMPARE(dictionary->get("BitsPerComponent").getInteger(), pdf::PDFInteger(1));
            QCOMPARE(QString::fromLatin1(dictionary->get("ColorSpace").getString()), QString("DeviceGray"));
        }
    }
}

void ImageOptimizerTest::test_optimizer_bitonal_algorithms()
{
    const QImage textScan = createTextScanImage(128);
    pdf::PDFDocument document = createDocumentWithImage(textScan, false, pdf::PDFImage::ImageCompression::Flate);

    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
    QCOMPARE(infos.size(), 1u);

    for (const pdf::PDFImageOptimizer::CompressionAlgorithm algorithm : { pdf::PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4, pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2 })
    {
        const bool isCCITT = algorithm == pdf::PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4;

        pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
        settings.enabled = true;
        settings.autoMode = false;
        settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Bitonal;
        settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality;
        settings.keepOriginalIfLarger = false;
        settings.preserveTransparency = true;
        settings.bitonalProfile.algorithm = algorithm;
        settings.bitonalProfile.targetDpi = 0;

        // The algorithms are supported, so the plan keeps them
        const pdf::PDFImageOptimizer::ResolvedPlan plan = pdf::PDFImageOptimizer::resolvePlan(infos[0], settings);
        QCOMPARE(plan.algorithm, algorithm);
        QCOMPARE(plan.resolvedColorMode, pdf::PDFImageOptimizer::ColorMode::Bitonal);
        QVERIFY(!plan.hadUnsupportedCompression);
        QCOMPARE(plan.encodeOptions.compression, isCCITT ? pdf::PDFImage::ImageCompression::CCITTGroup4 : pdf::PDFImage::ImageCompression::JBIG2);
        QVERIFY(pdf::PDFImageOptimizer::estimateEncodedBytes(infos[0], plan) > 0);

        pdf::PDFImageOptimizer optimizer;
        std::vector<pdf::PDFImageOptimizer::ImageResult> results;
        pdf::PDFDocument optimized = optimizer.optimize(&document, settings, {}, nullptr, nullptr, &results);

        QCOMPARE(results.size(), 1u);
        QVERIFY(!results[0].keptOriginal);
        QVERIFY(results[0].newBytes > 0);

        const pdf::PDFObject& imageObject = optimized.getObjectByReference(infos[0].reference);
        QVERIFY(imageObject.isStream());

        const pdf::PDFDictionary* dictionary = imageObject.getStream()->getDictionary();
        QVERIFY(dictionary);
        QCOMPARE(QString::fromLatin1(optimized.getObject(dictionary->get("Filter")).getString()), QString(isCCITT ? "CCITTFaxDecode" : "JBIG2Decode"));
        QCOMPARE(optimized.getObject(dictionary->get("BitsPerComponent")).getInteger(), pdf::PDFInteger(1));

        // The optimized image decodes to the bitonal version of the source - the
        // pure white and pure black pixels of the source keep their colours
        std::vector<pdf::PDFImageOptimizer::ImageInfo> optimizedInfos = pdf::PDFImageOptimizer::collectImageInfos(&optimized);
        QCOMPARE(optimizedInfos.size(), 1u);
        QCOMPARE(optimizedInfos[0].image.size(), textScan.size());

        for (int y = 0; y < textScan.height(); ++y)
        {
            for (int x = 0; x < textScan.width(); ++x)
            {
                const QRgb pixel = textScan.pixel(x, y);
                if (pixel == qRgb(255, 255, 255) || pixel == qRgb(0, 0, 0))
                {
                    QCOMPARE(isBlackPixel(optimizedInfos[0].image, x, y), pixel == qRgb(0, 0, 0));
                }
            }
        }
    }

    // The automatic mode selects JBIG2 for the bitonal images
    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = true;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Bitonal;
    settings.bitonalProfile.algorithm = pdf::PDFImageOptimizer::CompressionAlgorithm::Auto;

    pdf::PDFImageOptimizer::ResolvedPlan autoPlan = pdf::PDFImageOptimizer::resolvePlan(infos[0], settings);
    QCOMPARE(autoPlan.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2);
    QVERIFY(!autoPlan.hadUnsupportedCompression);

    settings.autoMode = false;
    autoPlan = pdf::PDFImageOptimizer::resolvePlan(infos[0], settings);
    QCOMPARE(autoPlan.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2);
}

void ImageOptimizerTest::test_bitonal_creator_uses_jbig2()
{
    constexpr int size = 48;
    const QImage source = createLineArtImage(size);
    pdf::PDFDocument document = createDocumentWithImage(source, false, pdf::PDFImage::ImageCompression::Flate);

    pdf::PDFBitonalDocumentCreator creator(&document, nullptr, nullptr);
    const std::vector<pdf::PDFObjectReference> images = creator.getConvertibleImages();
    QCOMPARE(images.size(), size_t(1));

    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;
    settings.conversionMethod = pdf::PDFImageConversion::ConversionMethod::Automatic;

    pdf::PDFBitonalDocumentCreator::ItemInfo item;
    item.imageReference = images.front();
    settings.items.push_back(item);

    QVERIFY(creator.createBitonalDocument(settings));
    const pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();

    // The converted image is coded by JBIG2
    const pdf::PDFDictionary* imageDictionary = getFirstPageImageDictionary(bitonalDocument);
    QVERIFY(imageDictionary);
    QCOMPARE(QString::fromLatin1(bitonalDocument.getObject(imageDictionary->get("Filter")).getString()), QString("JBIG2Decode"));

    // The image decodes to the bitonal conversion of the source
    pdf::PDFBitonalDocumentCreator decodingCreator(&bitonalDocument, nullptr, nullptr);
    const std::vector<pdf::PDFObjectReference> convertedImages = decodingCreator.getConvertibleImages();
    QCOMPARE(convertedImages.size(), size_t(1));

    const QImage decoded = decodingCreator.getDecodedImage(convertedImages.front(), nullptr);
    const QImage expected = pdf::PDFBitonalDocumentCreator::convertImageToBitonal(source, pdf::PDFImageConversion::ConversionMethod::Automatic, 128, nullptr, nullptr);
    QVERIFY(!decoded.isNull());
    QVERIFY(!expected.isNull());
    QCOMPARE(decoded.size(), expected.size());

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            if (isBlackPixel(decoded, x, y) != isBlackPixel(expected, x, y))
            {
                QFAIL(qPrintable(QString("Pixel [%1, %2] differs.").arg(x).arg(y)));
            }
        }
    }
}

void ImageOptimizerTest::test_image_stream_jbig2_falls_back_for_large_images()
{
    // The JBIG2 decoder accepts a bitmap of at most 65536 pixels in either dimension.
    // A larger image requested to be coded by JBIG2 is coded by the fax coding instead,
    // so the stream can be read back - a JBIG2 stream, which the decoder refuses, would
    // be worthless. The largest accepted size stays coded by JBIG2.
    for (const auto& [width, height, isJBIG2] : { std::tuple<int, int, bool>(65536, 1, true),
                                                   std::tuple<int, int, bool>(65537, 1, false),
                                                   std::tuple<int, int, bool>(1, 65537, false) })
    {
        QImage bitonal(width, height, QImage::Format_Mono);
        bitonal.fill(1);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if ((x * 7 + y * 3) % 11 < 4)
                {
                    bitonal.setPixel(x, y, 0);
                }
            }
        }

        pdf::PDFImage::ImageEncodeOptions options;
        options.compression = pdf::PDFImage::ImageCompression::JBIG2;
        options.colorMode = pdf::PDFImage::ImageColorMode::Preserve;

        ErrorCollector errorCollector;
        pdf::PDFStream stream = pdf::PDFImage::createStreamFromImage(bitonal, options, &errorCollector);
        QCOMPARE(errorCollector.messages.size(), isJBIG2 ? 0 : 1);

        const pdf::PDFDictionary* dictionary = stream.getDictionary();
        QVERIFY(dictionary);
        QCOMPARE(QString::fromLatin1(dictionary->get("Filter").getString()), QString(isJBIG2 ? "JBIG2Decode" : "CCITTFaxDecode"));
        QCOMPARE(dictionary->get("Width").getInteger(), pdf::PDFInteger(width));
        QCOMPARE(dictionary->get("Height").getInteger(), pdf::PDFInteger(height));
        QCOMPARE(dictionary->hasKey("DecodeParms"), !isJBIG2);

        // The image is read back exactly
        pdf::PDFDocument document = createDocumentWithImage(bitonal, false, pdf::PDFImage::ImageCompression::JBIG2);
        std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(&document);
        QCOMPARE(infos.size(), 1u);
        QCOMPARE(infos[0].image.size(), bitonal.size());

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (isBlackPixel(infos[0].image, x, y) != (bitonal.pixelIndex(x, y) == 0))
                {
                    QFAIL(qPrintable(QString("Pixel [%1, %2] of the image %3 x %4 differs.").arg(x).arg(y).arg(width).arg(height)));
                }
            }
        }

        // The optimizer selects JBIG2 for the bitonal images, so a long narrow image
        // going through it must be readable as well
        pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
        settings.enabled = true;
        settings.autoMode = false;
        settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Bitonal;
        settings.goal = pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality;
        settings.keepOriginalIfLarger = false;
        settings.bitonalProfile.algorithm = pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2;
        settings.bitonalProfile.targetDpi = 0;

        pdf::PDFImageOptimizer optimizer;
        std::vector<pdf::PDFImageOptimizer::ImageResult> results;
        pdf::PDFDocument optimized = optimizer.optimize(&document, settings, {}, nullptr, nullptr, &results);
        QCOMPARE(results.size(), 1u);

        const pdf::PDFObject& imageObject = optimized.getObjectByReference(infos[0].reference);
        QVERIFY(imageObject.isStream());
        QCOMPARE(QString::fromLatin1(optimized.getObject(imageObject.getStream()->getDictionary()->get("Filter")).getString()), QString(isJBIG2 ? "JBIG2Decode" : "CCITTFaxDecode"));

        std::vector<pdf::PDFImageOptimizer::ImageInfo> optimizedInfos = pdf::PDFImageOptimizer::collectImageInfos(&optimized);
        QCOMPARE(optimizedInfos.size(), 1u);
        QCOMPARE(optimizedInfos[0].image.size(), bitonal.size());
    }
}

QTEST_APPLESS_MAIN(ImageOptimizerTest)

#include "tst_imageoptimizertest.moc"
