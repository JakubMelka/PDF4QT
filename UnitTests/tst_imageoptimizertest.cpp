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

#include "pdfconstants.h"
#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfimage.h"
#include "pdfimageconversion.h"
#include "pdfimageoptimizer.h"
#include "pdfbitonaldocumentcreator.h"
#include "pdfpage.h"
#include "pdfconstants.h"

#include <QtTest>
#include <QColor>
#include <QImage>

#include <array>
#include <cstdlib>
#include <map>
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
    void test_image_analysis_tones_data();
    void test_image_analysis_tones();
    void test_image_analysis_all_pixels_data();
    void test_image_analysis_all_pixels();
    void test_image_analysis_tints_and_alpha();
    void test_plan_preserve_of_gray_tones_in_one_bit();
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
    void test_dct_decodes_flat_blocks();
    void test_dct_wrapping_size_is_refused();
    void test_dct_size_above_pixel_limit_is_refused();
    void test_dct_truncated_entropy_segment();
    void test_dct_header_size_differs_from_dictionary();
    void test_dct_error_after_decoding();
    void test_raw_soft_mask_valid();
    void test_raw_soft_mask_of_one_byte();
    void test_raw_soft_mask_one_row_short();
    void test_raw_soft_mask_with_decode_short();
    void test_raw_soft_mask_of_one_bit_short();
    void test_raw_image_short();
    void test_raw_image_mask_short();
    void test_raw_explicit_mask_short();
    void test_raw_alpha_mask_of_short_data_is_refused();

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

    pdf::PDFDictionaryBuilder dictionary(*imageObject.getStream()->getDictionary());
    QByteArray garbage("this is not a compressed image stream at all");
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(garbage.size()));

    pdf::PDFDocumentBuilder builder(&document);
    builder.setObject(imageReference, pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(garbage))));
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
            pdf::PDFObject::createStream(pdf::PDFStream(maskStream)));

        pdf::PDFDictionaryBuilder dict(*imageStream.getDictionary());
        dict.setEntry(pdf::PDFInplaceOrMemoryString("SMask"), pdf::PDFObject::createReference(maskRef));
        const QByteArray* content = imageStream.getContent();
        QByteArray contentDereferenced = content ? *content : QByteArray();
        imageStream = pdf::PDFStream(std::move(dict), std::move(contentDereferenced));
    }

    pdf::PDFObjectReference imageRef = builder.addObject(
        pdf::PDFObject::createStream(pdf::PDFStream(imageStream)));

    QByteArray content("q 200 0 0 200 0 0 cm /Im1 Do Q");
    pdf::PDFDictionaryBuilder contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                         pdf::PDFObject::createInteger(content.size()));
    pdf::PDFStream contentStream(std::move(contentDict), std::move(content));
    pdf::PDFObjectReference contentRef = builder.addObject(
        pdf::PDFObject::createStream(pdf::PDFStream(contentStream)));

    pdf::PDFDictionaryBuilder xObject;
    xObject.addEntry(pdf::PDFInplaceOrMemoryString("Im1"), pdf::PDFObject::createReference(imageRef));

    pdf::PDFDictionaryBuilder resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"),
                       pdf::PDFObject::createDictionary(std::move(xObject)));

    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                        pdf::PDFObject::createDictionary(std::move(resources)));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentRef));

    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::move(pageUpdate)));

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
    /// Operation control, which reports the operation as canceled from the beginning
    class CanceledOperationControl : public pdf::PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return true; }
    };

    CanceledOperationControl operationControl;

    pdf::PDFImageConversion conversion;
    conversion.setConversionMethod(pdf::PDFImageConversion::ConversionMethod::Automatic);
    conversion.setOperationControl(&operationControl);
    conversion.setImage(createLineArtImage(16));

    // A canceled conversion produces nothing, so its caller cannot use a partial result
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

void ImageOptimizerTest::test_image_analysis_tones_data()
{
    QTest::addColumn<int>("firstTone");
    QTest::addColumn<int>("secondTone");
    QTest::addColumn<bool>("isTextScan");

    // Only tones of the first and of the last of the 32 quantized steps are black and white
    QTest::newRow("black-and-white") << 0 << 255 << true;
    QTest::newRow("last-black-last-white") << 7 << 248 << true;
    QTest::newRow("solid-black") << 0 << 0 << true;
    QTest::newRow("solid-white") << 255 << 255 << true;
    QTest::newRow("first-tone-above-black") << 8 << 255 << false;
    QTest::newRow("first-tone-below-white") << 0 << 247 << false;
    QTest::newRow("solid-light-gray") << 200 << 200 << false;
    QTest::newRow("panel-on-white") << 230 << 255 << false;
}

void ImageOptimizerTest::test_image_analysis_tones()
{
    QFETCH(int, firstTone);
    QFETCH(int, secondTone);
    QFETCH(bool, isTextScan);

    QImage image(64, 64, QImage::Format_Grayscale8);
    image.fill(qRgb(firstTone, firstTone, firstTone));
    for (int x = 0; x < image.width(); ++x)
    {
        image.setPixel(x, 32, qRgb(secondTone, secondTone, secondTone));
    }

    const pdf::PDFImageOptimizer::ImageAnalysis analysis = pdf::PDFImageOptimizer::analyzeImage(image);
    QVERIFY(analysis.grayscale);
    QCOMPARE(analysis.kind == pdf::PDFImageOptimizer::ImageAnalysis::Kind::TextScan, isTextScan);
}

