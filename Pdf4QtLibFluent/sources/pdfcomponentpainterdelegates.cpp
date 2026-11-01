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

// Drawing of the rows of the views (step 5 of components.md, chapter 6.3):
// Thumbnail item, Bookmark row, Note row, Attachment row, Search result row,
// Font row, Recent item row and Shortcut row. The delegates of
// pdfcomponentdelegates.h draw the items of their views by these functions.

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QtMath>

#include <algorithm>

namespace pdf
{

/// Geometry constants of the cards of the rows of the views and their drawing helpers
class PDFComponentDelegatePainterHelper
{
public:
    PDFComponentDelegatePainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using State = PDFComponentPainter::State;
    using Radii = PDFStylePainter::Radii;

    // Lines of the texts (automatic line heights of Inter)
    static constexpr int LINE_13 = 16;      ///< Regular / Semi Bold 13
    static constexpr int LINE_12 = 15;      ///< Regular / Medium / Semi Bold 12
    static constexpr int LINE_11 = 13;      ///< Regular / Medium / Semi Bold 11
    static constexpr int MIN_TEXT = 40;     ///< Minimal width of an elided text column
    static constexpr int BADGE = 32;
    static constexpr int MAX_EXTENT = 1 << 20;  ///< Largest width or height, which is laid out

    // Thumbnail item 191:777: padding 8, 8, 6, 8, gap 6, radius 10, the page with
    // a border of 1 outside (2 selected), radius 2, shadow (0, 1, 3); the label in
    // a pill of 15 px (line 13 + padding 1), padding 8 at the sides
    static constexpr int THUMB_PAGE_WIDTH = 108;
    static constexpr int THUMB_PAGE_HEIGHT = 148;
    static constexpr int THUMB_MAX_PAGE = 4096;
    static constexpr int THUMB_SIDE = 8;
    static constexpr int THUMB_TOP = 8;
    static constexpr int THUMB_BOTTOM = 6;
    static constexpr int THUMB_GAP = 6;
    static constexpr int THUMB_LABEL_HEIGHT = 15;
    static constexpr int THUMB_LABEL_SIDE = 8;
    static constexpr qreal THUMB_RADIUS = 10.0;
    static constexpr qreal THUMB_PAGE_RADIUS = 2.0;
    static constexpr qreal THUMB_SHADOW_OFFSET = 1.0;
    static constexpr qreal THUMB_SHADOW_BLUR = 3.0;

    // Rows of the document panels: radius 8
    static constexpr qreal ROW_RADIUS = 8.0;

    // Bookmark row 191:911: 276 x 46, padding 8 / 10, gap 10, icon 16, texts with a gap of 1
    static constexpr int BOOKMARK_WIDTH = 276;
    static constexpr int BOOKMARK_HEIGHT = 46;
    static constexpr int BOOKMARK_SIDE = 10;
    static constexpr int BOOKMARK_GAP = 10;
    static constexpr int BOOKMARK_ICON = 16;

    // Note row 191:998: padding 8, 10, 8, 8, gap 10, the head line 15 (gap 8 before
    // the date), gap 2, the text of two lines of 15 px
    static constexpr int NOTE_WIDTH = 276;
    static constexpr int NOTE_TOP = 8;
    static constexpr int NOTE_RIGHT = 10;
    static constexpr int NOTE_LEFT = 8;
    static constexpr int NOTE_GAP = 10;
    static constexpr int NOTE_HEAD_GAP = 8;
    static constexpr int NOTE_TEXT_GAP = 2;
    static constexpr int NOTE_MAX_LINES = 2;

    // State icon of a note row (eye-off of a hidden annotation, 1026:21995): icon/muted
    // 14 at 4 from the trailing edge, centered on the first line of the text; the
    // text keeps the gap 4 before it
    static constexpr int NOTE_STATE_ICON = 14;
    static constexpr int NOTE_STATE_ICON_RIGHT = 4;
    static constexpr int NOTE_STATE_ICON_GAP = 4;

    // Attachment row 191:1045: 276 x 48, padding 8, 6, 8, 8, gap 10, save 24
    static constexpr int ATTACHMENT_WIDTH = 276;
    static constexpr int ATTACHMENT_HEIGHT = 48;
    static constexpr int ATTACHMENT_LEFT = 8;
    static constexpr int ATTACHMENT_RIGHT = 6;
    static constexpr int ATTACHMENT_GAP = 10;
    static constexpr int ATTACHMENT_SAVE = 24;
    static constexpr int ATTACHMENT_SAVE_ICON = 14;
    static constexpr qreal ATTACHMENT_SAVE_RADIUS = 6.0;

    // Search result row 191:1073: padding 8 / 10, gap 3, the page line 13, the snippet of two lines of 15 px
    static constexpr int SEARCH_WIDTH = 276;
    static constexpr int SEARCH_TOP = 8;
    static constexpr int SEARCH_SIDE = 10;
    static constexpr int SEARCH_GAP = 3;
    static constexpr int SEARCH_MAX_LINES = 2;

    // Font row 206:944: 692 x 48, padding 8, gap 12, type 130, status 128 (dot 8, gap 6), copy 24 (icon 14)
    static constexpr int FONT_WIDTH = 692;
    static constexpr int FONT_HEIGHT = 48;
    static constexpr int FONT_SIDE = 8;
    static constexpr int FONT_GAP = 12;
    static constexpr int FONT_TYPE = 130;
    static constexpr int FONT_STATUS = 128;
    static constexpr int FONT_DOT = 8;
    static constexpr int FONT_DOT_GAP = 6;
    static constexpr int FONT_COPY = 24;
    static constexpr int FONT_COPY_ICON = 14;

    // Recent item row 79:1026: 802 x 56, padding 0, 8, 0, 10, gap 14, leading 36 x 42
    // (thumbnail 32 x 42, or a circle 32 with an icon 16), texts with a gap of 2,
    // progress 132 (text, gap 5, bar 120 x 4), opened 96, star 24, menu 16
    static constexpr int RECENT_WIDTH = 802;
    static constexpr int RECENT_HEIGHT = 56;
    static constexpr int RECENT_LEFT = 10;
    static constexpr int RECENT_RIGHT = 8;
    static constexpr int RECENT_GAP = 14;
    static constexpr int RECENT_LEADING = 36;
    static constexpr int RECENT_ICON = 16;
    static constexpr int RECENT_TEXT_GAP = 2;
    static constexpr int RECENT_PROGRESS = 132;
    static constexpr int RECENT_PROGRESS_GAP = 5;
    static constexpr int RECENT_BAR_WIDTH = 120;
    static constexpr int RECENT_BAR_HEIGHT = 4;
    static constexpr int RECENT_OPENED = 96;
    static constexpr int RECENT_STAR = 24;
    static constexpr int RECENT_MORE = 16;

    // Shortcut row 145:2904: 780 x 40, padding 0 / 10, gap 12, icon 16, the cap
    // 23 high with the padding 9 and the border 1 (8 and the ring 2 when recording),
    // radius 6, dashes 3 / 3 of the placeholder
    static constexpr int SHORTCUT_WIDTH = 780;
    static constexpr int SHORTCUT_HEIGHT = 40;
    static constexpr int SHORTCUT_SIDE = 10;
    static constexpr int SHORTCUT_GAP = 12;
    static constexpr int SHORTCUT_ICON = 16;
    static constexpr int SHORTCUT_CAP_HEIGHT = 23;
    static constexpr int SHORTCUT_CAP_SIDE = 10;
    static constexpr qreal SHORTCUT_CAP_RADIUS = 6.0;
    static constexpr qreal SHORTCUT_DASH = 3.0;

    // Tile rows of the frames of the tools: radius 8, tile radius 8, gap 1 between
    // the title and the description, gap 6 between the title and the tag, check
    // box 18, ending icon 16; the kinds in getTileRowMetrics
    static constexpr int TILE_CHECK_BOX = 18;
    static constexpr int TILE_END_ICON = 16;
    static constexpr int TILE_TEXT_GAP = 1;
    static constexpr int TILE_TAG_GAP = 6;
    static constexpr int TILE_NATURAL_TEXT = 530;           ///< The widest column of the texts of a row (the form 438:13119)
    static constexpr int TILE_MAX_TEXT_LENGTH = 1024;       ///< A longer text is cut before it is laid out (an address of a hostile document)
    static constexpr int TILE_CAPTION_TOP = 6;              ///< Caption 438:14866: paddings 6 / 10 / 2 around a line of 13
    static constexpr int TILE_CAPTION_BOTTOM = 2;
    static constexpr qreal TILE_RADIUS = 8.0;

    /// Geometry of a kind of a tile row
    struct TileRowMetrics
    {
        int verticalPadding = 7;
        int horizontalPadding = 10;
        int gap = 12;
        int tile = 30;
        int icon = 16;
        PDFThemeFonts::Role titleFont = PDFThemeFonts::Role::BodyMedium;
        int titleLine = LINE_13;
        int descriptionLine = LINE_11;
        int maxDescriptionLines = 2;
        Color descriptionColor = Color::TextMuted;
    };

    static TileRowMetrics getTileRowMetrics(PDFComponentPainter::TileRowKind kind)
    {
        using Kind = PDFComponentPainter::TileRowKind;

        TileRowMetrics metrics;
        switch (PDFComponentPainter::normalized(kind))
        {
            case Kind::MenuItem:
            case Kind::ListItem:
                break;

            case Kind::PanelStep:
                // 303:4560: a single line of 15 px
                metrics.descriptionLine = 15;
                metrics.maxDescriptionLines = 1;
                break;

            case Kind::Step:
                // 303:4405: padding 5 / 6, gap 10, tile 28, Medium 12 in a line of 16, the description in a line of 14
                metrics.verticalPadding = 5;
                metrics.horizontalPadding = 6;
                metrics.gap = 10;
                metrics.tile = 28;
                metrics.icon = 14;
                metrics.titleFont = PDFThemeFonts::Role::SmallMedium;
                metrics.descriptionLine = 14;
                metrics.maxDescriptionLines = 1;
                break;

            case Kind::Pending:
                // 389:20177: padding 8, gap 10, Semi Bold 12, the description text/tertiary
                metrics.verticalPadding = 8;
                metrics.horizontalPadding = 8;
                metrics.gap = 10;
                metrics.titleFont = PDFThemeFonts::Role::SmallSemiBold;
                metrics.titleLine = LINE_12;
                metrics.maxDescriptionLines = 1;
                metrics.descriptionColor = Color::TextTertiary;
                break;

            case Kind::Result:
                // 438:14342: a tile of 26 with an icon of 14
                metrics.tile = 26;
                metrics.icon = 14;
                metrics.maxDescriptionLines = 1;
                break;

            case Kind::Caption:
                // 438:14866: the title only, Semi Bold 11
                metrics.verticalPadding = 0;
                metrics.gap = 0;
                metrics.tile = 0;
                metrics.icon = 0;
                metrics.titleFont = PDFThemeFonts::Role::CaptionSemiBold;
                metrics.titleLine = LINE_11;
                metrics.maxDescriptionLines = 0;
                break;
        }
        return metrics;
    }

    /// Returns true for a caption of a list: it has the title only
    static bool isTileRowCaption(const PDFComponentPainter::TileRow& row)
    {
        return PDFComponentPainter::normalized(row.kind) == PDFComponentPainter::TileRowKind::Caption;
    }

    static bool isValidColor(Color color) { return static_cast<int>(color) >= 0 && static_cast<int>(color) < static_cast<int>(PDFThemeColors::ColorCount); }

