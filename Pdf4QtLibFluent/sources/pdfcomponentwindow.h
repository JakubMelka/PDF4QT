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

#ifndef PDFCOMPONENTWINDOW_H
#define PDFCOMPONENTWINDOW_H

#include "pdffluentglobal.h"
#include "pdfcomponentpainter.h"

#include <QAbstractButton>
#include <QPointer>
#include <QWidget>

#include <array>
#include <optional>

namespace pdf
{

/// Button of the frameless application window (component Window button
/// 947:1475): Minimize, Maximize, Restore or Close, 46 x 38 with a glyph of
/// 16 px, or a traffic light of macOS (a circle of 12 px in a target of
/// 20 x 38). The button draws the variants Default, Hover, Pressed, Inactive
/// (the window is not active) and Disabled (Maximize of a window of a fixed
/// size: \p setEnabled(false)).
///
/// The hover and the press come from the mouse (Qt events) or from the
/// application (\p setExternalHover, \p setExternalPressed): on Windows the
/// main window answers WM_NCHITTEST with HTMAXBUTTON over the Maximize button,
/// so Windows 11 shows its Snap Layouts; the system then sends non-client
/// messages instead of the mouse events of Qt, and the window passes them to
/// the button (and calls \p click on WM_NCLBUTTONUP over it).
///
/// The button takes no keyboard focus (the window has its system menu). Its
/// accessible name and its tooltip are the name of the action ("Minimize",
/// "Maximize", "Restore", "Close"; on macOS "Close", "Minimize", "Zoom" without
/// a tooltip, as the native buttons), translated again after a change of the
/// language; a name or a tooltip set by the application is kept.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFWindowButton : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    using Kind = PDFComponentPainter::WindowButtonKind;
    using Platform = PDFComponentPainter::WindowPlatform;

    /// Activity of the window, which the button shows
    enum class Activity
    {
        FollowWindow,   ///< The state of the window of the button (QWidget::isActiveWindow)
        Active,         ///< Always the active variants
        Inactive        ///< Always the variant Inactive
    };
    Q_ENUM(Activity)

    explicit PDFWindowButton(QWidget* parent = nullptr);
    explicit PDFWindowButton(Kind kind, Platform platform = PDFComponentPainter::getNativeWindowPlatform(), QWidget* parent = nullptr);

    /// Kind of the button (an invalid value is Minimize)
    Kind getKind() const { return m_kind; }
    void setKind(Kind kind);

    /// Platform of the button (an invalid value is the native platform)
    Platform getPlatform() const { return m_platform; }
    void setPlatform(Platform platform);

    /// Activity of the window shown by the button (an invalid value is FollowWindow)
    Activity getActivity() const { return m_activity; }
    void setActivity(Activity activity);

    /// Returns true, if the button shows the active variants
    bool isWindowActive() const;

    /// Hover given by the application (the pointer is over the button in the
    /// non-client area of the window). It is joined with the hover of the mouse.
    bool isExternalHover() const { return m_externalHover; }
    void setExternalHover(bool hover);

    /// Press given by the application (the button is pressed in the non-client
    /// area of the window). It is joined with the press of the mouse. The
    /// application activates the button itself (\p click), when the press ends
    /// over it.
    bool isExternalPressed() const { return m_externalPressed; }
    void setExternalPressed(bool pressed);

    /// Returns the state, which the button draws (hover and press of the mouse
    /// and of the application, enabled state, layout direction)
    PDFComponentPainter::State getPaintedState() const;

    /// Returns the content, which the button draws
    PDFComponentPainter::WindowButton getPaintedButton() const;

    /// Returns the name of the action of the button ("Minimize", "Maximize",
    /// "Restore", "Close", "Zoom" for the maximize button of macOS), translated
    static QString getActionName(Kind kind, Platform platform);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    void init();

    /// Takes the name and the tooltip of the kind (a name or a tooltip of the application is kept)
    void updateTexts();

    Kind m_kind = Kind::Minimize;
    Platform m_platform = PDFComponentPainter::getNativeWindowPlatform();
    Activity m_activity = Activity::FollowWindow;
    bool m_externalHover = false;
    bool m_externalPressed = false;
    QString m_defaultToolTip;   ///< Tooltip set by the button (replaced only, if the application has not changed it)
};

