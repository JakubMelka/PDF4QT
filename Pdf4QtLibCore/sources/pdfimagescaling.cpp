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
#include "pdfdbgheap.h"

#include <QtMath>

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <vector>

namespace pdf
{

/// Span of the source pixels, which contribute to a single destination pixel of one axis.
/// The first and the last source pixel are covered only partially (by \p firstWeight and
/// \p lastWeight of their area), all the source pixels between them contribute fully.
struct PDFImageScalingSpan
{
    int first = 0;
    int last = 0;
    double firstWeight = 0.0;
    double lastWeight = 0.0;
};

/// Creates the spans of the source pixels for all the destination pixels of one axis. The
/// destination pixel i covers the source interval [i * source / target, (i+1) * source /
/// target), which is evaluated in an exact integer arithmetic, so the resulting indices
/// never need to be clamped. The sum of the weights of a single span is exactly the ratio
/// of the sizes, so the sum of the weights of both of the axes is the area of the source
/// box of a destination pixel.
/// \param sourceSize Size of the source axis
/// \param targetSize Size of the target axis (must not be greater than the source size)
static std::vector<PDFImageScalingSpan> createImageScalingSpans(int sourceSize, int targetSize)
{
    Q_ASSERT(sourceSize > 0 && targetSize > 0 && targetSize <= sourceSize);

    std::vector<PDFImageScalingSpan> spans(static_cast<size_t>(targetSize));

    const qint64 source = sourceSize;
    const qint64 target = targetSize;

    for (int i = 0; i < targetSize; ++i)
    {
        const qint64 begin = qint64(i) * source;
        const qint64 end = qint64(i + 1) * source;

        // The integer division is an exact floor, both of the operands are positive
        const qint64 first = begin / target;
        const qint64 last = (end + target - 1) / target - 1;

        Q_ASSERT(first <= last && last < source);

        PDFImageScalingSpan& span = spans[size_t(i)];
        span.first = static_cast<int>(first);
        span.last = static_cast<int>(last);

        if (first == last)
        {
            // The whole destination pixel lies inside a single source pixel
            span.firstWeight = double(end - begin) / double(target);
            span.lastWeight = span.firstWeight;
        }
        else
        {
            span.firstWeight = double((first + 1) * target - begin) / double(target);
            span.lastWeight = double(end - last * target) / double(target);
        }
    }

    return spans;
}

/// Returns the weight of the source pixel in the span
static double getImageScalingSpanWeight(const PDFImageScalingSpan& span, int index)
{
    if (index == span.first)
    {
        return span.firstWeight;
    }

    if (index == span.last)
    {
        return span.lastWeight;
    }

    return 1.0;
}

/// Reduces a single source row of the ink indicators to the destination width. The output is
/// the ink area of the destination pixels in the units of the source pixels.
static void reduceImageScalingRow(const uint8_t* line, const std::vector<PDFImageScalingSpan>& spans, double* output)
{
    const size_t targetWidth = spans.size();

    for (size_t i = 0; i < targetWidth; ++i)
    {
        const PDFImageScalingSpan& span = spans[i];

        if (span.first == span.last)
        {
            output[i] = double(line[span.first]) * span.firstWeight;
            continue;
        }

        double sum = double(line[span.first]) * span.firstWeight + double(line[span.last]) * span.lastWeight;
        for (int x = span.first + 1; x < span.last; ++x)
        {
            sum += double(line[x]);
        }

        output[i] = sum;
    }
}

/// Creates the table, which unpacks a single byte of a monochromatic image into eight ink
/// indicators. \p isMostSignificantBitFirst distinguishes Format_Mono from Format_MonoLSB,
/// \p inkSample is the sample value, which is considered to be the ink. The function is
/// immediate, so the tables below are built by the compiler and no code is generated for it.
static consteval std::array<std::array<uint8_t, 8>, 256> createImageScalingBitTable(bool isMostSignificantBitFirst, int inkSample)
{
    std::array<std::array<uint8_t, 8>, 256> table = { };

    for (int byteValue = 0; byteValue < 256; ++byteValue)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            const int shift = isMostSignificantBitFirst ? (7 - bit) : bit;
            const int sample = (byteValue >> shift) & 1;
            table[size_t(byteValue)][size_t(bit)] = (sample == inkSample) ? uint8_t(1) : uint8_t(0);
        }
    }

    return table;
}

