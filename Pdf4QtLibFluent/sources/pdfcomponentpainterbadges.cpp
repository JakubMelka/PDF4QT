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

// Drawing of the small painted elements of the pages of the application, which
// are frames of Main page (icon badge, status badge, fact pill, avatar, status
// row, tinted card). The class is declared in pdfcomponentpainter.h; this file
// holds their geometry constants.

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QPainter>
#include <QtMath>

namespace pdf
{

/// Geometry constants of the badges, pills, avatars and status rows
class PDFComponentBadgePainterHelper
{
public:
    PDFComponentBadgePainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using State = PDFComponentPainter::State;
    using Role = PDFAccentHueColors::Role;

    /// Largest extent of a badge or an avatar (a hostile value is bounded)
    static constexpr int MAX_EXTENT = 1024;

    // Status badge: border 1, paddings 3 / 10 / 3 / 8, icon 12, gap 5, height 21
    static constexpr int STATUS_HEIGHT = 21;
    static constexpr int STATUS_BORDER = 1;
    static constexpr int STATUS_LEADING = 8;
    static constexpr int STATUS_TRAILING = 10;
    static constexpr int STATUS_ICON = 12;
    static constexpr int STATUS_GAP = 5;

    // Fact pill: border 1, paddings 9 / 11 inside of it, icon 13, gap 6, height 25
    // (pill-Open source 111:2227: the icon at 10, the label 12 px before the end)
    static constexpr int PILL_HEIGHT = 25;
    static constexpr int PILL_LEADING = 1 + 9;
    static constexpr int PILL_TRAILING = 11 + 1;
    static constexpr int PILL_ICON = 13;
    static constexpr int PILL_GAP = 6;

    // Status row: paddings 10 / 8, dot 8, gaps 10, height 46, title 16 and
    // subtitle 13 with the gap 1 (30 px centered)
    static constexpr int ROW_HEIGHT = 46;
    static constexpr int ROW_LEADING = 10;
    static constexpr int ROW_TRAILING = 8;
    static constexpr int ROW_DOT = 8;
    static constexpr int ROW_GAP = 10;
    static constexpr int ROW_TITLE_LINE = 16;
    static constexpr int ROW_SUBTITLE_LINE = 13;
    static constexpr int ROW_TEXT_GAP = 1;
    static constexpr int MIN_TEXT = 40;

    static QFont statusFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold); }
    static QFont pillFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium); }
    static QFont titleFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold); }
    static QFont subtitleFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular); }

    static int textWidth(const QString& text, const QFont& font) { return qCeil(PDFStylePainter::textWidth(text, font)); }
    static int extent(int value) { return qBound(0, value, MAX_EXTENT); }
    static bool isDrawable(const QRectF& rect) { return rect.isValid() && !rect.isEmpty() && qIsFinite(rect.width()) && qIsFinite(rect.height()); }
    static Color text(const State& state, Color color) { return state.isEnabled() ? color : Color::TextDisabled; }
    static Color icon(const State& state, Color color) { return state.isEnabled() ? color : Color::IconDisabled; }
    static bool isValidColor(Color color) { return static_cast<int>(color) >= 0 && static_cast<int>(color) < static_cast<int>(PDFThemeColors::ColorCount); }

    /// Returns the rectangle given in the left-to-right layout of the outer
    /// rectangle: x from the left edge, the height centered in the outer rectangle
    static QRectF line(const QRectF& outer, qreal left, qreal width, qreal height)
    {
        return QRectF(left, outer.top() + qFloor((outer.height() - height) / 2.0), qMax<qreal>(0.0, width), height);
    }
};

QSize PDFComponentPainter::getIconBadgeSize(const IconBadge& badge)
{
    const int extent = PDFComponentBadgePainterHelper::extent(badge.extent);
    return QSize(extent, extent);
}

void PDFComponentPainter::paintIconBadge(QPainter* painter, const QRectF& rect, const IconBadge& badge, const State& state)
{
    using Helper = PDFComponentBadgePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    // The badge keeps its size, a smaller rectangle makes it smaller
    const qreal extent = qMin<qreal>(Helper::extent(badge.extent), qMin(rect.width(), rect.height()));
    if (extent <= 0.0)
    {
        return;
    }

    const QRectF box = alignedCentered(rect, QSizeF(extent, extent));
    const PDFStylePainter::Radii radii(badge.radius < 0.0 || !qIsFinite(badge.radius) ? extent / 2.0 : qMin<qreal>(badge.radius, extent / 2.0));

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (badge.fill.has_value() && Helper::isValidColor(*badge.fill))
    {
        PDFStylePainter::fill(painter, box, radii, *badge.fill);
    }
    if (badge.border.has_value() && Helper::isValidColor(*badge.border))
    {
        PDFStylePainter::strokeInside(painter, box, radii, *badge.border, 1.0);
    }

    const qreal iconExtent = qMin<qreal>(Helper::extent(badge.iconExtent), extent);
    if (iconExtent <= 0.0)
    {
        return;
    }

    const QRectF iconRect = alignedCentered(box, QSizeF(iconExtent, iconExtent));
    if (badge.logo)
    {
        PDFPreviewPainter::paintLogo(painter, iconRect);
    }
    else if (!badge.iconName.isEmpty())
    {
        const Color iconColor = Helper::isValidColor(badge.iconColor) ? badge.iconColor : Color::IconDefault;
        PDFStylePainter::icon(painter, iconRect, badge.iconName, Helper::icon(state, iconColor));
    }
}

