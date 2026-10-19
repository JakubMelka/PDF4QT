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

// Drawing of the buttons of the frameless application window (components
// Window button 947:1475 and Window controls 947:1504). The class is declared
// in pdfcomponentpainter.h; this file holds the geometry constants of the two
// components.

#include "pdfcomponentpainter.h"
#include "pdfstylepainterguard.h"

#include <QPainter>
#include <QtMath>

namespace pdf
{

/// Geometry constants of the window buttons
class PDFComponentWindowPainterHelper
{
public:
    PDFComponentWindowPainterHelper() = delete;

    // Window button 947:1475: 46 x 38, glyph 16 centered, no radius
    static constexpr int BUTTON_WIDTH = 46;
    static constexpr int BUTTON_HEIGHT = 38;
    static constexpr int BUTTON_ICON = 16;

    // Window controls 947:1504, Platform=macOS: circles of 12 px with gaps of
    // 8 px and margins of 14 px (80 x 38); the target of a circle takes the half
    // of the gap at each side (20 x 38), so the margin outside of the targets is 10
    static constexpr int CIRCLE = 12;
    static constexpr int CIRCLE_GAP = 8;
    static constexpr int MAC_MARGIN = 14;
    static constexpr int MAC_TARGET_WIDTH = CIRCLE + CIRCLE_GAP;
    static constexpr int MAC_TARGET_MARGIN = MAC_MARGIN - CIRCLE_GAP / 2;

    /// Returns true, if the rectangle can be drawn into (not empty, finite)
    static bool isUsable(const QRectF& rect)
    {
        return !rect.isEmpty() && qIsFinite(rect.left()) && qIsFinite(rect.top()) && qIsFinite(rect.width()) && qIsFinite(rect.height());
    }

    /// Returns true, if the two kinds are the same button of the group
    /// (Maximize and Restore are the middle button)
    static bool isSameButton(PDFComponentPainter::WindowButtonKind first, PDFComponentPainter::WindowButtonKind second)
    {
        return getSlot(first) == getSlot(second);
    }

