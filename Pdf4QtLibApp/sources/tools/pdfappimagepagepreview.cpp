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

#include "pdfappimagepagepreview.h"

#include "pdfexception.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Helpers of the preview of an image page
class PDFAppImagePagePreviewHelper
{
public:
    PDFAppImagePagePreviewHelper() = delete;

    /// Largest side of the page at the actual size (logical pixels): a page of 14 400 points
    /// has 19 200 at 100 %, a widget may be 16 777 215 at most
    static constexpr int MAXIMUM_SIDE = 1 << 20;

    /// A side in logical pixels: whole, at least 1, at most MAXIMUM_SIDE
    static int toPixels(double value)
    {
        if (!std::isfinite(value) || value < 1.0)
        {
            return 1;
        }
        return int(std::min<double>(std::round(value), MAXIMUM_SIDE));
    }

    /// Angle of a turn clockwise in degrees
    static double getAngle(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::None:
                return 0.0;
            case PageRotation::Rotate90:
                return 90.0;
            case PageRotation::Rotate180:
                return 180.0;
            case PageRotation::Rotate270:
                return 270.0;
        }
        return 0.0;
    }
};

PDFAppImagePagePreview::PDFAppImagePagePreview(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolImagePagePreview"));
}

PDFAppImagePagePreview::~PDFAppImagePagePreview() = default;

void PDFAppImagePagePreview::setImage(const QImage& image, QSizeF imageSize)
{
    m_image = image;
    m_imageSize = imageSize;
    updateGeometry();
    update();
}

void PDFAppImagePagePreview::setPageLayout(const PDFImagePageLayout& layout)
{
    if (m_layout != layout)
    {
        m_layout = layout;
        updateGeometry();
        update();
    }
}

void PDFAppImagePagePreview::setFit(bool isFit)
{
    if (m_isFit != isFit)
    {
        m_isFit = isFit;
        updateGeometry();
        update();
    }
}

void PDFAppImagePagePreview::setShadowVisible(bool isVisible)
{
    if (m_isShadowVisible != isVisible)
    {
        m_isShadowVisible = isVisible;
        update();
    }
}

bool PDFAppImagePagePreview::hasPage() const
{
    const QSizeF pageSize = getPageSize();
    return !m_imageSize.isEmpty() && std::isfinite(m_imageSize.width()) && std::isfinite(m_imageSize.height()) && !pageSize.isEmpty();
}

QSizeF PDFAppImagePagePreview::getPageSize() const
{
    if (m_imageSize.isEmpty())
    {
        return QSizeF();
    }
    return m_layout.getPageSize(m_imageSize);
}

double PDFAppImagePagePreview::getScale() const
{
    if (!hasPage())
    {
        return 0.0;
    }

    const double actualScale = ACTUAL_DPI / 72.0;
    if (!m_isFit)
    {
        return actualScale;
    }

    const QSizeF pageSize = getPageSize();
    const QRect contents = contentsRect();
    if (contents.width() < 1 || contents.height() < 1)
    {
        return 0.0;
    }
    return std::min(contents.width() / pageSize.width(), contents.height() / pageSize.height());
}

QRectF PDFAppImagePagePreview::getPageRect() const
{
    const double scale = getScale();
    if (scale <= 0.0)
    {
        return QRectF();
    }

    // Centered in the contents rectangle; the actual size starts at its corner when it is larger
    const QSizeF pageSize = getPageSize() * scale;
    const QRectF contents(contentsRect());
    const double left = contents.left() + std::max(0.0, (contents.width() - pageSize.width()) * 0.5);
    const double top = contents.top() + std::max(0.0, (contents.height() - pageSize.height()) * 0.5);
    return QRectF(QPointF(std::round(left), std::round(top)), pageSize);
}

QRectF PDFAppImagePagePreview::mapFromPage(const QRectF& rect) const
{
    const QRectF pageRect = getPageRect();
    const double scale = getScale();
    if (pageRect.isEmpty() || rect.isEmpty())
    {
        return QRectF();
    }
    return QRectF(pageRect.left() + rect.left() * scale, pageRect.top() + rect.top() * scale, rect.width() * scale, rect.height() * scale);
}

