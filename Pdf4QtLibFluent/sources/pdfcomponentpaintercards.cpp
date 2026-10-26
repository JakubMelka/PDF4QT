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

// Drawing of the tiles and cards of PDFComponentPainter (step 3 of components.md,
// chapter 6.3). The class is declared in pdfcomponentpainter.h; this file holds
// the geometry constants of the cards of the specification.

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QtMath>

#include <limits>

namespace pdf
{

/// Geometry constants of the cards of the specification and their drawing helpers
class PDFComponentCardPainterHelper
{
public:
    PDFComponentCardPainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using State = PDFComponentPainter::State;
    using Role = PDFAccentHueColors::Role;

    static constexpr int MIN_TEXT = 40;             ///< Minimal width of an elided text column
    static constexpr int UNLIMITED = std::numeric_limits<int>::max();
    static constexpr qreal CARD_RADIUS = 12.0;
    static constexpr int MORE = 16;

    // Lines of the texts (automatic line heights of Inter; multi-line texts of
    // 130 % / 135 % on whole pixels, decision 30)
    static constexpr int LINE_SEMIBOLD_13 = 16;
    static constexpr int LINE_REGULAR_11 = 13;
    static constexpr int LINE_12 = 15;
    static constexpr int LINE_BOLD_14 = 17;
    static constexpr int LINE_BOLD_15 = 18;
    static constexpr int LINE_BOLD_13 = 16;
    static constexpr int LINE_12_WRAPPED = 16;      ///< Regular 12 / 130 % and 135 % (15.6 and 16.2)
    static constexpr int LINE_11_WRAPPED = 14;      ///< Regular 11 / 130 % (14.3)

    // Quick action tile 46:431: 224 x 88, padding 16 (the border is not counted
    // in the layout), circle 40 with an icon 20, gap 12, title Bold 14 and the
    // description Regular 12 with a gap of 2, star 28
    static constexpr int TILE_WIDTH = 224;
    static constexpr int TILE_HEIGHT = 88;
    static constexpr int TILE_PADDING = 16;
    static constexpr int TILE_MIN_PADDING = 8;      ///< Texts of more lines may reach 8 px from the edges
    static constexpr int TILE_CIRCLE = 40;
    static constexpr int TILE_ICON = 20;
    static constexpr int TILE_GAP = 12;
    static constexpr int TILE_STAR = 28;
    static constexpr int TILE_TEXT_GAP = 2;
    static constexpr int TILE_TITLE_LINES = 2;

    // Folder card 63:597: 252 x 84, border 1 counted in the layout, padding
    // 12, 10, 12, 14, circle 36 with an icon 18, gap 12, texts with gaps of 2
    static constexpr int FOLDER_WIDTH = 252;
    static constexpr int FOLDER_HEIGHT = 84;
    static constexpr int FOLDER_LEFT = 15;
    static constexpr int FOLDER_RIGHT = 11;
    static constexpr int FOLDER_CIRCLE = 36;
    static constexpr int FOLDER_ICON = 18;
    static constexpr int FOLDER_GAP = 12;
    static constexpr int FOLDER_TEXT_GAP = 2;

    // Pinned card 79:1115: 259 x 100, border 1 counted in the layout, padding
    // 12, 8, 12, 12, thumbnail 56 x 74, gap 12, texts with gaps of 3, bar 4,
    // column of the star 24 and the menu 16 with a gap of 4
    static constexpr int PINNED_WIDTH = 259;
    static constexpr int PINNED_HEIGHT = 100;
    static constexpr int PINNED_INSET = 13;
    static constexpr int PINNED_RIGHT = 9;
    static constexpr int PINNED_GAP = 12;
    static constexpr int PINNED_TEXT_GAP = 3;
    static constexpr int PINNED_STAR = 24;
    static constexpr int PINNED_ACTIONS_GAP = 4;
    static constexpr int PINNED_BAR = 4;

    // Tool category header 89:1424: 393 x 61 (bottom border 1 counted), padding
    // 12, 16, circle 36 with an icon 18, gap 12, title Bold 15 and description
    // Regular 12 with a gap of 2, count Medium 12
    static constexpr int HEADER_WIDTH = 393;
    static constexpr int HEADER_HEIGHT = 61;
    static constexpr int HEADER_SIDE = 16;
    static constexpr int HEADER_CIRCLE = 36;
    static constexpr int HEADER_ICON = 18;
    static constexpr int HEADER_GAP = 12;
    static constexpr int HEADER_TEXT_GAP = 2;

    // Tool choice 417:1006: 154 x 48, radius 10, the content 11 px from the
    // sides (padding 10 and the border 1, or padding 11 of the selected choice
    // with its ring of 2 px not counted), badge 28, gap 8, name Medium 12 in two
    // lines at most, check 16
    static constexpr int CHOICE_WIDTH = 154;
    static constexpr int MAX_EXTENT = 1 << 20;      ///< Largest width of a layout (larger values are bounded)
    static constexpr qreal CHOICE_RADIUS = 10.0;
    static constexpr int CHOICE_SIDE = 11;
    static constexpr int CHOICE_BADGE = 28;
    static constexpr int CHOICE_GAP = 8;
    static constexpr int CHOICE_CHECK = 16;
    static constexpr int CHOICE_VERTICAL = 9;       ///< Border 1 and padding 8 (or the padding 9 of the selected card)
    static constexpr int CHOICE_MAX_LINES = 1000;   ///< Bound of the lines of a name (the card hugs its name)

    /// Returns the width of the name of the selected choice of the width (the
    /// lines of the name are counted at it in each state, so the height is stable)
    static int getChoiceNameWidth(int width)
    {
        return width - 2 * CHOICE_SIDE - CHOICE_BADGE - 2 * CHOICE_GAP - CHOICE_CHECK;
    }

    // Resource card 50:242: 116 wide, padding 16, 14 (the border is not
    // counted), icon 24 in the place 36 x 32, gap 12, title Bold 13 and the
    // description Regular 11 / 130 % with a gap of 4
    static constexpr int RESOURCE_WIDTH = 116;
    static constexpr int RESOURCE_TOP = 16;
    static constexpr int RESOURCE_SIDE = 14;
    static constexpr int RESOURCE_ICON_WIDTH = 36;
    static constexpr int RESOURCE_ICON_HEIGHT = 32;
    static constexpr int RESOURCE_ICON = 24;
    static constexpr int RESOURCE_GAP = 12;
    static constexpr int RESOURCE_TEXT_GAP = 4;

    // Highlight card 103:1894: 259 wide, border 1 counted, padding 16, circle 40
    // with an icon 20, gap 10, title Bold 14 and the description Regular 12 /
    // 130 % with a gap of 4
    static constexpr int HIGHLIGHT_WIDTH = 259;
    static constexpr int HIGHLIGHT_INSET = 17;
    static constexpr int HIGHLIGHT_CIRCLE = 40;
    static constexpr int HIGHLIGHT_ICON = 20;
    static constexpr int HIGHLIGHT_GAP = 10;
    static constexpr int HIGHLIGHT_TEXT_GAP = 4;

    // Video card 116:2418: 272 wide, border 1 counted, preview 150 over the inner
    // width, body padding 12, title Semi Bold 13 (one line), description Regular
    // 12 / 135 % (two lines at most) with a gap of 3, tags after a gap of 10 with
    // gaps of 6; play 44 (triangle 11.5 x 13), duration 8 px from the corner
    static constexpr int VIDEO_WIDTH = 272;
    static constexpr int VIDEO_THUMBNAIL = 150;
    static constexpr int VIDEO_PADDING = 12;
    static constexpr int VIDEO_TEXT_GAP = 3;
    static constexpr int VIDEO_DESCRIPTION_LINES = 2;
    static constexpr int VIDEO_TAGS_GAP = 10;
    static constexpr int VIDEO_TAG_GAP = 6;
    static constexpr int VIDEO_PLAY = 44;
    static constexpr int VIDEO_DURATION_SIDE = 7;
    static constexpr int VIDEO_DURATION_HEIGHT = 17;
    static constexpr int VIDEO_DURATION_OFFSET = 8;
    static constexpr qreal VIDEO_DURATION_RADIUS = 6.0;

