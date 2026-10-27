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

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QtMath>

#include <cmath>

namespace pdf
{

/// Drawing of the parts of the Color picker 1294:1695
class PDFComponentColorPickerPainterHelper
{
public:
    PDFComponentColorPickerPainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using Metrics = PDFComponentPainter::ColorPickerMetrics;

    /// Largest width or height of a drawn part (a larger or a non-finite one draws nothing)
    static constexpr qreal MAX_EXTENT = 1 << 20;

    /// Largest value of the hue slider (the slider has the values 0 - 359)
    static constexpr qreal MAX_HUE = 359.0;

    /// Shadow of the marker (effect/shadow, 0 / 1 / 4 as the marker of 1294:1537)
    static constexpr qreal MARKER_SHADOW_BLUR = 4.0;
    static constexpr qreal MARKER_SHADOW_OFFSET = 1.0;

    static bool isDrawable(const QRectF& rect)
    {
        return !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height()) &&
               rect.width() <= MAX_EXTENT && rect.height() <= MAX_EXTENT;
    }

    /// Returns the value bounded to 0 - 1 (a non-finite value is 0)
    static qreal unit(qreal value)
    {
        return qIsFinite(value) ? qBound(0.0, value, 1.0) : 0.0;
    }

    /// Returns the hue bounded to 0 - 359 (a non-finite value is 0)
    static qreal hue(qreal value)
    {
        return qIsFinite(value) ? qBound(0.0, value, MAX_HUE) : 0.0;
    }

    /// Largest width or height of the pictures of the colors in device pixels (a larger picture is stretched)
    static constexpr int MAX_PICTURE_EXTENT = 4096;

    /// Distance of the channels, under which a pixel is the leak color of the theme
    static constexpr int LEAK_TOLERANCE = 6;

    /// Returns the color of the picture: a color, which would be the leak color
    /// of the theme (violet, the sign of a color of the system in the user
    /// interface), gets its blue channel moved out of it - the pictures of the
    /// colors (the area, the spectrum) never show the leak color, a band of 3°
    /// of the hues differs by an invisible amount
    static QRgb avoidLeak(int red, int green, int blue)
    {
        const QColor leak = PDFThemeColors::getLeakColor();
        if (qAbs(red - leak.red()) <= LEAK_TOLERANCE && qAbs(green - leak.green()) <= LEAK_TOLERANCE && qAbs(blue - leak.blue()) <= LEAK_TOLERANCE)
        {
            blue = leak.blue() > 127 ? leak.blue() - LEAK_TOLERANCE - 1 : leak.blue() + LEAK_TOLERANCE + 1;
        }
        return qRgb(red, green, blue);
    }

    /// Returns the color of HSV (the hue 0 - 360, the saturation and the value 0 - 1)
    static QRgb hsv(qreal hueValue, qreal saturation, qreal value)
    {
        const qreal h = std::fmod(hue(hueValue), 360.0) / 60.0;
        const int sector = qBound(0, int(h), 5);
        const qreal fraction = h - sector;
        const qreal s = unit(saturation);
        const qreal v = unit(value);
        const qreal p = v * (1.0 - s);
        const qreal q = v * (1.0 - s * fraction);
        const qreal t = v * (1.0 - s * (1.0 - fraction));

        qreal red = v;
        qreal green = t;
        qreal blue = p;
        switch (sector)
        {
            case 1:
                red = q;
                green = v;
                blue = p;
                break;
            case 2:
                red = p;
                green = v;
                blue = t;
                break;
            case 3:
                red = p;
                green = q;
                blue = v;
                break;
            case 4:
                red = t;
                green = p;
                blue = v;
                break;
            case 5:
                red = v;
                green = p;
                blue = q;
                break;
            default:
                break;
        }
        return avoidLeak(qRound(red * 255.0), qRound(green * 255.0), qRound(blue * 255.0));
    }