    /// Returns the tone of the tile by the rules of PDFComponentPainter::TileRow
    static PDFComponentPainter::TileRowTone getTileRowTone(const PDFComponentPainter::TileRow& row)
    {
        using Kind = PDFComponentPainter::TileRowKind;
        using Tone = PDFComponentPainter::TileRowTone;

        const Kind kind = PDFComponentPainter::normalized(row.kind);
        if (row.checkBoxVisible)
        {
            return row.checkBoxChecked ? Tone::Accent : Tone::Muted;
        }
        if (kind == Kind::MenuItem || kind == Kind::PanelStep)
        {
            // An item of a choice list, chosen or not (438:15328, 313:7327, 339:9493)
            return Tone::Accent;
        }
        return PDFComponentPainter::normalized(row.tone);
    }

    /// Returns true, if the row is chosen and it shows the check mark
    static bool hasTileRowCheckMark(const PDFComponentPainter::TileRow& row)
    {
        using Kind = PDFComponentPainter::TileRowKind;
        const Kind kind = PDFComponentPainter::normalized(row.kind);
        return row.chosen && (kind == Kind::MenuItem || kind == Kind::PanelStep);
    }

    /// Returns true, if the row has the place of an ending icon (the panel 303:4560 keeps it also without the check mark)
    static bool hasTileRowEndPlace(const PDFComponentPainter::TileRow& row)
    {
        return !isTileRowCaption(row) && (hasTileRowCheckMark(row) || !row.endIconName.isEmpty() || PDFComponentPainter::normalized(row.kind) == PDFComponentPainter::TileRowKind::PanelStep);
    }

    /// Returns the text bounded for the layout
    static QString boundedTileText(const QString& text)
    {
        return text.size() > TILE_MAX_TEXT_LENGTH ? text.left(TILE_MAX_TEXT_LENGTH) : text;
    }

    /// Returns the width of what stands at the trailing side of the texts of a row with its gap
    static int getTileRowEndingWidth(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics)
    {
        if (isTileRowCaption(row))
        {
            return 0;
        }

        int width = 0;
        if (!row.linkText.isEmpty())
        {
            width += qMin(MAX_EXTENT, PDFComponentPainter::getTextLinkSize(PDFComponentPainter::TextLink{ boundedTileText(row.linkText) }).width()) + metrics.gap;
        }
        if (hasTileRowEndPlace(row))
        {
            width += TILE_END_ICON + metrics.gap;
        }
        return width;
    }

    /// Returns the width of what stands at the leading side of the texts of a row with its gaps
    static int getTileRowLeadingWidth(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics)
    {
        if (isTileRowCaption(row))
        {
            return 0;
        }

        return (row.checkBoxVisible ? TILE_CHECK_BOX + metrics.gap : 0) + metrics.tile + metrics.gap;
    }

    /// Returns the width of the column of the texts of a row of the width
    static int getTileRowTextWidth(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics, int width)
    {
        return qMax(0, width - 2 * metrics.horizontalPadding - getTileRowLeadingWidth(row, metrics) - getTileRowEndingWidth(row, metrics));
    }

    /// Returns the size of the tag of a row
    static QSize getTileRowTagSize(const PDFComponentPainter::TileRow& row)
    {
        return PDFComponentPainter::getTagSize(PDFComponentPainter::Tag{ boundedTileText(row.tag) });
    }

    /// Returns true, if the tag fits the column of the texts of the width (the title keeps at least MIN_TEXT)
    static bool isTileRowTagShown(const PDFComponentPainter::TileRow& row, int textWidth)
    {
        return !row.tag.isEmpty() && textWidth - qMin(MAX_EXTENT, getTileRowTagSize(row).width()) - TILE_TAG_GAP >= MIN_TEXT;
    }

    /// Returns the height of the title line of a row in the column of the width
    static int getTileRowTitleHeight(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics, int textWidth)
    {
        return isTileRowTagShown(row, textWidth) ? qMax(metrics.titleLine, getTileRowTagSize(row).height()) : metrics.titleLine;
    }

    /// Returns the lines of the description of a row in the column of the width
    static QStringList getTileRowDescriptionLines(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics, int textWidth)
    {
        if (row.description.isEmpty())
        {
            return QStringList();
        }

        const QString description = boundedTileText(row.description);
        if (metrics.maxDescriptionLines <= 1)
        {
            return { description };
        }

        const QStringList lines = PDFComponentPainter::wrapText(description, font(PDFThemeFonts::Role::CaptionRegular), textWidth, metrics.maxDescriptionLines);
        return lines.isEmpty() ? QStringList{ description } : lines;
    }

    /// Returns the number of the lines of the description of a row in the column of the
    /// width. A list of thousands of rows asks for the height of each of them: the
    /// width of a text without a line break says it without a layout of the text.
    static int getTileRowDescriptionLineCount(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics, int textWidth)
    {
        if (row.description.isEmpty() || metrics.maxDescriptionLines <= 0)
        {
            return 0;
        }
        if (metrics.maxDescriptionLines == 1 || textWidth <= 0)
        {
            return 1;
        }

        const QString description = boundedTileText(row.description);
        if (description.contains(QLatin1Char('\n')) || description.contains(QChar::LineSeparator))
        {
            return int(getTileRowDescriptionLines(row, metrics, textWidth).size());
        }
        return PDFStylePainter::textWidth(description, font(PDFThemeFonts::Role::CaptionRegular)) <= textWidth ? 1 : metrics.maxDescriptionLines;
    }

    /// Returns the height of the block of the texts of a row in the column of the width
    static int getTileRowTextHeight(const PDFComponentPainter::TileRow& row, const TileRowMetrics& metrics, int textWidth)
    {
        const int lines = getTileRowDescriptionLineCount(row, metrics, textWidth);
        return getTileRowTitleHeight(row, metrics, textWidth) + (lines > 0 ? TILE_TEXT_GAP + lines * metrics.descriptionLine : 0);
    }

    /// Paints the fill of a row by its kind (see PDFComponentPainter::TileRow)
    static void fillTileRow(QPainter* painter, const QRectF& rect, const PDFComponentPainter::TileRow& row, const State& state)
    {
        using Kind = PDFComponentPainter::TileRowKind;

        const Kind kind = PDFComponentPainter::normalized(row.kind);
        const Radii radii(TILE_RADIUS);
        if (kind != Kind::MenuItem && kind != Kind::PanelStep)
        {
            fillRow(painter, rect, radii, state, true);
            return;
        }

        // The hover and the press lie over the fill of a chosen item (438:15319, 313:7291)
        if (row.chosen && state.isEnabled())
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
        }
        fillRow(painter, rect, radii, state, false);
    }

    /// Returns the colors of the tile of a row (a disabled row has icon/disabled on bg/muted)
    static void getTileRowColors(const PDFComponentPainter::TileRow& row, const State& state, Color& fill, Color& content)
    {
        using Tone = PDFComponentPainter::TileRowTone;

        switch (state.isEnabled() ? getTileRowTone(row) : Tone::Muted)
        {
            case Tone::Accent:
                fill = Color::BgAccentSubtle;
                content = Color::IconAccent;
                break;
            case Tone::Muted:
                fill = Color::BgMuted;
                content = icon(state, Color::IconMuted);
                break;
            case Tone::Danger:
                fill = Color::AccentRedBg;
                content = Color::AccentRedIcon;
                break;
        }
    }

    /// Returns the color of the title of a row: text/primary, a row with a check box,
    /// which is not ticked, text/secondary (438:14900; a muted row without a check box
    /// stays text/primary, 434:4476), a skipped step text/tertiary (319:7385)
    static Color getTileRowTitleColor(const PDFComponentPainter::TileRow& row)
    {
        using Kind = PDFComponentPainter::TileRowKind;
        using Tone = PDFComponentPainter::TileRowTone;

        if (getTileRowTone(row) == Tone::Muted)
        {
            const Kind kind = PDFComponentPainter::normalized(row.kind);
            if (kind == Kind::Step)
            {
                return Color::TextTertiary;
            }
            if (row.checkBoxVisible && kind != Kind::Pending && kind != Kind::Result)
            {
                return Color::TextSecondary;
            }
        }
        return Color::TextPrimary;
    }

    // Page tile 389:20177 / 388:20614: padding 6, 6, 5, 6, gap 4, radius 8, the label
    // a line of 13, the caption a line of 12, the border 1 of the selected tile lies
    // in the layout, the badge 20 reaches 6 px over the page, its icon 12
    static constexpr int PAGE_TILE_SLOT_WIDTH = 104;
    static constexpr int PAGE_TILE_SLOT_HEIGHT = 104;
    static constexpr int PAGE_TILE_MAX_SLOT = 4096;
    static constexpr int PAGE_TILE_SIDE = 6;
    static constexpr int PAGE_TILE_TOP = 6;
    static constexpr int PAGE_TILE_BOTTOM = 5;
    static constexpr int PAGE_TILE_GAP = 4;
    static constexpr int PAGE_TILE_CAPTION = 12;
    static constexpr int PAGE_TILE_BORDER = 1;
    static constexpr int PAGE_TILE_BADGE = 20;
    static constexpr int PAGE_TILE_BADGE_ICON = 12;
    static constexpr int PAGE_TILE_BADGE_OVERLAP = 6;
    static constexpr qreal PAGE_TILE_RADIUS = 8.0;
    static constexpr qreal PAGE_TILE_PAPER_OPACITY = 0.50;      ///< The marked page 388:20614
    static constexpr qreal PAGE_TILE_PREVIEW_OPACITY = 0.35;    ///< The picture of the marked page 388:20614
    static constexpr qreal PAGE_TILE_DEFAULT_PAPER_WIDTH = 76.0;
    static constexpr qreal PAGE_TILE_DEFAULT_PAPER_HEIGHT = 104.0;

    /// Returns the distance of the slot from the edge of the tile added by the state
    /// (the border of the selected tile lies in the layout)
    static int getPageTileInset(const PDFComponentPainter::PageTile& tile)
    {
        return PDFComponentPainter::normalized(tile.tileState) == PDFComponentPainter::PageTileState::Selected ? PAGE_TILE_BORDER : 0;
    }

    static QFont font(PDFThemeFonts::Role role) { return PDFThemeFonts::getFont(role); }

    static int textWidth(const QString& text, const QFont& font)
    {
        return text.isEmpty() ? 0 : qMin(MAX_EXTENT, qCeil(PDFStylePainter::textWidth(text, font)));
    }

