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

#include "pdfapptab.h"
#include "pdfappcontext.h"
#include "pdfappworkspace.h"

#include "pdffluentwidgets.h"
#include "pdfwidgetstyleproperties.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>

namespace pdf
{

quint64 PDFAppTab::s_nextId = 1;

PDFAppTab::PDFAppTab(PDFAppTabKind kind, PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_kind(kind),
    m_id(s_nextId++),
    m_context(context)
{

}

PDFAppTab::~PDFAppTab() = default;

void PDFAppTab::setPage(PDFAppPage* page)
{
    if (m_page == page)
    {
        return;
    }

    // The previous page may be the sender of the navigation: it is deleted later
    if (PDFAppPage* oldPage = m_page)
    {
        oldPage->disconnect(this);
        if (QVBoxLayout* layout = getPageLayout())
        {
            layout->removeWidget(oldPage);
        }
        oldPage->hide();
        oldPage->deleteLater();
    }

    m_page = page;

    if (page)
    {
        QWidget* area = getPageArea();
        page->setParent(area ? area : this);
        if (QVBoxLayout* layout = getPageLayout())
        {
            layout->addWidget(page, 1);
        }
        page->show();
        connect(page, &PDFAppPage::titleChanged, this, &PDFAppTab::onPageTitleChanged);
    }

    onPageChanged();
}

void PDFAppTab::setNavItem(PDFAppNavItem item)
{
    Q_UNUSED(item);
}

PDFAppNavItem PDFAppTab::getNavItem() const
{
    return PDFAppNavItem::None;
}

bool PDFAppTab::isClosable() const
{
    return true;
}

PDFLineEdit* PDFAppTab::getSearchField() const
{
    return nullptr;
}

bool PDFAppTab::confirmClose()
{
    return true;
}

bool PDFAppTab::executeCommand(const QString& commandId)
{
    Q_UNUSED(commandId);
    return false;
}

bool PDFAppTab::isCommandEnabled(const QString& commandId) const
{
    Q_UNUSED(commandId);
    return false;
}

bool PDFAppTab::isCommandChecked(const QString& commandId) const
{
    Q_UNUSED(commandId);
    return false;
}

void PDFAppTab::onPageChanged()
{

}

void PDFAppTab::onPageTitleChanged()
{
    QPointer<PDFAppTab> guard(this);
    onPageChanged();
    if (guard)
    {
        Q_EMIT pageTitleChanged();
    }
}

PDFAppWorkspaceTab::PDFAppWorkspaceTab(PDFAppTabKind kind, PDFAppContext* context, QWidget* parent) :
    BaseClass((kind == PDFAppTabKind::Home || kind == PDFAppTabKind::Help) ? kind : PDFAppTabKind::NewTab, context, parent)
{
    const bool isHome = getKind() == PDFAppTabKind::Home;
    setObjectName(isHome ? QStringLiteral("homeTab") : (getKind() == PDFAppTabKind::Help ? QStringLiteral("helpTab") : QStringLiteral("newTab")));

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (isHome)
    {
        m_sidebar = new PDFAppSidebar(context, this);
        layout->addWidget(m_sidebar);
        connect(m_sidebar, &PDFAppSidebar::navigationRequested, this, &PDFAppTab::navigationRequested);
    }

    // workspace-pane: bg/canvas, padding 32, gaps 24 (header, banner and page, footer);
    // the bottom padding of the Home tab is 16 (owner decision O11) and it is a part
    // of the footer, which centers its row on the axis of the switch of the sidebar.
    // The horizontal paddings belong to the rows: the page scroll area reaches into
    // them by the width of its overlay scroll bar (updatePageInsets)
    QWidget* workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("workspace"));
    PDFWidgetStyleProperties::set(workspace, PDFWidgetStyleProperties::CANVAS);
    QVBoxLayout* workspaceLayout = new QVBoxLayout(workspace);
    m_workspaceLayout = workspaceLayout;
    workspaceLayout->setContentsMargins(0, PADDING, 0, isHome ? 0 : PADDING);
    workspaceLayout->setSpacing(24);
    layout->addWidget(workspace, 1);

    m_header = new PDFAppWorkspaceHeader(workspace);
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(PADDING, 0, PADDING, 0);
    headerRow->addWidget(m_header);
    workspaceLayout->addLayout(headerRow);
    connect(m_header, &PDFAppWorkspaceHeader::searchRequested, this, &PDFAppWorkspaceTab::onSearchRequested);
    connect(m_header, &PDFAppWorkspaceHeader::breadcrumbActivated, this, &PDFAppWorkspaceTab::onBreadcrumbActivated);

    // The page scrolls (the minimal window, long pages) together with the banner
    // at its top (the top of the content column, 958:28036), so a long banner in
    // a low window is never cut; the header and the footer stay. The gap 24 below
    // the banner disappears with it (a hidden widget has no spacing).
    m_pageArea = new QWidget();
    m_pageArea->setObjectName(QStringLiteral("pageArea"));
    m_pageLayout = new QVBoxLayout(m_pageArea);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(24);

    if (isHome)
    {
        m_banner = new PDFAppStorageBanner(m_pageArea);
        m_pageLayout->addWidget(m_banner);
        m_bannerLayout = m_pageLayout;
        connect(m_banner, &PDFAppStorageBanner::tryAgainRequested, this, &PDFAppWorkspaceTab::onTryAgainRequested);
        connect(m_banner, &PDFAppStorageBanner::showInFolderRequested, this, &PDFAppWorkspaceTab::onShowInFolderRequested);
        connect(m_banner, &PDFAppStorageBanner::checkForUpdatesRequested, this, &PDFAppWorkspaceTab::onCheckForUpdatesRequested);
    }

    // The vertical scroll bar lies over the right padding of the workspace, it takes
    // no width of the page (the columns end where the frames end)
    m_pageScrollArea = new PDFAppScrollArea(m_pageArea, workspace);
    m_pageScrollArea->setObjectName(QStringLiteral("pageScrollArea"));
    m_pageScrollArea->setOverlayScrollBars(true);
    m_pageScrollRow = new QHBoxLayout();
    m_pageScrollRow->addWidget(m_pageScrollArea);
    workspaceLayout->addLayout(m_pageScrollRow, 1);

    if (getKind() == PDFAppTabKind::Help)
    {
        // pdf4qt-help 955:152873 (owner decision O13): the page of the Help tab is at
        // most 1166 px wide, centered in the view (the scroll bar stays at the edge);
        // the header follows the page (updateHelpMargins)
        m_pageScrollArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_pageScrollArea->viewport()->installEventFilter(this);
    }
    updatePageInsets();

    if (isHome)
    {
        m_footer = new PDFAppFooter(workspace);
        QHBoxLayout* footerRow = new QHBoxLayout();
        footerRow->setContentsMargins(PADDING, 0, PADDING, 0);
        footerRow->addWidget(m_footer);
        workspaceLayout->addLayout(footerRow);
        connect(m_footer, &PDFAppFooter::externalUrlRequested, this, &PDFAppTab::externalUrlRequested);

        if (context)
        {
            connect(context, &PDFAppContext::storageStateChanged, this, &PDFAppWorkspaceTab::onStorageStateChanged);
        }
        onStorageStateChanged();
    }
}

PDFAppWorkspaceTab::~PDFAppWorkspaceTab()
{
    // The page is deleted with the children of the tab, after this destructor: its
    // signal aboutToBeDestroyed must not reach the destroyed tab
    if (PDFAppPage* page = getPage())
    {
        disconnect(page, &PDFAppPage::aboutToBeDestroyed, this, &PDFAppWorkspaceTab::onPageAboutToBeDestroyed);
    }
}

void PDFAppWorkspaceTab::insertTopWidget(QWidget* widget)
{
    if (!widget || !m_workspaceLayout)
    {
        return;
    }

    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(PADDING, 0, PADDING, 0);
    row->addWidget(widget);
    m_workspaceLayout->insertLayout(0, row);
}

void PDFAppWorkspaceTab::setNavItem(PDFAppNavItem item)
{
    if (m_sidebar)
    {
        m_sidebar->setCurrentItem(item);
    }
}

PDFAppNavItem PDFAppWorkspaceTab::getNavItem() const
{
    return m_sidebar ? m_sidebar->getCurrentItem() : PDFAppNavItem::None;
}

QString PDFAppWorkspaceTab::getTabTitle() const
{
    switch (getKind())
    {
        case PDFAppTabKind::Home:
            return tr("Home");
        case PDFAppTabKind::Help:
            return tr("Help");
        default:
            return tr("New tab");
    }
}

QString PDFAppWorkspaceTab::getTabIconName() const
{
    switch (getKind())
    {
        case PDFAppTabKind::Home:
            return QStringLiteral("home");
        case PDFAppTabKind::Help:
            return QStringLiteral("help-circle");
        default:
            return QStringLiteral("folder");
    }
}

bool PDFAppWorkspaceTab::isClosable() const
{
    return getKind() != PDFAppTabKind::Home;
}

PDFLineEdit* PDFAppWorkspaceTab::getSearchField() const
{
    if (!m_header || m_header->isHidden())
    {
        return nullptr;
    }
    return m_header->getSearchField();
}

QWidget* PDFAppWorkspaceTab::getPageArea() const
{
    return m_pageArea;
}

QVBoxLayout* PDFAppWorkspaceTab::getPageLayout() const
{
    return m_pageLayout;
}

void PDFAppWorkspaceTab::onPageChanged()
{
    // A new page starts at its top (not a page, which has changed its title)
    if (m_pageScrollArea && m_scrolledPage != getPage())
    {
        m_scrolledPage = getPage();
        m_pageScrollArea->verticalScrollBar()->setValue(0);
        m_pageScrollArea->horizontalScrollBar()->setValue(0);
    }

    placeBanner();

    if (!m_header)
    {
        return;
    }

    // Settings have no header with the search
    PDFAppPage* page = getPage();
    m_header->setVisible(!page || page->hasSearchHeader());
    m_header->setTexts(page ? page->getHeaderTitle() : QString(), page ? page->getHeaderSubtitle() : QString(),
                       page ? page->getHeaderSubtitleAccent() : QString());
    m_header->setBreadcrumbs(page ? page->getHeaderBreadcrumbs() : QList<PDFAppBreadcrumbItem>());
    m_header->setIcon(page ? page->getHeaderIcon() : PDFAppHeaderIcon());
}

void PDFAppWorkspaceTab::placeBanner()
{
    PDFAppPage* page = getPage();
    if (page)
    {
        connect(page, &PDFAppPage::aboutToBeDestroyed, this, &PDFAppWorkspaceTab::onPageAboutToBeDestroyed, Qt::UniqueConnection);
    }

    // The top of the main column of a page with a side column (958:28036, 955:150880)
    QBoxLayout* layout = page ? page->getBannerLayout() : nullptr;
    if (!layout || !layout->parentWidget() || (layout->parentWidget() != page && !page->isAncestorOf(layout->parentWidget())))
    {
        layout = m_pageLayout;
    }
    moveBanner(layout);
}

void PDFAppWorkspaceTab::onPageAboutToBeDestroyed()
{
    // The page is being destroyed (its children still exist): the banner is not one
    // of them. The page itself is not called.
    moveBanner(m_pageLayout);
}

void PDFAppWorkspaceTab::moveBanner(QBoxLayout* layout)
{
    if (!m_banner || !layout || !layout->parentWidget() || (m_bannerLayout == layout && m_banner->parentWidget() == layout->parentWidget()))
    {
        return;
    }

    // A new parent hides the widget: the banner keeps its own visibility (the kind
    // of the storage state, a dismissed banner)
    const bool isHidden = m_banner->isHidden();
    if (m_bannerLayout)
    {
        m_bannerLayout->removeWidget(m_banner);
    }
    m_banner->setParent(layout->parentWidget());
    layout->insertWidget(0, m_banner);
    m_bannerLayout = layout;
    m_banner->setVisible(!isHidden);
}

void PDFAppWorkspaceTab::onStorageStateChanged()
{
    // Without a context (tests of the parts) nothing is known: no banner
    PDFDatabaseState state;
    state.mode = PDFDatabaseStorageMode::Disk;
    if (getContext())
    {
        state = getContext()->getStorageState();
    }

    if (m_banner)
    {
        m_banner->setState(state);
    }
    if (m_footer)
    {
        m_footer->setState(state);
    }
}

bool PDFAppWorkspaceTab::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize && m_pageScrollArea && watched == m_pageScrollArea->viewport())
    {
        updateHelpMargins();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppWorkspaceTab::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    // Another style has another scroll bar (or none over the content)
    if (event->type() == QEvent::StyleChange)
    {
        updatePageInsets();
    }
}