QRectF PDFAppImagePagePreview::getImageRect() const
{
    if (!hasPage())
    {
        return QRectF();
    }
    return mapFromPage(m_layout.getImageRect(getPageSize(), m_imageSize));
}

QRectF PDFAppImagePagePreview::getAreaRect() const
{
    if (!hasPage())
    {
        return QRectF();
    }
    return mapFromPage(m_layout.getArea(getPageSize()));
}

QImage PDFAppImagePagePreview::getColorManagedPicture(const QImage& picture, const PDFCMS* cms)
{
    if (!cms || picture.isNull())
    {
        return picture;
    }

    // The color space of the image of the page (PDFImagePageFactory::createColorSpace): ICCBased
    // of the profile of the image, else DeviceGray for a gray image and DeviceRGB
    const QByteArray iccProfile = picture.colorSpace().isValid() ? picture.colorSpace().iccProfile() : QByteArray();
    const QColorSpace profileSpace = iccProfile.isEmpty() ? QColorSpace() : QColorSpace::fromIccProfile(iccProfile);
    const bool isCmykProfile = profileSpace.isValid() && profileSpace.colorModel() == QColorSpace::ColorModel::Cmyk;
    const bool isGrayProfile = profileSpace.isValid() && profileSpace.colorModel() == QColorSpace::ColorModel::Gray;
    const bool isRgbProfile = profileSpace.isValid() && profileSpace.colorModel() == QColorSpace::ColorModel::Rgb;
    const bool isCmyk = isCmykProfile && picture.format() == QImage::Format_CMYK8888;
    const bool hasProfile = isCmyk || isGrayProfile || isRgbProfile;
    const bool isGray = hasProfile ? isGrayProfile : picture.isGrayscale();
    const int channels = isCmyk ? 4 : (isGray ? 1 : 3);
    const QByteArray iccId = hasProfile ? QCryptographicHash::hash(iccProfile, QCryptographicHash::Sha256) : QByteArray();

    const QImage source = isCmyk ? picture : picture.convertToFormat(QImage::Format_ARGB32);
    const QImage alpha = isCmyk ? QImage() : source;
    QImage result(picture.size(), QImage::Format_ARGB32);
    if (result.isNull())
    {
        return picture;
    }

    const int width = picture.width();
    PDFRenderErrorReporterDummy reporter;
    std::vector<float> colors;
    std::vector<unsigned char> output;
    for (int firstRow = 0; firstRow < picture.height(); firstRow += COLOR_MANAGED_ROWS)
    {
        const int rows = qMin(COLOR_MANAGED_ROWS, picture.height() - firstRow);
        const size_t pixels = size_t(width) * size_t(rows);
        colors.resize(pixels * size_t(channels));
        output.assign(pixels * 3, 0);

        // The components 0 - 1 of the pixels of the rows
        size_t position = 0;
        for (int y = firstRow; y < firstRow + rows; ++y)
        {
            const uchar* line = source.constScanLine(y);
            for (int x = 0; x < width; ++x)
            {
                if (isCmyk)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        colors[position++] = line[4 * x + c] / 255.0f;
                    }
                    continue;
                }
                const QRgb rgb = reinterpret_cast<const QRgb*>(line)[x];
                if (isGray)
                {
                    colors[position++] = qGray(rgb) / 255.0f;
                }
                else
                {
                    colors[position++] = qRed(rgb) / 255.0f;
                    colors[position++] = qGreen(rgb) / 255.0f;
                    colors[position++] = qBlue(rgb) / 255.0f;
                }
            }
        }

        const RenderingIntent intent = RenderingIntent::RelativeColorimetric;
        PDFRenderErrorReporter* reporterPointer = &reporter;
        const bool isConverted = hasProfile ? cms->fillRGBBufferFromICC(colors, intent, output.data(), iccId, iccProfile, reporterPointer)
                                            : (isGray ? cms->fillRGBBufferFromDeviceGray(colors, intent, output.data(), reporterPointer)
                                                      : cms->fillRGBBufferFromDeviceRGB(colors, intent, output.data(), reporterPointer));
        if (!isConverted)
        {
            return picture;
        }

        position = 0;
        for (int y = firstRow; y < firstRow + rows; ++y)
        {
            QRgb* target = reinterpret_cast<QRgb*>(result.scanLine(y));
            const QRgb* alphaLine = alpha.isNull() ? nullptr : reinterpret_cast<const QRgb*>(alpha.constScanLine(y));
            for (int x = 0; x < width; ++x)
            {
                target[x] = qRgba(output[position], output[position + 1], output[position + 2], alphaLine ? qAlpha(alphaLine[x]) : 255);
                position += 3;
            }
        }
    }

    result.setDevicePixelRatio(picture.devicePixelRatio());
    return result;
}