    /// Returns the button of the group of the kind (Restore is the Maximize button)
    static PDFComponentPainter::WindowButtonKind getSlot(PDFComponentPainter::WindowButtonKind kind)
    {
        using Kind = PDFComponentPainter::WindowButtonKind;
        const Kind normalizedKind = PDFComponentPainter::normalized(kind);
        return normalizedKind == Kind::Restore ? Kind::Maximize : normalizedKind;
    }
};

PDFComponentPainter::WindowButtonKind PDFComponentPainter::normalized(WindowButtonKind kind)
{
    switch (kind)
    {
        case WindowButtonKind::Minimize:
        case WindowButtonKind::Maximize:
        case WindowButtonKind::Restore:
        case WindowButtonKind::Close:
            return kind;
    }

    return WindowButtonKind::Minimize;
}

PDFComponentPainter::WindowPlatform PDFComponentPainter::normalized(WindowPlatform platform)
{
    switch (platform)
    {
        case WindowPlatform::Windows:
        case WindowPlatform::Linux:
        case WindowPlatform::MacOS:
            return platform;
    }

    return getNativeWindowPlatform();
}

PDFComponentPainter::WindowPlatform PDFComponentPainter::getNativeWindowPlatform()
{
#if defined(Q_OS_MACOS)
    return WindowPlatform::MacOS;
#elif defined(Q_OS_WIN)
    return WindowPlatform::Windows;
#else
    return WindowPlatform::Linux;
#endif
}

QString PDFComponentPainter::getWindowButtonIconName(WindowButtonKind kind)
{
    switch (normalized(kind))
    {
        case WindowButtonKind::Minimize:
            return QStringLiteral("window-minimize");
        case WindowButtonKind::Maximize:
            return QStringLiteral("window-maximize");
        case WindowButtonKind::Restore:
            return QStringLiteral("window-restore");
        case WindowButtonKind::Close:
            return QStringLiteral("window-close");
    }

    return QString();
}

QSize PDFComponentPainter::getWindowButtonSize(WindowPlatform platform)
{
    using Helper = PDFComponentWindowPainterHelper;

    if (normalized(platform) == WindowPlatform::MacOS)
    {
        return QSize(Helper::MAC_TARGET_WIDTH, Helper::BUTTON_HEIGHT);
    }

    return QSize(Helper::BUTTON_WIDTH, Helper::BUTTON_HEIGHT);
}

int PDFComponentPainter::getWindowButtonIconExtent()
{
    return PDFComponentWindowPainterHelper::BUTTON_ICON;
}

int PDFComponentPainter::getWindowButtonCircleExtent()
{
    return PDFComponentWindowPainterHelper::CIRCLE;
}

PDFThemeColors::Color PDFComponentPainter::getWindowButtonIconColor(const WindowButton& button, const State& state)
{
    if (!state.isEnabled())
    {
        return PDFThemeColors::IconDisabled;
    }

    // Hover and press look the same in an active and in an inactive window
    if (state.isPressed() || state.isHovered())
    {
        return normalized(button.kind) == WindowButtonKind::Close ? PDFThemeColors::IconOnAccent : PDFThemeColors::IconStrong;
    }

    return button.windowActive ? PDFThemeColors::IconDefault : PDFThemeColors::IconDisabled;
}

std::optional<PDFThemeColors::Color> PDFComponentPainter::getWindowButtonBackground(const WindowButton& button, const State& state)
{
    const bool isClose = normalized(button.kind) == WindowButtonKind::Close;

    if (state.isPressed())
    {
        return isClose ? PDFThemeColors::BgDangerPressed : PDFThemeColors::BgPressed;
    }

    if (state.isHovered())
    {
        return isClose ? PDFThemeColors::BgDanger : PDFThemeColors::BgHover;
    }

    return std::nullopt;
}

PDFThemeColors::Color PDFComponentPainter::getWindowButtonCircleColor(const WindowButton& button, const State& state)
{
    if (!state.isEnabled() || !button.windowActive)
    {
        return PDFThemeColors::WindowMacosInactive;
    }

    switch (normalized(button.kind))
    {
        case WindowButtonKind::Minimize:
            return PDFThemeColors::WindowMacosMinimize;
        case WindowButtonKind::Maximize:
        case WindowButtonKind::Restore:
            return PDFThemeColors::WindowMacosZoom;
        case WindowButtonKind::Close:
            return PDFThemeColors::WindowMacosClose;
    }

    return PDFThemeColors::WindowMacosInactive;
}

void PDFComponentPainter::paintWindowButton(QPainter* painter, const QRectF& rect, const WindowButton& button, const State& state)
{
    using Helper = PDFComponentWindowPainterHelper;

    if (!painter || !Helper::isUsable(rect))
    {
        return;
    }

    const DirectionGuard directionGuard(painter, state);

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (normalized(button.platform) == WindowPlatform::MacOS)
    {
        const qreal extent = qMin<qreal>(Helper::CIRCLE, qMin(rect.width(), rect.height()));
        PDFStylePainter::fillEllipse(painter, alignedCentered(rect, QSizeF(extent, extent)), PDFStylePainter::color(getWindowButtonCircleColor(button, state)));
    }
    else
    {
        // The background fills the whole button (it touches the edges of the window)
        if (const std::optional<PDFThemeColors::Color> background = getWindowButtonBackground(button, state))
        {
            PDFStylePainter::fillRect(painter, rect, *background);
        }

        const qreal extent = qMin<qreal>(Helper::BUTTON_ICON, qMin(rect.width(), rect.height()));
        PDFStylePainter::icon(painter, alignedCentered(rect, QSizeF(extent, extent)), getWindowButtonIconName(button.kind), getWindowButtonIconColor(button, state));
    }
}

QList<PDFComponentPainter::WindowButtonKind> PDFComponentPainter::getWindowControlsButtons(const WindowControls& controls)
{
    const WindowButtonKind middle = (controls.maximized && normalized(controls.platform) != WindowPlatform::MacOS) ? WindowButtonKind::Restore : WindowButtonKind::Maximize;

    if (normalized(controls.platform) == WindowPlatform::MacOS)
    {
        return { WindowButtonKind::Close, WindowButtonKind::Minimize, middle };
    }

    return { WindowButtonKind::Minimize, middle, WindowButtonKind::Close };
}

QSize PDFComponentPainter::getWindowControlsSize(WindowPlatform platform)
{
    using Helper = PDFComponentWindowPainterHelper;

    if (normalized(platform) == WindowPlatform::MacOS)
    {
        return QSize(2 * Helper::MAC_MARGIN + 3 * Helper::CIRCLE + 2 * Helper::CIRCLE_GAP, Helper::BUTTON_HEIGHT);
    }

    return QSize(3 * Helper::BUTTON_WIDTH, Helper::BUTTON_HEIGHT);
}

QRectF PDFComponentPainter::getWindowControlsButtonRect(const QRectF& rect, const WindowControls& controls, WindowButtonKind kind, const State& state)
{
    using Helper = PDFComponentWindowPainterHelper;

    if (!Helper::isUsable(rect) || normalized(kind) != kind)
    {
        return QRectF();
    }

    const WindowPlatform platform = normalized(controls.platform);
    const QList<WindowButtonKind> buttons = getWindowControlsButtons(controls);
    const qreal width = getWindowButtonSize(platform).width();
    qreal left = rect.left() + (platform == WindowPlatform::MacOS ? Helper::MAC_TARGET_MARGIN : 0);

    for (const WindowButtonKind button : buttons)
    {
        if (Helper::isSameButton(button, kind))
        {
            return state.visualRect(rect, QRectF(left, rect.top(), width, rect.height()));
        }
        left += width;
    }

    return QRectF();
}

std::optional<PDFComponentPainter::WindowButtonKind> PDFComponentPainter::getWindowControlsButtonAt(const QRectF& rect, const WindowControls& controls, const QPointF& point, const State& state)
{
    for (const WindowButtonKind button : getWindowControlsButtons(controls))
    {
        const QRectF buttonRect = getWindowControlsButtonRect(rect, controls, button, state);
        if (!buttonRect.isEmpty() && buttonRect.contains(point))
        {
            return button;
        }
    }

    return std::nullopt;
}

void PDFComponentPainter::paintWindowControls(QPainter* painter, const QRectF& rect, const WindowControls& controls, const State& state)
{
    using Helper = PDFComponentWindowPainterHelper;

    if (!painter || !Helper::isUsable(rect))
    {
        return;
    }

    const WindowPlatform platform = normalized(controls.platform);

    // A rectangle smaller than the group cuts the buttons off
    const PDFStylePainterGuard guard(painter);
    painter->setClipRect(rect, Qt::IntersectClip);

    for (const WindowButtonKind kind : getWindowControlsButtons(controls))
    {
        const QRectF buttonRect = getWindowControlsButtonRect(rect, controls, kind, state);
        if (buttonRect.isEmpty())
        {
            continue;
        }

        const bool isMaximize = kind == WindowButtonKind::Maximize || kind == WindowButtonKind::Restore;
        QStyle::State flags = state.flags & ~(QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus);
        if (isMaximize && !controls.resizable)
        {
            flags &= ~QStyle::State_Enabled;
        }
        if (controls.hovered.has_value() && Helper::isSameButton(*controls.hovered, kind))
        {
            flags |= QStyle::State_MouseOver;
        }
        if (controls.pressed.has_value() && Helper::isSameButton(*controls.pressed, kind))
        {
            flags |= QStyle::State_Sunken;
        }

        const WindowButton button{ kind, platform, controls.windowActive };
        paintWindowButton(painter, buttonRect, button, State(flags, state.direction));
    }
}

}   // namespace pdf
