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

// Drawing of the frames of the page AI of Main page (step 7): the avatar of an
// assistant, the client row, the activity row, the assistant tile and the code
// box of the setup card. The class is declared in pdfcomponentpainter.h; this
// file holds their geometry constants.

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QPainter>
#include <QTextLayout>
#include <QtMath>

#include <memory>

namespace pdf
{

/// Geometry constants of the frames of the page AI and their drawing helpers
class PDFComponentAssistantPainterHelper
{
public:
    PDFComponentAssistantPainterHelper() = delete;

    using Color = PDFThemeColors::Color;
    using State = PDFComponentPainter::State;
    using Role = PDFAccentHueColors::Role;

    static constexpr int MAX_EXTENT = 1024;         ///< Largest extent of an avatar (a hostile value is bounded)
    static constexpr int MAX_WIDTH = 1 << 20;       ///< Largest width of a layout (larger values are bounded)
    static constexpr int MIN_TEXT = 40;             ///< Minimal width of an elided text column

    // Lines of the texts (automatic line heights of Inter)
    static constexpr int LINE_SEMIBOLD_13 = 16;
    static constexpr int LINE_REGULAR_11 = 13;

    // Avatar: CLI badge 16 at 5/8 of the extent (20 of 32, 22 of 36), radius 8,
    // the icon terminal 10 in its middle
    static constexpr int BADGE = 16;
    static constexpr int BADGE_ICON = 10;

    // Client row 653:5591: paddings 10 / 8, avatar 32, gap 12, name 16 (the name
    // row 21 with a tag), gap 2, meta 13, disconnect 28, tag after a gap of 8
    static constexpr int CLIENT_SIDE = 10;
    static constexpr int CLIENT_VERTICAL = 8;
    static constexpr int CLIENT_GAP = 12;
    static constexpr int CLIENT_TEXT_GAP = 2;
    static constexpr int CLIENT_TAG_GAP = 8;
    static constexpr int CLIENT_BUTTON = 28;
    static constexpr int CLIENT_BUTTON_ICON = 16;
    static constexpr qreal CLIENT_BUTTON_RADIUS = 6.0;

    // Activity row 955:147547: paddings 10 / 8, circle 24 with the icon 12, gap
    // 10, the title row 16 (the time after a gap of 8), gaps 2, meta 13, status
    // in lines of 13, radius 8, the default width 322
    static constexpr int ACTIVITY_WIDTH = 322;
    static constexpr int ACTIVITY_SIDE = 10;
    static constexpr int ACTIVITY_VERTICAL = 8;
    static constexpr int ACTIVITY_CIRCLE = 24;
    static constexpr int ACTIVITY_ICON = 12;
    static constexpr int ACTIVITY_GAP = 10;
    static constexpr int ACTIVITY_TIME_GAP = 8;
    static constexpr int ACTIVITY_TEXT_GAP = 2;
    static constexpr int ACTIVITY_MAX_STATUS_LINES = 4;
    static constexpr qreal ACTIVITY_RADIUS = 8.0;

    // Assistant tile 653:5691: 184 x 66, padding 14 inside of the border 1, logo
    // 36, gap 12, name 16, gap 2, vendor in lines of 13 (two at most), check 16
    static constexpr int TILE_WIDTH = 184;
    static constexpr int TILE_HEIGHT = 66;
    static constexpr int TILE_INSET = 15;
    static constexpr int TILE_LOGO = 36;
    static constexpr int TILE_GAP = 12;
    static constexpr int TILE_CHECK = 16;
    static constexpr int TILE_TEXT_GAP = 2;
    static constexpr int TILE_VENDOR_LINES = 2;
    static constexpr int TILE_MIN_TEXT = 60;
    static constexpr qreal TILE_RADIUS = 12.0;

    // Code box 653:5754: border 1, paddings 14 / 4 / 4 / 4, gap 12, the copy
    // Toolbar button 36, lines of 18, radius 8; the widest hint 1166
    static constexpr int CODE_LEADING = 15;
    static constexpr int CODE_TRAILING = 5;
    static constexpr int CODE_VERTICAL = 5;
    static constexpr int CODE_GAP = 12;
    static constexpr int CODE_BUTTON = 36;
    static constexpr int CODE_BUTTON_ICON = 20;
    static constexpr qreal CODE_BUTTON_RADIUS = 10.0;
    static constexpr int CODE_LINE = 18;
    static constexpr int CODE_MAX_HINT = 1166;
    static constexpr qreal CODE_RADIUS = 8.0;

