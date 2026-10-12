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

#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfthemeiconengine.h"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPixmapCache>
#include <QtMath>

#include <vector>

namespace pdf
{

class PDFStylePainterHelper
{
public:
    PDFStylePainterHelper() = delete;

    /// Blurs the image of format Alpha8 by three box filters, which
    /// approximate the gaussian blur of the given standard deviation.
    static void blur(QImage& image, qreal sigma)
    {
        if (sigma <= 0.0 || image.isNull())
        {
            return;
        }

        const int width = image.width();
        const int height = image.height();

        // Ideal width of the box of three passes
        int box = qMax(1, qRound(qSqrt(4.0 * sigma * sigma + 1.0)));
        if (box % 2 == 0)
        {
            ++box;
        }
        const int radius = box / 2;

        std::vector<uchar> source(static_cast<size_t>(qMax(width, height)));
        std::vector<uchar> target(source.size());

        auto blurLine = [radius, box, &source, &target](int count)
        {
            int sum = 0;
            for (int i = -radius; i <= radius; ++i)
            {
                sum += source[static_cast<size_t>(qBound(0, i, count - 1))];
            }

            for (int i = 0; i < count; ++i)
            {
                target[static_cast<size_t>(i)] = static_cast<uchar>(sum / box);
                const int removed = qBound(0, i - radius, count - 1);
                const int added = qBound(0, i + radius + 1, count - 1);
                sum += source[static_cast<size_t>(added)] - source[static_cast<size_t>(removed)];
            }
        };

        for (int pass = 0; pass < 3; ++pass)
        {
            for (int y = 0; y < height; ++y)
            {
                uchar* line = image.scanLine(y);
                std::copy(line, line + width, source.begin());
                blurLine(width);
                std::copy(target.begin(), target.begin() + width, line);
            }

            for (int x = 0; x < width; ++x)
            {
                for (int y = 0; y < height; ++y)
                {
                    source[static_cast<size_t>(y)] = image.scanLine(y)[x];
                }
                blurLine(height);
                for (int y = 0; y < height; ++y)
                {
                    image.scanLine(y)[x] = target[static_cast<size_t>(y)];
                }
            }
        }
    }