    static bool isDrawable(const QRectF& rect)
    {
        return !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height()) &&
               rect.width() <= MAX_EXTENT && rect.height() <= MAX_EXTENT;
    }

    /// Returns the color of a text (text/disabled for a disabled component)
    static Color text(const State& state, Color color) { return state.isEnabled() ? color : Color::TextDisabled; }

    /// Returns the color of an icon (icon/disabled for a disabled component)
    static Color icon(const State& state, Color color) { return state.isEnabled() ? color : Color::IconDisabled; }

    /// Returns the logical rectangle of the height centered vertically in the outer rectangle
    static QRectF line(const QRectF& outer, qreal left, qreal width, qreal height)
    {
        return QRectF(left, outer.top() + qFloor((outer.height() - height) / 2.0), qMax<qreal>(0.0, width), height);
    }

    /// Kind of a text (the user interface, data, a name of a file, components.md 6.6)
    using TextKind = PDFComponentPainter::TextKind;

    /// Draws a text of a single line, elided at its end, into the logical rectangle
    static void drawText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& text,
                         const QFont& font, Color color, TextKind kind, bool isTrailing = false)
    {
        if (logical.width() > 0.0 && logical.height() > 0.0)
        {
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(outer, logical), text, font, color, kind, isTrailing);
        }
    }

    /// Draws a title (Semi Bold 13, line 16) and a subtitle (Regular 11, line 13)
    /// separated by the gap as a block centered vertically in the outer rectangle
    /// (a missing subtitle leaves the title alone in the middle)
    static void drawTextBlock(QPainter* painter, const State& state, const QRectF& outer, qreal left, qreal width, int gap,
                              const QString& title, Color titleColor, TextKind titleKind,
                              const QString& subtitle, Color subtitleColor, TextKind subtitleKind)
    {
        if (width <= 0.0)
        {
            return;
        }

        const bool hasSubtitle = !subtitle.isEmpty();
        const qreal blockHeight = LINE_13 + (hasSubtitle ? gap + LINE_11 : 0);
        const qreal top = outer.top() + qFloor((outer.height() - blockHeight) / 2.0);
        drawText(painter, state, outer, QRectF(left, top, width, LINE_13), title, font(PDFThemeFonts::Role::BodySemiBold), titleColor, titleKind);
        if (hasSubtitle)
        {
            drawText(painter, state, outer, QRectF(left, top + LINE_13 + gap, width, LINE_11), subtitle, font(PDFThemeFonts::Role::CaptionRegular), subtitleColor, subtitleKind);
        }
    }

    /// Fills the background of a row by its state: the selection (bg/accent-subtle,
    /// its hover and press, decision 10), or the hover / press of an unselected
    /// row. A row without the axis Selected ignores the selection.
    static void fillRow(QPainter* painter, const QRectF& rect, const Radii& radii, const State& state, bool hasSelection)
    {
        if (hasSelection && state.isOn())
        {
            Color fill = Color::BgAccentSubtle;
            if (state.isPressed())
            {
                fill = Color::BgAccentSubtlePressed;
            }
            else if (state.isHovered())
            {
                fill = Color::BgAccentSubtleHover;
            }
            PDFStylePainter::fill(painter, rect, radii, fill);
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

    /// Returns true, if the row is selected (the rows with the axis Selected)
    static bool isSelected(const State& state) { return state.isOn(); }

    /// Returns the state of a nested button: enabled as the row, in its direction,
    /// with the hover and the press of the button itself
    static State nestedState(const State& rowState, QStyle::State buttonFlags, bool isOn = false)
    {
        QStyle::State flags = rowState.isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
        if (rowState.isEnabled())
        {
            flags |= buttonFlags & (QStyle::State_MouseOver | QStyle::State_Sunken);
        }
        if (isOn)
        {
            flags |= QStyle::State_On;
        }
        return State(flags, rowState.direction);
    }

    /// Largest number of the snippets, whose elided text is remembered
    static constexpr int SNIPPET_CACHE_SIZE = 512;

    /// Returns the key of a snippet laid out by the font into the width
    static QString getSnippetKey(const QString& text, const QList<PDFComponentPainter::TextRange>& matches, const QFont& font, qreal width, int maxLines)
    {
        QString key = font.key() + QLatin1Char('|') + QString::number(width, 'f', 3) + QLatin1Char('|') + QString::number(maxLines) + QLatin1Char('|');
        for (const PDFComponentPainter::TextRange& range : matches)
        {
            key += QString::number(range.start) + QLatin1Char(',') + QString::number(range.length) + QLatin1Char(';');
        }
        return key + QLatin1Char('|') + text;
    }

    /// Cache of the elided texts of the snippets (laying a snippet out with the
    /// elision is a search of the longest beginning, which fits). Without a lock:
    /// the painter is used only in the thread of the user interface.
    static QHash<QString, QString>& getSnippetCache()
    {
        static QHash<QString, QString> cache;
        return cache;
    }

    /// Remembers the elided text of a snippet under its key (getSnippetKey)
    static void rememberSnippet(const QString& key, const QString& result)
    {
        QHash<QString, QString>& cache = getSnippetCache();
        if (cache.size() >= SNIPPET_CACHE_SIZE)
        {
            cache.clear();
        }
        cache.insert(key, result);
    }

    /// Returns the beginning of the text up to \p end (without the spaces at
    /// its end) with an ellipsis: a candidate of the elided last line
    static QString getElidedSnippet(const QString& text, int end)
    {
        QString prefix = text.left(end);
        while (!prefix.isEmpty() && prefix.back().isSpace())
        {
            prefix.chop(1);
        }
        return prefix + QChar(0x2026);
    }

    /// Lays the text of a snippet out into lines of the width, the found terms
    /// in \p matchFormat (ranges outside of the text - also after the elision -
    /// are clipped)
    /// \returns Number of the lines (all of them, at most 1000)
    static int layoutSnippetText(QTextLayout& layout, const QString& laidText, const QList<PDFComponentPainter::TextRange>& matches, const QFont& font,
                                 const QTextOption& option, const QTextCharFormat& matchFormat, qreal width)
    {
        QList<QTextLayout::FormatRange> formats;
        for (const PDFComponentPainter::TextRange& range : matches)
        {
            const qint64 size = qint64(laidText.size());
            const qint64 start = qBound(qint64(0), qint64(range.start), size);
            const qint64 end = qBound(start, qint64(range.start) + qint64(qMax(range.length, 0)), size);
            if (end > start)
            {
                QTextLayout::FormatRange format;
                format.start = int(start);
                format.length = int(end - start);
                format.format = matchFormat;
                formats << format;
            }
        }

        layout.clearLayout();
        layout.setText(laidText);
        layout.setFont(font);
        layout.setTextOption(option);
        layout.setFormats(formats);
        layout.beginLayout();
        int count = 0;
        for (QTextLine textLine = layout.createLine(); textLine.isValid() && count < 1000; textLine = layout.createLine())
        {
            textLine.setLineWidth(width);
            textLine.setPosition(QPointF(0.0, count * LINE_12 + (LINE_12 - textLine.height()) / 2.0));
            ++count;
        }
        layout.endLayout();
        return count;
    }

    /// Returns the number of the lines of a snippet in the width (at most
    /// \p maxLines): one layout, without the elision of the last line (the
    /// height of a row does not need the elided text)
    static int countSnippetLines(const QString& text, const QList<PDFComponentPainter::TextRange>& matches, const QFont& font, qreal width, int maxLines)
    {
        QTextLayout layout;
        return layoutSnippet(layout, text, matches, font, width, maxLines, Qt::LeftToRight, Color::TextPrimary, false);
    }

    /// Lays a snippet with the found terms out into at most \p maxLines lines of
    /// the width; the last line is elided at its end, when the snippet does not
    /// fit and \p elide is true (otherwise all lines are laid out, the count is
    /// limited to \p maxLines)
    /// \returns Number of the lines
    static int layoutSnippet(QTextLayout& layout, const QString& text, const QList<PDFComponentPainter::TextRange>& matches, const QFont& font,
                             qreal width, int maxLines, Qt::LayoutDirection rowDirection, Color matchColor, bool elide = true)
    {
        if (text.isEmpty() || !(width > 0.0) || !qIsFinite(width) || maxLines <= 0)
        {
            return 0;
        }

        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setTextDirection(PDFComponentPainter::getDataTextDirection(text));
        option.setAlignment((rowDirection == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute);

        QFont matchFont = font;
        matchFont.setWeight(QFont::DemiBold);
        QTextCharFormat matchFormat;
        matchFormat.setFont(matchFont);
        matchFormat.setForeground(PDFStylePainter::color(matchColor));

        QString laidText = text;
        laidText.replace(QLatin1Char('\n'), QLatin1Char(' '));

        // The elided text of a snippet laid out before (each repaint of a row)
        const QString key = elide ? getSnippetKey(laidText, matches, font, width, maxLines) : QString();
        if (elide)
        {
            const auto it = getSnippetCache().constFind(key);
            if (it != getSnippetCache().cend())
            {
                // A copy: the layout below may remember another snippet
                const QString cached = it.value();
                return qMin(layoutSnippetText(layout, cached, matches, font, option, matchFormat, width), maxLines);
            }
        }

        const int count = layoutSnippetText(layout, laidText, matches, font, option, matchFormat, width);
        if (count <= maxLines || !elide)
        {
            if (elide)
            {
                rememberSnippet(key, laidText);
            }
            return qMin(count, maxLines);
        }

        // The last allowed line ends by an ellipsis: the longest beginning of the
        // text (at a boundary of a grapheme), which fits the lines with it
        const int lastLineEnd = layout.lineAt(maxLines - 1).textStart() + layout.lineAt(maxLines - 1).textLength();
        QList<int> boundaries;
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, laidText);
        for (qsizetype position = 0; position >= 0 && position <= lastLineEnd; position = finder.toNextBoundary())
        {
            boundaries << int(position);
        }

        int low = 0;
        int high = int(boundaries.size()) - 1;
        int best = 0;
        while (low <= high)
        {
            const int middle = (low + high) / 2;
            if (layoutSnippetText(layout, getElidedSnippet(laidText, boundaries.at(middle)), matches, font, option, matchFormat, width) <= maxLines)
            {
                best = middle;
                low = middle + 1;
            }
            else
            {
                high = middle - 1;
            }
        }

        const QString result = getElidedSnippet(laidText, boundaries.value(best));
        rememberSnippet(key, result);
        return qMin(layoutSnippetText(layout, result, matches, font, option, matchFormat, width), maxLines);
    }

    /// Returns the number of the lines of the text of a note in the width (at most two)
    static int getNoteLines(const PDFComponentPainter::NoteRow& row, qreal textWidth)
    {
        return static_cast<int>(PDFComponentPainter::wrapText(row.text, font(PDFThemeFonts::Role::Small), textWidth, NOTE_MAX_LINES).size());
    }

    /// Returns the width of the text block of a note row of the width
    static qreal getNoteTextWidth(qreal width)
    {
        return width - NOTE_LEFT - BADGE - NOTE_GAP - NOTE_RIGHT;
    }

    /// Returns the width, which the state icon takes from the lines of the text of a note
    static qreal getNoteStateIconReserve(const PDFComponentPainter::NoteRow& row)
    {
        return row.stateIconName.isEmpty() ? 0.0 : qMax(0, NOTE_STATE_ICON + NOTE_STATE_ICON_RIGHT + NOTE_STATE_ICON_GAP - NOTE_RIGHT);
    }

    /// Returns the width of a key cap of a shortcut row (the text and the padding of 10 at each side)
    static int getShortcutCapWidth(const PDFComponentPainter::ShortcutRow& row)
    {
        const QFont capFont = font(PDFThemeFonts::Role::SmallMedium);
        QString text = row.shortcut;
        if (row.recording)
        {
            text = PDFComponentPainter::getRecordShortcutText();
        }
        else if (text.isEmpty())
        {
            text = PDFComponentPainter::getAddShortcutText();
        }
        return textWidth(text, capFont) + 2 * SHORTCUT_CAP_SIDE;
    }
};

QSize PDFComponentPainter::getThumbnailItemPageSize(const ThumbnailItem& item)
{
    using Helper = PDFComponentDelegatePainterHelper;

    // An invalid or empty size is the default, a size out of range is clamped (components.md 6.4)
    const QSize size = item.pageSize;
    if (!size.isValid() || size.isEmpty())
    {
        return QSize(Helper::THUMB_PAGE_WIDTH, Helper::THUMB_PAGE_HEIGHT);
    }

    return QSize(qMin(size.width(), Helper::THUMB_MAX_PAGE), qMin(size.height(), Helper::THUMB_MAX_PAGE));
}

QSize PDFComponentPainter::getThumbnailItemSize(const ThumbnailItem& item)
{
    using Helper = PDFComponentDelegatePainterHelper;

    const QSize page = getThumbnailItemPageSize(item);
    const QFont labelFont = Helper::font(PDFThemeFonts::Role::CaptionSemiBold);
    const int labelWidth = Helper::textWidth(item.label, labelFont) + 2 * Helper::THUMB_LABEL_SIDE;
    const int width = qMax(page.width(), qMin(labelWidth, Helper::THUMB_MAX_PAGE)) + 2 * Helper::THUMB_SIDE;
    return QSize(width, Helper::THUMB_TOP + page.height() + Helper::THUMB_GAP + Helper::THUMB_LABEL_HEIGHT + Helper::THUMB_BOTTOM);
}

QRectF PDFComponentPainter::getThumbnailItemPageRect(const QRectF& rect, const ThumbnailItem& item)
{
    using Helper = PDFComponentDelegatePainterHelper;

    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    // The slot of the page is centered horizontally at the top padding
    const QSizeF slotSize = QSizeF(getThumbnailItemPageSize(item));
    const QRectF slot(qFloor(rect.left() + (rect.width() - slotSize.width()) / 2.0), rect.top() + Helper::THUMB_TOP, slotSize.width(), slotSize.height());

    if (item.pixmap.isNull() || item.pixmap.width() <= 0 || item.pixmap.height() <= 0)
    {
        return slot;
    }

    // The picture keeps its aspect ratio, whole logical pixels
    const QSizeF pixmapSize = QSizeF(item.pixmap.size()) / qMax<qreal>(item.pixmap.devicePixelRatio(), 1.0);
    QSizeF pageSize = pixmapSize.scaled(slotSize, Qt::KeepAspectRatio);
    pageSize = QSizeF(qMax<qreal>(1.0, qRound(pageSize.width())), qMax<qreal>(1.0, qRound(pageSize.height())));
    return QRectF(qFloor(slot.left() + (slot.width() - pageSize.width()) / 2.0), qFloor(slot.top() + (slot.height() - pageSize.height()) / 2.0), pageSize.width(), pageSize.height());
}

void PDFComponentPainter::paintThumbnailItem(QPainter* painter, const QRectF& rect, const ThumbnailItem& item, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::THUMB_RADIUS);
    const bool isSelected = Helper::isSelected(state);
    const bool isEnabled = state.isEnabled();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, true);

    // The page: the shadow of the frame with its border, the border outside of
    // the page (bg/accent 2 px when selected, border/strong of a disabled one)
    const QRectF page = getThumbnailItemPageRect(rect, item);
    const qreal borderWidth = isSelected ? 2.0 : 1.0;
    const Color borderColor = (isSelected && isEnabled) ? Color::BgAccent : Color::BorderStrong;
    const Helper::Radii pageRadii(Helper::THUMB_PAGE_RADIUS);
    const QRectF framed = page.adjusted(-borderWidth, -borderWidth, borderWidth, borderWidth);
    PDFStylePainter::shadow(painter, framed, pageRadii.adjusted(-borderWidth), QPointF(0.0, Helper::THUMB_SHADOW_OFFSET), Helper::THUMB_SHADOW_BLUR,
                            PDFStylePainter::color(Color::EffectShadow));
    PDFStylePainter::fill(painter, page, pageRadii, Color::ReadingPaperPage);

    if (!item.pixmap.isNull())
    {
        const PDFStylePainterGuard innerGuard(painter);
        painter->setClipPath(PDFStylePainter::roundedPath(page, pageRadii), Qt::IntersectClip);
        drawScaledPixmap(painter, page, item.pixmap);
    }
    PDFStylePainter::strokeOutside(painter, page, pageRadii, PDFStylePainter::color(borderColor), borderWidth);

    // The label of the page in a pill below the page (a page label is data)
    const qreal slotBottom = rect.top() + Helper::THUMB_TOP + getThumbnailItemPageSize(item).height();
    const QFont labelFont = Helper::font(isSelected ? PDFThemeFonts::Role::CaptionSemiBold : PDFThemeFonts::Role::Caption);
    const qreal maxLabelWidth = rect.width() - 2.0 * Helper::THUMB_SIDE;
    const qreal pillWidth = qMin<qreal>(Helper::textWidth(item.label, labelFont) + 2 * Helper::THUMB_LABEL_SIDE, maxLabelWidth);
    if (!item.label.isEmpty() && pillWidth > 2 * Helper::THUMB_LABEL_SIDE)
    {
        const QRectF pill(qFloor(rect.left() + (rect.width() - pillWidth) / 2.0), slotBottom + Helper::THUMB_GAP, pillWidth, Helper::THUMB_LABEL_HEIGHT);
        Color textColor = Color::TextTertiary;
        if (isSelected)
        {
            PDFStylePainter::fill(painter, pill, Helper::Radii(pill.height() / 2.0), isEnabled ? Color::BgAccent : Color::BgMuted);
            textColor = Color::IconOnAccent;
        }

        const QRectF textRect = pill.adjusted(Helper::THUMB_LABEL_SIDE, 0.0, -Helper::THUMB_LABEL_SIDE, 0.0);
        drawDataText(painter, textRect, Qt::AlignCenter | Qt::TextSingleLine, elidedText(item.label, labelFont, textRect.width()), labelFont,
                     PDFStylePainter::color(Helper::text(state, textColor)), getDataTextDirection(item.label));
    }

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