void ImageOptimizerTest::test_image_analysis_all_pixels_data()
{
    QTest::addColumn<int>("rowOffset");

    // Four neighboring rows: a sample of every fourth row misses at least three of them
    for (int rowOffset = 0; rowOffset < 4; ++rowOffset)
    {
        QTest::addRow("row-%d", rowOffset) << rowOffset;
    }
}

void ImageOptimizerTest::test_image_analysis_all_pixels()
{
    QFETCH(int, rowOffset);

    // One row of a large black and white image decides, wherever it lies
    QImage image(1024, 1024, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int x = 0; x < image.width(); ++x)
    {
        image.setPixel(x, 40, qRgb(0, 0, 0));
    }
    QCOMPARE(pdf::PDFImageOptimizer::analyzeImage(image).kind, pdf::PDFImageOptimizer::ImageAnalysis::Kind::TextScan);

    QImage grayRow = image;
    QImage redRow = image;
    for (int x = 0; x < image.width(); ++x)
    {
        grayRow.setPixel(x, 500 + rowOffset, qRgb(128, 128, 128));
        redRow.setPixel(x, 500 + rowOffset, qRgb(255, 0, 0));
    }

    const pdf::PDFImageOptimizer::ImageAnalysis grayAnalysis = pdf::PDFImageOptimizer::analyzeImage(grayRow);
    QVERIFY(grayAnalysis.grayscale);
    QVERIFY(grayAnalysis.kind != pdf::PDFImageOptimizer::ImageAnalysis::Kind::TextScan);

    const pdf::PDFImageOptimizer::ImageAnalysis redAnalysis = pdf::PDFImageOptimizer::analyzeImage(redRow);
    QVERIFY(!redAnalysis.grayscale);
    QVERIFY(redAnalysis.kind != pdf::PDFImageOptimizer::ImageAnalysis::Kind::TextScan);
}

void ImageOptimizerTest::test_image_analysis_tints_and_alpha()
{
    // A cream tint: red and blue differ, each of them only a little from the green
    QImage cream(64, 64, QImage::Format_RGB32);
    cream.fill(qRgb(255, 252, 249));
    QVERIFY(!pdf::PDFImageOptimizer::analyzeImage(cream).grayscale);

    // A half transparent black is no black: flattened, it gives other tones
    QImage shadow(64, 64, QImage::Format_ARGB32);
    shadow.fill(qRgba(0, 0, 0, 255));
    for (int x = 0; x < shadow.width(); ++x)
    {
        shadow.setPixel(x, 32, qRgba(0, 0, 0, 128));
    }
    const pdf::PDFImageOptimizer::ImageAnalysis shadowAnalysis = pdf::PDFImageOptimizer::analyzeImage(shadow);
    QVERIFY(shadowAnalysis.grayscale);
    QVERIFY(shadowAnalysis.kind != pdf::PDFImageOptimizer::ImageAnalysis::Kind::TextScan);
}

