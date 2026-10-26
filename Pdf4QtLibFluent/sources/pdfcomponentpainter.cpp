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
#include "pdfthemefonts.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QtMath>

namespace pdf
{

/// Geometry constants of the cards of the specification
class PDFComponentPainterHelper
{
public:
    PDFComponentPainterHelper() = delete;

    // Tag 116:2353: border 1 + padding 9 at the sides, 1 + 3 at the top and
    // the bottom, label of a fixed line of 13 px
    static constexpr int TAG_SIDE = 10;
    static constexpr int TAG_LINE = 13;
    static constexpr int TAG_HEIGHT = 21;

    // Text link 49:152: padding 2 at the sides, line of Semi Bold 13 (auto = 16)
    static constexpr int LINK_SIDE = 2;
    static constexpr int LINK_HEIGHT = 16;
    static constexpr qreal LINK_RADIUS = 4.0;

    // Star button 64:569: radius 6, icon 16 in both sizes
    static constexpr qreal STAR_RADIUS = 6.0;
    static constexpr int STAR_ICON = 16;

    // Rows and cards: nested widgets 12 px from the trailing edge, gaps of 12
    // (rows) and 8 (cards), radius of a card 12
    static constexpr int ROW_TRAILING_MARGIN = 12;
    static constexpr int ROW_TRAILING_SPACING = 12;
    static constexpr int CARD_TRAILING_MARGIN = 12;
    static constexpr int CARD_TRAILING_SPACING = 8;
    static constexpr qreal CARD_RADIUS = 12.0;

    // Color swatch 206:805: circle 28, color 20 in the middle, slash of 1.5 px
    // at 45 degrees, 20 px long. The card gives the origin of the line (3, 17)
    // of the color; Figma draws the stroke of a line shifted by half of its width
    // across the line (SVG export of 206:781), so the drawn center line starts
    // at (3 - 0.75 / sqrt 2, 17 - 0.75 / sqrt 2) = (2.47, 16.47).
    static constexpr int SWATCH_EXTENT = 28;
    static constexpr int SWATCH_COLOR = 20;
    static constexpr qreal SWATCH_SLASH_WIDTH = 1.5;
    static constexpr qreal SWATCH_SLASH_SHIFT = SWATCH_SLASH_WIDTH / 2.0 / M_SQRT2;
    static constexpr qreal SWATCH_SLASH_X = 3.0 - SWATCH_SLASH_SHIFT;
    static constexpr qreal SWATCH_SLASH_Y = 17.0 - SWATCH_SLASH_SHIFT;
    static constexpr qreal SWATCH_SLASH_LENGTH = 20.0;

    // Icon choice of the collections editor (283:1888, icons 284:2227): the
    // Tool icon 32 in the padding of 2 (36), the selected one in the ring of 2
    // border/focus (40), radius 10
    static constexpr int ICON_CHOICE = 36;
    static constexpr int ICON_CHOICE_SELECTED = 40;
    static constexpr qreal ICON_CHOICE_RADIUS = 10.0;
    static constexpr qreal ICON_CHOICE_RING = 2.0;

    // Page thumbnail 78:892: radius 2, border 1 inside, shadow (0, 1, 2, 0)
    static constexpr qreal THUMBNAIL_RADIUS = 2.0;
    static constexpr qreal THUMBNAIL_SHADOW_OFFSET = 1.0;
    static constexpr qreal THUMBNAIL_SHADOW_BLUR = 2.0;

    /// Returns the line without the spaces and line separators at its end
    static QString trimmedLineEnd(QString line)
    {
        while (!line.isEmpty() && (line.back().isSpace() || line.back() == QChar::LineSeparator))
        {
            line.chop(1);
        }
        return line;
    }

    /// Layout of the placeholder lines of a page thumbnail
    struct ThumbnailGeometry
    {
        int width;          ///< Width of the frame
        int height;         ///< Height of the frame
        int paddingTop;     ///< Padding at the top (inside of the border)
        int paddingSide;    ///< Padding at the sides (inside of the border)
        int gap;            ///< Gap between the lines
        int titleHeight;    ///< Height of the title line (text/muted)
        int lineHeight;     ///< Height of a line (border/strong)
        int fullLines;      ///< Number of the lines over the whole width
        int lastLineWidth;  ///< Width of the last, shorter line
    };

    static ThumbnailGeometry getThumbnailGeometry(PDFComponentPainter::ThumbnailSize size)
    {
        switch (size)
        {
            case PDFComponentPainter::ThumbnailSize::Small:
                break;

            case PDFComponentPainter::ThumbnailSize::Medium:
                return { 56, 74, 9, 8, 4, 4, 2, 5, 25 };

            case PDFComponentPainter::ThumbnailSize::Large:
                return { 80, 106, 13, 11, 5, 5, 3, 7, 36 };
        }

        return { 32, 42, 5, 4, 2, 2, 1, 2, 14 };
    }
};

PDFComponentPainter::State::State(QStyle::State flags, Qt::LayoutDirection direction) :
    flags(flags),
    direction(direction)
{

}

QRectF PDFComponentPainter::State::visualRect(const QRectF& outer, const QRectF& logical) const
{
    if (!isRightToLeft())
    {
        return logical;
    }

    return QRectF(2.0 * outer.left() + outer.width() - logical.left() - logical.width(), logical.top(), logical.width(), logical.height());
}

PDFComponentPainter::DirectionGuard::DirectionGuard(QPainter* painter, const State& state) :
    m_painter(painter)
{
    if (m_painter)
    {
        m_direction = m_painter->layoutDirection();
        m_painter->setLayoutDirection(state.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight);
    }
}

PDFComponentPainter::DirectionGuard::~DirectionGuard()
{
    if (m_painter)
    {
        m_painter->setLayoutDirection(m_direction);
    }
}

QRectF PDFComponentPainter::alignedCentered(const QRectF& outer, const QSizeF& size)
{
    return QRectF(qFloor(outer.left() + (outer.width() - size.width()) / 2.0),
                  qFloor(outer.top() + (outer.height() - size.height()) / 2.0),
                  size.width(), size.height());
}

QString PDFComponentPainter::elidedText(const QString& text, const QFont& font, qreal width, Qt::TextElideMode mode)
{
    if (text.isEmpty() || !(width > 0.0))
    {
        return QString();
    }

    const QString elided = QFontMetricsF(font).elidedText(text, mode, width);
    if (elided == text)
    {
        return elided;
    }

    // QFontMetricsF keeps the space before the ellipsis ("jumps over the …"),
    // the design has none ("jumps over the…")
    const QChar ellipsis(0x2026);
    const qsizetype index = elided.indexOf(ellipsis);
    if (index <= 0)
    {
        return elided;
    }

    qsizetype start = index;
    while (start > 0 && elided.at(start - 1).isSpace())
    {
        --start;
    }

    return start > 0 ? elided.left(start) + elided.mid(index) : elided;
}

QStringList PDFComponentPainter::wrapText(const QString& text, const QFont& font, qreal width, int maxLines)
{
    QStringList lines;
    if (text.isEmpty() || !(width > 0.0) || !qIsFinite(width) || maxLines <= 0)
    {
        return lines;
    }

    QString layoutText = text;
    layoutText.replace(QLatin1Char('\n'), QChar::LineSeparator);

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout layout(layoutText, font);
    layout.setTextOption(option);
    layout.beginLayout();
    for (QTextLine textLine = layout.createLine(); textLine.isValid(); textLine = layout.createLine())
    {
        textLine.setLineWidth(width);
        const int start = textLine.textStart();

        if (lines.size() == maxLines - 1)
        {
            // The last allowed line takes the rest of the text, elided
            QString rest = layoutText.mid(start);
            rest.replace(QChar::LineSeparator, QLatin1Char(' '));
            lines << elidedText(PDFComponentPainterHelper::trimmedLineEnd(rest), font, width);
            break;
        }

        lines << PDFComponentPainterHelper::trimmedLineEnd(layoutText.mid(start, textLine.textLength()));
    }
    layout.endLayout();
    return lines;
}

qreal PDFComponentPainter::getLongestWordWidth(const QString& text, const QFont& font)
{
    if (text.isEmpty())
    {
        return 0.0;
    }

    // The parts between the opportunities of a line break (UAX #14, as QTextLayout breaks the lines)
    const QFontMetricsF metrics(font);
    QTextBoundaryFinder finder(QTextBoundaryFinder::Line, text);
    qreal longest = 0.0;
    qsizetype start = 0;
    for (qsizetype position = finder.toNextBoundary(); position > 0; position = finder.toNextBoundary())
    {
        const QString word = PDFComponentPainterHelper::trimmedLineEnd(text.mid(start, position - start));
        longest = qMax(longest, metrics.horizontalAdvance(word));
        start = position;
    }

    return longest;
}

Qt::LayoutDirection PDFComponentPainter::getDataTextDirection(const QString& text)
{
    // QString::isRightToLeft implements P2 and P3 (it skips isolated parts)
    return text.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight;
}

void PDFComponentPainter::drawDataText(QPainter* painter, const QRectF& rect, int flags, const QString& text, const QFont& font, const QColor& color, Qt::LayoutDirection direction)
{
    if (!painter || text.isEmpty())
    {
        return;
    }

    const Qt::LayoutDirection previousDirection = painter->layoutDirection();
    const Qt::LayoutDirection baseDirection = (direction == Qt::LeftToRight || direction == Qt::RightToLeft) ? direction : getDataTextDirection(text);

    // The alignment of the component is kept: a relative alignment is resolved
    // in the direction of the painter of the caller before the direction changes
    // (no horizontal flag is the leading edge, as QPainter::drawText takes it)
    int absoluteFlags = flags;
    const Qt::Alignment horizontal = Qt::Alignment(flags) & Qt::AlignHorizontal_Mask;
    if (!(flags & Qt::AlignAbsolute) && (horizontal == Qt::Alignment() || (horizontal & (Qt::AlignLeft | Qt::AlignRight))))
    {
        const Qt::LayoutDirection callerDirection = previousDirection == Qt::RightToLeft ? Qt::RightToLeft : Qt::LeftToRight;
        const Qt::Alignment visual = QStyle::visualAlignment(callerDirection, horizontal == Qt::Alignment() ? Qt::Alignment(Qt::AlignLeft) : horizontal);
        absoluteFlags = (flags & ~int(Qt::AlignHorizontal_Mask)) | int(visual & Qt::AlignHorizontal_Mask) | int(Qt::AlignAbsolute);
    }

    painter->setLayoutDirection(baseDirection);
    PDFStylePainter::text(painter, rect, absoluteFlags, text, font, color);
    painter->setLayoutDirection(previousDirection);
}

QString PDFComponentPainter::elidedTextOfKind(const QString& text, const QFont& font, qreal width, TextKind kind)
{
    switch (kind)
    {
        case TextKind::FileName:
            return elidedFileName(text, font, width);

        case TextKind::Interface:
        case TextKind::Data:
            break;
    }
    return elidedText(text, font, width);
}

void PDFComponentPainter::drawElidedText(QPainter* painter, const State& state, const QRectF& visualRect, const QString& text, const QFont& font,
                                         PDFThemeColors::Color color, TextKind kind, bool isTrailing)
{
    if (!painter || !(visualRect.width() > 0.0) || !(visualRect.height() > 0.0) || text.isEmpty())
    {
        return;
    }

    const int alignment = Qt::AlignVCenter | Qt::TextSingleLine | (isTrailing ? state.trailingAlignment() : state.leadingAlignment());
    const QColor textColor = PDFStylePainter::color(color);
    switch (kind)
    {
        case TextKind::Data:
        case TextKind::FileName:
            // The direction of the whole text: the elision may cut its first strong character off (review C18)
            drawDataText(painter, visualRect, alignment, elidedTextOfKind(text, font, visualRect.width(), kind), font, textColor, getDataTextDirection(text));
            return;

        case TextKind::Interface:
            break;
    }

    PDFStylePainter::text(painter, visualRect, alignment, elidedText(text, font, visualRect.width()), font, textColor);
}

void PDFComponentPainter::drawScaledPixmap(QPainter* painter, const QRectF& target, const QPixmap& pixmap)
{
    const bool isTargetValid = !target.isEmpty() && qIsFinite(target.left()) && qIsFinite(target.top()) && qIsFinite(target.width()) && qIsFinite(target.height());
    if (!painter || pixmap.isNull() || pixmap.width() <= 0 || pixmap.height() <= 0 || !isTargetValid)
    {
        return;
    }

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The device, into which the painter really paints (a widget rendered into
    // an image by QWidget::render is redirected), gives the pixel ratio
    const QPaintEngine* engine = painter->paintEngine();
    const QPaintDevice* device = (engine && engine->paintDevice()) ? engine->paintDevice() : painter->device();
    const qreal devicePixelRatio = device ? qMax<qreal>(device->devicePixelRatioF(), 1.0) : 1.0;
    const QTransform transform = painter->combinedTransform();
    const bool isScaleOnly = transform.type() <= QTransform::TxScale;
    // The size is bounded by the picture before it is converted to integers (a
    // huge or a non-finite scale draws the picture itself)
    const qreal deviceWidth = target.width() * qAbs(transform.m11()) * devicePixelRatio;
    const qreal deviceHeight = target.height() * qAbs(transform.m22()) * devicePixelRatio;
    const bool isBounded = deviceWidth > 0.0 && deviceHeight > 0.0 && deviceWidth < pixmap.width() && deviceHeight < pixmap.height();
    const QSize deviceSize = isBounded ? QSize(qCeil(deviceWidth), qCeil(deviceHeight)) : QSize();
    const bool isDownscaled = isBounded && deviceSize.width() < pixmap.width() && deviceSize.height() < pixmap.height();

    if (isScaleOnly && isDownscaled)
    {
        // The copy of the size of the device pixels is scaled once
        const QString key = QStringLiteral("pdf4qt-scaled-%1-%2x%3").arg(pixmap.cacheKey()).arg(deviceSize.width()).arg(deviceSize.height());
        QPixmap scaled;
        if (!QPixmapCache::find(key, &scaled))
        {
            scaled = pixmap.scaled(deviceSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QPixmapCache::insert(key, scaled);
        }
        painter->drawPixmap(target, scaled, QRectF(QPointF(0.0, 0.0), QSizeF(scaled.size())));
    }
    else
    {
        painter->drawPixmap(target, pixmap, QRectF(QPointF(0.0, 0.0), QSizeF(pixmap.size())));
    }
}

void PDFComponentPainter::paintFocusRingOutside(QPainter* painter, const QRectF& shape, const PDFStylePainter::Radii& radii)
{
    if (!painter || shape.isEmpty())
    {
        return;
    }

    PDFStylePainter::focusRing(painter, shape, radii, false);
}

void PDFComponentPainter::paintFocusRingInside(QPainter* painter, const QRectF& shape, const PDFStylePainter::Radii& radii)
{
    PDFStylePainter::strokeInside(painter, shape, radii, PDFThemeColors::BorderFocus, 2.0);
}

QMargins PDFComponentPainter::getRowTrailingMargins()
{
    return QMargins(0, 0, PDFComponentPainterHelper::ROW_TRAILING_MARGIN, 0);
}

int PDFComponentPainter::getRowTrailingSpacing()
{
    return PDFComponentPainterHelper::ROW_TRAILING_SPACING;
}

qreal PDFComponentPainter::getCardRadius()
{
    return PDFComponentPainterHelper::CARD_RADIUS;
}

QMargins PDFComponentPainter::getCardTrailingMargins()
{
    return QMargins(0, 0, PDFComponentPainterHelper::CARD_TRAILING_MARGIN, 0);
}

int PDFComponentPainter::getCardTrailingSpacing()
{
    return PDFComponentPainterHelper::CARD_TRAILING_SPACING;
}

PDFComponentPainter::BadgeSize PDFComponentPainter::normalized(BadgeSize size)
{
    switch (size)
    {
        case BadgeSize::Small:
        case BadgeSize::Medium:
        case BadgeSize::Large:
            return size;
    }

    return BadgeSize::Medium;
}

PDFAccentHue PDFComponentPainter::normalized(PDFAccentHue hue)
{
    switch (hue)
    {
        case PDFAccentHue::Blue:
        case PDFAccentHue::Violet:
        case PDFAccentHue::Amber:
        case PDFAccentHue::Red:
        case PDFAccentHue::Teal:
        case PDFAccentHue::Green:
        case PDFAccentHue::Neutral:
            return hue;
    }

    return PDFAccentHue::Neutral;
}

int PDFComponentPainter::getBadgeDiameter(BadgeSize size)
{
    return static_cast<int>(normalized(size));
}

int PDFComponentPainter::getBadgeIconExtent(BadgeSize size)
{
    return getBadgeDiameter(size) / 2;
}

QSize PDFComponentPainter::getToolBadgeSize(const ToolBadge& badge)
{
    const int diameter = getBadgeDiameter(badge.size);
    return QSize(diameter, diameter);
}

void PDFComponentPainter::paintToolBadge(QPainter* painter, const QRectF& rect, const ToolBadge& badge, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Role = PDFAccentHueColors::Role;

    // The badge is never scaled up, only a too small rectangle makes it smaller
    const qreal diameter = qMin<qreal>(getBadgeDiameter(badge.size), qMin(rect.width(), rect.height()));
    const QRectF circle = alignedCentered(rect, QSizeF(diameter, diameter));
    const PDFAccentHue hue = normalized(badge.hue);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fillEllipse(painter, circle, PDFStylePainter::color(PDFAccentHueColors::getColor(hue, Role::Background)));

    if (!badge.iconName.isEmpty())
    {
        const qreal iconExtent = diameter / 2.0;
        const PDFThemeColors::Color iconColor = state.isEnabled() ? PDFAccentHueColors::getColor(hue, Role::Icon) : PDFThemeColors::IconDisabled;
        PDFStylePainter::icon(painter, alignedCentered(circle, QSizeF(iconExtent, iconExtent)), badge.iconName, iconColor);
    }
}

QFont PDFComponentPainter::getTagFont()
{
    return PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption);
}

QSize PDFComponentPainter::getTagSize(const Tag& tag)
{
    const int textWidth = qCeil(PDFStylePainter::textWidth(tag.text, getTagFont()));
    return QSize(textWidth + 2 * PDFComponentPainterHelper::TAG_SIDE, PDFComponentPainterHelper::TAG_HEIGHT);
}

void PDFComponentPainter::paintTag(QPainter* painter, const QRectF& rect, const Tag& tag, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PDFStylePainter::Radii radii(qMin(rect.width(), rect.height()) / 2.0);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Hover and press lay bg/hover / bg/pressed over the own fill bg/subtle (decision 27)
    PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgSubtle);
    if (state.isPressed())
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgPressed);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgHover);
    }

    // The focus ring replaces the border, the label stays at its place
    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, PDFThemeColors::BorderDefault, 1.0);
    }

    const qreal side = PDFComponentPainterHelper::TAG_SIDE;
    const qreal line = PDFComponentPainterHelper::TAG_LINE;
    const QRectF textRect(rect.left() + side, rect.top() + qFloor((rect.height() - line) / 2.0), rect.width() - 2.0 * side, line);
    const QFont font = getTagFont();
    const QColor textColor = PDFStylePainter::color(state.isEnabled() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextDisabled);
    PDFStylePainter::text(painter, textRect, Qt::AlignVCenter | state.leadingAlignment() | Qt::TextSingleLine,
                          elidedText(tag.text, font, textRect.width()), font, textColor);
}