    static QFont nameFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold); }
    static QFont smallFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular); }
    static QFont linkFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold); }

    static int textWidth(const QString& text, const QFont& font) { return text.isEmpty() ? 0 : qCeil(PDFStylePainter::textWidth(text, font)); }
    static int extent(int value) { return qBound(0, value, MAX_EXTENT); }
    static int validWidth(int width) { return qBound(0, width, MAX_WIDTH); }
    static bool isDrawable(const QRectF& rect) { return rect.isValid() && !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height()); }
    static Color text(const State& state, Color color) { return state.isEnabled() ? color : Color::TextDisabled; }
    static Color icon(const State& state, Color color) { return state.isEnabled() ? color : Color::IconDisabled; }

    /// Returns the logical rectangle of the height centered vertically in the outer rectangle
    static QRectF line(const QRectF& outer, qreal left, qreal width, qreal height)
    {
        return QRectF(left, outer.top() + qFloor((outer.height() - height) / 2.0), qMax<qreal>(0.0, width), height);
    }

    /// Draws a single line of a text of the kind into the logical rectangle
    static void drawText(QPainter* painter, const State& state, const QRectF& outer, const QRectF& logical, const QString& text, const QFont& font,
                         Color color, PDFComponentPainter::TextKind kind, bool isTrailing = false)
    {
        if (logical.width() > 0.0 && logical.height() > 0.0 && !text.isEmpty())
        {
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(outer, logical), text, font, color, kind, isTrailing);
        }
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

    /// Returns the offset of the CLI badge from the top left corner of the circle (20 of 32, 22 of 36)
    static int badgeOffset(int circle) { return (circle * 5) / 8; }

    /// Returns the width, by which the CLI badge reaches out of the circle
    static int badgeOverhang(const PDFComponentPainter::AssistantAvatar& avatar)
    {
        const int circle = extent(avatar.extent);
        return (avatar.commandLine && circle > 0) ? qMax(0, badgeOffset(circle) + BADGE - circle) : 0;
    }

    /// Returns the rectangle of the disconnect button of the client row in its logical layout
    static QRectF clientButtonRect(const QRectF& rect)
    {
        return line(rect, rect.right() - CLIENT_SIDE - CLIENT_BUTTON, CLIENT_BUTTON, CLIENT_BUTTON);
    }

    /// Returns the color of the status of an activity row
    static Color statusColor(PDFComponentPainter::ActivityStatusTone tone)
    {
        switch (PDFComponentPainter::normalized(tone))
        {
            case PDFComponentPainter::ActivityStatusTone::Secondary:
                return Color::TextSecondary;

            case PDFComponentPainter::ActivityStatusTone::Danger:
                return Color::TextDanger;

            case PDFComponentPainter::ActivityStatusTone::Muted:
                break;
        }
        return Color::TextMuted;
    }

    /// Lays the status of an activity row out into the width: the status in the
    /// font Regular 11 followed by the inline link Semi Bold 11, which wrap
    /// together, lines of 13. The colors are formats of the layout.
    static std::unique_ptr<QTextLayout> layoutStatus(const PDFComponentPainter::ActivityRow& row, qreal width, const State& state)
    {
        const QString statusText = row.status + row.linkText;
        if (statusText.isEmpty() || !(width > 0.0) || !qIsFinite(width))
        {
            return nullptr;
        }

        std::unique_ptr<QTextLayout> layout = std::make_unique<QTextLayout>(statusText, smallFont());

        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setTextDirection(state.direction);
        option.setAlignment(state.leadingAlignment());
        layout->setTextOption(option);

        QList<QTextLayout::FormatRange> formats;
        QTextLayout::FormatRange statusRange;
        statusRange.start = 0;
        statusRange.length = int(row.status.size());
        statusRange.format.setForeground(PDFStylePainter::color(text(state, statusColor(row.statusTone))));
        formats << statusRange;

        if (!row.linkText.isEmpty())
        {
            QTextLayout::FormatRange linkRange;
            linkRange.start = int(row.status.size());
            linkRange.length = int(row.linkText.size());
            linkRange.format.setFont(linkFont());
            linkRange.format.setForeground(PDFStylePainter::color(text(state, Color::TextAccent)));
            formats << linkRange;
        }
        layout->setFormats(formats);

        layout->beginLayout();
        int lineIndex = 0;
        while (true)
        {
            QTextLine textLine = layout->createLine();
            if (!textLine.isValid())
            {
                break;
            }

            textLine.setLineWidth(width);
            textLine.setPosition(QPointF(0.0, lineIndex * LINE_REGULAR_11 + qFloor((LINE_REGULAR_11 - textLine.height()) / 2.0)));
            ++lineIndex;
        }
        layout->endLayout();
        return layout;
    }

    /// Returns the number of the lines of the status of an activity row of the width of its text
    static int statusLineCount(const PDFComponentPainter::ActivityRow& row, qreal width)
    {
        const std::unique_ptr<QTextLayout> layout = layoutStatus(row, width, State());
        return layout ? qMin(layout->lineCount(), ACTIVITY_MAX_STATUS_LINES) : 0;
    }

    /// Returns the width of the texts of an activity row in the row of the width
    static qreal activityTextWidth(qreal width)
    {
        return width - 2 * ACTIVITY_SIDE - ACTIVITY_CIRCLE - ACTIVITY_GAP;
    }

    /// Returns the height of the text column of an activity row of the width
    static int activityTextHeight(const PDFComponentPainter::ActivityRow& row, qreal width)
    {
        const int statusLines = statusLineCount(row, activityTextWidth(width));
        int height = LINE_SEMIBOLD_13 + ACTIVITY_TEXT_GAP + LINE_REGULAR_11;
        if (statusLines > 0)
        {
            height += ACTIVITY_TEXT_GAP + statusLines * LINE_REGULAR_11;
        }
        return height;
    }

    /// Returns the width of the text of a code box of the width
    static qreal codeTextWidth(qreal width)
    {
        return width - CODE_LEADING - CODE_GAP - CODE_BUTTON - CODE_TRAILING;
    }

    /// Returns the height of the text of a code box of the lines (a text shorter
    /// than the button is centered on it)
    static int codeContentHeight(int lineCount)
    {
        return qMax(CODE_BUTTON, lineCount * CODE_LINE);
    }

    /// Returns the paragraphs of the text of a code box (line breaks of all systems)
    static QStringList codeParagraphs(const QString& text)
    {
        QString normalized = text;
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        return normalized.split(QLatin1Char('\n'));
    }

    /// Returns the text option of a code box: wrapped at any character
    static QTextOption codeTextOption()
    {
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        return option;
    }

    /// Draws the toolbar button of the copy (icon copy 20, no background at rest)
    static void drawCopyButton(QPainter* painter, const QRectF& rect, const State& state)
    {
        const PDFStylePainter::Radii radii(CODE_BUTTON_RADIUS);
        fillPointer(painter, rect, radii, state);
        PDFStylePainter::icon(painter, PDFStylePainter::centered(rect, QSizeF(CODE_BUTTON_ICON, CODE_BUTTON_ICON)), QStringLiteral("copy"),
                              icon(state, state.isHovered() || state.isPressed() ? Color::IconStrong : Color::IconDefault));
    }
};