void ImageOptimizerTest::test_plan_preserve_of_gray_tones_in_one_bit()
{
    // One bit of the file with /Decode [0.8 1] or a gray /Indexed: a light gray panel on white.
    // Preserve must not write it black and white, a real black and white image still is.
    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Preserve;

    pdf::PDFImageOptimizer::ImageInfo info;
    info.bitsPerComponent = 1;
    info.colorSpaceName = QStringLiteral("DeviceGray");
    info.image = QImage(64, 64, QImage::Format_Grayscale8);
    info.image.fill(qRgb(205, 205, 205));
    QCOMPARE(pdf::PDFImageOptimizer::resolvePlan(info, settings).resolvedColorMode, pdf::PDFImageOptimizer::ColorMode::Grayscale);

    info.colorSpaceName = QString();
    QCOMPARE(pdf::PDFImageOptimizer::resolvePlan(info, settings).resolvedColorMode, pdf::PDFImageOptimizer::ColorMode::Grayscale);

    info.image.fill(qRgb(0, 0, 0));
    QCOMPARE(pdf::PDFImageOptimizer::resolvePlan(info, settings).resolvedColorMode, pdf::PDFImageOptimizer::ColorMode::Bitonal);
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
        // pure white and pure black pixels of the source keep their colors
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

    // The default compression selects the smallest result of all the algorithms, which
    // is not the JBIG2 coding for every image, so this test asks for it explicitly - it
    // verifies, that an image, which the creator has coded by JBIG2, can be read back.
    settings.compression = pdf::PDFBitonalDocumentCreator::Compression::JBIG2;

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

/// Hand-written baseline JPEG data (ISO/IEC 10918-1, Annex B) for the tests of the DCT
/// decoding of PDFImage::createImage. Every 8 x 8 block is flat: only its DC coefficient
/// is coded and the first AC symbol is the end of block. The quantization value of every
/// coefficient is 8, so the inverse DCT of a block with the DC coefficient d gives
/// d * 8 / 8 = d in all its 64 samples, and the level shift adds 128 (A.3.1, A.3.3): the
/// sample s is coded as the DC coefficient s - 128 and the expected samples are set by hand.
/// For such a block the integer inverse DCT of libjpeg is exact (the DC term alone, no
/// rounding of a fraction); ISO/IEC 10918-2 would allow 1 level, the tests need none.
class DCTImageTestHelper
{
public:
    /// Data of a JPEG of \p width x \p height samples and the components \p componentIds
    /// (their identifiers, one or three; no subsampling, so an MCU is one block of each
    /// component). \p blockSamples are the samples of the coded blocks in the order of the
    /// MCUs, the components of an MCU one after another; the blocks behind them are not
    /// coded. The entropy-coded segment is cut after \p entropyBytes bytes, when it is not
    /// negative; \p trailer follows it, then EOI, when \p withEndMarker.
    static QByteArray createJpeg(int width,
                                 int height,
                                 const QByteArray& componentIds,
                                 const std::vector<int>& blockSamples,
                                 int entropyBytes,
                                 bool withEndMarker,
                                 const QByteArray& trailer);

    /// Decodes \p jpeg as the data of an image XObject (/Filter /DCTDecode) with the given
    /// /Width and /Height of its dictionary and the color space DeviceGray or DeviceRGB
    static pdf::PDFImage decode(const QByteArray& jpeg, int dictionaryWidth, int dictionaryHeight, bool isRGB, ErrorCollector* reporter);

    /// Message of the exception, by which decode() refuses \p jpeg, or an empty string, when it decodes it
    static QString getDecodeError(const QByteArray& jpeg, int dictionaryWidth, int dictionaryHeight, bool isRGB);

    /// Sample \p component of the pixel \p x, \p y of the decoded data of \p image
    static int getSample(const pdf::PDFImage& image, int x, int y, int component);

private:
    struct BitWriter
    {
        QByteArray bytes;
        int buffer = 0;
        int bitCount = 0;
    };

    struct Code
    {
        quint32 word = 0;
        int length = 0;
    };

    /// Code words of a table by Annex C (C.1 - C.3): \p bits[i] codes of the length i + 1 for the symbols \p values in their order
    static std::map<int, Code> createCodes(const std::array<int, 16>& bits, const std::vector<int>& values);

    /// Appends \p length bits of \p value, the most significant first; a byte 0xFF is followed by a stuffed 0x00 (F.1.2.3)
    static void writeBits(BitWriter& writer, quint32 value, int length);

    /// Pads the last byte with 1 bits (F.1.2.3)
    static void flushBits(BitWriter& writer);

    static void appendWord(QByteArray& data, int value);

    /// The luminance DC table of Annex K (Table K.3): the categories 0 - 11
    static std::array<int, 16> getDCBits() { return { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 }; }
    static std::vector<int> getDCValues() { return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }; }

    /// An AC table of one symbol, the end of block (0x00), coded by the single bit 0
    static std::array<int, 16> getACBits() { return { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; }
    static std::vector<int> getACValues() { return { 0x00 }; }
};

std::map<int, DCTImageTestHelper::Code> DCTImageTestHelper::createCodes(const std::array<int, 16>& bits, const std::vector<int>& values)
{
    std::map<int, Code> codes;
    quint32 word = 0;
    size_t valueIndex = 0;
    for (int length = 1; length <= 16; ++length)
    {
        for (int i = 0; i < bits[length - 1]; ++i)
        {
            codes[values[valueIndex++]] = Code{ word++, length };
        }
        word <<= 1;
    }
    return codes;
}

void DCTImageTestHelper::writeBits(BitWriter& writer, quint32 value, int length)
{
    for (int i = length - 1; i >= 0; --i)
    {
        writer.buffer = (writer.buffer << 1) | int((value >> i) & 1);
        if (++writer.bitCount == 8)
        {
            writer.bytes.append(char(writer.buffer));
            if (writer.buffer == 0xFF)
            {
                writer.bytes.append(char(0x00));
            }
            writer.buffer = 0;
            writer.bitCount = 0;
        }
    }
}

void DCTImageTestHelper::flushBits(BitWriter& writer)
{
    while (writer.bitCount != 0)
    {
        writeBits(writer, 1, 1);
    }
}

void DCTImageTestHelper::appendWord(QByteArray& data, int value)
{
    data.append(char((value >> 8) & 0xFF));
    data.append(char(value & 0xFF));
}

QByteArray DCTImageTestHelper::createJpeg(int width,
                                          int height,
                                          const QByteArray& componentIds,
                                          const std::vector<int>& blockSamples,
                                          int entropyBytes,
                                          bool withEndMarker,
                                          const QByteArray& trailer)
{
    const int componentCount = int(componentIds.size());
    QByteArray data("\xFF\xD8", 2);

    // DQT (B.2.4.1): table 0 of 8-bit values, all 8
    data.append("\xFF\xDB", 2);
    appendWord(data, 67);
    data.append(char(0x00));
    data.append(QByteArray(64, char(8)));

    // DHT (B.2.4.2): DC table 0 and AC table 0
    const std::array<int, 16> dcBits = getDCBits();
    const std::vector<int> dcValues = getDCValues();
    const std::array<int, 16> acBits = getACBits();
    const std::vector<int> acValues = getACValues();
    data.append("\xFF\xC4", 2);
    appendWord(data, int(2 + 17 + dcValues.size() + 17 + acValues.size()));
    data.append(char(0x00));
    for (int count : dcBits)
    {
        data.append(char(count));
    }
    for (int value : dcValues)
    {
        data.append(char(value));
    }
    data.append(char(0x10));
    for (int count : acBits)
    {
        data.append(char(count));
    }
    for (int value : acValues)
    {
        data.append(char(value));
    }

    // SOF0 (B.2.2): 8-bit samples, the height, the width, the components (H = V = 1, table 0)
    data.append("\xFF\xC0", 2);
    appendWord(data, 8 + 3 * componentCount);
    data.append(char(8));
    appendWord(data, height);
    appendWord(data, width);
    data.append(char(componentCount));
    for (char componentId : componentIds)
    {
        data.append(componentId);
        data.append(char(0x11));
        data.append(char(0x00));
    }

    // SOS (B.2.3): all components, tables 0 / 0, Ss = 0, Se = 63, Ah = Al = 0
    data.append("\xFF\xDA", 2);
    appendWord(data, 6 + 2 * componentCount);
    data.append(char(componentCount));
    for (char componentId : componentIds)
    {
        data.append(componentId);
        data.append(char(0x00));
    }
    data.append(char(0x00));
    data.append(char(0x3F));
    data.append(char(0x00));

    // The entropy-coded segment (F.1.2.1): the difference of the DC coefficient to the
    // previous block of the component as its category and its additional bits, then EOB
    const std::map<int, Code> dcCodes = createCodes(dcBits, dcValues);
    const std::map<int, Code> acCodes = createCodes(acBits, acValues);
    std::vector<int> predictions(componentCount, 0);
    BitWriter writer;
    for (size_t i = 0; i < blockSamples.size(); ++i)
    {
        const int component = int(i % componentCount);
        const int coefficient = blockSamples[i] - 128;
        const int difference = coefficient - predictions[component];
        predictions[component] = coefficient;

        int category = 0;
        while ((std::abs(difference) >> category) != 0)
        {
            ++category;
        }

        const Code& dcCode = dcCodes.at(category);
        writeBits(writer, dcCode.word, dcCode.length);
        if (category > 0)
        {
            const int additionalBits = difference >= 0 ? difference : difference + (1 << category) - 1;
            writeBits(writer, quint32(additionalBits), category);
        }

        const Code& endOfBlock = acCodes.at(0x00);
        writeBits(writer, endOfBlock.word, endOfBlock.length);
    }
    flushBits(writer);

    data.append(entropyBytes >= 0 ? writer.bytes.left(entropyBytes) : writer.bytes);
    data.append(trailer);
    if (withEndMarker)
    {
        data.append("\xFF\xD9", 2);
    }
    return data;
}

pdf::PDFImage DCTImageTestHelper::decode(const QByteArray& jpeg, int dictionaryWidth, int dictionaryHeight, bool isRGB, ErrorCollector* reporter)
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));

    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(dictionaryWidth));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(dictionaryHeight));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName(isRGB ? "DeviceRGB" : "DeviceGray"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("DCTDecode"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(jpeg.size()));
    const pdf::PDFObjectReference reference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(jpeg))));
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFObject& object = document.getObjectByReference(reference);
    const pdf::PDFColorSpacePointer colorSpace = pdf::PDFAbstractColorSpace::createDeviceColorSpaceByName(nullptr, &document, nullptr, isRGB ? QByteArrayLiteral("DeviceRGB") : QByteArrayLiteral("DeviceGray"));
    return pdf::PDFImage::createImage(&document, object.getStream(), colorSpace, false, pdf::RenderingIntent::Perceptual, reporter);
}