    static void appendCorner(QPainterPath& path, const QRectF& rect, qreal radius, int corner)
    {
        // Corners are numbered clockwise from the top left one
        switch (corner)
        {
            case 0:
                if (radius > 0.0)
                {
                    path.arcTo(QRectF(rect.left(), rect.top(), 2 * radius, 2 * radius), 180.0, -90.0);
                }
                else
                {
                    path.lineTo(rect.topLeft());
                }
                break;

            case 1:
                if (radius > 0.0)
                {
                    path.arcTo(QRectF(rect.right() - 2 * radius, rect.top(), 2 * radius, 2 * radius), 90.0, -90.0);
                }
                else
                {
                    path.lineTo(rect.topRight());
                }
                break;

            case 2:
                if (radius > 0.0)
                {
                    path.arcTo(QRectF(rect.right() - 2 * radius, rect.bottom() - 2 * radius, 2 * radius, 2 * radius), 0.0, -90.0);
                }
                else
                {
                    path.lineTo(rect.bottomRight());
                }
                break;

            default:
                if (radius > 0.0)
                {
                    path.arcTo(QRectF(rect.left(), rect.bottom() - 2 * radius, 2 * radius, 2 * radius), 270.0, -90.0);
                }
                else
                {
                    path.lineTo(rect.bottomLeft());
                }
                break;
        }
    }
};

PDFStylePainter::Radii PDFStylePainter::Radii::adjusted(qreal delta) const
{
    auto adjust = [delta](qreal radius)
    {
        // A sharp corner stays sharp, if the shape shrinks
        return (radius <= 0.0 && delta >= 0.0) ? 0.0 : qMax(0.0, radius - delta);
    };

    return Radii(adjust(topLeft), adjust(topRight), adjust(bottomRight), adjust(bottomLeft));
}

QColor PDFStylePainter::color(PDFThemeColors::Color color)
{
    return PDFThemeManager::getInstance()->getColor(color);
}

QPainterPath PDFStylePainter::roundedPath(const QRectF& rect, const Radii& radii)
{
    QPainterPath path;

    if (rect.isEmpty())
    {
        return path;
    }

    const qreal limit = qMin(rect.width(), rect.height()) / 2.0;
    const qreal topLeft = qBound(0.0, radii.topLeft, limit);
    const qreal topRight = qBound(0.0, radii.topRight, limit);
    const qreal bottomRight = qBound(0.0, radii.bottomRight, limit);
    const qreal bottomLeft = qBound(0.0, radii.bottomLeft, limit);

    path.moveTo(rect.left(), rect.top() + topLeft);
    PDFStylePainterHelper::appendCorner(path, rect, topLeft, 0);
    path.lineTo(rect.right() - topRight, rect.top());
    PDFStylePainterHelper::appendCorner(path, rect, topRight, 1);
    path.lineTo(rect.right(), rect.bottom() - bottomRight);
    PDFStylePainterHelper::appendCorner(path, rect, bottomRight, 2);
    path.lineTo(rect.left() + bottomLeft, rect.bottom());
    PDFStylePainterHelper::appendCorner(path, rect, bottomLeft, 3);
    path.closeSubpath();
    return path;
}

void PDFStylePainter::fill(QPainter* painter, const QRectF& rect, const Radii& radii, const QColor& color)
{
    if (!painter || rect.isEmpty() || color.alpha() == 0)
    {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPath(roundedPath(rect, radii));
    painter->restore();
}

void PDFStylePainter::fill(QPainter* painter, const QRectF& rect, const Radii& radii, PDFThemeColors::Color color)
{
    fill(painter, rect, radii, PDFStylePainter::color(color));
}

void PDFStylePainter::strokeInside(QPainter* painter, const QRectF& rect, const Radii& radii, const QColor& color, qreal width)
{
    if (!painter || rect.isEmpty() || color.alpha() == 0 || width <= 0.0)
    {
        return;
    }

    const qreal half = width / 2.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(roundedPath(rect.adjusted(half, half, -half, -half), radii.adjusted(half)));
    painter->restore();
}

void PDFStylePainter::strokeInside(QPainter* painter, const QRectF& rect, const Radii& radii, PDFThemeColors::Color color, qreal width)
{
    strokeInside(painter, rect, radii, PDFStylePainter::color(color), width);
}

void PDFStylePainter::strokeOutside(QPainter* painter, const QRectF& rect, const Radii& radii, const QColor& color, qreal width)
{
    strokeInside(painter, rect.adjusted(-width, -width, width, width), radii.adjusted(-width), color, width);
}

void PDFStylePainter::fillRect(QPainter* painter, const QRectF& rect, const QColor& color)
{
    if (!painter || rect.isEmpty() || color.alpha() == 0)
    {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(rect, color);
    painter->restore();
}

void PDFStylePainter::fillRect(QPainter* painter, const QRectF& rect, PDFThemeColors::Color color)
{
    fillRect(painter, rect, PDFStylePainter::color(color));
}

void PDFStylePainter::fillEllipse(QPainter* painter, const QRectF& rect, const QColor& color)
{
    if (!painter || rect.isEmpty() || color.alpha() == 0)
    {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawEllipse(rect);
    painter->restore();
}

void PDFStylePainter::strokeEllipseInside(QPainter* painter, const QRectF& rect, const QColor& color, qreal width)
{
    if (!painter || rect.isEmpty() || color.alpha() == 0 || width <= 0.0)
    {
        return;
    }

    const qreal half = width / 2.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(color, width));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(rect.adjusted(half, half, -half, -half));
    painter->restore();
}

void PDFStylePainter::shadow(QPainter* painter, const QRectF& rect, const Radii& radii, const QPointF& offset, qreal blur, const QColor& color)
{
    if (!painter || rect.isEmpty() || color.alpha() == 0)
    {
        return;
    }

    const QPaintDevice* device = painter->device();
    const qreal devicePixelRatio = device ? device->devicePixelRatioF() : 1.0;
    const qreal margin = qCeil(blur);
    const QSizeF logicalSize(rect.width() + 2 * margin, rect.height() + 2 * margin);
    const QSize pixelSize(qCeil(logicalSize.width() * devicePixelRatio), qCeil(logicalSize.height() * devicePixelRatio));

    if (pixelSize.isEmpty() || pixelSize.width() > 8192 || pixelSize.height() > 8192)
    {
        return;
    }

    const QString key = QStringLiteral("pdf4qtfluent/shadow/%1x%2@%9/%3/%4/%5/%6/%7/%8")
            .arg(pixelSize.width()).arg(pixelSize.height())
            .arg(radii.topLeft).arg(radii.topRight).arg(radii.bottomRight).arg(radii.bottomLeft)
            .arg(blur).arg(color.rgba(), 8, 16, QLatin1Char('0')).arg(qRound(devicePixelRatio * 100.0));

    QPixmap pixmap;
    if (!QPixmapCache::find(key, &pixmap))
    {
        QImage alpha(pixelSize, QImage::Format_Alpha8);
        alpha.fill(0);

        {
            QPainter alphaPainter(&alpha);
            alphaPainter.setRenderHint(QPainter::Antialiasing, true);
            alphaPainter.scale(devicePixelRatio, devicePixelRatio);
            alphaPainter.setPen(Qt::NoPen);
            alphaPainter.setBrush(Qt::black);
            alphaPainter.drawPath(roundedPath(QRectF(margin, margin, rect.width(), rect.height()), radii));
        }

        // The blur of the design is twice the standard deviation
        PDFStylePainterHelper::blur(alpha, blur * devicePixelRatio / 2.0);

        QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        const int red = color.red();
        const int green = color.green();
        const int blue = color.blue();
        const int opacity = color.alpha();

        for (int y = 0; y < pixelSize.height(); ++y)
        {
            const uchar* source = alpha.constScanLine(y);
            QRgb* target = reinterpret_cast<QRgb*>(image.scanLine(y));

            for (int x = 0; x < pixelSize.width(); ++x)
            {
                const int value = source[x] * opacity / 255;
                target[x] = qPremultiply(qRgba(red, green, blue, value));
            }
        }

        pixmap = QPixmap::fromImage(std::move(image));
        QPixmapCache::insert(key, pixmap);
    }

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawPixmap(QRectF(rect.topLeft() + offset - QPointF(margin, margin), logicalSize), pixmap, QRectF(QPointF(0, 0), QSizeF(pixmap.size())));
    painter->restore();
}

void PDFStylePainter::icon(QPainter* painter, const QRectF& rect, const QString& name, const QColor& color)
{
    PDFThemeIcons::paint(painter, rect, name, color);
}

void PDFStylePainter::icon(QPainter* painter, const QRectF& rect, const QString& name, PDFThemeColors::Color color)
{
    PDFThemeIcons::paint(painter, rect, name, PDFStylePainter::color(color));
}

void PDFStylePainter::text(QPainter* painter, const QRectF& rect, int flags, const QString& text, const QFont& font, const QColor& color)
{
    if (!painter || text.isEmpty())
    {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setFont(font);
    painter->setPen(color);
    painter->drawText(rect, flags, text);
    painter->restore();
}

qreal PDFStylePainter::textWidth(const QString& text, const QFont& font, int flags)
{
    const QFontMetricsF metrics(font);

    if (flags & (Qt::TextShowMnemonic | Qt::TextHideMnemonic))
    {
        return metrics.size(flags, text).width();
    }

    return metrics.horizontalAdvance(text);
}

QRectF PDFStylePainter::centered(const QRectF& outer, const QSizeF& size)
{
    return QRectF(outer.left() + (outer.width() - size.width()) / 2.0,
                  outer.top() + (outer.height() - size.height()) / 2.0,
                  size.width(), size.height());
}

}   // namespace pdf
