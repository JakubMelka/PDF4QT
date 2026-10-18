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

// Drawing of the composite widgets of PDFComponentPainter (step 4 of
// components.md, chapter 6.3): Page selection, Continue with, Color button,
// Color palette popover, Signature card and Strength meter. The class is
// declared in pdfcomponentpainter.h; this file holds the geometry constants of
// the cards of the specification of these components.

#include "pdfcomponentpainter.h"
#include "pdfthemefonts.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <limits>

namespace pdf
{

/// Geometry constants of the composite widgets and their drawing helpers
class PDFComponentPanelPainterHelper
{
public:
    PDFComponentPanelPainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using State = PDFComponentPainter::State;
    using Role = PDFAccentHueColors::Role;

    /// Largest width or height of a layout (a larger value is bounded, so the
    /// sums of the layouts never overflow)
    static constexpr int MAX_EXTENT = 1 << 20;

    // Lines of the texts (automatic line heights of Inter on whole pixels, decision 30)
    static constexpr int LINE_SEMIBOLD_13 = 16;
    static constexpr int LINE_13 = 16;
    static constexpr int LINE_12 = 15;
    static constexpr int LINE_11 = 13;

    // Page selection 265:985: 320 wide, gaps of 10; header 15 (label Semi Bold 12,
    // count Medium 11), modes 32, range field 34, filters: caption 13, gap 8,
    // two rows of check boxes 18 with a gap of 8 in two columns with a gap of 8,
    // status 14 (icon 14, gap 6, Medium 11), error row 14 (Regular 11, wraps)
    static constexpr int SELECTION_WIDTH = 320;
    static constexpr int SELECTION_GAP = 10;
    static constexpr int SELECTION_HEADER = 15;
    static constexpr int SELECTION_HEADER_GAP = 8;
    static constexpr int SELECTION_MODES = 32;
    static constexpr int SELECTION_RANGE = 34;
    static constexpr int SELECTION_FILTERS_LABEL = 13;
    static constexpr int SELECTION_FILTERS_GAP = 8;
    static constexpr int SELECTION_FILTER = 18;
    static constexpr int SELECTION_STATUS = 14;
    static constexpr int SELECTION_STATUS_ICON = 14;
    static constexpr int SELECTION_STATUS_GAP = 6;
    static constexpr int SELECTION_SEGMENT_MIN_PADDING = 6;
    static constexpr int SELECTION_CHECK_INDICATOR = 18;
    static constexpr int SELECTION_CHECK_GAP = 8;

    // Continue with 417:1007: 480 wide, gaps of 8; header 16 (title Semi Bold 12,
    // note Regular 11 after a gap of 6, link at the trailing edge), choices with
    // gaps of 8 as high as the tallest one (46 for names of one line, 48 for two
    // lines: 418:32221, 417:1007), caption Regular 11 (wraps)
    static constexpr int CONTINUE_WIDTH = 480;
    static constexpr int CONTINUE_GAP = 8;
    static constexpr int CONTINUE_HEADER = 16;
    static constexpr int CONTINUE_HEADER_GAP = 6;
    static constexpr int CONTINUE_CHOICE_GAP = 8;
    static constexpr int CONTINUE_MAX_CHOICES = 3;
    static constexpr int CONTINUE_MIN_TITLE = 40;

    // Color button 156:4239: 260 x 34, radius 8, border 1 counted in the layout,
    // padding 8 / 10 (the content 9 and 11 px from the edges, also with the
    // focus ring of 2 px and the padding 7 / 9), swatch 20 (radius 5), gaps of
    // 10, name Regular 13 (186 wide), chevron 14
    static constexpr int BUTTON_WIDTH = 260;
    static constexpr int BUTTON_HEIGHT = 34;
    static constexpr qreal BUTTON_RADIUS = 8.0;
    static constexpr int BUTTON_LEFT = 9;
    static constexpr int BUTTON_RIGHT = 11;
    static constexpr int BUTTON_SWATCH = 20;
    static constexpr qreal BUTTON_SWATCH_RADIUS = 5.0;
    static constexpr int BUTTON_GAP = 10;
    static constexpr int BUTTON_CHEVRON = 14;
    static constexpr int BUTTON_MIN_NAME = 40;

    // Color palette popover 156:4240: 232 wide, padding 12 (the border is not
    // counted), gaps of 10, labels Medium 11, swatches 28 six in a row with gaps
    // of 8 (inner width 208), divider 1, link 16, radius 12
    static constexpr int PALETTE_WIDTH = 232;
    static constexpr int PALETTE_PADDING = 12;
    static constexpr int PALETTE_GAP = 10;
    static constexpr int PALETTE_SWATCH = 28;
    static constexpr int PALETTE_SWATCH_GAP = 8;
    static constexpr int PALETTE_COLUMNS = 6;
    static constexpr int PALETTE_LINK = 16;
    static constexpr qreal PALETTE_RADIUS = 12.0;

    // Signature card 192:1032: 276 wide, border 1 counted, padding 12, gaps of
    // 10, radius 12; header 32 (badge 32 with an icon 16, gap 10, signer Semi
    // Bold 13 and meta Regular 11 with a gap of 1, the trailing padding 24 for
    // the toggle 24, which lies 8 px from the edge of the card); status tag 19
    // (padding 2 / 8, border 1, Semi Bold 11); summary Regular 12; details: key
    // 92 Regular 11, gap 8, value Medium 11, rows with gaps of 6; caption Medium
    // 11; chain: rail 8 (dot 8 at 4, the line from 15), gap 10, name Semi Bold
    // 12, info Regular 11 with a gap of 1, the link after a gap of 1 + 4, the
    // bottom padding 10 of all but the last certificate
    static constexpr int CARD_WIDTH = 276;
    static constexpr int CARD_INSET = 13;
    static constexpr qreal CARD_RADIUS = 12.0;
    static constexpr int CARD_GAP = 10;
    static constexpr int CARD_HEADER = 32;
    static constexpr int CARD_BADGE = 32;
    static constexpr int CARD_BADGE_ICON = 16;
    static constexpr int CARD_HEADER_TRAILING = 24;
    static constexpr int CARD_TOGGLE = 24;
    static constexpr int CARD_TOGGLE_ICON = 14;
    static constexpr int CARD_TOGGLE_EDGE = 8;
    static constexpr int CARD_TEXT_GAP = 1;
    static constexpr int CARD_PILL_HEIGHT = 19;
    static constexpr int CARD_PILL_SIDE = 9;
    static constexpr int CARD_KEY = 92;
    static constexpr int CARD_KEY_GAP = 8;
    static constexpr int CARD_DETAIL_GAP = 6;
    static constexpr int CARD_RAIL = 8;
    static constexpr int CARD_RAIL_GAP = 10;
    static constexpr int CARD_DOT = 8;
    static constexpr int CARD_DOT_TOP = 4;
    static constexpr int CARD_LINE_TOP = 15;
    static constexpr int CARD_LINE_LEFT = 3;    ///< The line of 1 px lies on the whole pixel 3 of the rail of 8 (Figma 3.5, centered across two pixels; a crisp line is kept, review F8)
    static constexpr int CARD_CERTIFICATE_BOTTOM = 10;
    static constexpr int CARD_LINK_TOP = 4;
    static constexpr int CARD_LINK = 16;
    static constexpr int CARD_MIN_TEXT = 60;
    static constexpr int CARD_MIN_VALUE = 40;

    // Strength meter 455:1270: 300 wide, gaps of 6; top 15 (level Semi Bold 12,
    // detail Regular 11), track 6 (radius 3) of 20 cells, criteria 17 high
    // (padding 2, 8, 2, 6, mark 12, gap 4, Medium 11), wrapped with gaps of 4
    static constexpr int METER_WIDTH = 300;
    static constexpr int METER_GAP = 6;
    static constexpr int METER_TOP_GAP = 8;     ///< Gap between the level and the detail (top gap8)
    static constexpr int METER_TOP = 15;
    static constexpr int METER_TRACK = 6;
    static constexpr qreal METER_TRACK_RADIUS = 3.0;
    static constexpr int METER_CELLS = 20;
    static constexpr int METER_CRITERION = 17;
    static constexpr int METER_CRITERION_LEFT = 6;
    static constexpr int METER_CRITERION_RIGHT = 8;
    static constexpr int METER_CRITERION_GAP = 4;
    static constexpr int METER_MARK = 12;
    static constexpr int METER_DOT = 4;
    static constexpr int METER_CRITERIA_GAP = 4;
    static constexpr qreal METER_CRITERION_RADIUS = 10.0;
    static constexpr int METER_MIN_WIDTH = 120;