QString DCTImageTestHelper::getDecodeError(const QByteArray& jpeg, int dictionaryWidth, int dictionaryHeight, bool isRGB)
{
    ErrorCollector reporter;
    try
    {
        decode(jpeg, dictionaryWidth, dictionaryHeight, isRGB, &reporter);
    }
    catch (const pdf::PDFException& exception)
    {
        return exception.getMessage();
    }
    return QString();
}

int DCTImageTestHelper::getSample(const pdf::PDFImage& image, int x, int y, int component)
{
    const pdf::PDFImageData& data = image.getImageData();
    const qsizetype offset = qsizetype(y) * qsizetype(data.getStride()) + qsizetype(x) * qsizetype(data.getComponents()) + component;
    return int(static_cast<unsigned char>(data.getData().at(offset)));
}

void ImageOptimizerTest::test_dct_decodes_flat_blocks()
{
    // Gray, 16 x 16: four blocks (the MCUs go row by row) of the samples 200, 56 / 128, 200;
    // the differences 72, -144, 72, 72 use the categories 7 and 8 of Table K.3
    {
        const QByteArray jpeg = DCTImageTestHelper::createJpeg(16, 16, QByteArray(1, char(1)), { 200, 56, 128, 200 }, -1, true, QByteArray());
        ErrorCollector reporter;
        const pdf::PDFImage image = DCTImageTestHelper::decode(jpeg, 16, 16, false, &reporter);
        const pdf::PDFImageData& data = image.getImageData();
        QCOMPARE(data.getWidth(), 16u);
        QCOMPARE(data.getHeight(), 16u);
        QCOMPARE(data.getComponents(), 1u);
        QCOMPARE(data.getBitsPerComponent(), 8u);
        QCOMPARE(data.getStride(), 16u);
        QCOMPARE(data.getData().size(), qsizetype(16 * 16));
        QVERIFY2(reporter.messages.isEmpty(), qPrintable(reporter.messages.join('\n')));

        for (int y = 0; y < 16; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const int expected = (y < 8) ? (x < 8 ? 200 : 56) : (x < 8 ? 128 : 200);
                QCOMPARE(DCTImageTestHelper::getSample(image, x, y, 0), expected);
            }
        }
    }

    // RGB by the identifiers 'R', 'G', 'B' (no JFIF / Adobe marker: no color transform),
    // 16 x 8: two MCUs of (200, 128, 56) and (56, 200, 128)
    {
        const QByteArray jpeg = DCTImageTestHelper::createJpeg(16, 8, QByteArray("RGB"), { 200, 128, 56, 56, 200, 128 }, -1, true, QByteArray());
        ErrorCollector reporter;
        const pdf::PDFImage image = DCTImageTestHelper::decode(jpeg, 16, 8, true, &reporter);
        const pdf::PDFImageData& data = image.getImageData();
        QCOMPARE(data.getWidth(), 16u);
        QCOMPARE(data.getHeight(), 8u);
        QCOMPARE(data.getComponents(), 3u);
        QCOMPARE(data.getStride(), 48u);
        QCOMPARE(data.getData().size(), qsizetype(48 * 8));

        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                const bool left = x < 8;
                QCOMPARE(DCTImageTestHelper::getSample(image, x, y, 0), left ? 200 : 56);
                QCOMPARE(DCTImageTestHelper::getSample(image, x, y, 1), left ? 128 : 200);
                QCOMPARE(DCTImageTestHelper::getSample(image, x, y, 2), left ? 56 : 128);
            }
        }
    }
}

