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

#ifndef PDFCOMPONENTBUTTONS_H
#define PDFCOMPONENTBUTTONS_H

#include "pdffluentglobal.h"
#include "pdfaccenthue.h"
#include "pdfcomponentpainter.h"
#include "pdfthemecolors.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QList>
#include <QMargins>
#include <QMenu>
#include <QPixmap>
#include <QPointer>

#include <optional>

class QContextMenuEvent;

namespace pdf
{

class PDFMoreButton;

/// Small button with an icon only (components Icon button and Tab close button).
/// It serves the secondary actions inside of rows, cards and fields: copy, edit,
/// confirm, cancel, expand, show password, save, close. The button has no
/// background and a muted icon at rest, the background of the hover or of the
/// press and a darker icon under the pointer, the focus ring of 2 px outside
/// (drawn by the style) and the icon icon/disabled when it is disabled. Space,
/// Enter and Return press the button (the key does not go to the default button
/// of a dialog). The name of the button for assistive technologies and its
/// tooltip must be set.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFIconButton : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    /// Size of the button
    enum class Size
    {
        Small,      ///< 24 x 24 with the icon of 14 px, radius 6 (Icon button, Size=24)
        Medium,     ///< 28 x 28 with the icon of 16 px, radius 6 (Icon button, Size=28)
        TabClose    ///< 20 x 20 with the icon of 12 px, radius 4 (Tab close button)
    };

    explicit PDFIconButton(QWidget* parent = nullptr);
    explicit PDFIconButton(const QString& iconName, Size size = Size::Small, QWidget* parent = nullptr);

    const QString& getIconName() const { return m_iconName; }
    void setIconName(const QString& iconName);

    Size getButtonSize() const { return m_size; }
    void setButtonSize(Size size);

    /// Sets the color of the icon in all enabled states, only the background
    /// changes with the state (an icon of the confirming action is icon/accent,
    /// a destructive action icon/danger). A disabled button has the icon
    /// icon/disabled. An invalid value of the enumeration resets the color.
    void setIconColor(PDFThemeColors::Color color);
    void resetIconColor();
    std::optional<PDFThemeColors::Color> getIconColor() const { return m_iconColor; }

    /// Returns the color of the icon in the current state
    PDFThemeColors::Color getCurrentIconColor() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Returns the extent of the button of the size
    static int getExtent(Size size);

    /// Returns the extent of the icon of the button of the size
    static int getIconExtent(Size size);

    /// Returns the radius of the button of the size
    static qreal getRadius(Size size);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

    /// Returns the content and the state, which the button draws by
    /// PDFComponentPainter::paintIconButton (for subclasses drawing it themselves)
    PDFComponentPainter::IconButton getPaintedButton() const;
    PDFComponentPainter::State getPaintedState() const;

private:
    void init();

    QString m_iconName;
    Size m_size = Size::Small;
    std::optional<PDFThemeColors::Color> m_iconColor;
};

/// Stamp of the Insert panel (component Stamp chip): a label in capitals in the
/// colors of a hue, which says the meaning of the stamp (Approved is green,
/// Rejected is red). Height 26, the label is 11 px from the edges, the focus
/// ring is outside (drawn by the style) and it replaces the border of the hue.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStampChip : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    explicit PDFStampChip(QWidget* parent = nullptr);
    explicit PDFStampChip(const QString& text, PDFAccentHue hue = PDFAccentHue::Green, QWidget* parent = nullptr);

    /// Hue of the stamp: Green, Red, Blue or Amber
    PDFAccentHue getHue() const { return m_hue; }
    void setHue(PDFAccentHue hue);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Returns the font of the label (Semi Bold 11, capitals, letter spacing 0.4 px)
    static QFont getLabelFont();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void init();

    PDFAccentHue m_hue = PDFAccentHue::Green;
};