QSize PDFAppImagePagePreview::sizeHint() const
{
    using Helper = PDFAppImagePagePreviewHelper;

    const QMargins margins = contentsMargins();
    if (!hasPage())
    {
        return QSize(margins.left() + margins.right() + 1, margins.top() + margins.bottom() + 1);
    }

    // The actual size (Fit takes any size, its hint is the actual one too)
    const QSizeF actual = getPageSize() * (ACTUAL_DPI / 72.0);
    return QSize(Helper::toPixels(actual.width()) + margins.left() + margins.right(), Helper::toPixels(actual.height()) + margins.top() + margins.bottom());
}

QSize PDFAppImagePagePreview::minimumSizeHint() const
{
    // Fit takes any size; the actual size is as large as the page (a scroll area scrolls it)
    if (m_isFit)
    {
        const QMargins margins = contentsMargins();
        return QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    return sizeHint();
}

void PDFAppImagePagePreview::paintEvent(QPaintEvent* event)
{
    using Helper = PDFAppImagePagePreviewHelper;
    Q_UNUSED(event);

    const QRectF pageRect = getPageRect();
    if (pageRect.isEmpty())
    {
        return;
    }

    // page 336:2235: reading/paper/page with the shadow 0 / 8 / 24 of effect/shadow
    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (m_isShadowVisible)
    {
        PDFStylePainter::shadow(&painter, pageRect, PDFStylePainter::Radii(0.0), QPointF(0.0, SHADOW_OFFSET), SHADOW_BLUR, PDFStylePainter::color(PDFThemeColors::EffectShadow));
    }
    PDFStylePainter::fillRect(&painter, pageRect, PDFThemeColors::ReadingPaperPage);

    const QRectF areaRect = getAreaRect();
    const QRectF imageRect = getImageRect();
    if (m_image.isNull() || areaRect.isEmpty() || imageRect.isEmpty())
    {
        return;
    }

    // The clip of the shown image in its pixels, turned into the rectangle of the laid out image,
    // clipped by the area (the same rule as the content of the page, PDFImagePageFactory)
    const QRectF clip = m_layout.getEffectiveClip();
    const QRectF source(clip.left() * m_image.width(), clip.top() * m_image.height(), clip.width() * m_image.width(), clip.height() * m_image.height());
    const bool isTransposed = m_layout.turn == PageRotation::Rotate90 || m_layout.turn == PageRotation::Rotate270;
    const QSizeF targetSize = isTransposed ? imageRect.size().transposed() : imageRect.size();

    PDFPainterStateGuard imageGuard(&painter);
    painter.setClipRect(areaRect, Qt::IntersectClip);
    painter.translate(imageRect.center());
    painter.rotate(Helper::getAngle(m_layout.turn));
    painter.drawImage(QRectF(QPointF(-targetSize.width() * 0.5, -targetSize.height() * 0.5), targetSize), m_image, source);
}

}   // namespace pdf

#include "moc_pdfappimagepagepreview.cpp"