void ImageOptimizerTest::test_dct_wrapping_size_is_refused()
{
    // 46341 x 30894 x 3 = 2^32 + 9266 bytes: in 32 bits the buffer would have 9266 bytes,
    // while a row alone has 139023. The pixels (1 431 658 854) are below the limit of 2^31,
    // the bytes are above what the image data address (32 bits): refused before decoding.
    const QByteArray jpeg = DCTImageTestHelper::createJpeg(46341, 30894, QByteArray("RGB"), { 200, 128, 56 }, -1, false, QByteArray());
    const QString error = DCTImageTestHelper::getDecodeError(jpeg, 46341, 30894, true);
    QVERIFY2(error.contains(QStringLiteral("46341")) && error.contains(QStringLiteral("30894")), qPrintable(error));
}

void ImageOptimizerTest::test_dct_size_above_pixel_limit_is_refused()
{
    // 65500 x 32787 = 2 147 548 500 pixels, above 2^31 = 2 147 483 648 (one component, so the
    // bytes alone, 2^31 + 64 852, would still be addressable): refused before decoding. 2^31
    // itself is no product of two sides of at most 65500 (JPEG_MAX_DIMENSION of libjpeg).
    const QByteArray jpeg = DCTImageTestHelper::createJpeg(65500, 32787, QByteArray(1, char(1)), { }, -1, true, QByteArray());
    const QString error = DCTImageTestHelper::getDecodeError(jpeg, 65500, 32787, false);
    QVERIFY2(error.contains(QStringLiteral("65500")) && error.contains(QStringLiteral("32787")), qPrintable(error));
}

void ImageOptimizerTest::test_dct_truncated_entropy_segment()
{
    // Gray, 8 x 64: eight blocks, one per MCU row. The first is 200 (13 bits: the code of
    // category 7, its 7 bits, EOB), the second continues it (3 bits: difference 0, EOB) -
    // together exactly the 2 bytes kept. libjpeg reads the missing bits of the third block as
    // zeros (difference 0, EOB: 200 again), then it leaves the remaining blocks zero (128).
    const std::vector<int> blocks = { 200, 200, 200, 200, 56, 56, 56, 56 };
    const QByteArray truncated = DCTImageTestHelper::createJpeg(8, 64, QByteArray(1, char(1)), blocks, 2, false, QByteArray());
    const QByteArray truncatedWithEndMarker = DCTImageTestHelper::createJpeg(8, 64, QByteArray(1, char(1)), blocks, 2, true, QByteArray());
    QCOMPARE(truncatedWithEndMarker.size(), truncated.size() + 2);

    for (const bool withEndMarker : { false, true })
    {
        ErrorCollector reporter;
        const pdf::PDFImage image = DCTImageTestHelper::decode(withEndMarker ? truncatedWithEndMarker : truncated, 8, 64, false, &reporter);
        const pdf::PDFImageData& data = image.getImageData();
        QCOMPARE(data.getWidth(), 8u);
        QCOMPARE(data.getHeight(), 64u);
        QCOMPARE(data.getData().size(), qsizetype(8 * 64));

        for (int y = 0; y < 64; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                QCOMPARE(DCTImageTestHelper::getSample(image, x, y, 0), y < 24 ? 200 : 128);
            }
        }

        // The data without the end marker are reported, the libjpeg warning of the other is not passed on
        QCOMPARE(reporter.messages.size(), withEndMarker ? 0 : 1);
    }
}

void ImageOptimizerTest::test_dct_header_size_differs_from_dictionary()
{
    // ISO 32000-2, 8.9.5: /Width and /Height of the dictionary describe the image, the DCT
    // data carry their own size (16 x 8). The decoded data and every consumer use the size
    // of the data consistently, whether the dictionary declares more or less.
    const QByteArray jpeg = DCTImageTestHelper::createJpeg(16, 8, QByteArray(1, char(1)), { 200, 56 }, -1, true, QByteArray());
    for (const QSize& dictionarySize : { QSize(1000, 1000), QSize(1, 1) })
    {
        ErrorCollector reporter;
        const pdf::PDFImage image = DCTImageTestHelper::decode(jpeg, dictionarySize.width(), dictionarySize.height(), false, &reporter);
        const pdf::PDFImageData& data = image.getImageData();
        QCOMPARE(data.getWidth(), 16u);
        QCOMPARE(data.getHeight(), 8u);
        QCOMPARE(data.getData().size(), qsizetype(16 * 8));

        pdf::PDFCMSGeneric cms;
        const QImage picture = image.getImage(&cms, &reporter, nullptr);
        QCOMPARE(picture.size(), QSize(16, 8));
        QCOMPARE(qGray(picture.pixel(0, 0)), 200);
        QCOMPARE(qGray(picture.pixel(15, 7)), 56);
    }
}