    // Reading theme card 156:4218 and Color mode card 171:4545: padding 5, 5, 7,
    // 5, the sample (53 high, radius 8, border 1 counted, padding 12) or the
    // picture (89), gap 6, the label of 15 px with padding 5, check 14 after a gap of 6
    static constexpr int READING_WIDTH = 159;
    static constexpr int READING_HEIGHT = 86;
    static constexpr int READING_SAMPLE_HEIGHT = 53;
    static constexpr qreal READING_SAMPLE_RADIUS = 8.0;
    static constexpr int READING_SAMPLE_INSET = 13;
    static constexpr int READING_LETTERS_LINE = 27;
    static constexpr int READING_LETTERS_GAP = 8;
    static constexpr int READING_SAMPLE_LINE = 12;
    static constexpr int READING_SAMPLE_GAP = 2;
    static constexpr int PREVIEW_CARD_WIDTH = 125;
    static constexpr int PREVIEW_CARD_HEIGHT = 122;
    static constexpr int CHOICE_CARD_PADDING = 5;
    static constexpr int CHOICE_CARD_BOTTOM = 7;
    static constexpr int CHOICE_CARD_GAP = 6;
    static constexpr int CHOICE_CARD_LABEL = 15;
    static constexpr int CHOICE_CARD_LABEL_SIDE = 5;
    static constexpr int CHOICE_CARD_CHECK = 14;

    static QFont font(PDFThemeFonts::Role role) { return PDFThemeFonts::getFont(role); }

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

    /// Returns the logical rectangle of the height centered vertically in the outer rectangle
    static QRectF line(const QRectF& outer, qreal left, qreal width, qreal height)
    {
        return QRectF(left, outer.top() + qFloor((outer.height() - height) / 2.0), qMax<qreal>(0.0, width), height);
    }

    /// Draws a text of a single line, elided at its end, into the logical rectangle
    static void drawText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& text,
                         const QFont& font, Color color, bool isTrailing = false)
    {
        if (logical.width() > 0.0 && logical.height() > 0.0)
        {
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(outer, logical), text, font, color, PDFComponentPainter::TextKind::Interface, isTrailing);
        }
    }

    /// Draws a text of data (a name, a path) of a single line, elided at its end,
    /// into the logical rectangle: its base direction is its own (components.md 6.6)
    static void drawDataText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& text,
                             const QFont& font, Color color)
    {
        if (logical.width() > 0.0 && logical.height() > 0.0)
        {
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(outer, logical), text, font, color, PDFComponentPainter::TextKind::Data);
        }
    }

    /// Draws an icon into the logical rectangle
    static void drawIcon(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& name, Color color)
    {
        if (!name.isEmpty() && !logical.isEmpty())
        {
            PDFStylePainter::icon(painter, state.visualRect(outer, logical), name, color);
        }
    }

    /// Draws a circle with an icon in its middle into the logical rectangle
    static void drawCircleIcon(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, Color fill,
                               const QString& name, qreal iconExtent, Color iconColor)
    {
        if (logical.isEmpty())
        {
            return;
        }

        const QRectF circle = state.visualRect(outer, logical);
        PDFStylePainter::fillEllipse(painter, circle, PDFStylePainter::color(fill));
        if (!name.isEmpty())
        {
            PDFStylePainter::icon(painter, PDFComponentPainter::alignedCentered(circle, QSizeF(iconExtent, iconExtent)), name, iconColor);
        }
    }

    /// Splits the text into lines of the width, at most \p maxLines lines (\p PDFComponentPainter::wrapText)
    static QStringList wrap(const QString& text, const QFont& font, qreal width, int maxLines)
    {
        return PDFComponentPainter::wrapText(text, font, width, maxLines);
    }

    /// Draws the lines from the top of the logical rectangle, one line of the height each
    static void drawLines(QPainter* painter, const State& state, const QRectF& outer, qreal left, qreal top, qreal width, int lineHeight,
                          const QStringList& lines, const QFont& font, Color color)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            drawText(painter, state, outer, QRectF(left, top + i * lineHeight, width, lineHeight), lines.at(i), font, color);
        }
    }

    /// Wrapped title and description of a card
    struct TextBlock
    {
        QStringList title;
        QStringList description;
        int titleLine = 0;
        int descriptionLine = 0;
        int gap = 0;

        int height() const
        {
            const int titleHeight = int(title.size()) * titleLine;
            const int descriptionHeight = int(description.size()) * descriptionLine;
            return titleHeight + descriptionHeight + ((titleHeight > 0 && descriptionHeight > 0) ? gap : 0);
        }
    };

    /// Lays the title and the description out into the width; the block is at
    /// most \p maxHeight high (the title is kept first, the description gets the
    /// rest), the title has at most \p titleMaxLines lines
    static TextBlock layoutBlock(const QString& title, const QFont& titleFont, int titleLine, int titleMaxLines,
                                 const QString& description, const QFont& descriptionFont, int descriptionLine, int descriptionMaxLines,
                                 int gap, qreal width, int maxHeight)
    {
        TextBlock block;
        block.titleLine = titleLine;
        block.descriptionLine = descriptionLine;
        block.gap = gap;

        maxHeight = qMax(0, maxHeight);
        const int titleLines = qMin(titleMaxLines, titleLine > 0 ? maxHeight / titleLine : 0);
        block.title = wrap(title, titleFont, width, qMax(1, titleLines));
        if (titleLines <= 0)
        {
            block.title.clear();
        }

        int available = maxHeight - int(block.title.size()) * titleLine;
        if (!block.title.isEmpty())
        {
            available -= gap;
        }

        const int descriptionLines = qMin(descriptionMaxLines, descriptionLine > 0 ? qMax(0, available) / descriptionLine : 0);
        if (descriptionLines > 0)
        {
            block.description = wrap(description, descriptionFont, width, descriptionLines);
        }
        return block;
    }

    /// Returns the state of a nested element drawn by a card: enabled as the
    /// card, in the direction of the card, checked when \p isOn
    static State nestedState(const State& cardState, bool isOn)
    {
        QStyle::State flags = cardState.isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
        if (isOn)
        {
            flags |= QStyle::State_On;
        }
        return State(flags, cardState.direction);
    }

    /// Returns the state of a menu button drawn by a card: the pointer over the
    /// card (or its press) makes the icon darker, the focus of the card does not
    static State menuState(const State& cardState)
    {
        return State(cardState.flags & (QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken), cardState.direction);
    }

    /// Fills a card of a hue by the recipe of Quick action tile, Folder card and
    /// Resource card: at rest bg and border, under the pointer bg-hover and
    /// border-strong, pressed bg-pressed and border-strong; the focus ring outside
    /// replaces the border (drawn only with \p paintFocusRing)
    static void paintHuedCard(QPainter* painter, const QRectF& rect, PDFAccentHue hue, const State& state, bool paintFocusRing)
    {
        const PDFStylePainter::Radii radii(CARD_RADIUS);

        Role fill = Role::Background;
        if (state.isPressed())
        {
            fill = Role::BackgroundPressed;
        }
        else if (state.isHovered())
        {
            fill = Role::BackgroundHover;
        }
        PDFStylePainter::fill(painter, rect, radii, PDFAccentHueColors::getColor(hue, fill));

        if (state.hasFocusRing())
        {
            if (paintFocusRing)
            {
                PDFComponentPainter::paintFocusRingOutside(painter, rect, radii);
            }
        }
        else
        {
            const bool isPointer = state.isHovered() || state.isPressed();
            PDFStylePainter::strokeInside(painter, rect, radii, PDFAccentHueColors::getColor(hue, isPointer ? Role::BorderStrong : Role::Border), 1.0);
        }
    }

    /// Fills a white card of its own surface: bg/surface with the overlay of the
    /// pointer (decision 21), the border border/default (border/strong under the
    /// pointer), which the focus ring outside replaces
    static void paintSurfaceCard(QPainter* painter, const QRectF& rect, const State& state, bool paintFocusRing)
    {
        const PDFStylePainter::Radii radii(CARD_RADIUS);
        PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
        if (state.isPressed())
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgPressed);
        }
        else if (state.isHovered())
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
        }

        if (state.hasFocusRing())
        {
            if (paintFocusRing)
            {
                PDFComponentPainter::paintFocusRingOutside(painter, rect, radii);
            }
        }
        else
        {
            const bool isPointer = state.isHovered() || state.isPressed();
            PDFStylePainter::strokeInside(painter, rect, radii, isPointer ? Color::BorderStrong : Color::BorderDefault, 1.0);
        }
    }

    /// Draws the label row of a choice card (Reading theme card, Color mode card):
    /// the name and the check of the selected card
    static void paintChoiceLabel(QPainter* painter, const QRectF& rect, const QString& name, const State& state)
    {
        const qreal top = rect.top() + rect.height() - CHOICE_CARD_BOTTOM - CHOICE_CARD_LABEL;
        const qreal left = rect.left() + CHOICE_CARD_PADDING + CHOICE_CARD_LABEL_SIDE;
        qreal right = rect.right() - CHOICE_CARD_PADDING - CHOICE_CARD_LABEL_SIDE;

        if (state.isOn())
        {
            const QRectF check(right - CHOICE_CARD_CHECK, top + qFloor((CHOICE_CARD_LABEL - CHOICE_CARD_CHECK) / 2.0), CHOICE_CARD_CHECK, CHOICE_CARD_CHECK);
            drawIcon(painter, state, rect, check, QStringLiteral("check"), icon(state, Color::IconAccent));
            right = check.left() - CHOICE_CARD_GAP;
        }

        drawText(painter, state, rect, QRectF(left, top, right - left, CHOICE_CARD_LABEL), name, font(PDFThemeFonts::Role::SmallSemiBold),
                 text(state, state.isOn() ? Color::TextAccent : Color::TextPrimary));
    }

    /// Draws the background of a choice card: the overlay of the pointer of the
    /// card, which is not selected, or the fill of the selection (the selected
    /// card does not react to the pointer)
    static void paintChoiceBackground(QPainter* painter, const QRectF& rect, const State& state)
    {
        const PDFStylePainter::Radii radii(CARD_RADIUS);
        if (state.isOn())
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgAccentSubtle);
        }
        else if (state.isPressed())
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgPressed);
        }
        else if (state.isHovered())
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
        }
    }

    /// Draws the ring of the selection (inside) and the focus ring (outside) of a
    /// choice card. The selection ring of a disabled card is border/strong (review O-09).
    static void paintChoiceRings(QPainter* painter, const QRectF& rect, const State& state)
    {
        const PDFStylePainter::Radii radii(CARD_RADIUS);
        if (state.isOn())
        {
            PDFStylePainter::strokeInside(painter, rect, radii, state.isEnabled() ? Color::BgAccent : Color::BorderStrong, 2.0);
        }
        if (state.hasFocusRing())
        {
            PDFComponentPainter::paintFocusRingOutside(painter, rect, radii);
        }
    }

    /// Returns the height of the text block of a resource card of the width
    static TextBlock resourceBlock(const PDFComponentPainter::ResourceCard& card, qreal width)
    {
        return layoutBlock(card.title, font(PDFThemeFonts::Role::BodyBold), LINE_BOLD_13, UNLIMITED,
                           card.description, font(PDFThemeFonts::Role::CaptionRegular), LINE_11_WRAPPED, UNLIMITED,
                           RESOURCE_TEXT_GAP, width, UNLIMITED);
    }

    /// Returns the text block of a highlight card of the width
    static TextBlock highlightBlock(const PDFComponentPainter::HighlightCard& card, qreal width)
    {
        return layoutBlock(card.title, font(PDFThemeFonts::Role::TileTitle), LINE_BOLD_14, UNLIMITED,
                           card.description, font(PDFThemeFonts::Role::Small), LINE_12_WRAPPED, UNLIMITED,
                           HIGHLIGHT_TEXT_GAP, width, UNLIMITED);
    }

    /// Returns the text block of a video card of the width
    static TextBlock videoBlock(const PDFComponentPainter::VideoCard& card, qreal width)
    {
        return layoutBlock(card.title, font(PDFThemeFonts::Role::BodySemiBold), LINE_SEMIBOLD_13, 1,
                           card.description, font(PDFThemeFonts::Role::Small), LINE_12_WRAPPED, VIDEO_DESCRIPTION_LINES,
                           VIDEO_TEXT_GAP, width, UNLIMITED);
    }

    /// Returns the rectangles of the tags of a video card, whose text block is
    /// already laid out (one row of tags; a tag, which does not fit, has an
    /// empty rectangle, as the tags after it)
    static QList<QRectF> videoTagRects(const QRectF& rect, const QStringList& tags, const State& state, const TextBlock& block)
    {
        QList<QRectF> rects;
        rects.reserve(tags.size());

        const qreal left = rect.left() + 1.0 + VIDEO_PADDING;
        const qreal right = rect.right() - 1.0 - VIDEO_PADDING;
        const qreal top = rect.top() + 1.0 + VIDEO_THUMBNAIL + VIDEO_PADDING + block.height() + (block.height() > 0 ? VIDEO_TAGS_GAP : 0);

        qreal position = left;
        bool fits = rect.isValid();
        for (const QString& tag : tags)
        {
            const QSize size = PDFComponentPainter::getTagSize(PDFComponentPainter::Tag{ tag });
            const QRectF tagRect(position, top, size.width(), size.height());
            fits = fits && tagRect.right() <= right + 0.01 && tagRect.bottom() <= rect.bottom() + 0.01;
            rects << (fits ? state.visualRect(rect, tagRect) : QRectF());
            position = tagRect.right() + VIDEO_TAG_GAP;
        }

        return rects;
    }

    /// Returns true, if the rectangle can be drawn into (not empty, finite)
    static bool isDrawable(const QRectF& rect)
    {
        return !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height());
    }

    /// Returns a width, which is used for a layout (a width of an invalid or negative value is 0)
    static int validWidth(int width) { return qMax(0, width); }

    /// Returns a color of a reading theme: the valid color of the user, or the
    /// color of the token (of the fallback for an invalid token)
    static QColor getReadingThemeColor(const QColor& custom, Color token, Color fallback)
    {
        if (custom.isValid())
        {
            return custom;
        }

        const bool isValidToken = static_cast<int>(token) >= 0 && static_cast<int>(token) < static_cast<int>(PDFThemeColors::ColorCount);
        return PDFStylePainter::color(isValidToken ? token : fallback);
    }
};

