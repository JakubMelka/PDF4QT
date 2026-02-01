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
#include "pdfconstants.h"

#include <QtTest>
#include <QColor>
#include <QImage>

#include <memory>
#include <random>

class ImageOptimizerTest : public QObject
{
    Q_OBJECT

private slots:
    void test_bitonal_conversion_otsu();
    void test_bitonal_conversion_manual();
    void test_image_analysis_classification();
    void test_optimizer_keeps_original_if_larger();
    void test_optimizer_preserves_smask();
    void test_optimizer_reduces_size_for_photo();

private:
    static QImage createLineArtImage(int size);
    static QImage createTextScanImage(int size);
    static QImage createPhotoImage(int size);
    static QImage createAlphaImage(int size);
    static QImage extractAlphaMask(const QImage& image);
    static pdf::PDFDocument createDocumentWithImage(const QImage& image,
                                                    bool addSoftMask,
                                                    pdf::PDFImage::ImageCompression compression);
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
        imageStream = pdf::PDFStream(std::move(dict), content ? *content : QByteArray());
    }

    pdf::PDFObjectReference imageRef = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(imageStream)));

    QByteArray content("q 200 0 0 200 0 0 cm /Im1 Do Q");
    pdf::PDFDictionary contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                         pdf::PDFObject::createInteger(content.size()));
    pdf::PDFStream contentStream(std::move(contentDict), content);
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

    const pdf::PDFObject& filterObject = optimized.getObject(dictionary->get(PDF_STREAM_DICT_FILTER));
    QVERIFY(filterObject.isName());
    QCOMPARE(QString::fromLatin1(filterObject.getString()), QString("RunLengthDecode"));
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

QTEST_APPLESS_MAIN(ImageOptimizerTest)

#include "tst_imageoptimizertest.moc"
