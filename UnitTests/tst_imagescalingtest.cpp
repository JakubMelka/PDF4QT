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

#include "pdfimagescaling.h"
#include "pdfpainter.h"
#include "pdfblpainter.h"

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <QBuffer>
#include <QPdfWriter>
#include <QPicture>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using ImageType = pdf::PDFImageScaling::ImageType;

static constexpr QRgb BLACK = qRgb(0, 0, 0);
static constexpr QRgb WHITE = qRgb(255, 255, 255);

/// Creates a bitonal image. The vector \p inkSamples holds one value per pixel, row by row;
/// a nonzero value is the ink. When \p isInkSampleOne is false (the layout of a real scan),
/// the ink is stored as the sample value zero and the color table is { ink, paper };
/// otherwise the ink is the sample value one and the color table is { paper, ink }. Both of
/// the representations describe the very same image, so they must be scaled to the very same
/// result.
static QImage createBitonalImage(int width,
                                 int height,
                                 const std::vector<int>& inkSamples,
                                 QRgb inkColor = BLACK,
                                 QRgb paperColor = WHITE,
                                 bool isInkSampleOne = false,
                                 QImage::Format format = QImage::Format_Mono)
{
    QImage image(width, height, format);
    image.setColorTable(isInkSampleOne ? QList<QRgb>{ paperColor, inkColor } : QList<QRgb>{ inkColor, paperColor });
    image.fill(0u);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const bool isInk = inkSamples[size_t(y) * size_t(width) + size_t(x)] != 0;
            image.setPixel(x, y, uint(isInk == isInkSampleOne ? 1 : 0));
        }
    }

    return image;
}

/// Creates the ink samples of a pattern of vertical strokes of a single pixel, which repeat
/// with the given period
static std::vector<int> createStrokeSamples(int width, int height, int period, int phase)
{
    std::vector<int> samples(size_t(width) * size_t(height), 0);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (x % period == phase)
            {
                samples[size_t(y) * size_t(width) + size_t(x)] = 1;
            }
        }
    }

    return samples;
}

/// Creates the ink samples of vertical bands, which cover the given fraction of each period
static std::vector<int> createBandSamples(int width, int height, int period, int inkWidth)
{
    std::vector<int> samples(size_t(width) * size_t(height), 0);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (x % period < inkWidth)
            {
                samples[size_t(y) * size_t(width) + size_t(x)] = 1;
            }
        }
    }

    return samples;
}

/// Creates a stencil mask - an image, in which each pixel is either fully transparent, or it
/// has the given opaque color. A nonzero sample is the opaque (ink) one.
static QImage createStencilMask(int width,
                                int height,
                                const std::vector<int>& inkSamples,
                                QRgb color = BLACK,
                                QImage::Format format = QImage::Format_ARGB32_Premultiplied)
{
    QImage image(width, height, format);
    image.fill(Qt::transparent);

    for (int y = 0; y < height; ++y)
    {
        QRgb* scanLine = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x)
        {
            if (inkSamples[size_t(y) * size_t(width) + size_t(x)] != 0)
            {
                scanLine[x] = qRgba(qRed(color), qGreen(color), qBlue(color), 255);
            }
        }
    }

    return image;
}

/// Returns the ink coverage of a pixel of a grayscale image of a black ink on a white paper
static double getInkCoverage(const QImage& image, int x, int y)
{
    return double(255 - image.constScanLine(y)[x]) / 255.0;
}

/// Paint device, which reports an arbitrary device type. The decision about the downscaling
/// of the images queries the type only, so the device need not be able to paint.
class DeviceTypePaintDevice : public QPaintDevice
{
public:
    explicit DeviceTypePaintDevice(int deviceType) :
        m_deviceType(deviceType)
    {

    }

    virtual int devType() const override { return m_deviceType; }
    virtual QPaintEngine* paintEngine() const override { return nullptr; }

private:
    int m_deviceType;
};

class ImageScalingTest : public QObject
{
    Q_OBJECT

private slots:
    void testGeometryNoShrink();
    void testGeometryShrink();
    void testImageTypeDetection();
    void testImageTypeDetectionOfDegenerateColorTables();
    void testInkConservation();
    void testStrokePreservation();
    void testColorTableInversion();
    void testBitOrderAndInkPolarity();
    void testAnisotropicScaling();
    void testStencilMask();
    void testHighInkFraction();
    void testEndpoints();
    void testOutputFormat();
    void testGenericPathUnchanged();
    void testRejectedArguments();
    void testUnallocatableResult();
    void testDegenerate();
    void testDownscalingEnabled();
    void testCache();
    void testPrecompiledPageDrawing();
    void testPrecompiledPageTargets();
    void testHighDpiGeometry();
    void testHighDpiImageResolution();
};


void ImageScalingTest::testGeometryNoShrink()
{
    const QSize imageSize(1000, 800);

    // Enlargement
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(2000.0, 1600.0)).isValid());

    // Exactly the size of the image - nothing to do
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(1000.0, 800.0)).isValid());

    // An empty size of the image
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(QSize(), QTransform::fromScale(100.0, 100.0)).isValid());

    // Degenerate transforms - the width and the height are tested separately
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(0.0, 100.0)).isValid());
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(100.0, 0.0)).isValid());

    // Skewed transform
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform(100.0, 0.0, 50.0, 100.0, 0.0, 0.0)).isValid());

    // Projective transform
    QTransform projectiveTransform(100.0, 0.0, 0.001, 0.0, 100.0, 0.0, 0.0, 0.0, 1.0);
    QVERIFY(!projectiveTransform.isAffine());
    QVERIFY(!pdf::PDFImageScaling::getDownscaledSize(imageSize, projectiveTransform).isValid());
}

void ImageScalingTest::testGeometryShrink()
{
    const QSize imageSize(1000, 800);

    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(250.0, 200.0)), QSize(250, 200));

    // The aspect ratio is deliberately not preserved - the resolution of an anisotropically
    // scaled image must not be thrown away
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(100.0, 800.0)), QSize(100, 800));

    // Vertical flip of the PDF coordinate system
    QTransform flippedTransform = QTransform::fromScale(1.0, -1.0) * QTransform::fromScale(250.0, 200.0);
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, flippedTransform), QSize(250, 200));

    // Rotation keeps the mapped vectors orthogonal
    QTransform rotatedTransform = QTransform::fromScale(250.0, 200.0) * QTransform().rotate(37.0);
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, rotatedTransform), QSize(250, 200));

    // An extreme shrink must not produce a zero size
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(QSize(5000, 5000), QTransform::fromScale(0.5, 0.5)), QSize(1, 1));
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform()), QSize(1, 1));

    // One of the axes can be enlarged, while the image as a whole is shrunk - the target
    // size of such an axis is limited by the size of the image
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(1200.0, 1.0)), QSize(1000, 1));
    QCOMPARE(pdf::PDFImageScaling::getDownscaledSize(imageSize, QTransform::fromScale(1.0, 1000.0)), QSize(1, 800));
}