// ----------------------------------------------------------- Assistant avatar

QSize PDFComponentPainter::getAssistantAvatarSize(const AssistantAvatar& avatar)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int size = Helper::extent(avatar.extent) + Helper::badgeOverhang(avatar);
    return QSize(size, size);
}

QRectF PDFComponentPainter::getAssistantAvatarCircle(const QRectF& rect, const AssistantAvatar& avatar, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const qreal circle = qMin<qreal>(Helper::extent(avatar.extent), qMin(rect.width(), rect.height()));
    if (!(circle > 0.0))
    {
        return QRectF();
    }
    return state.visualRect(rect, QRectF(rect.left(), rect.top(), circle, circle));
}

void PDFComponentPainter::paintAssistantAvatar(QPainter* painter, const QRectF& rect, const AssistantAvatar& avatar, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const QRectF circle = getAssistantAvatarCircle(rect, avatar, state);
    if (circle.isEmpty())
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFAccentHue hue = normalized(avatar.hue);
    const Color iconColor = Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    PDFStylePainter::fillEllipse(painter, circle, PDFStylePainter::color(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background)));

    // The monogram is data (at most three characters); without it the icon mcp
    const QString monogram = avatar.monogram.trimmed().left(3);
    if (monogram.isEmpty())
    {
        const qreal iconExtent = qFloor(circle.width() / 2.0);
        if (iconExtent > 0.0)
        {
            PDFStylePainter::icon(painter, alignedCentered(circle, QSizeF(iconExtent, iconExtent)), QStringLiteral("mcp"), iconColor);
        }
    }
    else
    {
        const QFont font = PDFThemeFonts::getFont(QFont::Bold, qBound(1, avatar.fontPixelSize, Helper::MAX_EXTENT));
        drawDataText(painter, circle, Qt::AlignCenter | Qt::TextSingleLine, monogram, font, PDFStylePainter::color(Helper::text(state, iconColor)));
    }

    if (avatar.commandLine)
    {
        // The CLI badge at the trailing bottom of the circle
        const int offset = Helper::badgeOffset(qRound(circle.width()));
        const QRectF logicalCircle = state.visualRect(rect, circle);
        const QRectF badge = state.visualRect(rect, QRectF(logicalCircle.left() + offset, logicalCircle.top() + offset, Helper::BADGE, Helper::BADGE));
        const PDFStylePainter::Radii radii(Helper::BADGE / 2.0);
        PDFStylePainter::fill(painter, badge, radii, Color::BgSurface);
        PDFStylePainter::strokeInside(painter, badge, radii, Color::BorderDefault, 1.0);
        PDFStylePainter::icon(painter, alignedCentered(badge, QSizeF(Helper::BADGE_ICON, Helper::BADGE_ICON)), QStringLiteral("terminal"), Helper::icon(state, Color::IconMuted));
    }
}