    /// Returns the size in device pixels of a picture of the logical size (bounded)
    static QSize getPictureSize(const QPainter* painter, const QSizeF& size)
    {
        const QPaintDevice* device = painter->device();
        const qreal ratio = device ? device->devicePixelRatioF() : 1.0;
        const qreal boundedRatio = (qIsFinite(ratio) && ratio > 0.0) ? qMin(ratio, 8.0) : 1.0;
        return QSize(qBound(1, qCeil(size.width() * boundedRatio), MAX_PICTURE_EXTENT), qBound(1, qCeil(size.height() * boundedRatio), MAX_PICTURE_EXTENT));
    }

    /// Returns the picture of the area of the hue (cached)
    static QPixmap getAreaPicture(qreal hueValue, const QSize& pixels)
    {
        const int hueKey = qRound(hue(hueValue) * 100.0);
        const QString key = QStringLiteral("pdf4qtfluent/colorarea/%1/%2x%3").arg(hueKey).arg(pixels.width()).arg(pixels.height());
        QPixmap pixmap;
        if (QPixmapCache::find(key, &pixmap))
        {
            return pixmap;
        }

        QImage image(pixels, QImage::Format_RGB32);
        for (int y = 0; y < pixels.height(); ++y)
        {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            const qreal value = 1.0 - (y + 0.5) / pixels.height();
            for (int x = 0; x < pixels.width(); ++x)
            {
                line[x] = hsv(hueKey / 100.0, (x + 0.5) / pixels.width(), value);
            }
        }
        pixmap = QPixmap::fromImage(image);
        QPixmapCache::insert(key, pixmap);
        return pixmap;
    }

    /// Returns the picture of the spectrum of a track (one row, cached): the hues
    /// lie under the center of the thumb, from a half of the thumb to the other half
    static QPixmap getSpectrumPicture(const QSize& pixels, qreal logicalWidth)
    {
        const QString key = QStringLiteral("pdf4qtfluent/huespectrum/%1/%2").arg(pixels.width()).arg(qRound(logicalWidth * 100.0));
        QPixmap pixmap;
        if (QPixmapCache::find(key, &pixmap))
        {
            return pixmap;
        }

        const qreal travel = qMax(1.0, logicalWidth - Metrics::SLIDER_THUMB);
        QImage image(QSize(pixels.width(), 1), QImage::Format_RGB32);
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(0));
        for (int x = 0; x < pixels.width(); ++x)
        {
            const qreal logicalX = (x + 0.5) * logicalWidth / pixels.width();
            line[x] = hsv((logicalX - Metrics::SLIDER_THUMB / 2.0) / travel * MAX_HUE, 1.0, 1.0);
        }
        pixmap = QPixmap::fromImage(image);
        QPixmapCache::insert(key, pixmap);
        return pixmap;
    }

    /// Returns the length of the travel of the thumb of the slider
    static qreal getThumbTravel(const QRectF& rect)
    {
        return qMax(0.0, rect.width() - Metrics::SLIDER_THUMB);
    }

    /// Draws the marker of the area: the ring of 2 px sample/white with the
    /// shadow effect/shadow outside of the ring (the color stays visible inside)
    static void paintMarker(QPainter* painter, const QPointF& center)
    {
        const qreal radius = Metrics::MARKER / 2.0;
        const QRectF marker(center.x() - radius, center.y() - radius, Metrics::MARKER, Metrics::MARKER);
        const PDFStylePainter::Radii radii(radius);

        {
            const PDFStylePainterGuard guard(painter);
            QPainterPath outside;
            outside.addRect(marker.adjusted(-2.0 * MARKER_SHADOW_BLUR, -2.0 * MARKER_SHADOW_BLUR, 2.0 * MARKER_SHADOW_BLUR, 2.0 * MARKER_SHADOW_BLUR));
            outside.addEllipse(marker.adjusted(Metrics::MARKER_STROKE, Metrics::MARKER_STROKE, -Metrics::MARKER_STROKE, -Metrics::MARKER_STROKE));
            outside.setFillRule(Qt::OddEvenFill);
            painter->setClipPath(outside, Qt::IntersectClip);
            PDFStylePainter::shadow(painter, marker, radii, QPointF(0.0, MARKER_SHADOW_OFFSET), MARKER_SHADOW_BLUR, PDFStylePainter::color(Color::EffectShadow));
        }

        PDFStylePainter::strokeEllipseInside(painter, marker, PDFStylePainter::color(Color::SampleWhite), Metrics::MARKER_STROKE);
    }

    /// Draws the thumb of the hue slider as the thumb of Slider (the handle of the style)
    static void paintThumb(QPainter* painter, const QRectF& thumb, const PDFComponentPainter::State& state)
    {
        if (!state.isEnabled())
        {
            PDFStylePainter::fillEllipse(painter, thumb, PDFStylePainter::color(Color::BgMuted));
            PDFStylePainter::strokeEllipseInside(painter, thumb, PDFStylePainter::color(Color::BorderStrong), 2.0);
            return;
        }

        if (state.isPressed())
        {
            PDFStylePainter::fillEllipse(painter, thumb, PDFStylePainter::color(Color::BgAccent));
        }
        else
        {
            PDFStylePainter::fillEllipse(painter, thumb, PDFStylePainter::color(Color::BgSurface));
            PDFStylePainter::strokeEllipseInside(painter, thumb, PDFStylePainter::color(state.isHovered() ? Color::AccentBlueIcon : Color::BgAccent), 2.0);
        }

        if (state.hasFocusRing())
        {
            PDFComponentPainter::paintFocusRingOutside(painter, thumb, PDFStylePainter::Radii(thumb.width() / 2.0));
        }
    }
};