/// Common base of the buttons of the components: rows (PDFRowButton), tiles and
/// cards (PDFCardButton) and the small elements (Star button, Color swatch, Tag,
/// Text link). It holds the behaviour, which all of them share:
/// - the state for PDFComponentPainter (hover, press, visible focus ring,
///   checked, enabled, layout direction), the selected item of a single
///   selection group, which does not react to the pointer, a disabled button
///   without hover (O-08),
/// - the focus ring inside (drawn by the component) or outside of the shape
///   (drawn by the focus frame of the style, which reads the property \c radius;
///   it is set to 0 by the constructor, so the ring is never missing),
/// - activation by Space (Qt) and by Enter / Return,
/// - checkable variants (\p SelectionBehavior),
/// - accessibility: role Button, RadioButton (exclusive selection) or a checkable
///   button, the name from the text (title), the description from the subtitle,
/// - nested widgets (star, menu button, action buttons) placed at the trailing
///   side of the button, mirrored for right-to-left layouts,
/// - repaint and new size after a change of the theme, of the font, of the
///   layout direction and of the enabled state.
///
/// The text of the button is data (a title like "Protect & Sign", a tag "R&D"):
/// it is drawn, measured and announced literally, an ampersand never creates a
/// mnemonic shortcut (\p setText). A shortcut set by the application is kept.
///
/// A subclass draws itself in \p paintContent through PDFComponentPainter and
/// says its size in \p getContentSizeHint.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFComponentButton : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    /// Placement of the focus ring
    enum class FocusRing
    {
        Inside,     ///< The component draws the ring of 2 px inside of its shape (rows, tags)
        Outside     ///< The focus frame of the style draws the ring outside of the shape (free-standing elements, cards)
    };
    Q_ENUM(FocusRing)

    /// Checkable behaviour of the button. The exclusivity of a group comes only
    /// from an exclusive QButtonGroup, into which the application or the composite
    /// widget puts the buttons; the button never groups itself with other buttons
    /// (Qt's auto-exclusivity would take every auto-exclusive button anywhere
    /// below the parent widget, also of an unrelated group of the same page).
    /// Without a group an exclusive button is a single choice of its own.
    enum class SelectionBehavior
    {
        None,               ///< Not checkable, a plain button
        Toggle,             ///< Checkable on its own (Star button)
        Exclusive,          ///< One of a group; a click on the selected item keeps it, the selected item reacts to the pointer (Color swatch)
        ExclusiveStatic,    ///< One of a group; the selected item does not react to the pointer, the mouse, Space, Enter and the press action of assistive technologies do nothing on it (no clicked signal) (Nav item, Reading theme card, Color mode card)
        Deselectable        ///< One of a group, which can be left empty: a click (mouse, Space, Enter, press action) on the selected item unselects it, also in an exclusive QButtonGroup (Tool choice)
    };
    Q_ENUM(SelectionBehavior)

    explicit PDFComponentButton(FocusRing focusRing, QWidget* parent = nullptr);
    virtual ~PDFComponentButton() override;

    /// Sets the text (the title) of the button. The text is taken literally: an
    /// ampersand is a character, not a mnemonic (QAbstractButton::setText would
    /// register Alt + the next character as a shortcut, "Protect & Sign" would
    /// take Alt+Space). A shortcut set by the application by \p setShortcut is kept.
    /// The text set through a pointer to QAbstractButton is handled as well: its
    /// mnemonic is removed, when the button is polished or the mnemonic is pressed.
    void setText(const QString& text);

    FocusRing getFocusRing() const { return m_focusRing; }

    /// Radius of the shape, around which the focus frame of the style draws the
    /// outside ring. It has no effect on a button with the ring inside.
    qreal getFocusRingRadius() const { return m_focusRingRadius; }
    void setFocusRingRadius(qreal radius);

    /// Checkable behaviour, see \p SelectionBehavior. The exclusive behaviours
    /// make the button checkable; put the buttons of a choice into an exclusive
    /// QButtonGroup (the arrows then move the selection). Changing the behaviour
    /// to None unchecks the button (with the signal toggled), also when it is the
    /// checked button of an exclusive group.
    SelectionBehavior getSelectionBehavior() const { return m_selectionBehavior; }
    void setSelectionBehavior(SelectionBehavior behavior);

    /// Returns the state of the button for PDFComponentPainter. The flag
    /// State_HasFocus is set only for the ring inside, when it is visible.
    PDFComponentPainter::State getComponentState() const;

    /// Returns the name for assistive technologies: the accessible name of the
    /// widget, or the default name of the button (its text, taken literally)
    QString getAccessibleText() const;

    /// Returns the description for assistive technologies: the accessible
    /// description of the widget, or the default description of the button
    /// (its tooltip, unless it only repeats the name)
    QString getAccessibleDescriptionText() const;

    /// Returns true, if the button is interactive (otherwise it is announced as a text)
    virtual bool isInteractive() const { return true; }

    /// Returns true, if the button opens a popup (a menu, a palette); it is told
    /// to assistive technologies (the state hasPopup)
    virtual bool hasPopup() const { return false; }

    /// Returns true, if the popup of the button is expandable for assistive
    /// technologies (as the popup of a combo box): it is expanded, while
    /// \p isPopupShown is true. A button, which shows or hides its popup, sends
    /// QAccessibleStateChangeEvent with the state expanded.
    virtual bool hasExpandablePopup() const { return false; }

    /// Returns true, if the popup of the button is shown
    virtual bool isPopupShown() const { return false; }

    /// Returns true, if the user can activate the button now: it is enabled,
    /// interactive and it is not the selected item of an ExclusiveStatic choice.
    /// A button, which cannot be activated, ignores the mouse, Space, Enter and
    /// the press action of assistive technologies. QAbstractButton::click called
    /// by the program is not affected.
    bool isActivatable() const;

    /// Adds a nested widget (star, menu button, action button) to the trailing
    /// side of the button. The widget becomes a child of the button, the first
    /// added widget is the leading one of the trailing widgets. A widget hidden
    /// by the application stays hidden. A click on a disabled nested widget does
    /// not activate the button (the widget gets Qt::WA_NoMousePropagation, which
    /// is removed again by \p removeTrailingWidget). A nested widget, which is
    /// deleted or moved to another parent, is removed from the trailing side.
    void addTrailingWidget(QWidget* widget);

    /// Removes a nested widget from the trailing side. The widget stays a child
    /// of the button and keeps its position, the caller hides or deletes it.
    void removeTrailingWidget(QWidget* widget);

    /// Returns the nested widgets of the trailing side (also the hidden ones)
    QList<QWidget*> getTrailingWidgets() const;

    /// Distances of the trailing widgets from the edges of the button, given for
    /// the left-to-right layout (right = distance from the trailing edge)
    QMargins getTrailingMargins() const { return m_trailingMargins; }
    void setTrailingMargins(const QMargins& margins);

    /// Gap between two trailing widgets
    int getTrailingSpacing() const { return m_trailingSpacing; }
    void setTrailingSpacing(int spacing);

    /// Orientation of the trailing widgets: a row centered vertically (rows),
    /// or a column at the top, its widgets centered horizontally (Pinned card)
    Qt::Orientation getTrailingOrientation() const { return m_trailingOrientation; }
    void setTrailingOrientation(Qt::Orientation orientation);

    /// Returns the rectangle of the visible trailing widgets with the gaps between
    /// them, in the left-to-right layout of the button (the content of the
    /// button ends before it). An empty rectangle is returned, if none is visible.
    QRect getTrailingRect() const;

    /// Returns the rectangle of the content of the button in its left-to-right
    /// layout: the button without the padding, which ends \p trailingGap before
    /// the trailing widgets (if any is visible). Mirror it for right-to-left
    /// layouts by \p PDFComponentPainter::State::visualRect.
    QRect getContentRect(const QMargins& padding, int trailingGap) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Returns the height of the button of the width, when its size policy has
    /// the height for width (\p computeHeightForWidth, cached: a layout asks for
    /// the same width repeatedly), -1 otherwise
    virtual int heightForWidth(int width) const override;