qreal PDFComponentPainter::normalizedProgress(qreal progress)
{
    if (!qIsFinite(progress))
    {
        // NaN is no progress, an infinite value is its limit
        return (qIsInf(progress) && progress > 0.0) ? 1.0 : 0.0;
    }

    return qBound<qreal>(0.0, progress, 1.0);
}

void PDFComponentPainter::paintProgressBar(QPainter* painter, const QRectF& rect, qreal progress, const State& state)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    progress = normalizedProgress(progress);
    const PDFStylePainter::Radii radii(rect.height() / 2.0);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BorderDefault);

    const qreal valueWidth = rect.width() * progress;
    if (valueWidth > 0.0)
    {
        // The value has its own rounded corners, also at its end in the middle
        // of the track; a value narrower than the height keeps its radius smaller
        // (the value of a disabled bar is border/strong, as QProgressBar of the style)
        // (from the leading edge: the right edge in a right-to-left layout)
        const QRectF value(state.isRightToLeft() ? rect.right() - valueWidth : rect.left(), rect.top(), valueWidth, rect.height());
        const bool isFinished = qFuzzyCompare(progress, 1.0);
        PDFThemeColors::Color color = isFinished ? PDFThemeColors::AccentGreenIcon : PDFThemeColors::BgAccent;
        if (!state.isEnabled())
        {
            color = PDFThemeColors::BorderStrong;
        }
        PDFStylePainter::fill(painter, value, PDFStylePainter::Radii(qMin(valueWidth, rect.height()) / 2.0), color);
    }
}

void PDFComponentPainter::paintProgressBar(QPainter* painter, const QRectF& rect, qreal progress)
{
    paintProgressBar(painter, rect, progress, State());
}

// ------------------------------------------------------------ Quick action tile

QSize PDFComponentPainter::getQuickActionTileSize(const QuickActionTile& tile)
{
    Q_UNUSED(tile);
    return QSize(PDFComponentCardPainterHelper::TILE_WIDTH, PDFComponentCardPainterHelper::TILE_HEIGHT);
}

QSize PDFComponentPainter::getQuickActionTileMinimumSize(const QuickActionTile& tile)
{
    using Helper = PDFComponentCardPainterHelper;
    const int starWidth = tile.starVisible ? Helper::TILE_GAP + Helper::TILE_STAR : 0;
    return QSize(2 * Helper::TILE_PADDING + Helper::TILE_CIRCLE + Helper::TILE_GAP + Helper::MIN_TEXT + starWidth, Helper::TILE_HEIGHT);
}

QMargins PDFComponentPainter::getQuickActionTileTrailingMargins()
{
    return QMargins(0, 0, PDFComponentCardPainterHelper::TILE_PADDING, 0);
}

