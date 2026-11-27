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

#ifndef PDFFLUENTWIDGETS_H
#define PDFFLUENTWIDGETS_H

#include "pdffluentglobal.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QCalendarWidget>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QItemEditorFactory>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QDial>
#include <QTextEdit>
#include <QTreeView>

class QAction;
class QHBoxLayout;
class QMenu;
class QToolButton;

namespace pdf
{

class PDFIconButton;

// Widgets of Qt, which draw their text by themselves, take its colors from the
// palette of the widget. The palette of the application is filled with the leak
// color, so such a widget would be violet. The widgets below are their
// replacements: each of them writes the colors of the active theme into its own
// palette (only the roles, which its text engine reads) and follows the changes
// of the theme. Everything else is drawn by PDFWidgetStyle.

/// Fills the palettes of the widgets of the library with the colors of the theme
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThemeWidgetHelper
{
public:
    PDFThemeWidgetHelper() = delete;

    /// Returns the palette of a widget, which draws a text by itself. Roles of
    /// the text have the colors of the active theme, the other roles keep the
    /// leak color.
    /// \param text Color of the text
    /// \param disabledText Color of the text of a disabled widget
    static QPalette createTextPalette(PDFThemeColors::Color text, PDFThemeColors::Color disabledText = PDFThemeColors::TextDisabled);

    /// Sets the palette of the text to the widget and keeps it updated, when
    /// the theme is changed. It can be called again with other colors.
    static void bindTextPalette(QWidget* widget, PDFThemeColors::Color text = PDFThemeColors::TextPrimary, PDFThemeColors::Color disabledText = PDFThemeColors::TextDisabled);
};

/// Label. Font and color are given by the roles of the design. Rich text and
/// links are supported as in QLabel.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFLabel : public QLabel
{
    Q_OBJECT

public:
    explicit PDFLabel(QWidget* parent = nullptr);
    explicit PDFLabel(const QString& text, QWidget* parent = nullptr);
    explicit PDFLabel(const QString& text, PDFThemeFonts::Role fontRole, PDFThemeColors::Color color, QWidget* parent = nullptr);

    PDFThemeFonts::Role getFontRole() const { return m_fontRole; }
    void setFontRole(PDFThemeFonts::Role fontRole);

    PDFThemeColors::Color getTextColor() const { return m_color; }
    void setTextColor(PDFThemeColors::Color color);

    /// Line height of the design [pixels] of a plain text, 0 = the line spacing
    /// of the font (as QLabel). QLabel has no line height: a plain text (not a
    /// rich text, no picture) with a line height is laid out and drawn by the
    /// label itself, each line of the text in a line of this height (the text is
    /// centered in it); the text stays the plain text of QLabel. The selection by
    /// the mouse (Qt::TextSelectableByMouse) works with Ctrl+C and the context menu
    /// (Copy, Select All), other interactions of a text (keyboard, links) are not
    /// supported for it. Bounded to 0 - 256.
    int getLineHeight() const { return m_lineHeight; }
    void setLineHeight(int lineHeight);

    /// The plain text of the label is data (a size, a path, a name): it is drawn in its own
    /// direction (PDFComponentPainter::getDataTextDirection), also in an interface of the
    /// other direction, where a painter would draw "807.2 kB" as "kB 807.2". The alignment
    /// of the label stays by the direction of the label. Not for a text with a line height,
    /// a rich text or a selectable text, which are not drawn by the label itself.
    bool isDataText() const { return m_isDataText; }
    void setDataText(bool isDataText);

    /// Returns the selected text (also of a text with a line height, which is
    /// selected by the label itself)
    QString selectedText() const;
    bool hasSelectedText() const;

    static constexpr int MAX_LINE_HEIGHT = 256;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual int heightForWidth(int width) const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void init();

    /// Returns true, if the label lays out and draws its text in the lines of the line height
    bool isLineLayout() const;

    /// Visits the lines of the text laid out at the given width (lines of the line
    /// height). \returns The height of the text.
    /// \param width Width of the lines
    /// \param painter Painter drawing the lines (nullptr = only measure)
    /// \param origin Top left corner of the text
    /// \param point Point, whose position in the text is returned in \p position
    /// \param position Position in the text of the point (nullptr = no hit test)
    int layoutLines(qreal width, QPainter* painter, const QPointF& origin, const QPointF& point, int* position) const;

    /// Returns the rectangle of the text and the top left corner of its lines
    QRectF getLineLayoutRect(QPointF* origin) const;

    /// Returns the position in the text of a point of the widget
    int getTextPosition(const QPointF& point) const;

    /// Returns the width of the widget around the text (margins), horizontally / vertically
    QSize getLineLayoutExtra() const;

    /// Copy and Select All of the context menu, called from the event loop (queued from the
    /// actions of the popup menu: the clipboard may run a slot, which deletes the label and
    /// its menu, while the menu still emits); Copy takes the selection of the opening
    void copyContextMenuSelection();
    void selectAllFromContextMenu();

    PDFThemeFonts::Role m_fontRole = PDFThemeFonts::Role::Body;
    PDFThemeColors::Color m_color = PDFThemeColors::TextPrimary;
    int m_lineHeight = 0;
    bool m_isDataText = false;

    // Selection of a text with a line height (positions in the text, the text of
    // the selection - QLabel::setText is not virtual, a new text ends it)
    int m_selectionAnchor = -1;
    int m_selectionCursor = -1;
    QString m_selectionText;
    bool m_isSelecting = false;

    /// The selected text, when the context menu was opened (its Copy)
    QString m_contextMenuSelection;
};

/// Line edit of a single line
class PDF4QTLIBFLUENTSHARED_EXPORT PDFLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit PDFLineEdit(QWidget* parent = nullptr);
    explicit PDFLineEdit(const QString& text, QWidget* parent = nullptr);

