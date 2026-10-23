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

#ifndef PDFCOMPONENTPAINTER_H
#define PDFCOMPONENTPAINTER_H

#include "pdffluentglobal.h"
#include "pdfaccenthue.h"
#include "pdfpreviewpainter.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QMargins>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QStyle>

#include <limits>
#include <optional>

class QPainter;

namespace pdf
{

/// Drawing of the components of the design (Figma page Components). Each
/// component has one drawing function, which takes the description of its
/// content and its state, and the size computations of the component. The
/// same function draws the widget (in its paint event), the samples of the
/// gallery (every variant without the mouse), the delegates of the views and
/// the probes of the tests. This class is the single place with the geometry
/// constants of the cards of the specification (components_spec.md).
///
/// All functions tolerate a null painter, an empty or negative rectangle, empty
/// texts, unknown icon names and invalid values of the enumerations. They turn
/// the antialiasing on themselves (a widget and a sample of the gallery look the
/// same), restore the state of the painter and take the colors of the active
/// theme at the time of drawing. They draw only into the given rectangle, with
/// two exceptions, for which the caller reserves the space around it:
/// - a focus ring outside of the shape (the flag State_HasFocus of a component
///   with the ring outside, see \p State) lies 2 px around the rectangle,
/// - the drop shadow of a page thumbnail lies around its frame
///   (\p getPageThumbnailShadowMargins).
///
/// The functions are to be called only from the thread of the user interface
/// (a paint event, a delegate, a picture of the thread of the application):
/// they read the active theme of PDFThemeManager, draw the icons and shadows
/// of PDFStylePainter (QPixmapCache, shared SVG renderers) and keep their own
/// caches without a lock. A picture of a worker thread is to be drawn by the
/// application itself and passed to a component as a pixmap.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFComponentPainter
{
public:
    PDFComponentPainter() = delete;

    /// State of a drawn component. The flags of the style are interpreted so:
    /// - State_Enabled: the component is enabled (disabled components have no
    ///   hover, press or focus, texts text/disabled and icons icon/disabled),
    /// - State_MouseOver: the pointer is over the component,
    /// - State_Sunken: the component is pressed,
    /// - State_HasFocus: the focus ring is visible (keyboard focus). A component,
    ///   whose ring lies outside of its shape (Star button, Text link, Color
    ///   swatch, cards), draws it outside of the rectangle; a widget leaves the
    ///   flag out, because the focus frame of the style draws that ring,
    /// - State_On or State_Selected: the component is selected / pinned / checked.
    ///
    /// The default state is enabled. A state built from flags is disabled, unless
    /// the flags contain State_Enabled (the state of a QStyleOption has it).
    struct PDF4QTLIBFLUENTSHARED_EXPORT State
    {
        State() = default;
        explicit State(QStyle::State flags, Qt::LayoutDirection direction = Qt::LeftToRight);

        QStyle::State flags = QStyle::State_Enabled;
        Qt::LayoutDirection direction = Qt::LeftToRight;

        bool isEnabled() const { return flags.testFlag(QStyle::State_Enabled); }
        bool isHovered() const { return isEnabled() && flags.testFlag(QStyle::State_MouseOver); }
        bool isPressed() const { return isEnabled() && flags.testFlag(QStyle::State_Sunken); }
        bool hasFocusRing() const { return isEnabled() && flags.testFlag(QStyle::State_HasFocus); }
        bool isOn() const { return isChecked() || isSelected(); }
        bool isChecked() const { return flags.testFlag(QStyle::State_On); }        ///< checked / pinned (State_On)
        bool isSelected() const { return flags.testFlag(QStyle::State_Selected); } ///< selected item of a view (State_Selected)
        bool isRightToLeft() const { return direction == Qt::RightToLeft; }

        /// Returns the rectangle given in the left-to-right layout of the outer
        /// rectangle placed in the layout direction of the state (mirrored for RTL)
        QRectF visualRect(const QRectF& outer, const QRectF& logical) const;

        /// Returns the absolute horizontal alignment of the leading edge (left
        /// for LTR, right for RTL). It is absolute (Qt::AlignAbsolute), so the
        /// layout direction of the painter does not mirror it again.
        Qt::Alignment leadingAlignment() const { return (isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute; }

        /// Returns the absolute horizontal alignment of the trailing edge (right for LTR, left for RTL)
        Qt::Alignment trailingAlignment() const { return (isRightToLeft() ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute; }
    };

    /// Sets the layout direction of the state to the painter for the lifetime of
    /// the guard and restores the previous one (the base direction of the texts
    /// of a component is the direction of its state, never guessed from the text
    /// and never taken from the painter, which the caller has begun).
    class PDF4QTLIBFLUENTSHARED_EXPORT DirectionGuard
    {
    public:
        explicit DirectionGuard(QPainter* painter, const State& state);
        ~DirectionGuard();

        DirectionGuard(const DirectionGuard&) = delete;
        DirectionGuard& operator=(const DirectionGuard&) = delete;

    private:
        QPainter* m_painter;
        Qt::LayoutDirection m_direction = Qt::LeftToRight;
    };

    // ------------------------------------------------------------------ helpers

    /// Returns the rectangle of the given size centered in the outer rectangle,
    /// its top left corner lies on a whole logical pixel
    static QRectF alignedCentered(const QRectF& outer, const QSizeF& size);

    /// Returns the text elided at its end (\p mode Qt::ElideRight) or in the
    /// middle (Qt::ElideMiddle), so it fits the width. The spaces before the
    /// ellipsis are left out ("jumps over the…", as the design). An empty text
    /// is returned for a width, which is not positive.
    static QString elidedText(const QString& text, const QFont& font, qreal width, Qt::TextElideMode mode = Qt::ElideRight);

    /// Splits the text into lines of the width (at the word boundaries, a word
    /// longer than the width anywhere), at most \p maxLines lines; the last
    /// allowed line is elided at its end, when the rest of the text does not fit.
    /// A line break of the text starts a new line, the spaces at the ends of the
    /// lines are left out. An empty list is returned for an empty text, a width,
    /// which is not positive, and \p maxLines, which is not positive.
    static QStringList wrapText(const QString& text, const QFont& font, qreal width, int maxLines = std::numeric_limits<int>::max());

    /// Returns the width of the widest part of the text, which \p wrapText never
    /// breaks (a word between two opportunities of a line break of Unicode,
    /// without its trailing spaces). A text wrapped at this width or wider is
    /// broken only between the words. Zero is returned for an empty text.
    static qreal getLongestWordWidth(const QString& text, const QFont& font);

    /// Returns the base direction of a text, which is data: the direction of its
    /// first strong character (rules P2 and P3 of UAX #9, as the isolate FSI),
    /// left-to-right for a text without a strong character (numbers, dates).
    ///
    /// Texts of the components are of two kinds (components.md 6.6). A text of
    /// the user interface (a label, a title, a translated message) has the base
    /// direction of the component (\p State::direction). A text, which is data
    /// (a name of a file, a path, a name of a person, a version, a number, a
    /// value of a property, content of the document or of the user), keeps its
    /// own direction in any layout, it is isolated from the direction of the
    /// component: "Annual_Report_2026.pdf" elided in a right-to-left row is
    /// "Annual_Report_20…", not "…Annual_Report_20". Both kinds keep the
    /// alignment of the component (the leading edge of the state).
    static Qt::LayoutDirection getDataTextDirection(const QString& text);

    /// Draws a text, which is data (see \p getDataTextDirection): the painter
    /// gets the base direction of the text for the time of drawing, the alignment
    /// stays absolute (a relative horizontal alignment is taken in the direction,
    /// which the painter has when the function is called). The caller elides the
    /// text before (\p elidedText, \p elidedFileName), so the ellipsis lies at the
    /// end of the text in its own direction. The lines of one wrapped text share
    /// the direction of the whole text: pass it as \p direction (LeftToRight or
    /// RightToLeft; Qt::LayoutDirectionAuto takes the direction of \p text).
    static void drawDataText(QPainter* painter, const QRectF& rect, int flags, const QString& text, const QFont& font, const QColor& color,
                             Qt::LayoutDirection direction = Qt::LayoutDirectionAuto);

    /// Kind of a text of a component (components.md 6.6)
    enum class TextKind
    {
        Interface,  ///< Text of the user interface: the base direction of the component
        Data,       ///< Text, which is data: its own direction, elided at its end
        FileName    ///< Name of a file: data elided in the middle, the extension stays
    };

    /// Returns the text elided to the width by its kind: a text of the user
    /// interface and data at its end (\p elidedText), a name of a file or a
    /// path in the middle, so its extension stays visible (\p elidedFileName) -
    /// the elision of \p drawElidedText
    static QString elidedTextOfKind(const QString& text, const QFont& font, qreal width, TextKind kind);

    /// Draws a text of a single line, elided to the width of the rectangle, into
    /// the visual (already mirrored) rectangle, centered vertically, aligned to
    /// the leading edge of the state (\p isTrailing: to its trailing edge). A text
    /// of data keeps the direction of the whole text, also when the elision has
    /// cut its first strong character off. The single place of the elision and
    /// of the direction of the single-line texts of all drawing functions.
    static void drawElidedText(QPainter* painter, const State& state, const QRectF& visualRect, const QString& text, const QFont& font,
                               PDFThemeColors::Color color, TextKind kind = TextKind::Interface, bool isTrailing = false);

    /// Draws the whole picture into the target rectangle (stretched to it). A
    /// picture larger than the target is drawn from a copy scaled smoothly to
    /// the pixels of the device, which is kept in QPixmapCache, so a repaint of
    /// a large picture of the application (a page rendered at 150 dpi, the
    /// picture of a video) is not scaled again and a strong downscale does not
    /// alias. Nothing is drawn for a null painter, a null picture or an empty or
    /// invalid target; a huge or non-finite scale of the painter draws the
    /// picture itself.
    static void drawScaledPixmap(QPainter* painter, const QRectF& target, const QPixmap& pixmap);

    /// Draws the focus ring of 2 px outside of the shape (outside focus ring of
    /// the components, which the focus frame of the style draws for a widget)
    static void paintFocusRingOutside(QPainter* painter, const QRectF& shape, const PDFStylePainter::Radii& radii);

    /// Draws the focus ring of 2 px inside of the shape (rows, tags, fields)
    static void paintFocusRingInside(QPainter* painter, const QRectF& shape, const PDFStylePainter::Radii& radii);

    // ------------------------------------------------------------ rows, cards

    /// Distances of the nested widgets of a row from its edges (12 from the
    /// trailing edge, given for the left-to-right layout)
    static QMargins getRowTrailingMargins();

    /// Gap between two nested widgets of a row
    static int getRowTrailingSpacing();

    /// Radius of a card (shape of its focus ring outside)
    static qreal getCardRadius();

    /// Distances of the nested widgets of a card from its edges (12 from the trailing edge)
    static QMargins getCardTrailingMargins();

    /// Gap between two nested widgets of a card
    static int getCardTrailingSpacing();

    // --------------------------------------------------------------- Tool icon

    /// Diameter of the badge of a tool (component Tool icon 89:1289, axis Size)
    enum class BadgeSize
    {
        Small = 28,     ///< Tool choice, title of a tool, search results
        Medium = 32,    ///< Rows and cards
        Large = 44      ///< Header of a workspace
    };

    /// Content of the badge of a tool: a full circle of the hue with an icon
    /// of half of its diameter, the stroke of the icon is 2 px at each size.
    struct ToolBadge
    {
        QString iconName;
        PDFAccentHue hue = PDFAccentHue::Blue;
        BadgeSize size = BadgeSize::Medium;
    };

    /// Returns the valid size (an invalid value is replaced by Medium)
    static BadgeSize normalized(BadgeSize size);

    /// Returns the valid hue (an invalid value is replaced by Neutral)
    static PDFAccentHue normalized(PDFAccentHue hue);

    /// Returns the diameter of the badge
    static int getBadgeDiameter(BadgeSize size);

    /// Returns the extent of the icon of the badge (half of the diameter)
    static int getBadgeIconExtent(BadgeSize size);

    /// Returns the size of the badge
    static QSize getToolBadgeSize(const ToolBadge& badge);

    /// Draws the badge centered in the rectangle (smaller, if the rectangle is smaller)
    static void paintToolBadge(QPainter* painter, const QRectF& rect, const ToolBadge& badge, const State& state);

    // --------------------------------------------------------------------- Tag

    /// Content of a tag (component Tag 116:2353): a pill with a label Medium 11
    /// of a fixed line of 13 px, so the tag is 21 px high for any character.
    struct Tag
    {
        QString text;
    };

    /// Returns the font of the label of a tag
    static QFont getTagFont();

    /// Returns the size of the tag
    static QSize getTagSize(const Tag& tag);

    /// Draws the tag into the rectangle. Hover and press lay bg/hover / bg/pressed
    /// over the fill bg/subtle, the border stays. The focus ring lies inside and
    /// replaces the border. A too narrow rectangle elides the label.
    static void paintTag(QPainter* painter, const QRectF& rect, const Tag& tag, const State& state);

    // -------------------------------------------------------------- Icon badge

    /// Content of an icon badge (icon-circle of the cards of What's New, About,
    /// Support and Sponsor, logo-box of About, the circles of the empty states;
    /// frames of Main page, not a component of Figma): a box of \p extent with
    /// the fill, an optional border (1 px inside) and the radius (a negative
    /// radius is a circle), the icon of \p iconExtent (or the logo of the
    /// application) in its center. A badge without the fill is only the icon. The
    /// colors are tokens; for a hue use PDFAccentHueColors::getColor. Without states;
    /// a disabled badge has the icon icon/disabled.
    struct IconBadge
    {
        QString iconName;
        int extent = 40;
        int iconExtent = 20;
        qreal radius = -1.0;    ///< Radius of the box, a negative value = a circle
        std::optional<PDFThemeColors::Color> fill = PDFThemeColors::BgSubtle;
        std::optional<PDFThemeColors::Color> border;
        PDFThemeColors::Color iconColor = PDFThemeColors::IconDefault;
        bool logo = false;      ///< Draw the logo of the application (PDFPreviewPainter::paintLogo) instead of the icon
    };

    /// Returns the size of the badge (extent x extent, a negative extent is 0)
    static QSize getIconBadgeSize(const IconBadge& badge);

    /// Draws the badge centered in the rectangle (it keeps its size, a smaller
    /// rectangle makes it smaller)
    static void paintIconBadge(QPainter* painter, const QRectF& rect, const IconBadge& badge, const State& state);

    // ------------------------------------------------------------ Status badge

    /// Content of a status badge (badge-up-to-date of the release card of
    /// What's New 103:1951; a frame of Main page): a pill of the background and
    /// the border of the hue, the icon 12 and the text Semi Bold 11 in the icon
    /// color of the hue; paddings 3 / 10 / 3 / 8 inside of the border, gap 5,
    /// height 21. Announced as a text.
    struct StatusBadge
    {
        QString text;
        QString iconName = QStringLiteral("check");
        PDFAccentHue hue = PDFAccentHue::Green;
    };

    /// Returns the size of the badge (its text is not elided)
    static QSize getStatusBadgeSize(const StatusBadge& badge);

    /// Draws the badge into the rectangle (a narrower rectangle elides the text)
    static void paintStatusBadge(QPainter* painter, const QRectF& rect, const StatusBadge& badge, const State& state);

    // --------------------------------------------------------------- Fact pill

    /// Content of a fact of a hero card (pill-… of About 111:2226 and Sponsor
    /// 122:2954; frames of Main page): a pill bg/surface with the border
    /// border/default, the icon 13 icon/muted and the label Medium 12
    /// text/secondary; paddings 9 / 11, gap 6, height 25. Not interactive.
    struct FactPill
    {
        QString iconName;
        QString text;
    };

    /// Returns the size of the pill (its text is not elided)
    static QSize getFactPillSize(const FactPill& pill);

    /// Draws the pill into the rectangle (a narrower rectangle elides the label)
    static void paintFactPill(QPainter* painter, const QRectF& rect, const FactPill& pill, const State& state);

    // ------------------------------------------------------------------ Avatar

    /// Content of an avatar of a person (avatar of About and Sponsor; frames of
    /// Main page): a circle of the background of the hue with the border of the
    /// hue and the initials Bold in the icon color of the hue (at most three
    /// characters are drawn)
    struct Avatar
    {
        QString initials;
        int extent = 40;
        int fontPixelSize = 14;
        PDFAccentHue hue = PDFAccentHue::Blue;
    };

    /// Returns the size of the avatar (extent x extent)
    static QSize getAvatarSize(const Avatar& avatar);

    /// Draws the avatar centered in the rectangle
    static void paintAvatar(QPainter* painter, const QRectF& rect, const Avatar& avatar, const State& state);

    // -------------------------------------------------------------- Status row

    /// Content of a row of a list of states (priority-… of Sponsor 123:2869,
    /// "What the time goes into now"; a frame of Main page): the dot 8 (bg/accent
    /// for an active item, border/strong otherwise), the title Semi Bold 13
    /// text/primary and the subtitle Regular 11 text/muted (both elided, data) and
    /// the status as a Tag at the trailing side; paddings 10 / 8, gaps 10,
    /// height 46. Without interaction.
    struct StatusRow
    {
        QString title;
        QString subtitle;
        QString status;
        bool active = false;
    };

    /// Returns the size of the row (the texts are not elided)
    static QSize getStatusRowSize(const StatusRow& row);

    /// Returns the minimal size of the row (the texts are elided, the tag is not)
    static QSize getStatusRowMinimumSize(const StatusRow& row);

    /// Draws the row into the rectangle
    static void paintStatusRow(QPainter* painter, const QRectF& rect, const StatusRow& row, const State& state);

    // ------------------------------------------------------------- Tinted card

    /// Draws the surface of a card of a hue (the release card of the running
    /// version and the hero cards accent/blue, Thank you and the promise of
    /// Sponsor accent/green; frames of Main page): the background and the border
    /// of the hue, radius 12. Without a hue it is the plain card bg/surface with
    /// border/default.
    static void paintTintedCard(QPainter* painter, const QRectF& rect, std::optional<PDFAccentHue> hue);

    /// Radius of a tinted card
    static constexpr qreal TINTED_CARD_RADIUS = 12.0;

    // --------------------------------------------------------------- Text link

    /// Size of the text of a link: Default - Semi Bold 13 (component Text link
    /// 49:152), Small - Semi Bold 12, a link inside of a text of 12 px (MIT License
    /// of app-footer 28:217). Both have the line box of 16 px.
    enum class TextLinkSize
    {
        Default,
        Small
    };

    /// Content of a link (component Text link 49:152): Semi Bold 13 (or 12) in
    /// text/accent, underlined under the pointer, focus ring outside (radius 4).
    struct TextLink
    {
        QString text;
        TextLinkSize size = TextLinkSize::Default;
    };

    /// Returns the font of a link (an unknown size is the default size)
    static QFont getTextLinkFont(bool isUnderlined = false);
    static QFont getTextLinkFont(TextLinkSize size, bool isUnderlined);

    /// Returns the size of the link
    static QSize getTextLinkSize(const TextLink& link);

    /// Returns the radius of the shape of the link (for its focus ring)
    static qreal getTextLinkRadius();

    /// Draws the link into the rectangle, a too narrow rectangle elides it
    static void paintTextLink(QPainter* painter, const QRectF& rect, const TextLink& link, const State& state);

    // ------------------------------------------------------------- Star button

    /// Size of the star (component Star button 64:569): 28 in Tool row and Quick
    /// action tile, 24 in file rows and in Pinned card. The icon has 16 px.
    enum class StarSize
    {
        Small = 24,
        Medium = 28
    };

    /// Returns the valid size (an invalid value is replaced by Medium)
    static StarSize normalized(StarSize size);

    /// Returns the size of the star button
    static QSize getStarSize(StarSize size);

    /// Returns the radius of the shape of the star button
    static qreal getStarRadius();

    /// Draws the star button centered in the rectangle; the pinned star is
    /// the state On (filled star icon/sun), the unpinned one an outline icon/muted.
    /// Hover and press lay bg/hover / bg/pressed over the own surface bg/surface.
    static void paintStar(QPainter* painter, const QRectF& rect, StarSize size, const State& state);

    // ------------------------------------------------------------ Color swatch

    /// Kind of a color swatch (component Color swatch 206:805)
    enum class SwatchKind
    {
        Color,  ///< The color fills the inner circle
        None    ///< No color: the inner circle is bg/surface, crossed by a slash icon/danger
    };

    /// Content of a color swatch: the circle of 28 px with the color of 20 px in the middle
    struct ColorSwatch
    {
        QColor color;
        SwatchKind kind = SwatchKind::Color;
    };

    /// Returns the valid kind (an invalid value is replaced by Color)
    static SwatchKind normalized(SwatchKind kind);

    /// Returns the size of the swatch (28 x 28)
    static QSize getColorSwatchSize();

    /// Returns the radius of the shape of the swatch (14)
    static qreal getColorSwatchRadius();

    /// Draws the swatch centered in the rectangle. The selection (state On) is
    /// a ring bg/accent 2 px inside, the focus ring lies outside of it. Hover and
    /// press lay bg/hover / bg/pressed over the own surface bg/surface.
    static void paintColorSwatch(QPainter* painter, const QRectF& rect, const ColorSwatch& swatch, const State& state);

    // ---------------------------------------------------------- Page thumbnail

    /// Size of the frame of a page thumbnail (component Page thumbnail 78:892)
    enum class ThumbnailSize
    {
        Small,      ///< 32 x 42 (Recent item row)
        Medium,     ///< 56 x 74 (Pinned card)
        Large       ///< 80 x 106
    };

    /// Content of a page thumbnail: the frame of the page with a shadow. The
    /// picture of the page is drawn into the frame; without it the frame shows
    /// the placeholder lines of the component. A document protected by a
    /// password (\p locked) has no picture: the frame is bg/subtle with the icon
    /// lock (icon/muted, 14 / 20 / 28 px by the size) in its center - a proposal
    /// consistent with the tokens, Figma has no frame of this state.
    struct PageThumbnail
    {
        ThumbnailSize size = ThumbnailSize::Small;
        QPixmap pixmap;
        bool locked = false;    ///< Protected by a password: the lock instead of the picture
    };

    /// Returns the valid size (an invalid value is replaced by Small)
    static ThumbnailSize normalized(ThumbnailSize size);

    /// Returns the size of the frame of the thumbnail (without the shadow)
    static QSize getPageThumbnailSize(ThumbnailSize size);

    /// Returns the margins of the shadow around the frame (left 2, top 1, right 2,
    /// bottom 3). The shadow takes no space in a layout, the frame has the size
    /// of the component.
    static QMargins getPageThumbnailShadowMargins();

    /// Draws the thumbnail with its shadow: the shadow outside of the frame
    /// (\p getPageThumbnailShadowMargins) and the frame into its rectangle
    static void paintPageThumbnail(QPainter* painter, const QRectF& frame, const PageThumbnail& thumbnail, const State& state);

    /// Draws only the shadow of a thumbnail, whose frame has the rectangle
    static void paintPageThumbnailShadow(QPainter* painter, const QRectF& frame);

    /// Draws only the frame of a thumbnail into its rectangle (without the
    /// shadow). The placeholder lines are mirrored for right-to-left layouts.
    static void paintPageThumbnailFrame(QPainter* painter, const QRectF& frame, const PageThumbnail& thumbnail, const State& state);

    // ------------------------------------------------------------- Icon button

    /// Content of a small button with an icon only (component Icon button
    /// 706:147674): no background at rest, bg/hover and bg/pressed under the
    /// pointer, the icon icon/muted, icon/default (hover) and icon/strong
    /// (pressed), or the overridden color in all enabled states, icon/disabled.
    struct IconButton
    {
        QString iconName;
        int extent = 24;        ///< Size of the square button (24, 28, or 20 for Tab close button)
        int iconExtent = 14;    ///< Size of the icon (14, 16, or 12)
        qreal radius = 6.0;     ///< Radius of the shape (6, or 4 for Tab close button)
        std::optional<PDFThemeColors::Color> iconColor;
    };

    /// Returns the color of the icon of the button in the state: icon/disabled for
    /// a disabled button, the overridden color in all enabled states (an invalid
    /// value of the enumeration is ignored), icon/strong when pressed, icon/default
    /// under the pointer and icon/muted at rest
    static PDFThemeColors::Color getIconButtonColor(const IconButton& button, const State& state);

    /// Draws the icon button centered in the rectangle (the focus ring lies outside)
    static void paintIconButton(QPainter* painter, const QRectF& rect, const IconButton& button, const State& state);

    // ------------------------------------------------------------- More button

    /// Returns the size of the menu button of a row or a card (icon more-vertical 16 x 16)
    static QSize getMoreButtonSize();

    /// Returns the radius of the shape of the menu button (its focus ring lies outside)
    static qreal getMoreButtonRadius();

    /// Draws the menu button (the icon more-vertical, which is not an Icon button
    /// in the design): icon/muted at rest, icon/default, when the row or the card
    /// (or the button) is under the pointer or pressed, icon/disabled.
    static void paintMoreButton(QPainter* painter, const QRectF& rect, const State& state);

    // ---------------------------------------------------------------- Link row

    /// Content of a link row (component Link row 49:190): an icon, a title Semi
    /// Bold 13 with a subtitle Regular 11 and a chevron. The texts are not elided
    /// in the design (the minimal size holds them), a narrower rectangle elides them.
    struct LinkRow
    {
        QString title;
        QString subtitle;
        QString iconName;
        std::optional<PDFThemeColors::Color> iconColor;    ///< Color of the icon (icon/default by default)
    };

    /// Returns the size of the link row (328 x 50 or wider for long texts)
    static QSize getLinkRowSize(const LinkRow& row);

    /// Returns the minimal size of the link row (the texts are not elided)
    static QSize getLinkRowMinimumSize(const LinkRow& row);

    /// Draws the link row into the rectangle
    static void paintLinkRow(QPainter* painter, const QRectF& rect, const LinkRow& row, const State& state);

    // --------------------------------------------------------- Recent file row

    /// Status of a recent file
    enum class FileStatus
    {
        Available,  ///< The file exists
        Missing     ///< The file was moved or deleted: dimmed name and icon, the badge "No longer exists" instead of the time
    };

    /// Content of a row of the recent files (component Recent file row 49:280).
    /// The nested star (24) and the menu button lie at the trailing side; a widget
    /// with real nested widgets sets \p paintNested to false (their place is kept).
    struct RecentFileRow
    {
        QString fileName;
        QString path;
        QString time;
        FileStatus status = FileStatus::Available;
        bool starVisible = true;
        bool pinned = false;
        bool paintNested = true;
    };

    /// Returns the valid status (an invalid value is replaced by Available)
    static FileStatus normalized(FileStatus status);

    /// Returns the name of a file elided in the middle, so it fits the width.
    /// The extension stays visible ("Technical_Doc…ation_v2.pdf"). The extension
    /// is the part after the last dot of the name, when it has 1 to 8 characters
    /// without a white space ("Minutes 2026.final draft" has none). A name
    /// without an extension, and a name, whose extension with an ellipsis before
    /// it does not fit, is elided at its end.
    static QString elidedFileName(const QString& fileName, const QFont& font, qreal width);

    /// Returns the size of the row (381 x 51)
    static QSize getRecentFileRowSize(const RecentFileRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getRecentFileRowMinimumSize(const RecentFileRow& row);

    /// Returns the text of the badge of a missing file ("No longer exists", translated)
    static QString getMissingFileText();

    /// Returns the size of the badge of a missing file (Tag font, padding 8,
    /// height 19, the badge of Recent file row 49:280 in the state Missing)
    static QSize getMissingFileBadgeSize();

    /// Draws the badge of a missing file into the rectangle: accent/red/bg,
    /// radius 10 and the text text/danger centered (text/disabled when disabled)
    static void paintMissingFileBadge(QPainter* painter, const QRectF& rect, const State& state);

    /// Draws the row into the rectangle
    static void paintRecentFileRow(QPainter* painter, const QRectF& rect, const RecentFileRow& row, const State& state);

    // --------------------------------------------------------- Recent tool row

    /// Content of a recently used tool (component Recent tool row 50:279):
    /// the badge of the tool, its name and the time of the last use, a chevron
    struct RecentToolRow
    {
        QString name;
        QString lastUsed;
        QString iconName;
        PDFAccentHue hue = PDFAccentHue::Amber;
    };

    /// Returns the size of the row (342 x 48)
    static QSize getRecentToolRowSize(const RecentToolRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getRecentToolRowMinimumSize(const RecentToolRow& row);

    /// Draws the row into the rectangle
    static void paintRecentToolRow(QPainter* painter, const QRectF& rect, const RecentToolRow& row, const State& state);

    // ----------------------------------------------------------- Workspace row

    /// Content of a saved workspace (component Workspace row 64:544): the badge
    /// (Violet with the icon layers by default), the name, the meta text and the
    /// menu button at the trailing side (\p paintNested as in RecentFileRow)
    struct WorkspaceRow
    {
        QString name;
        QString meta;
        QString iconName = QStringLiteral("layers");
        PDFAccentHue hue = PDFAccentHue::Violet;
        bool paintNested = true;
    };

    /// Returns the size of the row (344 x 52)
    static QSize getWorkspaceRowSize(const WorkspaceRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getWorkspaceRowMinimumSize(const WorkspaceRow& row);

    /// Returns the distances of the nested menu button from the edges (8 from the trailing edge)
    static QMargins getWorkspaceRowTrailingMargins();

    /// Draws the row into the rectangle
    static void paintWorkspaceRow(QPainter* painter, const QRectF& rect, const WorkspaceRow& row, const State& state);

    // ---------------------------------------------------------------- Tool row

    /// Content of a tool of the catalog (component Tool row 89:1330): the badge,
    /// the name and the description, an optional tag (shown, when its text is not
    /// empty) and the star (28) at the trailing side (\p paintNested as in RecentFileRow)
    struct ToolRow
    {
        QString name;
        QString description;
        QString iconName;
        PDFAccentHue hue = PDFAccentHue::Blue;
        QString tag;
        bool pinned = false;
        bool paintNested = true;
    };

    /// Returns the size of the row (380 x 48)
    static QSize getToolRowSize(const ToolRow& row);

    /// Returns the minimal size of the row (the texts are elided, the tag is not)
    static QSize getToolRowMinimumSize(const ToolRow& row);

    /// Returns the distances of the nested tag and star from the edges (6 from the trailing edge)
    static QMargins getToolRowTrailingMargins();

    /// Returns the gap between the nested tag and the star (10)
    static int getToolRowTrailingSpacing();

    /// Returns true, if the tag of the row is shown in the rectangle: it has a
    /// text and it does not reach the badge (a row narrower than its minimal
    /// width drops the tag, the widget hides its nested tag the same way)
    static bool isToolRowTagShown(const QRectF& rect, const ToolRow& row);

    /// Draws the row into the rectangle
    static void paintToolRow(QPainter* painter, const QRectF& rect, const ToolRow& row, const State& state);

    // ---------------------------------------------------------- Collection row

    /// Content of a collection of tools (component Collection row 89:1367). The
    /// circle of the icon has the fixed colors bg/accent-subtle and icon/accent
    /// (it is not a Tool icon, review F-32).
    struct CollectionRow
    {
        QString name;
        QString meta;
        QString iconName = QStringLiteral("layers");
    };

    /// Returns the size of the row (324 x 52)
    static QSize getCollectionRowSize(const CollectionRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getCollectionRowMinimumSize(const CollectionRow& row);

    /// Draws the row into the rectangle (the chevron is icon/default under the pointer)
    static void paintCollectionRow(QPainter* painter, const QRectF& rect, const CollectionRow& row, const State& state);

    // ----------------------------------------------------------- Tool shortcut

    /// Content of a pinned tool of the Tools panel (component Tool shortcut
    /// 192:836): a bordered tile with the badge and the name. Its focus ring
    /// lies outside (radius 10) and replaces the border.
    struct ToolShortcut
    {
        QString name;
        QString iconName;
        PDFAccentHue hue = PDFAccentHue::Blue;
    };

    /// Returns the size of the shortcut (180 x 48)
    static QSize getToolShortcutSize(const ToolShortcut& shortcut);

    /// Returns the minimal size of the shortcut (the name is elided)
    static QSize getToolShortcutMinimumSize(const ToolShortcut& shortcut);

    /// Returns the radius of the shape of the shortcut (10)
    static qreal getToolShortcutRadius();

    /// Draws the shortcut into the rectangle. The flag State_HasFocus removes the
    /// border; the focus ring outside is drawn only with \p paintFocusRing (the
    /// focus frame of the style draws it for a widget).
    static void paintToolShortcut(QPainter* painter, const QRectF& rect, const ToolShortcut& shortcut, const State& state, bool paintFocusRing = true);

    // --------------------------------------------------------------- Issue row

    /// Content of a fixed issue of a release (component Issue row 103:1931): the
    /// green status circle with an icon (check by default), the number in the
    /// column of 44 px (a longer number wraps, it is not elided), the title and
    /// the description, the date
    struct IssueRow
    {
        QString number;
        QString title;
        QString description;
        QString date;
        QString statusIconName = QStringLiteral("check");
    };

    /// Returns the size of the row (786 x 52)
    static QSize getIssueRowSize(const IssueRow& row);

    /// Returns the minimal size of the row (the texts are elided, the date is not)
    static QSize getIssueRowMinimumSize(const IssueRow& row);

    /// Draws the row into the rectangle
    static void paintIssueRow(QPainter* painter, const QRectF& rect, const IssueRow& row, const State& state);

    // ------------------------------------------------------------- Version row

    /// Content of a release of the version history (component Version row
    /// 103:1950): the rail with the dot and the line, the version, the date and
    /// the summary, which wraps (Regular 11, line 130 %). The current (installed)
    /// version has only the dot bg/accent. The selected version (the version
    /// shown by the page, also the flag State_Selected of the state) has the fill
    /// bg/accent-subtle and the version text/accent. The row is a link: hover
    /// bg/hover (selected bg/accent-subtle-hover), pressed bg/pressed (selected
    /// bg/accent-subtle-pressed), the focus ring 2 px inside, radius 8.
    struct VersionRow
    {
        QString version;
        QString date;
        QString summary;
        bool current = true;
        bool selected = false;
        bool lineVisible = true;
    };

    /// Returns the size of the row of the default width (324 x 60 for the summary of two lines)
    static QSize getVersionRowSize(const VersionRow& row);

    /// Returns the height of the row of the given width (the summary wraps)
    static int getVersionRowHeight(const VersionRow& row, int width);

    /// Returns the minimal size of the row
    static QSize getVersionRowMinimumSize(const VersionRow& row);

    /// Draws the row into the rectangle
    static void paintVersionRow(QPainter* painter, const QRectF& rect, const VersionRow& row, const State& state);

    // ---------------------------------------------------- Rendering issue row

    /// Severity of a rendering issue (the icon of the row)
    enum class IssueSeverity
    {
        Information,    ///< icon info, accent/blue/icon
        Warning,        ///< icon alert-triangle, accent/amber/icon
        Error           ///< icon x-circle, accent/red/icon
    };

    /// Content of a rendering issue of a document (issue-row of the popover
    /// Rendering issues 198:5950): the icon 16 of the severity, the page (Medium
    /// 11 text/muted, line 13) and the message below it (Regular 12
    /// text/secondary, line 15), which wraps; paddings 10 / 8, gap 10, radius 8.
    /// The row is a link to the page: hover bg/hover, pressed bg/pressed, the focus
    /// ring 2 px inside. The page and the message are data (their own direction).
    struct RenderingIssueRow
    {
        QString page;
        QString message;
        IssueSeverity severity = IssueSeverity::Warning;
    };

    /// Returns the valid severity (an invalid value is Warning)
    static IssueSeverity normalized(IssueSeverity severity);

    /// Returns the icon name and the color of the icon of the severity
    static QString getIssueSeverityIconName(IssueSeverity severity);
    static PDFThemeColors::Color getIssueSeverityIconColor(IssueSeverity severity);

    /// Returns the size of the row of the default width (406 x 46 for a message of one line)
    static QSize getRenderingIssueRowSize(const RenderingIssueRow& row);

    /// Returns the height of the row of the given width (the message wraps)
    static int getRenderingIssueRowHeight(const RenderingIssueRow& row, int width);

    /// Returns the minimal size of the row
    static QSize getRenderingIssueRowMinimumSize(const RenderingIssueRow& row);

    /// Draws the row into the rectangle
    static void paintRenderingIssueRow(QPainter* painter, const QRectF& rect, const RenderingIssueRow& row, const State& state);

    // ------------------------------------------------------------ Property row

    /// State of a property row (axis State of the component)
    enum class PropertyState
    {
        Default,    ///< At rest
        Hover,      ///< Under the pointer: bg/hover, the copy button is shown
        Copied,     ///< For a moment after the copy: a green check in the place of the copy button
        Editing     ///< The inline editor replaces the value (only an editable row)
    };

    /// Action of a property row: an Icon button 24 at the trailing side
    enum class PropertyAction
    {
        Edit,       ///< icon edit; icon/muted at rest, icon/default on the highlighted row
        Copy,       ///< icon copy, icon/default; the green check in the state Copied
        Confirm,    ///< icon check, icon/accent in all enabled states
        Cancel      ///< icon x
    };

    /// Content of a row of the document properties (component Property row
    /// 206:873): the key (132 px), the value and the actions (edit, copy, confirm,
    /// cancel - Icon buttons 24). An editable row always shows the edit button.
    /// A widget with real nested buttons and editor sets \p paintNested to false;
    /// the painter then draws only the background, the texts and the frame of the
    /// editor (their places are kept).
    struct PropertyRow
    {
        QString key;
        QString value;
        bool editable = false;
        PropertyState state = PropertyState::Default;
        bool paintNested = true;
    };

    /// Returns the valid state (an invalid value is replaced by Default; Editing
    /// of a row, which is not editable, is Default)
    static PropertyState normalized(PropertyState propertyState, bool editable);

    /// Returns the size of the row (326 x 30)
    static QSize getPropertyRowSize(const PropertyRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getPropertyRowMinimumSize(const PropertyRow& row);

    /// Returns the distances of the action buttons from the edges (4 from the trailing edge, 3 from the top)
    static QMargins getPropertyRowTrailingMargins();

    /// Returns the rectangle of the inline editor (the place of the value, 24 px
    /// high), mirrored for right-to-left layouts
    static QRectF getPropertyRowEditorRect(const QRectF& rect, const PropertyRow& row, const State& state);

    /// Returns the distance of the text of the inline editor from its edges: the
    /// ring of 2 px is counted in the layout of the field, the padding is 8, so
    /// the text starts 10 px from the edge of the field
    static int getPropertyRowEditorTextInset();

    /// Returns the font of the value (Medium 12)
    static QFont getPropertyRowValueFont();

    /// Draws the row into the rectangle (State_MouseOver is the state Hover)
    static void paintPropertyRow(QPainter* painter, const QRectF& rect, const PropertyRow& row, const State& state);

    /// Returns the valid action (an invalid value is replaced by Copy)
    static PropertyAction normalized(PropertyAction action);

    /// Returns true, if the copy action is drawn in the state of the row: under
    /// the pointer of the row (Hover), as the check of the state Copied, or when
    /// the copy button has the keyboard focus (its place is reserved otherwise)
    static bool isPropertyRowCopyShown(PropertyState propertyState, bool hasFocus);

    /// Draws an action of a property row centered in the rectangle: an Icon
    /// button 24, whose icon is icon/default at rest on the highlighted row
    /// (Hover, Copied); the copy action of the state Copied is the green check
    /// without a background. The widget of the row and \p paintPropertyRow draw
    /// the actions by this function.
    /// \param propertyState State of the row
    /// \param buttonState State of the button itself (enabled, hover, press)
    static void paintPropertyRowAction(QPainter* painter, const QRectF& rect, PropertyAction action, PropertyState propertyState, const State& buttonState);

    // ============================================== tiles and cards (step 3)
    //
    // Cards with a border, which their focus ring outside replaces (Quick action
    // tile, Folder card, Pinned card, Resource card, Video card), take the flag
    // State_HasFocus and \p paintFocusRing: with the flag the border is left out,
    // the ring itself is drawn only with \p paintFocusRing (the focus frame of the
    // style draws it around a widget). Choice cards (Tool choice, Reading theme
    // card, Color mode card) keep their content with the focus and draw the ring
    // outside, when the flag is set. A card is drawn into its rectangle; only the
    // focus ring and the shadow of a thumbnail lie outside of it.

    // ------------------------------------------------------- Progress bar

    /// Returns the valid progress: a value of 0 to 1, NaN and negative values are 0
    static qreal normalizedProgress(qreal progress);

    /// Draws the reading progress (component Progress bar 78:868) into the
    /// rectangle: the track border/default and the value bg/accent, the finished
    /// document (progress 1) accent/green/icon, the value of a disabled bar
    /// border/strong (as QProgressBar of the style). The width of the value is
    /// the progress times the width of the track, both have the radius of half
    /// of the height; progress 0 draws no value (the value is continuous, review I-32).
    /// The value fills from the leading edge of the state: from the right in a
    /// right-to-left state (as QProgressBar of the style), for every caller.
    static void paintProgressBar(QPainter* painter, const QRectF& rect, qreal progress, const State& state);

    /// Draws the enabled progress bar from the left (see the overload with the
    /// state; an overload, not a default argument: a nested class with default
    /// member initializers cannot be a default argument inside its enclosing
    /// class, CWG 1397, GCC rejects it)
    static void paintProgressBar(QPainter* painter, const QRectF& rect, qreal progress);

    // --------------------------------------------------- Quick action tile

    /// Content of a tile of the Home screen (component Quick action tile 46:431):
    /// a hued tile 88 px high with the icon in a white circle, the title Bold 14
    /// and the description Regular 12, which wrap, and the optional star (28) at
    /// the trailing side. Hover, press and the rest are one recipe of the hue.
    struct QuickActionTile
    {
        QString title;
        QString description;
        QString iconName = QStringLiteral("folder-open");
        PDFAccentHue hue = PDFAccentHue::Blue;
        bool starVisible = false;
        bool pinned = false;
        bool paintNested = true;    ///< Draw the star (false: a widget has a real star, its place is kept)
    };

    /// Returns the size of the tile (224 x 88; the width is given by the grid)
    static QSize getQuickActionTileSize(const QuickActionTile& tile);

    /// Returns the minimal size of the tile (the texts are elided)
    static QSize getQuickActionTileMinimumSize(const QuickActionTile& tile);

    /// Returns the distances of the nested star from the edges (16 from the trailing edge)
    static QMargins getQuickActionTileTrailingMargins();

    /// Draws the tile into the rectangle
    static void paintQuickActionTile(QPainter* painter, const QRectF& rect, const QuickActionTile& tile, const State& state, bool paintFocusRing = true);

    // --------------------------------------------------------- Folder card

    /// Content of a folder of the Open screen (component Folder card 63:597): the
    /// icon in a white circle, the name, the path and the meta line (Last used, or
    /// the kind of a pinned location), the menu button. Hues Blue, Neutral, Green
    /// and Violet (the standard folders and the others), one recipe of the hue.
    struct FolderCard
    {
        QString name;
        QString path;
        QString meta;
        bool metaVisible = true;
        QString iconName = QStringLiteral("folder");
        PDFAccentHue hue = PDFAccentHue::Neutral;
        bool paintNested = true;    ///< Draw the menu button
    };

    /// Returns the hue of a folder card: Blue, Neutral, Green or Violet (any other value is Neutral)
    static PDFAccentHue normalizedFolderHue(PDFAccentHue hue);

    /// Returns the size of the card (252 x 84)
    static QSize getFolderCardSize(const FolderCard& card);

    /// Returns the minimal size of the card (the texts are elided)
    static QSize getFolderCardMinimumSize(const FolderCard& card);

    /// Returns the distances of the nested menu button from the edges (11 from the trailing edge)
    static QMargins getFolderCardTrailingMargins();

    /// Draws the card into the rectangle
    static void paintFolderCard(QPainter* painter, const QRectF& rect, const FolderCard& card, const State& state, bool paintFocusRing = true);

    // --------------------------------------------------------- Pinned card

    /// Content of a pinned document of the Recent screen (component Pinned card
    /// 79:1115): the thumbnail of the first page with its shadow, the name, the
    /// path, the reading progress (text and bar) and a column of the star (24,
    /// pinned) and the menu button at the trailing side.
    struct PinnedCard
    {
        QString name;
        QString path;
        QString progressText;
        qreal progress = 0.0;       ///< Reading progress of 0 to 1
        QPixmap pixmap;             ///< Picture of the first page (the placeholder lines without it)
        bool pinned = true;
        bool paintNested = true;    ///< Draw the star and the menu button
        FileStatus status = FileStatus::Available;  ///< Missing: the name text/muted, the badge "No longer exists" instead of the progress (a proposal, no frame in Figma)
        bool locked = false;        ///< Protected by a password: the thumbnail shows the lock (PageThumbnail::locked)
    };

    /// Returns the size of the card (259 x 100)
    static QSize getPinnedCardSize(const PinnedCard& card);

    /// Returns the minimal size of the card (the texts are elided)
    static QSize getPinnedCardMinimumSize(const PinnedCard& card);

    /// Returns the distances of the nested column (star and menu button) from the edges (top 13, trailing 9)
    static QMargins getPinnedCardTrailingMargins();

    /// Returns the gap between the star and the menu button (4)
    static int getPinnedCardTrailingSpacing();

    /// Draws the card into the rectangle
    static void paintPinnedCard(QPainter* painter, const QRectF& rect, const PinnedCard& card, const State& state, bool paintFocusRing = true);

    // ------------------------------------------------ Tool category header

    /// Content of the tinted header of a category card of the Tools screen
    /// (component Tool category header 89:1424). Without states.
    struct ToolCategoryHeader
    {
        QString title;
        QString description;
        QString count;
        QString iconName = QStringLiteral("grid");
        PDFAccentHue hue = PDFAccentHue::Blue;
        qreal topRadius = 0.0;      ///< Radius of the top corners: the header lies at the top of a rounded card, which clips it in the design
    };

    /// Returns the size of the header (393 x 61)
    static QSize getToolCategoryHeaderSize(const ToolCategoryHeader& header);

    /// Returns the minimal size of the header (the title and the description are elided, the count is not)
    static QSize getToolCategoryHeaderMinimumSize(const ToolCategoryHeader& header);

    /// Draws the header into the rectangle (the bottom border is its last row)
    static void paintToolCategoryHeader(QPainter* painter, const QRectF& rect, const ToolCategoryHeader& header, const State& state);

    // ---------------------------------------------------------- Tool choice

    /// Content of a suggested tool of "Then continue with" (component Tool choice
    /// 417:1006): the badge 28 and the name, which wraps by words. The card hugs
    /// its content (z=fix/hug): 46 high for a name of one line, 48 for two lines.
    /// The lines are counted at the width of the name of the selected choice (the
    /// check takes 24 px), so the height does not change with the selection. The
    /// selected choice (state On) is bg/surface + bg/accent-subtle with the ring
    /// bg/accent 2 px inside and a check; hover and press lay bg/hover and
    /// bg/pressed over the fill in both selections. The focus ring lies outside.
    struct ToolChoice
    {
        QString name;
        QString iconName = QStringLiteral("grid");
        PDFAccentHue hue = PDFAccentHue::Blue;
    };

    /// Returns the size of the choice (154 wide, 48 high for a name of two lines, 46 for one line, in each state)
    static QSize getToolChoiceSize(const ToolChoice& choice);

    /// Returns the height of the choice of the width (the name wraps, review F1)
    static int getToolChoiceHeight(const ToolChoice& choice, int width);

    /// Returns the minimal size of the choice: the longest word of the name fits
    /// the name of the selected choice, so a word is never broken (review F3)
    static QSize getToolChoiceMinimumSize(const ToolChoice& choice);

    /// Returns the radius of the choice (10, the shape of its focus ring)
    static qreal getToolChoiceRadius();

    /// Draws the choice into the rectangle
    static void paintToolChoice(QPainter* painter, const QRectF& rect, const ToolChoice& choice, const State& state);

    // -------------------------------------------------------- Resource card

    /// Content of a card of Learn & Resources (component Resource card 50:242):
    /// the icon 24, the title Bold 13 and the description Regular 11 (lines of
    /// 14 px), which wrap; the height follows the width. Hues Neutral, Green,
    /// Amber and Violet, one recipe of the hue.
    struct ResourceCard
    {
        QString title;
        QString description;
        QString iconName = QStringLiteral("book-open");
        PDFAccentHue hue = PDFAccentHue::Neutral;
    };

    /// Returns the hue of a resource card: Neutral, Green, Amber or Violet (any other value is Neutral)
    static PDFAccentHue normalizedResourceHue(PDFAccentHue hue);

    /// Returns the size of the card of the default width (116 x 124 for the texts of two lines)
    static QSize getResourceCardSize(const ResourceCard& card);

    /// Returns the height of the card of the given width (the texts wrap)
    static int getResourceCardHeight(const ResourceCard& card, int width);

    /// Returns the minimal size of the card: the minimal width and the height of
    /// the fixed parts and of one line of each text (a layout of the height for
    /// width gives the real height)
    static QSize getResourceCardMinimumSize(const ResourceCard& card);

    /// Draws the card into the rectangle (the content lies at the top, a taller
    /// card of a row of cards keeps the space below it)
    static void paintResourceCard(QPainter* painter, const QRectF& rect, const ResourceCard& card, const State& state, bool paintFocusRing = true);

    // ------------------------------------------------------- Highlight card

    /// Content of a highlight of a release (component Highlight card 103:1894):
    /// a white bordered card with the icon in a circle of the hue, the title Bold
    /// 14 and the description Regular 12 (lines of 16 px), which wrap. Not interactive.
    struct HighlightCard
    {
        QString title;
        QString description;
        QString iconName = QStringLiteral("grid");
        PDFAccentHue hue = PDFAccentHue::Blue;
    };

    /// Returns the size of the card of the default width (259 x 137 for the description of two lines)
    static QSize getHighlightCardSize(const HighlightCard& card);

    /// Returns the height of the card of the given width (the texts wrap)
    static int getHighlightCardHeight(const HighlightCard& card, int width);

    /// Returns the minimal size of the card: the minimal width and the height of
    /// the fixed parts and of one line of each text (as Resource card)
    static QSize getHighlightCardMinimumSize(const HighlightCard& card);

    /// Draws the card into the rectangle (the content lies at the top)
    static void paintHighlightCard(QPainter* painter, const QRectF& rect, const HighlightCard& card, const State& state);

    // ----------------------------------------------------------- Video card

    /// Content of a video tutorial (component Video card 116:2418): the preview
    /// 150 px high over the inner width (the picture of the application, or the
    /// placeholder bg/muted) with the play button and the duration, the title
    /// (one line), the description (at most two lines of 16 px) and the tags.
    /// Empty tags are left out; tags, which do not fit the width, are hidden.
    struct VideoCard
    {
        QString title;
        QString description;
        QString duration;
        QStringList tags;
        QPixmap pixmap;
        bool paintNested = true;    ///< Draw the tags (false: a widget has real tags, their places are kept)
    };

    /// Returns the tags of the card, which are drawn (the tags without the empty ones)
    static QStringList getVideoCardTags(const VideoCard& card);

    /// Returns the rectangles of the tags of \p getVideoCardTags in the card of
    /// the rectangle, mirrored for right-to-left layouts; a tag, which does not
    /// fit the width of the card, has an empty rectangle
    static QList<QRectF> getVideoCardTagRects(const QRectF& rect, const VideoCard& card, const State& state);

    /// Returns the size of the card of the default width (272 x 258 for the description of two lines)
    static QSize getVideoCardSize(const VideoCard& card);

    /// Returns the height of the card of the given width (the description wraps)
    static int getVideoCardHeight(const VideoCard& card, int width);

    /// Returns the minimal size of the card
    static QSize getVideoCardMinimumSize(const VideoCard& card);

    /// Draws the card into the rectangle
    static void paintVideoCard(QPainter* painter, const QRectF& rect, const VideoCard& card, const State& state, bool paintFocusRing = true);

    // --------------------------------------------------- Reading theme card

    /// Content of a preset of the page and text colors of the Custom colors mode
    /// (component Reading theme card 156:4218): the sample "Aa" with two lines of
    /// text in the colors of the theme and the name. The colors are the tokens
    /// reading/* (\p pageColor, \p textColor) or colors of the user (\p customPageColor,
    /// \p customTextColor, used when they are valid). The selection (state On) is
    /// a ring bg/accent 2 px inside with the fill bg/accent-subtle and a check,
    /// the focus ring lies outside.
    struct ReadingThemeCard
    {
        QString name;
        PDFThemeColors::Color pageColor = PDFThemeColors::ReadingPaperPage;
        PDFThemeColors::Color textColor = PDFThemeColors::ReadingPaperText;
        QColor customPageColor;
        QColor customTextColor;
    };

    /// Returns the size of the card (159 x 86)
    static QSize getReadingThemeCardSize(const ReadingThemeCard& card);

    /// Returns the minimal size of the card (the texts are elided)
    static QSize getReadingThemeCardMinimumSize(const ReadingThemeCard& card);

    /// Returns the texts of the sample of a reading theme ("Aa", "Quick brown fox", "jumps over the dog", translated)
    static QStringList getReadingThemeSampleTexts();

    /// Draws the card into the rectangle
    static void paintReadingThemeCard(QPainter* painter, const QRectF& rect, const ReadingThemeCard& card, const State& state);

    // ----------------------------------------------------- Color mode card

    /// Content of a card of a choice with a picture (component Color mode card
    /// 171:4545): the picture of PDFPreviewPainter (six color modes and four page
    /// layouts, review O-05) and the name. Selection and focus as Reading theme card.
    struct PreviewCard
    {
        QString name;
        PDFPreviewPainter::Preview preview = PDFPreviewPainter::Preview::ModeNormal;
    };

    /// Returns the size of the card (125 x 122)
    static QSize getPreviewCardSize(const PreviewCard& card);

    /// Returns the minimal size of the card (the name is elided)
    static QSize getPreviewCardMinimumSize(const PreviewCard& card);

    /// Draws the card into the rectangle
    static void paintPreviewCard(QPainter* painter, const QRectF& rect, const PreviewCard& card, const State& state);

    // ======================================== composite widgets (step 4)
    //
    // The composite widgets (pdfcomponentpanels.h) are drawn here: the painted
    // parts of Page selection and Continue with (their segments, fields, check
    // boxes, choices and links are child widgets), the whole Color button, Color
    // palette popover, Signature card and Strength meter. A content with
    // \p paintNested set draws also the nested buttons (choices, swatches, links,
    // the expand button) for the gallery; a widget with real nested widgets sets
    // it to false. The layout functions return the places of the parts in the
    // left-to-right layout of the component (mirror them for right-to-left by
    // \p State::visualRect); the widgets place their children by them.

    // --------------------------------------------------------- Page selection

    /// Painted parts of a page selection (component Page selection 265:985): the
    /// header with the label and the page count, the caption of the filters, the
    /// status row with the count of the selected pages and the error row. The
    /// segments of the modes, the range field and the check boxes of the filters
    /// are child widgets (drawn by the style); their texts are used for the
    /// minimal width.
    struct PageSelection
    {
        QString label;              ///< "Pages"
        QString total;              ///< "48 pages"
        QString filtersLabel;       ///< "Only"
        QString status;             ///< "7 pages selected · 3 (iii) – 12 (xii)"
        QString error;              ///< Message of the error row; the row is shown, when it is not empty
        bool rangeVisible = false;  ///< The range field is shown (mode Range)
        QStringList modeLabels;     ///< Labels of the segments (All, Current, Selected, Range)
        QStringList filterLabels;   ///< Labels of the check boxes (Even pages, Odd pages, Portrait, Landscape)
    };

    /// Places of the parts of a page selection in its left-to-right layout
    struct PageSelectionLayout
    {
        QRect header;
        QRect modes;
        QRect range;                ///< Empty, when the range field is hidden
        QRect filtersLabel;
        QRect filters[4];           ///< Even, Odd, Portrait, Landscape (two columns, gap 8)
        QRect status;
        QRect error;                ///< Empty without an error
        int height = 0;
    };

    /// Returns the places of the parts of the page selection of the width (the
    /// error message wraps, so the height follows the width)
    static PageSelectionLayout getPageSelectionLayout(const PageSelection& selection, int width);

    /// Returns the height of the page selection of the width (156, 200 with the
    /// range field, 24 more with the error row of one line)
    static int getPageSelectionHeight(const PageSelection& selection, int width);

    /// Returns the size of the page selection (320 wide)
    static QSize getPageSelectionSize(const PageSelection& selection);

    /// Returns the minimal size of the page selection: the segments with the
    /// padding of 6, two columns of the check boxes, the header
    static QSize getPageSelectionMinimumSize(const PageSelection& selection);

    /// Draws the painted parts of the page selection into the rectangle
    static void paintPageSelection(QPainter* painter, const QRectF& rect, const PageSelection& selection, const State& state);

    // ---------------------------------------------------------- Continue with

    /// Content of the choice of the next tool of a result dialog (component
    /// Continue with 417:1007): the header (title, note, link "Another tool…"),
    /// zero to three Tool choices dividing the width equally and the caption,
    /// which wraps. The choices and the link are nested. The row of the choices
    /// is as high as the tallest choice at its width (46 when every name fits
    /// one line, 48 for a name of two lines); without choices the row and its
    /// gap are left out (review O-10).
    struct ContinueWith
    {
        QString title;
        QString note;
        QString linkText;
        QString caption;
        QList<ToolChoice> choices;  ///< At most three are shown
        int selectedChoice = -1;    ///< Selected choice (drawn with \p paintNested), -1 for none
        bool paintNested = true;
    };

    /// Places of the parts of Continue with in its left-to-right layout
    struct ContinueWithLayout
    {
        QRect title;
        QRect note;
        QRect link;
        QList<QRect> choices;       ///< One rectangle for each shown choice
        QRect caption;
        int height = 0;
    };

    /// Returns the maximal count of the choices (3)
    static int getContinueWithMaxChoices();

    /// Returns the places of the parts of Continue with of the width
    static ContinueWithLayout getContinueWithLayout(const ContinueWith& content, int width);

    /// Returns the height of Continue with of the width (93 with choices of two
    /// lines and one line of the caption, 91 with names of one line, 37 without choices)
    static int getContinueWithHeight(const ContinueWith& content, int width);

    /// Returns the size of Continue with (480 wide)
    static QSize getContinueWithSize(const ContinueWith& content);

    /// Returns the minimal size of Continue with: the minimal choices (the
    /// longest word of each name stays whole), the link and a part of the title
    static QSize getContinueWithMinimumSize(const ContinueWith& content);

    /// Draws Continue with into the rectangle
    static void paintContinueWith(QPainter* painter, const QRectF& rect, const ContinueWith& content, const State& state);

    // ----------------------------------------------------------- Color button

    /// Content of a color button (component Color button 156:4239): the swatch
    /// 20 x 20 (an invalid color is no color: crossed), the name of the color
    /// and the chevron. The focus ring of 2 px lies inside and replaces the
    /// border (flag State_HasFocus); an open palette is drawn as pressed.
    struct ColorButton
    {
        QColor color;
        QString name;
        bool open = false;
    };

    /// Returns the size of the color button (260 x 34)
    static QSize getColorButtonSize(const ColorButton& button);

    /// Returns the minimal size of the color button (the name is elided)
    static QSize getColorButtonMinimumSize(const ColorButton& button);

    /// Returns the radius of the color button (8)
    static qreal getColorButtonRadius();

    /// Draws the color button into the rectangle
    static void paintColorButton(QPainter* painter, const QRectF& rect, const ColorButton& button, const State& state);

    // ---------------------------------------------------- Color palette popover

    /// Content of the palette of a color button (component Color palette popover
    /// 156:4240): the round swatches of the suggested and of the recently used
    /// colors, six in a row with gaps of 8, and the link "More colors…". An
    /// invalid color is no color (a crossed swatch). The selected suggested color
    /// is the selected swatch. The panel is 232 wide (padding 12, the border is
    /// not counted), its shadow belongs to the popup window.
    struct ColorPalette
    {
        QList<QColor> suggested;
        QList<QColor> recent;
        int selected = -1;          ///< Index of the selected suggested color, -1 for none
        QString suggestedLabel;     ///< "Suggested"
        QString recentLabel;        ///< "Recently used"
        QString moreText;           ///< "More colors…"
        bool paintNested = true;    ///< Draw the swatches and the link
        bool panelVisible = true;   ///< Draw the panel (bg/surface, the border, radius 12); a popover window of the style has the panel of the style
    };

    /// Places of the parts of a palette in its left-to-right layout; a section
    /// without colors is left out
    struct ColorPaletteLayout
    {
        QRect suggestedLabel;
        QList<QRect> suggested;
        QRect recentLabel;
        QList<QRect> recent;
        QRect divider;
        QRect more;
        QSize size;
    };

    /// Returns the count of the swatches in a row (6)
    static int getColorPaletteColumns();

    /// Returns the radius of the panel of the palette (12)
    static qreal getColorPaletteRadius();

    /// Returns the places of the parts of the palette (the panel starts at 0, 0)
    static ColorPaletteLayout getColorPaletteLayout(const ColorPalette& palette);

    /// Returns the size of the panel of the palette (232 x 209 for two rows of suggested colors and one row of recent ones)
    static QSize getColorPaletteSize(const ColorPalette& palette);

    /// Draws the panel of the palette (without its shadow) into the rectangle
    static void paintColorPalette(QPainter* painter, const QRectF& rect, const ColorPalette& palette, const State& state);

    // --------------------------------------------------------- Signature card

    /// Status of a signature or of a certificate of its chain
    enum class SignatureStatus
    {
        Valid,      ///< Green: shield-check, "Valid"
        Warning,    ///< Amber: alert-triangle, "Needs attention"
        Invalid     ///< Red: x-circle, "Invalid"
    };

    /// Row of the details of an expanded signature card ("Signed on", "Sep 28, 2026, 14:02")
    struct SignatureDetail
    {
        QString key;
        QString value;
    };

    /// Certificate of the chain of an expanded signature card: the dot and the
    /// information have the color of the status (green, amber, red; the
    /// information of a valid certificate is text/muted), an optional link
    /// ("Add to trusted certificates") follows the information
    struct SignatureCertificate
    {
        QString name;
        QString info;
        SignatureStatus status = SignatureStatus::Warning; ///< Verification has not been supplied yet
        QString linkText;
    };

    /// Content of a signature or a timestamp of the Signatures tab (component
    /// Signature card 192:1032): the status badge, the signer, the meta line,
    /// the expand button (Icon button 24, nested), the status tag, the summary
    /// and, when expanded, the details and the certificate chain.
    struct SignatureCard
    {
        QString signer;
        QString meta;
        QString summary;
        SignatureStatus status = SignatureStatus::Warning; ///< Verification has not been supplied yet
        bool expanded = false;
        bool toggleVisible = true;      ///< The expand button is shown (its place is kept)
        QList<SignatureDetail> details;
        QString chainCaption;           ///< "Certificate chain"
        QList<SignatureCertificate> certificates;
        bool paintNested = true;        ///< Draw the expand button and the links
    };

    /// Places of the nested widgets of a signature card in its left-to-right layout
    struct SignatureCardLayout
    {
        QRect toggle;
        QList<QRect> links;     ///< One rectangle for each certificate, empty without a link or when collapsed
        int height = 0;
    };

    /// Returns a known status; unknown verification states are Invalid.
    static SignatureStatus normalized(SignatureStatus status);

    /// Returns the text of the status tag ("Valid", "Needs attention", "Invalid", translated)
    static QString getSignatureStatusText(SignatureStatus status);

    /// Returns the places of the nested widgets and the height of the card of the width
    static SignatureCardLayout getSignatureCardLayout(const SignatureCard& card, int width);

    /// Returns the height of the card of the width (127 collapsed, 382 expanded as in the design)
    static int getSignatureCardHeight(const SignatureCard& card, int width);

    /// Returns the size of the card (276 wide)
    static QSize getSignatureCardSize(const SignatureCard& card);

    /// Returns the minimal size of the card
    static QSize getSignatureCardMinimumSize(const SignatureCard& card);

    /// Draws the card into the rectangle
    static void paintSignatureCard(QPainter* painter, const QRectF& rect, const SignatureCard& card, const State& state);

    // --------------------------------------------------------- Strength meter

    /// Level of a strength meter
    enum class StrengthLevel
    {
        Empty,      ///< Nothing measured: the title ("Password strength"), no cell
        VeryWeak,   ///< Red, 3 cells
        Weak,       ///< Red, 7 cells
        Moderate,   ///< Amber, 11 cells
        Strong,     ///< Green, 16 cells
        VeryStrong  ///< Green, 20 cells
    };

    /// Criterion, which makes a password stronger
    enum class StrengthCriterion
    {
        None = 0x0,
        Length = 0x1,   ///< "12+ characters"
        Case = 0x2,     ///< "Aa"
        Digits = 0x4,   ///< "123"
        Symbols = 0x8   ///< "#$&"
    };
    Q_DECLARE_FLAGS(StrengthCriteria, StrengthCriterion)

    /// Content of a strength meter of a password or of an encryption algorithm
    /// (component Strength meter 455:1270): the level in words, the detail (the
    /// measured entropy), the bar of 20 cells and the four criteria, which wrap.
    struct StrengthMeter
    {
        StrengthLevel level = StrengthLevel::Empty;
        QString title;                  ///< Text of the level Empty ("Password strength")
        QString detail;                 ///< "118 of 128 bits"
        bool detailVisible = true;
        bool criteriaVisible = true;
        StrengthCriteria satisfied;     ///< Satisfied criteria (green with a check)
        int filledCells = -1;           ///< Filled cells (0 to 20), a negative value is the count of the level; the level Empty has none
        QStringList criteriaTexts;      ///< Texts of the four criteria (the default texts are used for the missing ones)
    };

    /// Returns the valid level (an invalid value is Empty)
    static StrengthLevel normalized(StrengthLevel level);

    /// Returns the count of the cells of the bar (20)
    static int getStrengthMeterCellCount();

    /// Returns the count of the filled cells of the meter (by the level, or \p StrengthMeter::filledCells
    /// bounded to 0 to 20); the level Empty has no filled cell
    static int getStrengthMeterFilledCells(const StrengthMeter& meter);

    /// Returns the name of the level ("Very weak", ..., translated; the title for Empty)
    static QString getStrengthLevelText(const StrengthMeter& meter);

    /// Returns the default text of a criterion ("12+ characters", "Aa", "123", "#$&")
    static QString getStrengthCriterionText(StrengthCriterion criterion);

    /// Returns the text of the criterion of the meter (its own text, or the default one)
    static QString getStrengthCriterionText(const StrengthMeter& meter, StrengthCriterion criterion);

    /// Returns the rectangles of the four criteria in the meter of the width (they wrap)
    static QList<QRect> getStrengthMeterCriterionRects(const StrengthMeter& meter, int width);

    /// Returns the height of the meter of the width (50, when the criteria fit one line)
    static int getStrengthMeterHeight(const StrengthMeter& meter, int width);

    /// Returns the size of the meter (300 wide)
    static QSize getStrengthMeterSize(const StrengthMeter& meter);

    /// Returns the minimal size of the meter
    static QSize getStrengthMeterMinimumSize(const StrengthMeter& meter);

    /// Draws the meter into the rectangle
    static void paintStrengthMeter(QPainter* painter, const QRectF& rect, const StrengthMeter& meter, const State& state);

    // ======================================== rows of the views (step 5)
    //
    // Items of the views, whose models give the data (the delegates of
    // pdfcomponentdelegates.h draw them by these functions). The state of an
    // item comes from its view: State_Selected is the selected item (Thumbnail
    // item, Bookmark row, Note row, Attachment row, Search result row; the rows
    // without the axis Selected in the design - Font row, Recent item row,
    // Shortcut row - ignore it), a selected item reacts to the pointer
    // (bg/accent-subtle-hover / -pressed, decision 10). The focus ring of 2 px
    // lies inside of the item (State_HasFocus). A disabled item is drawn by the
    // rule O-08. Nested buttons of a row (save, copy, star, menu, key cap) have
    // their own flags in the content (\p QStyle::State_MouseOver and
    // \p QStyle::State_Sunken of the button itself), their rectangles are given
    // by the functions get...Rect for the hit testing of the delegates.

    /// Range of characters of a text (a found term in a snippet)
    struct TextRange
    {
        int start = 0;
        int length = 0;
    };

    // ---------------------------------------------------------- Thumbnail item

    /// Content of a page of the thumbnails of the document (component Thumbnail
    /// item 191:777): the picture of the page in its frame with the shadow and
    /// the label of the page in a pill (bg/accent with icon/on-accent when
    /// selected). The picture keeps its aspect ratio in the slot of the page
    /// (108 x 148 in the design), the frame hugs the picture; without a picture
    /// the frame is the whole slot filled with reading/paper/page.
    struct ThumbnailItem
    {
        QString label;
        QPixmap pixmap;
        QSize pageSize = QSize(108, 148);   ///< Slot of the page (an invalid or empty size is the default)
    };

    /// Returns the valid slot of the page (an invalid or empty size is 108 x 148,
    /// each side is clamped to 4096)
    static QSize getThumbnailItemPageSize(const ThumbnailItem& item);

    /// Returns the size of the item (124 x 183 for the slot of the design)
    static QSize getThumbnailItemSize(const ThumbnailItem& item);

    /// Returns the frame of the page (the picture fitted into its slot, without
    /// the border, which lies outside of it) in the item of the rectangle
    static QRectF getThumbnailItemPageRect(const QRectF& rect, const ThumbnailItem& item);

    /// Draws the item into the rectangle
    static void paintThumbnailItem(QPainter* painter, const QRectF& rect, const ThumbnailItem& item, const State& state);

    // ------------------------------------------------------------ Bookmark row

    /// Content of a bookmark of the document sidebar (component Bookmark row
    /// 191:911): the filled bookmark icon/accent, the title (text/accent when
    /// selected), the subtitle and an optional tag ("Auto", shown when its text
    /// is not empty).
    struct BookmarkRow
    {
        QString title;
        QString subtitle;
        QString tag;
    };

    /// Returns the size of the row (276 x 46)
    static QSize getBookmarkRowSize(const BookmarkRow& row);

    /// Returns the minimal size of the row (the texts are elided, the tag is not)
    static QSize getBookmarkRowMinimumSize(const BookmarkRow& row);

    /// Draws the row into the rectangle
    static void paintBookmarkRow(QPainter* painter, const QRectF& rect, const BookmarkRow& row, const State& state);

    // ---------------------------------------------------------------- Note row

    /// Content of an annotation of the Notes tab (component Note row 191:998):
    /// the badge of the type of the annotation (Tool icon 32, Amber highlighter
    /// by default), the author (text/accent when selected), the date and the
    /// text of at most two lines. The height follows the width (48 for one
    /// line of the text, 63 for two).
    struct NoteRow
    {
        QString author;
        QString date;
        QString text;
        QString iconName = QStringLiteral("highlighter");
        PDFAccentHue hue = PDFAccentHue::Amber;
    };

    /// Returns the height of the row of the width
    static int getNoteRowHeight(const NoteRow& row, int width);

    /// Returns the size of the row of the default width (276 x 63 for a text of two lines)
    static QSize getNoteRowSize(const NoteRow& row);

    /// Returns the minimal size of the row
    static QSize getNoteRowMinimumSize(const NoteRow& row);

    /// Draws the row into the rectangle
    static void paintNoteRow(QPainter* painter, const QRectF& rect, const NoteRow& row, const State& state);

    // ---------------------------------------------------------- Attachment row

    /// Content of a file embedded in the document (component Attachment row
    /// 191:1045): the badge (Tool icon 32, Neutral file-text by default), the
    /// name (text/accent when selected), the meta line and the save action - an
    /// Icon button 24 with the icon download (icon/muted, icon/default on the
    /// hovered or pressed row in every state of the button; the own hover and
    /// press of the button change only its background, card Icon button).
    struct AttachmentRow
    {
        QString name;
        QString meta;
        QString iconName = QStringLiteral("file-text");
        PDFAccentHue hue = PDFAccentHue::Neutral;
        QStyle::State saveFlags = QStyle::State_None;   ///< Hover and press of the save button itself
    };

    /// Returns the size of the row (276 x 48)
    static QSize getAttachmentRowSize(const AttachmentRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getAttachmentRowMinimumSize(const AttachmentRow& row);

    /// Returns the rectangle of the save button (24 x 24, 6 from the trailing edge) in the row
    static QRectF getAttachmentRowSaveRect(const QRectF& rect, const State& state);

    /// Draws the row into the rectangle
    static void paintAttachmentRow(QPainter* painter, const QRectF& rect, const AttachmentRow& row, const State& state);

    // ------------------------------------------------------- Search result row

    /// Content of an occurrence of the search (component Search result row
    /// 191:1073): the page (Medium 11 text/muted, text/accent when selected) and
    /// the snippet of at most two lines (Regular 12 text/secondary), whose found
    /// terms (\p matches) are Semi Bold text/primary. The height follows the
    /// width (62 for two lines of the snippet).
    struct SearchResultRow
    {
        QString page;
        QString snippet;
        QList<TextRange> matches;
    };

    /// Returns the height of the row of the width
    static int getSearchResultRowHeight(const SearchResultRow& row, int width);

    /// Returns the size of the row of the default width (276 x 62 for a snippet of two lines)
    static QSize getSearchResultRowSize(const SearchResultRow& row);

    /// Returns the minimal size of the row
    static QSize getSearchResultRowMinimumSize(const SearchResultRow& row);

    /// Draws the row into the rectangle
    static void paintSearchResultRow(QPainter* painter, const QRectF& rect, const SearchResultRow& row, const State& state);

    // ---------------------------------------------------------------- Font row

    /// Content of a font of the document (component Font row 206:944): the badge
    /// (Neutral, type), the name and the family, the tag of the type of the font
    /// ("TrueType"), the status (a dot accent/green/icon with "Embedded subset",
    /// accent/amber/icon with "Not embedded", or the own text \p statusText) and
    /// the copy icon, which is shown on the hovered and on the pressed row. The
    /// copy place is a plain frame in the design without states of its own.
    struct FontRow
    {
        QString name;
        QString family;
        QString type;
        bool embedded = true;
        QString statusText;     ///< Text of the status (empty: the text of \p embedded)
    };

    /// Returns the default text of the status ("Embedded subset" / "Not embedded", translated)
    static QString getFontStatusText(bool embedded);

    /// Returns the size of the row (692 x 48)
    static QSize getFontRowSize(const FontRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getFontRowMinimumSize(const FontRow& row);

    /// Returns the rectangle of the copy action (24 x 24, 8 from the trailing edge) in the row
    static QRectF getFontRowCopyRect(const QRectF& rect, const State& state);

    /// Returns true, if the copy icon is shown in the state (hover or press of the row)
    static bool isFontRowCopyShown(const State& state);

    /// Draws the row into the rectangle
    static void paintFontRow(QPainter* painter, const QRectF& rect, const FontRow& row, const State& state);

    // --------------------------------------------------------- Recent item row

    /// Type of a recent item
    enum class RecentItemType
    {
        File,       ///< A document: the thumbnail of its first page and the reading progress
        Folder,     ///< A folder: the icon folder in a circle of the hue Neutral
        Workspace   ///< A workspace: the icon layers in a circle of the hue Violet
    };

    /// Content of an item of the Recent list (component Recent item row
    /// 79:1026): the thumbnail (File) or the icon in a circle, the name and the
    /// details, the reading progress (text and bar, the bar only for a file with
    /// a valid progress), the time of opening, the star (24) and the menu button.
    struct RecentItemRow
    {
        QString name;
        QString details;
        QString progressText;
        qreal progress = -1.0;      ///< Reading progress of 0 to 1; a negative value or NaN has no bar
        QString opened;
        RecentItemType type = RecentItemType::File;
        QPixmap pixmap;             ///< Picture of the first page (the placeholder lines without it)
        bool pinned = false;
        QStyle::State starFlags = QStyle::State_None;   ///< Hover and press of the star itself
        QStyle::State moreFlags = QStyle::State_None;   ///< Hover and press of the menu button itself
        bool paintNested = true;    ///< Draw the star and the menu button
        bool starVisible = true;    ///< Draw the star (an item, which cannot be pinned, a workspace, has none; its place stays, so the columns of a list stay aligned)
        FileStatus status = FileStatus::Available;  ///< Missing: the name text/muted, the badge "No longer exists" instead of the progress (a proposal, no frame in Figma)
        bool locked = false;        ///< Protected by a password: the thumbnail of a file shows the lock (PageThumbnail::locked)
    };

    /// Returns the valid type (an invalid value is File)
    static RecentItemType normalized(RecentItemType type);

    /// Returns the size of the row (802 x 56)
    static QSize getRecentItemRowSize(const RecentItemRow& row);

    /// Returns the minimal size of the row (the texts are elided)
    static QSize getRecentItemRowMinimumSize(const RecentItemRow& row);

    /// Returns the rectangle of the star (24 x 24) in the row
    static QRectF getRecentItemRowStarRect(const QRectF& rect, const State& state);

    /// Returns the rectangle of the menu button (16 x 16, 8 from the trailing edge) in the row
    static QRectF getRecentItemRowMoreRect(const QRectF& rect, const State& state);

    /// Draws the row into the rectangle
    static void paintRecentItemRow(QPainter* painter, const QRectF& rect, const RecentItemRow& row, const State& state);

    // ------------------------------------------------------------ Shortcut row

    /// Content of an action of the table of the shortcuts (component Shortcut
    /// row 145:2904): the icon, the name of the action and the key cap with the
    /// shortcut at the trailing edge; an action without a shortcut has the dashed
    /// placeholder "Add shortcut" there. A row, which records a new shortcut
    /// (\p recording), shows the cap with the focus ring inside and "Press keys…"
    /// in text/accent; the row itself is drawn as Default. The cap and the
    /// placeholder are the target of the click, which starts the recording.
    struct ShortcutRow
    {
        QString action;
        QString shortcut;           ///< Text of the shortcut ("Ctrl+O"); empty: the placeholder
        QString iconName = QStringLiteral("folder-open");
        bool recording = false;
    };

    /// Returns the text of the placeholder ("Add shortcut", translated)
    static QString getAddShortcutText();

    /// Returns the prompt of the recording ("Press keys…", translated)
    static QString getRecordShortcutText();

    /// Returns the size of the row (780 x 40)
    static QSize getShortcutRowSize(const ShortcutRow& row);

    /// Returns the minimal size of the row (the name of the action is elided)
    static QSize getShortcutRowMinimumSize(const ShortcutRow& row);

    /// Returns the rectangle of the key cap, of the placeholder or of the
    /// recorded cap (23 high, 10 from the trailing edge) in the row
    static QRectF getShortcutRowKeyRect(const QRectF& rect, const ShortcutRow& row, const State& state);

    /// Draws the row into the rectangle
    static void paintShortcutRow(QPainter* painter, const QRectF& rect, const ShortcutRow& row, const State& state);

    // ----------------------------------------------------------- Window button

    /// Kind of a button of the frameless application window (component Window
    /// button 947:1475). Maximize is shown for a normal window, Restore for a
    /// maximized one; on macOS both are the green zoom button.
    enum class WindowButtonKind
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    /// Platform of the buttons of the window (component Window controls 947:1504)
    enum class WindowPlatform
    {
        Windows,    ///< Minimize, Maximize / Restore, Close at the right end of the top bar, 46 x 38 each
        Linux,      ///< The same buttons as Windows (the application draws them, no desktop theme is followed)
        MacOS       ///< The traffic lights close, minimize, zoom (circles of 12 px) at the left end of the top bar
    };

    /// Content of a window button. Its state: State_Enabled (a disabled button
    /// is Maximize of a window, which cannot be resized: glyph icon/disabled, no
    /// hover, no press), State_MouseOver and State_Sunken (from the mouse or
    /// from the non-client messages of the system). A button of an inactive
    /// window (\p windowActive false) is the variant Inactive: no background,
    /// glyph icon/disabled; its hover and press look as in an active window.
    /// The buttons take no keyboard focus (they have no focus ring), the window
    /// is controlled by the keyboard through its system menu.
    struct WindowButton
    {
        WindowButtonKind kind = WindowButtonKind::Minimize;
        WindowPlatform platform = WindowPlatform::Windows;
        bool windowActive = true;
    };

    /// Returns the valid kind (an invalid value is Minimize)
    static WindowButtonKind normalized(WindowButtonKind kind);

    /// Returns the valid platform (an invalid value is the native platform)
    static WindowPlatform normalized(WindowPlatform platform);

    /// Returns the platform of the system, for which the library is built
    /// (macOS, Windows, Linux for every other system)
    static WindowPlatform getNativeWindowPlatform();

    /// Returns the name of the icon of the glyph of the kind ("window-minimize", ...)
    static QString getWindowButtonIconName(WindowButtonKind kind);

    /// Returns the size of a window button of the platform: 46 x 38 (Windows,
    /// Linux), 20 x 38 for a traffic light of macOS (the circle of 12 px and the
    /// half of the gap of 8 px at each side, so the targets of the three circles touch)
    static QSize getWindowButtonSize(WindowPlatform platform);

    /// Returns the size of the glyph (16) of a button of Windows and Linux
    static int getWindowButtonIconExtent();

    /// Returns the diameter of a traffic light of macOS (12)
    static int getWindowButtonCircleExtent();

    /// Returns the color of the glyph of a button of Windows and Linux in the
    /// state: icon/disabled for a disabled button, icon/on-accent for the
    /// hovered or pressed Close, icon/strong for the hovered or pressed other
    /// buttons, icon/disabled in an inactive window, icon/default otherwise
    static PDFThemeColors::Color getWindowButtonIconColor(const WindowButton& button, const State& state);

    /// Returns the background of a button of Windows and Linux in the state
    /// (bg/hover, bg/pressed, bg/danger, bg/danger-pressed), nothing at rest,
    /// in an inactive window and for a disabled button
    static std::optional<PDFThemeColors::Color> getWindowButtonBackground(const WindowButton& button, const State& state);

    /// Returns the color of a traffic light of macOS: window/macos-close,
    /// window/macos-minimize, window/macos-zoom, window/macos-inactive in an
    /// inactive window and for a disabled zoom button. The hover and the press
    /// have no drawing of their own (the native buttons draw their glyphs).
    static PDFThemeColors::Color getWindowButtonCircleColor(const WindowButton& button, const State& state);

    /// Draws the window button into the rectangle: the background fills the whole
    /// rectangle without a radius, the glyph of 16 px (or the circle of 12 px on
    /// macOS) lies in its middle. The glyphs are not mirrored for right-to-left.
    static void paintWindowButton(QPainter* painter, const QRectF& rect, const WindowButton& button, const State& state);

    // --------------------------------------------------------- Window controls

    /// Content of the group of the window buttons (component Window controls
    /// 947:1504). Windows and Linux: Minimize, Maximize (Restore, when the window
    /// is maximized) and Close, 138 x 38, placed at the right end of app-top-bar
    /// (the bar keeps 46 px free for dragging the window before them). macOS: the
    /// traffic lights close, minimize, zoom, 80 x 38 including the margins of
    /// 14 px, at the left end of the bar. A right-to-left layout mirrors the
    /// group as the systems do (Windows mirrors the caption of a window with the
    /// layout WS_EX_LAYOUTRTL, Close is then the leftmost button): the order is
    /// mirrored by the direction of the state, the glyphs are not.
    struct WindowControls
    {
        WindowPlatform platform = WindowPlatform::Windows;
        bool maximized = false;     ///< Restore instead of Maximize (macOS: no change)
        bool windowActive = true;   ///< false: the variant Inactive of each button
        bool resizable = true;      ///< false: Maximize / zoom is disabled (a window of a fixed size)
        std::optional<WindowButtonKind> hovered;    ///< Button under the pointer (Maximize and Restore are the same button)
        std::optional<WindowButtonKind> pressed;    ///< Pressed button
    };

    /// Returns the buttons of the group in their logical (left-to-right) order
    static QList<WindowButtonKind> getWindowControlsButtons(const WindowControls& controls);

    /// Returns the size of the group (138 x 38, 80 x 38 on macOS)
    static QSize getWindowControlsSize(WindowPlatform platform);

    /// Returns the rectangle of the button of the kind (Maximize and Restore are
    /// the same button) in the visual (mirrored for right-to-left) layout of the
    /// group drawn into the rectangle: the buttons fill the height of the
    /// rectangle and start at its leading edge. An empty rectangle is returned
    /// for an invalid kind or rectangle.
    static QRectF getWindowControlsButtonRect(const QRectF& rect, const WindowControls& controls, WindowButtonKind kind, const State& state);

    /// Returns the button at the point (in the coordinates of the rectangle), nothing outside of the buttons
    static std::optional<WindowButtonKind> getWindowControlsButtonAt(const QRectF& rect, const WindowControls& controls, const QPointF& point, const State& state);

    /// Draws the group into the rectangle. The enabled flag of the state applies
    /// to the whole group; the hover and the press of the buttons are given by
    /// \p WindowControls::hovered and \p WindowControls::pressed.
    static void paintWindowControls(QPainter* painter, const QRectF& rect, const WindowControls& controls, const State& state);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFComponentPainter::StrengthCriteria)

}   // namespace pdf

#endif // PDFCOMPONENTPAINTER_H