void PDFComponentPainter::paintQuickActionTile(QPainter* painter, const QRectF& rect, const QuickActionTile& tile, const State& state, bool paintFocusRing)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFAccentHue hue = normalized(tile.hue);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    Helper::paintHuedCard(painter, rect, hue, state, paintFocusRing);

    const PDFStylePainterGuard innerGuard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The icon in a white circle, the color of the hue in every state
    const QRectF circle = Helper::line(rect, rect.left() + Helper::TILE_PADDING, Helper::TILE_CIRCLE, Helper::TILE_CIRCLE);
    Helper::drawCircleIcon(painter, state, rect, circle, Color::BgSurface, tile.iconName, Helper::TILE_ICON,
                           Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)));

    qreal textRight = rect.right() - Helper::TILE_PADDING;
    if (tile.starVisible)
    {
        const QRectF starRect = Helper::line(rect, textRight - Helper::TILE_STAR, Helper::TILE_STAR, Helper::TILE_STAR);
        textRight = starRect.left() - Helper::TILE_GAP;
        if (tile.paintNested)
        {
            paintStar(painter, state.visualRect(rect, starRect), StarSize::Medium, Helper::nestedState(state, tile.pinned));
        }
    }

    // The title and the description wrap; the block is centered vertically
    // (fixed height 88), longer texts may reach 8 px from the edges
    const qreal textLeft = circle.right() + Helper::TILE_GAP;
    const qreal textWidth = textRight - textLeft;
    const QFont titleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::TileTitle);
    const QFont descriptionFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
    const int maxHeight = qFloor(rect.height()) - 2 * Helper::TILE_MIN_PADDING;
    const Helper::TextBlock block = Helper::layoutBlock(tile.title, titleFont, Helper::LINE_BOLD_14, Helper::TILE_TITLE_LINES,
                                                        tile.description, descriptionFont, Helper::LINE_12_WRAPPED, Helper::UNLIMITED,
                                                        Helper::TILE_TEXT_GAP, textWidth, maxHeight);
    const qreal top = rect.top() + qFloor((rect.height() - block.height()) / 2.0);
    Helper::drawLines(painter, state, rect, textLeft, top, textWidth, block.titleLine, block.title, titleFont, Helper::text(state, Color::TextPrimary));
    const qreal descriptionTop = top + block.title.size() * block.titleLine + (block.title.isEmpty() ? 0 : block.gap);
    Helper::drawLines(painter, state, rect, textLeft, descriptionTop, textWidth, block.descriptionLine, block.description, descriptionFont, Helper::text(state, Color::TextTertiary));
}

// ------------------------------------------------------------------ Folder card

PDFAccentHue PDFComponentPainter::normalizedFolderHue(PDFAccentHue hue)
{
    switch (hue)
    {
        case PDFAccentHue::Blue:
        case PDFAccentHue::Neutral:
        case PDFAccentHue::Green:
        case PDFAccentHue::Violet:
            return hue;

        default:
            break;
    }

    return PDFAccentHue::Neutral;
}

QSize PDFComponentPainter::getFolderCardSize(const FolderCard& card)
{
    Q_UNUSED(card);
    return QSize(PDFComponentCardPainterHelper::FOLDER_WIDTH, PDFComponentCardPainterHelper::FOLDER_HEIGHT);
}

QSize PDFComponentPainter::getFolderCardMinimumSize(const FolderCard& card)
{
    Q_UNUSED(card);
    using Helper = PDFComponentCardPainterHelper;
    return QSize(Helper::FOLDER_LEFT + Helper::FOLDER_CIRCLE + Helper::FOLDER_GAP + Helper::MIN_TEXT + Helper::FOLDER_GAP + Helper::MORE + Helper::FOLDER_RIGHT, Helper::FOLDER_HEIGHT);
}

QMargins PDFComponentPainter::getFolderCardTrailingMargins()
{
    return QMargins(0, 0, PDFComponentCardPainterHelper::FOLDER_RIGHT, 0);
}

void PDFComponentPainter::paintFolderCard(QPainter* painter, const QRectF& rect, const FolderCard& card, const State& state, bool paintFocusRing)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFAccentHue hue = normalizedFolderHue(card.hue);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    Helper::paintHuedCard(painter, rect, hue, state, paintFocusRing);

    const PDFStylePainterGuard innerGuard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);

    const QRectF circle = Helper::line(rect, rect.left() + Helper::FOLDER_LEFT, Helper::FOLDER_CIRCLE, Helper::FOLDER_CIRCLE);
    Helper::drawCircleIcon(painter, state, rect, circle, Color::BgSurface, card.iconName, Helper::FOLDER_ICON,
                           Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)));

    const QRectF moreRect = Helper::line(rect, rect.right() - Helper::FOLDER_RIGHT - Helper::MORE, Helper::MORE, Helper::MORE);
    if (card.paintNested)
    {
        paintMoreButton(painter, state.visualRect(rect, moreRect), Helper::menuState(state));
    }

    // Name, path and the meta line, centered vertically as a block
    const qreal textLeft = circle.right() + Helper::FOLDER_GAP;
    const qreal textWidth = moreRect.left() - Helper::FOLDER_GAP - textLeft;
    const int blockHeight = Helper::LINE_SEMIBOLD_13 + Helper::FOLDER_TEXT_GAP + Helper::LINE_REGULAR_11 + (card.metaVisible ? Helper::FOLDER_TEXT_GAP + Helper::LINE_REGULAR_11 : 0);
    const qreal top = rect.top() + qFloor((rect.height() - blockHeight) / 2.0);
    const QFont smallFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
    Helper::drawDataText(painter, state, rect, QRectF(textLeft, top, textWidth, Helper::LINE_SEMIBOLD_13), card.name, PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), Helper::text(state, Color::TextPrimary));
    const qreal pathTop = top + Helper::LINE_SEMIBOLD_13 + Helper::FOLDER_TEXT_GAP;
    Helper::drawDataText(painter, state, rect, QRectF(textLeft, pathTop, textWidth, Helper::LINE_REGULAR_11), card.path, smallFont, Helper::text(state, Color::TextMuted));
    if (card.metaVisible)
    {
        const qreal metaTop = pathTop + Helper::LINE_REGULAR_11 + Helper::FOLDER_TEXT_GAP;
        Helper::drawText(painter, state, rect, QRectF(textLeft, metaTop, textWidth, Helper::LINE_REGULAR_11), card.meta, smallFont, Helper::text(state, Color::TextTertiary));
    }
}

// ------------------------------------------------------------------ Pinned card

QSize PDFComponentPainter::getPinnedCardSize(const PinnedCard& card)
{
    Q_UNUSED(card);
    return QSize(PDFComponentCardPainterHelper::PINNED_WIDTH, PDFComponentCardPainterHelper::PINNED_HEIGHT);
}

QSize PDFComponentPainter::getPinnedCardMinimumSize(const PinnedCard& card)
{
    Q_UNUSED(card);
    using Helper = PDFComponentCardPainterHelper;
    const int thumbnailWidth = getPageThumbnailSize(ThumbnailSize::Medium).width();
    return QSize(Helper::PINNED_INSET + thumbnailWidth + Helper::PINNED_GAP + Helper::MIN_TEXT + Helper::PINNED_GAP + Helper::PINNED_STAR + Helper::PINNED_RIGHT, Helper::PINNED_HEIGHT);
}

QMargins PDFComponentPainter::getPinnedCardTrailingMargins()
{
    return QMargins(0, PDFComponentCardPainterHelper::PINNED_INSET, PDFComponentCardPainterHelper::PINNED_RIGHT, 0);
}

int PDFComponentPainter::getPinnedCardTrailingSpacing()
{
    return PDFComponentCardPainterHelper::PINNED_ACTIONS_GAP;
}