void ImageScalingTest::testImageTypeDetection()
{
    const std::vector<int> samples = createStrokeSamples(32, 8, 4, 0);

    QCOMPARE(pdf::PDFImageScaling::getImageType(createBitonalImage(32, 8, samples)), ImageType::Monochrome);
    QCOMPARE(pdf::PDFImageScaling::getImageType(createBitonalImage(32, 8, samples, BLACK, WHITE, false, QImage::Format_MonoLSB)), ImageType::Monochrome);
    QCOMPARE(pdf::PDFImageScaling::getImageType(createStencilMask(32, 8, samples)), ImageType::StencilMask);
    QCOMPARE(pdf::PDFImageScaling::getImageType(createStencilMask(32, 8, samples, BLACK, QImage::Format_ARGB32)), ImageType::StencilMask);

    QVERIFY(pdf::PDFImageScaling::isBitonal(ImageType::Monochrome));
    QVERIFY(pdf::PDFImageScaling::isBitonal(ImageType::StencilMask));
    QVERIFY(!pdf::PDFImageScaling::isBitonal(ImageType::Generic));

    // A null image
    QCOMPARE(pdf::PDFImageScaling::getImageType(QImage()), ImageType::Generic);

    // A fully opaque image of a single color is not a stencil mask
    QImage opaqueImage(8, 8, QImage::Format_ARGB32_Premultiplied);
    opaqueImage.fill(Qt::black);
    QCOMPARE(pdf::PDFImageScaling::getImageType(opaqueImage), ImageType::Generic);

    // Neither is a fully transparent image
    QImage transparentImage(8, 8, QImage::Format_ARGB32_Premultiplied);
    transparentImage.fill(Qt::transparent);
    QCOMPARE(pdf::PDFImageScaling::getImageType(transparentImage), ImageType::Generic);

    // Neither is an image with a partial transparency
    QImage partiallyTransparentImage = createStencilMask(32, 8, samples);
    partiallyTransparentImage.setPixel(1, 1, qRgba(0, 0, 0, 128));
    QCOMPARE(pdf::PDFImageScaling::getImageType(partiallyTransparentImage), ImageType::Generic);

    // Neither is an image with two different opaque colors
    QImage twoColorImage = createStencilMask(32, 8, samples);
    twoColorImage.setPixel(0, 0, qRgba(255, 0, 0, 255));
    QCOMPARE(pdf::PDFImageScaling::getImageType(twoColorImage), ImageType::Generic);

    // A grayscale image is a generic one
    QImage grayscaleImage(8, 8, QImage::Format_Grayscale8);
    grayscaleImage.fill(128);
    QCOMPARE(pdf::PDFImageScaling::getImageType(grayscaleImage), ImageType::Generic);
}

void ImageScalingTest::testImageTypeDetectionOfDegenerateColorTables()
{
    const std::vector<int> samples = createStrokeSamples(32, 8, 4, 0);

    // A color table of a single entry
    QImage singleEntryImage = createBitonalImage(32, 8, samples);
    singleEntryImage.setColorTable({ BLACK });
    QCOMPARE(pdf::PDFImageScaling::getImageType(singleEntryImage), ImageType::Generic);

    // A color table of two identical entries
    QCOMPARE(pdf::PDFImageScaling::getImageType(createBitonalImage(32, 8, samples, qRgb(17, 17, 17), qRgb(17, 17, 17))), ImageType::Generic);

    // A color table, whose entries are not opaque - both of the entries are tested
    QImage transparentInkImage = createBitonalImage(32, 8, samples);
    transparentInkImage.setColorTable({ qRgba(0, 0, 0, 128), WHITE });
    QCOMPARE(pdf::PDFImageScaling::getImageType(transparentInkImage), ImageType::Generic);

    QImage transparentPaperImage = createBitonalImage(32, 8, samples);
    transparentPaperImage.setColorTable({ BLACK, qRgba(255, 255, 255, 128) });
    QCOMPARE(pdf::PDFImageScaling::getImageType(transparentPaperImage), ImageType::Generic);
}

void ImageScalingTest::testInkConservation()
{
    // Without the stem darkening curve the downscaler is an exact area average, so it must
    // conserve the total amount of the ink. The sizes are deliberately not divisors of each
    // other, so the fractional arithmetic of the boxes is tested.
    const int sourceWidth = 1000;
    std::vector<int> samples(size_t(sourceWidth), 0);

    quint32 randomState = 12345u;
    int inkCount = 0;
    for (int x = 0; x < sourceWidth; ++x)
    {
        randomState = randomState * 1103515245u + 12345u;
        if (((randomState >> 16) & 3u) == 0u)
        {
            samples[size_t(x)] = 1;
            ++inkCount;
        }
    }

    QVERIFY(inkCount > 0);

    const QImage image = createBitonalImage(sourceWidth, 1, samples);

    for (int targetWidth : { 37, 333, 500, 999 })
    {
        const QImage scaledImage = pdf::PDFImageScaling::scaleDownBitonal(image, QSize(targetWidth, 1), ImageType::Monochrome, 1.0);
        QVERIFY(!scaledImage.isNull());
        QCOMPARE(scaledImage.size(), QSize(targetWidth, 1));

        const double scale = double(sourceWidth) / double(targetWidth);
        double totalInk = 0.0;
        for (int x = 0; x < targetWidth; ++x)
        {
            totalInk += getInkCoverage(scaledImage, x, 0) * scale;
        }

        // The tolerance is the quantization of the coverage into eight bits, accumulated
        // over all the destination pixels
        const double tolerance = 0.5 * scale * double(targetWidth) / 255.0 + 1e-6;
        QVERIFY2(std::abs(totalInk - double(inkCount)) <= tolerance,
                 qPrintable(QString("target width %1: ink %2, expected %3, tolerance %4")
                            .arg(targetWidth).arg(totalInk).arg(inkCount).arg(tolerance)));
    }

    // The same in both of the dimensions at once, with a source height, which is not a
    // multiple of the target height, so the source rows on the boundaries are shared by
    // two destination rows
    const int sourceHeight = 100;
    const int targetHeight = 30;
    const QImage image2D = createBitonalImage(sourceWidth, sourceHeight, createStrokeSamples(sourceWidth, sourceHeight, 4, 0));
    const QImage scaledImage2D = pdf::PDFImageScaling::scaleDownBitonal(image2D, QSize(333, targetHeight), ImageType::Monochrome, 1.0);

    QVERIFY(!scaledImage2D.isNull());

    double totalInk2D = 0.0;
    for (int y = 0; y < targetHeight; ++y)
    {
        for (int x = 0; x < 333; ++x)
        {
            totalInk2D += getInkCoverage(scaledImage2D, x, y);
        }
    }

    totalInk2D *= (double(sourceWidth) / 333.0) * (double(sourceHeight) / double(targetHeight));
    const double expectedInk2D = double(sourceWidth / 4) * double(sourceHeight);
    QVERIFY2(std::abs(totalInk2D - expectedInk2D) <= 0.01 * expectedInk2D,
             qPrintable(QString("ink %1, expected %2").arg(totalInk2D).arg(expectedInk2D)));
}