QFont PDFComponentPainter::getTextLinkFont(bool isUnderlined)
{
    return getTextLinkFont(TextLinkSize::Default, isUnderlined);
}

QFont PDFComponentPainter::getTextLinkFont(TextLinkSize size, bool isUnderlined)
{
    QFont font = PDFThemeFonts::getFont(size == TextLinkSize::Small ? PDFThemeFonts::Role::SmallSemiBold : PDFThemeFonts::Role::BodySemiBold);
    font.setUnderline(isUnderlined);
    return font;
}

QSize PDFComponentPainter::getTextLinkSize(const TextLink& link)
{
    const int textWidth = qCeil(PDFStylePainter::textWidth(link.text, getTextLinkFont(link.size, false)));
    return QSize(textWidth + 2 * PDFComponentPainterHelper::LINK_SIDE, PDFComponentPainterHelper::LINK_HEIGHT);
}

qreal PDFComponentPainter::getTextLinkRadius()
{
    return PDFComponentPainterHelper::LINK_RADIUS;
}

void PDFComponentPainter::paintTextLink(QPainter* painter, const QRectF& rect, const TextLink& link, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    PDFThemeColors::Color textColor = PDFThemeColors::TextAccent;
    if (!state.isEnabled())
    {
        textColor = PDFThemeColors::TextDisabled;
    }
    else if (state.isPressed())
    {
        textColor = PDFThemeColors::TextAccentPressed;
    }

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QFont font = getTextLinkFont(link.size, false);
    const qreal side = PDFComponentPainterHelper::LINK_SIDE;
    const QRectF textRect = rect.adjusted(side, 0.0, -side, 0.0);
    const QString shownText = elidedText(link.text, font, textRect.width());
    const QColor color = PDFStylePainter::color(textColor);
    PDFStylePainter::text(painter, textRect, Qt::AlignVCenter | state.leadingAlignment() | Qt::TextSingleLine, shownText, font, color);

    // Hover and press underline the link, the focus ring does not. The design
    // draws the underline of 1 px at the bottom edge of the line box of 16 px
    // (Figma does not clip it); here it is a whole pixel, the last row of the box.
    if ((state.isHovered() || state.isPressed()) && !shownText.isEmpty())
    {
        const qreal lineHeight = PDFComponentPainterHelper::LINK_HEIGHT;
        const qreal lineTop = rect.top() + qFloor((rect.height() - lineHeight) / 2.0);
        const qreal underlineTop = qMax(rect.top(), qMin(lineTop + lineHeight, rect.top() + rect.height()) - 1.0);
        const qreal underlineWidth = qMin<qreal>(PDFStylePainter::textWidth(shownText, font), textRect.width());
        const QRectF underline = state.visualRect(textRect, QRectF(textRect.left(), underlineTop, underlineWidth, 1.0));
        PDFStylePainter::fillRect(painter, underline, color);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, rect, PDFStylePainter::Radii(getTextLinkRadius()));
    }
}

PDFComponentPainter::StarSize PDFComponentPainter::normalized(StarSize size)
{
    switch (size)
    {
        case StarSize::Small:
        case StarSize::Medium:
            return size;
    }

    return StarSize::Medium;
}

QSize PDFComponentPainter::getStarSize(StarSize size)
{
    const int extent = static_cast<int>(normalized(size));
    return QSize(extent, extent);
}

qreal PDFComponentPainter::getStarRadius()
{
    return PDFComponentPainterHelper::STAR_RADIUS;
}

void PDFComponentPainter::paintStar(QPainter* painter, const QRectF& rect, StarSize size, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const qreal extent = qMin<qreal>(static_cast<int>(normalized(size)), qMin(rect.width(), rect.height()));
    const QRectF shape = alignedCentered(rect, QSizeF(extent, extent));
    const PDFStylePainter::Radii radii(getStarRadius());

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Hover and press lay the overlay over the own surface (decision 28), so
    // the star does not add a second overlay to the hover of its row
    if (state.isPressed() || state.isHovered())
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgSurface);
        PDFStylePainter::fill(painter, shape, radii, state.isPressed() ? PDFThemeColors::BgPressed : PDFThemeColors::BgHover);
    }

    const qreal iconExtent = qMin<qreal>(PDFComponentPainterHelper::STAR_ICON, extent);
    const QRectF iconRect = alignedCentered(shape, QSizeF(iconExtent, iconExtent));

    if (state.isOn())
    {
        // Pinned: filled amber star, the pointer changes only the background
        PDFStylePainter::icon(painter, iconRect, QStringLiteral("star-filled"), state.isEnabled() ? PDFThemeColors::IconSun : PDFThemeColors::IconDisabled);
    }
    else
    {
        PDFThemeColors::Color iconColor = PDFThemeColors::IconMuted;
        if (!state.isEnabled())
        {
            iconColor = PDFThemeColors::IconDisabled;
        }
        else if (state.isHovered() || state.isPressed())
        {
            iconColor = PDFThemeColors::IconDefault;
        }

        PDFStylePainter::icon(painter, iconRect, QStringLiteral("star"), iconColor);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, shape, radii);
    }
}