void PDFComponentPainter::paintPinnedCard(QPainter* painter, const QRectF& rect, const PinnedCard& card, const State& state, bool paintFocusRing)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // bg/surface with the overlay of the pointer over it (decision 21)
    Helper::paintSurfaceCard(painter, rect, state, paintFocusRing);

    const PDFStylePainterGuard innerGuard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The thumbnail of the first page with its shadow (inside of the card)
    const QSize thumbnailSize = getPageThumbnailSize(ThumbnailSize::Medium);
    const QRectF thumbnail(rect.left() + Helper::PINNED_INSET, rect.top() + Helper::PINNED_INSET, thumbnailSize.width(), thumbnailSize.height());
    paintPageThumbnail(painter, state.visualRect(rect, thumbnail), PageThumbnail{ ThumbnailSize::Medium, card.pixmap, card.locked }, Helper::nestedState(state, false));

    // The column of the star and the menu button
    const qreal actionsLeft = rect.right() - Helper::PINNED_RIGHT - Helper::PINNED_STAR;
    if (card.paintNested)
    {
        const QRectF starRect(actionsLeft, rect.top() + Helper::PINNED_INSET, Helper::PINNED_STAR, Helper::PINNED_STAR);
        paintStar(painter, state.visualRect(rect, starRect), StarSize::Small, Helper::nestedState(state, card.pinned));
        const QRectF moreRect(actionsLeft + (Helper::PINNED_STAR - Helper::MORE) / 2, starRect.bottom() + Helper::PINNED_ACTIONS_GAP, Helper::MORE, Helper::MORE);
        paintMoreButton(painter, state.visualRect(rect, moreRect), Helper::menuState(state));
    }

    // Name and path at the top, the progress text and the bar at the bottom
    const qreal textLeft = thumbnail.right() + Helper::PINNED_GAP;
    const qreal textWidth = actionsLeft - Helper::PINNED_GAP - textLeft;
    const qreal top = rect.top() + Helper::PINNED_INSET;
    const qreal bottom = qMin(rect.bottom(), thumbnail.bottom());
    const QFont smallFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
    const bool isMissing = normalized(card.status) == FileStatus::Missing;
    Helper::drawDataText(painter, state, rect, QRectF(textLeft, top, textWidth, Helper::LINE_SEMIBOLD_13), card.name, PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold),
                         Helper::text(state, isMissing ? Color::TextMuted : Color::TextPrimary));
    Helper::drawDataText(painter, state, rect, QRectF(textLeft, top + Helper::LINE_SEMIBOLD_13 + Helper::PINNED_TEXT_GAP, textWidth, Helper::LINE_REGULAR_11), card.path, smallFont, Helper::text(state, Color::TextMuted));

    if (isMissing)
    {
        // A missing file: the badge "No longer exists" instead of the progress, at the bottom
        const QSize badgeSize = getMissingFileBadgeSize();
        const QRectF badge(textLeft, bottom - badgeSize.height(), qMin<qreal>(badgeSize.width(), qMax<qreal>(0.0, textWidth)), badgeSize.height());
        if (badge.top() > top && badge.width() > 0.0)
        {
            paintMissingFileBadge(painter, state.visualRect(rect, badge), state);
        }
        return;
    }

    const QRectF barRect(textLeft, bottom - Helper::PINNED_BAR, qMax<qreal>(0.0, textWidth), Helper::PINNED_BAR);
    const QRectF progressRect(textLeft, barRect.top() - Helper::PINNED_TEXT_GAP - Helper::LINE_REGULAR_11, textWidth, Helper::LINE_REGULAR_11);
    Helper::drawText(painter, state, rect, progressRect, card.progressText, smallFont, Helper::text(state, Color::TextTertiary));
    if (barRect.top() > top && !barRect.isEmpty())
    {
        // The bar fills from the leading edge (paintProgressBar mirrors it by the state)
        paintProgressBar(painter, state.visualRect(rect, barRect), card.progress, Helper::nestedState(state, false));
    }
}

// --------------------------------------------------------- Tool category header

QSize PDFComponentPainter::getToolCategoryHeaderSize(const ToolCategoryHeader& header)
{
    Q_UNUSED(header);
    return QSize(PDFComponentCardPainterHelper::HEADER_WIDTH, PDFComponentCardPainterHelper::HEADER_HEIGHT);
}

QSize PDFComponentPainter::getToolCategoryHeaderMinimumSize(const ToolCategoryHeader& header)
{
    using Helper = PDFComponentCardPainterHelper;
    const int countWidth = header.count.isEmpty() ? 0 : Helper::textWidth(header.count, Helper::font(PDFThemeFonts::Role::SmallMedium)) + Helper::HEADER_GAP;
    return QSize(2 * Helper::HEADER_SIDE + Helper::HEADER_CIRCLE + Helper::HEADER_GAP + Helper::MIN_TEXT + countWidth, Helper::HEADER_HEIGHT);
}

void PDFComponentPainter::paintToolCategoryHeader(QPainter* painter, const QRectF& rect, const ToolCategoryHeader& header, const State& state)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;
    using Role = PDFAccentHueColors::Role;

    const PDFAccentHue hue = normalized(header.hue);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The tint of the hue with the border at the bottom; the top corners follow
    // the card of the category, which clips the header in the design
    const qreal topRadius = qIsFinite(header.topRadius) ? qBound<qreal>(0.0, header.topRadius, qMin(rect.width(), rect.height()) / 2.0) : 0.0;
    PDFStylePainter::fill(painter, rect, PDFStylePainter::Radii(topRadius, topRadius, 0.0, 0.0), PDFAccentHueColors::getColor(hue, Role::Background));
    PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0), PDFAccentHueColors::getColor(hue, Role::Border));

    // The content lies above the bottom border
    const QRectF content(rect.left(), rect.top(), rect.width(), qMax<qreal>(0.0, rect.height() - 1.0));
    const QRectF circle = Helper::line(content, content.left() + Helper::HEADER_SIDE, Helper::HEADER_CIRCLE, Helper::HEADER_CIRCLE);
    Helper::drawCircleIcon(painter, state, rect, circle, Color::BgSurface, header.iconName, Helper::HEADER_ICON, Helper::icon(state, PDFAccentHueColors::getColor(hue, Role::Icon)));

    qreal textRight = content.right() - Helper::HEADER_SIDE;
    if (!header.count.isEmpty())
    {
        // The count is never elided, it takes its width from the texts
        const QFont countFont = Helper::font(PDFThemeFonts::Role::SmallMedium);
        const qreal countWidth = qMin<qreal>(Helper::textWidth(header.count, countFont), qMax<qreal>(0.0, textRight - circle.right()));
        const QRectF countRect = Helper::line(content, textRight - countWidth, countWidth, Helper::LINE_12);
        Helper::drawText(painter, state, rect, countRect, header.count, countFont, Helper::text(state, Color::TextTertiary), true);
        textRight = countRect.left() - Helper::HEADER_GAP;
    }

    const qreal textLeft = circle.right() + Helper::HEADER_GAP;
    const qreal textWidth = textRight - textLeft;
    const int blockHeight = Helper::LINE_BOLD_15 + (header.description.isEmpty() ? 0 : Helper::HEADER_TEXT_GAP + Helper::LINE_12);
    const qreal top = content.top() + qFloor((content.height() - blockHeight) / 2.0);
    const QRectF titleRect(textLeft, top, textWidth, Helper::LINE_BOLD_15);
    if (header.isTitleData)
    {
        Helper::drawDataText(painter, state, rect, titleRect, header.title, Helper::font(PDFThemeFonts::Role::Title), Helper::text(state, Color::TextPrimary));
    }
    else
    {
        Helper::drawText(painter, state, rect, titleRect, header.title, Helper::font(PDFThemeFonts::Role::Title), Helper::text(state, Color::TextPrimary));
    }
    Helper::drawText(painter, state, rect, QRectF(textLeft, top + Helper::LINE_BOLD_15 + Helper::HEADER_TEXT_GAP, textWidth, Helper::LINE_12), header.description,
                     Helper::font(PDFThemeFonts::Role::Small), Helper::text(state, Color::TextTertiary));
}

// ------------------------------------------------------------------ Tool choice

QSize PDFComponentPainter::getToolChoiceSize(const ToolChoice& choice)
{
    using Helper = PDFComponentCardPainterHelper;
    const int width = qMax(Helper::CHOICE_WIDTH, getToolChoiceMinimumSize(choice).width());
    return QSize(width, getToolChoiceHeight(choice, width));
}

int PDFComponentPainter::getToolChoiceHeight(const ToolChoice& choice, int width)
{
    using Helper = PDFComponentCardPainterHelper;

    // The card hugs the badge and the name (z=fix/hug): 46 for a name of one line
    const int nameWidth = Helper::getChoiceNameWidth(qBound(0, width, Helper::MAX_EXTENT));
    const QStringList lines = nameWidth > 0 ? Helper::wrap(choice.name, Helper::font(PDFThemeFonts::Role::SmallMedium), nameWidth, Helper::CHOICE_MAX_LINES) : QStringList();
    const int lineCount = static_cast<int>(lines.size());
    return 2 * Helper::CHOICE_VERTICAL + qMax(Helper::CHOICE_BADGE, lineCount * Helper::LINE_12);
}

QSize PDFComponentPainter::getToolChoiceMinimumSize(const ToolChoice& choice)
{
    using Helper = PDFComponentCardPainterHelper;

    // The longest word of the name fits the name of the selected choice
    const int word = qMin(Helper::MAX_EXTENT, qCeil(getLongestWordWidth(choice.name, Helper::font(PDFThemeFonts::Role::SmallMedium))));
    const int width = 2 * Helper::CHOICE_SIDE + Helper::CHOICE_BADGE + 2 * Helper::CHOICE_GAP + qMax(Helper::MIN_TEXT, word) + Helper::CHOICE_CHECK;
    return QSize(width, getToolChoiceHeight(choice, width));
}

