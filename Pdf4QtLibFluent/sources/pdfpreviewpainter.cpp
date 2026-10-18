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

#include "pdfpreviewpainter.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"

#include <QCoreApplication>
#include <QImage>
#include <QList>
#include <QPaintEngine>
#include <QPainter>
#include <QSvgRenderer>
#include <QtMath>

namespace pdf
{

/// Geometry constants of the pictures (cards Mode preview, Layout preview,
/// Locked document and Logo of the specification) and their drawing helpers
class PDFPreviewPainterHelper
{
public:
    PDFPreviewPainterHelper() = delete;

    using Color = PDFThemeColors::Color;

    // Mode preview 171:4367: 115 x 89, radius 8, border 1 counted in the layout,
    // padding 10: heading 56 x 6, lines of 4 (full width, 90, 69) with gaps of 5,
    // the picture of two blocks 26 high with a gap of 4 after a padding of 3
    static constexpr int PREVIEW_WIDTH = 115;
    static constexpr int PREVIEW_HEIGHT = 89;
    static constexpr qreal PREVIEW_RADIUS = 8.0;
    static constexpr qreal MODE_INSET = 11.0;
    static constexpr qreal MODE_HEADING_TOP = 11.0;
    static constexpr qreal MODE_HEADING_WIDTH = 56.0;
    static constexpr qreal MODE_HEADING_HEIGHT = 6.0;
    static constexpr qreal MODE_LINE_HEIGHT = 4.0;
    static constexpr qreal MODE_LINE_1_TOP = 22.0;
    static constexpr qreal MODE_LINE_2_TOP = 31.0;
    static constexpr qreal MODE_LINE_2_WIDTH = 90.0;
    static constexpr qreal MODE_LINE_3_TOP = 40.0;
    static constexpr qreal MODE_LINE_3_WIDTH = 69.0;
    static constexpr qreal MODE_BLOCK_TOP = 52.0;
    static constexpr qreal MODE_BLOCK_HEIGHT = 26.0;
    static constexpr qreal MODE_BLOCK_GAP = 4.0;
    static constexpr qreal MODE_RADIUS = 2.0;

    // Layout preview 192:735: pages 40 x 56 (single page, continuous) or
    // 36 x 50 (two pages, two columns), radius 1, lines 2 px at 6, 7 + 6 / 5 / 5 ...
    static constexpr qreal PAGE_RADIUS = 1.0;
    static constexpr qreal PAGE_LINE_LEFT = 6.0;
    static constexpr qreal PAGE_LINE_HEIGHT = 2.0;
    static constexpr qreal PAGE_LINE_RADIUS = 1.0;

    // Locked document 225:906: 160 x 160; the shadow of the badge (ring 86 to
    // 154, offset 4, blur 12) reaches 6 px beyond the right and 10 px beyond
    // the bottom edge, the component does not clip it
    static constexpr int LOCKED_EXTENT = 160;
    static constexpr qreal LOCKED_SHADOW_OFFSET = 4.0;
    static constexpr qreal LOCKED_SHADOW_BLUR = 12.0;
    static constexpr int LOCKED_OVERFLOW_RIGHT = 6;
    static constexpr int LOCKED_OVERFLOW_BOTTOM = 10;

    // Logo / PDF4QT 110:2154
    static constexpr int LOGO_EXTENT = 130;
    static constexpr int LOGO_CACHE_SIZE = 8;               ///< Count of the cached sizes of the logo
    static constexpr int LOGO_CACHE_MAX_EXTENT = 2048;      ///< Larger pictures are drawn from the SVG

    /// Picture of the logo of a size of the device pixels
    struct LogoPicture
    {
        QSize size;
        QImage image;
    };