protected:
    /// Returns the height of the button of the width. A button, whose height
    /// follows its width, sets QSizePolicy::setHeightForWidth and overrides this
    /// function (the default returns -1). Its result is cached by the width and
    /// the text of the button; every other change of the content must call
    /// \p invalidateHeight (a setter of such a button calls it instead of
    /// update, also for a change, which looks only as a change of the colors).
    /// A change of the theme, of the font, of the style and of the language
    /// invalidates the cached height by itself.
    virtual int computeHeightForWidth(int width) const;

    /// Forgets the cached height, updates the geometry and repaints the button
    void invalidateHeight();

    /// Returns true, if the nested widget lies in the button and it is not hidden
    bool isNestedShown(const QWidget* widget) const;

    /// Shows the menu of a row or a card by its menu button: at the pointer
    /// for a mouse, below the menu button for the context menu key. The menu is
    /// requested first by \p requestMoreButtonMenu (it emits the signal
    /// menuRequested of the button, whose slot may fill the menu, or delete the
    /// button or the menu button). A request, which deleted the button, accepts
    /// the event (it has been handled, and the event must not go on to the
    /// deleted widget); without the menu button the event is ignored (it goes to
    /// the parent).
    void showMoreButtonMenu(PDFMoreButton* moreButton, QContextMenuEvent* event);

    /// Requests the menu of the menu button before \p showMoreButtonMenu shows
    /// it: a row or a card with a menu button emits its signal menuRequested
    /// (the slot may delete the button). The default does nothing.
    virtual void requestMoreButtonMenu();

    /// Joins the parts, which are not empty, by the pattern ("%1, %2")
    static QString joinTexts(const QStringList& parts, const QString& pattern);

    /// Returns the pattern, which joins two parts of an accessible description
    /// ("%1, %2", translated in the context of PDFComponentButton)
    static QString getListPattern();

    /// Returns the description of the button for assistive technologies, when
    /// the application has not set its own (the tooltip by default, unless it
    /// only repeats the name of the button)
    virtual QString getDefaultAccessibleDescription() const;

    /// Draws the button through PDFComponentPainter
    /// \param painter Painter (never null)
    /// \param rect Rectangle of the button
    /// \param state State of the button
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const = 0;

    /// Returns the size of the button by its content
    virtual QSize getContentSizeHint() const = 0;

    /// Returns the minimal size of the button (the size hint by default)
    virtual QSize getContentMinimumSizeHint() const { return getContentSizeHint(); }

    /// Returns the name of the button for assistive technologies, when the
    /// application has not set its own (the text of the button by default)
    virtual QString getDefaultAccessibleName() const;

    /// Places the trailing widgets; a subclass may place them differently
    virtual void layoutTrailingWidgets();

    /// Replaces the accessible description, when it is empty or it is the
    /// previous text (a description set by the application is kept)
    void replaceAccessibleDescription(const QString& previousText, const QString& text);

    /// Replaces the tooltip, when it is empty or it is the previous text
    void replaceToolTip(const QString& previousText, const QString& text);

    /// Unchecks the button, also when it is the checked button of an exclusive
    /// QButtonGroup (or of an auto-exclusive group of the application), which
    /// is then left without a checked button. The signal toggled is emitted.
    void uncheck();

    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void keyReleaseEvent(QKeyEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual bool hitButton(const QPoint& pos) const override;
    virtual void nextCheckState() override;

private:
    void onThemeChanged();
    void requestTrailingLayout();
    void pruneTrailingWidgets(const QObject* removedChild);
    void removeMnemonicShortcut();
    void updateFocusFrame();
    QPointer<QButtonGroup> liftGroupExclusivity();

    /// Height of the last width and text (QAbstractButton::setText bypasses the setters)
    struct HeightCache
    {
        int width = -1;
        int height = -1;
        QString text;
    };

    FocusRing m_focusRing;
    qreal m_focusRingRadius = 0.0;
    SelectionBehavior m_selectionBehavior = SelectionBehavior::None;
    QList<QPointer<QWidget>> m_trailingWidgets;
    QMargins m_trailingMargins;
    int m_trailingSpacing = 0;
    Qt::Orientation m_trailingOrientation = Qt::Horizontal;
    QSize m_trailingSize;

    /// Exclusive group, which the mouse or Space on the selected Deselectable
    /// button has made non-exclusive for the time of the click (Qt does not
    /// uncheck the checked button of an exclusive group by a click)
    QPointer<QButtonGroup> m_liftedGroup;

    mutable HeightCache m_heightCache;
};