// ----------------------------------------------------------------- Client row

QSize PDFComponentPainter::getAssistantRowSize(const AssistantRow& row)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int tagWidth = row.tag.isEmpty() ? 0 : Helper::CLIENT_TAG_GAP + getTagSize(Tag{ row.tag }).width();
    const int nameWidth = Helper::textWidth(row.name, Helper::nameFont()) + tagWidth;
    const int textWidth = qMax(nameWidth, Helper::textWidth(row.meta, Helper::smallFont()));
    const QSize minimumSize = getAssistantRowMinimumSize(row);
    return QSize(minimumSize.width() - Helper::MIN_TEXT - tagWidth + qMax(textWidth, Helper::MIN_TEXT + tagWidth), minimumSize.height());
}

QSize PDFComponentPainter::getAssistantRowMinimumSize(const AssistantRow& row)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int tagWidth = row.tag.isEmpty() ? 0 : Helper::CLIENT_TAG_GAP + getTagSize(Tag{ row.tag }).width();
    const int buttonWidth = row.disconnectVisible ? Helper::CLIENT_GAP + Helper::CLIENT_BUTTON : 0;
    const int width = 2 * Helper::CLIENT_SIDE + getAssistantAvatarSize(AssistantAvatar{ QString(), PDFAccentHue::Teal, row.avatar.extent, 12, false }).width() +
                      Helper::CLIENT_GAP + Helper::MIN_TEXT + tagWidth + buttonWidth;

    // 48 (the avatar 32), 52 with the tag (the name row 21)
    const int nameLine = row.tag.isEmpty() ? Helper::LINE_SEMIBOLD_13 : qMax(Helper::LINE_SEMIBOLD_13, getTagSize(Tag{ row.tag }).height());
    const int textHeight = nameLine + Helper::CLIENT_TEXT_GAP + Helper::LINE_REGULAR_11;
    const int height = 2 * Helper::CLIENT_VERTICAL + qMax(qMax(textHeight, Helper::extent(row.avatar.extent)), row.disconnectVisible ? Helper::CLIENT_BUTTON : 0);
    return QSize(width, height);
}

QRectF PDFComponentPainter::getAssistantRowDisconnectRect(const QRectF& rect, const AssistantRow& row, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    if (!row.disconnectVisible || !Helper::isDrawable(rect))
    {
        return QRectF();
    }
    return state.visualRect(rect, Helper::clientButtonRect(rect));
}

void PDFComponentPainter::paintAssistantRow(QPainter* painter, const QRectF& rect, const AssistantRow& row, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The avatar: its circle centered vertically, the CLI badge reaches out of it
    const int circle = Helper::extent(row.avatar.extent);
    const QSize avatarSize = getAssistantAvatarSize(row.avatar);
    const QRectF avatarRect(rect.left() + Helper::CLIENT_SIDE, rect.top() + qFloor((rect.height() - circle) / 2.0), avatarSize.width(), avatarSize.height());
    paintAssistantAvatar(painter, state.visualRect(rect, avatarRect), row.avatar, State(state.flags & QStyle::State_Enabled, state.direction));

    // The disconnect button at the trailing side (an Icon button 28)
    qreal textRight = rect.right() - Helper::CLIENT_SIDE;
    if (row.disconnectVisible)
    {
        const QRectF buttonRect = Helper::clientButtonRect(rect);
        if (row.paintNested)
        {
            const IconButton button{ QStringLiteral("unplug"), Helper::CLIENT_BUTTON, Helper::CLIENT_BUTTON_ICON, Helper::CLIENT_BUTTON_RADIUS, std::nullopt };
            paintIconButton(painter, state.visualRect(rect, buttonRect), button, State(state.flags & QStyle::State_Enabled, state.direction));
        }
        textRight = buttonRect.left() - Helper::CLIENT_GAP;
    }

    // The texts: the name row (the name and the tag) and the meta text, centered
    const qreal textLeft = rect.left() + Helper::CLIENT_SIDE + circle + Helper::CLIENT_GAP;
    const qreal textWidth = qMax<qreal>(0.0, textRight - textLeft);
    const QSize tagSize = row.tag.isEmpty() ? QSize() : getTagSize(Tag{ row.tag });
    const int nameLine = row.tag.isEmpty() ? Helper::LINE_SEMIBOLD_13 : qMax(Helper::LINE_SEMIBOLD_13, tagSize.height());
    const int textHeight = nameLine + Helper::CLIENT_TEXT_GAP + Helper::LINE_REGULAR_11;
    const qreal top = rect.top() + qFloor((rect.height() - textHeight) / 2.0);
    const QFont nameFont = Helper::nameFont();

    qreal nameWidth = textWidth;
    if (!row.tag.isEmpty())
    {
        // The tag follows the name (never elided, the name gives its place)
        const qreal fullNameWidth = Helper::textWidth(row.name, nameFont);
        nameWidth = qMax<qreal>(0.0, qMin(fullNameWidth, textWidth - Helper::CLIENT_TAG_GAP - tagSize.width()));
        const qreal tagLeft = textLeft + nameWidth + (nameWidth > 0.0 ? Helper::CLIENT_TAG_GAP : 0.0);
        if (tagLeft + tagSize.width() <= textRight + 0.01)
        {
            const QRectF tagRect(tagLeft, top + qFloor((nameLine - tagSize.height()) / 2.0), tagSize.width(), tagSize.height());
            paintTag(painter, state.visualRect(rect, tagRect), Tag{ row.tag }, State(state.flags & QStyle::State_Enabled, state.direction));
        }
    }

    Helper::drawText(painter, state, rect, QRectF(textLeft, top + qFloor((nameLine - Helper::LINE_SEMIBOLD_13) / 2.0), nameWidth, Helper::LINE_SEMIBOLD_13), row.name, nameFont,
                     Helper::text(state, Color::TextPrimary), TextKind::Data);
    Helper::drawText(painter, state, rect, QRectF(textLeft, top + nameLine + Helper::CLIENT_TEXT_GAP, textWidth, Helper::LINE_REGULAR_11), row.meta, Helper::smallFont(),
                     Helper::text(state, Color::TextMuted), TextKind::Data);
}

