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
    BaseClass(kind == PDFAppTabKind::Home ? PDFAppTabKind::Home : PDFAppTabKind::NewTab, context, parent)
{
    const bool isHome = getKind() == PDFAppTabKind::Home;
    setObjectName(isHome ? QStringLiteral("homeTab") : QStringLiteral("newTab"));

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (isHome)
    {
        m_sidebar = new PDFAppSidebar(context, this);
        layout->addWidget(m_sidebar);
        connect(m_sidebar, &PDFAppSidebar::navigationRequested, this, &PDFAppTab::navigationRequested);
    }

    // workspace-pane: bg/canvas, padding 32, gaps 24 (header, banner, page, footer);
    // the bottom padding of the Home tab is 16 (owner decision O11) and it is a part
    // of the footer, which centers its row on the axis of the switch of the sidebar
    QWidget* workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("workspace"));
    PDFWidgetStyleProperties::set(workspace, PDFWidgetStyleProperties::CANVAS);
    QVBoxLayout* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(32, 32, 32, isHome ? 0 : 32);
    workspaceLayout->setSpacing(24);
    layout->addWidget(workspace, 1);

    m_header = new PDFAppWorkspaceHeader(workspace);
    workspaceLayout->addWidget(m_header);
    connect(m_header, &PDFAppWorkspaceHeader::searchRequested, this, &PDFAppWorkspaceTab::onSearchRequested);

    if (isHome)
    {
        m_banner = new PDFAppStorageBanner(workspace);
        workspaceLayout->addWidget(m_banner);
        connect(m_banner, &PDFAppStorageBanner::tryAgainRequested, this, &PDFAppWorkspaceTab::onTryAgainRequested);
        connect(m_banner, &PDFAppStorageBanner::showInFolderRequested, this, &PDFAppWorkspaceTab::onShowInFolderRequested);
        connect(m_banner, &PDFAppStorageBanner::checkForUpdatesRequested, this, &PDFAppWorkspaceTab::onCheckForUpdatesRequested);
    }

    // The page scrolls (the minimal window, long pages), the header, the banner
    // and the footer stay
    m_pageArea = new QWidget();
    m_pageArea->setObjectName(QStringLiteral("pageArea"));
    m_pageLayout = new QVBoxLayout(m_pageArea);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(0);
    m_pageScrollArea = new PDFAppScrollArea(m_pageArea, workspace);
    m_pageScrollArea->setObjectName(QStringLiteral("pageScrollArea"));
    workspaceLayout->addWidget(m_pageScrollArea, 1);

    if (isHome)
    {
        m_footer = new PDFAppFooter(workspace);
        workspaceLayout->addWidget(m_footer);
        connect(m_footer, &PDFAppFooter::externalUrlRequested, this, &PDFAppTab::externalUrlRequested);

        if (context)
        {
            connect(context, &PDFAppContext::storageStateChanged, this, &PDFAppWorkspaceTab::onStorageStateChanged);
        }
        onStorageStateChanged();
    }
}

PDFAppWorkspaceTab::~PDFAppWorkspaceTab() = default;

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
    return getKind() == PDFAppTabKind::Home ? tr("Home") : tr("New tab");
}

QString PDFAppWorkspaceTab::getTabIconName() const
{
    return getKind() == PDFAppTabKind::Home ? QStringLiteral("home") : QStringLiteral("folder");
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

    if (!m_header)
    {
        return;
    }

    // Settings have no header with the search
    PDFAppPage* page = getPage();
    m_header->setVisible(!page || page->hasSearchHeader());
    m_header->setTexts(page ? page->getHeaderTitle() : QString(), page ? page->getHeaderSubtitle() : QString());
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