/// Badge of a tool (component Tool icon): a full circle of the hue of the
/// category of the tool with an icon of half of its diameter. Diameter 32 in
/// rows and cards, 28 in Tool choice, in the title of a tool and in the search
/// results, 44 in the header of a workspace. The badge is never scaled, the
/// stroke of the icon is 2 px at each size. Neutral is the hue of everything
/// without a category. Without interaction; a disabled badge has the icon icon/disabled.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFToolBadge : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using BadgeSize = PDFComponentPainter::BadgeSize;

    explicit PDFToolBadge(QWidget* parent = nullptr);
    explicit PDFToolBadge(const QString& iconName, PDFAccentHue hue = PDFAccentHue::Blue, BadgeSize size = BadgeSize::Medium, QWidget* parent = nullptr);

    PDFAccentHue getHue() const { return m_badge.hue; }
    void setHue(PDFAccentHue hue);

    const QString& getIconName() const { return m_badge.iconName; }
    void setIconName(const QString& iconName);

    BadgeSize getBadgeSize() const { return m_badge.size; }
    void setBadgeSize(BadgeSize size);

    const PDFComponentPainter::ToolBadge& getBadge() const { return m_badge; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    PDFComponentPainter::ToolBadge m_badge;
};

/// Topic tag (component Tag): a pill with a label Medium 11, 21 px high for any
/// character. By default it is only a label: it takes no focus, the mouse passes
/// through it to the row or card below and assistive technologies announce it as
/// a text. A clickable tag (it filters by its topic) reacts to the pointer, takes
/// the focus and has the focus ring inside. The text is taken literally ("R&D").
class PDF4QTLIBFLUENTSHARED_EXPORT PDFTag : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFTag(QWidget* parent = nullptr);
    explicit PDFTag(const QString& text, QWidget* parent = nullptr);

    /// A tag, which stops being clickable, gives its focus away
    bool isClickable() const { return m_isClickable; }
    void setClickable(bool isClickable);

    virtual bool isInteractive() const override { return m_isClickable; }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;