PDFComponentPainter::SwatchKind PDFComponentPainter::normalized(SwatchKind kind)
{
    switch (kind)
    {
        case SwatchKind::Color:
        case SwatchKind::None:
            return kind;
    }

    return SwatchKind::Color;
}

QSize PDFComponentPainter::getColorSwatchSize()
{
    return QSize(PDFComponentPainterHelper::SWATCH_EXTENT, PDFComponentPainterHelper::SWATCH_EXTENT);
}

qreal PDFComponentPainter::getColorSwatchRadius()
{
    return PDFComponentPainterHelper::SWATCH_EXTENT / 2.0;
}

void PDFComponentPainter::paintColorSwatch(QPainter* painter, const QRectF& rect, const ColorSwatch& swatch, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const qreal extent = qMin<qreal>(PDFComponentPainterHelper::SWATCH_EXTENT, qMin(rect.width(), rect.height()));
    const qreal scale = extent / PDFComponentPainterHelper::SWATCH_EXTENT;
    const QRectF shape = alignedCentered(rect, QSizeF(extent, extent));
    const qreal colorExtent = PDFComponentPainterHelper::SWATCH_COLOR * scale;
    const QRectF colorRect = alignedCentered(shape, QSizeF(colorExtent, colorExtent));
    const bool enabled = state.isEnabled();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Hover and press fill the whole circle, the overlay lies over the own
    // surface (decision 28); a disabled swatch has no hover
    if (state.isPressed() || state.isHovered())
    {
        PDFStylePainter::fillEllipse(painter, shape, PDFStylePainter::color(PDFThemeColors::BgSurface));
        PDFStylePainter::fillEllipse(painter, shape, PDFStylePainter::color(state.isPressed() ? PDFThemeColors::BgPressed : PDFThemeColors::BgHover));
    }

    // The color is a value, a disabled swatch keeps it. The border of 1 px lies
    // inside of the circle of the color. An opaque content is drawn over a disc
    // of the color of the border and ends at the inner edge of the border, so
    // the antialiased edges of the fill and of the border do not let the fill
    // (or the background) bleed into the border; a translucent color ends in
    // the middle of the border instead (the disc would tint it).
    const QColor borderColor = PDFStylePainter::color(enabled ? PDFThemeColors::BorderStrong : PDFThemeColors::BorderDefault);
    const bool isNone = normalized(swatch.kind) == SwatchKind::None;
    const bool isOpaque = isNone || (swatch.color.isValid() && swatch.color.alpha() == 255);
    const qreal fillInset = (isOpaque ? 1.0 : 0.5) * qMin<qreal>(scale, 1.0);
    const QRectF fillRect = colorRect.adjusted(fillInset, fillInset, -fillInset, -fillInset);
    if (isOpaque)
    {
        PDFStylePainter::fillEllipse(painter, colorRect, borderColor);
    }

    if (isNone)
    {
        PDFStylePainter::fillEllipse(painter, fillRect, PDFStylePainter::color(PDFThemeColors::BgSurface));

        QPainterPath clip;
        clip.addEllipse(colorRect);
        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipPath(clip, Qt::IntersectClip);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(PDFStylePainter::color(enabled ? PDFThemeColors::IconDanger : PDFThemeColors::IconDisabled),
                             PDFComponentPainterHelper::SWATCH_SLASH_WIDTH * scale, Qt::SolidLine, Qt::FlatCap));
        const qreal leg = PDFComponentPainterHelper::SWATCH_SLASH_LENGTH * scale / M_SQRT2;
        const QPointF start(colorRect.left() + PDFComponentPainterHelper::SWATCH_SLASH_X * scale, colorRect.top() + PDFComponentPainterHelper::SWATCH_SLASH_Y * scale);
        painter->drawLine(start, start + QPointF(leg, -leg));
    }
    else if (swatch.color.isValid())
    {
        PDFStylePainter::fillEllipse(painter, fillRect, swatch.color);
    }

    PDFStylePainter::strokeEllipseInside(painter, colorRect, borderColor, 1.0);

    // The selection is a ring inside of the circle, the focus ring lies outside of it
    if (state.isOn())
    {
        PDFStylePainter::strokeEllipseInside(painter, shape, PDFStylePainter::color(enabled ? PDFThemeColors::BgAccent : PDFThemeColors::BorderStrong), 2.0);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, shape, PDFStylePainter::Radii(extent / 2.0));
    }
}

QSize PDFComponentPainter::getIconChoiceSize(bool isSelected)
{
    const int extent = isSelected ? PDFComponentPainterHelper::ICON_CHOICE_SELECTED : PDFComponentPainterHelper::ICON_CHOICE;
    return QSize(extent, extent);
}

qreal PDFComponentPainter::getIconChoiceRadius()
{
    return PDFComponentPainterHelper::ICON_CHOICE_RADIUS;
}

void PDFComponentPainter::paintIconChoice(QPainter* painter, const QRectF& rect, const IconChoice& choice, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    // The selected choice has the ring of 2 px around the padding of 2 (40 x 40),
    // the others only the padding (36 x 36); the shape is centered in the rectangle
    const bool isSelected = state.isChecked();
    const QSize size = getIconChoiceSize(isSelected);
    const QRectF shape = alignedCentered(rect, QSizeF(qMin<qreal>(size.width(), rect.width()), qMin<qreal>(size.height(), rect.height())));
    const PDFStylePainter::Radii radii(PDFComponentPainterHelper::ICON_CHOICE_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (state.isPressed())
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgPressed);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgHover);
    }

    const ToolBadge badge{ choice.iconName, isSelected ? PDFAccentHue::Blue : PDFAccentHue::Neutral, BadgeSize::Medium };
    paintToolBadge(painter, shape, badge, state);

    if (isSelected)
    {
        PDFStylePainter::strokeInside(painter, shape, radii, state.isEnabled() ? PDFThemeColors::BorderFocus : PDFThemeColors::BorderStrong, PDFComponentPainterHelper::ICON_CHOICE_RING);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, shape, radii);
    }
}

PDFComponentPainter::ThumbnailSize PDFComponentPainter::normalized(ThumbnailSize size)
{
    switch (size)
    {
        case ThumbnailSize::Small:
        case ThumbnailSize::Medium:
        case ThumbnailSize::Large:
            return size;
    }

    return ThumbnailSize::Small;
}

QSize PDFComponentPainter::getPageThumbnailSize(ThumbnailSize size)
{
    const PDFComponentPainterHelper::ThumbnailGeometry geometry = PDFComponentPainterHelper::getThumbnailGeometry(normalized(size));
    return QSize(geometry.width, geometry.height);
}

QMargins PDFComponentPainter::getPageThumbnailShadowMargins()
{
    // The blur of 2 reaches 2 px from the shadow, which is 1 px lower than the frame
    const int blur = qCeil(PDFComponentPainterHelper::THUMBNAIL_SHADOW_BLUR);
    const int offset = qCeil(PDFComponentPainterHelper::THUMBNAIL_SHADOW_OFFSET);
    return QMargins(blur, blur - offset, blur, blur + offset);
}

void PDFComponentPainter::paintPageThumbnail(QPainter* painter, const QRectF& frame, const PageThumbnail& thumbnail, const State& state)
{
    paintPageThumbnailShadow(painter, frame);
    paintPageThumbnailFrame(painter, frame, thumbnail, state);
}

void PDFComponentPainter::paintPageThumbnailShadow(QPainter* painter, const QRectF& frame)
{
    if (!painter || frame.isEmpty())
    {
        return;
    }

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::shadow(painter, frame, PDFStylePainter::Radii(PDFComponentPainterHelper::THUMBNAIL_RADIUS), QPointF(0.0, PDFComponentPainterHelper::THUMBNAIL_SHADOW_OFFSET),
                            PDFComponentPainterHelper::THUMBNAIL_SHADOW_BLUR, PDFStylePainter::color(PDFThemeColors::EffectShadow));
}