qreal PDFComponentPainter::getToolChoiceRadius()
{
    return PDFComponentCardPainterHelper::CHOICE_RADIUS;
}

void PDFComponentPainter::paintToolChoice(QPainter* painter, const QRectF& rect, const ToolChoice& choice, const State& state)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::CHOICE_RADIUS);
    const bool isSelected = state.isOn();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Own surface, the selection over it, the overlay of the pointer over both
    // (decision 21); the selected choice reacts to the pointer (it can be unselected)
    PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
    if (isSelected)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgAccentSubtle);
    }
    if (state.isPressed())
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgPressed);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
    }

    if (isSelected)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, state.isEnabled() ? Color::BgAccent : Color::BorderStrong, 2.0);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
    }

    {
        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipRect(rect, Qt::IntersectClip);

        // The content keeps its places in both selections
        const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::CHOICE_SIDE, Helper::CHOICE_BADGE, Helper::CHOICE_BADGE);
        paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ choice.iconName, choice.hue, BadgeSize::Small }, state);

        qreal textRight = rect.right() - Helper::CHOICE_SIDE;
        if (isSelected)
        {
            const QRectF checkRect = Helper::line(rect, textRight - Helper::CHOICE_CHECK, Helper::CHOICE_CHECK, Helper::CHOICE_CHECK);
            Helper::drawIcon(painter, state, rect, checkRect, QStringLiteral("check"), Helper::icon(state, Color::IconAccent));
            textRight = checkRect.left() - Helper::CHOICE_GAP;
        }

        const qreal textLeft = badgeRect.right() + Helper::CHOICE_GAP;
        const qreal textWidth = textRight - textLeft;
        const QFont nameFont = Helper::font(PDFThemeFonts::Role::SmallMedium);
        const int maxLines = qMax(1, static_cast<int>((rect.height() - 2 * Helper::CHOICE_VERTICAL) / Helper::LINE_12));
        const QStringList lines = Helper::wrap(choice.name, nameFont, textWidth, qMin(maxLines, Helper::CHOICE_MAX_LINES));
        const qreal top = rect.top() + qFloor((rect.height() - lines.size() * Helper::LINE_12) / 2.0);
        Helper::drawLines(painter, state, rect, textLeft, top, textWidth, Helper::LINE_12, lines, nameFont, Helper::text(state, Color::TextPrimary));
    }

    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, rect, radii);
    }
}

// ---------------------------------------------------------------- Resource card

PDFAccentHue PDFComponentPainter::normalizedResourceHue(PDFAccentHue hue)
{
    switch (hue)
    {
        case PDFAccentHue::Neutral:
        case PDFAccentHue::Green:
        case PDFAccentHue::Amber:
        case PDFAccentHue::Violet:
            return hue;

        default:
            break;
    }

    return PDFAccentHue::Neutral;
}

QSize PDFComponentPainter::getResourceCardSize(const ResourceCard& card)
{
    const int width = PDFComponentCardPainterHelper::RESOURCE_WIDTH;
    return QSize(width, getResourceCardHeight(card, width));
}

int PDFComponentPainter::getResourceCardHeight(const ResourceCard& card, int width)
{
    using Helper = PDFComponentCardPainterHelper;
    const qreal textWidth = Helper::validWidth(width) - 2 * Helper::RESOURCE_SIDE;
    return Helper::RESOURCE_TOP + Helper::RESOURCE_ICON_HEIGHT + Helper::RESOURCE_GAP + Helper::resourceBlock(card, textWidth).height() + Helper::RESOURCE_TOP;
}

QSize PDFComponentPainter::getResourceCardMinimumSize(const ResourceCard& card)
{
    // The height of the fixed parts and of one line of each text: the height
    // at the minimal width would be the height of texts wrapped into many short
    // lines, which a layout without the height for width would take as the
    // minimum (the layout of the height for width gives the real height)
    using Helper = PDFComponentCardPainterHelper;
    const int width = 2 * Helper::RESOURCE_SIDE + qMax(Helper::RESOURCE_ICON_WIDTH, Helper::MIN_TEXT);
    Helper::TextBlock block;
    block.titleLine = Helper::LINE_BOLD_13;
    block.descriptionLine = Helper::LINE_11_WRAPPED;
    block.gap = Helper::RESOURCE_TEXT_GAP;
    block.title = card.title.isEmpty() ? QStringList() : QStringList{ card.title };
    block.description = card.description.isEmpty() ? QStringList() : QStringList{ card.description };
    return QSize(width, 2 * Helper::RESOURCE_TOP + Helper::RESOURCE_ICON_HEIGHT + Helper::RESOURCE_GAP + block.height());
}

void PDFComponentPainter::paintResourceCard(QPainter* painter, const QRectF& rect, const ResourceCard& card, const State& state, bool paintFocusRing)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFAccentHue hue = normalizedResourceHue(card.hue);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    Helper::paintHuedCard(painter, rect, hue, state, paintFocusRing);

    const PDFStylePainterGuard innerGuard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The icon without a circle, the texts below it from the top of the card
    const QRectF iconPlace(rect.left() + Helper::RESOURCE_SIDE, rect.top() + Helper::RESOURCE_TOP, Helper::RESOURCE_ICON_WIDTH, Helper::RESOURCE_ICON_HEIGHT);
    Helper::drawIcon(painter, state, rect, alignedCentered(iconPlace, QSizeF(Helper::RESOURCE_ICON, Helper::RESOURCE_ICON)), card.iconName,
                     Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)));

    const qreal textLeft = rect.left() + Helper::RESOURCE_SIDE;
    const qreal textWidth = rect.width() - 2 * Helper::RESOURCE_SIDE;
    const Helper::TextBlock block = Helper::resourceBlock(card, textWidth);
    const qreal top = iconPlace.bottom() + Helper::RESOURCE_GAP;
    Helper::drawLines(painter, state, rect, textLeft, top, textWidth, block.titleLine, block.title, Helper::font(PDFThemeFonts::Role::BodyBold), Helper::text(state, Color::TextPrimary));
    const qreal descriptionTop = top + block.title.size() * block.titleLine + (block.title.isEmpty() ? 0 : block.gap);
    Helper::drawLines(painter, state, rect, textLeft, descriptionTop, textWidth, block.descriptionLine, block.description, Helper::font(PDFThemeFonts::Role::CaptionRegular), Helper::text(state, Color::TextTertiary));
}

// --------------------------------------------------------------- Highlight card

QSize PDFComponentPainter::getHighlightCardSize(const HighlightCard& card)
{
    const int width = PDFComponentCardPainterHelper::HIGHLIGHT_WIDTH;
    return QSize(width, getHighlightCardHeight(card, width));
}

int PDFComponentPainter::getHighlightCardHeight(const HighlightCard& card, int width)
{
    using Helper = PDFComponentCardPainterHelper;
    const qreal textWidth = Helper::validWidth(width) - 2 * Helper::HIGHLIGHT_INSET;
    return 2 * Helper::HIGHLIGHT_INSET + Helper::HIGHLIGHT_CIRCLE + Helper::HIGHLIGHT_GAP + Helper::highlightBlock(card, textWidth).height();
}

QSize PDFComponentPainter::getHighlightCardMinimumSize(const HighlightCard& card)
{
    // The height of the fixed parts and of one line of each text (as Resource card)
    using Helper = PDFComponentCardPainterHelper;
    const int width = 2 * Helper::HIGHLIGHT_INSET + qMax(Helper::HIGHLIGHT_CIRCLE, Helper::MIN_TEXT);
    Helper::TextBlock block;
    block.titleLine = Helper::LINE_BOLD_14;
    block.descriptionLine = Helper::LINE_12_WRAPPED;
    block.gap = Helper::HIGHLIGHT_TEXT_GAP;
    block.title = card.title.isEmpty() ? QStringList() : QStringList{ card.title };
    block.description = card.description.isEmpty() ? QStringList() : QStringList{ card.description };
    return QSize(width, 2 * Helper::HIGHLIGHT_INSET + Helper::HIGHLIGHT_CIRCLE + Helper::HIGHLIGHT_GAP + block.height());
}