void PDFAppWorkspaceTab::updatePageInsets()
{
    if (!m_pageScrollArea || !m_pageScrollRow || !m_pageLayout)
    {
        return;
    }

    // The area reaches into the paddings of the workspace by the width of the
    // overlay scroll bar at both sides (the page of the Help tab stays centered in
    // the window), the page keeps the paddings inside of the area
    const int inset = qBound(0, m_pageScrollArea->getScrollBarOverlap(), int(PADDING));
    if (inset == m_pageInset)
    {
        return;
    }

    m_pageInset = inset;
    m_pageScrollRow->setContentsMargins(PADDING - inset, 0, PADDING - inset, 0);
    m_pageLayout->setContentsMargins(inset, 0, inset, 0);
    if (getKind() == PDFAppTabKind::Help && m_pageArea)
    {
        m_pageArea->setMaximumWidth(HELP_CONTENT_WIDTH + 2 * inset);
    }
    updateHelpMargins();
}

void PDFAppWorkspaceTab::updateHelpMargins()
{
    if (getKind() != PDFAppTabKind::Help || !m_pageScrollArea || !m_header)
    {
        return;
    }

    // The header has the horizontal position and the width of the centered page (the
    // header is narrower than the area by the insets of the page at both sides)
    const int inset = qMax(0, m_pageInset);
    const int viewportWidth = m_pageScrollArea->viewport()->width();
    const int pageWidth = qMin(HELP_CONTENT_WIDTH + 2 * inset, viewportWidth);
    const int left = qMax(0, (viewportWidth - pageWidth) / 2);
    const int contentWidth = qMax(0, pageWidth - 2 * inset);
    const int right = qMax(0, m_pageScrollArea->width() - 2 * inset - left - contentWidth);
    if (m_header->contentsMargins() != QMargins(left, 0, right, 0))
    {
        m_header->setContentsMargins(left, 0, right, 0);
    }
}