    static QFont font(PDFThemeFonts::Role role) { return PDFThemeFonts::getFont(role); }

    static int textWidth(const QString& text, const QFont& font)
    {
        return text.isEmpty() ? 0 : qMin(MAX_EXTENT, qCeil(PDFStylePainter::textWidth(text, font)));
    }

    /// Returns the width bounded to 0 to MAX_EXTENT
    static int validWidth(int width) { return qBound(0, width, MAX_EXTENT); }

    static bool isDrawable(const QRectF& rect)
    {
        return !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height()) &&
               rect.width() <= MAX_EXTENT && rect.height() <= MAX_EXTENT;
    }

    /// Returns the color of a text (text/disabled for a disabled component)
    static Color text(const State& state, Color color) { return state.isEnabled() ? color : Color::TextDisabled; }

    /// Returns the color of an icon (icon/disabled for a disabled component)
    static Color icon(const State& state, Color color) { return state.isEnabled() ? color : Color::IconDisabled; }

    /// Returns the rectangle of the layout of a component (its origin is the top
    /// left corner of the component) in the rectangle of the component, mirrored
    /// for the right-to-left layout
    static QRectF place(const QRectF& outer, const QRectF& local, const State& state)
    {
        return state.visualRect(outer, local.translated(outer.topLeft()));
    }

    /// Draws a text of a single line, elided at its end, into the local rectangle
    /// (a line of the height of the rectangle, the text centered vertically in it)
    static void drawText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& local, const QString& text,
                         const QFont& font, Color color, bool isTrailing = false)
    {
        if (local.width() > 0.0 && local.height() > 0.0)
        {
            PDFComponentPainter::drawElidedText(painter, state, place(outer, local, state), text, font, color, PDFComponentPainter::TextKind::Interface, isTrailing);
        }
    }

    /// Draws a text of data (a name, a value) of a single line, elided at its end,
    /// into the local rectangle: its base direction is \p direction (its own
    /// direction for Qt::LayoutDirectionAuto, components.md 6.6)
    static void drawDataText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& local, const QString& text,
                             const QFont& font, Color color, Qt::LayoutDirection direction = Qt::LayoutDirectionAuto)
    {
        if (local.width() <= 0.0 || local.height() <= 0.0 || text.isEmpty())
        {
            return;
        }

        if (direction != Qt::LeftToRight && direction != Qt::RightToLeft)
        {
            // The direction of the whole text (the elision may cut its first strong character off)
            PDFComponentPainter::drawElidedText(painter, state, place(outer, local, state), text, font, color, PDFComponentPainter::TextKind::Data);
            return;
        }

        // A line of a wrapped text has the direction of the whole wrapped text
        const int alignment = Qt::AlignVCenter | Qt::TextSingleLine | state.leadingAlignment();
        PDFComponentPainter::drawDataText(painter, place(outer, local, state), alignment, PDFComponentPainter::elidedText(text, font, local.width()), font,
                                          PDFStylePainter::color(color), direction);
    }

    /// Draws the lines from the top of the local rectangle, one line of the height each
    static void drawLines(QPainter* painter, const State& state, const QRectF& outer, const QRectF& local, int lineHeight,
                          const QStringList& lines, const QFont& font, Color color)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            drawText(painter, state, outer, QRectF(local.left(), local.top() + i * lineHeight, local.width(), lineHeight), lines.at(i), font, color);
        }
    }

    /// Draws the lines of a wrapped text of data: they share the direction of the whole text
    static void drawDataLines(QPainter* painter, const State& state, const QRectF& outer, const QRectF& local, int lineHeight,
                              const QStringList& lines, const QFont& font, Color color, Qt::LayoutDirection direction)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            drawDataText(painter, state, outer, QRectF(local.left(), local.top() + i * lineHeight, local.width(), lineHeight), lines.at(i), font, color, direction);
        }
    }

    /// Draws an icon into the local rectangle
    static void drawIcon(QPainter* painter, const State& state, const QRectF& outer, const QRectF& local, const QString& name, Color color)
    {
        if (!name.isEmpty() && !local.isEmpty())
        {
            PDFStylePainter::icon(painter, place(outer, local, state), name, color);
        }
    }

    /// Returns the local rectangle of a line of the height centered vertically in the band
    static QRectF line(qreal left, qreal bandTop, qreal bandHeight, qreal width, qreal height)
    {
        return QRectF(left, bandTop + qFloor((bandHeight - height) / 2.0), qMax<qreal>(0.0, width), height);
    }

    /// Returns the state of a nested element drawn by a composite widget:
    /// enabled as the widget, in its direction, checked when \p isOn
    static State nestedState(const State& state, bool isOn)
    {
        QStyle::State flags = state.isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
        if (isOn)
        {
            flags |= QStyle::State_On;
        }
        return State(flags, state.direction);
    }

    /// Returns the hue of the status of a signature
    static PDFAccentHue getStatusHue(PDFComponentPainter::SignatureStatus status)
    {
        switch (PDFComponentPainter::normalized(status))
        {
            case PDFComponentPainter::SignatureStatus::Valid:
                return PDFAccentHue::Green;
            case PDFComponentPainter::SignatureStatus::Warning:
                return PDFAccentHue::Amber;
            case PDFComponentPainter::SignatureStatus::Invalid:
                return PDFAccentHue::Red;
        }

        return PDFAccentHue::Green;
    }

    /// Returns the icon of the status of a signature
    static QString getStatusIcon(PDFComponentPainter::SignatureStatus status)
    {
        switch (PDFComponentPainter::normalized(status))
        {
            case PDFComponentPainter::SignatureStatus::Valid:
                return QStringLiteral("shield-check");
            case PDFComponentPainter::SignatureStatus::Warning:
                return QStringLiteral("alert-triangle");
            case PDFComponentPainter::SignatureStatus::Invalid:
                return QStringLiteral("x-circle");
        }

        return QStringLiteral("shield-check");
    }

    /// Returns the shown choices of Continue with (at most three)
    static int getChoiceCount(const PDFComponentPainter::ContinueWith& content)
    {
        return qMin(static_cast<int>(content.choices.size()), CONTINUE_MAX_CHOICES);
    }

    /// Returns the left edges of the items of the count, which divide the
    /// width equally (the gaps between them are \p gap); the last item ends at
    /// the width. The widths differ by one pixel at most.
    static QList<QRect> divide(int left, int top, int width, int height, int count, int gap)
    {
        QList<QRect> rects;
        if (count <= 0)
        {
            return rects;
        }

        const qint64 total = static_cast<qint64>(qMax(0, width)) + gap;
        for (int i = 0; i < count; ++i)
        {
            const int start = static_cast<int>(total * i / count);
            const int end = static_cast<int>(total * (i + 1) / count) - gap;
            rects << QRect(left + start, top, qMax(0, end - start), height);
        }
        return rects;
    }

    /// Lines of a detail of a signature card
    struct DetailLines
    {
        QStringList key;
        QStringList value;
        int height = 0;
    };

    static DetailLines layoutDetail(const PDFComponentPainter::SignatureDetail& detail, int innerWidth)
    {
        DetailLines lines;
        const int valueWidth = qMax(0, innerWidth - CARD_KEY - CARD_KEY_GAP);
        lines.key = PDFComponentPainter::wrapText(detail.key, font(PDFThemeFonts::Role::CaptionRegular), CARD_KEY);
        lines.value = PDFComponentPainter::wrapText(detail.value, font(PDFThemeFonts::Role::Caption), valueWidth);
        lines.height = qMax(1, static_cast<int>(qMax(lines.key.size(), lines.value.size()))) * LINE_11;
        return lines;
    }

    /// Lines and the height of a certificate of a signature card
    struct CertificateLines
    {
        QStringList info;
        int height = 0;
        int linkTop = -1;   ///< Top of the link relative to the certificate, -1 without a link
    };

    static CertificateLines layoutCertificate(const PDFComponentPainter::SignatureCertificate& certificate, int innerWidth, bool isLast)
    {
        CertificateLines lines;
        const int textWidth = qMax(0, innerWidth - CARD_RAIL - CARD_RAIL_GAP);
        lines.info = PDFComponentPainter::wrapText(certificate.info, font(PDFThemeFonts::Role::CaptionRegular), textWidth);

        int height = LINE_12;
        if (!lines.info.isEmpty())
        {
            height += CARD_TEXT_GAP + static_cast<int>(lines.info.size()) * LINE_11;
        }
        if (!certificate.linkText.isEmpty())
        {
            lines.linkTop = height + CARD_TEXT_GAP + CARD_LINK_TOP;
            height = lines.linkTop + CARD_LINK;
        }
        if (!isLast)
        {
            height += CARD_CERTIFICATE_BOTTOM;
        }

        lines.height = height;
        return lines;
    }

    /// Returns the width of a criterion of a strength meter
    static int getCriterionWidth(const QString& text)
    {
        return METER_CRITERION_LEFT + METER_MARK + METER_CRITERION_GAP + textWidth(text, font(PDFThemeFonts::Role::Caption)) + METER_CRITERION_RIGHT;
    }

    /// Returns the color of the level of a strength meter
    static Color getLevelColor(PDFComponentPainter::StrengthLevel level)
    {
        switch (PDFComponentPainter::normalized(level))
        {
            case PDFComponentPainter::StrengthLevel::Empty:
                return Color::TextTertiary;
            case PDFComponentPainter::StrengthLevel::VeryWeak:
            case PDFComponentPainter::StrengthLevel::Weak:
                return Color::AccentRedIcon;
            case PDFComponentPainter::StrengthLevel::Moderate:
                return Color::AccentAmberIcon;
            case PDFComponentPainter::StrengthLevel::Strong:
            case PDFComponentPainter::StrengthLevel::VeryStrong:
                return Color::AccentGreenIcon;
        }

        return Color::TextTertiary;
    }

    /// Criteria of a strength meter in their order
    static constexpr PDFComponentPainter::StrengthCriterion CRITERIA[] =
    {
        PDFComponentPainter::StrengthCriterion::Length,
        PDFComponentPainter::StrengthCriterion::Case,
        PDFComponentPainter::StrengthCriterion::Digits,
        PDFComponentPainter::StrengthCriterion::Symbols
    };

    /// Adds a section of a color palette (the label and the grid of the
    /// swatches) at the position \p y and moves it after the section
    static void addPaletteSection(const QList<QColor>& colors, int& y, QRect& label, QList<QRect>& swatches)
    {
        if (colors.isEmpty())
        {
            return;
        }

        const int padding = PALETTE_PADDING;
        const int innerWidth = PALETTE_WIDTH - 2 * padding;
        const int step = PALETTE_SWATCH + PALETTE_SWATCH_GAP;

        label = QRect(padding, y, innerWidth, LINE_11);
        y += LINE_11 + PALETTE_GAP;

        const int count = static_cast<int>(qMin<qsizetype>(colors.size(), 4096));
        for (int i = 0; i < count; ++i)
        {
            swatches << QRect(padding + (i % PALETTE_COLUMNS) * step, y + (i / PALETTE_COLUMNS) * step, PALETTE_SWATCH, PALETTE_SWATCH);
        }

        const int rows = (count + PALETTE_COLUMNS - 1) / PALETTE_COLUMNS;
        y += rows * step - PALETTE_SWATCH_GAP + PALETTE_GAP;
    }

    /// Paints the swatches of a section of a color palette, the swatch \p selected is checked
    static void paintPaletteSwatches(QPainter* painter, const QRectF& rect, const QList<QColor>& colors, const QList<QRect>& rects, int selected, const State& state)
    {
        for (int i = 0; i < rects.size() && i < colors.size(); ++i)
        {
            const QColor& color = colors.at(i);
            const PDFComponentPainter::ColorSwatch swatch{ color, color.isValid() ? PDFComponentPainter::SwatchKind::Color : PDFComponentPainter::SwatchKind::None };
            PDFComponentPainter::paintColorSwatch(painter, place(rect, QRectF(rects.at(i)), state), swatch, nestedState(state, i == selected));
        }
    }
};