void PDFComponentPainter::paintHighlightCard(QPainter* painter, const QRectF& rect, const HighlightCard& card, const State& state)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;
    using Role = PDFAccentHueColors::Role;

    const PDFAccentHue hue = normalized(card.hue);
    const PDFStylePainter::Radii radii(Helper::CARD_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
    PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Only the circle and its icon have the hue
    const QRectF circle(rect.left() + Helper::HIGHLIGHT_INSET, rect.top() + Helper::HIGHLIGHT_INSET, Helper::HIGHLIGHT_CIRCLE, Helper::HIGHLIGHT_CIRCLE);
    Helper::drawCircleIcon(painter, state, rect, circle, PDFAccentHueColors::getColor(hue, Role::Background), card.iconName, Helper::HIGHLIGHT_ICON,
                           Helper::icon(state, PDFAccentHueColors::getColor(hue, Role::Icon)));

    const qreal textLeft = rect.left() + Helper::HIGHLIGHT_INSET;
    const qreal textWidth = rect.width() - 2 * Helper::HIGHLIGHT_INSET;
    const Helper::TextBlock block = Helper::highlightBlock(card, textWidth);
    const qreal top = circle.bottom() + Helper::HIGHLIGHT_GAP;
    Helper::drawLines(painter, state, rect, textLeft, top, textWidth, block.titleLine, block.title, Helper::font(PDFThemeFonts::Role::TileTitle), Helper::text(state, Color::TextPrimary));
    const qreal descriptionTop = top + block.title.size() * block.titleLine + (block.title.isEmpty() ? 0 : block.gap);
    Helper::drawLines(painter, state, rect, textLeft, descriptionTop, textWidth, block.descriptionLine, block.description, Helper::font(PDFThemeFonts::Role::Small), Helper::text(state, Color::TextTertiary));
}

// ------------------------------------------------------------------- Video card

QStringList PDFComponentPainter::getVideoCardTags(const VideoCard& card)
{
    QStringList tags;
    for (const QString& tag : card.tags)
    {
        if (!tag.trimmed().isEmpty())
        {
            tags << tag;
        }
    }
    return tags;
}

QList<QRectF> PDFComponentPainter::getVideoCardTagRects(const QRectF& rect, const VideoCard& card, const State& state)
{
    using Helper = PDFComponentCardPainterHelper;

    // One row of tags; a tag, which does not fit, is hidden (with the tags after it)
    const qreal textWidth = rect.width() - 2.0 * (1.0 + Helper::VIDEO_PADDING);
    return Helper::videoTagRects(rect, getVideoCardTags(card), state, Helper::videoBlock(card, textWidth));
}

QSize PDFComponentPainter::getVideoCardSize(const VideoCard& card)
{
    const int width = PDFComponentCardPainterHelper::VIDEO_WIDTH;
    return QSize(width, getVideoCardHeight(card, width));
}

int PDFComponentPainter::getVideoCardHeight(const VideoCard& card, int width)
{
    using Helper = PDFComponentCardPainterHelper;
    const qreal textWidth = Helper::validWidth(width) - 2 * (1 + Helper::VIDEO_PADDING);
    const int textHeight = Helper::videoBlock(card, textWidth).height();
    const bool hasTags = !getVideoCardTags(card).isEmpty();
    const int tagsHeight = hasTags ? getTagSize(Tag{ QStringLiteral("A") }).height() + (textHeight > 0 ? Helper::VIDEO_TAGS_GAP : 0) : 0;
    return 2 + Helper::VIDEO_THUMBNAIL + 2 * Helper::VIDEO_PADDING + textHeight + tagsHeight;
}

QSize PDFComponentPainter::getVideoCardMinimumSize(const VideoCard& card)
{
    using Helper = PDFComponentCardPainterHelper;
    const int width = 2 * (1 + Helper::VIDEO_PADDING) + qMax(Helper::VIDEO_PLAY, Helper::MIN_TEXT);
    return QSize(width, getVideoCardHeight(card, width));
}

void PDFComponentPainter::paintVideoCard(QPainter* painter, const QRectF& rect, const VideoCard& card, const State& state, bool paintFocusRing)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    const PDFStylePainter::Radii radii(Helper::CARD_RADIUS);
    const bool isPointer = state.isHovered() || state.isPressed();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Surface: bg/subtle under the pointer, bg/muted pressed
    Color fill = Color::BgSurface;
    if (state.isPressed())
    {
        fill = Color::BgMuted;
    }
    else if (state.isHovered())
    {
        fill = Color::BgSubtle;
    }
    PDFStylePainter::fill(painter, rect, radii, fill);

    // The card clips its content by its rounded shape (the corners of the preview)
    {
        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipPath(PDFStylePainter::roundedPath(rect, radii), Qt::IntersectClip);

        const QRectF thumbnail(rect.left() + 1.0, rect.top() + 1.0, qMax<qreal>(0.0, rect.width() - 2.0), qMin<qreal>(Helper::VIDEO_THUMBNAIL, qMax<qreal>(0.0, rect.height() - 2.0)));
        if (!thumbnail.isEmpty())
        {
            PDFStylePainter::fillRect(painter, thumbnail, Color::BgMuted);
            if (!card.pixmap.isNull() && card.pixmap.width() > 0 && card.pixmap.height() > 0)
            {
                // The picture of the application covers the preview (it is cropped, not distorted)
                const QSizeF pixmapSize = QSizeF(card.pixmap.size()) / qMax<qreal>(card.pixmap.devicePixelRatio(), 1.0);
                const QRectF target = PDFStylePainter::centered(thumbnail, pixmapSize.scaled(thumbnail.size(), Qt::KeepAspectRatioByExpanding));
                const PDFStylePainterGuard innermostGuard(painter);
                painter->setClipRect(thumbnail, Qt::IntersectClip);
                drawScaledPixmap(painter, target, card.pixmap);
            }
            PDFStylePainter::fillRect(painter, QRectF(thumbnail.left(), thumbnail.bottom() - 1.0, thumbnail.width(), 1.0), Color::BorderDivider);

            // The play button in the middle; the triangle lies 2 px right of the
            // center of the circle (it is a symbol of playing, it is not mirrored)
            if (thumbnail.width() >= Helper::VIDEO_PLAY && thumbnail.height() >= Helper::VIDEO_PLAY)
            {
                const QRectF play = alignedCentered(thumbnail, QSizeF(Helper::VIDEO_PLAY, Helper::VIDEO_PLAY));
                PDFStylePainter::fillEllipse(painter, play, PDFStylePainter::color(Color::BgScrim));

                const qreal triangleLeft = play.left() + 19.0;
                QPainterPath triangle;
                triangle.moveTo(triangleLeft, play.top() + 15.5);
                triangle.lineTo(triangleLeft + 11.5, play.top() + 22.0);
                triangle.lineTo(triangleLeft, play.top() + 28.5);
                triangle.closeSubpath();
                painter->fillPath(triangle, PDFStylePainter::color(Color::IconOnAccent));
            }

            // The duration in the trailing bottom corner
            if (!card.duration.isEmpty())
            {
                const QFont durationFont = Helper::font(PDFThemeFonts::Role::Caption);
                const qreal durationWidth = qMin<qreal>(Helper::textWidth(card.duration, durationFont) + 2 * Helper::VIDEO_DURATION_SIDE, thumbnail.width() - 2 * Helper::VIDEO_DURATION_OFFSET);
                const QRectF duration(thumbnail.right() - Helper::VIDEO_DURATION_OFFSET - durationWidth, thumbnail.bottom() - Helper::VIDEO_DURATION_OFFSET - Helper::VIDEO_DURATION_HEIGHT,
                                      durationWidth, Helper::VIDEO_DURATION_HEIGHT);
                if (duration.width() > 2 * Helper::VIDEO_DURATION_SIDE && duration.top() > thumbnail.top())
                {
                    const QRectF visualDuration = state.visualRect(rect, duration);
                    PDFStylePainter::fill(painter, visualDuration, PDFStylePainter::Radii(Helper::VIDEO_DURATION_RADIUS), Color::BgScrim);
                    const QRectF durationText = visualDuration.adjusted(Helper::VIDEO_DURATION_SIDE, 0.0, -Helper::VIDEO_DURATION_SIDE, 0.0);
                    drawDataText(painter, durationText, Qt::AlignCenter | Qt::TextSingleLine, elidedText(card.duration, durationFont, durationText.width()),
                                 durationFont, PDFStylePainter::color(Color::IconOnAccent), getDataTextDirection(card.duration));
                }
            }
        }

        // Title of one line, description of two lines at most, the row of the tags
        const qreal textLeft = rect.left() + 1.0 + Helper::VIDEO_PADDING;
        const qreal textWidth = rect.width() - 2.0 * (1.0 + Helper::VIDEO_PADDING);
        const Helper::TextBlock block = Helper::videoBlock(card, textWidth);
        const qreal top = thumbnail.bottom() + Helper::VIDEO_PADDING;
        const QFont titleFont = Helper::font(PDFThemeFonts::Role::BodySemiBold);
        Helper::drawLines(painter, state, rect, textLeft, top, textWidth, block.titleLine, block.title, titleFont, Helper::text(state, Color::TextPrimary));
        const qreal descriptionTop = top + block.title.size() * block.titleLine + (block.title.isEmpty() ? 0 : block.gap);
        Helper::drawLines(painter, state, rect, textLeft, descriptionTop, textWidth, block.descriptionLine, block.description, Helper::font(PDFThemeFonts::Role::Small), Helper::text(state, Color::TextTertiary));

        if (card.paintNested)
        {
            // The text block is laid out once for the texts and the tags
            const QStringList tags = getVideoCardTags(card);
            const QList<QRectF> tagRects = Helper::videoTagRects(rect, tags, state, block);
            for (qsizetype i = 0; i < tags.size() && i < tagRects.size(); ++i)
            {
                if (!tagRects.at(i).isEmpty())
                {
                    paintTag(painter, tagRects.at(i), Tag{ tags.at(i) }, Helper::nestedState(state, false));
                }
            }
        }
    }

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
}

