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

#include "pdfapptopbar.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentwindow.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>

#include <cmath>

namespace pdf
{

PDFAppHitTestResult PDFAppHitTest::hitTest(const PDFAppHitTestInput& input)
{
    const int width = input.windowSize.width();
    const int height = input.windowSize.height();
    const QPoint& position = input.position;

    if (width <= 0 || height <= 0 || position.x() < 0 || position.y() < 0 || position.x() >= width || position.y() >= height)
    {
        return PDFAppHitTestResult::Client;
    }

    // Resize borders only in a normal window (a maximized window has the bar at
    // the top edge of the screen, its tabs are hit at the edge)
    const bool canResize = input.isResizable && !input.isMaximized && !input.isFullScreen && input.resizeBorder > 0;
    if (canResize)
    {
        const int border = qMin(input.resizeBorder, qMax(1, qMin(width, height) / 4));
        const int corner = 2 * border;
        const bool isTop = position.y() < border;
        const bool isBottom = input.hasSideBorders && position.y() >= height - border;
        const bool isLeft = input.hasSideBorders && position.x() < border;
        const bool isRight = input.hasSideBorders && position.x() >= width - border;
        const bool isNearLeft = position.x() < corner;
        const bool isNearRight = position.x() >= width - corner;
        const bool isNearTop = position.y() < corner;
        const bool isNearBottom = position.y() >= height - corner;

        if (isTop)
        {
            return isNearLeft ? PDFAppHitTestResult::TopLeft : (isNearRight ? PDFAppHitTestResult::TopRight : PDFAppHitTestResult::Top);
        }
        if (isBottom)
        {
            return isNearLeft ? PDFAppHitTestResult::BottomLeft : (isNearRight ? PDFAppHitTestResult::BottomRight : PDFAppHitTestResult::Bottom);
        }
        if (isLeft)
        {
            return isNearTop ? PDFAppHitTestResult::TopLeft : (isNearBottom ? PDFAppHitTestResult::BottomLeft : PDFAppHitTestResult::Left);
        }
        if (isRight)
        {
            return isNearTop ? PDFAppHitTestResult::TopRight : (isNearBottom ? PDFAppHitTestResult::BottomRight : PDFAppHitTestResult::Right);
        }
    }

    if (position.y() < input.topBarHeight)
    {
        if (!input.maximizeButtonRect.isEmpty() && input.maximizeButtonRect.contains(position))
        {
            return PDFAppHitTestResult::MaximizeButton;
        }

        for (const QRect& rect : input.interactiveRects)
        {
            if (rect.contains(position))
            {
                return PDFAppHitTestResult::Client;
            }
        }

        return PDFAppHitTestResult::Caption;
    }

    return PDFAppHitTestResult::Client;
}

QPoint PDFAppHitTest::toLogicalPosition(const QPoint& physicalClientPosition, qreal devicePixelRatio)
{
    if (!std::isfinite(devicePixelRatio) || devicePixelRatio <= 0.0)
    {
        devicePixelRatio = 1.0;
    }

    return QPoint(qFloor(physicalClientPosition.x() / devicePixelRatio), qFloor(physicalClientPosition.y() / devicePixelRatio));
}

Qt::Edges PDFAppHitTest::getEdges(PDFAppHitTestResult result)
{
    switch (result)
    {
        case PDFAppHitTestResult::Top:
            return Qt::TopEdge;
        case PDFAppHitTestResult::TopLeft:
            return Qt::TopEdge | Qt::LeftEdge;
        case PDFAppHitTestResult::TopRight:
            return Qt::TopEdge | Qt::RightEdge;
        case PDFAppHitTestResult::Left:
            return Qt::LeftEdge;
        case PDFAppHitTestResult::Right:
            return Qt::RightEdge;
        case PDFAppHitTestResult::Bottom:
            return Qt::BottomEdge;
        case PDFAppHitTestResult::BottomLeft:
            return Qt::BottomEdge | Qt::LeftEdge;
        case PDFAppHitTestResult::BottomRight:
            return Qt::BottomEdge | Qt::RightEdge;
        case PDFAppHitTestResult::Client:
        case PDFAppHitTestResult::Caption:
        case PDFAppHitTestResult::MaximizeButton:
            break;
    }
    return Qt::Edges();
}

Qt::CursorShape PDFAppHitTest::getCursorShape(PDFAppHitTestResult result)
{
    switch (result)
    {
        case PDFAppHitTestResult::Top:
        case PDFAppHitTestResult::Bottom:
            return Qt::SizeVerCursor;
        case PDFAppHitTestResult::Left:
        case PDFAppHitTestResult::Right:
            return Qt::SizeHorCursor;
        case PDFAppHitTestResult::TopLeft:
        case PDFAppHitTestResult::BottomRight:
            return Qt::SizeFDiagCursor;
        case PDFAppHitTestResult::TopRight:
        case PDFAppHitTestResult::BottomLeft:
            return Qt::SizeBDiagCursor;
        case PDFAppHitTestResult::Client:
        case PDFAppHitTestResult::Caption:
        case PDFAppHitTestResult::MaximizeButton:
            break;
    }
    return Qt::ArrowCursor;
}

PDFAppTabBar::PDFAppTabBar(QWidget* parent) :
    BaseClass(parent)
{

}

QSize PDFAppTabBar::minimumSizeHint() const
{
    // QTabBar with scroll buttons has the minimal width of two scroll buttons and
    // 75 px, and a layout widens a widget to its minimal size hint: a bar of a few
    // tabs would be wider than its tabs, the New tab button would not follow the
    // last tab (8 px in the design)
    const QSize minimumSize = BaseClass::minimumSizeHint();
    return QSize(qMin(minimumSize.width(), sizeHint().width()), minimumSize.height());
}

PDFAppTopBar::PDFAppTopBar(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("topBar"));
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::DOCUMENT_TABS);
    setFixedHeight(HEIGHT);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_tabBar = new PDFAppTabBar(this);
    m_tabBar->setObjectName(QStringLiteral("tabBar"));
    m_tabBar->setAccessibleName(tr("Tabs"));
    m_tabBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->installEventFilter(this);

    m_newTabButton = new QToolButton(this);
    m_newTabButton->setObjectName(QStringLiteral("newTabButton"));
    PDFWidgetStyleProperties::set(m_newTabButton, PDFWidgetStyleProperties::NEW_TAB);
    m_newTabButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("plus")));
    m_newTabButton->setAccessibleName(tr("New tab"));
    m_newTabButton->setToolTip(tr("New tab"));
    m_newTabButton->setFocusPolicy(Qt::TabFocus);

    m_windowControls = new PDFWindowControls(this);
    m_windowControls->setObjectName(QStringLiteral("windowControls"));

    // Windows / Linux: tabs at the leading end, the controls at the trailing end
    // (the layout mirrors them in a right-to-left layout); macOS: the traffic
    // lights at the leading end, the tabs after them
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    const bool isMacOS = m_windowControls->getPlatform() == PDFWindowControls::Platform::MacOS;
    if (isMacOS)
    {
        layout->addWidget(m_windowControls, 0, Qt::AlignTop);
        layout->addWidget(m_tabBar);
        layout->addWidget(m_newTabButton, 0, Qt::AlignBottom);
        layout->addStretch(1);
    }
    else
    {
        layout->addWidget(m_tabBar);
        layout->addWidget(m_newTabButton, 0, Qt::AlignBottom);
        layout->addStretch(1);
        layout->addSpacing(16 + 46);
        layout->addWidget(m_windowControls, 0, Qt::AlignTop);
    }

    updateMargins();

    connect(m_newTabButton, &QToolButton::clicked, this, &PDFAppTopBar::newTabRequested);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, &PDFAppTopBar::onTabBarDoubleClicked);
}