// ------------------------------------------------------------ Page selection

PDFComponentPainter::PageSelectionLayout PDFComponentPainter::getPageSelectionLayout(const PageSelection& selection, int width)
{
    using Helper = PDFComponentPanelPainterHelper;

    PageSelectionLayout layout;
    width = Helper::validWidth(width);

    int y = 0;
    layout.header = QRect(0, y, width, Helper::SELECTION_HEADER);
    y += Helper::SELECTION_HEADER + Helper::SELECTION_GAP;

    layout.modes = QRect(0, y, width, Helper::SELECTION_MODES);
    y += Helper::SELECTION_MODES;

    if (selection.rangeVisible)
    {
        y += Helper::SELECTION_GAP;
        layout.range = QRect(0, y, width, Helper::SELECTION_RANGE);
        y += Helper::SELECTION_RANGE;
    }

    y += Helper::SELECTION_GAP;
    layout.filtersLabel = QRect(0, y, width, Helper::SELECTION_FILTERS_LABEL);
    y += Helper::SELECTION_FILTERS_LABEL + Helper::SELECTION_FILTERS_GAP;

    for (int row = 0; row < 2; ++row)
    {
        const QList<QRect> columns = Helper::divide(0, y, width, Helper::SELECTION_FILTER, 2, Helper::SELECTION_FILTERS_GAP);
        layout.filters[2 * row] = columns.value(0);
        layout.filters[2 * row + 1] = columns.value(1);
        y += Helper::SELECTION_FILTER + (row == 0 ? Helper::SELECTION_FILTERS_GAP : 0);
    }

    y += Helper::SELECTION_GAP;
    layout.status = QRect(0, y, width, Helper::SELECTION_STATUS);
    y += Helper::SELECTION_STATUS;

    if (!selection.error.isEmpty())
    {
        // The message wraps beside the icon, the row is at least as high as the icon
        const int textWidth = width - Helper::SELECTION_STATUS_ICON - Helper::SELECTION_STATUS_GAP;
        const QStringList lines = wrapText(selection.error, Helper::font(PDFThemeFonts::Role::CaptionRegular), textWidth);
        const int height = qMax(Helper::SELECTION_STATUS, static_cast<int>(qMin<qsizetype>(lines.size(), 1000)) * Helper::LINE_11);

        y += Helper::SELECTION_GAP;
        layout.error = QRect(0, y, width, height);
        y += height;
    }

    layout.height = y;
    return layout;
}

int PDFComponentPainter::getPageSelectionHeight(const PageSelection& selection, int width)
{
    return getPageSelectionLayout(selection, width).height;
}

QSize PDFComponentPainter::getPageSelectionSize(const PageSelection& selection)
{
    using Helper = PDFComponentPanelPainterHelper;
    const int width = qMax(Helper::SELECTION_WIDTH, getPageSelectionMinimumSize(selection).width());
    return QSize(width, getPageSelectionHeight(selection, width));
}

QSize PDFComponentPainter::getPageSelectionMinimumSize(const PageSelection& selection)
{
    using Helper = PDFComponentPanelPainterHelper;

    // Segments of equal width with the padding of 6 at least
    int segment = 0;
    for (const QString& label : selection.modeLabels)
    {
        segment = qMax(segment, Helper::textWidth(label, Helper::font(PDFThemeFonts::Role::CaptionSemiBold)));
    }
    const int modes = static_cast<int>(qMin<qsizetype>(selection.modeLabels.size(), 16)) * (segment + 2 * Helper::SELECTION_SEGMENT_MIN_PADDING);

    // Two columns of the check boxes
    int filter = 0;
    for (const QString& label : selection.filterLabels)
    {
        filter = qMax(filter, Helper::textWidth(label, Helper::font(PDFThemeFonts::Role::Body)));
    }
    const int filters = 2 * (Helper::SELECTION_CHECK_INDICATOR + Helper::SELECTION_CHECK_GAP + filter) + Helper::SELECTION_FILTERS_GAP;

    // The header: the label and the count of the pages
    const int header = Helper::textWidth(selection.label, Helper::font(PDFThemeFonts::Role::SmallSemiBold)) + Helper::SELECTION_HEADER_GAP +
                       Helper::textWidth(selection.total, Helper::font(PDFThemeFonts::Role::Caption));

    const int width = qMin(Helper::MAX_EXTENT, qMax(qMax(modes, filters), header));
    return QSize(width, getPageSelectionHeight(selection, width));
}

