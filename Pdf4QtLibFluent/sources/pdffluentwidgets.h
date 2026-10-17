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
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QItemEditorFactory>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QDial>
#include <QTextEdit>
#include <QTreeView>

class QAction;
class QButtonGroup;
class QHBoxLayout;
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

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void init();

    PDFThemeFonts::Role m_fontRole = PDFThemeFonts::Role::Body;
    PDFThemeColors::Color m_color = PDFThemeColors::TextPrimary;
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
    void setPasswordField();

    /// Returns true, if the field is a password field
    bool isPasswordField() const { return m_passwordButton != nullptr; }

    /// Returns the button, which shows the password, nullptr, if the field is not a password field
    PDFIconButton* getPasswordButton() const { return m_passwordButton; }

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void init();
    void updateColors();
    void updatePasswordFont();
    void updatePasswordButton();
    void placePasswordButton();

    PDFIconButton* m_passwordButton = nullptr;
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

/// Spin box. The prefix and the suffix set by \p setDisplayPrefix and
/// \p setDisplaySuffix are drawn muted beside the value, as the design has them
/// (the prefix and the suffix of QSpinBox are a part of the edited text).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFSpinBox : public QSpinBox
{
    Q_OBJECT

public:
    explicit PDFSpinBox(QWidget* parent = nullptr);

    const QString& getDisplayPrefix() const { return m_displayPrefix; }
    void setDisplayPrefix(const QString& prefix);

    const QString& getDisplaySuffix() const { return m_displaySuffix; }
    void setDisplaySuffix(const QString& suffix);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_displayPrefix;
    QString m_displaySuffix;
};

/// Spin box of real numbers, see \p PDFSpinBox
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDoubleSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    explicit PDFDoubleSpinBox(QWidget* parent = nullptr);

    const QString& getDisplayPrefix() const { return m_displayPrefix; }
    void setDisplayPrefix(const QString& prefix);

    const QString& getDisplaySuffix() const { return m_displaySuffix; }
    void setDisplaySuffix(const QString& suffix);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_displayPrefix;
    QString m_displaySuffix;
};

/// Calendar of the design: cells of days with rounded shapes, no grid
class PDF4QTLIBFLUENTSHARED_EXPORT PDFCalendarWidget : public QCalendarWidget
{
    Q_OBJECT

public:
    explicit PDFCalendarWidget(QWidget* parent = nullptr);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateColors();

    QWidget* m_viewport = nullptr;
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

private:
    void init();
    void ensureCalendar();
};

/// Combo box
class PDF4QTLIBFLUENTSHARED_EXPORT PDFComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit PDFComboBox(QWidget* parent = nullptr);

    /// Compact variant of 28 px used in rows of properties
    void setCompact(bool compact);

    /// Select of 32 px with a muted prefix before the value ("Sort: Last opened")
    void setSelectPrefix(const QString& prefix);

protected:
    virtual bool event(QEvent* event) override;
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
    QHBoxLayout* m_layout;
    QButtonGroup* m_group;
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
/// The rows of the presets Outline and Certificate have the state Pressed of
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
    /// Returns true, if the preset has the state Pressed (Outline, Certificate)
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