    /// Returns the picture of the logo of the size of the device pixels (the
    /// SVG is parsed only for a new size; without a lock - the painter is used
    /// only in the thread of the user interface)
    static QImage getLogoImage(const QSize& size)
    {
        static QList<LogoPicture> cache;

        for (qsizetype i = 0; i < cache.size(); ++i)
        {
            if (cache.at(i).size == size)
            {
                // The most recent size first
                cache.move(i, 0);
                return cache.front().image;
            }
        }

        QSvgRenderer renderer(PDFPreviewPainter::getLogoResourcePath());
        if (!renderer.isValid())
        {
            return QImage();
        }

        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter imagePainter(&image);
            imagePainter.setRenderHint(QPainter::Antialiasing, true);
            renderer.render(&imagePainter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));
        }

        cache.prepend(LogoPicture{ size, image });
        while (cache.size() > LOGO_CACHE_SIZE)
        {
            cache.removeLast();
        }
        return image;
    }

    /// Colors of a mode preview
    struct ModeColors
    {
        Color page;
        Color heading;
        Color lines;
        Color block1;
        Color block2;
        bool outlinedBlock2 = false;    ///< The second block has the border sample/black (Bitonal)
    };

    static ModeColors getModeColors(PDFPreviewPainter::Preview preview)
    {
        using Preview = PDFPreviewPainter::Preview;

        switch (preview)
        {
            case Preview::ModeInverted:
                return { Color::SampleBlack, Color::SampleGrayLight, Color::SampleGrayDark, Color::SampleOrange, Color::SampleBlue };

            case Preview::ModeGrayscale:
                return { Color::SampleWhite, Color::SampleGrayDark, Color::SampleGrayMid, Color::SampleGrayMid, Color::SampleGrayLight };

            case Preview::ModeHighContrast:
                return { Color::SampleWhite, Color::SampleBlack, Color::SampleBlack, Color::SampleBlue, Color::SampleOrange };

            case Preview::ModeBitonal:
                return { Color::SampleWhite, Color::SampleBlack, Color::SampleBlack, Color::SampleBlack, Color::SampleWhite, true };

            case Preview::ModeCustomColors:
                return { Color::ReadingAmberPage, Color::ReadingAmberText, Color::ReadingAmberText, Color::ReadingAmberText, Color::ReadingAmberText };

            default:
                break;
        }

        return { Color::SampleWhite, Color::SampleGrayDark, Color::SampleGrayMid, Color::SampleBlueSoft, Color::SampleOrangeSoft };
    }

    /// Fills a rounded rectangle, which is not empty
    static void fill(QPainter* painter, const QRectF& rect, qreal radius, Color color)
    {
        if (rect.width() > 0.0 && rect.height() > 0.0)
        {
            PDFStylePainter::fill(painter, rect, PDFStylePainter::Radii(qMin(radius, qMin(rect.width(), rect.height()) / 2.0)), color);
        }
    }

    /// Draws a page of a layout preview with its top left corner at the point
    static void paintPage(QPainter* painter, const QPointF& topLeft, bool isLarge)
    {
        // Large page 40 x 56 (lines 17 / 28 / 20), small page 36 x 50 (lines 14 / 24 / 17)
        const QSizeF size = isLarge ? QSizeF(40.0, 56.0) : QSizeF(36.0, 50.0);
        const qreal titleWidth = isLarge ? 17.0 : 14.0;
        const qreal lineWidth = isLarge ? 28.0 : 24.0;
        const qreal lastWidth = isLarge ? 20.0 : 17.0;
        const QRectF page(topLeft, size);
        const PDFStylePainter::Radii radii(PAGE_RADIUS);

        PDFStylePainter::fill(painter, page, radii, Color::ReadingPaperPage);

        const qreal left = page.left() + PAGE_LINE_LEFT;
        fill(painter, QRectF(left, page.top() + 7.0, titleWidth, PAGE_LINE_HEIGHT), PAGE_LINE_RADIUS, Color::SampleGrayMid);
        for (const qreal top : { 13.0, 18.0, 23.0, 28.0 })
        {
            fill(painter, QRectF(left, page.top() + top, lineWidth, PAGE_LINE_HEIGHT), PAGE_LINE_RADIUS, Color::SampleGrayLight);
        }
        fill(painter, QRectF(left, page.top() + 33.0, lastWidth, PAGE_LINE_HEIGHT), PAGE_LINE_RADIUS, Color::SampleGrayLight);

        PDFStylePainter::strokeInside(painter, page, radii, Color::SampleGrayLight, 1.0);
    }

    /// Returns true, if the rectangle can be drawn into
    static bool isDrawable(const QRectF& rect)
    {
        return rect.isValid() && !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height());
    }

    /// Returns the scale, which fits a picture of the size into the rectangle (at most 1)
    static qreal fitScale(const QRectF& rect, const QSizeF& size)
    {
        return qMin<qreal>(1.0, qMin(rect.width() / size.width(), rect.height() / size.height()));
    }
};