QSize PDFComponentPainter::getStatusBadgeSize(const StatusBadge& badge)
{
    using Helper = PDFComponentBadgePainterHelper;
    const int textWidth = Helper::textWidth(badge.text, Helper::statusFont());
    return QSize(2 * Helper::STATUS_BORDER + Helper::STATUS_LEADING + Helper::STATUS_ICON + Helper::STATUS_GAP + textWidth + Helper::STATUS_TRAILING, Helper::STATUS_HEIGHT);
}

void PDFComponentPainter::paintStatusBadge(QPainter* painter, const QRectF& rect, const StatusBadge& badge, const State& state)
{
    using Helper = PDFComponentBadgePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PDFAccentHue hue = normalized(badge.hue);
    const PDFStylePainter::Radii radii(rect.height() / 2.0);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    PDFStylePainter::fill(painter, rect, radii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background));
    PDFStylePainter::strokeInside(painter, rect, radii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Border), 1.0);

    const Color color = PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon);
    const qreal iconLeft = rect.left() + Helper::STATUS_BORDER + Helper::STATUS_LEADING;
    const QRectF iconRect = Helper::line(rect, iconLeft, Helper::STATUS_ICON, Helper::STATUS_ICON);
    if (!badge.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, state.visualRect(rect, iconRect), badge.iconName, Helper::icon(state, color));
    }

    const qreal textLeft = iconRect.right() + Helper::STATUS_GAP;
    const qreal textWidth = rect.right() - Helper::STATUS_BORDER - Helper::STATUS_TRAILING - textLeft;
    drawElidedText(painter, state, state.visualRect(rect, QRectF(textLeft, rect.top(), qMax<qreal>(0.0, textWidth), rect.height())), badge.text, Helper::statusFont(),
                   Helper::text(state, color));
}

QSize PDFComponentPainter::getFactPillSize(const FactPill& pill)
{
    using Helper = PDFComponentBadgePainterHelper;
    const int textWidth = Helper::textWidth(pill.text, Helper::pillFont());
    return QSize(Helper::PILL_LEADING + Helper::PILL_ICON + Helper::PILL_GAP + textWidth + Helper::PILL_TRAILING, Helper::PILL_HEIGHT);
}

void PDFComponentPainter::paintFactPill(QPainter* painter, const QRectF& rect, const FactPill& pill, const State& state)
{
    using Helper = PDFComponentBadgePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainter::Radii radii(rect.height() / 2.0);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
    PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);

    const QRectF iconRect = Helper::line(rect, rect.left() + Helper::PILL_LEADING, Helper::PILL_ICON, Helper::PILL_ICON);
    if (!pill.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, state.visualRect(rect, iconRect), pill.iconName, Helper::icon(state, Color::IconMuted));
    }

    const qreal textLeft = iconRect.right() + Helper::PILL_GAP;
    const qreal textWidth = rect.right() - Helper::PILL_TRAILING - textLeft;
    drawElidedText(painter, state, state.visualRect(rect, QRectF(textLeft, rect.top(), qMax<qreal>(0.0, textWidth), rect.height())), pill.text, Helper::pillFont(),
                   Helper::text(state, Color::TextSecondary));
}

QSize PDFComponentPainter::getAvatarSize(const Avatar& avatar)
{
    const int extent = PDFComponentBadgePainterHelper::extent(avatar.extent);
    return QSize(extent, extent);
}

