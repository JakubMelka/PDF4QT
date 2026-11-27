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

// Drawing of the sections of the panels of Edit Content (Expander card) and of
// the path bar (breadcrumbs without routes). The class is declared in
// pdfcomponentpainter.h; this file holds their geometry constants.

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QPainter>
#include <QtMath>

namespace pdf
{

/// Geometry constants of the expander card and of the path bar
class PDFComponentSectionPainterHelper
{
public:
    PDFComponentSectionPainterHelper() = delete;

    // Expander card (section-Transparency 1647:21843): header gap 8, header to
    // card 8, card border 1 + padding 12, items 8 apart, radius 10
    static constexpr int EXPANDER_SPACING = 8;
    static constexpr int EXPANDER_CARD_MARGIN = 1 + 12;
    static constexpr int EXPANDER_CARD_SPACING = 8;
    static constexpr qreal EXPANDER_CARD_RADIUS = 10.0;

    // Path bar (isolation-bar 1647:42965, breadcrumb 1647:21730): gaps 8 / 4,
    // lines 16 / 13, chevron 10, radius of the focus ring of a link 4
    static constexpr int PATH_SPACING_DEFAULT = 8;
    static constexpr int PATH_SPACING_SMALL = 4;
    static constexpr int PATH_LINE_DEFAULT = 16;
    static constexpr int PATH_LINE_SMALL = 13;
    static constexpr int PATH_SEPARATOR = 10;
    static constexpr qreal PATH_RADIUS = 4.0;

    // Toggle action of a row: the icon 12 (1647:22603)
    static constexpr int TOGGLE_ICON = 12;

    /// Length of a text of a segment, which is measured and drawn (a hostile
    /// name of an object of a document may be megabytes long)
    static constexpr qsizetype MAX_SEGMENT_TEXT = 1024;

    static QString boundedText(const QString& text)
    {
        return text.size() > MAX_SEGMENT_TEXT ? text.left(MAX_SEGMENT_TEXT) : text;
    }
};

QFont PDFComponentPainter::getExpanderTitleFont()
{
    return PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold);
}

QFont PDFComponentPainter::getExpanderSummaryFont()
{
    return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
}

int PDFComponentPainter::getExpanderSpacing()
{
    return PDFComponentSectionPainterHelper::EXPANDER_SPACING;
}

QMargins PDFComponentPainter::getExpanderCardMargins()
{
    const int margin = PDFComponentSectionPainterHelper::EXPANDER_CARD_MARGIN;
    return QMargins(margin, margin, margin, margin);
}

int PDFComponentPainter::getExpanderCardSpacing()
{
    return PDFComponentSectionPainterHelper::EXPANDER_CARD_SPACING;
}

qreal PDFComponentPainter::getExpanderCardRadius()
{
    return PDFComponentSectionPainterHelper::EXPANDER_CARD_RADIUS;
}

void PDFComponentPainter::paintExpanderCard(QPainter* painter, const QRectF& rect, const State& state)
{
    Q_UNUSED(state);

    if (!painter || rect.isEmpty() || !rect.isValid())
    {
        return;
    }

    const PDFStylePainter::Radii radii(PDFComponentSectionPainterHelper::EXPANDER_CARD_RADIUS);
    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(painter, rect, radii, PDFThemeColors::BorderDefault, 1.0);
}

PDFComponentPainter::PathBarSize PDFComponentPainter::normalized(PathBarSize size)
{
    switch (size)
    {
        case PathBarSize::Default:
        case PathBarSize::Small:
            return size;
    }

    return PathBarSize::Default;
}

QFont PDFComponentPainter::getPathSegmentFont(PathBarSize size, bool isCurrent, bool isUnderlined)
{
    PDFThemeFonts::Role role = PDFThemeFonts::Role::Small;
    if (normalized(size) == PathBarSize::Small)
    {
        role = isCurrent ? PDFThemeFonts::Role::Caption : PDFThemeFonts::Role::CaptionRegular;
    }
    else
    {
        role = isCurrent ? PDFThemeFonts::Role::SmallSemiBold : PDFThemeFonts::Role::Small;
    }

    QFont font = PDFThemeFonts::getFont(role);
    font.setUnderline(isUnderlined);
    return font;
}

int PDFComponentPainter::getPathBarSpacing(PathBarSize size)
{
    return normalized(size) == PathBarSize::Small ? PDFComponentSectionPainterHelper::PATH_SPACING_SMALL : PDFComponentSectionPainterHelper::PATH_SPACING_DEFAULT;
}