QPointF PDFComponentPainter::getColorAreaPoint(const QRectF& area, qreal saturation, qreal value)
{
    using Helper = PDFComponentColorPickerPainterHelper;
    return QPointF(area.left() + Helper::unit(saturation) * area.width(), area.top() + (1.0 - Helper::unit(value)) * area.height());
}

QPointF PDFComponentPainter::getColorAreaValues(const QRectF& area, const QPointF& point)
{
    using Helper = PDFComponentColorPickerPainterHelper;

    if (!Helper::isDrawable(area) || !qIsFinite(point.x()) || !qIsFinite(point.y()))
    {
        return QPointF(0.0, 1.0);
    }

    const qreal saturation = Helper::unit((point.x() - area.left()) / area.width());
    const qreal value = Helper::unit(1.0 - (point.y() - area.top()) / area.height());
    return QPointF(saturation, value);
}

void PDFComponentPainter::paintColorArea(QPainter* painter, const QRectF& area, const ColorArea& content, const State& state)
{
    using Helper = PDFComponentColorPickerPainterHelper;
    using Metrics = ColorPickerMetrics;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(area))
    {
        return;
    }

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const PDFStylePainter::Radii radii(qMin<qreal>(Metrics::AREA_RADIUS, qMin(area.width(), area.height()) / 2.0));

    {
        // The colors of HSV: the hue, white to the left (the saturation), black
        // to the bottom (the value); the colors keep their values in each state.
        // They are the picture of the colors themselves, not tokens of the theme.
        const PDFStylePainterGuard areaGuard(painter);
        painter->setClipPath(PDFStylePainter::roundedPath(area, radii), Qt::IntersectClip);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QPixmap picture = Helper::getAreaPicture(content.hue, Helper::getPictureSize(painter, area.size()));
        painter->drawPixmap(area, picture, QRectF(picture.rect()));
    }

    // The focus ring of 2 px inside replaces the border
    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, area, radii);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, area, radii, Color::BorderDefault, 1.0);
    }

    Helper::paintMarker(painter, getColorAreaPoint(area, content.saturation, content.value));
}