// --------------------------------------------------------------- Activity row

PDFComponentPainter::ActivityStatusTone PDFComponentPainter::normalized(ActivityStatusTone tone)
{
    switch (tone)
    {
        case ActivityStatusTone::Secondary:
        case ActivityStatusTone::Muted:
        case ActivityStatusTone::Danger:
            return tone;
    }

    return ActivityStatusTone::Muted;
}

int PDFComponentPainter::getActivityRowHeight(const ActivityRow& row, int width)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int textHeight = Helper::activityTextHeight(row, Helper::validWidth(width));
    return 2 * Helper::ACTIVITY_VERTICAL + qMax(textHeight, Helper::ACTIVITY_CIRCLE);
}

QSize PDFComponentPainter::getActivityRowSize(const ActivityRow& row)
{
    using Helper = PDFComponentAssistantPainterHelper;
    return QSize(Helper::ACTIVITY_WIDTH, getActivityRowHeight(row, Helper::ACTIVITY_WIDTH));
}

QSize PDFComponentPainter::getActivityRowMinimumSize(const ActivityRow& row)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int timeWidth = row.time.isEmpty() ? 0 : Helper::ACTIVITY_TIME_GAP + Helper::textWidth(row.time, Helper::smallFont());
    const int width = 2 * Helper::ACTIVITY_SIDE + Helper::ACTIVITY_CIRCLE + Helper::ACTIVITY_GAP + Helper::MIN_TEXT + timeWidth;
    return QSize(width, getActivityRowHeight(row, width));
}