void ImageOptimizerTest::test_dct_error_after_decoding()
{
    // A second SOF marker after the scan (B.2.1 allows one frame): libjpeg stops by an error
    // in jpeg_finish_decompress, after all rows were decoded into the buffer. The error is the
    // exception of the image with the text of libjpeg, nothing else is left behind.
    QByteArray secondFrame("\xFF\xC0", 2);
    secondFrame.append(QByteArray::fromHex("000B080008000801011100"));
    const QByteArray jpeg = DCTImageTestHelper::createJpeg(16, 16, QByteArray(1, char(1)), { 200, 56, 128, 200 }, -1, true, secondFrame);
    const QString error = DCTImageTestHelper::getDecodeError(jpeg, 16, 16, false);
    QVERIFY2(error.startsWith(QStringLiteral("Error reading JPEG (DCT) image: ")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("two SOF markers")), qPrintable(error));
}

/// Images of raw samples (no image filter, the data uncompressed) for the tests of the sizes of the
/// data in PDFImage::createImage. ISO 32000-2, 8.9.3: the samples of a row are packed, a row is
/// padded to whole bytes, so a row has ceil(width x components x bits / 8) bytes; 11.6.5.3: a soft
/// mask is a DeviceGray image; 8.9.6.2: an image mask (stencil) has one bit per sample.
class RawImageTestHelper
{
public:
    struct Image
    {
        int width = 0;
        int height = 0;
        QByteArray colorSpace;              ///< A device color space, empty for an image mask
        int bitsPerComponent = 8;
        bool isImageMask = false;
        std::vector<int> decode;            ///< /Decode, when not empty
        QByteArray data;
    };

    /// Decodes \p image with \p mask (when its width is not zero) as its /SMask or, when
    /// \p isExplicitMask, as its /Mask
    static pdf::PDFImage decode(const Image& image, const Image& mask, bool isExplicitMask, ErrorCollector* reporter);

    /// Message of the exception, by which decode() refuses the image, an empty string when it decodes it
    static QString getDecodeError(const Image& image, const Image& mask, bool isExplicitMask);

    /// Alpha of the decoded soft mask of \p image at \p x, \p y (its byte, the masks are 8-bit)
    static int getMaskSample(const pdf::PDFImage& image, int x, int y);

private:
    static pdf::PDFObjectReference addImage(pdf::PDFDocumentBuilder& builder, const Image& image, const char* maskKey, pdf::PDFObjectReference mask);
};

pdf::PDFObjectReference RawImageTestHelper::addImage(pdf::PDFDocumentBuilder& builder, const Image& image, const char* maskKey, pdf::PDFObjectReference mask)
{
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(image.width));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(image.height));
    if (image.isImageMask)
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ImageMask"), pdf::PDFObject::createBool(true));
    }
    else
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName(image.colorSpace));
    }
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(image.bitsPerComponent));
    if (!image.decode.empty())
    {
        pdf::PDFArrayBuilder decode;
        for (const int value : image.decode)
        {
            decode.appendItem(pdf::PDFObject::createInteger(value));
        }
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Decode"), pdf::PDFObject::createArray(std::move(decode)));
    }
    if (maskKey)
    {
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(maskKey), pdf::PDFObject::createReference(mask));
    }
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(image.data.size()));
    return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), QByteArray(image.data))));
}

pdf::PDFImage RawImageTestHelper::decode(const Image& image, const Image& mask, bool isExplicitMask, ErrorCollector* reporter)
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));

    const char* maskKey = nullptr;
    pdf::PDFObjectReference maskReference;
    if (mask.width > 0)
    {
        maskKey = isExplicitMask ? "Mask" : "SMask";
        maskReference = addImage(builder, mask, nullptr, pdf::PDFObjectReference());
    }
    const pdf::PDFObjectReference reference = addImage(builder, image, maskKey, maskReference);
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFObject& object = document.getObjectByReference(reference);
    pdf::PDFColorSpacePointer colorSpace;
    if (!image.isImageMask)
    {
        colorSpace = pdf::PDFAbstractColorSpace::createDeviceColorSpaceByName(nullptr, &document, nullptr, image.colorSpace);
    }
    return pdf::PDFImage::createImage(&document, object.getStream(), colorSpace, false, pdf::RenderingIntent::Perceptual, reporter);
}

QString RawImageTestHelper::getDecodeError(const Image& image, const Image& mask, bool isExplicitMask)
{
    ErrorCollector reporter;
    try
    {
        decode(image, mask, isExplicitMask, &reporter);
    }
    catch (const pdf::PDFException& exception)
    {
        return exception.getMessage();
    }
    catch (const pdf::PDFRendererException& exception)
    {
        return exception.getError().message;
    }
    return QString();
}

int RawImageTestHelper::getMaskSample(const pdf::PDFImage& image, int x, int y)
{
    const pdf::PDFImageData& mask = image.getSoftMaskData();
    return int(static_cast<unsigned char>(mask.getData().at(qsizetype(y) * qsizetype(mask.getStride()) + x)));
}