PDFComponentPainter::TileRowKind PDFComponentPainter::normalized(TileRowKind kind)
{
    switch (kind)
    {
        case TileRowKind::MenuItem:
        case TileRowKind::PanelStep:
        case TileRowKind::ListItem:
        case TileRowKind::Step:
        case TileRowKind::Pending:
        case TileRowKind::Result:
        case TileRowKind::Caption:
            return kind;
    }
    return TileRowKind::MenuItem;
}

PDFComponentPainter::TileRowTone PDFComponentPainter::normalized(TileRowTone tone)
{
    switch (tone)
    {
        case TileRowTone::Accent:
        case TileRowTone::Muted:
        case TileRowTone::Danger:
            return tone;
    }
    return TileRowTone::Accent;
}

QString PDFComponentPainter::getBoundedTileRowText(const QString& text)
{
    return PDFComponentDelegatePainterHelper::boundedTileText(text);
}

QSize PDFComponentPainter::getTileRowSize(const TileRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;

    // The width of the texts on one line, at most the column of the widest frame
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    int titleWidth = Helper::textWidth(Helper::boundedTileText(row.title), Helper::font(metrics.titleFont));
    if (Helper::isTileRowCaption(row))
    {
        const int width = 2 * metrics.horizontalPadding + qBound(Helper::MIN_TEXT, titleWidth, Helper::TILE_NATURAL_TEXT);
        return QSize(width, getTileRowHeight(row, width));
    }

    if (!row.tag.isEmpty())
    {
        titleWidth = qMin(Helper::MAX_EXTENT, titleWidth + Helper::TILE_TAG_GAP + qMin(Helper::MAX_EXTENT, Helper::getTileRowTagSize(row).width()));
    }
    const int descriptionWidth = Helper::textWidth(Helper::boundedTileText(row.description), Helper::font(PDFThemeFonts::Role::CaptionRegular));
    const int textWidth = qBound(Helper::MIN_TEXT, qMax(titleWidth, descriptionWidth), Helper::TILE_NATURAL_TEXT);

    const int width = 2 * metrics.horizontalPadding + Helper::getTileRowLeadingWidth(row, metrics) + textWidth + Helper::getTileRowEndingWidth(row, metrics);
    return QSize(width, getTileRowHeight(row, width));
}

QSize PDFComponentPainter::getTileRowMinimumSize(const TileRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    const int width = 2 * metrics.horizontalPadding + Helper::getTileRowLeadingWidth(row, metrics) + Helper::MIN_TEXT + Helper::getTileRowEndingWidth(row, metrics);
    return QSize(width, Helper::isTileRowCaption(row) ? getTileRowHeight(row, width) : metrics.tile + 2 * metrics.verticalPadding);
}

int PDFComponentPainter::getTileRowHeight(const TileRow& row, int width)
{
    using Helper = PDFComponentDelegatePainterHelper;
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    if (Helper::isTileRowCaption(row))
    {
        return Helper::TILE_CAPTION_TOP + metrics.titleLine + Helper::TILE_CAPTION_BOTTOM;
    }

    const int textWidth = Helper::getTileRowTextWidth(row, metrics, qBound(0, width, Helper::MAX_EXTENT));
    return qMax(metrics.tile, Helper::getTileRowTextHeight(row, metrics, textWidth)) + 2 * metrics.verticalPadding;
}

int PDFComponentPainter::getTileRowTrailingMargin(TileRowKind kind)
{
    return PDFComponentDelegatePainterHelper::getTileRowMetrics(kind).horizontalPadding;
}

QRectF PDFComponentPainter::getTileRowTileRect(const QRectF& rect, const TileRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (Helper::isTileRowCaption(row))
    {
        return QRectF();
    }

    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    const qreal left = rect.left() + metrics.horizontalPadding + (row.checkBoxVisible ? Helper::TILE_CHECK_BOX + metrics.gap : 0);
    return state.visualRect(rect, Helper::line(rect, left, metrics.tile, metrics.tile));
}

QRectF PDFComponentPainter::getTileRowCheckBoxRect(const QRectF& rect, const TileRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!row.checkBoxVisible || Helper::isTileRowCaption(row))
    {
        return QRectF();
    }

    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    return state.visualRect(rect, Helper::line(rect, rect.left() + metrics.horizontalPadding, Helper::TILE_CHECK_BOX, Helper::TILE_CHECK_BOX));
}

QRectF PDFComponentPainter::getTileRowLinkRect(const QRectF& rect, const TileRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (row.linkText.isEmpty() || Helper::isTileRowCaption(row))
    {
        return QRectF();
    }

    // The link is the last element of the row
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    const QSize size = getTextLinkSize(TextLink{ Helper::boundedTileText(row.linkText) });
    return state.visualRect(rect, Helper::line(rect, rect.right() - metrics.horizontalPadding - size.width(), size.width(), size.height()));
}

QRectF PDFComponentPainter::getTileRowEndIconRect(const QRectF& rect, const TileRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!Helper::hasTileRowEndPlace(row))
    {
        return QRectF();
    }

    // The icon stands before a link, when the row has both
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    const int linkWidth = row.linkText.isEmpty() ? 0 : getTextLinkSize(TextLink{ Helper::boundedTileText(row.linkText) }).width() + metrics.gap;
    const qreal left = rect.right() - metrics.horizontalPadding - linkWidth - Helper::TILE_END_ICON;
    return state.visualRect(rect, Helper::line(rect, left, Helper::TILE_END_ICON, Helper::TILE_END_ICON));
}