void PDFComponentPainter::paintPageThumbnailFrame(QPainter* painter, const QRectF& frame, const PageThumbnail& thumbnail, const State& state)
{
    if (!painter || frame.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PDFComponentPainterHelper::ThumbnailGeometry geometry = PDFComponentPainterHelper::getThumbnailGeometry(normalized(thumbnail.size));
    const PDFStylePainter::Radii radii(PDFComponentPainterHelper::THUMBNAIL_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (thumbnail.locked)
    {
        // A document protected by a password: no picture, the lock in the center
        PDFStylePainter::fill(painter, frame, radii, PDFThemeColors::BgSubtle);
        const qreal extent = qMin<qreal>(geometry.width >= 80 ? 28.0 : (geometry.width >= 56 ? 20.0 : 14.0), qMin(frame.width(), frame.height()));
        PDFStylePainter::icon(painter, alignedCentered(frame, QSizeF(extent, extent)), QStringLiteral("lock"), state.isEnabled() ? PDFThemeColors::IconMuted : PDFThemeColors::IconDisabled);
        PDFStylePainter::strokeInside(painter, frame, radii, PDFThemeColors::BorderStrong, 1.0);
        return;
    }

    PDFStylePainter::fill(painter, frame, radii, PDFThemeColors::BgSurface);

    if (!thumbnail.pixmap.isNull() && thumbnail.pixmap.width() > 0 && thumbnail.pixmap.height() > 0)
    {
        // The picture of the page fits into the frame and keeps its aspect ratio
        const QSizeF pixmapSize = QSizeF(thumbnail.pixmap.size()) / qMax<qreal>(thumbnail.pixmap.devicePixelRatio(), 1.0);
        const QSizeF targetSize = pixmapSize.scaled(frame.size(), Qt::KeepAspectRatio);
        const QRectF target = PDFStylePainter::centered(frame, targetSize);

        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipPath(PDFStylePainter::roundedPath(frame, radii), Qt::IntersectClip);
        drawScaledPixmap(painter, target, thumbnail.pixmap);
    }
    else
    {
        // Placeholder lines of the component, from the top, inside of the border;
        // the shorter last line lies at the leading edge (mirrored for RTL)
        const qreal left = frame.left() + 1.0 + geometry.paddingSide;
        const qreal width = frame.width() - 2.0 - 2.0 * geometry.paddingSide;
        const qreal bottom = frame.bottom() - 1.0;
        qreal top = frame.top() + 1.0 + geometry.paddingTop;

        if (width > 0.0)
        {
            // The title line, the full lines and the shorter last line
            const int lineCount = qMax(geometry.fullLines, 0) + 2;
            for (int i = 0; i < lineCount; ++i)
            {
                const bool isTitle = i == 0;
                const qreal lineWidth = (i == lineCount - 1) ? qMin<qreal>(geometry.lastLineWidth, width) : width;
                const qreal height = isTitle ? geometry.titleHeight : geometry.lineHeight;
                if (top + height <= bottom && lineWidth > 0.0)
                {
                    const PDFThemeColors::Color color = isTitle ? PDFThemeColors::TextMuted : PDFThemeColors::BorderStrong;
                    PDFStylePainter::fillRect(painter, state.visualRect(frame, QRectF(left, top, lineWidth, height)), color);
                }
                top += height + geometry.gap;
            }
        }
    }

    PDFStylePainter::strokeInside(painter, frame, radii, PDFThemeColors::BorderStrong, 1.0);
}

/// Geometry constants and drawing helpers of the rows of the specification
class PDFComponentRowPainterHelper
{
public:
    PDFComponentRowPainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using State = PDFComponentPainter::State;

    // Lines of the texts (automatic line heights of Inter)
    static constexpr int TITLE_LINE = 16;       ///< Semi Bold 13
    static constexpr int SUBTITLE_LINE = 13;    ///< Regular / Medium 11
    static constexpr int SMALL_LINE = 15;       ///< Regular / Medium 12
    static constexpr int MIN_TEXT = 40;         ///< Minimal width of an elided text column
    static constexpr qreal ROW_RADIUS = 8.0;
    static constexpr int CHEVRON = 14;
    static constexpr int BADGE = 32;

    // Menu button: icon more-vertical 16 x 16
    static constexpr int MORE = 16;
    static constexpr qreal MORE_RADIUS = 4.0;

    // Link row 49:190: padding 10, icon 16, gap 12, title and subtitle with a gap of 1
    static constexpr int LINK_WIDTH = 328;
    static constexpr int LINK_HEIGHT = 50;
    static constexpr int LINK_PADDING = 10;
    static constexpr int LINK_ICON = 16;
    static constexpr int LINK_GAP = 12;

    // Recent file row 49:280: padding 10 / 12, gaps 12, icon 18 x 22, texts with
    // a gap of 2, star 24, menu 16, badge 19 high with padding 8 and radius 10
    static constexpr int FILE_WIDTH = 381;
    static constexpr int FILE_HEIGHT = 51;
    static constexpr int FILE_SIDE = 12;
    static constexpr int FILE_GAP = 12;
    static constexpr int FILE_ICON_WIDTH = 18;
    static constexpr int FILE_ICON_HEIGHT = 22;
    static constexpr int FILE_TEXT_GAP = 2;
    static constexpr int FILE_STAR = 24;
    static constexpr int FILE_BADGE_HEIGHT = 19;
    static constexpr int FILE_BADGE_PADDING = 8;
    static constexpr qreal FILE_BADGE_RADIUS = 10.0;

    // Recent tool row 50:279: padding 8, 12, 8, 10, gap 12
    static constexpr int RECENT_TOOL_WIDTH = 342;
    static constexpr int RECENT_TOOL_HEIGHT = 48;
    static constexpr int RECENT_TOOL_LEFT = 10;
    static constexpr int RECENT_TOOL_RIGHT = 12;
    static constexpr int RECENT_TOOL_GAP = 12;

    // Workspace row 64:544: padding 0, 8, 0, 10, gap 12
    static constexpr int WORKSPACE_WIDTH = 344;
    static constexpr int WORKSPACE_HEIGHT = 52;
    static constexpr int WORKSPACE_LEFT = 10;
    static constexpr int WORKSPACE_RIGHT = 8;
    static constexpr int WORKSPACE_GAP = 12;

    // Tool row 89:1330: padding 0, 6, 0, 8, gap 10, star 28
    static constexpr int TOOL_WIDTH = 380;
    static constexpr int TOOL_HEIGHT = 48;
    static constexpr int TOOL_LEFT = 8;
    static constexpr int TOOL_RIGHT = 6;
    static constexpr int TOOL_GAP = 10;
    static constexpr int TOOL_STAR = 28;

    // Collection row 89:1367: padding 0, 8, 0, 10, gap 12, circle 32 with an icon 16
    static constexpr int COLLECTION_WIDTH = 324;
    static constexpr int COLLECTION_HEIGHT = 52;
    static constexpr int COLLECTION_LEFT = 10;
    static constexpr int COLLECTION_RIGHT = 8;
    static constexpr int COLLECTION_GAP = 12;
    static constexpr int COLLECTION_ICON = 16;
    static constexpr int COLLECTION_CREATE_HEIGHT = 54;
    static constexpr int COLLECTION_CREATE_PADDING = 11;    ///< The padding 10 inside of the border of 1 (272:12952)

    // Tool shortcut 192:836: radius 10, border 1 counted in the layout, padding
    // 8, 12, 8, 8, gap 10: the badge at 9, the name at 51, 13 from the right
    static constexpr int SHORTCUT_WIDTH = 180;
    static constexpr int SHORTCUT_HEIGHT = 48;
    static constexpr qreal SHORTCUT_RADIUS = 10.0;
    static constexpr int SHORTCUT_LEFT = 9;
    static constexpr int SHORTCUT_RIGHT = 13;
    static constexpr int SHORTCUT_GAP = 10;

    // Issue row 103:1931: padding 0, 10, gap 12, status circle 24 with an icon 14, number 44
    static constexpr int ISSUE_WIDTH = 786;
    static constexpr int ISSUE_HEIGHT = 52;
    static constexpr int ISSUE_SIDE = 10;
    static constexpr int ISSUE_GAP = 12;
    static constexpr int ISSUE_STATUS = 24;
    static constexpr int ISSUE_STATUS_ICON = 14;
    static constexpr int ISSUE_NUMBER = 44;

    // Version row 103:1950: padding 0, 8, rail 12 (dot 10 at 4, line 2 after a
    // gap of 4), gap 12, top line 16, gap 2, summary of lines 130 % of 11 px
    // (14 px), bottom padding 14
    static constexpr int VERSION_WIDTH = 324;
    static constexpr int VERSION_SIDE = 8;
    static constexpr int VERSION_RAIL = 12;
    static constexpr int VERSION_GAP = 12;
    static constexpr int VERSION_DOT = 10;
    static constexpr int VERSION_DOT_TOP = 4;
    static constexpr int VERSION_LINE = 2;
    static constexpr int VERSION_LINE_GAP = 4;
    static constexpr int VERSION_TOP_GAP = 8;
    static constexpr int VERSION_TEXT_GAP = 2;
    static constexpr int VERSION_SUMMARY_LINE = 14;
    static constexpr int VERSION_BOTTOM = 14;

    // Property row 206:873: padding 0, 4, 0, 10, gap 8, key 132, actions of 24,
    // radius 6, editor 24 high with padding 8, border/focus 2 inside counted in
    // the layout of the field (the text starts 10 px from its edge), radius 6
    static constexpr int PROPERTY_WIDTH = 326;
    static constexpr int PROPERTY_HEIGHT = 30;
    static constexpr int PROPERTY_LEFT = 10;
    static constexpr int PROPERTY_RIGHT = 4;
    static constexpr int PROPERTY_GAP = 8;
    static constexpr int PROPERTY_KEY = 132;
    static constexpr int PROPERTY_ACTION = 24;
    static constexpr int PROPERTY_ACTION_ICON = 14;
    static constexpr qreal PROPERTY_RADIUS = 6.0;
    static constexpr int PROPERTY_EDITOR_HEIGHT = 24;
    static constexpr int PROPERTY_EDITOR_PADDING = 8;
    static constexpr int PROPERTY_EDITOR_BORDER = 2;

    static QFont titleFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold); }
    static QFont subtitleFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular); }

    static int textWidth(const QString& text, const QFont& font)
    {
        return text.isEmpty() ? 0 : qCeil(PDFStylePainter::textWidth(text, font));
    }

    /// Returns the color of a text (text/disabled for a disabled component)
    static Color text(const State& state, Color color) { return state.isEnabled() ? color : Color::TextDisabled; }

    /// Returns the color of an icon (icon/disabled for a disabled component)
    static Color icon(const State& state, Color color) { return state.isEnabled() ? color : Color::IconDisabled; }

    /// Returns the alignment of the trailing edge
    static Qt::Alignment trailingAlignment(const State& state) { return state.trailingAlignment(); }

    /// Returns the chevron, which points in the reading direction (as the style mirrors its chevrons)
    static QString chevronIcon(const State& state) { return state.isRightToLeft() ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"); }

    /// Returns true, if the value is a color of the enumeration
    static bool isValidColor(Color color) { return static_cast<int>(color) >= 0 && static_cast<int>(color) < static_cast<int>(PDFThemeColors::ColorCount); }

    /// Returns the given color, if it is valid, otherwise the fallback (an invalid
    /// value would be drawn in the leak color)
    static Color validColor(const std::optional<Color>& color, Color fallback) { return (color.has_value() && isValidColor(*color)) ? *color : fallback; }

    /// Returns the logical rectangle of the height centered vertically in the outer rectangle
    static QRectF line(const QRectF& outer, qreal left, qreal width, qreal height)
    {
        return QRectF(left, outer.top() + qFloor((outer.height() - height) / 2.0), qMax<qreal>(0.0, width), height);
    }

    /// Kind of a text (the user interface, data, a name of a file, components.md 6.6)
    using TextKind = PDFComponentPainter::TextKind;

    /// Draws a text of a single line, elided at its end, into the logical rectangle
    static void drawText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& text,
                         const QFont& font, Color color, bool isTrailing = false, TextKind kind = TextKind::Interface)
    {
        if (logical.width() > 0.0 && logical.height() > 0.0)
        {
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(outer, logical), text, font, color, kind, isTrailing);
        }
    }

    /// Draws a text of data of a single line, elided at its end, into the logical rectangle
    static void drawDataText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& text,
                             const QFont& font, Color color, bool isTrailing = false)
    {
        drawText(painter, state, outer, logical, text, font, color, isTrailing, TextKind::Data);
    }

    /// Draws an icon into the logical rectangle
    static void drawIcon(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& name, Color color)
    {
        if (!name.isEmpty() && !logical.isEmpty())
        {
            PDFStylePainter::icon(painter, state.visualRect(outer, logical), name, color);
        }
    }

    /// Draws a title (Semi Bold 13, line 16) and a subtitle (Regular 11, line 13)
    /// as a block centered vertically in the outer rectangle; a title without the
    /// subtitle is centered alone (an issue of the release without its description)
    static void drawTextBlock(QPainter* painter, const State& state, const QRectF& outer, qreal left, qreal width,
                              const QString& title, Color titleColor, const QString& subtitle, Color subtitleColor,
                              int gap, TextKind titleKind = TextKind::Interface, TextKind subtitleKind = TextKind::Interface)
    {
        if (width <= 0.0)
        {
            return;
        }

        if (subtitle.isEmpty())
        {
            drawText(painter, state, outer, line(outer, left, width, TITLE_LINE), title, titleFont(), titleColor, false, titleKind);
            return;
        }

        // The name of a file is elided in the middle, its extension stays visible
        const qreal blockHeight = TITLE_LINE + gap + SUBTITLE_LINE;
        const qreal top = outer.top() + qFloor((outer.height() - blockHeight) / 2.0);
        drawText(painter, state, outer, QRectF(left, top, width, TITLE_LINE), title, titleFont(), titleColor, false, titleKind);
        drawText(painter, state, outer, QRectF(left, top + TITLE_LINE + gap, width, SUBTITLE_LINE), subtitle, subtitleFont(), subtitleColor, false, subtitleKind);
    }

    /// Fills the background of the pointer (pressed bg/pressed, hover bg/hover)
    static void fillPointer(QPainter* painter, const QRectF& shape, const PDFStylePainter::Radii& radii, const State& state)
    {
        if (state.isPressed())
        {
            PDFStylePainter::fill(painter, shape, radii, Color::BgPressed);
        }
        else if (state.isHovered())
        {
            PDFStylePainter::fill(painter, shape, radii, Color::BgHover);
        }
    }

    /// Color of a chevron or a menu icon, which is icon/default under the pointer
    static Color pointerIcon(const State& state)
    {
        if (!state.isEnabled())
        {
            return Color::IconDisabled;
        }

        return (state.isHovered() || state.isPressed()) ? Color::IconDefault : Color::IconMuted;
    }

    /// Returns the state of a nested element drawn by a row: enabled as the row,
    /// in the direction of the row, checked when \p isOn
    static State nestedState(const State& rowState, bool isOn)
    {
        QStyle::State flags = rowState.isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
        if (isOn)
        {
            flags |= QStyle::State_On;
        }
        return State(flags, rowState.direction);
    }

    /// Returns the state of a menu button drawn by a row: the pointer over the
    /// row (or its press) makes the icon darker, the focus of the row does not
    static State menuState(const State& rowState)
    {
        return State(rowState.flags & (QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken), rowState.direction);
    }

    /// Lays the summary of a version row out into lines of the width
    /// \returns Number of lines
    static int layoutSummary(QTextLayout& layout, const QString& text, const QFont& font, qreal width, Qt::LayoutDirection rowDirection)
    {
        if (text.isEmpty() || width <= 0.0)
        {
            return 0;
        }

        QTextOption option;
        option.setWrapMode(QTextOption::WordWrap);
        // The summary is data (the text of a release): its base direction is its
        // own, the lines are aligned to the leading edge of the row
        option.setTextDirection(PDFComponentPainter::getDataTextDirection(text));
        option.setAlignment((rowDirection == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute);

        layout.setText(text);
        layout.setFont(font);
        layout.setTextOption(option);
        layout.beginLayout();

        int count = 0;
        for (QTextLine textLine = layout.createLine(); textLine.isValid(); textLine = layout.createLine())
        {
            textLine.setLineWidth(width);
            textLine.setPosition(QPointF(0.0, count * VERSION_SUMMARY_LINE + (VERSION_SUMMARY_LINE - textLine.height()) / 2.0));
            ++count;
        }

        layout.endLayout();
        return count;
    }

    /// Returns the width of the actions of a property row (one or two buttons of 24)
    static int propertyActionsWidth(bool editable) { return editable ? 2 * PROPERTY_ACTION : PROPERTY_ACTION; }

    /// Returns the width of the key of a property row of the width
    static qreal propertyKeyWidth(qreal width, bool editable)
    {
        const qreal available = width - PROPERTY_LEFT - PROPERTY_GAP - MIN_TEXT - PROPERTY_GAP - propertyActionsWidth(editable) - PROPERTY_RIGHT;
        return qBound<qreal>(0.0, available, PROPERTY_KEY);
    }

    /// Returns the logical rectangle of the value of a property row (30 px high)
    static QRectF propertyValueRect(const QRectF& rect, bool editable)
    {
        const qreal left = rect.left() + PROPERTY_LEFT + propertyKeyWidth(rect.width(), editable) + PROPERTY_GAP;
        const qreal right = rect.right() - PROPERTY_RIGHT - propertyActionsWidth(editable) - PROPERTY_GAP;
        return QRectF(left, rect.top(), qMax<qreal>(0.0, right - left), rect.height());
    }
};

