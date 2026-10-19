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

#include "pdfappwindowframe.h"

#include "pdfcomponentwindow.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace pdf
{

/// Constants of the frame
class PDFAppWindowFrameHelper
{
public:
    PDFAppWindowFrameHelper() = delete;

    /// Resize border in logical pixels: the 6 px above the shape of the tabs
    /// (components.md 4.9), the same on the other edges of the client area
    static constexpr int RESIZE_BORDER = 6;
};

PDFAppWindowFrame::PDFAppWindowFrame(QWidget* window, PDFAppTopBar* topBar) :
    BaseClass(window),
    m_window(window),
    m_topBar(topBar),
    m_mode(getPlatformMode())
{

}

PDFAppWindowFrame::~PDFAppWindowFrame()
{
    // The filter is removed from a window, which outlives the frame
    if (m_filteredWindow)
    {
        m_filteredWindow->removeEventFilter(this);
    }
}

PDFAppWindowFrame::Mode PDFAppWindowFrame::getPlatformMode()
{
    const QString platform = QGuiApplication::platformName();

#if defined(Q_OS_WIN)
    if (platform == QLatin1String("windows"))
    {
        return Mode::Windows;
    }
#elif defined(Q_OS_MACOS)
    if (platform == QLatin1String("cocoa"))
    {
        return Mode::MacOS;
    }
#endif

    if (platform.startsWith(QLatin1String("xcb")) || platform.startsWith(QLatin1String("wayland")))
    {
        return Mode::Client;
    }

    return Mode::None;
}

PDFWindowControls* PDFAppWindowFrame::getControls() const
{
    return m_topBar ? m_topBar->getWindowControls() : nullptr;
}

void PDFAppWindowFrame::initialize()
{
    if (!m_window)
    {
        return;
    }

    switch (m_mode)
    {
        case Mode::Windows:
        {
#if defined(Q_OS_WIN)
            // The normal styles stay (snapping, shadow, system menu); the title bar is
            // removed by WM_NCCALCSIZE, which is sent again by the change of the frame
            const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
            if (hwnd && !m_isFrameChanged)
            {
                m_isFrameChanged = true;
                SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
            }
#endif
            break;
        }

        case Mode::Client:
            // Linux: no decoration of the window manager or of the compositor (client
            // side decoration of Wayland is the top bar of the window)
            m_window->setWindowFlag(Qt::FramelessWindowHint, true);
            installWindowFilter();
            break;

        case Mode::MacOS:
            // The content extends into the title bar, the traffic lights of the system stay
            m_window->setWindowFlag(Qt::ExpandedClientAreaHint, true);
            m_window->setWindowFlag(Qt::NoTitleBarBackgroundHint, true);
            if (PDFWindowControls* controls = getControls())
            {
                controls->setSystemButtons(true);
            }
            installWindowFilter();
            break;

        case Mode::None:
            break;
    }
}

void PDFAppWindowFrame::installWindowFilter()
{
    if (!m_window)
    {
        return;
    }

    // The window of the widget receives all mouse events, also those over children
    m_window->winId();
    QWindow* windowHandle = m_window->windowHandle();
    if (windowHandle && m_filteredWindow != windowHandle)
    {
        if (m_filteredWindow)
        {
            m_filteredWindow->removeEventFilter(this);
        }
        m_filteredWindow = windowHandle;
        windowHandle->installEventFilter(this);
    }
}

int PDFAppWindowFrame::getResizeBorder() const
{
    return m_mode == Mode::MacOS ? 0 : PDFAppWindowFrameHelper::RESIZE_BORDER;
}

PDFAppHitTestResult PDFAppWindowFrame::hitTest(const QPoint& windowPosition) const
{
    if (!m_window || !m_topBar)
    {
        return PDFAppHitTestResult::Client;
    }

    PDFAppHitTestInput input = m_topBar->createHitTestInput(m_window, windowPosition);
    input.isMaximized = m_window->isMaximized();
    input.isFullScreen = m_window->isFullScreen();
    input.isResizable = m_window->minimumSize() != m_window->maximumSize();
    input.resizeBorder = getResizeBorder();
    input.hasSideBorders = m_mode != Mode::Windows;
    return PDFAppHitTest::hitTest(input);
}

void PDFAppWindowFrame::startSystemMove()
{
    if (!m_window || (m_mode != Mode::Client && m_mode != Mode::MacOS))
    {
        return;
    }

    if (QWindow* windowHandle = m_window->windowHandle())
    {
        windowHandle->startSystemMove();
    }
}

void PDFAppWindowFrame::updateCursor(const QPoint& windowPosition)
{
    if (!m_window)
    {
        return;
    }

    const Qt::CursorShape shape = PDFAppHitTest::getCursorShape(hitTest(windowPosition));
    if (shape != Qt::ArrowCursor)
    {
        m_window->setCursor(shape);
        m_isCursorOverridden = true;
    }
    else if (m_isCursorOverridden)
    {
        m_window->unsetCursor();
        m_isCursorOverridden = false;
    }
}

bool PDFAppWindowFrame::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_filteredWindow || !m_window || m_mode != Mode::Client)
    {
        return BaseClass::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::MouseMove:
        {
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (mouseEvent->buttons() == Qt::NoButton)
            {
                updateCursor(mouseEvent->position().toPoint());
            }
            break;
        }

        case QEvent::MouseButtonPress:
        {
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                const Qt::Edges edges = PDFAppHitTest::getEdges(hitTest(mouseEvent->position().toPoint()));
                QWindow* windowHandle = m_window->windowHandle();
                if (edges && windowHandle && windowHandle->startSystemResize(edges))
                {
                    return true;
                }
            }
            break;
        }

        case QEvent::Leave:
            if (m_isCursorOverridden)
            {
                m_window->unsetCursor();
                m_isCursorOverridden = false;
            }
            break;

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppWindowFrame::clearPress()
{
    if (PDFWindowControls* controls = getControls())
    {
        controls->setPressedButton(std::nullopt);
    }
}