void PDFComponentPainter::paintPageSelection(QPainter* painter, const QRectF& rect, const PageSelection& selection, const State& state)
{
    using Helper = PDFComponentPanelPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PageSelectionLayout layout = getPageSelectionLayout(selection, qFloor(rect.width()));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Header: the label at the leading edge, the count of the pages at the trailing edge
    const QFont labelFont = Helper::font(PDFThemeFonts::Role::SmallSemiBold);
    const QFont totalFont = Helper::font(PDFThemeFonts::Role::Caption);
    const int totalWidth = qMin(Helper::textWidth(selection.total, totalFont), layout.header.width());
    const QRectF header(layout.header);
    Helper::drawText(painter, state, rect, Helper::line(header.left() + header.width() - totalWidth, header.top(), header.height(), totalWidth, Helper::LINE_11),
                     selection.total, totalFont, Helper::text(state, Color::TextTertiary), true);
    const qreal labelWidth = header.width() - (totalWidth > 0 ? totalWidth + Helper::SELECTION_HEADER_GAP : 0);
    Helper::drawText(painter, state, rect, Helper::line(header.left(), header.top(), header.height(), labelWidth, Helper::LINE_12),
                     selection.label, labelFont, Helper::text(state, Color::TextPrimary));

    // Caption of the filters
    Helper::drawText(painter, state, rect, QRectF(layout.filtersLabel), selection.filtersLabel, Helper::font(PDFThemeFonts::Role::Caption), Helper::text(state, Color::TextTertiary));

    // Status: the check is icon/muted also with an error
    const qreal textLeft = Helper::SELECTION_STATUS_ICON + Helper::SELECTION_STATUS_GAP;
    const QRectF status(layout.status);
    Helper::drawIcon(painter, state, rect, QRectF(status.left(), status.top(), Helper::SELECTION_STATUS_ICON, Helper::SELECTION_STATUS_ICON),
                     QStringLiteral("check-circle"), Helper::icon(state, Color::IconMuted));
    Helper::drawText(painter, state, rect, Helper::line(status.left() + textLeft, status.top(), Helper::SELECTION_STATUS, status.width() - textLeft, Helper::LINE_11),
                     selection.status, Helper::font(PDFThemeFonts::Role::Caption), Helper::text(state, Color::TextSecondary));

    // Error row: the message wraps beside the icon, which is centered on the
    // lines (the row is lay=H MIN/CENTER, instance 385:11956)
    if (!layout.error.isEmpty())
    {
        const QRectF error(layout.error);
        const QFont messageFont = Helper::font(PDFThemeFonts::Role::CaptionRegular);
        Helper::drawIcon(painter, state, rect, Helper::line(error.left(), error.top(), error.height(), Helper::SELECTION_STATUS_ICON, Helper::SELECTION_STATUS_ICON),
                         QStringLiteral("alert-circle"), Helper::icon(state, Color::IconDanger));
        const qreal messageWidth = error.width() - textLeft;
        const QStringList lines = wrapText(selection.error, messageFont, messageWidth);
        Helper::drawLines(painter, state, rect, QRectF(error.left() + textLeft, error.top(), messageWidth, error.height()), Helper::LINE_11, lines, messageFont, Helper::text(state, Color::TextDanger));
    }

    painter->restore();
}

// ------------------------------------------------------------- Continue with

int PDFComponentPainter::getContinueWithMaxChoices()
{
    return PDFComponentPanelPainterHelper::CONTINUE_MAX_CHOICES;
}

PDFComponentPainter::ContinueWithLayout PDFComponentPainter::getContinueWithLayout(const ContinueWith& content, int width)
{
    using Helper = PDFComponentPanelPainterHelper;

    ContinueWithLayout layout;
    width = Helper::validWidth(width);

    // Header: the link at the trailing edge, the title and the note at the leading edge
    const int linkWidth = content.linkText.isEmpty() ? 0 : qMin(width, getTextLinkSize(TextLink{ content.linkText }).width());
    if (linkWidth > 0)
    {
        layout.link = QRect(width - linkWidth, 0, linkWidth, Helper::CONTINUE_HEADER);
    }

    const int available = qMax(0, width - (linkWidth > 0 ? linkWidth + 2 * Helper::CONTINUE_HEADER_GAP : 0));
    const int titleWidth = qMin(available, Helper::textWidth(content.title, Helper::font(PDFThemeFonts::Role::SmallSemiBold)));
    layout.title = QRect(0, 0, titleWidth, Helper::CONTINUE_HEADER);

    const int noteLeft = titleWidth > 0 ? titleWidth + Helper::CONTINUE_HEADER_GAP : 0;
    const int noteWidth = qMin(qMax(0, available - noteLeft), Helper::textWidth(content.note, Helper::font(PDFThemeFonts::Role::CaptionRegular)));
    if (noteWidth > 0)
    {
        layout.note = QRect(noteLeft, 0, noteWidth, Helper::CONTINUE_HEADER);
    }

    int y = Helper::CONTINUE_HEADER;

    // Choices divide the width equally
    const int count = Helper::getChoiceCount(content);
    if (count > 0)
    {
        // The row is as high as the tallest choice at its width (the choices fill the row)
        y += Helper::CONTINUE_GAP;
        layout.choices = Helper::divide(0, y, width, 0, count, Helper::CONTINUE_CHOICE_GAP);
        int rowHeight = 0;
        for (int i = 0; i < layout.choices.size(); ++i)
        {
            rowHeight = qMax(rowHeight, getToolChoiceHeight(content.choices.at(i), layout.choices.at(i).width()));
        }
        for (QRect& choice : layout.choices)
        {
            choice.setHeight(rowHeight);
        }
        y += rowHeight;
    }

    // The caption wraps
    const QStringList lines = wrapText(content.caption, Helper::font(PDFThemeFonts::Role::CaptionRegular), width);
    if (!lines.isEmpty())
    {
        y += Helper::CONTINUE_GAP;
        const int height = static_cast<int>(qMin<qsizetype>(lines.size(), 1000)) * Helper::LINE_11;
        layout.caption = QRect(0, y, width, height);
        y += height;
    }

    layout.height = y;
    return layout;
}

int PDFComponentPainter::getContinueWithHeight(const ContinueWith& content, int width)
{
    return getContinueWithLayout(content, width).height;
}

QSize PDFComponentPainter::getContinueWithSize(const ContinueWith& content)
{
    using Helper = PDFComponentPanelPainterHelper;
    const int width = qMax(Helper::CONTINUE_WIDTH, getContinueWithMinimumSize(content).width());
    return QSize(width, getContinueWithHeight(content, width));
}

QSize PDFComponentPainter::getContinueWithMinimumSize(const ContinueWith& content)
{
    using Helper = PDFComponentPanelPainterHelper;

    const int count = Helper::getChoiceCount(content);
    int choices = 0;
    for (int i = 0; i < count; ++i)
    {
        choices = qMax(choices, getToolChoiceMinimumSize(content.choices.at(i)).width());
    }
    choices = count > 0 ? count * choices + (count - 1) * Helper::CONTINUE_CHOICE_GAP : 0;

    const int linkWidth = content.linkText.isEmpty() ? 0 : getTextLinkSize(TextLink{ content.linkText }).width() + 2 * Helper::CONTINUE_HEADER_GAP;
    const int title = qMin(Helper::CONTINUE_MIN_TITLE, Helper::textWidth(content.title, Helper::font(PDFThemeFonts::Role::SmallSemiBold)));
    const int width = qMin(Helper::MAX_EXTENT, qMax(choices, linkWidth + title));
    return QSize(width, getContinueWithHeight(content, width));
}

void PDFComponentPainter::paintContinueWith(QPainter* painter, const QRectF& rect, const ContinueWith& content, const State& state)
{
    using Helper = PDFComponentPanelPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const ContinueWithLayout layout = getContinueWithLayout(content, qFloor(rect.width()));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    {
        const QRectF title(layout.title);
        Helper::drawText(painter, state, rect, Helper::line(title.left(), title.top(), title.height(), title.width(), Helper::LINE_12),
                         content.title, Helper::font(PDFThemeFonts::Role::SmallSemiBold), Helper::text(state, Color::TextPrimary));

        if (!layout.note.isEmpty())
        {
            const QRectF note(layout.note);
            Helper::drawText(painter, state, rect, Helper::line(note.left(), note.top(), note.height(), note.width(), Helper::LINE_11),
                             content.note, Helper::font(PDFThemeFonts::Role::CaptionRegular), Helper::text(state, Color::TextMuted));
        }

        if (!layout.caption.isEmpty())
        {
            const QFont captionFont = Helper::font(PDFThemeFonts::Role::CaptionRegular);
            const QStringList lines = wrapText(content.caption, captionFont, layout.caption.width());
            Helper::drawLines(painter, state, rect, QRectF(layout.caption), Helper::LINE_11, lines, captionFont, Helper::text(state, Color::TextMuted));
        }
    }

    if (content.paintNested)
    {
        if (!layout.link.isEmpty())
        {
            paintTextLink(painter, Helper::place(rect, QRectF(layout.link), state), TextLink{ content.linkText }, Helper::nestedState(state, false));
        }

        for (int i = 0; i < layout.choices.size(); ++i)
        {
            paintToolChoice(painter, Helper::place(rect, QRectF(layout.choices.at(i)), state), content.choices.at(i), Helper::nestedState(state, i == content.selectedChoice));
        }
    }

    painter->restore();
}