QRectF PDFComponentPainter::getTileRowTextRect(const QRectF& rect, const TileRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);
    const int textWidth = Helper::getTileRowTextWidth(row, metrics, qBound(0, qFloor(rect.width()), Helper::MAX_EXTENT));
    if (Helper::isTileRowCaption(row))
    {
        return QRectF(rect.left() + metrics.horizontalPadding, rect.top() + Helper::TILE_CAPTION_TOP, textWidth, metrics.titleLine);
    }

    const int textHeight = Helper::getTileRowTextHeight(row, metrics, textWidth);
    return Helper::line(rect, rect.left() + metrics.horizontalPadding + Helper::getTileRowLeadingWidth(row, metrics), textWidth, textHeight);
}

void PDFComponentPainter::paintTileRow(QPainter* painter, const QRectF& rect, const TileRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::TileRowMetrics metrics = Helper::getTileRowMetrics(row.kind);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // A caption is a text: no fill and no focus of a row (438:14866)
    if (Helper::isTileRowCaption(row))
    {
        Helper::drawText(painter, state, rect, getTileRowTextRect(rect, row), Helper::boundedTileText(row.title), Helper::font(metrics.titleFont),
                         Helper::text(state, Color::TextTertiary), row.titleKind);
        return;
    }

    Helper::fillTileRow(painter, rect, row, state);

    // The check box follows the row, a check box disabled alone is disabled
    if (row.paintNested && row.checkBoxVisible)
    {
        const State nested = Helper::nestedState(state, row.checkBoxFlags);
        PDFStylePainter::IndicatorState indicator;
        indicator.enabled = nested.isEnabled() && row.checkBoxEnabled;
        indicator.hover = indicator.enabled && nested.isHovered();
        indicator.pressed = indicator.enabled && nested.isPressed();
        indicator.on = row.checkBoxChecked;
        PDFStylePainter::indicator(painter, getTileRowCheckBoxRect(rect, row, state), PDFStylePainter::IndicatorKind::CheckBox, indicator);
    }

    // The tile with its icon, or a text (a number of a page)
    Color tileFill = Color::BgMuted;
    Color tileContent = Color::IconMuted;
    Helper::getTileRowColors(row, state, tileFill, tileContent);
    const QRectF tileRect = getTileRowTileRect(rect, row, state);
    PDFStylePainter::fill(painter, tileRect, Helper::Radii(Helper::TILE_RADIUS), tileFill);
    if (!row.tileText.isEmpty())
    {
        const QFont tileFont = Helper::font(PDFThemeFonts::Role::CaptionSemiBold);
        const QString tileText = Helper::boundedTileText(row.tileText);
        drawDataText(painter, tileRect, Qt::AlignCenter | Qt::TextSingleLine, elidedText(tileText, tileFont, tileRect.width()), tileFont,
                     PDFStylePainter::color(tileContent), getDataTextDirection(tileText));
    }
    else if (!row.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, alignedCentered(tileRect, QSizeF(metrics.icon, metrics.icon)), row.iconName, tileContent);
    }

    // The column of the texts: the title with the tag after it and the description
    const QRectF textRect = getTileRowTextRect(rect, row);
    const int textWidth = qFloor(textRect.width());
    const int titleHeight = Helper::getTileRowTitleHeight(row, metrics, textWidth);
    const QRectF titleLine(textRect.left(), textRect.top(), textRect.width(), titleHeight);

    const QFont titleFont = Helper::font(metrics.titleFont);
    const QString title = Helper::boundedTileText(row.title);
    qreal titleWidth = titleLine.width();
    if (Helper::isTileRowTagShown(row, textWidth))
    {
        const QSize tagSize = Helper::getTileRowTagSize(row);
        titleWidth = qMin<qreal>(Helper::textWidth(title, titleFont), textWidth - tagSize.width() - Helper::TILE_TAG_GAP);
        const QRectF tagRect = Helper::line(titleLine, titleLine.left() + titleWidth + Helper::TILE_TAG_GAP, tagSize.width(), tagSize.height());
        paintTag(painter, state.visualRect(rect, tagRect), Tag{ Helper::boundedTileText(row.tag) }, Helper::nestedState(state, QStyle::State_None));
    }
    Helper::drawText(painter, state, rect, QRectF(titleLine.left(), titleLine.top(), titleWidth, titleLine.height()), title, titleFont,
                     Helper::text(state, Helper::getTileRowTitleColor(row)), row.titleKind);

    const Color descriptionColor = (row.descriptionColor.has_value() && Helper::isValidColor(*row.descriptionColor)) ? *row.descriptionColor : metrics.descriptionColor;
    const QStringList lines = Helper::getTileRowDescriptionLines(row, metrics, textWidth);
    const QFont descriptionFont = Helper::font(PDFThemeFonts::Role::CaptionRegular);
    const qreal descriptionTop = titleLine.bottom() + Helper::TILE_TEXT_GAP;
    if (metrics.maxDescriptionLines > 1)
    {
        // The wrapped lines of one text share its direction
        const QString description = Helper::boundedTileText(row.description);
        const Qt::LayoutDirection direction = row.descriptionKind == TextKind::Interface ? state.direction : getDataTextDirection(description);
        for (qsizetype i = 0; i < lines.size(); ++i)
        {
            const QRectF lineRect = state.visualRect(rect, QRectF(textRect.left(), descriptionTop + i * metrics.descriptionLine, textRect.width(), metrics.descriptionLine));
            drawDataText(painter, lineRect, Qt::AlignVCenter | Qt::TextSingleLine | state.leadingAlignment(), elidedText(lines.at(i), descriptionFont, textRect.width()), descriptionFont,
                         PDFStylePainter::color(Helper::text(state, descriptionColor)), direction);
        }
    }
    else if (!lines.isEmpty())
    {
        Helper::drawText(painter, state, rect, QRectF(textRect.left(), descriptionTop, textRect.width(), metrics.descriptionLine), lines.first(), descriptionFont,
                         Helper::text(state, descriptionColor), row.descriptionKind);
    }

    // The ending: the check mark of a chosen item (icon/accent, 438:15032) or the icon of a state
    const QRectF endIconRect = getTileRowEndIconRect(rect, row, state);
    if (Helper::hasTileRowCheckMark(row))
    {
        PDFStylePainter::icon(painter, endIconRect, QStringLiteral("check"), Helper::icon(state, Color::IconAccent));
    }
    else if (!row.endIconName.isEmpty())
    {
        PDFStylePainter::icon(painter, endIconRect, row.endIconName, Helper::icon(state, Helper::isValidColor(row.endIconColor) ? row.endIconColor : Color::IconAccent));
    }

    if (row.paintNested && !row.linkText.isEmpty())
    {
        paintTextLink(painter, getTileRowLinkRect(rect, row, state), TextLink{ Helper::boundedTileText(row.linkText) }, Helper::nestedState(state, row.linkFlags));
    }

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, Helper::Radii(Helper::TILE_RADIUS));
    }
}

PDFComponentPainter::PageTileState PDFComponentPainter::normalized(PageTileState tileState)
{
    switch (tileState)
    {
        case PageTileState::Normal:
        case PageTileState::Selected:
        case PageTileState::Marked:
            return tileState;
    }
    return PageTileState::Normal;
}

QSize PDFComponentPainter::getPageTileSlotSize(const PageTile& tile)
{
    using Helper = PDFComponentDelegatePainterHelper;

    const QSize size = tile.slotSize;
    if (!size.isValid() || size.isEmpty())
    {
        return QSize(Helper::PAGE_TILE_SLOT_WIDTH, Helper::PAGE_TILE_SLOT_HEIGHT);
    }
    return QSize(qMin(size.width(), Helper::PAGE_TILE_MAX_SLOT), qMin(size.height(), Helper::PAGE_TILE_MAX_SLOT));
}

QSize PDFComponentPainter::getPageTileSize(const PageTile& tile)
{
    using Helper = PDFComponentDelegatePainterHelper;

    const QSize slot = getPageTileSlotSize(tile);
    const int border = 2 * Helper::getPageTileInset(tile);
    const int caption = tile.caption.isEmpty() ? 0 : Helper::PAGE_TILE_GAP + Helper::PAGE_TILE_CAPTION;
    return QSize(slot.width() + 2 * Helper::PAGE_TILE_SIDE + border,
                 Helper::PAGE_TILE_TOP + slot.height() + Helper::PAGE_TILE_GAP + Helper::LINE_11 + caption + Helper::PAGE_TILE_BOTTOM + border);
}

QRectF PDFComponentPainter::getPageTilePageRect(const QRectF& rect, const PageTile& tile)
{
    using Helper = PDFComponentDelegatePainterHelper;

    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    // The slot is centered horizontally at the top padding (a tile of a grid may be wider)
    const QSizeF slotSize = QSizeF(getPageTileSlotSize(tile));
    const QRectF slot(qFloor(rect.left() + (rect.width() - slotSize.width()) / 2.0), rect.top() + Helper::PAGE_TILE_TOP + Helper::getPageTileInset(tile), slotSize.width(), slotSize.height());

    // The proportions of the page as it will be saved, of the picture, or of the design
    QSizeF paperSize(Helper::PAGE_TILE_DEFAULT_PAPER_WIDTH, Helper::PAGE_TILE_DEFAULT_PAPER_HEIGHT);
    const bool isPageSizeValid = qIsFinite(tile.pageSize.width()) && qIsFinite(tile.pageSize.height()) && tile.pageSize.width() > 0.0 && tile.pageSize.height() > 0.0;
    if (isPageSizeValid)
    {
        // Relative to its longer side: QSizeF::scaled overflows on a page of a hostile
        // document (1e308 x 1e308), a side too small to be relative gives one pixel below
        const qreal longerSide = qMax(tile.pageSize.width(), tile.pageSize.height());
        paperSize = QSizeF(qMax(tile.pageSize.width() / longerSide, 1e-6), qMax(tile.pageSize.height() / longerSide, 1e-6));
    }
    else if (!tile.pixmap.isNull() && tile.pixmap.width() > 0 && tile.pixmap.height() > 0)
    {
        paperSize = QSizeF(tile.pixmap.size());
    }

    // Whole logical pixels, at least one (a hostile page 14400 x 1)
    paperSize = paperSize.scaled(slotSize, Qt::KeepAspectRatio);
    const QSizeF pageSize(qBound<qreal>(1.0, qRound(paperSize.width()), slotSize.width()), qBound<qreal>(1.0, qRound(paperSize.height()), slotSize.height()));
    return QRectF(qFloor(slot.left() + (slot.width() - pageSize.width()) / 2.0), qFloor(slot.top() + (slot.height() - pageSize.height()) / 2.0), pageSize.width(), pageSize.height());
}

QRectF PDFComponentPainter::getPageTileBadgeRect(const QRectF& rect, const PageTile& tile, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;

    if (tile.badgeIconName.isEmpty() || !Helper::isDrawable(rect))
    {
        return QRectF();
    }

    const QRectF page = getPageTilePageRect(rect, tile);
    const QRectF badge(page.right() + Helper::PAGE_TILE_BADGE_OVERLAP - Helper::PAGE_TILE_BADGE, page.top() - Helper::PAGE_TILE_BADGE_OVERLAP, Helper::PAGE_TILE_BADGE, Helper::PAGE_TILE_BADGE);
    return state.visualRect(rect, badge);
}