QRectF PDFComponentPainter::getHueSliderThumbRect(const QRectF& rect, qreal hue)
{
    using Helper = PDFComponentColorPickerPainterHelper;
    using Metrics = ColorPickerMetrics;

    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    const qreal left = rect.left() + Helper::hue(hue) / Helper::MAX_HUE * Helper::getThumbTravel(rect);
    const qreal top = rect.top() + (rect.height() - Metrics::SLIDER_THUMB) / 2.0;
    return QRectF(left, top, Metrics::SLIDER_THUMB, Metrics::SLIDER_THUMB);
}

qreal PDFComponentPainter::getHueSliderValue(const QRectF& rect, qreal x)
{
    using Helper = PDFComponentColorPickerPainterHelper;
    using Metrics = ColorPickerMetrics;

    const qreal travel = Helper::getThumbTravel(rect);
    if (!Helper::isDrawable(rect) || !qIsFinite(x) || travel <= 0.0)
    {
        return 0.0;
    }

    // The center of the thumb follows the pointer
    return Helper::hue((x - rect.left() - Metrics::SLIDER_THUMB / 2.0) / travel * Helper::MAX_HUE);
}

void PDFComponentPainter::paintHueSlider(QPainter* painter, const QRectF& rect, qreal hue, const State& state)
{
    using Helper = PDFComponentColorPickerPainterHelper;
    using Metrics = ColorPickerMetrics;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Track of the full width with the spectrum: the hues lie under the center
    // of the thumb (from a half of the thumb to the other half)
    const QRectF track(rect.left(), rect.top() + (rect.height() - Metrics::SLIDER_TRACK) / 2.0, rect.width(), Metrics::SLIDER_TRACK);
    {
        const PDFStylePainterGuard trackGuard(painter);
        painter->setClipPath(PDFStylePainter::roundedPath(track, PDFStylePainter::Radii(Metrics::SLIDER_TRACK / 2.0)), Qt::IntersectClip);
        const QPixmap spectrum = Helper::getSpectrumPicture(Helper::getPictureSize(painter, track.size()), track.width());
        painter->drawPixmap(track, spectrum, QRectF(spectrum.rect()));
    }

    Helper::paintThumb(painter, getHueSliderThumbRect(rect, hue), state);
}

void PDFComponentPainter::paintColorComparison(QPainter* painter, const QRectF& rect, const ColorComparison& content, const State& state)
{
    using Helper = PDFComponentColorPickerPainterHelper;
    using Metrics = ColorPickerMetrics;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const PDFStylePainter::Radii radii(qMin<qreal>(Metrics::COMPARISON_RADIUS, qMin(rect.width(), rect.height()) / 2.0));

    {
        const PDFStylePainterGuard clipGuard(painter);
        painter->setClipPath(PDFStylePainter::roundedPath(rect, radii), Qt::IntersectClip);

        // The current color at the leading side, the new one at the trailing side
        const qreal half = rect.width() / 2.0;
        const QRectF leading = state.visualRect(rect, QRectF(0.0, 0.0, half, rect.height()));
        const QRectF trailing = state.visualRect(rect, QRectF(half, 0.0, rect.width() - half, rect.height()));

        if (content.current.isValid())
        {
            painter->fillRect(leading, content.current.toRgb());
        }
        else
        {
            // No color: bg/surface crossed by icon/danger (as the swatch of the Color button)
            painter->fillRect(leading, PDFStylePainter::color(Color::BgSurface));
            QPen pen(PDFStylePainter::color(state.isEnabled() ? Color::IconDanger : Color::IconDisabled), 1.5);
            pen.setCapStyle(Qt::FlatCap);
            painter->setPen(pen);
            const QRectF slash = leading.adjusted(3.0, 3.0, -3.0, -3.0);
            if (state.isRightToLeft())
            {
                painter->drawLine(slash.topLeft(), slash.bottomRight());
            }
            else
            {
                painter->drawLine(slash.bottomLeft(), slash.topRight());
            }
        }

        painter->fillRect(trailing, content.color.isValid() ? content.color.toRgb() : PDFStylePainter::color(Color::BgSurface));
    }

    PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
}

}   // namespace pdf
