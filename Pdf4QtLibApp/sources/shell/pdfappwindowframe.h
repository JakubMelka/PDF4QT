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

#ifndef PDFAPPWINDOWFRAME_H
#define PDFAPPWINDOWFRAME_H

#include "pdfappglobal.h"
#include "pdfapptopbar.h"

#include <QObject>
#include <QPointer>
#include <QRect>

class QWidget;

namespace pdf
{

class PDFWindowControls;

/// The window without the frame of the system (decision 9, components.md 4.9).
///
/// Windows: the window keeps its normal styles (snapping, the shadow and the
/// animations of the system, the system menu): WM_NCCALCSIZE removes only the
/// title bar (the invisible resize frame of the left, right and bottom edge
/// stays with the system; a maximized window is moved by the thickness of the
/// frame into the work area), WM_NCHITTEST answers by PDFAppHitTest (the top
/// resize border, HTCAPTION on the free area of the top bar, HTMAXBUTTON over
/// the enabled Maximize button for the Snap Layouts of Windows 11, HTCLIENT
/// over the tabs and the buttons), and the non-client mouse messages over
/// Maximize are passed to the window controls. The positions of the messages
/// are physical pixels of the screen: ScreenToClient and the division by the
/// device pixel ratio of the window give the logical position.
///
/// Linux (X11 and Wayland) and macOS: the client area handles everything: a
/// press on the free area of the top bar starts QWindow::startSystemMove, a
/// press on an edge QWindow::startSystemResize; the edges show the resize
/// cursors. macOS: the content extends into the title bar
/// (Qt::ExpandedClientAreaHint) and the traffic lights of the system are used.
///
/// The frame is a helper of the main window, it lives as its child.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWindowFrame : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Kind of the frame handling
    enum class Mode
    {
        Windows,    ///< Native messages of Windows
        Client,     ///< System move / resize from the client area (Linux, other platforms)
        MacOS,      ///< macOS: expanded client area with the system traffic lights
        None        ///< Offscreen and other platforms without windows (tests): nothing
    };

    explicit PDFAppWindowFrame(QWidget* window, PDFAppTopBar* topBar);
    virtual ~PDFAppWindowFrame() override;

    /// Mode of the platform of the application
    static Mode getPlatformMode();

    Mode getMode() const { return m_mode; }

    /// Sets the flags of the window (before it is shown); called again, when the
    /// native window has been created again (QEvent::WinIdChange)
    void initialize();

    /// Called from QWidget::nativeEvent of the window
    bool handleNativeEvent(const QByteArray& eventType, void* message, qintptr* result);

    /// Thickness of the resize border in logical pixels
    int getResizeBorder() const;

    /// Hit test of the logical position of the window
    PDFAppHitTestResult hitTest(const QPoint& windowPosition) const;

    /// Starts the system move of the window (Client and MacOS modes)
    void startSystemMove();

    /// Inset of the client area of a maximized window at an edge of the monitor
    /// with an auto-hide taskbar (Windows): the taskbar appears, when the pointer
    /// touches that edge, a window covering the whole edge would hide it forever
    static constexpr int AUTO_HIDE_TASKBAR_INSET = 2;

    /// Client area of a maximized window (physical pixels, Windows) moved off
    /// the edges of the monitor, where an auto-hide taskbar is
    static QRect adjustMaximizedClientRect(const QRect& clientRect, Qt::Edges autoHideTaskbarEdges);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installWindowFilter();
    void updateCursor(const QPoint& windowPosition);
    void clearPress();

    PDFWindowControls* getControls() const;

    QPointer<QWidget> m_window;
    QPointer<PDFAppTopBar> m_topBar;
    QPointer<QObject> m_filteredWindow;
    Mode m_mode = Mode::None;
    bool m_isCursorOverridden = false;

    /// Native window, whose frame has been changed (a change of the flags of a
    /// widget creates a new native window, its frame is changed again)
    WId m_framedWindowId = 0;
};

}   // namespace pdf

#endif // PDFAPPWINDOWFRAME_H