// -------------------------------------------------------------- Color button

QSize PDFComponentPainter::getColorButtonSize(const ColorButton& button)
{
    using Helper = PDFComponentPanelPainterHelper;
    return QSize(qMax(Helper::BUTTON_WIDTH, getColorButtonMinimumSize(button).width()), Helper::BUTTON_HEIGHT);
}

QSize PDFComponentPainter::getColorButtonMinimumSize(const ColorButton& button)
{
    Q_UNUSED(button);

    using Helper = PDFComponentPanelPainterHelper;
    const int width = Helper::BUTTON_LEFT + Helper::BUTTON_SWATCH + Helper::BUTTON_GAP + Helper::BUTTON_MIN_NAME + Helper::BUTTON_GAP + Helper::BUTTON_CHEVRON + Helper::BUTTON_RIGHT;
    return QSize(width, Helper::BUTTON_HEIGHT);
}

qreal PDFComponentPainter::getColorButtonRadius()
{
    return PDFComponentPanelPainterHelper::BUTTON_RADIUS;
}

void PDFComponentPainter::paintColorButton(QPainter* painter, const QRectF& rect, const ColorButton& button, const State& state)
{
    using Helper = PDFComponentPanelPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PDFStylePainter::Radii radii(qMin(Helper::BUTTON_RADIUS, qMin(rect.width(), rect.height()) / 2.0));
    const bool isPressed = state.isEnabled() && (state.isPressed() || button.open);
    const bool isHovered = state.isHovered();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Field: bg/surface, bg/subtle when pressed (or open), bg/muted when disabled
    Color fill = Color::BgSurface;
    if (!state.isEnabled())
    {
        fill = Color::BgMuted;
    }
    else if (isPressed)
    {
        fill = Color::BgSubtle;
    }
    PDFStylePainter::fill(painter, rect, radii, fill);

    // The focus ring of 2 px inside replaces the border
    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, (isPressed || isHovered) ? Color::BorderStrong : Color::BorderDefault, 1.0);
    }

    painter->save();
    painter->setClipRect(rect, Qt::IntersectClip);

    // Swatch: the color keeps its value in each state; no color is crossed
    const QRectF swatch = Helper::line(Helper::BUTTON_LEFT, 0.0, rect.height(), Helper::BUTTON_SWATCH, Helper::BUTTON_SWATCH);
    const QRectF visualSwatch = Helper::place(rect, swatch, state);
    const PDFStylePainter::Radii swatchRadii(Helper::BUTTON_SWATCH_RADIUS);
    if (button.color.isValid())
    {
        PDFStylePainter::fill(painter, visualSwatch, swatchRadii, button.color);
    }
    else
    {
        PDFStylePainter::fill(painter, visualSwatch, swatchRadii, Color::BgSurface);

        painter->save();
        painter->setClipPath(PDFStylePainter::roundedPath(visualSwatch, swatchRadii), Qt::IntersectClip);
        QPen pen(PDFStylePainter::color(Helper::icon(state, Color::IconDanger)), 1.5);
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const QRectF slash = visualSwatch.adjusted(3.0, 3.0, -3.0, -3.0);
        if (state.isRightToLeft())
        {
            painter->drawLine(slash.topLeft(), slash.bottomRight());
        }
        else
        {
            painter->drawLine(slash.bottomLeft(), slash.topRight());
        }
        painter->restore();
    }
    PDFStylePainter::strokeInside(painter, visualSwatch, swatchRadii, Color::BorderStrong, 1.0);

    // Chevron at the trailing edge, the name between them
    const qreal chevronLeft = rect.width() - Helper::BUTTON_RIGHT - Helper::BUTTON_CHEVRON;
    Helper::drawIcon(painter, state, rect, Helper::line(chevronLeft, 0.0, rect.height(), Helper::BUTTON_CHEVRON, Helper::BUTTON_CHEVRON),
                     QStringLiteral("chevron-down"), Helper::icon(state, Color::IconDefault));

    const qreal nameLeft = swatch.right() + Helper::BUTTON_GAP;
    Helper::drawDataText(painter, state, rect, Helper::line(nameLeft, 0.0, rect.height(), chevronLeft - Helper::BUTTON_GAP - nameLeft, Helper::LINE_13),
                         button.name, Helper::font(PDFThemeFonts::Role::Body), Helper::text(state, Color::TextPrimary));

    painter->restore();
    painter->restore();
}

// ----------------------------------------------------- Color palette popover

int PDFComponentPainter::getColorPaletteColumns()
{
    return PDFComponentPanelPainterHelper::PALETTE_COLUMNS;
}

qreal PDFComponentPainter::getColorPaletteRadius()
{
    return PDFComponentPanelPainterHelper::PALETTE_RADIUS;
}

PDFComponentPainter::ColorPaletteLayout PDFComponentPainter::getColorPaletteLayout(const ColorPalette& palette)
{
    using Helper = PDFComponentPanelPainterHelper;

    ColorPaletteLayout layout;
    const int padding = Helper::PALETTE_PADDING;
    const int innerWidth = Helper::PALETTE_WIDTH - 2 * padding;
    int y = padding;

    // A section is the label and the grid of the swatches
    Helper::addPaletteSection(palette.suggested, y, layout.suggestedLabel, layout.suggested);
    Helper::addPaletteSection(palette.recent, y, layout.recentLabel, layout.recent);

    // The divider separates the colors from the link
    if (!layout.suggested.isEmpty() || !layout.recent.isEmpty())
    {
        layout.divider = QRect(padding, y, innerWidth, 1);
        y += 1 + Helper::PALETTE_GAP;
    }

    const int linkWidth = palette.moreText.isEmpty() ? 0 : qMin(innerWidth, getTextLinkSize(TextLink{ palette.moreText }).width());
    if (linkWidth > 0)
    {
        layout.more = QRect(padding, y, linkWidth, Helper::PALETTE_LINK);
        y += Helper::PALETTE_LINK + padding;
    }
    else
    {
        // The last gap belongs to the padding
        y += padding - Helper::PALETTE_GAP;
    }

    layout.size = QSize(Helper::PALETTE_WIDTH, qMax(2 * padding, y));
    return layout;
}

QSize PDFComponentPainter::getColorPaletteSize(const ColorPalette& palette)
{
    return getColorPaletteLayout(palette).size;
}

void PDFComponentPainter::paintColorPalette(QPainter* painter, const QRectF& rect, const ColorPalette& palette, const State& state)
{
    using Helper = PDFComponentPanelPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const ColorPaletteLayout layout = getColorPaletteLayout(palette);
    const PDFStylePainter::Radii radii(qMin(Helper::PALETTE_RADIUS, qMin(rect.width(), rect.height()) / 2.0));

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Panel of the popover (its shadow belongs to the window, a popover window
    // of the style draws the whole panel by the style)
    if (palette.panelVisible)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
    }

    const QFont labelFont = Helper::font(PDFThemeFonts::Role::Caption);
    Helper::drawText(painter, state, rect, QRectF(layout.suggestedLabel), palette.suggestedLabel, labelFont, Helper::text(state, Color::TextMuted));
    Helper::drawText(painter, state, rect, QRectF(layout.recentLabel), palette.recentLabel, labelFont, Helper::text(state, Color::TextMuted));

    if (!layout.divider.isEmpty())
    {
        PDFStylePainter::fillRect(painter, Helper::place(rect, QRectF(layout.divider), state), Color::BorderDivider);
    }

    if (palette.paintNested)
    {
        Helper::paintPaletteSwatches(painter, rect, palette.suggested, layout.suggested, palette.selected, state);
        Helper::paintPaletteSwatches(painter, rect, palette.recent, layout.recent, -1, state);

        if (!layout.more.isEmpty())
        {
            paintTextLink(painter, Helper::place(rect, QRectF(layout.more), state), TextLink{ palette.moreText }, Helper::nestedState(state, false));
        }
    }

    painter->restore();
}