void PDFComponentPainter::paintAvatar(QPainter* painter, const QRectF& rect, const Avatar& avatar, const State& state)
{
    using Helper = PDFComponentBadgePainterHelper;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const qreal extent = qMin<qreal>(Helper::extent(avatar.extent), qMin(rect.width(), rect.height()));
    if (extent <= 0.0)
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFAccentHue hue = normalized(avatar.hue);
    const QRectF circle = alignedCentered(rect, QSizeF(extent, extent));
    const PDFStylePainter::Radii radii(extent / 2.0);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(painter, circle, radii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background));
    PDFStylePainter::strokeInside(painter, circle, radii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Border), 1.0);

    // The initials are data (at most three characters, never a whole name)
    const QFont font = PDFThemeFonts::getFont(QFont::Bold, qBound(1, avatar.fontPixelSize, Helper::MAX_EXTENT));
    PDFStylePainter::text(painter, circle, Qt::AlignCenter | Qt::TextSingleLine, avatar.initials.left(3), font,
                          PDFStylePainter::color(Helper::text(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon))));
}

QSize PDFComponentPainter::getStatusRowSize(const StatusRow& row)
{
    using Helper = PDFComponentBadgePainterHelper;
    const int textWidth = qMax(Helper::textWidth(row.title, Helper::titleFont()), Helper::textWidth(row.subtitle, Helper::subtitleFont()));
    return QSize(getStatusRowMinimumSize(row).width() - Helper::MIN_TEXT + textWidth, Helper::ROW_HEIGHT);
}

QSize PDFComponentPainter::getStatusRowMinimumSize(const StatusRow& row)
{
    using Helper = PDFComponentBadgePainterHelper;
    const int statusWidth = row.status.isEmpty() ? 0 : getTagSize(Tag{ row.status }).width() + Helper::ROW_GAP;
    return QSize(Helper::ROW_LEADING + Helper::ROW_DOT + Helper::ROW_GAP + Helper::MIN_TEXT + statusWidth + Helper::ROW_TRAILING, Helper::ROW_HEIGHT);
}

void PDFComponentPainter::paintStatusRow(QPainter* painter, const QRectF& rect, const StatusRow& row, const State& state)
{
    using Helper = PDFComponentBadgePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    const QRectF dot = Helper::line(rect, rect.left() + Helper::ROW_LEADING, Helper::ROW_DOT, Helper::ROW_DOT);
    const Color dotColor = !state.isEnabled() ? Color::BorderDefault : (row.active ? Color::BgAccent : Color::BorderStrong);
    PDFStylePainter::fillEllipse(painter, state.visualRect(rect, dot), PDFStylePainter::color(dotColor));

    // The status tag at the trailing edge (a label, not interactive)
    qreal textRight = rect.right() - Helper::ROW_TRAILING;
    if (!row.status.isEmpty())
    {
        const QSize tagSize = getTagSize(Tag{ row.status });
        const QRectF tagRect = Helper::line(rect, textRight - tagSize.width(), tagSize.width(), tagSize.height());
        if (tagRect.left() > dot.right())
        {
            paintTag(painter, state.visualRect(rect, tagRect), Tag{ row.status }, State(state.flags & QStyle::State_Enabled, state.direction));
        }
        textRight = tagRect.left() - Helper::ROW_GAP;
    }

    // The title and the subtitle (data), 30 px centered
    const qreal textLeft = dot.right() + Helper::ROW_GAP;
    const qreal textWidth = qMax<qreal>(0.0, textRight - textLeft);
    const qreal top = rect.top() + qFloor((rect.height() - (Helper::ROW_TITLE_LINE + Helper::ROW_TEXT_GAP + Helper::ROW_SUBTITLE_LINE)) / 2.0);
    drawElidedText(painter, state, state.visualRect(rect, QRectF(textLeft, top, textWidth, Helper::ROW_TITLE_LINE)), row.title, Helper::titleFont(),
                   Helper::text(state, Color::TextPrimary), TextKind::Data);
    drawElidedText(painter, state, state.visualRect(rect, QRectF(textLeft, top + Helper::ROW_TITLE_LINE + Helper::ROW_TEXT_GAP, textWidth, Helper::ROW_SUBTITLE_LINE)), row.subtitle,
                   Helper::subtitleFont(), Helper::text(state, Color::TextMuted), TextKind::Data);
}

void PDFComponentPainter::paintTintedCard(QPainter* painter, const QRectF& rect, std::optional<PDFAccentHue> hue)
{
    if (!painter || !PDFComponentBadgePainterHelper::isDrawable(rect))
    {
        return;
    }

    const PDFStylePainter::Radii radii(TINTED_CARD_RADIUS);
    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (hue.has_value())
    {
        const PDFAccentHue validHue = normalized(*hue);
        PDFStylePainter::fill(painter, rect, radii, PDFAccentHueColors::getColor(validHue, PDFAccentHueColors::Role::Background));
        PDFStylePainter::strokeInside(painter, rect, radii, PDFAccentHueColors::getColor(validHue, PDFAccentHueColors::Role::Border), 1.0);
    }
    else
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(painter, rect, radii, PDFThemeColors::BorderDefault, 1.0);
    }
}

}   // namespace pdf