void PDFComponentPainter::paintActivityRow(QPainter* painter, const QRectF& rect, const ActivityRow& row, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainter::Radii radii(Helper::ACTIVITY_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::fillPointer(painter, rect, radii, state);

    if (state.hasFocusRing())
    {
        paintFocusRingInside(painter, rect, radii);
    }

    // The status circle centered vertically
    const PDFAccentHue hue = normalized(row.hue);
    const QRectF circle = Helper::line(rect, rect.left() + Helper::ACTIVITY_SIDE, Helper::ACTIVITY_CIRCLE, Helper::ACTIVITY_CIRCLE);
    const QRectF visualCircle = state.visualRect(rect, circle);
    PDFStylePainter::fillEllipse(painter, visualCircle, PDFStylePainter::color(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background)));
    if (!row.iconName.isEmpty())
    {
        PDFStylePainter::icon(painter, alignedCentered(visualCircle, QSizeF(Helper::ACTIVITY_ICON, Helper::ACTIVITY_ICON)), row.iconName,
                              Helper::icon(state, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)));
    }

    // The text column from the top padding
    const qreal textLeft = circle.right() + Helper::ACTIVITY_GAP;
    const qreal textWidth = qMax<qreal>(0.0, rect.right() - Helper::ACTIVITY_SIDE - textLeft);
    const qreal top = rect.top() + Helper::ACTIVITY_VERTICAL;
    const QFont smallFont = Helper::smallFont();

    // The title row: the title elided, the time at its end (never elided)
    qreal titleWidth = textWidth;
    if (!row.time.isEmpty())
    {
        const qreal timeWidth = qMin<qreal>(Helper::textWidth(row.time, smallFont), textWidth);
        const QRectF timeRect = Helper::line(QRectF(textLeft, top, textWidth, Helper::LINE_SEMIBOLD_13), textLeft + textWidth - timeWidth, timeWidth, Helper::LINE_REGULAR_11);
        Helper::drawText(painter, state, rect, timeRect, row.time, smallFont, Helper::text(state, Color::TextMuted), TextKind::Data, true);
        titleWidth = qMax<qreal>(0.0, textWidth - timeWidth - Helper::ACTIVITY_TIME_GAP);
    }
    Helper::drawText(painter, state, rect, QRectF(textLeft, top, titleWidth, Helper::LINE_SEMIBOLD_13), row.title, Helper::nameFont(),
                     Helper::text(state, Color::TextPrimary), TextKind::Interface);

    // The meta text (data)
    const qreal metaTop = top + Helper::LINE_SEMIBOLD_13 + Helper::ACTIVITY_TEXT_GAP;
    Helper::drawText(painter, state, rect, QRectF(textLeft, metaTop, textWidth, Helper::LINE_REGULAR_11), row.meta, smallFont,
                     Helper::text(state, Color::TextMuted), TextKind::Data);

    // The status and the inline link, which wrap (at most four lines, the last one elided)
    const std::unique_ptr<QTextLayout> layout = Helper::layoutStatus(row, textWidth, state);
    if (!layout || layout->lineCount() == 0)
    {
        return;
    }

    const qreal statusTop = metaTop + Helper::LINE_REGULAR_11 + Helper::ACTIVITY_TEXT_GAP;
    const QRectF statusRect = state.visualRect(rect, QRectF(textLeft, statusTop, textWidth, Helper::ACTIVITY_MAX_STATUS_LINES * Helper::LINE_REGULAR_11));
    const int lineCount = layout->lineCount();
    const bool isTruncated = lineCount > Helper::ACTIVITY_MAX_STATUS_LINES;
    const int drawnLines = qMin(lineCount, Helper::ACTIVITY_MAX_STATUS_LINES - (isTruncated ? 1 : 0));

    {
        const PDFStylePainterGuard statusGuard(painter);
        painter->setPen(PDFStylePainter::color(Helper::text(state, Helper::statusColor(row.statusTone))));
        for (int i = 0; i < drawnLines; ++i)
        {
            layout->lineAt(i).draw(painter, statusRect.topLeft());
        }
    }

    if (isTruncated)
    {
        // The rest of the text in the last line, elided at its end
        const QTextLine last = layout->lineAt(drawnLines);
        const QString rest = (row.status + row.linkText).mid(last.textStart()).simplified();
        const QRectF lastRect(textLeft, statusTop + drawnLines * Helper::LINE_REGULAR_11, textWidth, Helper::LINE_REGULAR_11);
        Helper::drawText(painter, state, rect, lastRect, rest, smallFont, Helper::text(state, Helper::statusColor(row.statusTone)), TextKind::Interface);
    }
}

// ------------------------------------------------------------- Assistant tile

QSize PDFComponentPainter::getAssistantTileSize(const AssistantTile& tile)
{
    using Helper = PDFComponentAssistantPainterHelper;
    return QSize(qMax(Helper::TILE_WIDTH, getAssistantTileMinimumSize(tile).width()), Helper::TILE_HEIGHT);
}

QSize PDFComponentPainter::getAssistantTileMinimumSize(const AssistantTile& tile)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int logo = getAssistantAvatarSize(tile.logo).width();
    return QSize(2 * Helper::TILE_INSET + logo + 2 * Helper::TILE_GAP + Helper::TILE_MIN_TEXT + Helper::TILE_CHECK, Helper::TILE_HEIGHT);
}