PDFThemeColors::Color PDFComponentPainter::getIconButtonColor(const IconButton& button, const State& state)
{
    using Helper = PDFComponentRowPainterHelper;

    if (!state.isEnabled())
    {
        return PDFThemeColors::IconDisabled;
    }

    // A semantic color of the button does not change with its state
    if (button.iconColor.has_value() && Helper::isValidColor(*button.iconColor))
    {
        return *button.iconColor;
    }

    if (state.isPressed())
    {
        return PDFThemeColors::IconStrong;
    }

    return state.isHovered() ? PDFThemeColors::IconDefault : PDFThemeColors::IconMuted;
}

void PDFComponentPainter::paintIconButton(QPainter* painter, const QRectF& rect, const IconButton& button, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const qreal extent = qMin<qreal>(qMax(button.extent, 0), qMin(rect.width(), rect.height()));
    if (extent <= 0.0)
    {
        return;
    }

    const QRectF shape = PDFStylePainter::centered(rect, QSizeF(extent, extent));
    const PDFStylePainter::Radii radii(qBound<qreal>(0.0, button.radius, extent / 2.0));

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Hover and press add a background over the surface below the button
    PDFComponentRowPainterHelper::fillPointer(painter, shape, radii, state);

    const PDFThemeColors::Color iconColor = getIconButtonColor(button, state);
    const qreal iconExtent = qMin<qreal>(qMax(button.iconExtent, 0), extent);
    if (iconExtent > 0.0 && !button.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, PDFStylePainter::centered(shape, QSizeF(iconExtent, iconExtent)), button.iconName, iconColor);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, shape, radii);
    }
}

QSize PDFComponentPainter::getMoreButtonSize()
{
    return QSize(PDFComponentRowPainterHelper::MORE, PDFComponentRowPainterHelper::MORE);
}

qreal PDFComponentPainter::getMoreButtonRadius()
{
    return PDFComponentRowPainterHelper::MORE_RADIUS;
}

void PDFComponentPainter::paintMoreButton(QPainter* painter, const QRectF& rect, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;

    const qreal extent = qMin<qreal>(Helper::MORE, qMin(rect.width(), rect.height()));
    const QRectF shape = alignedCentered(rect, QSizeF(extent, extent));

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::icon(painter, shape, QStringLiteral("more-vertical"), Helper::pointerIcon(state));

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, shape, PDFStylePainter::Radii(Helper::MORE_RADIUS));
    }
}

QSize PDFComponentPainter::getLinkRowSize(const LinkRow& row)
{
    const QSize minimum = getLinkRowMinimumSize(row);
    return QSize(qMax(PDFComponentRowPainterHelper::LINK_WIDTH, minimum.width()), PDFComponentRowPainterHelper::LINK_HEIGHT);
}

QSize PDFComponentPainter::getLinkRowMinimumSize(const LinkRow& row)
{
    using Helper = PDFComponentRowPainterHelper;

    // The texts hug their content in the design, they are not elided
    const int textWidth = qMax(Helper::textWidth(row.title, Helper::titleFont()), Helper::textWidth(row.subtitle, Helper::subtitleFont()));
    const int width = 2 * Helper::LINK_PADDING + Helper::LINK_ICON + Helper::LINK_GAP + textWidth + Helper::LINK_GAP + Helper::CHEVRON;
    return QSize(width, Helper::LINK_HEIGHT);
}

void PDFComponentPainter::paintLinkRow(QPainter* painter, const QRectF& rect, const LinkRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    const qreal left = rect.left();
    const Color iconColor = Helper::icon(state, Helper::validColor(row.iconColor, Color::IconDefault));
    Helper::drawIcon(painter, state, rect, Helper::line(rect, left + Helper::LINK_PADDING, Helper::LINK_ICON, Helper::LINK_ICON), row.iconName, iconColor);

    const qreal chevronLeft = rect.right() - Helper::LINK_PADDING - Helper::CHEVRON;
    Helper::drawIcon(painter, state, rect, Helper::line(rect, chevronLeft, Helper::CHEVRON, Helper::CHEVRON), Helper::chevronIcon(state), Helper::pointerIcon(state));

    const qreal textLeft = left + Helper::LINK_PADDING + Helper::LINK_ICON + Helper::LINK_GAP;
    const qreal textWidth = chevronLeft - Helper::LINK_GAP - textLeft;
    Helper::drawTextBlock(painter, state, rect, textLeft, textWidth, row.title, Helper::text(state, Color::TextPrimary), row.subtitle, Helper::text(state, Color::TextTertiary), 1);
}

PDFComponentPainter::FileStatus PDFComponentPainter::normalized(FileStatus status)
{
    switch (status)
    {
        case FileStatus::Available:
        case FileStatus::Missing:
            return status;
    }

    return FileStatus::Available;
}

QString PDFComponentPainter::elidedFileName(const QString& fileName, const QFont& font, qreal width)
{
    if (fileName.isEmpty() || !(width > 0.0))
    {
        return QString();
    }

    const QFontMetricsF metrics(font);
    if (metrics.horizontalAdvance(fileName) <= width)
    {
        return fileName;
    }

    // The extension is the part after the last dot of the name (not of a folder):
    // 1 to 8 characters without a white space ("Minutes 2026.final draft for
    // review" has none). A name starting with a dot (".profile") has none either.
    constexpr qsizetype MAX_EXTENSION = 8;
    const qsizetype separator = qMax(fileName.lastIndexOf(QLatin1Char('/')), fileName.lastIndexOf(QLatin1Char('\\')));
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    const qsizetype extensionLength = fileName.size() - dot - 1;
    bool hasExtension = dot > separator + 1 && extensionLength >= 1 && extensionLength <= MAX_EXTENSION;
    for (qsizetype i = dot + 1; hasExtension && i < fileName.size(); ++i)
    {
        hasExtension = !fileName.at(i).isSpace();
    }

    if (hasExtension)
    {
        const QString extension = fileName.mid(dot);
        const QString base = fileName.left(dot);
        const qreal baseWidth = width - metrics.horizontalAdvance(extension);

        // The extension is kept, only when an ellipsis fits before it
        if (baseWidth >= metrics.horizontalAdvance(QString(QChar(0x2026))))
        {
            const QString elidedBase = metrics.elidedText(base, Qt::ElideMiddle, baseWidth);
            if (!elidedBase.isEmpty())
            {
                return elidedBase + extension;
            }
        }
    }

    return metrics.elidedText(fileName, Qt::ElideRight, width);
}

QString PDFComponentPainter::getMissingFileText()
{
    return QCoreApplication::translate("PDFComponentPainter", "No longer exists");
}

QSize PDFComponentPainter::getMissingFileBadgeSize()
{
    using Helper = PDFComponentRowPainterHelper;
    return QSize(Helper::textWidth(getMissingFileText(), getTagFont()) + 2 * Helper::FILE_BADGE_PADDING, Helper::FILE_BADGE_HEIGHT);
}

void PDFComponentPainter::paintMissingFileBadge(QPainter* painter, const QRectF& rect, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(painter, rect, PDFStylePainter::Radii(Helper::FILE_BADGE_RADIUS), Color::AccentRedBg);
    PDFStylePainter::text(painter, rect, Qt::AlignCenter | Qt::TextSingleLine, getMissingFileText(), getTagFont(), PDFStylePainter::color(Helper::text(state, Color::TextDanger)));
}

QSize PDFComponentPainter::getRecentFileRowSize(const RecentFileRow& row)
{
    Q_UNUSED(row);
    return QSize(PDFComponentRowPainterHelper::FILE_WIDTH, PDFComponentRowPainterHelper::FILE_HEIGHT);
}

QSize PDFComponentPainter::getRecentFileRowMinimumSize(const RecentFileRow& row)
{
    using Helper = PDFComponentRowPainterHelper;

    int statusWidth = 0;
    if (normalized(row.status) == FileStatus::Missing)
    {
        statusWidth = Helper::textWidth(getMissingFileText(), getTagFont()) + 2 * Helper::FILE_BADGE_PADDING + Helper::FILE_GAP;
    }
    else if (!row.time.isEmpty())
    {
        statusWidth = Helper::textWidth(row.time, PDFThemeFonts::getFont(PDFThemeFonts::Role::Small)) + Helper::FILE_GAP;
    }

    const int starWidth = row.starVisible ? Helper::FILE_STAR + Helper::FILE_GAP : 0;
    const int width = Helper::FILE_SIDE + Helper::FILE_ICON_WIDTH + Helper::FILE_GAP + Helper::MIN_TEXT + Helper::FILE_GAP +
                      statusWidth + starWidth + Helper::MORE + Helper::FILE_SIDE;
    return QSize(width, Helper::FILE_HEIGHT);
}

void PDFComponentPainter::paintRecentFileRow(QPainter* painter, const QRectF& rect, const RecentFileRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);
    const bool isMissing = normalized(row.status) == FileStatus::Missing;
    const bool isPointer = state.isHovered() || state.isPressed();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    // The focus ring replaces the border, the pointer makes the border stronger
    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, isPointer ? Color::BorderStrong : Color::BorderDefault, 1.0);
    }

    const qreal left = rect.left();
    const QRectF iconRect = Helper::line(rect, left + Helper::FILE_SIDE, Helper::FILE_ICON_WIDTH, Helper::FILE_ICON_HEIGHT);
    Helper::drawIcon(painter, state, rect, iconRect, QStringLiteral("file-tall"), Helper::icon(state, isMissing ? Color::IconMuted : Color::IconPdf));

    // Nested elements from the trailing edge: menu, star, then the time or the badge
    const QRectF moreRect = Helper::line(rect, rect.right() - Helper::FILE_SIDE - Helper::MORE, Helper::MORE, Helper::MORE);
    qreal trailingLeft = moreRect.left();
    if (row.paintNested)
    {
        paintMoreButton(painter, state.visualRect(rect, moreRect), Helper::menuState(state));
    }

    if (row.starVisible)
    {
        const QRectF starRect = Helper::line(rect, trailingLeft - Helper::FILE_GAP - Helper::FILE_STAR, Helper::FILE_STAR, Helper::FILE_STAR);
        trailingLeft = starRect.left();
        if (row.paintNested)
        {
            paintStar(painter, state.visualRect(rect, starRect), StarSize::Small, Helper::nestedState(state, row.pinned));
        }
    }

    qreal statusLeft = trailingLeft;
    if (isMissing)
    {
        const qreal badgeWidth = getMissingFileBadgeSize().width();
        const QRectF badgeRect = Helper::line(rect, trailingLeft - Helper::FILE_GAP - badgeWidth, badgeWidth, Helper::FILE_BADGE_HEIGHT);
        statusLeft = badgeRect.left();

        if (badgeRect.left() > iconRect.right())
        {
            paintMissingFileBadge(painter, state.visualRect(rect, badgeRect), state);
        }
    }
    else if (!row.time.isEmpty())
    {
        const QFont timeFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
        const qreal timeWidth = Helper::textWidth(row.time, timeFont);
        const QRectF timeRect = Helper::line(rect, trailingLeft - Helper::FILE_GAP - timeWidth, timeWidth, Helper::SMALL_LINE);
        statusLeft = timeRect.left();

        if (timeRect.left() > iconRect.right())
        {
            Helper::drawText(painter, state, rect, timeRect, row.time, timeFont, Helper::text(state, Color::TextTertiary), true);
        }
    }

    const qreal textLeft = iconRect.right() + Helper::FILE_GAP;
    const qreal textWidth = statusLeft - Helper::FILE_GAP - textLeft;
    Helper::drawTextBlock(painter, state, rect, textLeft, textWidth, row.fileName, Helper::text(state, isMissing ? Color::TextMuted : Color::TextPrimary),
                          row.path, Helper::text(state, Color::TextMuted), Helper::FILE_TEXT_GAP, Helper::TextKind::FileName, Helper::TextKind::Data);
}

