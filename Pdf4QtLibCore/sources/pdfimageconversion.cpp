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
    m_automaticThreshold = DEFAULT_THRESHOLD;
}

void PDFImageConversion::setConversionMethod(ConversionMethod method)
{
    m_conversionMethod = method;
}

void PDFImageConversion::setThreshold(int threshold)
{
    m_manualThreshold = threshold;
}

bool PDFImageConversion::convert()
{
    if (m_image.isNull())
    {
        return false;
    }

    QImage bitonal;

    // Thresholding
    int threshold = DEFAULT_THRESHOLD;

    switch (m_conversionMethod)
    {
        case pdf::PDFImageConversion::ConversionMethod::Automatic:
            m_automaticThreshold = calculateOtsu1DThreshold();
            threshold = m_automaticThreshold;
            break;

        case pdf::PDFImageConversion::ConversionMethod::Manual:
            threshold = m_manualThreshold;
            break;

        case pdf::PDFImageConversion::ConversionMethod::Adaptive:
            m_automaticThreshold = calculateOtsu1DThreshold();
            bitonal = convertAdaptive();
            m_convertedImage = std::move(bitonal);
            return !m_convertedImage.isNull();

        case pdf::PDFImageConversion::ConversionMethod::Dither:
            if (m_manualThreshold != DEFAULT_THRESHOLD)
            {
                threshold = m_manualThreshold;
            }
            else
            {
                m_automaticThreshold = calculateOtsu1DThreshold();
                threshold = m_automaticThreshold;
            }
            bitonal = convertDithered(threshold);
            m_convertedImage = std::move(bitonal);
            return !m_convertedImage.isNull();

        default:
            Q_ASSERT(false);
            break;
    }

    bitonal = QImage(m_image.width(), m_image.height(), QImage::Format_Mono);
    bitonal.fill(0);

    for (int y = 0; y < m_image.height(); ++y)
    {
        for (int x = 0; x < m_image.width(); ++x)
        {
            QColor pixelColor = m_image.pixelColor(x, y);
            int pixelValue = pixelColor.lightness();
            bool bit = (pixelValue >= threshold);
            bitonal.setPixel(x, y, bit);
        }
    }

    m_convertedImage = std::move(bitonal);
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

int PDFImageConversion::calculateOtsu1DThreshold() const
{
    if (m_image.isNull())
    {
        return 128;
    }

    // Histogram of lightness occurences
    std::array<int, 256> histogram = { };

    for (int x = 0; x < m_image.width(); ++x)
    {
        for (int y = 0; y < m_image.height(); ++y)
        {
            int lightness = m_image.pixelColor(x, y).lightness();
            Q_ASSERT(lightness >= 0 && lightness <= 255);

            int clampedLightness = qBound(0, lightness, 255);
            histogram[clampedLightness] += 1;
        }
    }

    float factor = 1.0f / float(m_image.width() * m_image.height());

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

QImage PDFImageConversion::convertAdaptive() const
{
    if (m_image.isNull())
    {
        return QImage();
    }

    QImage source = m_image.convertToFormat(QImage::Format_Grayscale8);
    if (source.isNull())
    {
        return QImage();
    }

    const int width = source.width();
    const int height = source.height();
    const int radius = ADAPTIVE_WINDOW_RADIUS;

    std::vector<int> integral((width + 1) * (height + 1), 0);

    for (int y = 1; y <= height; ++y)
    {
        const uchar* row = source.constScanLine(y - 1);
        int rowSum = 0;
        for (int x = 1; x <= width; ++x)
        {
            rowSum += row[x - 1];
            const int idx = y * (width + 1) + x;
            integral.at(idx) = integral.at(idx - (width + 1)) + rowSum;
        }
    }

    QImage bitonal(width, height, QImage::Format_Mono);
    bitonal.fill(0);

    for (int y = 0; y < height; ++y)
    {
        int y0 = qMax(0, y - radius);
        int y1 = qMin(height - 1, y + radius);
        for (int x = 0; x < width; ++x)
        {
            int x0 = qMax(0, x - radius);
            int x1 = qMin(width - 1, x + radius);

            const int ax0 = x0;
            const int ay0 = y0;
            const int ax1 = x1 + 1;
            const int ay1 = y1 + 1;

            const int sum = integral[ay1 * (width + 1) + ax1]
                            - integral[ay0 * (width + 1) + ax1]
                            - integral[ay1 * (width + 1) + ax0]
                            + integral[ay0 * (width + 1) + ax0];

            const int count = (x1 - x0 + 1) * (y1 - y0 + 1);
            const int mean = count > 0 ? (sum / count) : 0;
            const int threshold = mean - ADAPTIVE_OFFSET;

            const int pixelValue = source.pixelColor(x, y).lightness();
            bitonal.setPixel(x, y, pixelValue >= threshold);
        }
    }

    return bitonal;
}

QImage PDFImageConversion::convertDithered(int threshold) const
{
    if (m_image.isNull())
    {
        return QImage();
    }

    QImage source = m_image.convertToFormat(QImage::Format_Grayscale8);
    if (source.isNull())
    {
        return QImage();
    }

    const int width = source.width();
    const int height = source.height();

    std::vector<float> buffer(width * height, 0.0f);
    for (int y = 0; y < height; ++y)
    {
        const uchar* row = source.constScanLine(y);
        for (int x = 0; x < width; ++x)
        {
            buffer.at(y * width + x) = row[x];
        }
    }

    QImage bitonal(width, height, QImage::Format_Mono);
    bitonal.fill(0);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int index = y * width + x;
            const float oldPixel = buffer[index];
            const float newPixel = oldPixel >= threshold ? 255.0f : 0.0f;
            const float error = oldPixel - newPixel;
            bitonal.setPixel(x, y, newPixel >= 128.0f);

            if (x + 1 < width)
            {
                buffer.at(index + 1) += error * 7.0f / 16.0f;
            }
            if (y + 1 < height)
            {
                if (x > 0)
                {
                    buffer.at(index + width - 1) += error * 3.0f / 16.0f;
                }
                buffer.at(index + width) += error * 5.0f / 16.0f;
                if (x + 1 < width)
                {
                    buffer.at(index + width + 1) += error * 1.0f / 16.0f;
                }
            }
        }
    }

    return bitonal;
}

}   // namespace pdf