void ImageScalingTest::testStrokePreservation()
{
    // The scenario of the issue - strokes of a single pixel every six pixels, shrunk three
    // times. Every stroke must survive, regardless of its phase relative to the sampling
    // grid, and it must be clearly darker than the paper.
    const int width = 600;
    const int height = 6;
    const int period = 6;

    std::vector<double> darkestCoverages;

    for (int phase = 0; phase < period; ++phase)
    {
        const QImage image = createBitonalImage(width, height, createStrokeSamples(width, height, period, phase));
        const QImage scaledImage = pdf::PDFImageScaling::scaleDown(image, QSize(width / 3, height / 3), ImageType::Monochrome);

        QVERIFY(!scaledImage.isNull());
        QCOMPARE(scaledImage.format(), QImage::Format_Grayscale8);

        // The strokes repeat with the period of two destination pixels, so each pair of the
        // destination pixels must contain one clearly visible stroke. Without the area
        // averaging the stroke either lands on a sampling point or it vanishes.
        double darkestOfPhase = 1.0;
        for (int x = 0; x + 1 < scaledImage.width(); x += 2)
        {
            const double firstCoverage = getInkCoverage(scaledImage, x, 0);
            const double secondCoverage = getInkCoverage(scaledImage, x + 1, 0);
            const double coverage = qMax(firstCoverage, secondCoverage);

            QVERIFY2(coverage >= 0.45,
                     qPrintable(QString("phase %1, pixel %2: the coverage %3 is too low").arg(phase).arg(x).arg(coverage)));

            // The paper next to the stroke stays blank - the stroke is not smeared over the
            // whole pair of the destination pixels
            QCOMPARE(qMin(firstCoverage, secondCoverage), 0.0);

            darkestOfPhase = qMin(darkestOfPhase, coverage);
        }

        darkestCoverages.push_back(darkestOfPhase);
    }

    // The result must be nearly independent on the phase - the phase dependency is exactly
    // what makes the bug visible
    const double minimumCoverage = *std::min_element(darkestCoverages.cbegin(), darkestCoverages.cend());
    const double maximumCoverage = *std::max_element(darkestCoverages.cbegin(), darkestCoverages.cend());
    QVERIFY2(maximumCoverage - minimumCoverage < 0.05,
             qPrintable(QString("the phase dependency %1 is too high").arg(maximumCoverage - minimumCoverage)));
}

void ImageScalingTest::testColorTableInversion()
{
    const int width = 600;
    const int height = 6;
    const QSize targetSize(width / 3, height / 3);

    const std::vector<int> samples = createStrokeSamples(width, height, 6, 2);

    // The very same image stored in the two possible representations - the ink as the sample
    // value zero with the color table { black, white }, and the ink as the sample value one
    // with the color table { white, black }. Both must be scaled to a pixel identical result;
    // the color table must never be assumed to start with the black color.
    const QImage image = createBitonalImage(width, height, samples, BLACK, WHITE, false);
    const QImage equivalentImage = createBitonalImage(width, height, samples, BLACK, WHITE, true);

    QCOMPARE(image.colorTable(), (QList<QRgb>{ BLACK, WHITE }));
    QCOMPARE(equivalentImage.colorTable(), (QList<QRgb>{ WHITE, BLACK }));

    const QImage scaledImage = pdf::PDFImageScaling::scaleDown(image, targetSize, ImageType::Monochrome);
    const QImage scaledEquivalentImage = pdf::PDFImageScaling::scaleDown(equivalentImage, targetSize, ImageType::Monochrome);

    QVERIFY(!scaledImage.isNull());
    QCOMPARE(scaledImage, scaledEquivalentImage);

    // The dark mode - the inverted colors mode inverts the image, so the glyphs are drawn by
    // the lighter color. They must stay light on the dark background, i.e. the minority must
    // be treated as the ink; treating the darker color as the ink would erode them.
    QImage darkModeImage = image;
    darkModeImage.invertPixels(QImage::InvertRgb);
    QCOMPARE(pdf::PDFImageScaling::getImageType(darkModeImage), ImageType::Monochrome);

    const QImage scaledDarkModeImage = pdf::PDFImageScaling::scaleDown(darkModeImage, targetSize, ImageType::Monochrome);
    QVERIFY(!scaledDarkModeImage.isNull());

    for (int x = 0; x + 1 < scaledDarkModeImage.width(); x += 2)
    {
        const int firstValue = scaledDarkModeImage.constScanLine(0)[x];
        const int secondValue = scaledDarkModeImage.constScanLine(0)[x + 1];

        QVERIFY2(qMax(firstValue, secondValue) >= int(0.45 * 255.0),
                 qPrintable(QString("pixel %1: the stroke value %2 is too dark").arg(x).arg(qMax(firstValue, secondValue))));

        // The background next to the stroke stays dark
        QCOMPARE(qMin(firstValue, secondValue), 0);
    }
}

void ImageScalingTest::testBitOrderAndInkPolarity()
{
    // All the four combinations of the bit order of the format and of the sample value of the
    // ink must give the very same result. The width is deliberately not a multiple of eight,
    // so the padding bits of the last byte of each row are exercised as well - they must
    // neither be counted as the ink nor unpacked into the row.
    const int width = 101;
    const int height = 12;
    const QSize targetSize(width / 4, height / 4);

    const std::vector<int> samples = createStrokeSamples(width, height, 5, 1);

    const QImage monoInkZero = createBitonalImage(width, height, samples, BLACK, WHITE, false, QImage::Format_Mono);
    const QImage monoInkOne = createBitonalImage(width, height, samples, BLACK, WHITE, true, QImage::Format_Mono);
    const QImage monoLsbInkZero = createBitonalImage(width, height, samples, BLACK, WHITE, false, QImage::Format_MonoLSB);
    const QImage monoLsbInkOne = createBitonalImage(width, height, samples, BLACK, WHITE, true, QImage::Format_MonoLSB);

    const QImage expectedImage = pdf::PDFImageScaling::scaleDown(monoInkZero, targetSize, ImageType::Monochrome);
    QVERIFY(!expectedImage.isNull());

    QCOMPARE(pdf::PDFImageScaling::scaleDown(monoInkOne, targetSize, ImageType::Monochrome), expectedImage);
    QCOMPARE(pdf::PDFImageScaling::scaleDown(monoLsbInkZero, targetSize, ImageType::Monochrome), expectedImage);
    QCOMPARE(pdf::PDFImageScaling::scaleDown(monoLsbInkOne, targetSize, ImageType::Monochrome), expectedImage);

    // A width, which is a multiple of eight, has no padding bits at all
    const QImage alignedImage = createBitonalImage(104, height, createStrokeSamples(104, height, 5, 1));
    QVERIFY(!pdf::PDFImageScaling::scaleDown(alignedImage, QSize(26, 3), ImageType::Monochrome).isNull());
}