QSize PDFComponentPainter::getRecentToolRowSize(const RecentToolRow& row)
{
    Q_UNUSED(row);
    return QSize(PDFComponentRowPainterHelper::RECENT_TOOL_WIDTH, PDFComponentRowPainterHelper::RECENT_TOOL_HEIGHT);
}

QSize PDFComponentPainter::getRecentToolRowMinimumSize(const RecentToolRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentRowPainterHelper;
    return QSize(Helper::RECENT_TOOL_LEFT + Helper::BADGE + 2 * Helper::RECENT_TOOL_GAP + Helper::MIN_TEXT + Helper::CHEVRON + Helper::RECENT_TOOL_RIGHT, Helper::RECENT_TOOL_HEIGHT);
}

void PDFComponentPainter::paintRecentToolRow(QPainter* painter, const QRectF& rect, const RecentToolRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::RECENT_TOOL_LEFT, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ row.iconName, row.hue, BadgeSize::Medium }, state);

    const qreal chevronLeft = rect.right() - Helper::RECENT_TOOL_RIGHT - Helper::CHEVRON;
    Helper::drawIcon(painter, state, rect, Helper::line(rect, chevronLeft, Helper::CHEVRON, Helper::CHEVRON), Helper::chevronIcon(state), Helper::pointerIcon(state));

    const qreal textLeft = badgeRect.right() + Helper::RECENT_TOOL_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, chevronLeft - Helper::RECENT_TOOL_GAP - textLeft,
                          row.name, Helper::text(state, Color::TextPrimary), row.lastUsed, Helper::text(state, Color::TextMuted), 1);
}

QSize PDFComponentPainter::getWorkspaceRowSize(const WorkspaceRow& row)
{
    Q_UNUSED(row);
    return QSize(PDFComponentRowPainterHelper::WORKSPACE_WIDTH, PDFComponentRowPainterHelper::WORKSPACE_HEIGHT);
}

QSize PDFComponentPainter::getWorkspaceRowMinimumSize(const WorkspaceRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentRowPainterHelper;
    return QSize(Helper::WORKSPACE_LEFT + Helper::BADGE + 2 * Helper::WORKSPACE_GAP + Helper::MIN_TEXT + Helper::MORE + Helper::WORKSPACE_RIGHT, Helper::WORKSPACE_HEIGHT);
}

QMargins PDFComponentPainter::getWorkspaceRowTrailingMargins()
{
    return QMargins(0, 0, PDFComponentRowPainterHelper::WORKSPACE_RIGHT, 0);
}

void PDFComponentPainter::paintWorkspaceRow(QPainter* painter, const QRectF& rect, const WorkspaceRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::WORKSPACE_LEFT, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ row.iconName, row.hue, BadgeSize::Medium }, state);

    const QRectF moreRect = Helper::line(rect, rect.right() - Helper::WORKSPACE_RIGHT - Helper::MORE, Helper::MORE, Helper::MORE);
    if (row.paintNested)
    {
        paintMoreButton(painter, state.visualRect(rect, moreRect), Helper::menuState(state));
    }

    const qreal textLeft = badgeRect.right() + Helper::WORKSPACE_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, moreRect.left() - Helper::WORKSPACE_GAP - textLeft,
                          row.name, Helper::text(state, Color::TextPrimary), row.meta, Helper::text(state, Color::TextMuted), 1, Helper::TextKind::Data);
}

QSize PDFComponentPainter::getToolRowSize(const ToolRow& row)
{
    Q_UNUSED(row);
    return QSize(PDFComponentRowPainterHelper::TOOL_WIDTH, PDFComponentRowPainterHelper::TOOL_HEIGHT);
}

QSize PDFComponentPainter::getToolRowMinimumSize(const ToolRow& row)
{
    using Helper = PDFComponentRowPainterHelper;
    const int tagWidth = row.tag.isEmpty() ? 0 : getTagSize(Tag{ row.tag }).width() + Helper::TOOL_GAP;
    const int starWidth = row.starVisible ? Helper::TOOL_GAP + Helper::TOOL_STAR : 0;
    return QSize(Helper::TOOL_LEFT + Helper::BADGE + Helper::TOOL_GAP + Helper::MIN_TEXT + tagWidth + starWidth + Helper::TOOL_RIGHT, Helper::TOOL_HEIGHT);
}

bool PDFComponentPainter::isToolRowTagShown(const QRectF& rect, const ToolRow& row)
{
    using Helper = PDFComponentRowPainterHelper;

    if (row.tag.isEmpty())
    {
        return false;
    }

    // The tag lies before the star; it is dropped, when it would reach the badge
    const qreal starWidth = row.starVisible ? Helper::TOOL_STAR + Helper::TOOL_GAP : 0.0;
    const qreal tagLeft = rect.right() - Helper::TOOL_RIGHT - starWidth - getTagSize(Tag{ row.tag }).width();
    return tagLeft > rect.left() + Helper::TOOL_LEFT + Helper::BADGE;
}

QMargins PDFComponentPainter::getToolRowTrailingMargins()
{
    return QMargins(0, 0, PDFComponentRowPainterHelper::TOOL_RIGHT, 0);
}

int PDFComponentPainter::getToolRowTrailingSpacing()
{
    return PDFComponentRowPainterHelper::TOOL_GAP;
}

void PDFComponentPainter::paintToolRow(QPainter* painter, const QRectF& rect, const ToolRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::TOOL_LEFT, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ row.iconName, row.hue, BadgeSize::Medium }, state);

    // The trailing part (the star, the tag) starts at trailingLeft; a row without
    // the star lets the texts and the tag reach the padding (trailingLeft is then
    // one gap behind it, the gap before the trailing part is subtracted below)
    qreal trailingLeft = rect.right() - Helper::TOOL_RIGHT + Helper::TOOL_GAP;
    if (row.starVisible)
    {
        const QRectF starRect = Helper::line(rect, rect.right() - Helper::TOOL_RIGHT - Helper::TOOL_STAR, Helper::TOOL_STAR, Helper::TOOL_STAR);
        trailingLeft = starRect.left();
        if (row.paintNested)
        {
            paintStar(painter, state.visualRect(rect, starRect), StarSize::Medium, Helper::nestedState(state, row.pinned));
        }
    }

    if (isToolRowTagShown(rect, row))
    {
        const Tag tag{ row.tag };
        const QSize tagSize = getTagSize(tag);
        const QRectF tagRect = Helper::line(rect, trailingLeft - Helper::TOOL_GAP - tagSize.width(), tagSize.width(), tagSize.height());
        trailingLeft = tagRect.left();
        if (row.paintNested)
        {
            paintTag(painter, state.visualRect(rect, tagRect), tag, Helper::nestedState(state, false));
        }
    }

    const qreal textLeft = badgeRect.right() + Helper::TOOL_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, trailingLeft - Helper::TOOL_GAP - textLeft,
                          row.name, Helper::text(state, Color::TextPrimary), row.description, Helper::text(state, Color::TextMuted), 1);
}

QSize PDFComponentPainter::getCollectionRowSize(const CollectionRow& row)
{
    using Helper = PDFComponentRowPainterHelper;
    return QSize(Helper::COLLECTION_WIDTH, row.isCreate ? Helper::COLLECTION_CREATE_HEIGHT : Helper::COLLECTION_HEIGHT);
}

QSize PDFComponentPainter::getCollectionRowMinimumSize(const CollectionRow& row)
{
    using Helper = PDFComponentRowPainterHelper;
    if (row.isCreate)
    {
        // The paddings inside of the border, no chevron
        return QSize(2 * Helper::COLLECTION_CREATE_PADDING + Helper::BADGE + Helper::COLLECTION_GAP + Helper::MIN_TEXT, Helper::COLLECTION_CREATE_HEIGHT);
    }
    return QSize(Helper::COLLECTION_LEFT + Helper::BADGE + 2 * Helper::COLLECTION_GAP + Helper::MIN_TEXT + Helper::CHEVRON + Helper::COLLECTION_RIGHT, Helper::COLLECTION_HEIGHT);
}

void PDFComponentPainter::paintCollectionRow(QPainter* painter, const QRectF& rect, const CollectionRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    if (row.isCreate)
    {
        // new-collection 272:12952: bg/subtle, the border border/default
        PDFStylePainter::fill(painter, rect, radii, Color::BgSubtle);
        Helper::fillPointer(painter, rect, radii, state);
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault);
        if (state.hasFocusRing())
        {
            paintFocusRingInside(painter, rect, radii);
        }

        // The circle bg/surface with its border, the icon icon/accent
        const QRectF createCircle = Helper::line(rect, rect.left() + Helper::COLLECTION_CREATE_PADDING, Helper::BADGE, Helper::BADGE);
        const QRectF visualCreateCircle = state.visualRect(rect, createCircle);
        PDFStylePainter::fillEllipse(painter, visualCreateCircle, PDFStylePainter::color(Color::BgSurface));
        PDFStylePainter::strokeEllipseInside(painter, visualCreateCircle, PDFStylePainter::color(Color::BorderDefault), 1.0);
        if (!row.iconName.isEmpty())
        {
            PDFStylePainter::icon(painter, alignedCentered(visualCreateCircle, QSizeF(Helper::COLLECTION_ICON, Helper::COLLECTION_ICON)), row.iconName, Helper::icon(state, Color::IconAccent));
        }

        // The name text/accent, no chevron
        const qreal createTextLeft = createCircle.right() + Helper::COLLECTION_GAP;
        Helper::drawTextBlock(painter, state, rect, createTextLeft, rect.right() - Helper::COLLECTION_CREATE_PADDING - createTextLeft,
                              row.name, Helper::text(state, Color::TextAccent), row.meta, Helper::text(state, Color::TextMuted), 1);
        return;
    }

    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    // The circle has the fixed colors bg/accent-subtle and icon/accent (review F-32)
    const QRectF circleRect = Helper::line(rect, rect.left() + Helper::COLLECTION_LEFT, Helper::BADGE, Helper::BADGE);
    const QRectF visualCircle = state.visualRect(rect, circleRect);
    PDFStylePainter::fillEllipse(painter, visualCircle, PDFStylePainter::color(Color::BgAccentSubtle));
    if (!row.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, alignedCentered(visualCircle, QSizeF(Helper::COLLECTION_ICON, Helper::COLLECTION_ICON)), row.iconName, Helper::icon(state, Color::IconAccent));
    }

    // The chevron is icon/default under the pointer, as in Link row and Recent tool row (decision 29)
    const qreal chevronLeft = rect.right() - Helper::COLLECTION_RIGHT - Helper::CHEVRON;
    Helper::drawIcon(painter, state, rect, Helper::line(rect, chevronLeft, Helper::CHEVRON, Helper::CHEVRON), Helper::chevronIcon(state), Helper::pointerIcon(state));

    // The name of a collection is data (a name given by the user): its own direction
    const qreal textLeft = circleRect.right() + Helper::COLLECTION_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, chevronLeft - Helper::COLLECTION_GAP - textLeft,
                          row.name, Helper::text(state, Color::TextPrimary), row.meta, Helper::text(state, Color::TextMuted), 1, Helper::TextKind::Data);
}

QSize PDFComponentPainter::getToolShortcutSize(const ToolShortcut& shortcut)
{
    Q_UNUSED(shortcut);
    return QSize(PDFComponentRowPainterHelper::SHORTCUT_WIDTH, PDFComponentRowPainterHelper::SHORTCUT_HEIGHT);
}

QSize PDFComponentPainter::getToolShortcutMinimumSize(const ToolShortcut& shortcut)
{
    Q_UNUSED(shortcut);
    using Helper = PDFComponentRowPainterHelper;
    return QSize(Helper::SHORTCUT_LEFT + Helper::BADGE + Helper::SHORTCUT_GAP + Helper::MIN_TEXT + Helper::SHORTCUT_RIGHT, Helper::SHORTCUT_HEIGHT);
}

