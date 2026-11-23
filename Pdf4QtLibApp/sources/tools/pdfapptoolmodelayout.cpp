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

#include "pdfdeferreddeletion.h"
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

/// The stack of the canvas area: QStackedLayout gives its widget the whole rectangle of the area, this
/// one keeps its contents margins free (the band of a page caption, PDFAppToolModeLayout::setCanvasTopInset)
class PDFAppToolCanvasStackedLayout : public QStackedLayout
{
public:
    explicit PDFAppToolCanvasStackedLayout(QWidget* parent) :
        QStackedLayout(parent)
    {

    }

    virtual void setGeometry(const QRect& rect) override
    {
        QStackedLayout::setGeometry(rect.marginsRemoved(contentsMargins()));
    }
};

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
    m_canvasLayout = new PDFAppToolCanvasStackedLayout(m_canvasArea);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
    m_splitter->addWidget(m_canvasArea);
}

PDFAppToolModeLayout::~PDFAppToolModeLayout()
{
    // The layout dies before its splitter (an earlier child of the host): a part given back by hide only goes to its
    // former parent now, it does not die with the splitter
    releasePendingParts();
}

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

    // A part given back by hide only (still in the splitter, releasePart) keeps its former parent
    for (auto it = m_pendingParts.begin(); widget && it != m_pendingParts.end(); ++it)
    {
        if (it->widget.data() == widget)
        {
            part.parent = it->parent;
            m_pendingParts.erase(it);
            break;
        }
    }

    if (widget)
    {
        // The part belongs to the shell of a tool (step A: the handle is cut by retire of the layout's host; destroyed
        // stays, the fallback of a part deleted during the mode - PA6: not aboutToBeDeleted, its receivers only queue
        // a check, which needs the part gone (its QPointer cleared), while aboutToBeDeleted comes at retire, before
        // the deletion; a retired part goes back by the token of the mode)
        PDFDeferredDeletion::connect(widget, &QObject::destroyed, this, &PDFAppToolModeLayout::onPartDestroyed);
    }
}

void PDFAppToolModeLayout::releasePart(Part& part)
{
    if (QWidget* widget = part.widget.data())
    {
        widget->disconnect(this);
        widget->hide();
        if (m_splitter && widget->parentWidget() == m_splitter.data())
        {
            // QSplitter deletes the handle of a widget, which leaves it, at once (QSplitter::childEvent of
            // ChildRemoved), and the handle is a child of the splitter: the mode may end inside of a loop of Qt over
            // the children of the splitter by a copied list of raw pointers (QWidget::event sends LanguageChange to
            // them, a panel of the sidebar emits a signal from its changeEvent, a slot closes the tool), which would
            // send the event to the deleted handle next. The part is hidden now and leaves the splitter from the
            // event loop.
            m_pendingParts.push_back(part);
            if (!m_isPendingReleaseQueued)
            {
                m_isPendingReleaseQueued = true;
                QMetaObject::invokeMethod(this, &PDFAppToolModeLayout::releasePendingParts, Qt::QueuedConnection);
            }
        }
        else
        {
            widget->setParent(part.parent.data());
        }
    }
    part.widget = nullptr;
    part.parent = nullptr;
}

void PDFAppToolModeLayout::releasePendingParts()
{
    m_isPendingReleaseQueued = false;
    std::vector<Part> parts;
    parts.swap(m_pendingParts);
    for (const Part& part : parts)
    {
        // A deleted part, a part without its former parent and a retired part or parent (deleted later with its
        // owner, from the event loop) stay hidden in the splitter; a part installed again is no longer in the list
        QWidget* widget = part.widget.data();
        QWidget* parent = part.parent.data();
        if (!widget || !parent || !m_splitter || widget->parentWidget() != m_splitter.data() || PDFDeferredDeletion::isRetired(widget) ||
            PDFDeferredDeletion::isRetired(parent))
        {
            continue;
        }
        widget->setParent(parent);
    }
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

    // A shown part resizes the canvas, its view emits (viewChanged): a slot may close the host (its tab) - a retired
    // host has given the installed parts back already (the mode token of the tool), nothing more enters it
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    if (parts.rightPanel && m_bodyLayout)
    {
        watchPart(m_rightPanel, parts.rightPanel);
        m_bodyLayout->addWidget(parts.rightPanel);
        parts.rightPanel->setFixedWidth(RIGHT_PANEL_WIDTH);
        parts.rightPanel->show();
    }

    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
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

    // The bar is a child of the canvas area and belongs to this layout (step A: retired by it, or with it)
    PDFAppInfoBar* infoBar = new PDFAppInfoBar(request, m_canvasArea);
    PDFDeferredDeletion::setOwner(infoBar, this);
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
            // The bar may be the sender of the signal, which hides it: disconnected from this and hidden now,
            // retired from the event loop (step A, lesson 1b - the rest of its emission reaches its other receivers)
            if (PDFAppInfoBar* infoBar = m_infoBarWidgets[i].data())
            {
                infoBar->disconnect(this);
                PDFDeferredDeletion::retireLater(infoBar);
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
    // A layout retired meanwhile (step A: its view closed) tells nothing
    hideInfoBar(infoBarId);
    if (!PDFDeferredDeletion::isRetired(this))
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

    // Top centered over the canvas, 12 px below the toolbar, 8 px apart (1028:24277); the band of a
    // page caption above the canvas widget is a part of the canvas (setCanvasTopInset)
    const QRect area = canvas->geometry().adjusted(0, -m_canvasTopInset, 0, 0);
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

void PDFAppToolModeLayout::setCanvasTopInset(int inset)
{
    inset = qMax(0, inset);
    if (m_canvasTopInset == inset)
    {
        return;
    }

    m_canvasTopInset = inset;
    if (m_canvasLayout)
    {
        // The canvas widget (the PDFWidget of the view, a canvas page) lies below the band
        m_canvasLayout->setContentsMargins(0, inset, 0, 0);
    }
    layoutInfoBars();
}

PDFAppToolModeLayout* PDFAppToolModeLayout::findForCanvasArea(const QWidget* canvasArea)
{
    // The layout is a child of its host, an ancestor of the canvas area
    for (const QWidget* widget = canvasArea ? canvasArea->parentWidget() : nullptr; widget; widget = widget->parentWidget())
    {
        for (PDFAppToolModeLayout* layout : widget->findChildren<PDFAppToolModeLayout*>(Qt::FindDirectChildrenOnly))
        {
            if (layout->getCanvasArea() == canvasArea)
            {
                return layout;
            }
        }
    }
    return nullptr;
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