void ImageScalingTest::testAnisotropicScaling()
{
    // Only one of the axes is shrunk, the other one keeps its size, so every destination
    // pixel of that axis lies inside a single source pixel
    const int width = 64;
    const int height = 64;
    const std::vector<int> samples = createStrokeSamples(width, height, 4, 0);
    const QImage image = createBitonalImage(width, height, samples);

    const QImage scaledImage = pdf::PDFImageScaling::scaleDownBitonal(image, QSize(width, height / 4), ImageType::Monochrome, 1.0);
    QVERIFY(!scaledImage.isNull());
    QCOMPARE(scaledImage.size(), QSize(width, height / 4));

    // The strokes are vertical, so shrinking the height alone keeps the columns intact
    for (int x = 0; x < width; ++x)
    {
        const double expectedCoverage = (x % 4 == 0) ? 1.0 : 0.0;
        QCOMPARE(getInkCoverage(scaledImage, x, 0), expectedCoverage);
    }
}

void ImageScalingTest::testStencilMask()
{
    const int width = 600;
    const int height = 6;
    const int period = 6;
    const QSize targetSize(width / 3, height / 3);

    const std::vector<int> samples = createStrokeSamples(width, height, period, 1);
    const QImage image = createStencilMask(width, height, samples, qRgb(0, 0, 255));

    const QImage scaledImage = pdf::PDFImageScaling::scaleDown(image, targetSize, ImageType::StencilMask);
    QVERIFY(!scaledImage.isNull());
    QCOMPARE(scaledImage.format(), QImage::Format_ARGB32_Premultiplied);

    for (int x = 0; x + 1 < scaledImage.width(); x += 2)
    {
        const QRgb* scanLine = reinterpret_cast<const QRgb*>(scaledImage.constScanLine(0));
        const int alpha = qMax(qAlpha(scanLine[x]), qAlpha(scanLine[x + 1]));
        QVERIFY2(alpha >= int(0.45 * 255.0), qPrintable(QString("pixel %1: the alpha %2 is too low").arg(x).arg(alpha)));
    }

    // The output is premultiplied, so no channel can exceed the alpha, and the color must
    // keep its hue (the mask is blue)
    for (int x = 0; x < scaledImage.width(); ++x)
    {
        const QRgb pixel = reinterpret_cast<const QRgb*>(scaledImage.constScanLine(0))[x];
        QCOMPARE(qRed(pixel), 0);
        QCOMPARE(qGreen(pixel), 0);
        QVERIFY(qBlue(pixel) <= qAlpha(pixel));
    }

    // A mask, which is not premultiplied, gives the very same result
    const QImage notPremultipliedImage = createStencilMask(width, height, samples, qRgb(0, 0, 255), QImage::Format_ARGB32);
    QCOMPARE(pdf::PDFImageScaling::scaleDown(notPremultipliedImage, targetSize, ImageType::StencilMask), scaledImage);

    // Without the stem darkening curve the total opacity is conserved
    const QImage areaAveragedImage = pdf::PDFImageScaling::scaleDownBitonal(image, targetSize, ImageType::StencilMask, 1.0);
    QVERIFY(!areaAveragedImage.isNull());

    double totalAlpha = 0.0;
    for (int y = 0; y < areaAveragedImage.height(); ++y)
    {
        const QRgb* scanLine = reinterpret_cast<const QRgb*>(areaAveragedImage.constScanLine(y));
        for (int x = 0; x < areaAveragedImage.width(); ++x)
        {
            totalAlpha += double(qAlpha(scanLine[x])) / 255.0;
        }
    }

    const double sourceOpaqueCount = double(width / period) * double(height);
    const double scaledOpaqueCount = totalAlpha * 9.0;
    QVERIFY2(std::abs(scaledOpaqueCount - sourceOpaqueCount) <= 0.02 * sourceOpaqueCount,
             qPrintable(QString("ink %1, expected %2").arg(scaledOpaqueCount).arg(sourceOpaqueCount)));
}

void ImageScalingTest::testHighInkFraction()
{
    // An image, which is covered by the ink from a large part, is not an ink on a paper, so
    // the stem darkening curve must not be applied to it - it is downscaled by a plain area
    // average, which does not bias it towards either of its two colors.
    const int width = 400;
    const int height = 20;
    const int period = 20;
    const int inkWidth = 9;

    const QImage image = createBitonalImage(width, height, createBandSamples(width, height, period, inkWidth));
    const QImage scaledImage = pdf::PDFImageScaling::scaleDown(image, QSize(width / period, 1), ImageType::Monochrome);

    QVERIFY(!scaledImage.isNull());

    const double expectedCoverage = double(inkWidth) / double(period);
    QVERIFY(expectedCoverage > pdf::PDFImageScaling::MAXIMUM_INK_FRACTION_FOR_DARKENING);

    for (int x = 0; x < scaledImage.width(); ++x)
    {
        QVERIFY2(std::abs(getInkCoverage(scaledImage, x, 0) - expectedCoverage) < 0.005,
                 qPrintable(QString("pixel %1: the coverage %2 differs from the area average %3")
                            .arg(x).arg(getInkCoverage(scaledImage, x, 0)).arg(expectedCoverage)));
    }

    // An image just below the limit is darkened
    const QImage darkenedImage = createBitonalImage(width, height, createBandSamples(width, height, period, 7));
    const QImage scaledDarkenedImage = pdf::PDFImageScaling::scaleDown(darkenedImage, QSize(width / period, 1), ImageType::Monochrome);

    QVERIFY(!scaledDarkenedImage.isNull());
    QVERIFY(getInkCoverage(scaledDarkenedImage, 0, 0) > 7.0 / double(period) + 0.05);
}