static constexpr std::array<std::array<uint8_t, 8>, 256> IMAGE_SCALING_BIT_TABLE_MSB_INK_0 = createImageScalingBitTable(true, 0);
static constexpr std::array<std::array<uint8_t, 8>, 256> IMAGE_SCALING_BIT_TABLE_MSB_INK_1 = createImageScalingBitTable(true, 1);
static constexpr std::array<std::array<uint8_t, 8>, 256> IMAGE_SCALING_BIT_TABLE_LSB_INK_0 = createImageScalingBitTable(false, 0);
static constexpr std::array<std::array<uint8_t, 8>, 256> IMAGE_SCALING_BIT_TABLE_LSB_INK_1 = createImageScalingBitTable(false, 1);

/// Returns the table, which unpacks a byte of a monochromatic image of the given bit order
/// into the indicators of the given ink sample value
static const std::array<std::array<uint8_t, 8>, 256>& getImageScalingBitTable(bool isMostSignificantBitFirst, int inkSample)
{
    if (isMostSignificantBitFirst)
    {
        return (inkSample == 0) ? IMAGE_SCALING_BIT_TABLE_MSB_INK_0 : IMAGE_SCALING_BIT_TABLE_MSB_INK_1;
    }

    return (inkSample == 0) ? IMAGE_SCALING_BIT_TABLE_LSB_INK_0 : IMAGE_SCALING_BIT_TABLE_LSB_INK_1;
}

/// Unpacks a single row of a monochromatic image into the ink indicators (0 or 1)
static void unpackImageScalingMonochromeRow(const uchar* scanLine,
                                            int width,
                                            const std::array<std::array<uint8_t, 8>, 256>& table,
                                            uint8_t* output)
{
    const int fullByteCount = width / 8;
    const int remainingBitCount = width % 8;

    for (int i = 0; i < fullByteCount; ++i)
    {
        std::memcpy(output + ptrdiff_t(i) * 8, table[scanLine[i]].data(), 8);
    }

    if (remainingBitCount > 0)
    {
        std::memcpy(output + ptrdiff_t(fullByteCount) * 8, table[scanLine[fullByteCount]].data(), size_t(remainingBitCount));
    }
}

/// Counts the samples of the value one in a monochromatic image. The padding bits at the end
/// of the rows are masked out, so they are not counted.
static qint64 countImageScalingSetSamples(const QImage& image, bool isMostSignificantBitFirst)
{
    const int width = image.width();
    const int height = image.height();
    const int fullByteCount = width / 8;
    const int remainingBitCount = width % 8;

    uchar mask = 0;
    if (remainingBitCount > 0)
    {
        const int value = isMostSignificantBitFirst ? (0xFF << (8 - remainingBitCount)) : (0xFF >> (8 - remainingBitCount));
        mask = static_cast<uchar>(value & 0xFF);
    }

    qint64 count = 0;

    for (int y = 0; y < height; ++y)
    {
        const uchar* scanLine = image.constScanLine(y);

        for (int i = 0; i < fullByteCount; ++i)
        {
            count += std::popcount(static_cast<unsigned char>(scanLine[i]));
        }

        if (remainingBitCount > 0)
        {
            count += std::popcount(static_cast<unsigned char>(scanLine[fullByteCount] & mask));
        }
    }

    return count;
}

/// Counts the opaque pixels of a stencil mask and returns the color of the first of them in
/// \p opaqueColor (which is left intact, when the mask is fully transparent).
static qint64 countImageScalingOpaquePixels(const QImage& image, QRgb& opaqueColor)
{
    qint64 count = 0;
    bool hasOpaqueColor = false;

    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(y));

        for (int x = 0; x < image.width(); ++x)
        {
            if (qAlpha(scanLine[x]) == 255)
            {
                ++count;

                if (!hasOpaqueColor)
                {
                    opaqueColor = scanLine[x];
                    hasOpaqueColor = true;
                }
            }
        }
    }

    return count;
}