void PDFComponentPainter::paintPageTile(QPainter* painter, const QRectF& rect, const PageTile& tile, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PageTileState tileState = normalized(tile.tileState);
    const bool isSelected = tileState == PageTileState::Selected;
    const bool isMarked = tileState == PageTileState::Marked;
    const Helper::Radii radii(Helper::PAGE_TILE_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The tile: the selected one bg/accent-subtle with its border (its hover and
    // press, decision 10), the others the overlay of the pointer only. A selected
    // state of the view is not a look: the model says the state of the tile.
    State fillState = state;
    fillState.flags.setFlag(QStyle::State_On, isSelected);
    fillState.flags.setFlag(QStyle::State_Selected, false);
    Helper::fillRow(painter, rect, radii, fillState, true);
    if (isSelected)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, state.isEnabled() ? Color::BorderFocus : Color::BorderStrong, Helper::PAGE_TILE_BORDER);
    }

    // The page: the paper with its border below the picture, which covers it. A
    // marked page is dimmed by the opacities of the layers of 388:20614, so its
    // border shows through (a content preview, not a control, rule N-22).
    const QRectF page = getPageTilePageRect(rect, tile);
    {
        const PDFStylePainterGuard paperGuard(painter);
        painter->setOpacity(painter->opacity() * (isMarked ? Helper::PAGE_TILE_PAPER_OPACITY : 1.0));
        PDFStylePainter::fillRect(painter, page, Color::SampleWhite);
        PDFStylePainter::strokeInside(painter, page, Helper::Radii(0.0), Color::BorderStrong, 1.0);
    }
    {
        const PDFStylePainterGuard previewGuard(painter);
        painter->setOpacity(painter->opacity() * (isMarked ? Helper::PAGE_TILE_PREVIEW_OPACITY : 1.0));
        PDFStylePainter::fillRect(painter, page, Color::ReadingPaperPage);
        if (!tile.pixmap.isNull())
        {
            drawScaledPixmap(painter, page, tile.pixmap);
        }
    }

    // The badge at the corner of the page
    const QRectF badgeRect = getPageTileBadgeRect(rect, tile, state);
    if (!badgeRect.isEmpty())
    {
        const PDFAccentHue hue = isMarked ? PDFAccentHue::Red : PDFAccentHue::Blue;
        const Helper::Radii badgeRadii(Helper::PAGE_TILE_BADGE / 2.0);
        PDFStylePainter::fill(painter, badgeRect, badgeRadii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background));
        PDFStylePainter::strokeInside(painter, badgeRect, badgeRadii, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Border), 1.0);
        PDFStylePainter::icon(painter, alignedCentered(badgeRect, QSizeF(Helper::PAGE_TILE_BADGE_ICON, Helper::PAGE_TILE_BADGE_ICON)), tile.badgeIconName,
                              Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)));
    }

    // The label (data) and the caption (a text of the user interface) below the slot
    const int inset = Helper::getPageTileInset(tile);
    const qreal labelTop = rect.top() + Helper::PAGE_TILE_TOP + inset + getPageTileSlotSize(tile).height() + Helper::PAGE_TILE_GAP;
    const qreal textWidth = qMax<qreal>(0.0, rect.width() - 2.0 * (Helper::PAGE_TILE_SIDE + inset));
    const qreal textLeft = rect.left() + Helper::PAGE_TILE_SIDE + inset;
    const QFont labelFont = Helper::font(isSelected ? PDFThemeFonts::Role::CaptionSemiBold : PDFThemeFonts::Role::Caption);
    const QString label = Helper::boundedTileText(tile.label);
    drawDataText(painter, QRectF(textLeft, labelTop, textWidth, Helper::LINE_11), Qt::AlignCenter | Qt::TextSingleLine, elidedText(label, labelFont, textWidth), labelFont,
                 PDFStylePainter::color(Helper::text(state, isSelected ? Color::TextAccent : Color::TextSecondary)), getDataTextDirection(label));

    if (!tile.caption.isEmpty())
    {
        const QFont captionFont = Helper::font(PDFThemeFonts::Role::Tiny);
        const Color captionColor = isMarked ? Color::TextDanger : (isSelected ? Color::TextAccent : Color::TextMuted);
        drawDataText(painter, QRectF(textLeft, labelTop + Helper::LINE_11 + Helper::PAGE_TILE_GAP, textWidth, Helper::PAGE_TILE_CAPTION), Qt::AlignCenter | Qt::TextSingleLine,
                     elidedText(Helper::boundedTileText(tile.caption), captionFont, textWidth), captionFont, PDFStylePainter::color(Helper::text(state, captionColor)), state.direction);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

QSize PDFComponentPainter::getBookmarkRowSize(const BookmarkRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(qMax(Helper::BOOKMARK_WIDTH, getBookmarkRowMinimumSize(row).width()), Helper::BOOKMARK_HEIGHT);
}

QSize PDFComponentPainter::getBookmarkRowMinimumSize(const BookmarkRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    const int tagWidth = row.tag.isEmpty() ? 0 : qMin(Helper::MAX_EXTENT, getTagSize(Tag{ row.tag }).width()) + Helper::BOOKMARK_GAP;
    return QSize(2 * Helper::BOOKMARK_SIDE + Helper::BOOKMARK_ICON + Helper::BOOKMARK_GAP + Helper::MIN_TEXT + tagWidth, Helper::BOOKMARK_HEIGHT);
}

void PDFComponentPainter::paintBookmarkRow(QPainter* painter, const QRectF& rect, const BookmarkRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);
    const bool isSelected = Helper::isSelected(state);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, true);

    // The filled bookmark (icon/accent fill and stroke in both selections, review I-13)
    const QRectF iconRect = Helper::line(rect, rect.left() + Helper::BOOKMARK_SIDE, Helper::BOOKMARK_ICON, Helper::BOOKMARK_ICON);
    const QColor iconColor = PDFStylePainter::color(Helper::icon(state, Color::IconAccent));
    PDFThemeIcons::paintFilled(painter, state.visualRect(rect, iconRect), QStringLiteral("bookmark"), iconColor, iconColor);

    // The tag at the trailing edge, dropped when the texts would have no room
    qreal textRight = rect.right() - Helper::BOOKMARK_SIDE;
    const qreal textLeft = iconRect.right() + Helper::BOOKMARK_GAP;
    if (!row.tag.isEmpty())
    {
        const Tag tag{ row.tag };
        const QSize tagSize = getTagSize(tag);
        const QRectF tagRect = Helper::line(rect, textRight - tagSize.width(), tagSize.width(), tagSize.height());
        if (tagRect.left() - Helper::BOOKMARK_GAP - textLeft >= Helper::MIN_TEXT)
        {
            paintTag(painter, state.visualRect(rect, tagRect), tag, Helper::nestedState(state, QStyle::State_None));
            textRight = tagRect.left() - Helper::BOOKMARK_GAP;
        }
    }

    Helper::drawTextBlock(painter, state, rect, textLeft, textRight - textLeft, 1,
                          row.title, Helper::text(state, isSelected ? Color::TextAccent : Color::TextPrimary), Helper::TextKind::Data,
                          row.subtitle, Helper::text(state, Color::TextMuted), Helper::TextKind::Data);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

int PDFComponentPainter::getNoteRowHeight(const NoteRow& row, int width)
{
    using Helper = PDFComponentDelegatePainterHelper;

    const int lines = Helper::getNoteLines(row, Helper::getNoteTextWidth(qBound(0, width, Helper::MAX_EXTENT)) - Helper::getNoteStateIconReserve(row));
    const int textHeight = Helper::LINE_12 + (lines > 0 ? Helper::NOTE_TEXT_GAP + lines * Helper::LINE_12 : 0);
    return 2 * Helper::NOTE_TOP + qMax(Helper::BADGE, textHeight);
}

QSize PDFComponentPainter::getNoteRowSize(const NoteRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(Helper::NOTE_WIDTH, getNoteRowHeight(row, Helper::NOTE_WIDTH));
}

QSize PDFComponentPainter::getNoteRowMinimumSize(const NoteRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(Helper::NOTE_LEFT + Helper::BADGE + Helper::NOTE_GAP + Helper::MIN_TEXT + Helper::NOTE_RIGHT, 2 * Helper::NOTE_TOP + Helper::BADGE);
}

QRectF PDFComponentPainter::getNoteRowStateIconRect(const QRectF& rect, const NoteRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (row.stateIconName.isEmpty() || !Helper::isDrawable(rect))
    {
        return QRectF();
    }

    // Centered on the first line of the text (on the head line without a text)
    const qreal lineTop = rect.top() + Helper::NOTE_TOP + (row.text.isEmpty() ? 0 : Helper::LINE_12 + Helper::NOTE_TEXT_GAP);
    const qreal top = lineTop + (Helper::LINE_12 - Helper::NOTE_STATE_ICON) / 2.0;
    return QRectF(rect.right() - Helper::NOTE_STATE_ICON_RIGHT - Helper::NOTE_STATE_ICON, top, Helper::NOTE_STATE_ICON, Helper::NOTE_STATE_ICON);
}

void PDFComponentPainter::paintNoteRow(QPainter* painter, const QRectF& rect, const NoteRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);
    const bool isSelected = Helper::isSelected(state);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, true);

    // The badge of the type of the annotation at the top
    const QRectF badgeRect(rect.left() + Helper::NOTE_LEFT, rect.top() + Helper::NOTE_TOP, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ row.iconName, row.hue, BadgeSize::Medium }, state);

    // The head: the author and the date at the trailing edge
    const qreal textLeft = badgeRect.right() + Helper::NOTE_GAP;
    const qreal textWidth = rect.right() - Helper::NOTE_RIGHT - textLeft;
    const qreal headTop = rect.top() + Helper::NOTE_TOP;

    // Without a text the state icon lies on the head line - the date keeps the gap before it
    const qreal headWidth = qMax<qreal>(0.0, textWidth - (row.text.isEmpty() ? Helper::getNoteStateIconReserve(row) : 0.0));
    qreal authorWidth = headWidth;
    if (!row.date.isEmpty() && headWidth > 0.0)
    {
        const QFont dateFont = Helper::font(PDFThemeFonts::Role::CaptionRegular);
        const qreal dateWidth = qMin<qreal>(Helper::textWidth(row.date, dateFont), headWidth);
        const QRectF dateRect(textLeft + headWidth - dateWidth, headTop + (Helper::LINE_12 - Helper::LINE_11) / 2, dateWidth, Helper::LINE_11);
        Helper::drawText(painter, state, rect, dateRect, row.date, dateFont, Helper::text(state, Color::TextMuted), Helper::TextKind::Interface, true);
        authorWidth = headWidth - dateWidth - Helper::NOTE_HEAD_GAP;
    }
    Helper::drawText(painter, state, rect, QRectF(textLeft, headTop, authorWidth, Helper::LINE_12), row.author, Helper::font(PDFThemeFonts::Role::SmallSemiBold),
                     Helper::text(state, isSelected ? Color::TextAccent : Color::TextPrimary), Helper::TextKind::Data);

    // The state icon at the trailing side of the first line of the text
    const QRectF stateIconRect = getNoteRowStateIconRect(rect, row);
    if (!stateIconRect.isEmpty())
    {
        PDFStylePainter::icon(painter, state.visualRect(rect, stateIconRect), row.stateIconName, Helper::icon(state, Color::IconMuted));
    }

    // The text of at most two lines (content of the user: its own direction)
    const QFont bodyFont = Helper::font(PDFThemeFonts::Role::Small);
    const qreal bodyWidth = qMax<qreal>(0.0, textWidth - Helper::getNoteStateIconReserve(row));
    const QStringList lines = wrapText(row.text, bodyFont, bodyWidth, Helper::NOTE_MAX_LINES);
    const Qt::LayoutDirection bodyDirection = getDataTextDirection(row.text);
    const qreal bodyTop = headTop + Helper::LINE_12 + Helper::NOTE_TEXT_GAP;
    for (int i = 0; i < lines.size(); ++i)
    {
        const QRectF lineRect = state.visualRect(rect, QRectF(textLeft, bodyTop + i * Helper::LINE_12, bodyWidth, Helper::LINE_12));
        drawDataText(painter, lineRect, Qt::AlignVCenter | Qt::TextSingleLine | state.leadingAlignment(), lines.at(i), bodyFont,
                     PDFStylePainter::color(Helper::text(state, Color::TextSecondary)), bodyDirection);
    }

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