void PDFAppWorkspaceTab::onBreadcrumbActivated(const PDFAppRoute& route)
{
    Q_EMIT navigationRequested(route, PDFAppNavigationMode::Push);
}

void PDFAppWorkspaceTab::onSearchRequested(const QString& text)
{
    Q_EMIT navigationRequested(PDFAppRoutes::search(text.left(256)), PDFAppNavigationMode::Push);
}

void PDFAppWorkspaceTab::onTryAgainRequested()
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return;
    }

    // The state signal of the database updates the banner
    if (!context->getDatabase()->retryOpen())
    {
        qWarning("The settings database still cannot be opened: %s", qPrintable(context->getStorageState().getDescription()));
    }
}

void PDFAppWorkspaceTab::onShowInFolderRequested()
{
    const PDFDatabaseState state = getContext() ? getContext()->getStorageState() : PDFDatabaseState();
    if (state.filePath.isEmpty())
    {
        return;
    }

    const QString folder = QFileInfo(state.filePath).absolutePath();
    Q_EMIT externalUrlRequested(QUrl::fromLocalFile(folder));
}

void PDFAppWorkspaceTab::onCheckForUpdatesRequested()
{
    Q_EMIT navigationRequested(PDFAppRoutes::whatsNew(), PDFAppNavigationMode::Push);
}

}   // namespace pdf