    /// The value of the field is invalid, the field has a red border
    /// \param invalid The value is invalid
    /// \param message Reason, why the value is invalid. The color is not the
    ///        only sign of the error: the message becomes the description of
    ///        the field for assistive technologies (and the form should show it).
    bool isInvalid() const;
    void setInvalid(bool invalid, const QString& message = QString());

    /// Pill shaped field (search field)
    bool isPill() const;
    void setPill(bool pill);

    /// Adds an action with an icon of the design into the field
    /// \param iconName Name of the icon
    /// \param position Position of the icon
    /// \param text Text of the action (name of its button for assistive technologies)
    QAction* addIconAction(const QString& iconName, ActionPosition position, const QString& text = QString());

    /// Turns the field into a search field: pill shape with the icon of the search
    void setSearchField();

    /// Turns the field into a password field (component Password field): 40 px
    /// high, the lock of 16 px at the left, the button of 28 px, which shows the
    /// password, at the right (an Icon button with its own states), the bullets
    /// of the hidden password 2 px apart. The lock is red, when the field is invalid.
    /// The value never leaves the field (plan of step 5 of phase 2, S5.2), also when the
    /// button shows it: Copy and Cut by the keys and by the context menu do nothing (Cut
    /// does not remove the selection either), the selection is never dragged, and the
    /// selection clipboard of X11 / Wayland, which QLineEdit fills after a key or a release
    /// of the mouse, is taken back at once.
    void setPasswordField();

    /// Returns true, if the field is a password field
    bool isPasswordField() const { return m_isPasswordField; }

    /// Returns the button, which shows the password, nullptr, if the field is not
    /// a password field (or the application has deleted the button)
    PDFIconButton* getPasswordButton() const;

    /// The value of the field without the focus ends with "…", when it is wider than
    /// the field (Text field 383:4410: a long path of a folder), instead of
    /// showing the part at the cursor; the field with the focus scrolls its text as
    /// every line edit. Only a field with a normal echo and without actions.
    bool isElidedWhenInactive() const { return m_isElidedWhenInactive; }
    void setElidedWhenInactive(bool elided);

