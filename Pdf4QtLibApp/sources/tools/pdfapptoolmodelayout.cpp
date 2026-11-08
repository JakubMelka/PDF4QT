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

#include "pdfapptoolmodelayout.h"

#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappviewstate.h"

#include "pdfthememanager.h"

#include <QBoxLayout>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>

namespace pdf
{

// The left panel of a tool starts with the width of the sidebar (sidebar-left 291:1030)
static_assert(PDFAppToolModeLayout::DEFAULT_LEFT_WIDTH == PDFAppDocumentSidebar::DEFAULT_WIDTH);

PDFAppToolModeLayout::PDFAppToolModeLayout(QWidget* host, QBoxLayout* hostLayout) :
    BaseClass(host),
    m_host(host)
{
    // The toolbar of the document, in the tool mode the toolbar of the tool (both 52 px)
    m_toolbarStack = new QStackedWidget(host);
    m_toolbarStack->setObjectName(QStringLiteral("toolbarStack"));
    m_toolbarStack->setFixedHeight(PDFAppDocumentToolbar::HEIGHT);
    m_toolbarStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    hostLayout->addWidget(m_toolbarStack);

    // document-body 193:882: the left widget, the splitter (6 px, 266:7854 - bg/sidebar
    // with the grip, PDFAppDocumentSplitterHandle) and the canvas; the right panel of a
    // tool lies next to the splitter, no handle before it (document-body 291:1029)
    m_bodyLayout = new QHBoxLayout();
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(0);
    hostLayout->addLayout(m_bodyLayout, 1);

    m_splitter = new PDFAppDocumentSplitter(Qt::Horizontal, host);
    m_splitter->setObjectName(QStringLiteral("documentSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(HANDLE_WIDTH);
    m_bodyLayout->addWidget(m_splitter, 1);

    // The canvas; the info bars lie over it (children of the area, placed by
    // layoutInfoBars); in the tool mode a page of the tool can replace the base canvas
    m_canvasArea = new QWidget(m_splitter);
    m_canvasArea->setObjectName(QStringLiteral("canvasArea"));
    m_canvasArea->installEventFilter(this);
    m_canvasLayout = new QStackedLayout(m_canvasArea);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
    m_splitter->addWidget(m_canvasArea);
}

PDFAppToolModeLayout::~PDFAppToolModeLayout() = default;

PDFAppDocumentStatusBar* PDFAppToolModeLayout::createStatusBar(QBoxLayout* hostLayout)
{
    if (!m_statusBar && m_host && hostLayout)
    {
        m_statusBar = new PDFAppDocumentStatusBar(m_host);
        hostLayout->addWidget(m_statusBar);
    }
    return m_statusBar;
}

void PDFAppToolModeLayout::setBaseToolbar(QWidget* toolbar)
{
    m_baseToolbar = toolbar;
    if (toolbar && m_toolbarStack)
    {
        m_toolbarStack->addWidget(toolbar);
        m_toolbarStack->setCurrentWidget(toolbar);
    }
}

void PDFAppToolModeLayout::setBaseLeftWidget(QWidget* leftWidget)
{
    m_baseLeftWidget = leftWidget;
    if (leftWidget && m_splitter)
    {
        m_splitter->insertWidget(0, leftWidget);
        m_splitter->setStretchFactor(0, 0);
        m_splitter->setStretchFactor(1, 1);
    }
}

void PDFAppToolModeLayout::setBaseCanvas(QWidget* canvas)
{
    m_baseCanvas = canvas;
    if (canvas && m_canvasLayout)
    {
        m_canvasLayout->addWidget(canvas);
        m_canvasLayout->setCurrentWidget(canvas);
    }
}

void PDFAppToolModeLayout::watchPart(Part& part, QWidget* widget)
{
    part.widget = widget;
    part.parent = widget ? widget->parentWidget() : nullptr;
    if (widget)
    {
        connect(widget, &QObject::destroyed, this, &PDFAppToolModeLayout::onPartDestroyed);
    }
}

void PDFAppToolModeLayout::releasePart(Part& part)
{
    if (QWidget* widget = part.widget.data())
    {
        widget->disconnect(this);
        widget->hide();
        widget->setParent(part.parent.data());
    }
    part.widget = nullptr;
    part.parent = nullptr;
}

void PDFAppToolModeLayout::installParts(const PDFAppToolModeParts& parts)
{
    if (parts.toolbar && m_toolbarStack)
    {
        watchPart(m_toolbar, parts.toolbar);
        m_toolbarStack->addWidget(parts.toolbar);
        m_toolbarStack->setCurrentWidget(parts.toolbar);
    }

    if (parts.canvasPage && m_canvasLayout)
    {
        watchPart(m_canvasPage, parts.canvasPage);
        m_canvasLayout->addWidget(parts.canvasPage);
        m_canvasLayout->setCurrentWidget(parts.canvasPage);

        // The bars over the canvas stay above the page (QStackedLayout raises what it shows)
        parts.canvasPage->lower();
    }

    if (parts.rightPanel && m_bodyLayout)
    {
        watchPart(m_rightPanel, parts.rightPanel);
        m_bodyLayout->addWidget(parts.rightPanel);
        parts.rightPanel->setFixedWidth(RIGHT_PANEL_WIDTH);
        parts.rightPanel->show();
    }

    if (parts.leftPanel && m_splitter)
    {
        watchPart(m_leftPanel, parts.leftPanel);
        m_leftPanelWidth = DEFAULT_LEFT_WIDTH;
        parts.leftPanel->setMinimumWidth(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH);
        m_splitter->insertWidget(m_splitter->indexOf(m_canvasArea), parts.leftPanel);
        parts.leftPanel->show();
    }

    // The canvas takes what the parts leave (the frame 291:994: 300 + 6 + 814 + 320), in
    // the minimal window less than the minimal size of the base canvas: below it QSplitter
    // would take the width from its handle
    if (m_canvasArea)
    {
        m_canvasArea->setMinimumWidth(1);
    }
}

bool PDFAppToolModeLayout::removeParts()
{
    // The keyboard focus inside of a part stays in the host (also of an inactive window,
    // which would give it to the hidden part at its activation)
    const QWidget* focusWidget = m_host ? m_host->window()->focusWidget() : nullptr;
    bool isFocusInPart = false;
    for (const Part* part : { &m_toolbar, &m_leftPanel, &m_canvasPage, &m_rightPanel })
    {
        isFocusInPart = isFocusInPart || (part->widget && focusWidget && (part->widget.data() == focusWidget || part->widget->isAncestorOf(focusWidget)));
    }

    if (m_toolbar.widget && m_toolbarStack)
    {
        m_toolbarStack->removeWidget(m_toolbar.widget);
    }
    if (m_toolbarStack && m_baseToolbar)
    {
        m_toolbarStack->setCurrentWidget(m_baseToolbar);
    }
    releasePart(m_toolbar);

    if (m_canvasPage.widget && m_canvasLayout)
    {
        m_canvasLayout->removeWidget(m_canvasPage.widget);
    }
    if (m_canvasLayout && m_baseCanvas)
    {
        m_canvasLayout->setCurrentWidget(m_baseCanvas);

        // QStackedLayout raises the widget it shows: the bars over the canvas stay above it
        m_baseCanvas->lower();
    }
    releasePart(m_canvasPage);

    if (m_rightPanel.widget)
    {
        if (m_bodyLayout)
        {
            m_bodyLayout->removeWidget(m_rightPanel.widget);
        }
        m_rightPanel.widget->setMinimumWidth(0);
        m_rightPanel.widget->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    releasePart(m_rightPanel);

    if (m_leftPanel.widget)
    {
        m_leftPanel.widget->setMinimumWidth(0);
    }
    releasePart(m_leftPanel);

    if (m_canvasArea)
    {
        m_canvasArea->setMinimumWidth(0);
    }
    return isFocusInPart;
}

bool PDFAppToolModeLayout::isPartAlive() const
{
    return m_toolbar.widget || m_leftPanel.widget || m_canvasPage.widget || m_rightPanel.widget;
}

QWidget* PDFAppToolModeLayout::getLeftWidget() const
{
    return m_leftPanel.widget ? m_leftPanel.widget.data() : m_baseLeftWidget.data();
}

QWidget* PDFAppToolModeLayout::getCanvasWidget() const
{
    return m_canvasPage.widget ? m_canvasPage.widget.data() : m_baseCanvas.data();
}

void PDFAppToolModeLayout::applySplitterSizes(int baseLeftWidth)
{
    if (!m_splitter || m_splitter->width() <= 0)
    {
        return;
    }

    QWidget* leftWidget = getLeftWidget();
    if (!leftWidget || leftWidget->isHidden())
    {
        return;
    }

    // At most a half of the splitter, so a canvas stays also in a narrow window
    const int preferredWidth = m_leftPanel.widget ? m_leftPanelWidth : baseLeftWidth;
    const int leftWidth = qBound(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, preferredWidth, qMax(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, m_splitter->width() / 2));
    const int canvasWidth = qMax(0, m_splitter->width() - leftWidth - m_splitter->handleWidth());

    QList<int> sizes;
    for (int i = 0; i < m_splitter->count(); ++i)
    {
        const QWidget* widget = m_splitter->widget(i);
        sizes << (widget == leftWidget ? leftWidth : (widget == m_canvasArea.data() ? canvasWidth : 0));
    }
    m_splitter->setSizes(sizes);
}

void PDFAppToolModeLayout::onPartDestroyed()
{
    Q_EMIT partDestroyed();
}

// ------------------------------------------------------------------ info bars

void PDFAppToolModeLayout::showInfoBar(const PDFAppInfoBarRequest& request)
{
    if (request.id.isEmpty() || !m_canvasArea)
    {
        return;
    }

    for (size_t i = 0; i < m_infoBars.size(); ++i)
    {
        if (m_infoBars[i].id == request.id)
        {
            m_infoBars[i] = request;
            if (m_infoBarWidgets[i])
            {
                m_infoBarWidgets[i]->setRequest(request);
            }
            layoutInfoBars();
            return;
        }
    }

    PDFAppInfoBar* infoBar = new PDFAppInfoBar(request, m_canvasArea);
    connect(infoBar, &PDFAppInfoBar::actionTriggered, this, &PDFAppToolModeLayout::onInfoBarAction);
    connect(infoBar, &PDFAppInfoBar::closeRequested, this, &PDFAppToolModeLayout::onInfoBarClosed);
    m_infoBars.push_back(request);
    m_infoBarWidgets.emplace_back(infoBar);
    infoBar->show();
    layoutInfoBars();
}

void PDFAppToolModeLayout::hideInfoBar(const QString& id)
{
    for (size_t i = 0; i < m_infoBars.size(); ++i)
    {
        if (m_infoBars[i].id == id)
        {
            // The bar may be the sender of the signal, which hides it
            if (PDFAppInfoBar* infoBar = m_infoBarWidgets[i].data())
            {
                infoBar->disconnect(this);
                infoBar->hide();
                infoBar->deleteLater();
            }
            m_infoBars.erase(m_infoBars.begin() + i);
            m_infoBarWidgets.erase(m_infoBarWidgets.begin() + i);
            layoutInfoBars();
            return;
        }
    }
}

QStringList PDFAppToolModeLayout::getInfoBarIds() const
{
    QStringList ids;
    for (const PDFAppInfoBarRequest& infoBar : m_infoBars)
    {
        ids << infoBar.id;
    }
    return ids;
}

const PDFAppInfoBarRequest* PDFAppToolModeLayout::findInfoBar(const QString& id) const
{
    for (const PDFAppInfoBarRequest& infoBar : m_infoBars)
    {
        if (infoBar.id == id)
        {
            return &infoBar;
        }
    }
    return nullptr;
}

PDFAppInfoBar* PDFAppToolModeLayout::findInfoBarWidget(const QString& id) const
{
    for (size_t i = 0; i < m_infoBars.size(); ++i)
    {
        if (m_infoBars[i].id == id)
        {
            return m_infoBarWidgets[i].data();
        }
    }
    return nullptr;
}

void PDFAppToolModeLayout::onInfoBarAction(const QString& infoBarId, const QString& actionId)
{
    Q_EMIT infoBarActionTriggered(infoBarId, actionId);
}

void PDFAppToolModeLayout::onInfoBarClosed(const QString& infoBarId)
{
    QPointer<PDFAppToolModeLayout> guard(this);
    hideInfoBar(infoBarId);
    if (guard)
    {
        Q_EMIT infoBarActionTriggered(infoBarId, QString());
    }
}

void PDFAppToolModeLayout::layoutInfoBars()
{
    const QWidget* canvas = getCanvasWidget();
    if (!m_canvasArea || !canvas)
    {
        return;
    }

    // Top centered over the canvas, 12 px below the toolbar, 8 px apart (1028:24277)
    const QRect area = canvas->geometry();
    const QMargins shadow = PDFAppInfoBar::getShadowMargins();
    const int maximumCardWidth = qMax(0, area.width() - 2 * INFO_BAR_TOP_GAP);
    int cardTop = area.top() + INFO_BAR_TOP_GAP;

    for (const QPointer<PDFAppInfoBar>& infoBar : m_infoBarWidgets)
    {
        if (!infoBar)
        {
            continue;
        }

        const int cardWidth = qMin(infoBar->sizeHint().width() - shadow.left() - shadow.right(), maximumCardWidth);
        const int cardLeft = area.left() + (area.width() - cardWidth) / 2;
        infoBar->setGeometry(QRect(cardLeft - shadow.left(), cardTop - shadow.top(), cardWidth + shadow.left() + shadow.right(), infoBar->height()));
        infoBar->raise();
        cardTop += PDFAppInfoBar::CARD_HEIGHT + INFO_BAR_GAP;
    }
}

bool PDFAppToolModeLayout::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvasArea && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
    {
        layoutInfoBars();
    }
    else if (watched == m_canvasArea && event->type() == QEvent::Paint)
    {
        // The scroll bars of the PDFWidget are not opaque: bg/track of the canvas (291:1061)
        // lies under them, not the white of the window; an empty canvas of an own tab the same
        QPainter painter(m_canvasArea);
        painter.fillRect(m_canvasArea->rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
    }
    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf

#include "moc_pdfapptoolmodelayout.cpp"