void ImageScalingTest::testEndpoints()
{
    // The transfer curve must keep its endpoints - a fully covered image stays a full ink and
    // a blank one stays a blank paper, for any exponent
    const int width = 64;
    const int height = 64;

    for (double gamma : { 1.0, pdf::PDFImageScaling::DEFAULT_INK_GAMMA, 0.4 })
    {
        const QImage inkImage = createBitonalImage(width, height, std::vector<int>(size_t(width) * size_t(height), 1));
        const QImage scaledInkImage = pdf::PDFImageScaling::scaleDownBitonal(inkImage, QSize(8, 8), ImageType::Monochrome, gamma);
        QVERIFY(!scaledInkImage.isNull());

        const QImage paperImage = createBitonalImage(width, height, std::vector<int>(size_t(width) * size_t(height), 0));
        const QImage scaledPaperImage = pdf::PDFImageScaling::scaleDownBitonal(paperImage, QSize(8, 8), ImageType::Monochrome, gamma);
        QVERIFY(!scaledPaperImage.isNull());

        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                QCOMPARE(scaledInkImage.constScanLine(y)[x], uchar(0));
                QCOMPARE(scaledPaperImage.constScanLine(y)[x], uchar(255));
            }
        }
    }
}

void ImageScalingTest::testOutputFormat()
{
    const int width = 120;
    const int height = 12;
    const std::vector<int> samples = createStrokeSamples(width, height, 6, 0);
    const QSize targetSize(width / 6, height / 4);

    // A neutral color table gives a grayscale image
    const QImage neutralImage = createBitonalImage(width, height, samples);
    QCOMPARE(pdf::PDFImageScaling::scaleDown(neutralImage, targetSize, ImageType::Monochrome).format(), QImage::Format_Grayscale8);

    // A color table, which is not neutral, gives a color image. Both of the entries are
    // tested - a neutral ink with a colored paper and a colored ink.
    const QImage coloredPaperImage = createBitonalImage(width, height, samples, BLACK, qRgb(20, 240, 250));
    QCOMPARE(pdf::PDFImageScaling::scaleDown(coloredPaperImage, targetSize, ImageType::Monochrome).format(), QImage::Format_RGB32);

    // A channel, in which only the last comparison of the neutrality decides
    const QImage almostNeutralImage = createBitonalImage(width, height, samples, qRgb(10, 10, 20), WHITE);
    QCOMPARE(pdf::PDFImageScaling::scaleDown(almostNeutralImage, targetSize, ImageType::Monochrome).format(), QImage::Format_RGB32);

    // The channels are interpolated correctly. The destination pixel covers exactly one
    // period of the strokes, so with an exact area average every destination pixel has the
    // very same coverage of one sixth.
    const QRgb inkColor = qRgb(200, 40, 10);
    const QRgb paperColor = qRgb(20, 240, 250);
    const QImage colorImage = createBitonalImage(width, height, samples, inkColor, paperColor);
    const QImage scaledColorImage = pdf::PDFImageScaling::scaleDownBitonal(colorImage, targetSize, ImageType::Monochrome, 1.0);

    QVERIFY(!scaledColorImage.isNull());
    QCOMPARE(scaledColorImage.format(), QImage::Format_RGB32);

    const double coverage = 1.0 / 6.0;

    for (int x = 0; x < scaledColorImage.width(); ++x)
    {
        const QRgb pixel = scaledColorImage.pixel(x, 1);

        QVERIFY(std::abs(qRed(pixel) - (qRed(paperColor) + (qRed(inkColor) - qRed(paperColor)) * coverage)) <= 1.5);
        QVERIFY(std::abs(qGreen(pixel) - (qGreen(paperColor) + (qGreen(inkColor) - qGreen(paperColor)) * coverage)) <= 1.5);
        QVERIFY(std::abs(qBlue(pixel) - (qBlue(paperColor) + (qBlue(inkColor) - qBlue(paperColor)) * coverage)) <= 1.5);
    }
}

void ImageScalingTest::testGenericPathUnchanged()
{
    QImage image(400, 400, QImage::Format_RGB888);
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
        }
    }

    const QSize targetSize(97, 123);
    const QImage scaledImage = pdf::PDFImageScaling::scaleDown(image, targetSize, ImageType::Generic);

    QCOMPARE(scaledImage, image.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));

    // A generic image must not be routed through the ink downscaler
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, targetSize, ImageType::Generic).isNull());
}

void ImageScalingTest::testRejectedArguments()
{
    const std::vector<int> samples = createStrokeSamples(64, 64, 4, 0);
    const QImage image = createBitonalImage(64, 64, samples);

    // Each of the four conditions of the target size is tested on its own
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, QSize(-1, 16), ImageType::Monochrome).isNull());
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, QSize(16, -1), ImageType::Monochrome).isNull());
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, QSize(128, 16), ImageType::Monochrome).isNull());
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, QSize(16, 128), ImageType::Monochrome).isNull());

    // An image, which is not monochromatic at all, declared as a monochromatic one
    QImage rgbImage(64, 64, QImage::Format_RGB888);
    rgbImage.fill(Qt::white);
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(rgbImage, QSize(16, 16), ImageType::Monochrome).isNull());

    // A monochromatic image with a color table of a single entry
    QImage singleEntryImage = createBitonalImage(64, 64, samples);
    singleEntryImage.setColorTable({ BLACK });
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(singleEntryImage, QSize(16, 16), ImageType::Monochrome).isNull());

    // A color table of two identical entries
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(createBitonalImage(64, 64, samples, qRgb(9, 9, 9), qRgb(9, 9, 9)),
                                                   QSize(16, 16), ImageType::Monochrome).isNull());

    // A color table, whose entries are not opaque - both of the entries are tested
    QImage transparentInkImage = createBitonalImage(64, 64, samples);
    transparentInkImage.setColorTable({ qRgba(0, 0, 0, 128), WHITE });
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(transparentInkImage, QSize(16, 16), ImageType::Monochrome).isNull());

    QImage transparentPaperImage = createBitonalImage(64, 64, samples);
    transparentPaperImage.setColorTable({ BLACK, qRgba(255, 255, 255, 128) });
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(transparentPaperImage, QSize(16, 16), ImageType::Monochrome).isNull());

    // A monochromatic image declared as a stencil mask
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, QSize(16, 16), ImageType::StencilMask).isNull());

    // A monochromatic image of the format MonoLSB is accepted
    const QImage monoLsbImage = createBitonalImage(64, 64, samples, BLACK, WHITE, false, QImage::Format_MonoLSB);
    QVERIFY(!pdf::PDFImageScaling::scaleDownBitonal(monoLsbImage, QSize(16, 16), ImageType::Monochrome).isNull());
}