QSize PDFComponentPainter::getAttachmentRowSize(const AttachmentRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(Helper::ATTACHMENT_WIDTH, Helper::ATTACHMENT_HEIGHT);
}

QSize PDFComponentPainter::getAttachmentRowMinimumSize(const AttachmentRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(Helper::ATTACHMENT_LEFT + Helper::BADGE + 2 * Helper::ATTACHMENT_GAP + Helper::MIN_TEXT + Helper::ATTACHMENT_SAVE + Helper::ATTACHMENT_RIGHT,
                 Helper::ATTACHMENT_HEIGHT);
}

QRectF PDFComponentPainter::getAttachmentRowSaveRect(const QRectF& rect, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    const QRectF save = Helper::line(rect, rect.right() - Helper::ATTACHMENT_RIGHT - Helper::ATTACHMENT_SAVE, Helper::ATTACHMENT_SAVE, Helper::ATTACHMENT_SAVE);
    return state.visualRect(rect, save);
}

void PDFComponentPainter::paintAttachmentRow(QPainter* painter, const QRectF& rect, const AttachmentRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);
    const bool isSelected = Helper::isSelected(state);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, true);

    const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::ATTACHMENT_LEFT, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ row.iconName, row.hue, BadgeSize::Medium }, state);

    // The save action: an Icon button 24, whose icon is icon/default on the
    // hovered or pressed row (F-80); the override of the row holds in every state
    // of the button, its own hover and press change only its background (card
    // Icon button 706:147674)
    const QRectF saveRect = getAttachmentRowSaveRect(rect, state);
    const State saveState = Helper::nestedState(state, row.saveFlags);
    IconButton save;
    save.iconName = QStringLiteral("download");
    save.extent = Helper::ATTACHMENT_SAVE;
    save.iconExtent = Helper::ATTACHMENT_SAVE_ICON;
    save.radius = Helper::ATTACHMENT_SAVE_RADIUS;
    if (state.isHovered() || state.isPressed())
    {
        save.iconColor = Color::IconDefault;
    }
    paintIconButton(painter, saveRect, save, saveState);

    // The name of the file is elided in the middle (its extension stays)
    const qreal textLeft = badgeRect.right() + Helper::ATTACHMENT_GAP;
    const qreal textRight = rect.right() - Helper::ATTACHMENT_RIGHT - Helper::ATTACHMENT_SAVE - Helper::ATTACHMENT_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, textRight - textLeft, 1,
                          row.name, Helper::text(state, isSelected ? Color::TextAccent : Color::TextPrimary), Helper::TextKind::FileName,
                          row.meta, Helper::text(state, Color::TextMuted), Helper::TextKind::Interface);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

int PDFComponentPainter::getSearchResultRowHeight(const SearchResultRow& row, int width)
{
    using Helper = PDFComponentDelegatePainterHelper;

    // The number of the lines only (one layout without the elision, review C5)
    const qreal snippetWidth = qreal(qBound(0, width, Helper::MAX_EXTENT)) - 2 * Helper::SEARCH_SIDE;
    const int lines = Helper::countSnippetLines(row.snippet, row.matches, Helper::font(PDFThemeFonts::Role::Small), snippetWidth, Helper::SEARCH_MAX_LINES);
    const int snippetHeight = lines > 0 ? Helper::SEARCH_GAP + lines * Helper::LINE_12 : 0;
    return 2 * Helper::SEARCH_TOP + Helper::LINE_11 + snippetHeight;
}

QSize PDFComponentPainter::getSearchResultRowSize(const SearchResultRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(Helper::SEARCH_WIDTH, getSearchResultRowHeight(row, Helper::SEARCH_WIDTH));
}

QSize PDFComponentPainter::getSearchResultRowMinimumSize(const SearchResultRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    const int pageWidth = Helper::textWidth(row.page, Helper::font(PDFThemeFonts::Role::Caption));
    const int snippetHeight = row.snippet.isEmpty() ? 0 : Helper::SEARCH_GAP + Helper::LINE_12;
    return QSize(2 * Helper::SEARCH_SIDE + qMax(Helper::MIN_TEXT, qMin(pageWidth, Helper::MAX_EXTENT)), 2 * Helper::SEARCH_TOP + Helper::LINE_11 + snippetHeight);
}

void PDFComponentPainter::paintSearchResultRow(QPainter* painter, const QRectF& rect, const SearchResultRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);
    const bool isSelected = Helper::isSelected(state);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, true);

    const qreal left = rect.left() + Helper::SEARCH_SIDE;
    const qreal width = rect.width() - 2 * Helper::SEARCH_SIDE;
    const qreal top = rect.top() + Helper::SEARCH_TOP;
    Helper::drawText(painter, state, rect, QRectF(left, top, width, Helper::LINE_11), row.page, Helper::font(PDFThemeFonts::Role::Caption),
                     Helper::text(state, isSelected ? Color::TextAccent : Color::TextMuted), Helper::TextKind::Interface);

    // The snippet: content of the document (its own direction), the found terms
    // Semi Bold text/primary, the rest Regular text/secondary
    QTextLayout layout;
    if (Helper::layoutSnippet(layout, row.snippet, row.matches, Helper::font(PDFThemeFonts::Role::Small), width, Helper::SEARCH_MAX_LINES,
                              state.direction, Helper::text(state, Color::TextPrimary)) > 0)
    {
        const QRectF snippetRect = state.visualRect(rect, QRectF(left, top + Helper::LINE_11 + Helper::SEARCH_GAP, width, 1.0));
        const PDFStylePainterGuard innerGuard(painter);
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setPen(PDFStylePainter::color(Helper::text(state, Color::TextSecondary)));
        layout.draw(painter, snippetRect.topLeft());
    }

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

QString PDFComponentPainter::getFontStatusText(bool embedded)
{
    return embedded ? QCoreApplication::translate("PDFComponentPainter", "Embedded subset") : QCoreApplication::translate("PDFComponentPainter", "Not embedded");
}

QSize PDFComponentPainter::getFontRowSize(const FontRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(qMax(Helper::FONT_WIDTH, getFontRowMinimumSize(row).width()), Helper::FONT_HEIGHT);
}

QSize PDFComponentPainter::getFontRowMinimumSize(const FontRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentDelegatePainterHelper;
    const int width = 2 * Helper::FONT_SIDE + Helper::BADGE + 4 * Helper::FONT_GAP + Helper::MIN_TEXT + Helper::FONT_TYPE + Helper::FONT_STATUS + Helper::FONT_COPY;
    return QSize(width, Helper::FONT_HEIGHT);
}

QRectF PDFComponentPainter::getFontRowCopyRect(const QRectF& rect, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    return state.visualRect(rect, Helper::line(rect, rect.right() - Helper::FONT_SIDE - Helper::FONT_COPY, Helper::FONT_COPY, Helper::FONT_COPY));
}

bool PDFComponentPainter::isFontRowCopyShown(const State& state)
{
    return state.isHovered() || state.isPressed();
}

void PDFComponentPainter::paintFontRow(QPainter* painter, const QRectF& rect, const FontRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, false);

    const QRectF badgeRect = Helper::line(rect, rect.left() + Helper::FONT_SIDE, Helper::BADGE, Helper::BADGE);
    paintToolBadge(painter, state.visualRect(rect, badgeRect), ToolBadge{ QStringLiteral("type"), PDFAccentHue::Neutral, BadgeSize::Medium }, state);

    // Columns from the trailing edge: copy, status, type
    const qreal copyLeft = rect.right() - Helper::FONT_SIDE - Helper::FONT_COPY;
    const qreal statusLeft = copyLeft - Helper::FONT_GAP - Helper::FONT_STATUS;
    const qreal typeLeft = statusLeft - Helper::FONT_GAP - Helper::FONT_TYPE;

    if (isFontRowCopyShown(state))
    {
        const QRectF copyRect = getFontRowCopyRect(rect, state);
        PDFStylePainter::icon(painter, alignedCentered(copyRect, QSizeF(Helper::FONT_COPY_ICON, Helper::FONT_COPY_ICON)), QStringLiteral("copy"),
                              Helper::icon(state, Color::IconDefault));
    }

    // Status: the dot of the hue and its text
    const QRectF dotRect = Helper::line(rect, statusLeft, Helper::FONT_DOT, Helper::FONT_DOT);
    PDFStylePainter::fillEllipse(painter, state.visualRect(rect, dotRect), PDFStylePainter::color(Helper::icon(state, row.embedded ? Color::AccentGreenIcon : Color::AccentAmberIcon)));
    const QString statusText = row.statusText.isEmpty() ? getFontStatusText(row.embedded) : row.statusText;
    const qreal statusTextLeft = dotRect.right() + Helper::FONT_DOT_GAP;
    Helper::drawText(painter, state, rect, Helper::line(rect, statusTextLeft, statusLeft + Helper::FONT_STATUS - statusTextLeft, Helper::LINE_12), statusText,
                     Helper::font(PDFThemeFonts::Role::SmallMedium), Helper::text(state, Color::TextSecondary), Helper::TextKind::Interface);

    // The tag of the type at the leading edge of its column (elided to the column)
    if (!row.type.isEmpty())
    {
        const Tag tag{ row.type };
        const QSize tagSize = getTagSize(tag);
        const qreal tagWidth = qMin<qreal>(tagSize.width(), Helper::FONT_TYPE);
        paintTag(painter, state.visualRect(rect, Helper::line(rect, typeLeft, tagWidth, tagSize.height())), tag, Helper::nestedState(state, QStyle::State_None));
    }

    const qreal textLeft = badgeRect.right() + Helper::FONT_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, typeLeft - Helper::FONT_GAP - textLeft, 1,
                          row.name, Helper::text(state, Color::TextPrimary), Helper::TextKind::Data,
                          row.family, Helper::text(state, Color::TextMuted), Helper::TextKind::Interface);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

PDFComponentPainter::RecentItemType PDFComponentPainter::normalized(RecentItemType type)
{
    switch (type)
    {
        case RecentItemType::File:
        case RecentItemType::Folder:
        case RecentItemType::Workspace:
            return type;
    }

    return RecentItemType::File;
}

QSize PDFComponentPainter::getRecentItemRowSize(const RecentItemRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(qMax(Helper::RECENT_WIDTH, getRecentItemRowMinimumSize(row).width()), Helper::RECENT_HEIGHT);
}

QSize PDFComponentPainter::getRecentItemRowMinimumSize(const RecentItemRow& row)
{
    Q_UNUSED(row);
    using Helper = PDFComponentDelegatePainterHelper;
    const int width = Helper::RECENT_LEFT + Helper::RECENT_LEADING + 6 * Helper::RECENT_GAP + Helper::MIN_TEXT + Helper::RECENT_PROGRESS + Helper::RECENT_OPENED +
                      Helper::RECENT_STAR + Helper::RECENT_MORE + Helper::RECENT_RIGHT;
    return QSize(width, Helper::RECENT_HEIGHT);
}