/// Returns true, if the color is a neutral (gray) one
static bool isImageScalingNeutralColor(QRgb color)
{
    return qRed(color) == qGreen(color) && qGreen(color) == qBlue(color);
}

/// Interpolates between the two channel values using the given factor from the range <0, 1>.
/// The result is always a valid channel value, so it needs no clamping.
static int interpolateImageScalingChannel(int from, int to, double factor)
{
    Q_ASSERT(factor >= 0.0 && factor <= 1.0);

    const int result = qRound(double(from) + (double(to) - double(from)) * factor);
    Q_ASSERT(result >= 0 && result <= 255);

    return result;
}

PDFImageScaling::ImageType PDFImageScaling::getImageType(const QImage& image)
{
    if (image.isNull())
    {
        return ImageType::Generic;
    }

    switch (image.format())
    {
        case QImage::Format_Mono:
        case QImage::Format_MonoLSB:
        {
            const QList<QRgb> colorTable = image.colorTable();

            if (colorTable.size() != 2 ||
                colorTable[0] == colorTable[1] ||
                qAlpha(colorTable[0]) != 255 ||
                qAlpha(colorTable[1]) != 255)
            {
                return ImageType::Generic;
            }

            return ImageType::Monochrome;
        }

        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied:
        {
            // Jakub Melka: a stencil mask (an image with the entry ImageMask in its
            // dictionary) is decoded into the alpha channel and then it is colorized by the
            // current filling color, so it never becomes a monochromatic image. It is still
            // a bitonal image - the coverage is carried by the binary alpha channel instead
            // of by the samples. Detect it by the fact, that each pixel is either fully
            // transparent, or it has one and the same opaque color.
            bool hasOpaquePixel = false;
            bool hasTransparentPixel = false;
            QRgb opaqueColor = 0;

            for (int y = 0; y < image.height(); ++y)
            {
                const QRgb* scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(y));

                for (int x = 0; x < image.width(); ++x)
                {
                    const QRgb pixel = scanLine[x];
                    const int alpha = qAlpha(pixel);

                    if (alpha == 0)
                    {
                        hasTransparentPixel = true;
                        continue;
                    }

                    if (alpha != 255)
                    {
                        return ImageType::Generic;
                    }

                    if (!hasOpaquePixel)
                    {
                        hasOpaquePixel = true;
                        opaqueColor = pixel;
                    }
                    else if (pixel != opaqueColor)
                    {
                        return ImageType::Generic;
                    }
                }
            }

            return (hasOpaquePixel && hasTransparentPixel) ? ImageType::StencilMask : ImageType::Generic;
        }

        default:
            break;
    }

    return ImageType::Generic;
}

QSize PDFImageScaling::getDownscaledSize(QSize imageSize, const QTransform& worldTransform)
{
    if (imageSize.isEmpty() || !worldTransform.isAffine())
    {
        return QSize();
    }

    // Vectors, to which the unit vectors of the image are mapped by the world transform
    const qreal mappedWidth = std::hypot(worldTransform.m11(), worldTransform.m12());
    const qreal mappedHeight = std::hypot(worldTransform.m21(), worldTransform.m22());

    if (!(mappedWidth > 0.0) || !(mappedHeight > 0.0))
    {
        return QSize();
    }

    // Jakub Melka: we can downscale the image only, if the mapped image is a rectangle, i.e.
    // the mapped vectors are orthogonal. Test it by their dot product, relatively to their
    // lengths - a matrix, which is a product of several matrices, is never exactly
    // orthogonal. Mirrored transformations are accepted, only skewed ones are rejected.
    const qreal dotProduct = worldTransform.m11() * worldTransform.m21() + worldTransform.m12() * worldTransform.m22();
    if (std::abs(dotProduct) > ORTHOGONALITY_TOLERANCE * mappedWidth * mappedHeight)
    {
        return QSize();
    }

    const qint64 newWidth = qMax<qint64>(1, static_cast<qint64>(mappedWidth));
    const qint64 newHeight = qMax<qint64>(1, static_cast<qint64>(mappedHeight));

    // Is the image being enlarged? Then we do not touch it at all.
    if (newWidth * newHeight >= qint64(imageSize.width()) * qint64(imageSize.height()))
    {
        return QSize();
    }

    // Jakub Melka: the aspect ratio is deliberately not preserved. The world transform is
    // rebuilt from the size of the downscaled image, so the image always fills the same
    // device rectangle; preserving the aspect ratio would just throw away the resolution
    // of an anisotropically scaled image.
    const QSize targetSize(static_cast<int>(qMin<qint64>(newWidth, imageSize.width())),
                           static_cast<int>(qMin<qint64>(newHeight, imageSize.height())));

    // The target size cannot be the size of the image - that would require both of the
    // mapped sizes to be at least the size of the image, and the test above has already
    // rejected such a transformation
    Q_ASSERT(targetSize != imageSize);

    return targetSize;
}