void ImageOptimizerTest::test_raw_soft_mask_valid()
{
    // DeviceRGB 2 x 2: red, green / blue, white; the 8-bit soft mask 0, 85 / 170, 255 (11.6.5.3:
    // the sample is the alpha). The rows have 6 and 2 bytes, the data are complete: nothing is
    // reported, the samples and the drawn picture are the data. Qt keeps the drawn picture
    // premultiplied: a channel of 0 or 255 comes back exactly, a fully transparent pixel has no color.
    RawImageTestHelper::Image image{ 2, 2, "DeviceRGB", 8, false, { }, QByteArray::fromHex("FF000000FF000000FFFFFFFF") };
    RawImageTestHelper::Image mask{ 2, 2, "DeviceGray", 8, false, { }, QByteArray::fromHex("0055AAFF") };
    ErrorCollector reporter;
    const pdf::PDFImage decoded = RawImageTestHelper::decode(image, mask, false, &reporter);
    QVERIFY2(reporter.messages.isEmpty(), qPrintable(reporter.messages.join('\n')));
    QCOMPARE(decoded.getImageData().getStride(), 6u);
    QCOMPARE(decoded.getImageData().getData(), image.data);
    QCOMPARE(decoded.getSoftMaskData().getStride(), 2u);
    QCOMPARE(decoded.getSoftMaskData().getData(), mask.data);

    pdf::PDFCMSGeneric cms;
    const QImage picture = decoded.getImage(&cms, &reporter, nullptr);
    QCOMPARE(picture.size(), QSize(2, 2));
    QCOMPARE(qAlpha(picture.pixel(0, 0)), 0);
    QCOMPARE(picture.pixelColor(1, 0), QColor(0, 255, 0, 85));
    QCOMPARE(picture.pixelColor(0, 1), QColor(0, 0, 255, 170));
    QCOMPARE(picture.pixelColor(1, 1), QColor(255, 255, 255, 255));
    QVERIFY2(reporter.messages.isEmpty(), qPrintable(reporter.messages.join('\n')));
}

void ImageOptimizerTest::test_raw_soft_mask_of_one_byte()
{
    // A soft mask of 64 x 64 8-bit samples needs 4096 bytes and has 1 (the value 200): the missing
    // samples are zero - transparent - and it is reported. Its rows were copied from behind the
    // buffer of 1 byte before (an out-of-bounds read of the heap into the alpha channel).
    RawImageTestHelper::Image image{ 64, 64, "DeviceGray", 8, false, { }, QByteArray(64 * 64, '\xFF') };
    RawImageTestHelper::Image mask{ 64, 64, "DeviceGray", 8, false, { }, QByteArray(1, '\xC8') };
    ErrorCollector reporter;
    const pdf::PDFImage decoded = RawImageTestHelper::decode(image, mask, false, &reporter);
    pdf::PDFCMSGeneric cms;
    const QImage picture = decoded.getImage(&cms, &reporter, nullptr);
    QCOMPARE(reporter.messages, QStringList({ QStringLiteral("Soft mask data are too short: 1 of 4096 bytes, the missing samples are zero.") }));
    QCOMPARE(decoded.getSoftMaskData().getData().size(), qsizetype(4096));
    QCOMPARE(picture.size(), QSize(64, 64));
    QCOMPARE(picture.pixelColor(0, 0), QColor(255, 255, 255, 200));
    for (int y = 0; y < 64; ++y)
    {
        for (int x = (y == 0) ? 1 : 0; x < 64; ++x)
        {
            QCOMPARE(qAlpha(picture.pixel(x, y)), 0);
        }
    }
}

void ImageOptimizerTest::test_raw_soft_mask_one_row_short()
{
    // 64 x 64, the rows 0 - 62 present (the row y has the value 4 y), the row 63 missing: it is
    // zero (transparent), the other rows are the data
    QByteArray data;
    for (int y = 0; y < 63; ++y)
    {
        data.append(QByteArray(64, char(4 * y)));
    }
    RawImageTestHelper::Image image{ 64, 64, "DeviceGray", 8, false, { }, QByteArray(64 * 64, '\xFF') };
    RawImageTestHelper::Image mask{ 64, 64, "DeviceGray", 8, false, { }, data };
    ErrorCollector reporter;
    const pdf::PDFImage decoded = RawImageTestHelper::decode(image, mask, false, &reporter);
    pdf::PDFCMSGeneric cms;
    const QImage picture = decoded.getImage(&cms, &reporter, nullptr);
    QCOMPARE(reporter.messages, QStringList({ QStringLiteral("Soft mask data are too short: 4032 of 4096 bytes, the missing samples are zero.") }));
    QCOMPARE(picture.size(), QSize(64, 64));
    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            QCOMPARE(RawImageTestHelper::getMaskSample(decoded, x, y), y < 63 ? 4 * y : 0);
            QCOMPARE(qAlpha(picture.pixel(x, y)), y < 63 ? 4 * y : 0);
        }
    }
}

void ImageOptimizerTest::test_raw_soft_mask_with_decode_short()
{
    // /Decode [1 0] inverts the mask (8.9.5.2): the sample 255 is the alpha 0, the sample 0 the
    // alpha 1. 16 x 16, 8 rows of 255 present: those rows are transparent, the padded rows 8 - 15
    // (zero samples) are opaque.
    RawImageTestHelper::Image image{ 16, 16, "DeviceGray", 8, false, { }, QByteArray(16 * 16, char(0)) };
    RawImageTestHelper::Image mask{ 16, 16, "DeviceGray", 8, false, { 1, 0 }, QByteArray(16 * 8, '\xFF') };
    ErrorCollector reporter;
    const pdf::PDFImage decoded = RawImageTestHelper::decode(image, mask, false, &reporter);
    pdf::PDFCMSGeneric cms;
    const QImage picture = decoded.getImage(&cms, &reporter, nullptr);
    QCOMPARE(reporter.messages, QStringList({ QStringLiteral("Soft mask data are too short: 128 of 256 bytes, the missing samples are zero.") }));
    QCOMPARE(picture.size(), QSize(16, 16));
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            QCOMPARE(qAlpha(picture.pixel(x, y)), y < 8 ? 0 : 255);
        }
    }
}