PDFPreviewPainter::Preview PDFPreviewPainter::normalized(Preview preview)
{
    switch (preview)
    {
        case Preview::ModeNormal:
        case Preview::ModeInverted:
        case Preview::ModeGrayscale:
        case Preview::ModeHighContrast:
        case Preview::ModeBitonal:
        case Preview::ModeCustomColors:
        case Preview::LayoutSinglePage:
        case Preview::LayoutContinuous:
        case Preview::LayoutTwoPages:
        case Preview::LayoutTwoColumns:
            return preview;
    }

    return Preview::ModeNormal;
}

PDFPreviewPainter::LockedState PDFPreviewPainter::normalized(LockedState state)
{
    switch (state)
    {
        case LockedState::Locked:
        case LockedState::Wrong:
            return state;
    }

    return LockedState::Locked;
}

bool PDFPreviewPainter::isModePreview(Preview preview)
{
    switch (normalized(preview))
    {
        case Preview::LayoutSinglePage:
        case Preview::LayoutContinuous:
        case Preview::LayoutTwoPages:
        case Preview::LayoutTwoColumns:
            return false;

        default:
            break;
    }

    return true;
}

QString PDFPreviewPainter::getPreviewName(Preview preview)
{
    switch (normalized(preview))
    {
        case Preview::ModeNormal:
            return QCoreApplication::translate("PDFPreviewPainter", "Normal");
        case Preview::ModeInverted:
            return QCoreApplication::translate("PDFPreviewPainter", "Inverted");
        case Preview::ModeGrayscale:
            return QCoreApplication::translate("PDFPreviewPainter", "Grayscale");
        case Preview::ModeHighContrast:
            return QCoreApplication::translate("PDFPreviewPainter", "High contrast");
        case Preview::ModeBitonal:
            return QCoreApplication::translate("PDFPreviewPainter", "Bitonal");
        case Preview::ModeCustomColors:
            return QCoreApplication::translate("PDFPreviewPainter", "Custom colors");
        case Preview::LayoutSinglePage:
            return QCoreApplication::translate("PDFPreviewPainter", "Single page");
        case Preview::LayoutContinuous:
            return QCoreApplication::translate("PDFPreviewPainter", "Continuous");
        case Preview::LayoutTwoPages:
            return QCoreApplication::translate("PDFPreviewPainter", "Two pages");
        case Preview::LayoutTwoColumns:
            return QCoreApplication::translate("PDFPreviewPainter", "Two columns");
    }

    return QString();
}

QSize PDFPreviewPainter::getPreviewSize()
{
    return QSize(PDFPreviewPainterHelper::PREVIEW_WIDTH, PDFPreviewPainterHelper::PREVIEW_HEIGHT);
}