QImage PDFImageScaling::scaleDown(const QImage& image, QSize targetSize, ImageType imageType)
{
    if (image.isNull() || targetSize.isEmpty())
    {
        return QImage();
    }

    if (isBitonal(imageType))
    {
        QImage scaledImage = scaleDownBitonal(image, targetSize, imageType);
        if (!scaledImage.isNull())
        {
            return scaledImage;
        }

        // The image is not a bitonal one after all - fall back to the box filter of Qt
    }

    return image.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage PDFImageScaling::scaleDownBitonal(const QImage& image,
                                         QSize targetSize,
                                         ImageType imageType,
                                         PDFReal inkGamma)
{
    const int sourceWidth = image.width();
    const int sourceHeight = image.height();
    const int targetWidth = targetSize.width();
    const int targetHeight = targetSize.height();

    if (targetWidth <= 0 || targetHeight <= 0 || targetWidth > sourceWidth || targetHeight > sourceHeight)
    {
        return QImage();
    }

    // Jakub Melka: verify only the properties, which are cheap to verify. Scanning all the
    // pixels again (as getImageType does for the stencil masks) would double the cost of the
    // scaling; the type of the image is determined once, when the image is created.
    const bool isMonochrome = (imageType == ImageType::Monochrome);
    const bool isMostSignificantBitFirst = (image.format() == QImage::Format_Mono);

    QRgb inkColor = qRgb(0, 0, 0);
    QRgb paperColor = qRgb(255, 255, 255);
    int inkSample = 1;
    qint64 inkSampleCount = 0;

    if (isMonochrome)
    {
        const QList<QRgb> colorTable = image.colorTable();

        if ((!isMostSignificantBitFirst && image.format() != QImage::Format_MonoLSB) ||
            colorTable.size() != 2 ||
            colorTable[0] == colorTable[1] ||
            qAlpha(colorTable[0]) != 255 ||
            qAlpha(colorTable[1]) != 255)
        {
            return QImage();
        }

        const qint64 setSampleCount = countImageScalingSetSamples(image, isMostSignificantBitFirst);
        const qint64 totalSampleCount = qint64(sourceWidth) * qint64(sourceHeight);

        // Jakub Melka: the ink is the minority sample value, not the darker color of the
        // color table. The inverted colors mode inverts the samples of the image and it
        // leaves the color table intact, so in the dark mode the glyphs are drawn by the
        // lighter color; treating the darker color as the ink would erode them.
        inkSample = (2 * setSampleCount <= totalSampleCount) ? 1 : 0;
        inkSampleCount = (inkSample == 1) ? setSampleCount : totalSampleCount - setSampleCount;
        inkColor = colorTable[inkSample];
        paperColor = colorTable[1 - inkSample];
    }
    else if (imageType != ImageType::StencilMask ||
             (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_ARGB32_Premultiplied))
    {
        return QImage();
    }
    else
    {
        // The ink of a stencil mask is always its opaque part
        inkSampleCount = countImageScalingOpaquePixels(image, inkColor);
    }

    const double scaleX = double(sourceWidth) / double(targetWidth);
    const double scaleY = double(sourceHeight) / double(targetHeight);
    const double sourceBoxArea = scaleX * scaleY;
    const double shrinkFactor = qMax(scaleX, scaleY);
    const double inkFraction = double(inkSampleCount) / (double(sourceWidth) * double(sourceHeight));

    // Apply the stem darkening curve only to an ink on a paper, and only to an image, which
    // is actually being shrunk - at the shrink factor 1.0 the curve is an identity and it
    // reaches its full strength at the shrink factor 2.0.
    double gamma = 1.0;
    if (inkFraction <= MAXIMUM_INK_FRACTION_FOR_DARKENING)
    {
        gamma = 1.0 - (1.0 - inkGamma) * qBound(0.0, shrinkFactor - 1.0, 1.0);
    }

    const bool isGrayscaleOutput = isMonochrome && isImageScalingNeutralColor(inkColor) && isImageScalingNeutralColor(paperColor);
    const QImage::Format outputFormat = isMonochrome ? (isGrayscaleOutput ? QImage::Format_Grayscale8 : QImage::Format_RGB32)
                                                     : QImage::Format_ARGB32_Premultiplied;

    QImage result(targetSize, outputFormat);
    if (result.isNull())
    {
        // The image cannot be allocated
        return QImage();
    }

    // Lookup table of the coverage transfer curve, which converts the ink coverage of a
    // destination pixel to its color
    std::array<uint8_t, COVERAGE_LUT_SIZE> grayscaleLookupTable = { };
    std::array<QRgb, COVERAGE_LUT_SIZE> colorLookupTable = { };

    for (int i = 0; i < COVERAGE_LUT_SIZE; ++i)
    {
        const double coverage = double(i) / double(COVERAGE_LUT_SIZE - 1);
        const double transferredCoverage = (gamma != 1.0) ? std::pow(coverage, gamma) : coverage;

        if (isGrayscaleOutput)
        {
            grayscaleLookupTable[size_t(i)] = static_cast<uint8_t>(interpolateImageScalingChannel(qRed(paperColor), qRed(inkColor), transferredCoverage));
        }
        else if (isMonochrome)
        {
            colorLookupTable[size_t(i)] = qRgb(interpolateImageScalingChannel(qRed(paperColor), qRed(inkColor), transferredCoverage),
                                               interpolateImageScalingChannel(qGreen(paperColor), qGreen(inkColor), transferredCoverage),
                                               interpolateImageScalingChannel(qBlue(paperColor), qBlue(inkColor), transferredCoverage));
        }
        else
        {
            // The output of a stencil mask is premultiplied, so all of the channels are
            // multiplied by the coverage
            colorLookupTable[size_t(i)] = qRgba(interpolateImageScalingChannel(0, qRed(inkColor), transferredCoverage),
                                                interpolateImageScalingChannel(0, qGreen(inkColor), transferredCoverage),
                                                interpolateImageScalingChannel(0, qBlue(inkColor), transferredCoverage),
                                                interpolateImageScalingChannel(0, 255, transferredCoverage));
        }
    }

    const std::vector<PDFImageScalingSpan> columnSpans = createImageScalingSpans(sourceWidth, targetWidth);
    const std::vector<PDFImageScalingSpan> rowSpans = createImageScalingSpans(sourceHeight, targetHeight);

    std::vector<uint8_t> sourceLine(size_t(sourceWidth), uint8_t(0));
    std::vector<double> reducedRow(size_t(targetWidth), 0.0);
    std::vector<double> accumulator(size_t(targetWidth), 0.0);

    const std::array<std::array<uint8_t, 8>, 256>& bitTable = getImageScalingBitTable(isMostSignificantBitFirst, inkSample);

    // The last source row, which has been unpacked and reduced. A source row at the boundary
    // belongs to two destination rows and would be reduced twice otherwise.
    int cachedSourceRow = -1;

    for (int targetRow = 0; targetRow < targetHeight; ++targetRow)
    {
        std::fill(accumulator.begin(), accumulator.end(), 0.0);

        const PDFImageScalingSpan& rowSpan = rowSpans[size_t(targetRow)];
        for (int sourceRow = rowSpan.first; sourceRow <= rowSpan.last; ++sourceRow)
        {
            if (sourceRow != cachedSourceRow)
            {
                if (isMonochrome)
                {
                    unpackImageScalingMonochromeRow(image.constScanLine(sourceRow), sourceWidth, bitTable, sourceLine.data());
                }
                else
                {
                    const QRgb* scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(sourceRow));
                    for (int x = 0; x < sourceWidth; ++x)
                    {
                        sourceLine[size_t(x)] = (qAlpha(scanLine[x]) == 255) ? uint8_t(1) : uint8_t(0);
                    }
                }

                reduceImageScalingRow(sourceLine.data(), columnSpans, reducedRow.data());
                cachedSourceRow = sourceRow;
            }

            const double rowWeight = getImageScalingSpanWeight(rowSpan, sourceRow);
            for (int targetColumn = 0; targetColumn < targetWidth; ++targetColumn)
            {
                accumulator[size_t(targetColumn)] += rowWeight * reducedRow[size_t(targetColumn)];
            }
        }

        uchar* outputScanLine = result.scanLine(targetRow);
        QRgb* outputColorScanLine = reinterpret_cast<QRgb*>(outputScanLine);

        for (int targetColumn = 0; targetColumn < targetWidth; ++targetColumn)
        {
            // Jakub Melka: the accumulator is a sum of the nonnegative products of the
            // weights, and the sum of all of the weights of a destination pixel is exactly
            // the area of its source box, so the coverage lies in the interval <0, 1>. The
            // accumulation is done in the double precision, so the rounding error of the
            // summation cannot exceed the half of a step of the lookup table (which would
            // need a relative error of 1/2048, while the error of a summation of n terms
            // is bounded by n times the machine epsilon, i.e. by 2^-22 even for an image
            // of the maximal possible size). The index therefore needs no clamping.
            const double coverage = accumulator[size_t(targetColumn)] / sourceBoxArea;
            const int index = static_cast<int>(coverage * double(COVERAGE_LUT_SIZE - 1) + 0.5);
            Q_ASSERT(index >= 0 && index < COVERAGE_LUT_SIZE);

            if (isGrayscaleOutput)
            {
                outputScanLine[targetColumn] = grayscaleLookupTable[size_t(index)];
            }
            else
            {
                outputColorScanLine[targetColumn] = colorLookupTable[size_t(index)];
            }
        }
    }

    return result;
}

