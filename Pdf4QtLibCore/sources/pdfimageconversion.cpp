#include "pdfimageconversion.h"
#include "pdfdbgheap.h"

#include <cmath>
#include <array>
#include <vector>

namespace pdf
{

PDFImageConversion::PDFImageConversion()
{

}

void PDFImageConversion::setImage(QImage image)
{
    m_image = std::move(image);
    m_convertedImage = QImage();
    m_convertedAlphaMask = QImage();
    m_automaticThreshold = DEFAULT_THRESHOLD;
    m_width = 0;
    m_height = 0;
    m_lightness.clear();
    m_gray.clear();
    m_opacity.clear();
}

void PDFImageConversion::setConversionMethod(ConversionMethod method)
{
    m_conversionMethod = method;
}

void PDFImageConversion::setThreshold(int threshold)
{
    m_manualThreshold = threshold;
}

void PDFImageConversion::setAlphaMode(AlphaMode alphaMode)
{
    m_alphaMode = alphaMode;
}

void PDFImageConversion::prepareSourceImage()
{
    m_width = 0;
    m_height = 0;
    m_lightness.clear();
    m_gray.clear();
    m_opacity.clear();
    m_convertedAlphaMask = QImage();

    if (m_image.isNull())
    {
        return;
    }

    const QImage source = m_image.convertToFormat(QImage::Format_ARGB32);
    if (source.isNull())
    {
        return;
    }

    m_width = source.width();
    m_height = source.height();

    const size_t pixelCount = size_t(m_width) * size_t(m_height);
    m_lightness.resize(pixelCount);
    m_gray.resize(pixelCount);

    // Alpha channel is processed only when it is not being ignored. Images without
    // the alpha channel are always fully opaque, so no opacity buffer is needed.
    const bool isAlphaProcessed = m_alphaMode == AlphaMode::Composite && source.hasAlphaChannel();
    std::vector<unsigned char> opacity;

    if (isAlphaProcessed)
    {
        opacity.resize(pixelCount, 255);
    }

    bool hasTransparentPixel = false;

    for (int y = 0; y < m_height; ++y)
    {
        const QRgb* sourceLine = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        unsigned char* lightnessLine = m_lightness.data() + size_t(y) * size_t(m_width);
        unsigned char* grayLine = m_gray.data() + size_t(y) * size_t(m_width);
        unsigned char* opacityLine = isAlphaProcessed ? opacity.data() + size_t(y) * size_t(m_width) : nullptr;

        for (int x = 0; x < m_width; ++x)
        {
            const QRgb pixel = sourceLine[x];

            int red = qRed(pixel);
            int green = qGreen(pixel);
            int blue = qBlue(pixel);

            if (isAlphaProcessed)
            {
                const int alpha = qAlpha(pixel);

                if (alpha != 255)
                {
                    // Composite the pixel onto the white background. Without this step,
                    // color values of the fully transparent pixels (which are often just
                    // an uninitialized garbage, or a leftover of a lossy compression)
                    // would be thresholded as if they were a real image content.
                    red = (red * alpha + 255 * (255 - alpha)) / 255;
                    green = (green * alpha + 255 * (255 - alpha)) / 255;
                    blue = (blue * alpha + 255 * (255 - alpha)) / 255;
                }

                opacityLine[x] = static_cast<unsigned char>(alpha);

                if (alpha < OPACITY_THRESHOLD)
                {
                    hasTransparentPixel = true;
                }
            }

            // Lightness of the HSL color model, i.e. the same value, which is
            // returned by the function QColor::lightness. It is used by the global
            // thresholding, which has always worked with this representation.
            const int maximum = qMax(red, qMax(green, blue));
            const int minimum = qMin(red, qMin(green, blue));
            lightnessLine[x] = static_cast<unsigned char>((maximum + minimum + 1) / 2);

            // Luminance, i.e. the value, which the conversion to the format
            // QImage::Format_Grayscale8 produces. Adaptive thresholding and dithering
            // have always worked with the grayscale representation of the image and
            // for saturated colors it differs substantially from the lightness.
            grayLine[x] = static_cast<unsigned char>(qGray(red, green, blue));
        }
    }

    if (!hasTransparentPixel)
    {
        // Image is fully opaque - we do not need the opacity buffer at all
        return;
    }

    m_opacity = std::move(opacity);

    // Create the transparency mask, so the caller can preserve the transparency
    // of the source image (for example as a soft mask of the converted image).
    m_convertedAlphaMask = createBitonalImage();

    for (int y = 0; y < m_height; ++y)
    {
        const size_t rowIndex = size_t(y) * size_t(m_width);

        for (int x = 0; x < m_width; ++x)
        {
            if (!isTransparent(rowIndex + size_t(x)))
            {
                setWhiteSample(m_convertedAlphaMask, x, y);
            }
        }
    }
}

bool PDFImageConversion::convert()
{
    m_convertedImage = QImage();

    if (m_image.isNull())
    {
        return false;
    }

    prepareSourceImage();

    if (m_lightness.empty())
    {
        return false;
    }

    switch (m_conversionMethod)
    {
        case pdf::PDFImageConversion::ConversionMethod::Automatic:
            m_automaticThreshold = calculateOtsu1DThreshold();
            m_convertedImage = convertThresholded(m_automaticThreshold);
            break;

        case pdf::PDFImageConversion::ConversionMethod::Manual:
            m_convertedImage = convertThresholded(m_manualThreshold);
            break;

        case pdf::PDFImageConversion::ConversionMethod::Adaptive:
            m_automaticThreshold = calculateOtsu1DThreshold();
            m_convertedImage = convertAdaptive();
            break;

        case pdf::PDFImageConversion::ConversionMethod::Dither:
        {
            int threshold = DEFAULT_THRESHOLD;

            if (m_manualThreshold != DEFAULT_THRESHOLD)
            {
                threshold = m_manualThreshold;
            }
            else
            {
                m_automaticThreshold = calculateOtsu1DThreshold();
                threshold = m_automaticThreshold;
            }

            m_convertedImage = convertDithered(threshold);
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    if (m_convertedImage.isNull())
    {
        m_convertedAlphaMask = QImage();
        return false;
    }

    return true;
}

int PDFImageConversion::getThreshold() const
{
    switch (m_conversionMethod)
    {
        case pdf::PDFImageConversion::ConversionMethod::Automatic:
        case pdf::PDFImageConversion::ConversionMethod::Adaptive:
            return m_automaticThreshold;

        case pdf::PDFImageConversion::ConversionMethod::Manual:
        case pdf::PDFImageConversion::ConversionMethod::Dither:
            return m_manualThreshold;

        default:
            Q_ASSERT(false);
            break;
    }

    return DEFAULT_THRESHOLD;
}

QImage PDFImageConversion::getConvertedImage() const
{
    return m_convertedImage;
}

QImage PDFImageConversion::getConvertedAlphaMask() const
{
    return m_convertedAlphaMask;
}

QImage PDFImageConversion::createAlphaMask(const QImage& image)
{
    if (image.isNull() || !image.hasAlphaChannel())
    {
        return QImage();
    }

    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    if (source.isNull())
    {
        return QImage();
    }

    const int width = source.width();
    const int height = source.height();

    QImage mask(width, height, QImage::Format_Mono);
    mask.fill(0);

    bool hasTransparentPixel = false;

    // The mask must be built with exactly the same opacity threshold, which the
    // conversion uses, otherwise the mask of a converted image and the mask of a
    // filled image would not agree on the border pixels. \sa prepareSourceImage
    for (int y = 0; y < height; ++y)
    {
        const QRgb* sourceLine = reinterpret_cast<const QRgb*>(source.constScanLine(y));

        for (int x = 0; x < width; ++x)
        {
            if (qAlpha(sourceLine[x]) >= OPACITY_THRESHOLD)
            {
                setWhiteSample(mask, x, y);
            }
            else
            {
                hasTransparentPixel = true;
            }
        }
    }

    if (!hasTransparentPixel)
    {
        // Image is fully opaque, so it does not need a mask at all
        return QImage();
    }

    return mask;
}

QImage PDFImageConversion::createBitonalImage() const
{
    QImage image(m_width, m_height, QImage::Format_Mono);
    image.fill(0);
    return image;
}

void PDFImageConversion::setWhiteSample(QImage& image, int x, int y)
{
    // Format_Mono stores the samples with the most significant bit first,
    // and the default color table maps the sample value 1 to the white color.
    uchar* line = image.scanLine(y);
    line[x >> 3] |= uchar(0x80 >> (x & 7));
}

int PDFImageConversion::calculateOtsu1DThreshold() const
{
    if (m_lightness.empty())
    {
        return DEFAULT_THRESHOLD;
    }

    // Histogram of lightness occurences. Transparent pixels are already composited
    // onto the white background, so they enter the histogram as a blank paper - which
    // is exactly, what they represent. Skipping them instead would leave a foreground
    // layer of a scanned page with a histogram of the ink color only, from which no
    // meaningful threshold can be calculated.
    std::array<int, 256> histogram = { };

    for (size_t i = 0; i < m_lightness.size(); ++i)
    {
        histogram[m_lightness[i]] += 1;
    }

    float factor = 1.0f / float(m_lightness.size());

    std::array<float, 256> normalizedHistogram = { };
    std::array<float, 256> cumulativeProbabilities = { };
    std::array<float, 256> interClassVariance = { };

    // Compute probabilities
    for (size_t i = 0; i < histogram.size(); ++i)
    {
        normalizedHistogram[i] = histogram[i] * factor;
        cumulativeProbabilities[i] = normalizedHistogram[i];

        if (i > 0)
        {
            cumulativeProbabilities[i] += cumulativeProbabilities[i - 1];
        }
    }

    // Calculate the inter-class variance for each threshold. Variables
    // with the subscript 0 denote the background, while those with
    // subscript 1 denote the foreground.
    for (size_t i = 0; i < histogram.size(); ++i)
    {
        const float w0 = cumulativeProbabilities[i] - normalizedHistogram[i];
        const float w1 = 1.0f - w0;

        float u0 = 0.0f;
        float u1 = 0.0f;

        // Calculate mean intensity value of the background.
        if (!qFuzzyIsNull(w0))
        {
            for (size_t j = 0; j < i; ++j)
            {
                u0 += j * normalizedHistogram[j];
            }

            u0 /= w0;
        }

        // Calculate mean intensity value of the foreground.
        if (!qFuzzyIsNull(w1))
        {
            for (size_t j = i; j < histogram.size(); ++j)
            {
                u1 += j * normalizedHistogram[j];
            }

            u1 /= w1;
        }

        const float variance = w0 * w1 * std::pow(u0 - u1, 2);
        interClassVariance[i] = variance;
    }

    // Find maximal value of the variance
    size_t maxVarianceIndex = 0;
    float maxVarianceValue = 0.0f;

    for (size_t i = 0; i < interClassVariance.size(); ++i)
    {
        if (interClassVariance[i] > maxVarianceValue)
        {
            maxVarianceValue = interClassVariance[i];
            maxVarianceIndex = i;
        }
    }

    return int(maxVarianceIndex);
}

QImage PDFImageConversion::convertThresholded(int threshold) const
{
    if (m_lightness.empty())
    {
        return QImage();
    }

    QImage bitonal = createBitonalImage();

    for (int y = 0; y < m_height; ++y)
    {
        const size_t rowIndex = size_t(y) * size_t(m_width);

        for (int x = 0; x < m_width; ++x)
        {
            const size_t index = rowIndex + size_t(x);

            // Transparent pixels are not painted at all, so they are converted
            // to the white color to not create an artificial black background.
            const bool isWhite = isTransparent(index) || m_lightness[index] >= threshold;

            if (isWhite)
            {
                setWhiteSample(bitonal, x, y);
            }
        }
    }

    return bitonal;
}

QImage PDFImageConversion::convertAdaptive() const
{
    if (m_gray.empty())
    {
        return QImage();
    }

    const int width = m_width;
    const int height = m_height;
    const int radius = ADAPTIVE_WINDOW_RADIUS;

    QImage bitonal = createBitonalImage();

    // For every column we keep the sum of the lightness values of the pixels lying
    // in the vertical part of the local window. Moving to the next row means adding
    // one row into these sums and removing another one, so the sum of the local
    // window can then be computed by moving a horizontal window over the column
    // sums. Compared to an integral image of the whole picture, this needs only
    // a memory proportional to the width of the image and it cannot overflow.
    std::vector<int> columnSum(size_t(width), 0);

    auto updateColumns = [&](int row, int sign)
    {
        if (row < 0 || row >= height)
        {
            return;
        }

        const size_t rowIndex = size_t(row) * size_t(width);

        for (int x = 0; x < width; ++x)
        {
            columnSum[size_t(x)] += sign * int(m_gray[rowIndex + size_t(x)]);
        }
    };

    for (int row = 0; row <= radius; ++row)
    {
        updateColumns(row, 1);
    }

    // Adaptive thresholding does not use one global threshold for the whole image.
    // Instead, each pixel gets its own threshold derived from the average brightness
    // in its local neighborhood. That makes the result more robust when one part of
    // the page is bright and another is darker because of shadows or uneven scanning.
    for (int y = 0; y < height; ++y)
    {
        if (y > 0)
        {
            updateColumns(y + radius, 1);
            updateColumns(y - radius - 1, -1);
        }

        const int windowHeight = qMin(height - 1, y + radius) - qMax(0, y - radius) + 1;

        int windowSum = 0;
        for (int x = 0; x <= radius && x < width; ++x)
        {
            windowSum += columnSum[size_t(x)];
        }

        const size_t rowIndex = size_t(y) * size_t(width);

        for (int x = 0; x < width; ++x)
        {
            if (x > 0)
            {
                const int addedColumn = x + radius;
                const int removedColumn = x - radius - 1;

                if (addedColumn < width)
                {
                    windowSum += columnSum[size_t(addedColumn)];
                }

                if (removedColumn >= 0)
                {
                    windowSum -= columnSum[size_t(removedColumn)];
                }
            }

            const size_t index = rowIndex + size_t(x);

            if (isTransparent(index))
            {
                setWhiteSample(bitonal, x, y);
                continue;
            }

            const int windowWidth = qMin(width - 1, x + radius) - qMax(0, x - radius) + 1;
            const int count = windowWidth * windowHeight;
            const int mean = count > 0 ? (windowSum / count) : 0;

            // We shift the threshold slightly below the neighborhood average.
            // Without this offset, faint dark strokes could disappear too easily
            // when the local average is already influenced by a bright background.
            const int threshold = mean - ADAPTIVE_OFFSET;

            // Pixels brighter than the local threshold become white, the others black.
            if (m_gray[index] >= threshold)
            {
                setWhiteSample(bitonal, x, y);
            }
        }
    }

    return bitonal;
}

QImage PDFImageConversion::convertDithered(int threshold) const
{
    if (m_gray.empty())
    {
        return QImage();
    }

    const int width = m_width;
    const int height = m_height;

    // Error-diffusion dithering intentionally keeps a working buffer in grayscale.
    // Each pixel is quantized to pure black or white, and the quantization error
    // is pushed into neighboring pixels that have not been processed yet.
    // Visually this replaces missing gray levels with a fine black/white pattern.
    std::vector<float> buffer(m_gray.begin(), m_gray.end());

    QImage bitonal = createBitonalImage();

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t index = size_t(y) * size_t(width) + size_t(x);

            if (isTransparent(index))
            {
                // Transparent pixels are not a part of the image content. They are
                // converted to the white color and they do not diffuse any error,
                // so the dithering pattern does not leak into the transparent areas.
                setWhiteSample(bitonal, x, y);
                continue;
            }

            const float oldPixel = buffer[index];
            // Reduce the current pixel to the nearest output value:
            // either full white or full black.
            const float newPixel = oldPixel >= threshold ? 255.0f : 0.0f;
            // The difference is not discarded. It is redistributed to surrounding
            // pixels so that the average tone over a larger area stays similar.
            const float error = oldPixel - newPixel;

            if (newPixel >= 128.0f)
            {
                setWhiteSample(bitonal, x, y);
            }

            // Floyd-Steinberg diffusion:
            //   current -> right        7/16
            //             down-left    3/16
            //             down         5/16
            //             down-right   1/16
            // The weights sum to 1, so the algorithm preserves total brightness
            // as much as possible while processing the image from left to right,
            // top to bottom.
            if (x + 1 < width)
            {
                buffer[index + 1] += error * 7.0f / 16.0f;
            }
            if (y + 1 < height)
            {
                if (x > 0)
                {
                    buffer[index + size_t(width) - 1] += error * 3.0f / 16.0f;
                }
                buffer[index + size_t(width)] += error * 5.0f / 16.0f;
                if (x + 1 < width)
                {
                    buffer[index + size_t(width) + 1] += error * 1.0f / 16.0f;
                }
            }
        }
    }

    return bitonal;
}

}   // namespace pdf