int PDFComponentPainter::getPathBarLineHeight(PathBarSize size)
{
    return normalized(size) == PathBarSize::Small ? PDFComponentSectionPainterHelper::PATH_LINE_SMALL : PDFComponentSectionPainterHelper::PATH_LINE_DEFAULT;
}

int PDFComponentPainter::getPathSeparatorExtent()
{
    return PDFComponentSectionPainterHelper::PATH_SEPARATOR;
}

qreal PDFComponentPainter::getPathSegmentRadius()
{
    return PDFComponentSectionPainterHelper::PATH_RADIUS;
}

QString PDFComponentPainter::getPathEllipsis()
{
    return QString(QChar(0x2026));
}

qreal PDFComponentPainter::getPathSegmentTextWidth(const PathSegment& segment)
{
    return PDFStylePainter::textWidth(PDFComponentSectionPainterHelper::boundedText(segment.text), getPathSegmentFont(segment.size, segment.current, false));
}

QSize PDFComponentPainter::getPathSegmentSize(const PathSegment& segment)
{
    return QSize(qCeil(getPathSegmentTextWidth(segment)), getPathBarLineHeight(segment.size));
}

void PDFComponentPainter::paintPathSegment(QPainter* painter, const QRectF& rect, const PathSegment& segment, const State& state)
{
    if (!painter || rect.isEmpty() || !rect.isValid())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    PDFThemeColors::Color textColor = segment.current ? PDFThemeColors::TextPrimary : PDFThemeColors::TextAccent;
    if (!state.isEnabled())
    {
        textColor = PDFThemeColors::TextDisabled;
    }
    else if (!segment.current && state.isPressed())
    {
        textColor = PDFThemeColors::TextAccentPressed;
    }

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Hover and press underline a link, the focus ring does not (as Text link)
    const bool isUnderlined = !segment.current && (state.isHovered() || state.isPressed());
    const QFont font = getPathSegmentFont(segment.size, segment.current, isUnderlined);
    drawElidedText(painter, state, rect, PDFComponentSectionPainterHelper::boundedText(segment.text), font, textColor, TextKind::Data);

    if (state.hasFocusRing() && !segment.current)
    {
        paintFocusRingOutside(painter, rect, PDFStylePainter::Radii(PDFComponentSectionPainterHelper::PATH_RADIUS));
    }
}

void PDFComponentPainter::paintPathSeparator(QPainter* painter, const QRectF& rect, const State& state)
{
    if (!painter || rect.isEmpty() || !rect.isValid())
    {
        return;
    }

    const qreal extent = PDFComponentSectionPainterHelper::PATH_SEPARATOR;
    const QRectF iconRect = alignedCentered(rect, QSizeF(extent, extent));
    const PDFThemeColors::Color color = state.isEnabled() ? PDFThemeColors::IconMuted : PDFThemeColors::IconDisabled;
    PDFStylePainter::icon(painter, iconRect, state.isRightToLeft() ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"), color);
}

void PDFComponentPainter::paintPathEllipsis(QPainter* painter, const QRectF& rect, PathBarSize size, const State& state)
{
    if (!painter || rect.isEmpty() || !rect.isValid())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const PDFThemeColors::Color color = state.isEnabled() ? PDFThemeColors::TextTertiary : PDFThemeColors::TextDisabled;
    PDFStylePainter::text(painter, rect, Qt::AlignCenter | Qt::TextSingleLine, getPathEllipsis(), getPathSegmentFont(size, false, false), PDFStylePainter::color(color));
}

qreal PDFComponentPainter::getPathEllipsisWidth(PathBarSize size)
{
    return PDFStylePainter::textWidth(getPathEllipsis(), getPathSegmentFont(size, false, false));
}

int PDFComponentPainter::getRowToggleIconExtent()
{
    return PDFComponentSectionPainterHelper::TOGGLE_ICON;
}

void PDFComponentPainter::paintRowToggleAction(QPainter* painter, const QRectF& rect, const QString& iconName, const State& state)
{
    if (!painter || rect.isEmpty() || !rect.isValid() || iconName.isEmpty())
    {
        return;
    }

    PDFThemeColors::Color color = PDFThemeColors::IconMuted;
    if (!state.isEnabled())
    {
        color = PDFThemeColors::IconDisabled;
    }
    else if (state.isPressed())
    {
        color = PDFThemeColors::IconStrong;
    }
    else if (state.isHovered())
    {
        color = PDFThemeColors::IconDefault;
    }

    const qreal extent = PDFComponentSectionPainterHelper::TOGGLE_ICON;
    PDFStylePainter::icon(painter, alignedCentered(rect, QSizeF(extent, extent)), iconName, color);
}

}   // namespace pdf