void ImageOptimizerTest::test_raw_soft_mask_of_one_bit_short()
{
    // A soft mask of 1-bit samples, 16 x 16: a row has 2 bytes, 2 rows of 0xFF (alpha 1) are
    // present, the other 14 rows are padded by zeros (alpha 0), as an 8-bit mask
    RawImageTestHelper::Image image{ 16, 16, "DeviceGray", 8, false, { }, QByteArray(16 * 16, char(0)) };
    RawImageTestHelper::Image mask{ 16, 16, "DeviceGray", 1, false, { }, QByteArray(4, '\xFF') };
    ErrorCollector reporter;
    const pdf::PDFImage decoded = RawImageTestHelper::decode(image, mask, false, &reporter);
    pdf::PDFCMSGeneric cms;
    const QImage picture = decoded.getImage(&cms, &reporter, nullptr);
    QCOMPARE(reporter.messages, QStringList({ QStringLiteral("Soft mask data are too short: 4 of 32 bytes, the missing samples are zero.") }));
    QCOMPARE(decoded.getSoftMaskData().getStride(), 2u);
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            QCOMPARE(qAlpha(picture.pixel(x, y)), y < 2 ? 255 : 0);
        }
    }
}

void ImageOptimizerTest::test_raw_image_short()
{
    // The samples of an image itself are refused when they are shorter, as their reading refused
    // them before: DeviceRGB 4 x 4 needs 48 bytes, 47 are present. Complete data are decoded.
    RawImageTestHelper::Image image{ 4, 4, "DeviceRGB", 8, false, { }, QByteArray(47, char(10)) };
    QCOMPARE(RawImageTestHelper::getDecodeError(image, RawImageTestHelper::Image(), false), QStringLiteral("Image data are too short: 47 of 48 bytes."));

    image.data.append(char(10));
    QCOMPARE(RawImageTestHelper::getDecodeError(image, RawImageTestHelper::Image(), false), QString());

    // 4 bits, 3 x 1: 36 bits are 5 bytes (the row padded to a whole byte)
    RawImageTestHelper::Image packed{ 3, 1, "DeviceRGB", 4, false, { }, QByteArray(4, char(0)) };
    QCOMPARE(RawImageTestHelper::getDecodeError(packed, RawImageTestHelper::Image(), false), QStringLiteral("Image data are too short: 4 of 5 bytes."));
}

void ImageOptimizerTest::test_raw_image_mask_short()
{
    // An image mask (stencil, 1 bit per sample) of 16 x 16 needs 32 bytes, 3 are present
    RawImageTestHelper::Image stencil{ 16, 16, QByteArray(), 1, true, { }, QByteArray(3, char(0)) };
    QCOMPARE(RawImageTestHelper::getDecodeError(stencil, RawImageTestHelper::Image(), false), QStringLiteral("Image data are too short: 3 of 32 bytes."));
}

void ImageOptimizerTest::test_raw_explicit_mask_short()
{
    // An explicit mask (/Mask, an image mask: 8.9.6.3) of 16 x 16 with 3 of its 32 bytes is refused
    // as the image mask itself; complete, it masks the image (the sample 1 masks out, its alpha 0)
    RawImageTestHelper::Image image{ 16, 16, "DeviceGray", 8, false, { }, QByteArray(16 * 16, char(0)) };
    RawImageTestHelper::Image mask{ 16, 16, QByteArray(), 1, true, { }, QByteArray(3, char(0)) };
    QCOMPARE(RawImageTestHelper::getDecodeError(image, mask, true), QStringLiteral("Image data are too short: 3 of 32 bytes."));

    mask.data = QByteArray(16, '\xFF') + QByteArray(16, char(0));
    ErrorCollector reporter;
    const pdf::PDFImage decoded = RawImageTestHelper::decode(image, mask, true, &reporter);
    pdf::PDFCMSGeneric cms;
    const QImage picture = decoded.getImage(&cms, &reporter, nullptr);
    for (int y = 0; y < 16; ++y)
    {
        QCOMPARE(qAlpha(picture.pixel(5, y)), y < 8 ? 0 : 255);
    }
}

void ImageOptimizerTest::test_raw_alpha_mask_of_short_data_is_refused()
{
    // Data built by hand, not by PDFImage::createImage (which pads or refuses them): a mask of
    // 4 x 4 8-bit samples (stride 4) with 10 bytes - its rows 2 and 3 are not in the data; a
    // mask of 4 x 2 with the stride 2 below its width and 4 bytes - its row 1 would be read from
    // the bytes 2 - 5. Both are refused, no row is read behind the data.
    const pdf::PDFImageData shortData(1, 8, 4, 4, 4, pdf::PDFImageData::MaskingType::None, QByteArray(10, char(1)), { }, { }, { });
    const pdf::PDFImageData narrowStride(1, 8, 4, 2, 2, pdf::PDFImageData::MaskingType::None, QByteArray(4, char(1)), { }, { }, { });
    for (const pdf::PDFImageData* data : { &shortData, &narrowStride })
    {
        QString error;
        try
        {
            pdf::PDFAbstractColorSpace::createAlphaMask(*data);
        }
        catch (const pdf::PDFException& exception)
        {
            error = exception.getMessage();
        }
        QVERIFY2(!error.isEmpty(), "the mask was read");
    }

    // The same mask with the 16 bytes it needs is read
    const pdf::PDFImageData complete(1, 8, 4, 4, 4, pdf::PDFImageData::MaskingType::None, QByteArray(16, char(7)), { }, { }, { });
    const QImage alpha = pdf::PDFAbstractColorSpace::createAlphaMask(complete);
    QCOMPARE(alpha.size(), QSize(4, 4));
    QCOMPARE(qAlpha(alpha.pixel(3, 3)), 7);
}

QTEST_APPLESS_MAIN(ImageOptimizerTest)

#include "tst_imageoptimizertest.moc"