qreal PDFComponentPainter::getToolShortcutRadius()
{
    return PDFComponentRowPainterHelper::SHORTCUT_RADIUS;
}

void PDFComponentPainter::paintToolShortcut(QPainter* painter, const QRectF& rect, const ToolShortcut& shortcut, const State& state, bool paintFocusRing)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::SHORTCUT_RADIUS);
    const bool isPointer = state.isHovered() || state.isPressed();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Hover and press darken the tile by the steps of the neutral hue
    Color fill = Color::BgSurface;
    if (state.isPressed())
    {
        fill = Color::AccentNeutralBgPressed;
    }
    else if (state.isHovered())
    {
        fill = Color::AccentNeutralBgHover;
    }
    PDFStylePainter::fill(painter, rect, radii, fill);

    // The focus ring outside replaces the border
    if (state.hasFocusRing())
    {
        if (paintFocusRing)
        {
            paintFocusRingOutside(painter, rect, radii);
        }
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, isPointer ? Color::BorderStrong : Color::BorderDefault, 1.0);
    }

    // The content is clipped to the tile, only the focus ring lies outside
    const PDFStylePainterGuard innerGuard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);
    const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::SHORTCUT_LEFT, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ shortcut.iconName, shortcut.hue, BadgeSize::Medium }, state);

    const qreal textLeft = badgeRect.right() + Helper::SHORTCUT_GAP;
    const qreal textWidth = rect.right() - Helper::SHORTCUT_RIGHT - textLeft;
    Helper::drawText(painter, state, rect, Helper::line(rect, textLeft, textWidth, Helper::TITLE_LINE), shortcut.name, Helper::titleFont(), Helper::text(state, Color::TextPrimary));
}

QSize PDFComponentPainter::getIssueRowSize(const IssueRow& row)
{
    Q_UNUSED(row);
    return QSize(PDFComponentRowPainterHelper::ISSUE_WIDTH, PDFComponentRowPainterHelper::ISSUE_HEIGHT);
}

QSize PDFComponentPainter::getIssueRowMinimumSize(const IssueRow& row)
{
    using Helper = PDFComponentRowPainterHelper;
    const int dateWidth = row.date.isEmpty() ? 0 : Helper::textWidth(row.date, Helper::subtitleFont()) + Helper::ISSUE_GAP;
    const int width = 2 * Helper::ISSUE_SIDE + Helper::ISSUE_STATUS + 2 * Helper::ISSUE_GAP + Helper::ISSUE_NUMBER + Helper::MIN_TEXT + dateWidth;
    return QSize(width, Helper::ISSUE_HEIGHT);
}

void PDFComponentPainter::paintIssueRow(QPainter* painter, const QRectF& rect, const IssueRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    // Status circle of the fixed green color
    const QRectF statusRect = Helper::line(rect, rect.left() + Helper::ISSUE_SIDE, Helper::ISSUE_STATUS, Helper::ISSUE_STATUS);
    const QRectF visualStatus = state.visualRect(rect, statusRect);
    PDFStylePainter::fillEllipse(painter, visualStatus, PDFStylePainter::color(Color::AccentGreenBg));
    if (!row.statusIconName.isEmpty())
    {
        PDFStylePainter::icon(painter, alignedCentered(visualStatus, QSizeF(Helper::ISSUE_STATUS_ICON, Helper::ISSUE_STATUS_ICON)), row.statusIconName, Helper::icon(state, Color::AccentGreenIcon));
    }

    const QFont titleFont = Helper::titleFont();
    const qreal numberLeft = statusRect.right() + Helper::ISSUE_GAP;
    const qreal numberWidth = qMin<qreal>(Helper::ISSUE_NUMBER, rect.right() - numberLeft);
    if (numberWidth > 0.0 && !row.number.isEmpty())
    {
        // The column has a fixed width, a longer number wraps (it is not elided);
        // the lines are centered vertically as the single line of a short number
        const QRectF numberRect = Helper::line(rect, numberLeft, numberWidth, rect.height());
        drawDataText(painter, state.visualRect(rect, numberRect), Qt::AlignVCenter | Qt::TextWrapAnywhere | state.leadingAlignment(),
                     row.number, titleFont, PDFStylePainter::color(Helper::text(state, Color::TextAccent)));
    }

    qreal textRight = rect.right() - Helper::ISSUE_SIDE;
    if (!row.date.isEmpty())
    {
        const QFont dateFont = Helper::subtitleFont();
        const qreal dateWidth = Helper::textWidth(row.date, dateFont);
        const QRectF dateRect = Helper::line(rect, textRight - dateWidth, dateWidth, Helper::SUBTITLE_LINE);
        if (dateRect.left() > numberLeft + Helper::ISSUE_NUMBER)
        {
            Helper::drawText(painter, state, rect, dateRect, row.date, dateFont, Helper::text(state, Color::TextMuted), true);
        }
        textRight = dateRect.left() - Helper::ISSUE_GAP;
    }

    const qreal textLeft = numberLeft + Helper::ISSUE_NUMBER + Helper::ISSUE_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, textRight - textLeft,
                          row.title, Helper::text(state, Color::TextPrimary), row.description, Helper::text(state, Color::TextMuted), 1, Helper::TextKind::Data, Helper::TextKind::Data);
}

QSize PDFComponentPainter::getVersionRowSize(const VersionRow& row)
{
    const int width = PDFComponentRowPainterHelper::VERSION_WIDTH;
    return QSize(width, getVersionRowHeight(row, width));
}

int PDFComponentPainter::getVersionRowHeight(const VersionRow& row, int width)
{
    using Helper = PDFComponentRowPainterHelper;

    const qreal textWidth = qreal(width) - 2 * Helper::VERSION_SIDE - Helper::VERSION_RAIL - Helper::VERSION_GAP;
    QTextLayout layout;
    const int lines = Helper::layoutSummary(layout, row.summary, Helper::subtitleFont(), textWidth, Qt::LeftToRight);
    const int summaryHeight = lines > 0 ? Helper::VERSION_TEXT_GAP + lines * Helper::VERSION_SUMMARY_LINE : 0;
    return Helper::TITLE_LINE + summaryHeight + Helper::VERSION_BOTTOM;
}

QSize PDFComponentPainter::getVersionRowMinimumSize(const VersionRow& row)
{
    using Helper = PDFComponentRowPainterHelper;
    const int width = 2 * Helper::VERSION_SIDE + Helper::VERSION_RAIL + Helper::VERSION_GAP + Helper::MIN_TEXT;
    return QSize(width, Helper::TITLE_LINE + Helper::VERSION_BOTTOM + (row.summary.isEmpty() ? 0 : Helper::VERSION_TEXT_GAP + Helper::VERSION_SUMMARY_LINE));
}

void PDFComponentPainter::paintVersionRow(QPainter* painter, const QRectF& rect, const VersionRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Selected (the shown version) bg/accent-subtle, the pointer and the press
    // in the tone of the fill, the focus ring inside
    const bool isSelected = row.selected || state.isSelected();
    const PDFStylePainter::Radii radii(Helper::ROW_RADIUS);
    if (state.isPressed())
    {
        PDFStylePainter::fill(painter, rect, radii, isSelected ? Color::BgAccentSubtlePressed : Color::BgPressed);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, rect, radii, isSelected ? Color::BgAccentSubtleHover : Color::BgHover);
    }
    else if (isSelected)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgAccentSubtle);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    // Rail: the dot at the top, the line below it to the bottom of the row
    const qreal railLeft = rect.left() + Helper::VERSION_SIDE;
    const QRectF dotRect(railLeft + (Helper::VERSION_RAIL - Helper::VERSION_DOT) / 2.0, rect.top() + Helper::VERSION_DOT_TOP, Helper::VERSION_DOT, Helper::VERSION_DOT);
    PDFStylePainter::fillEllipse(painter, state.visualRect(rect, dotRect), PDFStylePainter::color(row.current ? Color::BgAccent : Color::BorderStrong));

    const qreal lineTop = dotRect.bottom() + Helper::VERSION_LINE_GAP;
    if (row.lineVisible && rect.bottom() > lineTop)
    {
        const QRectF lineRect(railLeft + (Helper::VERSION_RAIL - Helper::VERSION_LINE) / 2.0, lineTop, Helper::VERSION_LINE, rect.bottom() - lineTop);
        PDFStylePainter::fillRect(painter, state.visualRect(rect, lineRect), Color::BgTrack);
    }

    // Version at the leading edge and the date at the trailing edge of the first line
    const qreal textLeft = railLeft + Helper::VERSION_RAIL + Helper::VERSION_GAP;
    const qreal textWidth = rect.right() - Helper::VERSION_SIDE - textLeft;
    const QFont dateFont = Helper::subtitleFont();
    const qreal dateWidth = qMin<qreal>(Helper::textWidth(row.date, dateFont), qMax<qreal>(0.0, textWidth));
    const QRectF dateRect(textLeft + textWidth - dateWidth, rect.top(), dateWidth, Helper::TITLE_LINE);
    Helper::drawText(painter, state, rect, dateRect, row.date, dateFont, Helper::text(state, Color::TextMuted), true);

    const qreal versionWidth = textWidth - (dateWidth > 0.0 ? dateWidth + Helper::VERSION_TOP_GAP : 0.0);
    Helper::drawDataText(painter, state, rect, QRectF(textLeft, rect.top(), versionWidth, Helper::TITLE_LINE), row.version, Helper::titleFont(),
                     Helper::text(state, isSelected ? Color::TextAccent : Color::TextPrimary));

    // Summary wraps into lines of 14 px
    QTextLayout layout;
    if (Helper::layoutSummary(layout, row.summary, dateFont, textWidth, state.direction) > 0)
    {
        const QRectF summaryRect = state.visualRect(rect, QRectF(textLeft, rect.top() + Helper::TITLE_LINE + Helper::VERSION_TEXT_GAP, textWidth, 1.0));
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setPen(PDFStylePainter::color(Helper::text(state, Color::TextTertiary)));
        layout.draw(painter, summaryRect.topLeft());
    }
}

/// Geometry of the rendering issue row (issue-row of 198:5950)
class PDFComponentIssueRowPainterHelper
{
public:
    PDFComponentIssueRowPainterHelper() = delete;

    static constexpr int WIDTH = 406;
    static constexpr int SIDE = 10;
    static constexpr int TOP = 8;
    static constexpr int BOTTOM = 8;
    static constexpr int ICON = 16;
    static constexpr int GAP = 10;
    static constexpr int PAGE_LINE = 13;
    static constexpr int TEXT_GAP = 2;
    static constexpr int MESSAGE_LINE = 15;
    static constexpr int MIN_TEXT = 40;

    static QFont pageFont() { return PDFThemeFonts::getFont(QFont::Medium, 11); }
    static QFont messageFont() { return PDFThemeFonts::getFont(QFont::Normal, 12); }

    /// Width of the text column of the row of the width
    static qreal textWidth(qreal width) { return width - 2 * SIDE - ICON - GAP; }