void PDFComponentPainter::paintAssistantTile(QPainter* painter, const QRectF& rect, const AssistantTile& tile, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainter::Radii radii(Helper::TILE_RADIUS);
    const bool isSelected = state.isOn();

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The surface: the selection bg/accent-subtle over bg/surface with the border
    // border/focus; the pointer over an unselected tile bg/hover / bg/pressed with
    // border/strong; the focus ring outside replaces the border of an unselected tile
    PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
    if (isSelected)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgAccentSubtle);
        PDFStylePainter::strokeInside(painter, rect, radii, state.isEnabled() ? Color::BorderFocus : Color::BorderStrong, 1.0);
    }
    else
    {
        Helper::fillPointer(painter, rect, radii, state);
        if (!state.hasFocusRing())
        {
            PDFStylePainter::strokeInside(painter, rect, radii, (state.isHovered() || state.isPressed()) ? Color::BorderStrong : Color::BorderDefault, 1.0);
        }
    }
    if (state.hasFocusRing())
    {
        paintFocusRingOutside(painter, rect, radii);
    }

    const PDFStylePainterGuard contentGuard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);

    // The logo centered vertically at the leading side
    const QSize logoSize = getAssistantAvatarSize(tile.logo);
    const int circle = Helper::extent(tile.logo.extent);
    const QRectF logoRect(rect.left() + Helper::TILE_INSET, rect.top() + qFloor((rect.height() - circle) / 2.0), logoSize.width(), logoSize.height());
    paintAssistantAvatar(painter, state.visualRect(rect, logoRect), tile.logo, State(state.flags & QStyle::State_Enabled, state.direction));

    // The check of the selected tile at the trailing side
    qreal textRight = rect.right() - Helper::TILE_INSET;
    if (isSelected)
    {
        const QRectF check = Helper::line(rect, textRight - Helper::TILE_CHECK, Helper::TILE_CHECK, Helper::TILE_CHECK);
        PDFStylePainter::icon(painter, state.visualRect(rect, check), QStringLiteral("check"), Helper::icon(state, Color::IconAccent));
        textRight = check.left() - Helper::TILE_GAP;
    }

    // The name and the vendor (two lines at most), the block centered vertically
    const qreal textLeft = rect.left() + Helper::TILE_INSET + logoSize.width() + Helper::TILE_GAP;
    const qreal textWidth = qMax<qreal>(0.0, textRight - textLeft);
    const QFont smallFont = Helper::smallFont();
    const QStringList vendorLines = wrapText(tile.vendor, smallFont, textWidth, Helper::TILE_VENDOR_LINES);
    const int blockHeight = Helper::LINE_SEMIBOLD_13 + (vendorLines.isEmpty() ? 0 : Helper::TILE_TEXT_GAP + int(vendorLines.size()) * Helper::LINE_REGULAR_11);
    const qreal top = rect.top() + qFloor((rect.height() - blockHeight) / 2.0);

    Helper::drawText(painter, state, rect, QRectF(textLeft, top, textWidth, Helper::LINE_SEMIBOLD_13), tile.name, Helper::nameFont(),
                     Helper::text(state, Color::TextPrimary), TextKind::Data);

    const Qt::LayoutDirection vendorDirection = getDataTextDirection(tile.vendor);
    const QColor vendorColor = PDFStylePainter::color(Helper::text(state, Color::TextMuted));
    for (int i = 0; i < vendorLines.size(); ++i)
    {
        const QRectF lineRect(textLeft, top + Helper::LINE_SEMIBOLD_13 + Helper::TILE_TEXT_GAP + i * Helper::LINE_REGULAR_11, textWidth, Helper::LINE_REGULAR_11);
        drawDataText(painter, state.visualRect(rect, lineRect), int(state.leadingAlignment() | Qt::AlignVCenter | Qt::TextSingleLine), vendorLines.at(i), smallFont,
                     vendorColor, vendorDirection);
    }
}

// ------------------------------------------------------------------- Code box

QFont PDFComponentPainter::getCodeBoxFont(bool monospace)
{
    return monospace ? PDFThemeFonts::getMonospaceFont(QFont::Normal, 12) : PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
}

int PDFComponentPainter::getCodeBoxLineHeight()
{
    return PDFComponentAssistantPainterHelper::CODE_LINE;
}

int PDFComponentPainter::getCodeBoxLineCount(const CodeBox& box, qreal textWidth)
{
    using Helper = PDFComponentAssistantPainterHelper;

    // One line at least (an empty text keeps the line of the box)
    if (!(textWidth > 0.0) || !qIsFinite(textWidth))
    {
        return 1;
    }

    const QFont font = getCodeBoxFont(box.monospace);
    const QTextOption option = Helper::codeTextOption();
    int lineCount = 0;
    const QStringList paragraphs = Helper::codeParagraphs(box.text);
    for (const QString& paragraph : paragraphs)
    {
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        int paragraphLines = 0;
        while (true)
        {
            QTextLine textLine = layout.createLine();
            if (!textLine.isValid())
            {
                break;
            }
            textLine.setLineWidth(textWidth);
            ++paragraphLines;
        }
        layout.endLayout();
        lineCount += qMax(1, paragraphLines);
    }
    return qMax(1, lineCount);
}

int PDFComponentPainter::getCodeBoxHeight(const CodeBox& box, int width)
{
    using Helper = PDFComponentAssistantPainterHelper;
    const int lineCount = getCodeBoxLineCount(box, Helper::codeTextWidth(Helper::validWidth(width)));
    return 2 * Helper::CODE_VERTICAL + Helper::codeContentHeight(lineCount);
}