// ------------------------------------------------------------ Signature card

PDFComponentPainter::SignatureStatus PDFComponentPainter::normalized(SignatureStatus status)
{
    switch (status)
    {
        case SignatureStatus::Valid:
        case SignatureStatus::Warning:
        case SignatureStatus::Invalid:
            return status;
    }

    return SignatureStatus::Valid;
}

QString PDFComponentPainter::getSignatureStatusText(SignatureStatus status)
{
    switch (normalized(status))
    {
        case SignatureStatus::Valid:
            return QCoreApplication::translate("PDFComponentPainter", "Valid");
        case SignatureStatus::Warning:
            return QCoreApplication::translate("PDFComponentPainter", "Needs attention");
        case SignatureStatus::Invalid:
            return QCoreApplication::translate("PDFComponentPainter", "Invalid");
    }

    return QString();
}

PDFComponentPainter::SignatureCardLayout PDFComponentPainter::getSignatureCardLayout(const SignatureCard& card, int width)
{
    using Helper = PDFComponentPanelPainterHelper;

    SignatureCardLayout layout;
    width = Helper::validWidth(width);
    const int innerWidth = qMax(0, width - 2 * Helper::CARD_INSET);

    // The toggle lies 8 px from the trailing edge of the card, centered on the header
    int y = Helper::CARD_INSET;
    layout.toggle = QRect(width - Helper::CARD_TOGGLE_EDGE - Helper::CARD_TOGGLE, y + (Helper::CARD_HEADER - Helper::CARD_TOGGLE) / 2, Helper::CARD_TOGGLE, Helper::CARD_TOGGLE);
    y += Helper::CARD_HEADER + Helper::CARD_GAP + Helper::CARD_PILL_HEIGHT;

    const QStringList summary = wrapText(card.summary, Helper::font(PDFThemeFonts::Role::Small), innerWidth);
    if (!summary.isEmpty())
    {
        y += Helper::CARD_GAP + static_cast<int>(qMin<qsizetype>(summary.size(), 1000)) * Helper::LINE_12;
    }

    for (int i = 0; i < card.certificates.size(); ++i)
    {
        layout.links << QRect();
    }

    if (card.expanded)
    {
        const int detailCount = static_cast<int>(qMin<qsizetype>(card.details.size(), 1000));
        if (detailCount > 0)
        {
            // Divider and the rows of the details
            y += Helper::CARD_GAP + 1 + Helper::CARD_GAP;
            for (int i = 0; i < detailCount; ++i)
            {
                y += Helper::layoutDetail(card.details.at(i), innerWidth).height + (i > 0 ? Helper::CARD_DETAIL_GAP : 0);
            }
        }

        const int certificateCount = static_cast<int>(qMin<qsizetype>(card.certificates.size(), 1000));
        if (certificateCount > 0)
        {
            // Divider, the caption and the chain
            y += Helper::CARD_GAP + 1 + Helper::CARD_GAP + Helper::LINE_11 + Helper::CARD_GAP;
            const int textLeft = Helper::CARD_INSET + Helper::CARD_RAIL + Helper::CARD_RAIL_GAP;
            for (int i = 0; i < certificateCount; ++i)
            {
                const SignatureCertificate& certificate = card.certificates.at(i);
                const Helper::CertificateLines lines = Helper::layoutCertificate(certificate, innerWidth, i == certificateCount - 1);
                if (lines.linkTop >= 0)
                {
                    const int linkWidth = qMin(qMax(0, innerWidth - Helper::CARD_RAIL - Helper::CARD_RAIL_GAP), getTextLinkSize(TextLink{ certificate.linkText }).width());
                    layout.links[i] = QRect(textLeft, y + lines.linkTop, linkWidth, Helper::CARD_LINK);
                }
                y += lines.height;
            }
        }
    }

    layout.height = y + Helper::CARD_INSET;
    return layout;
}

int PDFComponentPainter::getSignatureCardHeight(const SignatureCard& card, int width)
{
    return getSignatureCardLayout(card, width).height;
}

QSize PDFComponentPainter::getSignatureCardSize(const SignatureCard& card)
{
    using Helper = PDFComponentPanelPainterHelper;
    const int width = qMax(Helper::CARD_WIDTH, getSignatureCardMinimumSize(card).width());
    return QSize(width, getSignatureCardHeight(card, width));
}

QSize PDFComponentPainter::getSignatureCardMinimumSize(const SignatureCard& card)
{
    using Helper = PDFComponentPanelPainterHelper;

    // The header with a short text, the status tag, the column of the keys
    const int header = Helper::CARD_BADGE + Helper::CARD_GAP + Helper::CARD_MIN_TEXT + Helper::CARD_HEADER_TRAILING;
    const int pill = Helper::textWidth(getSignatureStatusText(card.status), Helper::font(PDFThemeFonts::Role::CaptionSemiBold)) + 2 * Helper::CARD_PILL_SIDE;
    const int details = Helper::CARD_KEY + Helper::CARD_KEY_GAP + Helper::CARD_MIN_VALUE;
    const int width = 2 * Helper::CARD_INSET + qMax(qMax(header, pill), details);
    return QSize(width, getSignatureCardHeight(card, width));
}