void ImageScalingTest::testUnallocatableResult()
{
    // A monochromatic image carries a single bit per pixel, while the downscaled result
    // carries eight, so the result can exceed the limits of QImage even when the source
    // fits into them comfortably. QImage refuses an image, whose width multiplied by the
    // depth overflows an int, so a source wider than INT_MAX / 8 cannot be converted to
    // the grayscale format at its own width.
    const int width = 1 << 28;

    QVERIFY(width > std::numeric_limits<int>::max() / 8);

    QImage image(width, 1, QImage::Format_Mono);
    if (image.isNull())
    {
        QSKIP("The source image cannot be allocated on this machine");
    }

    image.setColorTable({ BLACK, WHITE });
    image.fill(0u);

    QCOMPARE(pdf::PDFImageScaling::getImageType(image), ImageType::Monochrome);
    QVERIFY(QImage(QSize(width, 1), QImage::Format_Grayscale8).isNull());

    // The ink downscaler reports the failure instead of returning an unusable image
    QVERIFY(pdf::PDFImageScaling::scaleDownBitonal(image, QSize(width, 1), ImageType::Monochrome).isNull());

    // The caller then falls back to the box filter of Qt, which keeps the format of the
    // source and therefore still gives a usable image
    const QImage scaledImage = pdf::PDFImageScaling::scaleDown(image, QSize(width, 1), ImageType::Monochrome);
    QVERIFY(!scaledImage.isNull());
    QCOMPARE(scaledImage.size(), QSize(width, 1));
}

void ImageScalingTest::testDegenerate()
{
    const std::vector<int> samples = createStrokeSamples(64, 64, 4, 0);

    // A degenerate color table falls back to the box filter of Qt, but the result must still
    // be a valid image
    const QImage degenerateImage = createBitonalImage(64, 64, samples, qRgb(9, 9, 9), qRgb(9, 9, 9));
    QVERIFY(!pdf::PDFImageScaling::scaleDown(degenerateImage, QSize(16, 16), ImageType::Monochrome).isNull());

    // An invalid target size or a null image
    const QImage image = createBitonalImage(64, 64, samples);
    QVERIFY(pdf::PDFImageScaling::scaleDown(image, QSize(), ImageType::Monochrome).isNull());
    QVERIFY(pdf::PDFImageScaling::scaleDown(QImage(), QSize(16, 16), ImageType::Monochrome).isNull());

    // The image is downscaled to a single pixel - a quarter of it is the ink
    const QImage singlePixelImage = pdf::PDFImageScaling::scaleDownBitonal(image, QSize(1, 1), ImageType::Monochrome, 1.0);
    QCOMPARE(singlePixelImage.size(), QSize(1, 1));
    QVERIFY(std::abs(getInkCoverage(singlePixelImage, 0, 0) - 0.25) < 0.01);
}

void ImageScalingTest::testDownscalingEnabled()
{
    const std::array rasterDeviceTypes = { QInternal::Widget, QInternal::Pixmap, QInternal::Image, QInternal::Pbuffer,
                                           QInternal::FramebufferObject, QInternal::CustomRaster, QInternal::PaintBuffer, QInternal::OpenGL };
    const std::array vectorDeviceTypes = { QInternal::UnknownDevice, QInternal::Printer, QInternal::Picture };

    for (const QInternal::PaintDeviceFlags deviceType : rasterDeviceTypes)
    {
        const DeviceTypePaintDevice paintDevice(deviceType);

        // Bitonal images are downscaled for a raster device always, other ones only on request
        QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Monochrome, false));
        QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::StencilMask, false));
        QVERIFY(!pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Generic, false));
        QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Generic, true));
        QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Monochrome, true));
    }

    for (const QInternal::PaintDeviceFlags deviceType : vectorDeviceTypes)
    {
        const DeviceTypePaintDevice paintDevice(deviceType);

        // A vector device stores the image into a document, so nothing is downscaled implicitly
        QVERIFY(!pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Monochrome, false));
        QVERIFY(!pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::StencilMask, false));
        QVERIFY(!pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Generic, false));

        // The feature SmoothImages keeps its former meaning for all of the devices
        QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Generic, true));
        QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&paintDevice, ImageType::Monochrome, true));
    }

    // The real paint devices report the types, which the decision relies on
    const QImage image(1, 1, QImage::Format_RGB32);
    QVERIFY(pdf::PDFImageScaling::isDownscalingEnabled(&image, ImageType::Monochrome, false));

    const QPicture picture;
    QVERIFY(!pdf::PDFImageScaling::isDownscalingEnabled(&picture, ImageType::Monochrome, false));

    QBuffer buffer;
    QPdfWriter pdfWriter(&buffer);
    QVERIFY(!pdf::PDFImageScaling::isDownscalingEnabled(&pdfWriter, ImageType::Monochrome, false));
}

void ImageScalingTest::testCache()
{
    const int width = 600;
    const int height = 12;
    const QImage image = createBitonalImage(width, height, createStrokeSamples(width, height, 6, 0));
    const QSize targetSize(200, 4);
    const QSize magnifiedTargetSize(400, 8);

    pdf::PDFScaledImageCache cache;
    QCOMPARE(cache.getImageCount(), size_t(0));

    const QImage expectedImage = pdf::PDFImageScaling::scaleDown(image, targetSize, ImageType::Monochrome);

    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);

        const QImage scaledImage = cache.getScaledImage(image, targetSize, ImageType::Monochrome);
        QCOMPARE(scaledImage, expectedImage);
        QCOMPARE(cache.getImageCount(), size_t(1));
        QCOMPARE(cache.getMemoryConsumptionEstimate(), qint64(expectedImage.sizeInBytes()));

        // A cache hit gives the very same image
        QCOMPARE(cache.getScaledImage(image, targetSize, ImageType::Monochrome), expectedImage);
        QCOMPARE(cache.getImageCount(), size_t(1));

        // The magnifier tool draws the same page a second time at a different zoom, so both
        // of the sizes must be stored at once
        QVERIFY(!cache.getScaledImage(image, magnifiedTargetSize, ImageType::Monochrome).isNull());
        QCOMPARE(cache.getImageCount(), size_t(2));

        // An image, which cannot be scaled, is not stored
        QVERIFY(cache.getScaledImage(image, QSize(), ImageType::Monochrome).isNull());
        QCOMPARE(cache.getImageCount(), size_t(2));
    }

    // Both of the images have been used in the pass, so both of them survive it
    QCOMPARE(cache.getImageCount(), size_t(2));

    // An image, which is not used any more, is discarded after MAXIMUM_PASS_AGE passes
    for (quint64 i = 0; i <= pdf::PDFScaledImageCache::MAXIMUM_PASS_AGE; ++i)
    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);
        cache.getScaledImage(image, targetSize, ImageType::Monochrome);
    }

    QCOMPARE(cache.getImageCount(), size_t(1));
    QCOMPARE(cache.getMemoryConsumptionEstimate(), qint64(expectedImage.sizeInBytes()));

    // Nested passes do not discard the images of the outer pass
    {
        pdf::PDFScaledImageCache::DrawingPassGuard outerGuard(&cache);
        cache.getScaledImage(image, targetSize, ImageType::Monochrome);

        for (quint64 i = 0; i <= pdf::PDFScaledImageCache::MAXIMUM_PASS_AGE; ++i)
        {
            pdf::PDFScaledImageCache::DrawingPassGuard innerGuard(&cache);
        }

        QCOMPARE(cache.getImageCount(), size_t(1));
    }

    // Outside of a drawing pass the image is scaled, but it is not stored
    cache.clear();
    QCOMPARE(cache.getImageCount(), size_t(0));
    QCOMPARE(cache.getMemoryConsumptionEstimate(), qint64(0));

    QCOMPARE(cache.getScaledImage(image, targetSize, ImageType::Monochrome), expectedImage);
    QCOMPARE(cache.getImageCount(), size_t(0));

    // A tiny image is not stored either - it would fill the cache with the tiles of the
    // tiling patterns
    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);

        const QImage tinyImage = createBitonalImage(16, 16, createStrokeSamples(16, 16, 4, 0));
        QVERIFY(!cache.getScaledImage(tinyImage, QSize(4, 4), ImageType::Monochrome).isNull());
        QCOMPARE(cache.getImageCount(), size_t(0));
    }
}