/// Buttons of the frameless application window (component Window controls
/// 947:1504) for the top bar app-top-bar. Windows and Linux: Minimize, Maximize
/// (Restore, when \p setMaximized is true) and Close, 138 x 38, placed at the
/// right end of the bar, which keeps 46 px free for dragging the window before
/// them. macOS: the traffic lights close, minimize and zoom, 80 x 38, at the
/// left end of the bar, the tabs start after them; with \p setSystemButtons the
/// widget only keeps the place of the native buttons of the system (Qt keeps
/// them, when the content of the window extends into the title bar).
///
/// The buttons are child widgets (PDFWindowButton): each of them is a button
/// for assistive technologies with its own name. A right-to-left layout
/// mirrors the order of the buttons, as Windows mirrors the caption of a
/// window of a right-to-left layout and as the desktops of Linux do (Close is
/// then the leftmost button); the glyphs are not mirrored.
///
/// The group follows the window in two ways. The activity of the window
/// (variant Inactive) is followed by itself (\p setActivity overrides it). The
/// maximized state and the possibility to resize the window are told by the
/// application (\p setMaximized, \p setResizable), which knows them from the
/// events of its window.
///
/// For the non-client messages of Windows (WM_NCHITTEST answered by HTMAXBUTTON
/// over the Maximize button, which Windows 11 needs for its Snap Layouts) the
/// group gives the rectangles of its buttons in the coordinates of its window
/// (\p getButtonRect, \p getButtonAt), takes the hover and the press from the
/// window (\p setHoveredButton, \p setPressedButton) and activates a button
/// (\p activate). The signals are emitted after a click of a button, after
/// the press action of assistive technologies and by \p activate; the window
/// minimizes, maximizes or restores and closes itself.
///
/// The position of a non-client message of Windows is in the physical pixels
/// of the screen: convert it by ScreenToClient and divide it by
/// QWindow::devicePixelRatio (dividing the screen position and mapping it by
/// QWindow::mapFromGlobal is wrong on screens of different scales). The window
/// clears the press itself (WM_NCMOUSELEAVE, WM_LBUTTONUP in the client area,
/// WM_CAPTURECHANGED, deactivation), the release may come outside of the
/// button. On WM_NCLBUTTONUP it clears the press before \p activate, because
/// the slot of the signal may delete the group.
///
/// The group controls the enabled state of the middle button only
/// (\p setResizable) and hides the buttons only for the system buttons of
/// macOS: a button disabled or hidden by the application stays so.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFWindowControls : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using Kind = PDFComponentPainter::WindowButtonKind;
    using Platform = PDFComponentPainter::WindowPlatform;
    using Activity = PDFWindowButton::Activity;

    explicit PDFWindowControls(QWidget* parent = nullptr);
    explicit PDFWindowControls(Platform platform, QWidget* parent = nullptr);
    virtual ~PDFWindowControls() override;

    /// Platform of the buttons (the native platform by default; an invalid value is the native platform)
    Platform getPlatform() const { return m_platform; }
    void setPlatform(Platform platform);

    /// The window is maximized: the middle button is Restore (macOS: no change)
    bool isMaximized() const { return m_maximized; }
    void setMaximized(bool maximized);

    /// The window can be resized: false disables the Maximize (zoom) button
    bool isResizable() const { return m_resizable; }
    void setResizable(bool resizable);

    /// Activity of the window shown by the buttons (FollowWindow by default)
    Activity getActivity() const { return m_activity; }
    void setActivity(Activity activity);

    /// macOS only: the native traffic lights of the system are used. The group
    /// then draws nothing, has no buttons and takes no input, it keeps their
    /// place in the top bar (80 x 38). It has no effect on the other platforms.
    bool hasSystemButtons() const { return m_systemButtons; }
    void setSystemButtons(bool systemButtons);

    /// Returns true, if the buttons are drawn by the group (not the system buttons of macOS)
    bool hasOwnButtons() const;

    /// Returns the button of the kind (Maximize and Restore are the middle
    /// button), nullptr if the group has no such button (the system buttons of
    /// macOS, an invalid kind)
    PDFWindowButton* getButton(Kind kind) const;

    /// Returns the buttons in their logical (left-to-right) order (empty for the system buttons of macOS)
    QList<PDFWindowButton*> getButtons() const;

    /// Returns the rectangle of the button in the logical coordinates of the
    /// window of the group (QWidget::window), an empty rectangle if the group has
    /// no such button or the button is hidden (also by hiding the group or one
    /// of its parents)
    QRect getButtonRect(Kind kind) const;

    /// Returns the kind of the button at the position given in the logical
    /// coordinates of the window of the group, nothing outside of the buttons.
    /// The kind of the middle button is Maximize or Restore by the state.
    std::optional<Kind> getButtonAt(const QPoint& windowPosition) const;

    /// Hover of a button given by the window (non-client messages); nothing
    /// clears it. Maximize and Restore are the middle button.
    std::optional<Kind> getHoveredButton() const { return m_hoveredButton; }
    void setHoveredButton(std::optional<Kind> kind);

    /// Press of a button given by the window (non-client messages); nothing clears it
    std::optional<Kind> getPressedButton() const { return m_pressedButton; }
    void setPressedButton(std::optional<Kind> kind);

    /// Activates the button of the kind as its click does: emits its signal
    /// (its slot may delete the group). \returns true, if the button exists, is
    /// enabled and is not hidden
    bool activate(Kind kind);

    /// Returns the content, which the group draws by PDFComponentPainter (the gallery, the tests)
    PDFComponentPainter::WindowControls getPaintedControls() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The Minimize button has been activated
    void minimizeRequested();

    /// The Maximize or the Restore (macOS: zoom) button has been activated; the
    /// window maximizes or restores itself by its state
    void maximizeRestoreRequested();

    /// The Close button has been activated
    void closeRequested();

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void init();
    void updateButtons();
    void layoutButtons();
    void updateAccessibleName();
    void onMinimizeClicked();
    void onMaximizeClicked();
    void onCloseClicked();

    /// Index of the button of the kind in m_buttons (Minimize 0, Maximize / Restore 1, Close 2), -1 for an invalid kind
    static int getButtonIndex(Kind kind);

    Platform m_platform = PDFComponentPainter::getNativeWindowPlatform();
    bool m_maximized = false;
    bool m_resizable = true;
    bool m_systemButtons = false;
    Activity m_activity = Activity::FollowWindow;
    std::optional<Kind> m_hoveredButton;
    std::optional<Kind> m_pressedButton;
    QString m_defaultName;

    /// Buttons Minimize, Maximize / Restore, Close (children, deleted with the
    /// group or by the application)
    std::array<QPointer<PDFWindowButton>, 3> m_buttons;

    /// The button has been hidden by the group for the system buttons of macOS
    /// (it is shown again without them; a button hidden by the application is kept)
    std::array<bool, 3> m_hiddenByGroup = { };
};

}   // namespace pdf

#endif // PDFCOMPONENTWINDOW_H