private:
    bool m_isClickable = false;
};

/// Inline link (component Text link): a button, which looks as a link ("View all",
/// "MIT License"). Semi Bold 13 text/accent, underlined under the pointer,
/// text/accent-pressed when pressed, the focus ring outside (radius 4). The link
/// takes the focus by the keyboard only (as the icon buttons), a click leaves the
/// focus, where it was.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFTextLink : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFTextLink(QWidget* parent = nullptr);
    explicit PDFTextLink(const QString& text, QWidget* parent = nullptr);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
};

/// Toggle, which pins (stars) a file or a tool (component Star button). The
/// unpinned star is an outline icon/muted, the pinned one a filled amber star.
/// Its tooltip and its name are "Pin file" / "Unpin" by the state, "Pin tool" /
/// "Unpin tool" for a tool (\p setSubject; translated again after a change of
/// the language). The accessible description of the star is the name of the
/// row or of the card, in which it lies (the item, which it pins), unless the
/// application sets its own. Size 28 in Tool row and Quick action tile, 24 in
/// the file rows and in Pinned card.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStarButton : public PDFComponentButton
{
    Q_OBJECT
    Q_PROPERTY(bool pinned READ isPinned WRITE setPinned NOTIFY pinnedChanged)

private:
    using BaseClass = PDFComponentButton;

public:
    using StarSize = PDFComponentPainter::StarSize;

    /// What the star pins (it chooses the texts of the states)
    enum class Subject
    {
        File,   ///< "Pin file" / "Unpin"
        Tool    ///< "Pin tool" / "Unpin tool"
    };
    Q_ENUM(Subject)

    explicit PDFStarButton(QWidget* parent = nullptr);
    explicit PDFStarButton(StarSize size, QWidget* parent = nullptr);

    bool isPinned() const { return isChecked(); }
    void setPinned(bool isPinned);

    StarSize getStarSize() const { return m_size; }
    void setStarSize(StarSize size);

    /// What the star pins (File by default; an invalid value is File)
    Subject getSubject() const { return m_subject; }
    void setSubject(Subject subject);

    /// Returns the tooltip of the state ("Pin file" or "Unpin", "Pin tool" or "Unpin tool")
    QString getStateText() const;

Q_SIGNALS:
    /// The star has been pinned or unpinned (emitted only on a real change)
    void pinnedChanged(bool isPinned);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QString getDefaultAccessibleName() const override;
    virtual QString getDefaultAccessibleDescription() const override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// Takes the tooltip and the name of the current state
    void updateStateText();
    void onToggled(bool isChecked);

    StarSize m_size = StarSize::Medium;
    Subject m_subject = Subject::File;
    QString m_stateText;
};