PDFAppTopBar::~PDFAppTopBar() = default;

PDFAppHitTestInput PDFAppTopBar::createHitTestInput(const QWidget* window, const QPoint& windowPosition) const
{
    PDFAppHitTestInput input;
    input.position = windowPosition;
    if (!window)
    {
        return input;
    }

    input.windowSize = window->size();
    if (!window->isAncestorOf(this))
    {
        input.topBarHeight = 0;
        return input;
    }

    input.topBarHeight = mapTo(window, QPoint(0, height())).y();

    if (m_windowControls)
    {
        using Kind = PDFWindowControls::Kind;

        const PDFWindowButton* maximizeButton = m_windowControls->getButton(Kind::Maximize);
        if (maximizeButton && maximizeButton->isEnabled())
        {
            input.maximizeButtonRect = m_windowControls->getButtonRect(Kind::Maximize);
        }
        else if (maximizeButton)
        {
            // A disabled Maximize is a plain control (no Snap Layouts for a fixed window)
            input.interactiveRects << m_windowControls->getButtonRect(Kind::Maximize);
        }

        for (const Kind kind : { Kind::Minimize, Kind::Close })
        {
            const QRect rect = m_windowControls->getButtonRect(kind);
            if (!rect.isEmpty())
            {
                input.interactiveRects << rect;
            }
        }
    }

    if (m_tabBar && m_tabBar->isVisible())
    {
        const QRect visibleTabBar(m_tabBar->mapTo(window, QPoint(0, 0)), m_tabBar->size());
        for (int i = 0; i < m_tabBar->count(); ++i)
        {
            const QRect tabRect = m_tabBar->tabRect(i);
            const QRect rect = QRect(m_tabBar->mapTo(window, tabRect.topLeft()), tabRect.size()).intersected(visibleTabBar);
            if (!rect.isEmpty())
            {
                input.interactiveRects << rect;
            }
        }

        // Scroll buttons of many tabs
        for (const QWidget* child : m_tabBar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (child->isVisible())
            {
                input.interactiveRects << QRect(child->mapTo(window, QPoint(0, 0)), child->size());
            }
        }
    }

    if (m_newTabButton && m_newTabButton->isVisible())
    {
        input.interactiveRects << QRect(m_newTabButton->mapTo(window, QPoint(0, 0)), m_newTabButton->size());
    }

    return input;
}

