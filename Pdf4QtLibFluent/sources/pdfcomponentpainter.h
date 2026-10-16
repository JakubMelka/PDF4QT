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
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"

#include <QColor>
#include <QFont>
#include <QMargins>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStyle>

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

        /// Returns the horizontal alignment of the leading edge (left for LTR, right for RTL)
        Qt::Alignment leadingAlignment() const { return isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft; }
    };

    // ------------------------------------------------------------------ helpers

    /// Returns the rectangle of the given size centered in the outer rectangle,
    /// its top left corner lies on a whole logical pixel
    static QRectF alignedCentered(const QRectF& outer, const QSizeF& size);

    /// Returns the text elided at its end (\p mode Qt::ElideRight) or in the
    /// middle (Qt::ElideMiddle), so it fits the width. An empty text is returned
    /// for a width, which is not positive.
    static QString elidedText(const QString& text, const QFont& font, qreal width, Qt::TextElideMode mode = Qt::ElideRight);

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

    /// Draws the tag into the rectangle. The focus ring lies inside and replaces
    /// the border. A too narrow rectangle elides the label.
    static void paintTag(QPainter* painter, const QRectF& rect, const Tag& tag, const State& state);

    // --------------------------------------------------------------- Text link

    /// Content of a link (component Text link 49:152): Semi Bold 13 in
    /// text/accent, underlined under the pointer, focus ring outside (radius 4).
    struct TextLink
    {
        QString text;
    };

    /// Returns the font of a link
    static QFont getTextLinkFont(bool isUnderlined = false);

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
    /// a ring bg/accent 2 px inside, the focus ring lies outside of it.
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
    /// the placeholder lines of the component.
    struct PageThumbnail
    {
        ThumbnailSize size = ThumbnailSize::Small;
        QPixmap pixmap;
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

    /// Draws the row into the rectangle
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
    /// the summary, which wraps (Regular 11, line 130 %). The current version has
    /// the dot bg/accent and the version text/accent. Without interaction.
    struct VersionRow
    {
        QString version;
        QString date;
        QString summary;
        bool current = true;
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
};

}   // namespace pdf

#endif // PDFCOMPONENTPAINTER_H