QRectF PDFComponentPainter::getRecentItemRowMoreRect(const QRectF& rect, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    return state.visualRect(rect, Helper::line(rect, rect.right() - Helper::RECENT_RIGHT - Helper::RECENT_MORE, Helper::RECENT_MORE, Helper::RECENT_MORE));
}

QRectF PDFComponentPainter::getRecentItemRowStarRect(const QRectF& rect, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    const qreal starLeft = rect.right() - Helper::RECENT_RIGHT - Helper::RECENT_MORE - Helper::RECENT_GAP - Helper::RECENT_STAR;
    return state.visualRect(rect, Helper::line(rect, starLeft, Helper::RECENT_STAR, Helper::RECENT_STAR));
}

void PDFComponentPainter::paintRecentItemRow(QPainter* painter, const QRectF& rect, const RecentItemRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);
    const RecentItemType type = normalized(row.type);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, state, false);

    // Leading: the thumbnail of the first page, or the icon in the circle of the hue
    const QRectF leading = Helper::line(rect, rect.left() + Helper::RECENT_LEFT, Helper::RECENT_LEADING, Helper::RECENT_LEADING);
    const bool isMissing = normalized(row.status) == FileStatus::Missing;
    if (type == RecentItemType::File)
    {
        const QSizeF thumbnailSize = QSizeF(getPageThumbnailSize(ThumbnailSize::Small));
        const QRectF frame = alignedCentered(state.visualRect(rect, leading), thumbnailSize);
        paintPageThumbnail(painter, frame, PageThumbnail{ ThumbnailSize::Small, row.pixmap, row.locked }, state);
    }
    else
    {
        const bool isFolder = type == RecentItemType::Folder;
        const PDFAccentHue hue = isFolder ? PDFAccentHue::Neutral : PDFAccentHue::Violet;
        const QRectF circle = alignedCentered(state.visualRect(rect, leading), QSizeF(Helper::BADGE, Helper::BADGE));
        PDFStylePainter::fillEllipse(painter, circle, PDFStylePainter::color(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background)));
        PDFStylePainter::icon(painter, alignedCentered(circle, QSizeF(Helper::RECENT_ICON, Helper::RECENT_ICON)), isFolder ? QStringLiteral("folder") : QStringLiteral("layers"),
                              Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)));
    }

    // From the trailing edge: the menu, the star, the time, the progress
    const QRectF moreRect = getRecentItemRowMoreRect(rect, state);
    const QRectF starRect = getRecentItemRowStarRect(rect, state);
    if (row.paintNested)
    {
        // The menu is icon/default under the pointer of the row or of itself
        QStyle::State moreFlags = state.flags & (QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken);
        if (state.isEnabled())
        {
            moreFlags |= row.moreFlags & (QStyle::State_MouseOver | QStyle::State_Sunken);
        }
        paintMoreButton(painter, moreRect, State(moreFlags, state.direction));
        if (row.starVisible)
        {
            paintStar(painter, starRect, StarSize::Small, Helper::nestedState(state, row.starFlags, row.pinned));
        }
    }

    const qreal openedLeft = rect.right() - Helper::RECENT_RIGHT - Helper::RECENT_MORE - 2 * Helper::RECENT_GAP - Helper::RECENT_STAR - Helper::RECENT_OPENED;
    Helper::drawText(painter, state, rect, Helper::line(rect, openedLeft, Helper::RECENT_OPENED, Helper::LINE_12), row.opened, Helper::font(PDFThemeFonts::Role::Small),
                     Helper::text(state, Color::TextTertiary), Helper::TextKind::Interface, true);

    const qreal progressLeft = openedLeft - Helper::RECENT_GAP - Helper::RECENT_PROGRESS;
    if (isMissing)
    {
        // A missing item: the badge "No longer exists" in the column of the progress
        const QSize badgeSize = getMissingFileBadgeSize();
        const QRectF badge(progressLeft, rect.top() + qFloor((rect.height() - badgeSize.height()) / 2.0), qMin<qreal>(badgeSize.width(), Helper::RECENT_PROGRESS), badgeSize.height());
        paintMissingFileBadge(painter, state.visualRect(rect, badge), state);
    }
    else
    {
        const bool hasBar = type == RecentItemType::File && row.progress >= 0.0 && qIsFinite(row.progress);
        const qreal progressHeight = Helper::LINE_11 + (hasBar ? Helper::RECENT_PROGRESS_GAP + Helper::RECENT_BAR_HEIGHT : 0);
        const qreal progressTop = rect.top() + qFloor((rect.height() - progressHeight) / 2.0);
        Helper::drawText(painter, state, rect, QRectF(progressLeft, progressTop, Helper::RECENT_PROGRESS, Helper::LINE_11), row.progressText,
                         Helper::font(PDFThemeFonts::Role::CaptionRegular), Helper::text(state, Color::TextTertiary), Helper::TextKind::Interface);
        if (hasBar)
        {
            const QRectF bar(progressLeft, progressTop + Helper::LINE_11 + Helper::RECENT_PROGRESS_GAP, Helper::RECENT_BAR_WIDTH, Helper::RECENT_BAR_HEIGHT);
            paintProgressBar(painter, state.visualRect(rect, bar), row.progress, Helper::nestedState(state, QStyle::State_None));
        }
    }

    // The name (of a file elided in the middle) and the details are data
    const qreal textLeft = leading.right() + Helper::RECENT_GAP;
    Helper::drawTextBlock(painter, state, rect, textLeft, progressLeft - Helper::RECENT_GAP - textLeft, Helper::RECENT_TEXT_GAP,
                          row.name, Helper::text(state, isMissing ? Color::TextMuted : Color::TextPrimary), type == RecentItemType::File ? Helper::TextKind::FileName : Helper::TextKind::Data,
                          row.details, Helper::text(state, Color::TextMuted), Helper::TextKind::Data);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

QString PDFComponentPainter::getAddShortcutText()
{
    return QCoreApplication::translate("PDFComponentPainter", "Add shortcut");
}

QString PDFComponentPainter::getRecordShortcutText()
{
    return QCoreApplication::translate("PDFComponentPainter", "Press keys…");
}

QSize PDFComponentPainter::getShortcutRowSize(const ShortcutRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    return QSize(qMax(Helper::SHORTCUT_WIDTH, getShortcutRowMinimumSize(row).width()), Helper::SHORTCUT_HEIGHT);
}

QSize PDFComponentPainter::getShortcutRowMinimumSize(const ShortcutRow& row)
{
    using Helper = PDFComponentDelegatePainterHelper;
    const int capWidth = qMin(Helper::getShortcutCapWidth(row), Helper::MAX_EXTENT);
    return QSize(2 * Helper::SHORTCUT_SIDE + Helper::SHORTCUT_ICON + 2 * Helper::SHORTCUT_GAP + Helper::MIN_TEXT + capWidth, Helper::SHORTCUT_HEIGHT);
}

QRectF PDFComponentPainter::getShortcutRowKeyRect(const QRectF& rect, const ShortcutRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    // The cap ends 10 px before the trailing edge; it is never wider than the
    // room after the icon and a part of the name
    const qreal room = rect.width() - 2 * Helper::SHORTCUT_SIDE - Helper::SHORTCUT_ICON - 2 * Helper::SHORTCUT_GAP - Helper::MIN_TEXT;
    const qreal width = qMax<qreal>(0.0, qMin<qreal>(Helper::getShortcutCapWidth(row), room));
    return state.visualRect(rect, Helper::line(rect, rect.right() - Helper::SHORTCUT_SIDE - width, width, Helper::SHORTCUT_CAP_HEIGHT));
}

void PDFComponentPainter::paintShortcutRow(QPainter* painter, const QRectF& rect, const ShortcutRow& row, const State& state)
{
    using Helper = PDFComponentDelegatePainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const Helper::Radii radii(Helper::ROW_RADIUS);
    const bool isRecording = row.recording && state.isEnabled();

    // The recording row is drawn as Default: the ring belongs to the cap
    const State rowState = isRecording ? State(state.flags & QStyle::State_Enabled, state.direction) : state;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillRow(painter, rect, radii, rowState, false);

    const QRectF iconRect = Helper::line(rect, rect.left() + Helper::SHORTCUT_SIDE, Helper::SHORTCUT_ICON, Helper::SHORTCUT_ICON);
    if (!row.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, state.visualRect(rect, iconRect), row.iconName, Helper::icon(state, Color::IconMuted));
    }

    // The key cap, the placeholder of an action without a shortcut, or the recorded cap
    const QRectF keyRect = getShortcutRowKeyRect(rect, row, state);
    if (!keyRect.isEmpty())
    {
        const Helper::Radii capRadii(Helper::SHORTCUT_CAP_RADIUS);
        const QFont capFont = Helper::font(PDFThemeFonts::Role::SmallMedium);
        const QRectF capText = keyRect.adjusted(Helper::SHORTCUT_CAP_SIDE, 0.0, -Helper::SHORTCUT_CAP_SIDE, 0.0);

        if (isRecording)
        {
            PDFStylePainter::fill(painter, keyRect, capRadii, Color::BgSubtle);
            paintFocusRingInside(painter, keyRect, capRadii);
            PDFStylePainter::text(painter, capText, Qt::AlignCenter | Qt::TextSingleLine, elidedText(getRecordShortcutText(), capFont, capText.width()), capFont,
                                  PDFStylePainter::color(Color::TextAccent));
        }
        else if (!row.shortcut.isEmpty())
        {
            PDFStylePainter::fill(painter, keyRect, capRadii, Color::BgSubtle);
            PDFStylePainter::strokeInside(painter, keyRect, capRadii, Color::BorderDefault, 1.0);
            drawDataText(painter, capText, Qt::AlignCenter | Qt::TextSingleLine, elidedText(row.shortcut, capFont, capText.width()), capFont,
                         PDFStylePainter::color(Helper::text(state, Color::TextSecondary)), getDataTextDirection(row.shortcut));
        }
        else
        {
            // Dashed border of 1 px inside, dashes 3 / 3, no fill
            {
                const PDFStylePainterGuard innerGuard(painter);
                QPen pen(PDFStylePainter::color(Color::BorderStrong), 1.0, Qt::CustomDashLine, Qt::FlatCap, Qt::MiterJoin);
                pen.setDashPattern({ Helper::SHORTCUT_DASH, Helper::SHORTCUT_DASH });
                painter->setPen(pen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(PDFStylePainter::roundedPath(keyRect.adjusted(0.5, 0.5, -0.5, -0.5), capRadii.adjusted(0.5)));
            }
            PDFStylePainter::text(painter, capText, Qt::AlignCenter | Qt::TextSingleLine, elidedText(getAddShortcutText(), capFont, capText.width()), capFont,
                                  PDFStylePainter::color(Helper::text(state, Color::TextMuted)));
        }
    }

    // The name of the action ends 12 px before the visible cap
    const QRectF logicalKey = state.visualRect(rect, keyRect);
    const qreal textLeft = iconRect.right() + Helper::SHORTCUT_GAP;
    const qreal textRight = keyRect.isEmpty() ? rect.right() - Helper::SHORTCUT_SIDE : logicalKey.left() - Helper::SHORTCUT_GAP;
    Helper::drawText(painter, state, rect, Helper::line(rect, textLeft, textRight - textLeft, Helper::LINE_13), row.action, Helper::font(PDFThemeFonts::Role::Body),
                     Helper::text(state, Color::TextPrimary), Helper::TextKind::Interface);

    if (rowState.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
}

}   // namespace pdf