QSize PDFComponentPainter::getCodeBoxSize(const CodeBox& box)
{
    using Helper = PDFComponentAssistantPainterHelper;

    // The longest line of the text (at most the width of the page of the design)
    const QFont font = getCodeBoxFont(box.monospace);
    qreal longest = 0.0;
    const QStringList paragraphs = Helper::codeParagraphs(box.text);
    for (const QString& paragraph : paragraphs)
    {
        longest = qMax(longest, PDFStylePainter::textWidth(paragraph.left(Helper::CODE_MAX_HINT), font));
    }

    const int extra = Helper::CODE_LEADING + Helper::CODE_GAP + Helper::CODE_BUTTON + Helper::CODE_TRAILING;
    const int width = qBound(getCodeBoxMinimumSize(box).width(), qCeil(longest) + 1 + extra, Helper::CODE_MAX_HINT);
    return QSize(width, getCodeBoxHeight(box, width));
}

QSize PDFComponentPainter::getCodeBoxMinimumSize(const CodeBox& box)
{
    using Helper = PDFComponentAssistantPainterHelper;
    Q_UNUSED(box);
    return QSize(Helper::CODE_LEADING + Helper::MIN_TEXT + Helper::CODE_GAP + Helper::CODE_BUTTON + Helper::CODE_TRAILING, 2 * Helper::CODE_VERTICAL + Helper::CODE_BUTTON);
}

QRectF PDFComponentPainter::getCodeBoxTextRect(const QRectF& rect, const CodeBox& box, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }

    const qreal textWidth = qMax<qreal>(0.0, Helper::codeTextWidth(rect.width()));
    const int lineCount = getCodeBoxLineCount(box, textWidth);
    const int textHeight = lineCount * Helper::CODE_LINE;
    const qreal top = rect.top() + Helper::CODE_VERTICAL + qMax(0, (Helper::CODE_BUTTON - textHeight) / 2);
    return state.visualRect(rect, QRectF(rect.left() + Helper::CODE_LEADING, top, textWidth, textHeight));
}

QRectF PDFComponentPainter::getCodeBoxButtonRect(const QRectF& rect, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    if (!Helper::isDrawable(rect))
    {
        return QRectF();
    }
    return state.visualRect(rect, QRectF(rect.right() - Helper::CODE_TRAILING - Helper::CODE_BUTTON, rect.top() + Helper::CODE_VERTICAL, Helper::CODE_BUTTON, Helper::CODE_BUTTON));
}

void PDFComponentPainter::paintCodeBox(QPainter* painter, const QRectF& rect, const CodeBox& box, const State& state)
{
    using Helper = PDFComponentAssistantPainterHelper;
    using Color = PDFThemeColors::Color;

    if (!painter || !Helper::isDrawable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);
    const PDFStylePainter::Radii radii(Helper::CODE_RADIUS);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    PDFStylePainter::fill(painter, rect, radii, Color::BgSubtle);
    PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
    if (state.hasFocusRing())
    {
        // The text has the keyboard focus (a field draws its ring inside)
        paintFocusRingInside(painter, rect, radii);
    }

    if (!box.paintNested)
    {
        return;
    }

    painter->setClipRect(rect, Qt::IntersectClip);
    Helper::drawCopyButton(painter, getCodeBoxButtonRect(rect, state), State(state.flags & QStyle::State_Enabled, state.direction));

    // The text in lines of 18, wrapped at any character; its own direction (data)
    const QRectF textRect = getCodeBoxTextRect(rect, box, state);
    if (textRect.isEmpty() || box.text.isEmpty())
    {
        return;
    }

    QTextOption option = Helper::codeTextOption();
    option.setTextDirection(getDataTextDirection(box.text));
    option.setAlignment(state.leadingAlignment());

    const PDFStylePainterGuard textGuard(painter);
    painter->setPen(PDFStylePainter::color(Helper::text(state, Color::TextPrimary)));
    const QFont font = getCodeBoxFont(box.monospace);
    int lineIndex = 0;
    const QStringList paragraphs = Helper::codeParagraphs(box.text);
    for (const QString& paragraph : paragraphs)
    {
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        int paragraphLines = 0;
        while (true)
        {
            QTextLine textLine = layout.createLine();
            if (!textLine.isValid())
            {
                break;
            }
            textLine.setLineWidth(textRect.width());
            textLine.setPosition(QPointF(0.0, (lineIndex + paragraphLines) * Helper::CODE_LINE + qFloor((Helper::CODE_LINE - textLine.height()) / 2.0)));
            ++paragraphLines;
        }
        layout.endLayout();
        layout.draw(painter, textRect.topLeft());
        lineIndex += qMax(1, paragraphLines);

        if (textRect.top() + lineIndex * Helper::CODE_LINE > rect.bottom())
        {
            break;
        }
    }
}

}   // namespace pdf