void PDFScaledImageCache::beginDrawingPass()
{
    if (m_passDepth++ == 0)
    {
        ++m_currentPassId;
    }
}

void PDFScaledImageCache::endDrawingPass()
{
    Q_ASSERT(m_passDepth > 0);

    if (--m_passDepth > 0)
    {
        return;
    }

    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (m_currentPassId - it->second.lastUsedPassId > MAXIMUM_PASS_AGE)
        {
            m_memoryConsumptionEstimate -= it->second.memoryConsumption;
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

QImage PDFScaledImageCache::getScaledImage(const QImage& image, QSize targetSize, PDFImageScaling::ImageType imageType)
{
    const bool isCacheable = m_passDepth > 0 &&
                             qint64(image.width()) * qint64(image.height()) >= qint64(MINIMUM_SOURCE_PIXELS);

    if (!isCacheable)
    {
        return PDFImageScaling::scaleDown(image, targetSize, imageType);
    }

    Key key;
    key.imageCacheKey = image.cacheKey();
    key.width = targetSize.width();
    key.height = targetSize.height();

    auto it = m_entries.find(key);
    if (it != m_entries.cend())
    {
        it->second.lastUsedPassId = m_currentPassId;
        return it->second.image;
    }

    QImage scaledImage = PDFImageScaling::scaleDown(image, targetSize, imageType);
    if (scaledImage.isNull())
    {
        return scaledImage;
    }

    Entry entry;
    entry.image = scaledImage;
    entry.lastUsedPassId = m_currentPassId;
    entry.memoryConsumption = scaledImage.sizeInBytes();

    m_memoryConsumptionEstimate += entry.memoryConsumption;
    m_entries[key] = std::move(entry);

    return scaledImage;
}

void PDFScaledImageCache::clear()
{
    m_entries.clear();
    m_memoryConsumptionEstimate = 0;
}

}   // namespace pdf
