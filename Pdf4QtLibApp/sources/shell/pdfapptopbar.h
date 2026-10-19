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

#ifndef PDFAPPTOPBAR_H
#define PDFAPPTOPBAR_H

#include "pdfappglobal.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentwindow.h"

#include <QList>
#include <QPointer>
#include <QRect>
#include <QToolButton>
#include <QWidget>

namespace pdf
{

/// Result of the hit test of the frameless window (WM_NCHITTEST on Windows, the
/// system move and resize on Linux and macOS)
enum class PDFAppHitTestResult
{
    Client,         ///< A control of the window (tab, button, page)
    Caption,        ///< The free area of the top bar (moves the window, double click maximizes)
    MaximizeButton, ///< The Maximize / Restore button (HTMAXBUTTON, Snap Layouts of Windows 11)
    Top,
    TopLeft,
    TopRight,
    Left,
    Right,
    Bottom,
    BottomLeft,
    BottomRight
};

/// Input of the hit test, everything in the logical coordinates of the window
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHitTestInput
{
public:
    /// Size of the window (its client area)
    QSize windowSize;

    /// Position of the pointer
    QPoint position;

    bool isMaximized = false;
    bool isFullScreen = false;

    /// The window can be resized (not a window of a fixed size)
    bool isResizable = true;

    /// Thickness of the border, where the window is resized
    int resizeBorder = 6;

    /// The client area has the borders on the left, right and bottom edge too
    /// (Linux, macOS); on Windows the frame of the system has them, only the top
    /// border lies in the client area
    bool hasSideBorders = true;

    /// Height of the top bar
    int topBarHeight = 38;

    /// Rectangle of the enabled Maximize / Restore button (empty: none, disabled or hidden)
    QRect maximizeButtonRect;

    /// Rectangles of the controls in the top bar (tabs with their close buttons,
    /// the new tab button, the other window buttons): HTCLIENT
    QList<QRect> interactiveRects;
};

/// Hit test of the frameless window as a pure function (newapplication.md 3,
/// components.md 4.9): resize borders only in a normal window, the Maximize
/// button, the controls of the top bar and the free area of the top bar
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHitTest
{
public:
    PDFAppHitTest() = delete;

    static PDFAppHitTestResult hitTest(const PDFAppHitTestInput& input);

    /// Logical position of the window from the physical position of the client
    /// area (ScreenToClient of Windows) divided by the device pixel ratio of the
    /// window (the window may be on a screen of another scale than the others)
    static QPoint toLogicalPosition(const QPoint& physicalClientPosition, qreal devicePixelRatio);

    /// Edges of the window of a resize result (Qt::Edges() for the others)
    static Qt::Edges getEdges(PDFAppHitTestResult result);

    /// Cursor of a resize result (Qt::ArrowCursor for the others)
    static Qt::CursorShape getCursorShape(PDFAppHitTestResult result);
};

/// Tab bar of the top bar: it is never wider than its tabs (QTabBar with
/// scroll buttons has a minimal width of its own)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTabBar : public PDFDocumentTabBar
{
    Q_OBJECT

private:
    using BaseClass = PDFDocumentTabBar;

public:
    explicit PDFAppTabBar(QWidget* parent = nullptr);

    virtual QSize minimumSizeHint() const override;
};

/// Top bar of the window without the frame of the system (app-top-bar, 38 px,
/// bg/titlebar): the tabs (PDFDocumentTabBar, the tabs lie at the bottom, each
/// has the 6 px above it in its rectangle), the New tab button, the free area
/// for moving the window and the window controls at the trailing end (macOS:
/// the traffic lights of the system at the leading end). Paddings 16 and 200
/// (16 + 46 free + 138) of the design.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTopBar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int HEIGHT = 38;

    explicit PDFAppTopBar(QWidget* parent = nullptr);
    virtual ~PDFAppTopBar() override;

    PDFDocumentTabBar* getTabBar() const { return m_tabBar; }
    QToolButton* getNewTabButton() const { return m_newTabButton; }
    PDFWindowControls* getWindowControls() const { return m_windowControls; }

    /// Input of the hit test for the position in the coordinates of \p window
    /// (the window of the top bar); the caller fills the state of the window
    PDFAppHitTestInput createHitTestInput(const QWidget* window, const QPoint& windowPosition) const;

Q_SIGNALS:
    /// The New tab button has been clicked
    void newTabRequested();

    /// Double click on the free area of the bar (maximize / restore)
    void maximizeRestoreRequested();

    /// A press of the left button on the free area of the bar (moves the window
    /// on the platforms, where Qt moves it - not Windows, where it is HTCAPTION)
    void moveRequested();

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateMargins();
    void onTabBarDoubleClicked(int index);
    bool isInTabBar(const QPoint& position) const;

    QPointer<PDFDocumentTabBar> m_tabBar;
    QPointer<QToolButton> m_newTabButton;
    QPointer<PDFWindowControls> m_windowControls;
};

}   // namespace pdf

#endif // PDFAPPTOPBAR_H