    /// Lays out the message (data, its own direction) into lines of 15 px aligned
    /// to the leading edge of the row. \returns the number of the lines.
    static int layoutMessage(QTextLayout& layout, const QString& text, qreal width, Qt::LayoutDirection rowDirection)
    {
        if (text.isEmpty() || width <= 0.0)
        {
            return 0;
        }

        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setTextDirection(PDFComponentPainter::getDataTextDirection(text));
        option.setAlignment((rowDirection == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute);

        layout.setText(text);
        layout.setFont(messageFont());
        layout.setTextOption(option);
        layout.beginLayout();

        int count = 0;
        for (QTextLine textLine = layout.createLine(); textLine.isValid(); textLine = layout.createLine())
        {
            textLine.setLineWidth(width);
            textLine.setPosition(QPointF(0.0, count * MESSAGE_LINE + (MESSAGE_LINE - textLine.height()) / 2.0));
            ++count;
        }

        layout.endLayout();
        return count;
    }
};

PDFComponentPainter::IssueSeverity PDFComponentPainter::normalized(IssueSeverity severity)
{
    switch (severity)
    {
        case IssueSeverity::Information:
        case IssueSeverity::Warning:
        case IssueSeverity::Error:
            return severity;
    }

    return IssueSeverity::Warning;
}

QString PDFComponentPainter::getIssueSeverityIconName(IssueSeverity severity)
{
    switch (normalized(severity))
    {
        case IssueSeverity::Information:
            return QStringLiteral("info");
        case IssueSeverity::Warning:
            break;
        case IssueSeverity::Error:
            return QStringLiteral("x-circle");
    }

    return QStringLiteral("alert-triangle");
}

PDFThemeColors::Color PDFComponentPainter::getIssueSeverityIconColor(IssueSeverity severity)
{
    switch (normalized(severity))
    {
        case IssueSeverity::Information:
            return PDFThemeColors::AccentBlueIcon;
        case IssueSeverity::Warning:
            break;
        case IssueSeverity::Error:
            return PDFThemeColors::AccentRedIcon;
    }

    return PDFThemeColors::AccentAmberIcon;
}

QSize PDFComponentPainter::getRenderingIssueRowSize(const RenderingIssueRow& row)
{
    const int width = PDFComponentIssueRowPainterHelper::WIDTH;
    return QSize(width, getRenderingIssueRowHeight(row, width));
}

int PDFComponentPainter::getRenderingIssueRowHeight(const RenderingIssueRow& row, int width)
{
    using Helper = PDFComponentIssueRowPainterHelper;

    QTextLayout layout;
    const int lines = Helper::layoutMessage(layout, row.message, Helper::textWidth(width), Qt::LeftToRight);
    const int messageHeight = qMax(lines, 1) * Helper::MESSAGE_LINE;
    return Helper::TOP + Helper::PAGE_LINE + Helper::TEXT_GAP + messageHeight + Helper::BOTTOM;
}

QSize PDFComponentPainter::getRenderingIssueRowMinimumSize(const RenderingIssueRow& row)
{
    Q_UNUSED(row);

    using Helper = PDFComponentIssueRowPainterHelper;
    return QSize(2 * Helper::SIDE + Helper::ICON + Helper::GAP + Helper::MIN_TEXT,
                 Helper::TOP + Helper::PAGE_LINE + Helper::TEXT_GAP + Helper::MESSAGE_LINE + Helper::BOTTOM);
}

void PDFComponentPainter::paintRenderingIssueRow(QPainter* painter, const QRectF& rect, const RenderingIssueRow& row, const State& state)
{
    if (!painter || rect.isEmpty() || !qIsFinite(rect.width()) || !qIsFinite(rect.height()))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using RowHelper = PDFComponentRowPainterHelper;
    using Helper = PDFComponentIssueRowPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    const PDFStylePainter::Radii radii(RowHelper::ROW_RADIUS);
    RowHelper::fillPointer(painter, rect, radii, state);
    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    // The icon of the severity at the top, aligned with the line of the page
    const QRectF iconRect(rect.left() + Helper::SIDE, rect.top() + Helper::TOP, Helper::ICON, Helper::ICON);
    RowHelper::drawIcon(painter, state, rect, iconRect, getIssueSeverityIconName(row.severity), RowHelper::icon(state, getIssueSeverityIconColor(row.severity)));

    const qreal textLeft = iconRect.right() + Helper::GAP;
    const qreal textWidth = rect.right() - Helper::SIDE - textLeft;
    if (textWidth <= 0.0)
    {
        return;
    }

    RowHelper::drawDataText(painter, state, rect, QRectF(textLeft, rect.top() + Helper::TOP, textWidth, Helper::PAGE_LINE), row.page,
                            Helper::pageFont(), RowHelper::text(state, Color::TextMuted));

    QTextLayout layout;
    if (Helper::layoutMessage(layout, row.message, textWidth, state.direction) > 0)
    {
        const qreal messageTop = rect.top() + Helper::TOP + Helper::PAGE_LINE + Helper::TEXT_GAP;
        const QRectF messageRect = state.visualRect(rect, QRectF(textLeft, messageTop, textWidth, 1.0));
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setPen(PDFStylePainter::color(RowHelper::text(state, Color::TextSecondary)));
        layout.draw(painter, messageRect.topLeft());
    }
}

PDFComponentPainter::PropertyState PDFComponentPainter::normalized(PropertyState propertyState, bool editable)
{
    switch (propertyState)
    {
        case PropertyState::Default:
        case PropertyState::Hover:
        case PropertyState::Copied:
            return propertyState;

        case PropertyState::Editing:
            return editable ? propertyState : PropertyState::Default;
    }

    return PropertyState::Default;
}

QSize PDFComponentPainter::getPropertyRowSize(const PropertyRow& row)
{
    Q_UNUSED(row);
    return QSize(PDFComponentRowPainterHelper::PROPERTY_WIDTH, PDFComponentRowPainterHelper::PROPERTY_HEIGHT);
}

QSize PDFComponentPainter::getPropertyRowMinimumSize(const PropertyRow& row)
{
    using Helper = PDFComponentRowPainterHelper;
    const int width = Helper::PROPERTY_LEFT + Helper::PROPERTY_GAP + Helper::MIN_TEXT + Helper::PROPERTY_GAP + Helper::propertyActionsWidth(row.editable) + Helper::PROPERTY_RIGHT;
    return QSize(width, Helper::PROPERTY_HEIGHT);
}

QMargins PDFComponentPainter::getPropertyRowTrailingMargins()
{
    return QMargins(0, 0, PDFComponentRowPainterHelper::PROPERTY_RIGHT, 0);
}

int PDFComponentPainter::getPropertyRowEditorTextInset()
{
    return PDFComponentRowPainterHelper::PROPERTY_EDITOR_BORDER + PDFComponentRowPainterHelper::PROPERTY_EDITOR_PADDING;
}

QFont PDFComponentPainter::getPropertyRowValueFont()
{
    return PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium);
}

QRectF PDFComponentPainter::getPropertyRowEditorRect(const QRectF& rect, const PropertyRow& row, const State& state)
{
    using Helper = PDFComponentRowPainterHelper;
    const QRectF valueRect = Helper::propertyValueRect(rect, row.editable);
    return state.visualRect(rect, Helper::line(rect, valueRect.left(), valueRect.width(), qMin<qreal>(Helper::PROPERTY_EDITOR_HEIGHT, rect.height())));
}

void PDFComponentPainter::paintPropertyRow(QPainter* painter, const QRectF& rect, const PropertyRow& row, const State& state)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    PropertyState propertyState = normalized(row.state, row.editable);
    if (propertyState == PropertyState::Default && state.isHovered())
    {
        propertyState = PropertyState::Hover;
    }
    if (!state.isEnabled() && propertyState == PropertyState::Hover)
    {
        // A disabled row has no hover (O-08)
        propertyState = PropertyState::Default;
    }

    const PDFStylePainter::Radii radii(Helper::PROPERTY_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    if (propertyState != PropertyState::Default)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
    }

    // Key of a fixed width, the value fills the rest before the actions
    const QFont keyFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
    const QFont valueFont = getPropertyRowValueFont();
    const qreal keyWidth = Helper::propertyKeyWidth(rect.width(), row.editable);
    Helper::drawText(painter, state, rect, Helper::line(rect, rect.left() + Helper::PROPERTY_LEFT, keyWidth, Helper::SMALL_LINE), row.key, keyFont, Helper::text(state, Color::TextTertiary));

    const QRectF valueRect = Helper::propertyValueRect(rect, row.editable);
    if (propertyState == PropertyState::Editing)
    {
        // Frame of the inline editor: bg/surface with the focus ring inside
        const QRectF editorRect = getPropertyRowEditorRect(rect, row, state);
        if (!editorRect.isEmpty())
        {
            PDFStylePainter::fill(painter, editorRect, radii, Color::BgSurface);
            paintFocusRingInside(painter, editorRect, radii);
        }

        if (row.paintNested)
        {
            const qreal inset = getPropertyRowEditorTextInset();
            const QRectF textRect(valueRect.left() + inset, valueRect.top(), valueRect.width() - 2 * inset, valueRect.height());
            Helper::drawDataText(painter, state, rect, Helper::line(rect, textRect.left(), textRect.width(), Helper::SMALL_LINE), row.value, valueFont, Helper::text(state, Color::TextPrimary));
        }
    }
    else
    {
        Helper::drawDataText(painter, state, rect, Helper::line(rect, valueRect.left(), valueRect.width(), Helper::SMALL_LINE), row.value, valueFont, Helper::text(state, Color::TextPrimary));
    }

    if (row.paintNested)
    {
        // Actions: Icon buttons 24 at the trailing edge, the first one of an editable row is edit
        const qreal actionsLeft = rect.right() - Helper::PROPERTY_RIGHT - Helper::propertyActionsWidth(row.editable);
        const QRectF firstRect = Helper::line(rect, actionsLeft, Helper::PROPERTY_ACTION, Helper::PROPERTY_ACTION);
        const QRectF secondRect = firstRect.translated(Helper::PROPERTY_ACTION, 0.0);
        const QRectF copyRect = row.editable ? secondRect : firstRect;
        const State buttonState = Helper::nestedState(state, false);

        if (propertyState == PropertyState::Editing)
        {
            paintPropertyRowAction(painter, state.visualRect(rect, firstRect), PropertyAction::Confirm, propertyState, buttonState);
            paintPropertyRowAction(painter, state.visualRect(rect, secondRect), PropertyAction::Cancel, propertyState, buttonState);
        }
        else
        {
            if (row.editable)
            {
                paintPropertyRowAction(painter, state.visualRect(rect, firstRect), PropertyAction::Edit, propertyState, buttonState);
            }
            if (isPropertyRowCopyShown(propertyState, false))
            {
                paintPropertyRowAction(painter, state.visualRect(rect, copyRect), PropertyAction::Copy, propertyState, buttonState);
            }
        }
    }
}

PDFComponentPainter::PropertyAction PDFComponentPainter::normalized(PropertyAction action)
{
    switch (action)
    {
        case PropertyAction::Edit:
        case PropertyAction::Copy:
        case PropertyAction::Confirm:
        case PropertyAction::Cancel:
            return action;
    }

    return PropertyAction::Copy;
}

bool PDFComponentPainter::isPropertyRowCopyShown(PropertyState propertyState, bool hasFocus)
{
    switch (propertyState)
    {
        case PropertyState::Hover:
        case PropertyState::Copied:
            return true;

        case PropertyState::Default:
            return hasFocus;

        case PropertyState::Editing:
            break;
    }

    return false;
}

void PDFComponentPainter::paintPropertyRowAction(QPainter* painter, const QRectF& rect, PropertyAction action, PropertyState propertyState, const State& buttonState)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, buttonState);

    using Helper = PDFComponentRowPainterHelper;
    using Color = PDFThemeColors::Color;

    action = normalized(action);
    propertyState = normalized(propertyState, true);
    const bool isHighlighted = propertyState == PropertyState::Hover || propertyState == PropertyState::Copied;

    if (action == PropertyAction::Copy && propertyState == PropertyState::Copied)
    {
        // The confirmation of the copy: the green check without a background
        const qreal extent = qMin<qreal>(Helper::PROPERTY_ACTION_ICON, qMin(rect.width(), rect.height()));
        const PDFStylePainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::icon(painter, alignedCentered(rect, QSizeF(extent, extent)), QStringLiteral("check"), Helper::icon(buttonState, Color::AccentGreenIcon));
        return;
    }

    IconButton button;
    button.extent = Helper::PROPERTY_ACTION;
    button.iconExtent = Helper::PROPERTY_ACTION_ICON;
    button.radius = Helper::PROPERTY_RADIUS;

    // The color of the icon overridden by the row (card Property row 72): it holds
    // in every state of the button, the hover and the press of the button itself
    // change only its background (card Icon button 706:147674, review F6 of step 5)
    switch (action)
    {
        case PropertyAction::Edit:
            button.iconName = QStringLiteral("edit");
            if (isHighlighted)
            {
                button.iconColor = Color::IconDefault;
            }
            break;

        case PropertyAction::Copy:
            button.iconName = QStringLiteral("copy");
            button.iconColor = Color::IconDefault;
            break;

        case PropertyAction::Confirm:
            button.iconName = QStringLiteral("check");
            button.iconColor = Color::IconAccent;
            break;

        case PropertyAction::Cancel:
            button.iconName = QStringLiteral("x");
            break;
    }

    paintIconButton(painter, rect, button, buttonState);
}

}   // namespace pdf