bool PDFAppWindowFrame::handleNativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#if defined(Q_OS_WIN)
    if (m_mode != Mode::Windows || !m_window || !message || !result || eventType != QByteArrayLiteral("windows_generic_MSG"))
    {
        return false;
    }

    MSG* msg = static_cast<MSG*>(message);
    const HWND hwnd = reinterpret_cast<HWND>(m_window->internalWinId());
    if (!hwnd || msg->hwnd != hwnd)
    {
        return false;
    }

    using Kind = PDFWindowControls::Kind;
    PDFWindowControls* controls = getControls();

    switch (msg->message)
    {
        case WM_NCCALCSIZE:
        {
            if (msg->wParam != TRUE)
            {
                return false;
            }

            // The frame of the system keeps the left, right and bottom edge (resize,
            // shadow); the title bar is the top bar of the window
            NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            const LONG originalTop = params->rgrc[0].top;
            const LRESULT defaultResult = DefWindowProcW(hwnd, WM_NCCALCSIZE, msg->wParam, msg->lParam);
            if (defaultResult != 0)
            {
                *result = defaultResult;
                return true;
            }

            params->rgrc[0].top = originalTop;
            if (IsZoomed(hwnd) && !m_window->isFullScreen())
            {
                // A maximized window reaches beyond the screen by its frame
                const UINT dpi = GetDpiForWindow(hwnd);
                params->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            *result = 0;
            return true;
        }

        case WM_NCHITTEST:
        {
            // The edges of the frame of the system
            const LRESULT defaultResult = DefWindowProcW(hwnd, WM_NCHITTEST, msg->wParam, msg->lParam);
            if (defaultResult != HTCLIENT)
            {
                *result = defaultResult;
                return true;
            }

            // Physical pixels of the screen -> logical coordinates of the window
            POINT point = { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
            ScreenToClient(hwnd, &point);
            const QWindow* windowHandle = m_window->windowHandle();
            const qreal devicePixelRatio = windowHandle ? windowHandle->devicePixelRatio() : m_window->devicePixelRatioF();
            const QPoint position = PDFAppHitTest::toLogicalPosition(QPoint(point.x, point.y), devicePixelRatio);

            switch (hitTest(position))
            {
                case PDFAppHitTestResult::Client:
                    *result = HTCLIENT;
                    break;
                case PDFAppHitTestResult::Caption:
                    *result = HTCAPTION;
                    break;
                case PDFAppHitTestResult::MaximizeButton:
                    *result = HTMAXBUTTON;
                    break;
                case PDFAppHitTestResult::Top:
                    *result = HTTOP;
                    break;
                case PDFAppHitTestResult::TopLeft:
                    *result = HTTOPLEFT;
                    break;
                case PDFAppHitTestResult::TopRight:
                    *result = HTTOPRIGHT;
                    break;
                case PDFAppHitTestResult::Left:
                    *result = HTLEFT;
                    break;
                case PDFAppHitTestResult::Right:
                    *result = HTRIGHT;
                    break;
                case PDFAppHitTestResult::Bottom:
                    *result = HTBOTTOM;
                    break;
                case PDFAppHitTestResult::BottomLeft:
                    *result = HTBOTTOMLEFT;
                    break;
                case PDFAppHitTestResult::BottomRight:
                    *result = HTBOTTOMRIGHT;
                    break;
            }
            return true;
        }

        case WM_NCMOUSEMOVE:
        case WM_NCMOUSEHOVER:
        {
            if (controls)
            {
                if (msg->wParam == HTMAXBUTTON)
                {
                    controls->setHoveredButton(Kind::Maximize);

                    // WM_NCMOUSELEAVE clears the hover
                    TRACKMOUSEEVENT track = { };
                    track.cbSize = sizeof(track);
                    track.dwFlags = TME_LEAVE | TME_NONCLIENT;
                    track.hwndTrack = hwnd;
                    TrackMouseEvent(&track);
                }
                else
                {
                    controls->setHoveredButton(std::nullopt);
                }
            }

            // The system shows the Snap Layouts (default processing)
            return false;
        }

        case WM_NCMOUSELEAVE:
            if (controls)
            {
                controls->setHoveredButton(std::nullopt);
                controls->setPressedButton(std::nullopt);
            }
            return false;

        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
            if (msg->wParam == HTMAXBUTTON && controls)
            {
                // Consumed: Windows would draw its own button
                controls->setPressedButton(Kind::Maximize);
                *result = 0;
                return true;
            }
            clearPress();
            return false;

        case WM_NCLBUTTONUP:
            if (msg->wParam == HTMAXBUTTON && controls && controls->getPressedButton() == Kind::Maximize)
            {
                // The press is cleared first: the slot of the signal may delete the group
                controls->setPressedButton(std::nullopt);
                controls->activate(Kind::Maximize);
                *result = 0;
                return true;
            }
            clearPress();
            return false;

        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            clearPress();
            return false;

        case WM_ACTIVATE:
            if (LOWORD(msg->wParam) == WA_INACTIVE)
            {
                clearPress();
            }
            return false;

        default:
            break;
    }

    return false;
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

}   // namespace pdf