    /// Returns the text, which the field shows now: elided at its end (see
    /// \p setElidedWhenInactive), or the whole text (scrolled to the cursor)
    QString getDisplayedText() const;

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void init();
    void updateColors();
    void updatePasswordFont();

    /// A password field takes back the selection clipboard (X11 / Wayland), which QLineEdit
    /// has filled by the shown selection of the password
    void releaseSelectionClipboard();
    void updatePasswordButton();
    void placePasswordButton();
    void onPasswordButtonToggled(bool isShown);

    /// The rectangle of the line of the text (QLineEdit: the contents, the text margins,
    /// the horizontal margin of 2); \p lineHeight the height of the line
    QRect getTextLineRect(int lineHeight) const;

    /// Returns true, if the field shows its value elided now
    bool isTextElided() const;
    void paintElidedText();

    bool m_isPasswordField = false;
    bool m_isElidedWhenInactive = false;
    QPointer<QWidget> m_passwordButton; ///< PDFIconButton, a child, which the application may delete
    QString m_invalidMessage;
};

/// Editor of a rich text. Its lines have the height of their fonts: the line
/// height of the design (18 px) would have to be a format of the paragraphs,
/// which is a part of the edited text (it would be saved with it).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFTextEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit PDFTextEdit(QWidget* parent = nullptr);
    explicit PDFTextEdit(const QString& text, QWidget* parent = nullptr);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void init();
};

/// Editor of a plain text with the lines of the design (18 px). The lines are
/// made by the layout of its document, a document set by the application
/// (setDocument) has the lines of its own layout.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit PDFPlainTextEdit(QWidget* parent = nullptr);
    explicit PDFPlainTextEdit(const QString& text, QWidget* parent = nullptr);

    /// The height of the lines of the design (18 px by default; a view of a frame with other lines, the contents of
    /// Object Inspector 19 px, sets its own). A document set by the application has the lines of its own layout.
    qreal getLineHeight() const;
    void setLineHeight(qreal lineHeight);

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void init();
    void handleMouseEvent(QMouseEvent* event);
    QPointF getLinePosition(const QPointF& position) const;
};

/// State "mixed" of a field of the value of several selected objects, which
/// differ in it (edit-content-v2-multi-selected 1647:49437: the field shows
/// "Mixed" in the color of a value). Shared by PDFSpinBox and PDFDoubleSpinBox.
struct PDFMixedSpinBoxState
{
    bool isMixed = false;               ///< The field shows "Mixed"
    bool isLeftByText = false;          ///< The state has been left by a text typed by the user, the entry is not finished
    QString placeholderBeforeMixed;     ///< Placeholder of the editor of the application
};

/// Entry of the user into a spin box (PDFSpinBox, PDFDoubleSpinBox): valueEntered waits
/// for the end of the outermost event of the spin box, which has made the entry
struct PDFSpinBoxEntryState
{
    int eventDepth = 0;                 ///< Events of the spin box handled now (nested ones counted)
    bool isCommitPending = false;       ///< An entry is committed, valueEntered follows at the end of the event
};