void ImageScalingTest::testPrecompiledPageDrawing()
{
    // The bug of the issue is in the replay of a precompiled page, so verify it right there:
    // a bitonal image of thin strokes, drawn through a world transform, which shrinks it
    // three times, must keep every one of the strokes.
    const int width = 600;
    const int height = 60;
    const QSize targetSize(width / 3, height / 3);

    const QImage image = createBitonalImage(width, height, createStrokeSamples(width, height, 6, 0));

    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    page.addImage(image);
    page.finalize(0, { });

    QVERIFY(page.isValid());

    auto drawPage = [&](pdf::PDFScaledImageCache* cache, pdf::PDFRenderer::Features features)
    {
        QImage target(targetSize, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);

        QPainter painter(&target);
        page.draw(&painter, QRectF(), QTransform::fromScale(targetSize.width(), targetSize.height()),
                  features, 1.0, cache);
        painter.end();

        return target;
    };

    // Every pair of the destination pixels holds exactly one clearly visible stroke. Without
    // the downscaling the raster paint engine samples the source bilinearly and the strokes
    // fade unevenly, depending on their phase relative to the sampling grid.
    auto verifyStrokes = [](const QImage& drawnImage)
    {
        for (int x = 0; x + 1 < drawnImage.width(); x += 2)
        {
            const double firstCoverage = 1.0 - double(qGray(drawnImage.pixel(x, 1))) / 255.0;
            const double secondCoverage = 1.0 - double(qGray(drawnImage.pixel(x + 1, 1))) / 255.0;

            QVERIFY2(qMax(firstCoverage, secondCoverage) >= 0.45,
                     qPrintable(QString("pixel %1: the coverage %2 is too low").arg(x).arg(qMax(firstCoverage, secondCoverage))));
            QVERIFY(qMin(firstCoverage, secondCoverage) < 0.01);
        }
    };

    const QImage drawnImage = drawPage(nullptr, pdf::PDFRenderer::SmoothImages);
    verifyStrokes(drawnImage);
    if (QTest::currentTestFailed())
    {
        return;
    }

    // The cache is only a memoization - the result must not depend on it
    pdf::PDFScaledImageCache cache;

    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);

        QCOMPARE(drawPage(&cache, pdf::PDFRenderer::SmoothImages), drawnImage);

        // The image has been downscaled and the downscaled variant has been memoized
        QCOMPARE(cache.getImageCount(), size_t(1));

        // Drawing the page again in the same pass hits the cache
        QCOMPARE(drawPage(&cache, pdf::PDFRenderer::SmoothImages), drawnImage);
        QCOMPARE(cache.getImageCount(), size_t(1));
    }

    // A bitonal image is downscaled even without the feature SmoothImages - the paint engine
    // would lose its thin strokes otherwise. The image is not compared with the one drawn
    // above, because the painter samples it without the smooth pixmap transformation then.
    cache.clear();

    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);

        verifyStrokes(drawPage(&cache, pdf::PDFRenderer::None));
        QCOMPARE(cache.getImageCount(), size_t(1));
    }
}

void ImageScalingTest::testPrecompiledPageTargets()
{
    // Without the feature SmoothImages only the bitonal images drawn onto a raster target are
    // downscaled. The count of the cached images tells, whether an image has been downscaled.
    const int width = 600;
    const int height = 60;
    const QSize targetSize(width / 3, height / 3);

    auto getDownscaledImageCount = [&](const QImage& image, QPaintDevice* paintDevice)
    {
        pdf::PDFPrecompiledPage page;
        page.addSetWorldMatrix(QTransform());
        page.addImage(image);
        page.finalize(0, { });

        pdf::PDFScaledImageCache cache;
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);

        QPainter painter(paintDevice);
        page.draw(&painter, QRectF(), QTransform::fromScale(targetSize.width(), targetSize.height()),
                  pdf::PDFRenderer::None, 1.0, &cache);
        painter.end();

        return cache.getImageCount();
    };

    const QImage bitonalImage = createBitonalImage(width, height, createStrokeSamples(width, height, 6, 0));

    QImage genericImage(width, height, QImage::Format_RGB32);
    genericImage.fill(Qt::gray);

    QImage rasterTarget(targetSize, QImage::Format_ARGB32_Premultiplied);
    rasterTarget.fill(Qt::white);

    QCOMPARE(getDownscaledImageCount(bitonalImage, &rasterTarget), size_t(1));
    QCOMPARE(getDownscaledImageCount(genericImage, &rasterTarget), size_t(0));

    // The redaction replays the page into a pdf writer of 72 dpi, which stores the image into
    // the redacted document - there the image must be kept at its full resolution
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QPdfWriter pdfWriter(&buffer);
    pdfWriter.setResolution(72);
    QCOMPARE(getDownscaledImageCount(bitonalImage, &pdfWriter), size_t(0));

    QPicture picture;
    QCOMPARE(getDownscaledImageCount(bitonalImage, &picture), size_t(0));
}