void PDFComponentPainter::paintSignatureCard(QPainter* painter, const QRectF& rect, const SignatureCard& card, const State& state)
{
    using Helper = PDFComponentPanelPainterHelper;
    using Color = PDFThemeColors::Color;
    using Role = PDFAccentHueColors::Role;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const int width = qFloor(rect.width());
    const int innerWidth = qMax(0, width - 2 * Helper::CARD_INSET);
    const SignatureCardLayout layout = getSignatureCardLayout(card, width);
    const PDFAccentHue hue = Helper::getStatusHue(card.status);
    const PDFStylePainter::Radii radii(qMin(Helper::CARD_RADIUS, qMin(rect.width(), rect.height()) / 2.0));
    const qreal left = Helper::CARD_INSET;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Card
    PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
    PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);

    painter->save();
    painter->setClipRect(rect, Qt::IntersectClip);

    // Header: the badge of the status (the fill keeps its hue when disabled, O-08)
    qreal y = Helper::CARD_INSET;
    const QRectF badge = Helper::place(rect, QRectF(left, y, Helper::CARD_BADGE, Helper::CARD_BADGE), state);
    PDFStylePainter::fillEllipse(painter, badge, PDFStylePainter::color(PDFAccentHueColors::getColor(hue, Role::Background)));
    PDFStylePainter::icon(painter, alignedCentered(badge, QSizeF(Helper::CARD_BADGE_ICON, Helper::CARD_BADGE_ICON)), Helper::getStatusIcon(card.status),
                          Helper::icon(state, PDFAccentHueColors::getColor(hue, Role::Icon)));

    // Signer and meta, centered on the badge
    const qreal textLeft = left + Helper::CARD_BADGE + Helper::CARD_GAP;
    const qreal textWidth = innerWidth - Helper::CARD_BADGE - Helper::CARD_GAP - Helper::CARD_HEADER_TRAILING;
    const bool hasMeta = !card.meta.isEmpty();
    const qreal blockHeight = Helper::LINE_SEMIBOLD_13 + (hasMeta ? Helper::CARD_TEXT_GAP + Helper::LINE_11 : 0);
    const qreal blockTop = y + qFloor((Helper::CARD_HEADER - blockHeight) / 2.0);
    Helper::drawDataText(painter, state, rect, QRectF(textLeft, blockTop, textWidth, Helper::LINE_SEMIBOLD_13), card.signer,
                         Helper::font(PDFThemeFonts::Role::BodySemiBold), Helper::text(state, Color::TextPrimary));
    if (hasMeta)
    {
        Helper::drawText(painter, state, rect, QRectF(textLeft, blockTop + Helper::LINE_SEMIBOLD_13 + Helper::CARD_TEXT_GAP, textWidth, Helper::LINE_11), card.meta,
                         Helper::font(PDFThemeFonts::Role::CaptionRegular), Helper::text(state, Color::TextMuted));
    }
    y += Helper::CARD_HEADER + Helper::CARD_GAP;

    // Status tag: a pill of the hue
    const QFont pillFont = Helper::font(PDFThemeFonts::Role::CaptionSemiBold);
    const QString statusText = getSignatureStatusText(card.status);
    const qreal pillWidth = qMin<qreal>(innerWidth, Helper::textWidth(statusText, pillFont) + 2 * Helper::CARD_PILL_SIDE);
    if (pillWidth > 0.0)
    {
        const QRectF pill = Helper::place(rect, QRectF(left, y, pillWidth, Helper::CARD_PILL_HEIGHT), state);
        const PDFStylePainter::Radii pillRadii(Helper::CARD_PILL_HEIGHT / 2.0);
        PDFStylePainter::fill(painter, pill, pillRadii, PDFAccentHueColors::getColor(hue, Role::Background));
        PDFStylePainter::strokeInside(painter, pill, pillRadii, PDFAccentHueColors::getColor(hue, Role::Border), 1.0);
        Helper::drawText(painter, state, rect, QRectF(left + Helper::CARD_PILL_SIDE, y + (Helper::CARD_PILL_HEIGHT - Helper::LINE_11) / 2, pillWidth - 2 * Helper::CARD_PILL_SIDE, Helper::LINE_11),
                         statusText, pillFont, Helper::text(state, PDFAccentHueColors::getColor(hue, Role::Icon)));
    }
    y += Helper::CARD_PILL_HEIGHT;

    // Summary
    const QFont summaryFont = Helper::font(PDFThemeFonts::Role::Small);
    const QStringList summary = wrapText(card.summary, summaryFont, innerWidth);
    if (!summary.isEmpty())
    {
        y += Helper::CARD_GAP;
        Helper::drawLines(painter, state, rect, QRectF(left, y, innerWidth, summary.size() * Helper::LINE_12), Helper::LINE_12, summary, summaryFont, Helper::text(state, Color::TextSecondary));
        y += summary.size() * Helper::LINE_12;
    }

    if (card.expanded)
    {
        const QFont keyFont = Helper::font(PDFThemeFonts::Role::CaptionRegular);
        const QFont valueFont = Helper::font(PDFThemeFonts::Role::Caption);

        if (!card.details.isEmpty())
        {
            y += Helper::CARD_GAP;
            PDFStylePainter::fillRect(painter, Helper::place(rect, QRectF(left, y, innerWidth, 1.0), state), Color::BorderDivider);
            y += 1 + Helper::CARD_GAP;

            for (int i = 0; i < card.details.size() && i < 1000; ++i)
            {
                if (i > 0)
                {
                    y += Helper::CARD_DETAIL_GAP;
                }

                const Helper::DetailLines lines = Helper::layoutDetail(card.details.at(i), innerWidth);
                Helper::drawLines(painter, state, rect, QRectF(left, y, Helper::CARD_KEY, lines.height), Helper::LINE_11, lines.key, keyFont, Helper::text(state, Color::TextMuted));
                const qreal valueLeft = left + Helper::CARD_KEY + Helper::CARD_KEY_GAP;
                Helper::drawDataLines(painter, state, rect, QRectF(valueLeft, y, innerWidth - Helper::CARD_KEY - Helper::CARD_KEY_GAP, lines.height), Helper::LINE_11, lines.value, valueFont,
                                      Helper::text(state, Color::TextPrimary), getDataTextDirection(card.details.at(i).value));
                y += lines.height;
            }
        }

        if (!card.certificates.isEmpty())
        {
            y += Helper::CARD_GAP;
            PDFStylePainter::fillRect(painter, Helper::place(rect, QRectF(left, y, innerWidth, 1.0), state), Color::BorderDivider);
            y += 1 + Helper::CARD_GAP;

            Helper::drawText(painter, state, rect, QRectF(left, y, innerWidth, Helper::LINE_11), card.chainCaption, valueFont, Helper::text(state, Color::TextMuted));
            y += Helper::LINE_11 + Helper::CARD_GAP;

            const QFont nameFont = Helper::font(PDFThemeFonts::Role::SmallSemiBold);
            const qreal certificateLeft = left + Helper::CARD_RAIL + Helper::CARD_RAIL_GAP;
            const qreal certificateWidth = innerWidth - Helper::CARD_RAIL - Helper::CARD_RAIL_GAP;
            const int count = static_cast<int>(qMin<qsizetype>(card.certificates.size(), 1000));

            for (int i = 0; i < count; ++i)
            {
                const SignatureCertificate& certificate = card.certificates.at(i);
                const bool isLast = i == count - 1;
                const Helper::CertificateLines lines = Helper::layoutCertificate(certificate, innerWidth, isLast);
                const PDFAccentHue certificateHue = Helper::getStatusHue(certificate.status);
                const bool isValid = normalized(certificate.status) == SignatureStatus::Valid;

                // Rail: the dot of the status and the line to the next certificate
                PDFStylePainter::fillEllipse(painter, Helper::place(rect, QRectF(left, y + Helper::CARD_DOT_TOP, Helper::CARD_DOT, Helper::CARD_DOT), state),
                                             PDFStylePainter::color(Helper::icon(state, PDFAccentHueColors::getColor(certificateHue, Role::Icon))));
                if (!isLast && lines.height > Helper::CARD_LINE_TOP)
                {
                    PDFStylePainter::fillRect(painter, Helper::place(rect, QRectF(left + Helper::CARD_LINE_LEFT, y + Helper::CARD_LINE_TOP, 1.0, lines.height - Helper::CARD_LINE_TOP), state),
                                              Color::BorderStrong);
                }

                Helper::drawDataText(painter, state, rect, QRectF(certificateLeft, y, certificateWidth, Helper::LINE_12), certificate.name, nameFont, Helper::text(state, Color::TextPrimary));
                const Color infoColor = isValid ? Color::TextMuted : PDFAccentHueColors::getColor(certificateHue, Role::Icon);
                Helper::drawLines(painter, state, rect, QRectF(certificateLeft, y + Helper::LINE_12 + Helper::CARD_TEXT_GAP, certificateWidth, lines.info.size() * Helper::LINE_11),
                                  Helper::LINE_11, lines.info, keyFont, Helper::text(state, infoColor));

                if (card.paintNested && i < layout.links.size() && !layout.links.at(i).isEmpty())
                {
                    paintTextLink(painter, Helper::place(rect, QRectF(layout.links.at(i)), state), TextLink{ certificate.linkText }, Helper::nestedState(state, false));
                }

                y += lines.height;
            }
        }
    }

    // The toggle: an Icon button 24 with the chevron of the state
    if (card.paintNested && card.toggleVisible)
    {
        IconButton toggle;
        toggle.iconName = card.expanded ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down");
        toggle.extent = Helper::CARD_TOGGLE;
        toggle.iconExtent = Helper::CARD_TOGGLE_ICON;
        paintIconButton(painter, Helper::place(rect, QRectF(layout.toggle), state), toggle, Helper::nestedState(state, false));
    }

    painter->restore();
    painter->restore();
}

// ------------------------------------------------------------ Strength meter

PDFComponentPainter::StrengthLevel PDFComponentPainter::normalized(StrengthLevel level)
{
    switch (level)
    {
        case StrengthLevel::Empty:
        case StrengthLevel::VeryWeak:
        case StrengthLevel::Weak:
        case StrengthLevel::Moderate:
        case StrengthLevel::Strong:
        case StrengthLevel::VeryStrong:
            return level;
    }

    return StrengthLevel::Empty;
}

int PDFComponentPainter::getStrengthMeterCellCount()
{
    return PDFComponentPanelPainterHelper::METER_CELLS;
}

int PDFComponentPainter::getStrengthMeterFilledCells(const StrengthMeter& meter)
{
    // The level Empty has nothing measured: no cell is filled (also with own filled cells)
    if (normalized(meter.level) == StrengthLevel::Empty)
    {
        return 0;
    }

    if (meter.filledCells >= 0)
    {
        return qMin(meter.filledCells, PDFComponentPanelPainterHelper::METER_CELLS);
    }

    switch (normalized(meter.level))
    {
        case StrengthLevel::Empty:
            return 0;
        case StrengthLevel::VeryWeak:
            return 3;
        case StrengthLevel::Weak:
            return 7;
        case StrengthLevel::Moderate:
            return 11;
        case StrengthLevel::Strong:
            return 16;
        case StrengthLevel::VeryStrong:
            return 20;
    }

    return 0;
}