/// Menu button of a row or a card (the icon more-vertical 16 x 16 of Recent file
/// row, Workspace row, Folder card, Pinned card). In the design it is an icon,
/// not an Icon button: icon/muted at rest and icon/default, when the row or the
/// card, in which it lies, or the button itself is under the pointer or pressed.
/// It is a button of its own with the name and the tooltip "More actions", it is
/// reached by Tab (a click leaves the focus, where it was), its focus ring lies
/// outside. A click (or Space, Enter) shows the menu set by \p setMenu (not owned)
/// below the button, aligned with its leading edge (above it, when there is no
/// room below), as QPushButton places its menu. The button stays pressed, while
/// its menu is shown, and a second click of the button closes the menu.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFMoreButton : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFMoreButton(QWidget* parent = nullptr);
    virtual ~PDFMoreButton() override;

    /// Menu shown by a click (not owned; a deleted menu is forgotten)
    QMenu* getMenu() const { return m_menu.data(); }
    void setMenu(QMenu* menu);

    /// Shows the menu below the button (or above it), aligned with its leading edge
    /// \returns true, if a menu was shown
    bool showMenu();

    /// Shows the menu at the global position (a context menu at the pointer)
    /// \returns true, if a menu was shown
    bool showMenu(const QPoint& globalPosition);

    /// Returns true, if the button has a menu
    virtual bool hasPopup() const override { return m_menu != nullptr; }

Q_SIGNALS:
    /// The button has been clicked; emitted before the menu is shown (a slot may fill the menu)
    void menuRequested();

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QString getDefaultAccessibleName() const override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateText();
    void disconnectMenu();
    void onMenuClosed();
    void onClicked();

    QPointer<QMenu> m_menu;
    QString m_text;
    QMetaObject::Connection m_menuHiddenConnection;
    QMetaObject::Connection m_menuDestroyedConnection;
};

/// Predefined color (component Color swatch): a circle of 28 px with the color
/// of 20 px in the middle, or no color (crossed). It is checkable; the swatches
/// of a palette are put into an exclusive QButtonGroup (\p PDFComponentButton::SelectionBehavior).
/// The selection is a ring inside, the focus ring lies outside of it. A disabled
/// swatch keeps its color and does not react to the pointer. The name of the color
/// for assistive technologies is set by \p setColorName (the hexadecimal value,
/// "No color" or "Color" by default).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFColorSwatch : public PDFComponentButton
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ getColor WRITE setColor)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY toggled)

private:
    using BaseClass = PDFComponentButton;

public:
    using Kind = PDFComponentPainter::SwatchKind;

    explicit PDFColorSwatch(QWidget* parent = nullptr);
    explicit PDFColorSwatch(const QColor& color, const QString& colorName = QString(), QWidget* parent = nullptr);

    const QColor& getColor() const { return m_swatch.color; }
    void setColor(const QColor& color);

    Kind getKind() const { return m_swatch.kind; }
    void setKind(Kind kind);

    /// Selects or unselects the swatch. Unselecting the selected swatch of an
    /// exclusive group leaves the group without a selection (the current color
    /// is none of the swatches).
    bool isSelected() const { return isChecked(); }
    void setSelected(bool isSelected);

    const QString& getColorName() const { return m_colorName; }
    void setColorName(const QString& colorName);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QString getDefaultAccessibleName() const override;

private:
    /// Announces a change of the default name to assistive technologies
    void notifyNameChanged(const QString& previousName);

    PDFComponentPainter::ColorSwatch m_swatch;
    QString m_colorName;
};

/// Frame of the first page of a document (component Page thumbnail) with its
/// shadow. The application gives the picture of the page; without it the frame
/// shows the placeholder lines of the component.
///
/// The widget has exactly the size of the frame (32 x 42, 56 x 74, 80 x 106), as
/// the component in the layouts of the design: the drop shadow is an effect and
/// takes no space. A widget cannot draw outside of itself, so the shadow is drawn
/// by the graphics effect of the widget (QWidget::graphicsEffect) into its parent,
/// \p PDFComponentPainter::getPageThumbnailShadowMargins around the frame; keep
/// that space free of other widgets. The thumbnail is meant as a child widget
/// (a window has no parent to draw the shadow into). Replacing the graphics
/// effect removes the shadow. Rows and cards with a thumbnail draw the thumbnail and its shadow by
/// \p PDFComponentPainter::paintPageThumbnail within their own rectangle instead.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPageThumbnail : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using Size = PDFComponentPainter::ThumbnailSize;

    explicit PDFPageThumbnail(QWidget* parent = nullptr);
    explicit PDFPageThumbnail(Size size, QWidget* parent = nullptr);

    Size getThumbnailSize() const { return m_thumbnail.size; }
    void setThumbnailSize(Size size);

    const QPixmap& getPixmap() const { return m_thumbnail.pixmap; }
    void setPixmap(const QPixmap& pixmap);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    PDFComponentPainter::PageThumbnail m_thumbnail;
};

}   // namespace pdf

#endif // PDFCOMPONENTBUTTONS_H