void PDFAppTopBar::updateMargins()
{
    // The padding 16 before the tabs (Windows, Linux) or after them (macOS) lies at
    // the leading or trailing edge: the margins of a layout are not mirrored
    QLayout* layout = this->layout();
    if (!layout)
    {
        return;
    }

    const bool isMacOS = m_windowControls && m_windowControls->getPlatform() == PDFWindowControls::Platform::MacOS;
    const bool isPaddingLeft = isMacOS == (layoutDirection() == Qt::RightToLeft);
    layout->setContentsMargins(isPaddingLeft ? 16 : 0, 0, isPaddingLeft ? 0 : 16, 0);
}

void PDFAppTopBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
    {
        updateMargins();
    }
}

bool PDFAppTopBar::isInTabBar(const QPoint& position) const
{
    return m_tabBar && m_tabBar->isVisible() && m_tabBar->geometry().contains(position);
}

void PDFAppTopBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        event->accept();
        Q_EMIT moveRequested();
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppTopBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A double click of the free part of the tab bar comes as tabBarDoubleClicked(-1)
    if (event->button() == Qt::LeftButton && !isInTabBar(event->position().toPoint()))
    {
        event->accept();
        Q_EMIT maximizeRestoreRequested();
        return;
    }
    BaseClass::mouseDoubleClickEvent(event);
}

bool PDFAppTopBar::eventFilter(QObject* watched, QEvent* event)
{
    // A press of the free part of the tab bar moves the window as the free part of the bar
    if (watched == m_tabBar && event->type() == QEvent::MouseButtonPress)
    {
        const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_tabBar->tabAt(mouseEvent->position().toPoint()) < 0)
        {
            Q_EMIT moveRequested();
            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppTopBar::onTabBarDoubleClicked(int index)
{
    if (index < 0)
    {
        Q_EMIT maximizeRestoreRequested();
    }
}

}   // namespace pdf