/// Spin box. The prefix and the suffix set by \p setDisplayPrefix and
/// \p setDisplaySuffix are drawn muted beside the value, as the design has them
/// (the prefix and the suffix of QSpinBox are a part of the edited text).
///
/// Entry of the user (\p valueEntered): a panel, which writes the value of a field
/// into a document, connects only valueEntered and reads its argument (= \p value) -
/// never valueChanged (Qt emits it while the user types, with keyboard tracking, and
/// not for an entry equal to the value before) nor editingFinished (it comes also
/// when nothing was typed); the keyboard tracking does not matter then. The signal is
/// emitted, when the user commits a value: Enter / Return (always, also for an
/// unchanged value), leaving the field (Tab, a click elsewhere, a deactivated window)
/// after typing into it, a step (the arrows, Page Up / Down, the wheel, the buttons,
/// Step up / down of the context menu), and \p commitEntry. It is emitted also for a
/// value equal to the one before and when the field was mixed (it is not mixed any
/// more then). It is not emitted by \p setValue, \p setMixed or \p stepBy / stepUp /
/// stepDown called by the program, nor when the user leaves a field without typing
/// into it (a mixed field stays mixed then; Enter in a mixed field without a text
/// emits nothing either), nor by a retired spin box (PDFDeferredDeletion::retire moves
/// the focus out of it). It is emitted as the last statement after Qt has handled the
/// whole event (valueChanged and editingFinished of Qt come before it), so its slot
/// may delete the spin box, also synchronously.
///
/// Mixed state (\p setMixed): the editor is empty and shows "Mixed" (translated)
/// in the color of a value, also with a prefix and a suffix of Qt (setPrefix,
/// setSuffix: they come back with the text of a value). Entering and leaving the
/// state by the program never emits valueChanged / textChanged. In the state,
/// \p value returns the value held before the state (the value set by the
/// program), \p isMixed says it is not shown; accessibility presents the value
/// "Mixed" (no value interface: the held number is not read out). The user leaves
/// the state by entering a value: typing (accepted by Enter, by leaving the field or,
/// with keyboard tracking, at each acceptable key), the arrows and the wheel (they
/// step from the held value) - each entry ends with \p valueEntered. Leaving the
/// field with an empty or unacceptable text returns to the state (no valueEntered).
/// The library never emits a signal of QSpinBox itself (Qt emits them in its own
/// code and uses the spin box after them). \p setValue by the program leaves the
/// state: QAbstractSpinBox writes the text of the value with the signals of its
/// editor blocked, so the state follows the text of the editor - a text in it ends
/// the state (checked by \p isMixed and by the events).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFSpinBox : public QSpinBox
{
    Q_OBJECT

public:
    explicit PDFSpinBox(QWidget* parent = nullptr);

    const QString& getDisplayPrefix() const { return m_displayPrefix; }
    void setDisplayPrefix(const QString& prefix);

    const QString& getDisplaySuffix() const { return m_displaySuffix; }
    void setDisplaySuffix(const QString& suffix);

    /// Mixed state, see the class; no value signal
    bool isMixed() const;
    void setMixed(bool mixed);

    /// Commits an entry, which the user has typed and not confirmed yet, as Enter would
    /// (a panel commits its pending values before a save or a change of the tool): the text
    /// is interpreted and \p valueEntered is emitted (as the last statement). Nothing without
    /// a typed entry; a mixed field without an acceptable text returns to the state.
    void commitEntry();

    virtual void stepBy(int steps) override;

Q_SIGNALS:
    /// The user has committed the value \p value (see the class: Enter, leaving the field
    /// after typing, a step, commitEntry; also an unchanged value and from the mixed state;
    /// never by the program's setValue / setMixed / stepBy). Emitted last, after Qt has
    /// handled the event; a slot may delete the spin box.
    void valueEntered(int value);

protected:
    virtual bool event(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

    /// The menu of QAbstractSpinBox shown by popup() (no nested event loop)
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void onEditorTextChanged(const QString& text);
    void onEditorTextEdited();
    void emitPendingEntry();

    QString m_displayPrefix;
    QString m_displaySuffix;
    mutable PDFMixedSpinBoxState m_mixed;
    PDFSpinBoxEntryState m_entry;
};

/// Spin box of real numbers, see \p PDFSpinBox (also its mixed state and valueEntered)
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDoubleSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    explicit PDFDoubleSpinBox(QWidget* parent = nullptr);

    const QString& getDisplayPrefix() const { return m_displayPrefix; }
    void setDisplayPrefix(const QString& prefix);

    const QString& getDisplaySuffix() const { return m_displaySuffix; }
    void setDisplaySuffix(const QString& suffix);

    /// Mixed state, see \p PDFSpinBox; no value signal
    bool isMixed() const;
    void setMixed(bool mixed);

    /// Commits an entry typed by the user and not confirmed yet, see \p PDFSpinBox::commitEntry
    void commitEntry();

    virtual void stepBy(int steps) override;

    /// The text has no trailing zeros of the fraction ("13", "2.5"; the size 1567:19204 shows "13"),
    /// the precision of decimals() stays for the values with a fraction
    bool isTrailingZerosHidden() const { return m_isTrailingZerosHidden; }
    void setTrailingZerosHidden(bool isHidden);

    virtual QString textFromValue(double value) const override;

Q_SIGNALS:
    /// The user has committed the value \p value, see \p PDFSpinBox::valueEntered
    void valueEntered(double value);

protected:
    virtual bool event(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

    /// The menu of QAbstractSpinBox shown by popup() (no nested event loop)
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void onEditorTextChanged(const QString& text);
    void onEditorTextEdited();
    void emitPendingEntry();

    QString m_displayPrefix;
    QString m_displaySuffix;
    bool m_isTrailingZerosHidden = false;
    mutable PDFMixedSpinBoxState m_mixed;
    PDFSpinBoxEntryState m_entry;
};

/// Calendar of the design: cells of days with rounded shapes, no grid. The menu of the
/// months and the context menu of the editor of the year open by popup() (no nested event
/// loop of QToolButton::showMenu / QAbstractSpinBox::contextMenuEvent).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFCalendarWidget : public QCalendarWidget
{
    Q_OBJECT

public:
    explicit PDFCalendarWidget(QWidget* parent = nullptr);
    virtual ~PDFCalendarWidget() override;

    /// The menu of the months and the context menu of the editor of the year are shown by the
    /// calendar (popup()), not by QToolButton / QAbstractSpinBox (markers of the gate script
    /// UnitTests/nested_event_loops.py)
    static constexpr bool HAS_OWN_MONTH_MENU = true;
    static constexpr bool HAS_OWN_YEAR_EDITOR = true;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Opens the menu of the months below the button of the month (as QToolButton with an
    /// instant popup: the press of the button, Show Menu of assistive technologies), the
    /// button looks pressed as a button of an open menu, until the menu is hidden.
    /// \returns false without the button or the menu, or for a disabled button
    bool showMonthMenu();

    /// The menu of the months of QCalendarWidget (nullptr, if it is gone)
    QMenu* getMonthMenu() const;

protected:
    virtual void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateColors();

    /// The button of the month is pressed (the mouse, Space): the menu of the months opens
    void onMonthButtonPressed();

    /// The menu of the months is hidden: the button is released
    void onMonthMenuHidden();

    /// Slot of PDFLifetimeNotifier::aboutToBeDeleted of the menu of the months (retired or deleted alone): the
    /// button is released (nothing, when the calendar itself is retired)
    void onMonthMenuAboutToBeDeleted();

    QPointer<QWidget> m_viewport;
    QPointer<QToolButton> m_monthButton;    ///< Part of QCalendarWidget
    QPointer<QMenu> m_monthMenu;            ///< Part of QCalendarWidget (a child of the button of the month)
    QPointer<QSpinBox> m_yearEdit;          ///< Part of QCalendarWidget
    QMetaObject::Connection m_monthMenuHiddenConnection;
    QMetaObject::Connection m_monthMenuDeletedConnection;
};

/// Editor of a date and time. Its calendar popup is \p PDFCalendarWidget.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDateTimeEdit : public QDateTimeEdit
{
    Q_OBJECT

public:
    explicit PDFDateTimeEdit(QWidget* parent = nullptr);
    explicit PDFDateTimeEdit(QDate date, QWidget* parent = nullptr);
    explicit PDFDateTimeEdit(QTime time, QWidget* parent = nullptr);
    explicit PDFDateTimeEdit(const QDateTime& dateTime, QWidget* parent = nullptr);

    /// Shows the button of the calendar instead of the stepper
    void setCalendarPopupEnabled(bool enabled);

protected:
    virtual void showEvent(QShowEvent* event) override;

    /// The menu of QAbstractSpinBox shown by popup() (no nested event loop)
    virtual void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void init();
    void ensureCalendar();
};

/// Combo box.
///
/// Mixed state (\p setMixed, edit-content-v2-multi-selected 1647:49442): no item
/// is current and the box shows "Mixed" (translated) in the color of a value.
/// Entering and leaving the state by the program never emits currentIndexChanged,
/// currentTextChanged or activated. In the state, currentIndex() is -1 (Qt's "no
/// item"); \p getIndexBeforeMixed is the item, which was current before it, and
/// setMixed(false) makes it current again (no signal). The user leaves the state
/// by choosing an item (the signals of Qt, the index changes from -1); a
/// setCurrentIndex of an item by the program leaves it too (with the signals of
/// Qt). In the state the accessible description is "Mixed" (as the row of
/// swatches; a description of the application is kept). For a box, which is not
/// editable.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit PDFComboBox(QWidget* parent = nullptr);

    /// Compact variant of 28 px used in rows of properties
    void setCompact(bool compact);

    /// Select of 32 px with a muted prefix before the value ("Sort: Last opened")
    void setSelectPrefix(const QString& prefix);

    /// Mixed state, see the class; no signal
    bool isMixed() const { return m_isMixed; }
    void setMixed(bool mixed);

    /// The current item before the mixed state (-1 outside of the state)
    int getIndexBeforeMixed() const { return m_isMixed ? m_indexBeforeMixed : -1; }

protected:
    virtual bool event(QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onCurrentIndexChanged(int index);
    void leaveMixed();

    /// The accessible description of the state (unless the application has its own)
    void updateMixedDescription();

    bool m_isMixed = false;
    int m_indexBeforeMixed = -1;
    QString m_placeholderBeforeMixed;
    QString m_mixedDescription;         ///< The description set by the state (empty: none)
};

/// Status bar. It draws the message by the colors of the theme.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStatusBar : public QStatusBar
{
    Q_OBJECT

public:
    explicit PDFStatusBar(QWidget* parent = nullptr);

    /// Creates an item of the status bar (text with an optional icon of the design)
    QToolButton* createItem(const QString& text, const QString& iconName = QString(), bool isWarning = false);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Segmented control: exclusive segments on a track
class PDF4QTLIBFLUENTSHARED_EXPORT PDFSegmentedControl : public QWidget
{
    Q_OBJECT

public:
    explicit PDFSegmentedControl(QWidget* parent = nullptr);

    /// Adds a segment, the first segment becomes the current one
    /// \returns Index of the segment
    int addSegment(const QString& text, const QString& iconName = QString());

    int getCount() const;
    int getCurrentIndex() const;
    void setCurrentIndex(int index);

    /// Sets the color of the icon of the segment in all its enabled states
    /// (the sun of the Light segment of the theme switch is icon/sun). A disabled
    /// segment has the icon icon/disabled.
    void setSegmentIconColor(int index, PDFThemeColors::Color color);

    /// All segments have the width of the widest one, a control of a given
    /// width (the theme switch of the sidebar) divides it equally
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    void currentIndexChanged(int index);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void onSegmentToggled(int id, bool isChecked);

    /// The layout is reachable by QWidget::layout(), the application may delete it
    QPointer<QHBoxLayout> m_layout;
    /// A member, not a child: the application cannot reach it by children() and delete it
    QButtonGroup m_group;
};

/// Dial. The value follows the pointer along the arc of the design, which has
/// 270 degrees. QDial maps the pointer to an arc of 300 degrees, so the handle
/// of a plain QDial drawn by the style does not lie exactly below the pointer.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDial : public QDial
{
    Q_OBJECT

public:
    explicit PDFDial(QWidget* parent = nullptr);

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int getValueFromPoint(const QPointF& point) const;
};

/// List view with the inner margin of the design around its rows
class PDF4QTLIBFLUENTSHARED_EXPORT PDFListView : public QListView
{
    Q_OBJECT

public:
    explicit PDFListView(QWidget* parent = nullptr);
};

/// Tree view with the inner margin of the design around its rows. A tree
/// with a visible header has no margin, as the table of the design.
///
/// The rows of the presets Outline, Contents and Certificate have the state Pressed of
/// the design (bg/pressed, bg/accent-subtle-pressed). Qt never sets
/// State_Sunken on the items of a view, so the view tracks the press of the
/// left button itself: the pressed row (all its cells) gets State_Sunken in
/// its style option, while the pointer stays over it with the button held. A
/// release, a move without the button (a release lost to a modal dialog or to a
/// drag), a leave without the button, the loss of the focus, a blocking modal
/// window, a drag, a hide and a change of the model end the press. The delegates
/// of the view are not replaced; a delegate of the application gets the flag too.
///
/// The focus ring of these presets (2 px inside, after the keyboard) encloses
/// the whole current row - the area of the branches and all columns - as the
/// design (191:805, 706:147113), not only the current cell. The selection of
/// these rows stays bg/accent-subtle, when the view has no focus (the current
/// outline item, certificate), as the rows of the delegates of the components.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFTreeView : public QTreeView
{
    Q_OBJECT

private:
    using BaseClass = QTreeView;

public:
    explicit PDFTreeView(QWidget* parent = nullptr);

    /// Look of the rows of the view (components of the design)
    enum class RowStyle
    {
        Default,        ///< Rows of the page Widgets: height 30, radius 6, padding 8
        Outline,        ///< Outline row: radius 8, paddings 6 / 8, indentation 18
        Contents,       ///< Contents of the manual (help-contents 955:152873): height 28, radius 6, chevron 14 at 8, gap 8, indentation 24
        Certificate,    ///< Certificate row: height 34, padding 10, the selection keeps the texts
        Library         ///< Library row: height 30, padding 10, no hover
    };

    RowStyle getRowStyle() const { return m_rowStyle; }
    void setRowStyle(RowStyle rowStyle);

    /// Returns the pressed row (its first column), an invalid index, when no row
    /// is pressed or the pointer has left it
    QModelIndex getPressedRow() const;

    /// Returns the radius of the shape of the rows of the preset (Outline 8, others 6)
    static qreal getRowRadius(RowStyle rowStyle);

protected:
    /// Returns true, if the focus ring of the current row of the preset is shown
    /// (the view has the focus after the keyboard); the gallery overrides it
    virtual bool isRowFocusRingVisible() const;

    /// Draws the focus ring of the preset around the row (2 px inside)
    void drawRowFocusRing(QPainter* painter, const QRect& rowRect) const;

    virtual bool event(QEvent* event) override;
    virtual void startDrag(Qt::DropActions supportedActions) override;
    virtual void updateGeometries() override;
    virtual void drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual bool viewportEvent(QEvent* event) override;

private:
    /// Returns true, if the preset has the state Pressed (Outline, Contents, Certificate)
    bool isPressTracked() const;
    void startPress(const QPoint& position, Qt::MouseButton button);
    void setPressedInside(bool isInside);
    void clearPress();
    void updateRow(const QModelIndex& index);

    bool m_isUpdatingMargins = false;
    RowStyle m_rowStyle = RowStyle::Default;
    QPersistentModelIndex m_pressedRow;
    bool m_isPressedInside = false;
    bool m_isMoveWithButton = false;
};

/// Creates the editors of items of views from the widgets of the library. It
/// can be used by a delegate (\p PDFItemDelegate), or it can be installed for
/// all views by QItemEditorFactory::setDefaultFactory.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFItemEditorFactory : public QItemEditorFactory
{
public:
    virtual QWidget* createEditor(int userType, QWidget* parent) const override;
    virtual QByteArray valuePropertyName(int userType) const override;
};

/// Delegate of views, whose editors are the widgets of the library
class PDF4QTLIBFLUENTSHARED_EXPORT PDFItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit PDFItemDelegate(QObject* parent = nullptr);

    virtual void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

}   // namespace pdf

#endif // PDFFLUENTWIDGETS_H