/// Returns the bounding rectangle of the pixels, which differ from the background color
static QRect getPaintedBoundingRect(const QImage& image, QRgb backgroundColor)
{
    QRect boundingRect;

    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixel(x, y) != backgroundColor)
            {
                boundingRect = boundingRect.united(QRect(x, y, 1, 1));
            }
        }
    }

    return boundingRect;
}

void ImageScalingTest::testHighDpiGeometry()
{
    // The paint devices of both of the rendering engines carry a device pixel ratio, and the
    // painting must end up in the same real pixels in both of them. This pins down the
    // assumption of the image downscaling - that the device transform of the painter maps
    // to the real pixels of the target.
    const qreal devicePixelRatio = 2.0;
    const QSize physicalSize(200, 100);
    const QSize logicalSize(100, 50);
    const QRect logicalRect(10, 20, 50, 20);
    const QRect expectedRect(20, 40, 100, 40);

    const QRect logicalImageRect(60, 10, 20, 10);
    const QRect expectedImageRect(120, 20, 40, 20);
    const QRect logicalClipRect(10, 5, 10, 5);
    const QRect expectedClipRect(20, 10, 20, 10);

    QImage sourceImage(4, 2, QImage::Format_ARGB32_Premultiplied);
    sourceImage.fill(Qt::black);

    // The paint engine of Blend2D clears the buffer when it begins, so the background must
    // be painted through the painter and not into the image beforehand
    auto paint = [&](QPainter* painter)
    {
        painter->fillRect(QRect(QPoint(0, 0), logicalSize), Qt::white);
        painter->fillRect(logicalRect, Qt::black);

        // An image and a clipped path must land in the real pixels as well
        painter->drawImage(logicalImageRect, sourceImage);

        painter->save();
        painter->setClipRect(logicalClipRect);
        painter->fillRect(QRect(QPoint(0, 0), logicalSize), Qt::black);
        painter->restore();
    };

    QImage rasterBuffer(physicalSize, QImage::Format_ARGB32_Premultiplied);
    rasterBuffer.setDevicePixelRatio(devicePixelRatio);
    rasterBuffer.fill(Qt::white);

    {
        QPainter painter(&rasterBuffer);
        QCOMPARE(painter.deviceTransform().m11(), devicePixelRatio);
        QCOMPARE(painter.deviceTransform().m22(), devicePixelRatio);
        paint(&painter);
    }

    QCOMPARE(getPaintedBoundingRect(rasterBuffer, qRgb(255, 255, 255)), expectedRect.united(expectedImageRect).united(expectedClipRect));
    QCOMPARE(getPaintedBoundingRect(rasterBuffer.copy(expectedImageRect), qRgb(255, 255, 255)), QRect(QPoint(0, 0), expectedImageRect.size()));
    QCOMPARE(getPaintedBoundingRect(rasterBuffer.copy(QRect(0, 0, expectedClipRect.right() + 20, expectedClipRect.bottom() + 1)), qRgb(255, 255, 255)), expectedClipRect);

    QImage blendBuffer(physicalSize, QImage::Format_ARGB32_Premultiplied);
    blendBuffer.setDevicePixelRatio(devicePixelRatio);
    blendBuffer.fill(Qt::white);

    {
        pdf::PDFBLPaintDevice paintDevice(blendBuffer, false);
        QPainter painter;
        QVERIFY(painter.begin(&paintDevice));

        // The Blend2D paint device must report its device pixel ratio to the painter, so the
        // downscaling of the images targets the real pixels there as well
        QCOMPARE(painter.deviceTransform().m11(), devicePixelRatio);
        QCOMPARE(painter.deviceTransform().m22(), devicePixelRatio);

        paint(&painter);
        painter.end();
    }

    QCOMPARE(getPaintedBoundingRect(blendBuffer, qRgb(255, 255, 255)), expectedRect.united(expectedImageRect).united(expectedClipRect));
    QCOMPARE(getPaintedBoundingRect(blendBuffer.copy(expectedImageRect), qRgb(255, 255, 255)), QRect(QPoint(0, 0), expectedImageRect.size()));
    QCOMPARE(getPaintedBoundingRect(blendBuffer.copy(QRect(0, 0, expectedClipRect.right() + 20, expectedClipRect.bottom() + 1)), qRgb(255, 255, 255)), expectedClipRect);
}

void ImageScalingTest::testHighDpiImageResolution()
{
    // An image must be downscaled to the real pixels of the target and not to the logical
    // ones - otherwise a display with a device pixel ratio of two would get a half of the
    // resolution it can show, and the paint device would enlarge the image back.
    const int width = 1200;
    const int height = 120;
    const QSize logicalSize(200, 20);
    const qreal devicePixelRatio = 2.0;

    const QImage image = createBitonalImage(width, height, createStrokeSamples(width, height, 6, 0));

    pdf::PDFPrecompiledPage page;
    page.addSetWorldMatrix(QTransform());
    page.addImage(image);
    page.finalize(0, { });

    auto drawPage = [&](qreal dpr, pdf::PDFScaledImageCache* cache)
    {
        QImage target(logicalSize * dpr, QImage::Format_ARGB32_Premultiplied);
        target.setDevicePixelRatio(dpr);
        target.fill(Qt::white);

        QPainter painter(&target);
        page.draw(&painter, QRectF(), QTransform::fromScale(logicalSize.width(), logicalSize.height()),
                  pdf::PDFRenderer::SmoothImages, 1.0, cache);
        painter.end();

        return target;
    };

    pdf::PDFScaledImageCache cache;

    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&cache);
        const QImage target = drawPage(devicePixelRatio, &cache);

        QCOMPARE(target.size(), QSize(400, 40));
        QCOMPARE(cache.getImageCount(), size_t(1));

        // The image has been downscaled to the real pixels (400 x 40), so a stroke of a
        // single pixel every six pixels is still resolved - at the logical resolution
        // (200 x 20) the shrink factor would be six instead of three and the strokes would
        // be markedly paler
        const qint64 physicalMemory = cache.getMemoryConsumptionEstimate();
        QCOMPARE(physicalMemory, qint64(400) * qint64(40));
    }

    // The very same page drawn without the device pixel ratio uses the logical resolution
    pdf::PDFScaledImageCache logicalCache;

    {
        pdf::PDFScaledImageCache::DrawingPassGuard guard(&logicalCache);
        const QImage target = drawPage(1.0, &logicalCache);

        QCOMPARE(target.size(), logicalSize);
        QCOMPARE(logicalCache.getMemoryConsumptionEstimate(), qint64(200) * qint64(20));
    }
}


QTEST_MAIN(ImageScalingTest)

#include "tst_imagescalingtest.moc"