QString PDFComponentPainter::getStrengthLevelText(const StrengthMeter& meter)
{
    switch (normalized(meter.level))
    {
        case StrengthLevel::Empty:
            return meter.title;
        case StrengthLevel::VeryWeak:
            return QCoreApplication::translate("PDFComponentPainter", "Very weak");
        case StrengthLevel::Weak:
            return QCoreApplication::translate("PDFComponentPainter", "Weak");
        case StrengthLevel::Moderate:
            return QCoreApplication::translate("PDFComponentPainter", "Moderate");
        case StrengthLevel::Strong:
            return QCoreApplication::translate("PDFComponentPainter", "Strong");
        case StrengthLevel::VeryStrong:
            return QCoreApplication::translate("PDFComponentPainter", "Very strong");
    }

    return meter.title;
}

QString PDFComponentPainter::getStrengthCriterionText(StrengthCriterion criterion)
{
    switch (criterion)
    {
        case StrengthCriterion::Length:
            return QCoreApplication::translate("PDFComponentPainter", "12+ characters");
        case StrengthCriterion::Case:
            return QCoreApplication::translate("PDFComponentPainter", "Aa");
        case StrengthCriterion::Digits:
            return QCoreApplication::translate("PDFComponentPainter", "123");
        case StrengthCriterion::Symbols:
            return QCoreApplication::translate("PDFComponentPainter", "#$&");
        case StrengthCriterion::None:
            break;
    }

    return QString();
}

QString PDFComponentPainter::getStrengthCriterionText(const StrengthMeter& meter, StrengthCriterion criterion)
{
    using Helper = PDFComponentPanelPainterHelper;

    for (int i = 0; i < 4; ++i)
    {
        if (Helper::CRITERIA[i] == criterion)
        {
            const QString text = meter.criteriaTexts.value(i);
            return text.isEmpty() ? getStrengthCriterionText(criterion) : text;
        }
    }

    return QString();
}

QList<QRect> PDFComponentPainter::getStrengthMeterCriterionRects(const StrengthMeter& meter, int width)
{
    using Helper = PDFComponentPanelPainterHelper;

    QList<QRect> rects;
    width = Helper::validWidth(width);

    // The criteria flow from the leading edge and wrap with gaps of 4
    int x = 0;
    int y = Helper::METER_TOP + Helper::METER_GAP + Helper::METER_TRACK + Helper::METER_GAP;
    for (const StrengthCriterion criterion : Helper::CRITERIA)
    {
        const int criterionWidth = qMin(width, Helper::getCriterionWidth(getStrengthCriterionText(meter, criterion)));
        if (x > 0 && x + criterionWidth > width)
        {
            x = 0;
            y += Helper::METER_CRITERION + Helper::METER_CRITERIA_GAP;
        }

        rects << QRect(x, y, criterionWidth, Helper::METER_CRITERION);
        x += criterionWidth + Helper::METER_CRITERIA_GAP;
    }

    return rects;
}

int PDFComponentPainter::getStrengthMeterHeight(const StrengthMeter& meter, int width)
{
    using Helper = PDFComponentPanelPainterHelper;

    if (!meter.criteriaVisible)
    {
        return Helper::METER_TOP + Helper::METER_GAP + Helper::METER_TRACK;
    }

    const QList<QRect> rects = getStrengthMeterCriterionRects(meter, width);
    return rects.isEmpty() ? Helper::METER_TOP + Helper::METER_GAP + Helper::METER_TRACK : rects.back().bottom() + 1;
}

QSize PDFComponentPainter::getStrengthMeterSize(const StrengthMeter& meter)
{
    using Helper = PDFComponentPanelPainterHelper;
    const int width = qMax(Helper::METER_WIDTH, getStrengthMeterMinimumSize(meter).width());
    return QSize(width, getStrengthMeterHeight(meter, width));
}

QSize PDFComponentPainter::getStrengthMeterMinimumSize(const StrengthMeter& meter)
{
    using Helper = PDFComponentPanelPainterHelper;

    // The widest criterion, the level and the detail
    int width = Helper::METER_MIN_WIDTH;
    if (meter.criteriaVisible)
    {
        for (const StrengthCriterion criterion : Helper::CRITERIA)
        {
            width = qMax(width, Helper::getCriterionWidth(getStrengthCriterionText(meter, criterion)));
        }
    }

    width = qMin(width, Helper::MAX_EXTENT);
    return QSize(width, getStrengthMeterHeight(meter, width));
}

void PDFComponentPainter::paintStrengthMeter(QPainter* painter, const QRectF& rect, const StrengthMeter& meter, const State& state)
{
    using Helper = PDFComponentPanelPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const int width = qFloor(rect.width());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Top: the level at the leading edge, the detail at the trailing edge
    const QFont detailFont = Helper::font(PDFThemeFonts::Role::CaptionRegular);
    const int detailWidth = meter.detailVisible ? qMin(width, Helper::textWidth(meter.detail, detailFont)) : 0;
    if (detailWidth > 0)
    {
        Helper::drawText(painter, state, rect, Helper::line(width - detailWidth, 0.0, Helper::METER_TOP, detailWidth, Helper::LINE_11), meter.detail, detailFont,
                         Helper::text(state, Color::TextTertiary), true);
    }
    const qreal levelWidth = width - (detailWidth > 0 ? detailWidth + Helper::METER_TOP_GAP : 0);
    Helper::drawText(painter, state, rect, QRectF(0.0, 0.0, levelWidth, Helper::METER_TOP), getStrengthLevelText(meter), Helper::font(PDFThemeFonts::Role::SmallSemiBold),
                     Helper::text(state, Helper::getLevelColor(meter.level)));

    // Track of 20 cells: the filled cells from the leading edge, the leading end
    // rounded by the track, the trailing end of the value straight
    const QRectF track = Helper::place(rect, QRectF(0.0, Helper::METER_TOP + Helper::METER_GAP, width, Helper::METER_TRACK), state);
    const PDFStylePainter::Radii trackRadii(Helper::METER_TRACK_RADIUS);
    PDFStylePainter::fill(painter, track, trackRadii, Color::BorderDivider);

    const int cells = getStrengthMeterFilledCells(meter);
    if (cells > 0)
    {
        const qreal valueWidth = track.width() * cells / Helper::METER_CELLS;
        const QRectF value = Helper::place(rect, QRectF(0.0, Helper::METER_TOP + Helper::METER_GAP, valueWidth, Helper::METER_TRACK), state);
        painter->save();
        painter->setClipPath(PDFStylePainter::roundedPath(track, trackRadii), Qt::IntersectClip);

        // A disabled meter has the value border/strong (as the progress bar of the style)
        PDFStylePainter::fillRect(painter, value, state.isEnabled() ? Helper::getLevelColor(meter.level) : Color::BorderStrong);
        painter->restore();
    }

    if (meter.criteriaVisible)
    {
        const QList<QRect> rects = getStrengthMeterCriterionRects(meter, width);
        const QFont criterionFont = Helper::font(PDFThemeFonts::Role::Caption);

        for (int i = 0; i < rects.size(); ++i)
        {
            const StrengthCriterion criterion = Helper::CRITERIA[i];
            const bool isSatisfied = meter.satisfied.testFlag(criterion);
            const QRectF local(rects.at(i));
            const QRectF pill = Helper::place(rect, local, state);
            PDFStylePainter::fill(painter, pill, PDFStylePainter::Radii(qMin(Helper::METER_CRITERION_RADIUS, pill.height() / 2.0)), isSatisfied ? Color::AccentGreenBg : Color::BgMuted);

            const QRectF mark(local.left() + Helper::METER_CRITERION_LEFT, local.top() + (Helper::METER_CRITERION - Helper::METER_MARK) / 2, Helper::METER_MARK, Helper::METER_MARK);
            if (isSatisfied)
            {
                Helper::drawIcon(painter, state, rect, mark, QStringLiteral("check"), Helper::icon(state, Color::AccentGreenIcon));
            }
            else
            {
                const QRectF dot = alignedCentered(Helper::place(rect, mark, state), QSizeF(Helper::METER_DOT, Helper::METER_DOT));
                PDFStylePainter::fillEllipse(painter, dot, PDFStylePainter::color(Helper::text(state, Color::TextTertiary)));
            }

            const qreal textLeft = mark.right() + Helper::METER_CRITERION_GAP;
            Helper::drawText(painter, state, rect, QRectF(textLeft, local.top() + (Helper::METER_CRITERION - Helper::LINE_11) / 2, local.right() - Helper::METER_CRITERION_RIGHT - textLeft, Helper::LINE_11),
                             getStrengthCriterionText(meter, criterion), criterionFont, Helper::text(state, isSatisfied ? Color::AccentGreenIcon : Color::TextTertiary));
        }
    }

    painter->restore();
}

}   // namespace pdf