void PDFPreviewPainter::paintPreview(QPainter* painter, const QRectF& rect, Preview preview)
{
    using Helper = PDFPreviewPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    preview = normalized(preview);

    // A smaller rectangle scales the picture down evenly, a larger one keeps it
    const qreal scale = Helper::fitScale(rect, QSizeF(getPreviewSize()));
    if (!(scale > 0.0))
    {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(rect.topLeft());
    painter->scale(scale, scale);

    const QRectF shape(0.0, 0.0, rect.width() / scale, rect.height() / scale);
    const PDFStylePainter::Radii radii(Helper::PREVIEW_RADIUS);
    painter->setClipPath(PDFStylePainter::roundedPath(shape, radii), Qt::IntersectClip);

    if (isModePreview(preview))
    {
        // The page of the mode: the lines and the picture fill the width
        const Helper::ModeColors colors = Helper::getModeColors(preview);
        PDFStylePainter::fill(painter, shape, radii, colors.page);

        const qreal left = Helper::MODE_INSET;
        const qreal width = qMax<qreal>(0.0, shape.width() - 2.0 * Helper::MODE_INSET);
        Helper::fill(painter, QRectF(left, Helper::MODE_HEADING_TOP, qMin(Helper::MODE_HEADING_WIDTH, width), Helper::MODE_HEADING_HEIGHT), Helper::MODE_RADIUS, colors.heading);
        Helper::fill(painter, QRectF(left, Helper::MODE_LINE_1_TOP, width, Helper::MODE_LINE_HEIGHT), Helper::MODE_RADIUS, colors.lines);
        Helper::fill(painter, QRectF(left, Helper::MODE_LINE_2_TOP, qMin(Helper::MODE_LINE_2_WIDTH, width), Helper::MODE_LINE_HEIGHT), Helper::MODE_RADIUS, colors.lines);
        Helper::fill(painter, QRectF(left, Helper::MODE_LINE_3_TOP, qMin(Helper::MODE_LINE_3_WIDTH, width), Helper::MODE_LINE_HEIGHT), Helper::MODE_RADIUS, colors.lines);

        const qreal blockWidth = qMax<qreal>(0.0, (width - Helper::MODE_BLOCK_GAP) / 2.0);
        const QRectF block1(left, Helper::MODE_BLOCK_TOP, blockWidth, Helper::MODE_BLOCK_HEIGHT);
        const QRectF block2(left + blockWidth + Helper::MODE_BLOCK_GAP, Helper::MODE_BLOCK_TOP, blockWidth, Helper::MODE_BLOCK_HEIGHT);
        Helper::fill(painter, block1, Helper::MODE_RADIUS, colors.block1);
        Helper::fill(painter, block2, Helper::MODE_RADIUS, colors.block2);
        if (colors.outlinedBlock2 && !block2.isEmpty())
        {
            PDFStylePainter::strokeInside(painter, block2, PDFStylePainter::Radii(Helper::MODE_RADIUS), Color::SampleBlack, 1.0);
        }
    }
    else
    {
        // The pages lie in the track of the viewer, the composition of the
        // design is centered; pages outside of the picture are clipped
        PDFStylePainter::fill(painter, shape, radii, Color::BgTrack);
        const QPointF offset(qFloor((shape.width() - Helper::PREVIEW_WIDTH) / 2.0), qFloor((shape.height() - Helper::PREVIEW_HEIGHT) / 2.0));

        switch (preview)
        {
            case Preview::LayoutSinglePage:
                Helper::paintPage(painter, offset + QPointF(37.0, 16.0), true);
                break;

            case Preview::LayoutContinuous:
                Helper::paintPage(painter, offset + QPointF(37.0, -32.0), true);
                Helper::paintPage(painter, offset + QPointF(37.0, 30.0), true);
                break;

            case Preview::LayoutTwoPages:
                Helper::paintPage(painter, offset + QPointF(20.0, 19.0), false);
                Helper::paintPage(painter, offset + QPointF(59.0, 19.0), false);
                break;

            case Preview::LayoutTwoColumns:
                for (const qreal top : { -26.0, 30.0, 86.0 })
                {
                    Helper::paintPage(painter, offset + QPointF(20.0, top), false);
                    Helper::paintPage(painter, offset + QPointF(59.0, top), false);
                }
                break;

            default:
                break;
        }
    }

    PDFStylePainter::strokeInside(painter, shape, radii, Color::BorderStrong, 1.0);
    painter->restore();
}

QSize PDFPreviewPainter::getLockedDocumentSize()
{
    return QSize(PDFPreviewPainterHelper::LOCKED_EXTENT, PDFPreviewPainterHelper::LOCKED_EXTENT);
}

QMargins PDFPreviewPainter::getLockedDocumentOverflowMargins()
{
    return QMargins(0, 0, PDFPreviewPainterHelper::LOCKED_OVERFLOW_RIGHT, PDFPreviewPainterHelper::LOCKED_OVERFLOW_BOTTOM);
}

void PDFPreviewPainter::paintLockedDocument(QPainter* painter, const QRectF& rect, LockedState state, bool paintOverflow)
{
    using Helper = PDFPreviewPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const qreal extent = Helper::LOCKED_EXTENT;
    const qreal scale = Helper::fitScale(rect, QSizeF(extent, extent));
    if (!(scale > 0.0))
    {
        return;
    }

    // The Wrong state replaces the blue hue by the red one and the badge by icon/danger
    const bool isWrong = normalized(state) == LockedState::Wrong;
    const Color backdropColor = isWrong ? Color::AccentRedBg : Color::AccentBlueBg;
    const Color borderColor = isWrong ? Color::AccentRedBorder : Color::AccentBlueBorder;
    const Color strongColor = isWrong ? Color::AccentRedBorderStrong : Color::AccentBlueBorderStrong;
    const Color badgeColor = isWrong ? Color::IconDanger : Color::BgAccent;
    const QColor shadowColor = PDFStylePainter::color(Color::EffectShadow);
    const QPointF shadowOffset(0.0, Helper::LOCKED_SHADOW_OFFSET);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QPointF origin(rect.left() + qFloor((rect.width() - extent * scale) / 2.0), rect.top() + qFloor((rect.height() - extent * scale) / 2.0));
    if (paintOverflow)
    {
        // The shadow of the badge outside of the illustration (scaled with it)
        const QMarginsF overflow = QMarginsF(getLockedDocumentOverflowMargins()) * scale;
        painter->setClipRect(QRectF(origin, QSizeF(extent * scale, extent * scale)).marginsAdded(overflow).united(rect), Qt::IntersectClip);
    }
    else
    {
        painter->setClipRect(rect, Qt::IntersectClip);
    }
    painter->translate(origin);
    painter->scale(scale, scale);

    // Backdrop and sparks
    PDFStylePainter::fillEllipse(painter, QRectF(0.0, 0.0, extent, extent), PDFStylePainter::color(backdropColor));
    const QColor sparkColor = PDFStylePainter::color(strongColor);
    PDFStylePainter::fillEllipse(painter, QRectF(10.0, 44.0, 8.0, 8.0), sparkColor);
    PDFStylePainter::fillEllipse(painter, QRectF(140.0, 30.0, 6.0, 6.0), sparkColor);
    PDFStylePainter::fillEllipse(painter, QRectF(20.0, 122.0, 5.0, 5.0), sparkColor);

    // The sheet behind, turned by 8 degrees counterclockwise around its top left corner
    const PDFStylePainter::Radii sheetRadii(8.0);
    painter->save();
    painter->translate(28.0, 40.0);
    painter->rotate(-8.0);
    const QRectF backSheet(0.0, 0.0, 78.0, 100.0);
    PDFStylePainter::fill(painter, backSheet, sheetRadii, Color::BgSurface);
    PDFStylePainter::strokeInside(painter, backSheet, sheetRadii, borderColor, 1.0);
    painter->restore();

    // The sheet with its shadow, the heading and the lines of the text
    const QRectF sheet(40.0, 26.0, 82.0, 104.0);
    PDFStylePainter::shadow(painter, sheet, sheetRadii, shadowOffset, Helper::LOCKED_SHADOW_BLUR, shadowColor);
    PDFStylePainter::fill(painter, sheet, sheetRadii, Color::BgSurface);
    PDFStylePainter::fill(painter, QRectF(sheet.left() + 12.0, sheet.top() + 14.0, 34.0, 6.0), PDFStylePainter::Radii(3.0), strongColor);
    struct Line
    {
        qreal top;
        qreal width;
    };
    for (const Line& line : { Line{ 30.0, 58.0 }, Line{ 40.0, 58.0 }, Line{ 50.0, 44.0 }, Line{ 64.0, 58.0 }, Line{ 74.0, 36.0 } })
    {
        PDFStylePainter::fill(painter, QRectF(sheet.left() + 12.0, sheet.top() + line.top, line.width, 4.0), PDFStylePainter::Radii(2.0), Color::BgMuted);
    }
    PDFStylePainter::strokeInside(painter, sheet, sheetRadii, borderColor, 1.0);

    // The badge with the ring bg/surface of 4 px outside and its shadow
    const QRectF badge(90.0, 90.0, 60.0, 60.0);
    const QRectF badgeWithRing = badge.adjusted(-4.0, -4.0, 4.0, 4.0);
    PDFStylePainter::shadow(painter, badgeWithRing, PDFStylePainter::Radii(badgeWithRing.width() / 2.0), shadowOffset, Helper::LOCKED_SHADOW_BLUR, shadowColor);
    PDFStylePainter::fillEllipse(painter, badgeWithRing, PDFStylePainter::color(Color::BgSurface));
    PDFStylePainter::fillEllipse(painter, badge, PDFStylePainter::color(badgeColor));
    PDFStylePainter::icon(painter, PDFStylePainter::centered(badge, QSizeF(28.0, 28.0)), QStringLiteral("lock"), Color::IconOnAccent);

    painter->restore();
}

QSize PDFPreviewPainter::getLogoSize()
{
    return QSize(PDFPreviewPainterHelper::LOGO_EXTENT, PDFPreviewPainterHelper::LOGO_EXTENT);
}

QString PDFPreviewPainter::getLogoResourcePath()
{
    return QStringLiteral(":/pdf4qtfluent/logo/pdf4qt-logo.svg");
}

void PDFPreviewPainter::paintLogo(QPainter* painter, const QRectF& rect)
{
    if (!painter || !PDFPreviewPainterHelper::isDrawable(rect))
    {
        return;
    }

    // The logo is scaled freely (the description of the component), its aspect ratio is kept
    const QSizeF size = QSizeF(getLogoSize()).scaled(rect.size(), Qt::KeepAspectRatio);
    const QRectF target = PDFStylePainter::centered(rect, size);

    // The picture of the size of the device pixels (the device, into which the
    // painter really paints: a widget rendered into an image is redirected)
    const QPaintEngine* engine = painter->paintEngine();
    const QPaintDevice* device = (engine && engine->paintDevice()) ? engine->paintDevice() : painter->device();
    const qreal devicePixelRatio = device ? qMax<qreal>(device->devicePixelRatioF(), 1.0) : 1.0;
    // The size is bounded before it is converted to integers (a huge or a
    // non-finite scale of the painter is drawn from the SVG)
    const QTransform transform = painter->combinedTransform();
    const qreal pixelWidth = target.width() * qAbs(transform.m11()) * devicePixelRatio;
    const qreal pixelHeight = target.height() * qAbs(transform.m22()) * devicePixelRatio;
    const bool isCached = transform.type() <= QTransform::TxScale && pixelWidth > 0.0 && pixelHeight > 0.0 &&
                          pixelWidth <= PDFPreviewPainterHelper::LOGO_CACHE_MAX_EXTENT && pixelHeight <= PDFPreviewPainterHelper::LOGO_CACHE_MAX_EXTENT;
    const QSize pixelSize = isCached ? QSize(qCeil(pixelWidth), qCeil(pixelHeight)) : QSize();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    if (isCached)
    {
        const QImage image = PDFPreviewPainterHelper::getLogoImage(pixelSize);
        if (!image.isNull())
        {
            painter->drawImage(target, image);
        }
    }
    else
    {
        QSvgRenderer renderer(getLogoResourcePath());
        if (renderer.isValid())
        {
            renderer.render(painter, target);
        }
    }
    painter->restore();
}

}   // namespace pdf