// ----------------------------------------------------------- Reading theme card

QSize PDFComponentPainter::getReadingThemeCardSize(const ReadingThemeCard& card)
{
    Q_UNUSED(card);
    return QSize(PDFComponentCardPainterHelper::READING_WIDTH, PDFComponentCardPainterHelper::READING_HEIGHT);
}

QSize PDFComponentPainter::getReadingThemeCardMinimumSize(const ReadingThemeCard& card)
{
    using Helper = PDFComponentCardPainterHelper;
    const int lettersWidth = Helper::textWidth(getReadingThemeSampleTexts().value(0), Helper::font(PDFThemeFonts::Role::Display));
    Q_UNUSED(card);
    return QSize(2 * (Helper::CHOICE_CARD_PADDING + Helper::READING_SAMPLE_INSET) + lettersWidth + Helper::READING_LETTERS_GAP + Helper::MIN_TEXT, Helper::READING_HEIGHT);
}

QStringList PDFComponentPainter::getReadingThemeSampleTexts()
{
    return { QCoreApplication::translate("PDFComponentPainter", "Aa"),
             QCoreApplication::translate("PDFComponentPainter", "Quick brown fox"),
             QCoreApplication::translate("PDFComponentPainter", "jumps over the dog") };
}

void PDFComponentPainter::paintReadingThemeCard(QPainter* painter, const QRectF& rect, const ReadingThemeCard& card, const State& state)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;
    using Color = PDFThemeColors::Color;

    // The colors of the theme are data: a valid color of the user, or a valid token
    const QColor pageColor = Helper::getReadingThemeColor(card.customPageColor, card.pageColor, Color::ReadingPaperPage);
    const QColor textColor = Helper::getReadingThemeColor(card.customTextColor, card.textColor, Color::ReadingPaperText);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    Helper::paintChoiceBackground(painter, rect, state);

    {
        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipRect(rect, Qt::IntersectClip);

        // The sample: "Aa" and two lines of text in the colors of the theme
        const QRectF sample(rect.left() + Helper::CHOICE_CARD_PADDING, rect.top() + Helper::CHOICE_CARD_PADDING,
                            qMax<qreal>(0.0, rect.width() - 2 * Helper::CHOICE_CARD_PADDING), Helper::READING_SAMPLE_HEIGHT);
        if (!sample.isEmpty())
        {
            const PDFStylePainter::Radii sampleRadii(Helper::READING_SAMPLE_RADIUS);
            PDFStylePainter::fill(painter, sample, sampleRadii, pageColor);

            {
                const PDFStylePainterGuard innermostGuard(painter);
                painter->setClipPath(PDFStylePainter::roundedPath(sample, sampleRadii), Qt::IntersectClip);
                const QStringList texts = getReadingThemeSampleTexts();
                const QFont lettersFont = Helper::font(PDFThemeFonts::Role::Display);
                const qreal lettersWidth = Helper::textWidth(texts.value(0), lettersFont);
                const qreal left = sample.left() + Helper::READING_SAMPLE_INSET;
                const QRectF letters = Helper::line(sample, left, lettersWidth, Helper::READING_LETTERS_LINE);
                PDFStylePainter::text(painter, state.visualRect(rect, letters), Qt::AlignVCenter | Qt::TextSingleLine | state.leadingAlignment(), texts.value(0), lettersFont, textColor);

                const qreal linesLeft = letters.right() + Helper::READING_LETTERS_GAP;
                const qreal linesWidth = sample.right() - Helper::READING_SAMPLE_INSET - linesLeft;
                const qreal linesHeight = 2 * Helper::READING_SAMPLE_LINE + Helper::READING_SAMPLE_GAP;
                const qreal linesTop = sample.top() + qFloor((sample.height() - linesHeight) / 2.0);
                if (linesWidth > 0.0)
                {
                    const int alignment = Qt::AlignVCenter | Qt::TextSingleLine | state.leadingAlignment();
                    const QFont firstFont = Helper::font(PDFThemeFonts::Role::TinySemiBold);
                    const QFont secondFont = Helper::font(PDFThemeFonts::Role::Tiny);
                    PDFStylePainter::text(painter, state.visualRect(rect, QRectF(linesLeft, linesTop, linesWidth, Helper::READING_SAMPLE_LINE)), alignment,
                                          elidedText(texts.value(1), firstFont, linesWidth), firstFont, textColor);
                    PDFStylePainter::text(painter, state.visualRect(rect, QRectF(linesLeft, linesTop + Helper::READING_SAMPLE_LINE + Helper::READING_SAMPLE_GAP, linesWidth, Helper::READING_SAMPLE_LINE)), alignment,
                                          elidedText(texts.value(2), secondFont, linesWidth), secondFont, textColor);
                }
            }

            PDFStylePainter::strokeInside(painter, sample, sampleRadii, Color::BorderDefault, 1.0);
        }

        Helper::paintChoiceLabel(painter, rect, card.name, state);
    }

    Helper::paintChoiceRings(painter, rect, state);
}

// ------------------------------------------------------------- Color mode card

QSize PDFComponentPainter::getPreviewCardSize(const PreviewCard& card)
{
    Q_UNUSED(card);
    return QSize(PDFComponentCardPainterHelper::PREVIEW_CARD_WIDTH, PDFComponentCardPainterHelper::PREVIEW_CARD_HEIGHT);
}

QSize PDFComponentPainter::getPreviewCardMinimumSize(const PreviewCard& card)
{
    Q_UNUSED(card);
    using Helper = PDFComponentCardPainterHelper;
    return QSize(2 * (Helper::CHOICE_CARD_PADDING + Helper::CHOICE_CARD_LABEL_SIDE) + Helper::MIN_TEXT + Helper::CHOICE_CARD_GAP + Helper::CHOICE_CARD_CHECK, Helper::PREVIEW_CARD_HEIGHT);
}

void PDFComponentPainter::paintPreviewCard(QPainter* painter, const QRectF& rect, const PreviewCard& card, const State& state)
{
    if (!painter || !PDFComponentCardPainterHelper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    using Helper = PDFComponentCardPainterHelper;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    Helper::paintChoiceBackground(painter, rect, state);

    {
        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipRect(rect, Qt::IntersectClip);

        // The picture fills the width of the card (its height is the one of the design)
        const QSize previewSize = PDFPreviewPainter::getPreviewSize();
        const qreal labelTop = rect.bottom() - Helper::CHOICE_CARD_BOTTOM - Helper::CHOICE_CARD_LABEL;
        const qreal previewHeight = qMin<qreal>(previewSize.height(), labelTop - Helper::CHOICE_CARD_GAP - rect.top() - Helper::CHOICE_CARD_PADDING);
        const QRectF preview(rect.left() + Helper::CHOICE_CARD_PADDING, rect.top() + Helper::CHOICE_CARD_PADDING,
                             rect.width() - 2 * Helper::CHOICE_CARD_PADDING, previewHeight);
        if (preview.width() > 0.0 && preview.height() > 0.0)
        {
            // The picture is a scheme of a page, it is not mirrored for right-to-left layouts
            PDFPreviewPainter::paintPreview(painter, preview, card.preview);
        }

        Helper::paintChoiceLabel(painter, rect, card.name, state);
    }

    Helper::paintChoiceRings(painter, rect, state);
}

}   // namespace pdf
