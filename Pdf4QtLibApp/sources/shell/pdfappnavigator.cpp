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

#include "pdfappnavigator.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappmainwindow.h"
#include "pdfapppagenotfound.h"
#include "pdfapptab.h"

#include <QAccessible>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>

namespace pdf
{

PDFAppNavigator::PDFAppNavigator(PDFAppMainWindow* window, PDFAppContext* context) :
    BaseClass(window),
    m_window(window),
    m_context(context),
    m_routeTable(PDFAppRouteTable::createDefault())
{

}

PDFAppNavigator::~PDFAppNavigator() = default;

void PDFAppNavigator::navigate(const PDFAppRoute& route, PDFAppNavigationMode mode, PDFAppTab* sourceTab)
{
    Request request;
    request.kind = RequestKind::Navigate;
    request.route = route;
    request.mode = mode;
    request.tab = sourceTab;
    process(request);
}

void PDFAppNavigator::navigateLink(const QString& link, PDFAppNavigationMode mode, PDFAppTab* sourceTab)
{
    if (link.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
    {
        openExternalUrl(QUrl(link.left(PDFAppRoute::MAXIMUM_LENGTH), QUrl::StrictMode));
        return;
    }

    navigate(PDFAppRoute::parse(link), mode, sourceTab);
}

bool PDFAppNavigator::goBack(PDFAppTab* tab)
{
    PDFAppTab* target = tab ? tab : (m_window ? m_window->getCurrentTab() : nullptr);
    if (!target || !target->getHistory().canGoBack())
    {
        return false;
    }

    Request request;
    request.kind = RequestKind::Back;
    request.tab = target;
    process(request);
    return true;
}

bool PDFAppNavigator::goForward(PDFAppTab* tab)
{
    PDFAppTab* target = tab ? tab : (m_window ? m_window->getCurrentTab() : nullptr);
    if (!target || !target->getHistory().canGoForward())
    {
        return false;
    }

    Request request;
    request.kind = RequestKind::Forward;
    request.tab = target;
    process(request);
    return true;
}

void PDFAppNavigator::process(const Request& request)
{
    if (m_isNavigating)
    {
        // Requested from the code of a page during a navigation: done after it,
        // the last request wins
        m_pendingRequests.clear();
        m_pendingRequests.append(request);
        return;
    }

    QPointer<PDFAppNavigator> guard(this);
    Request current = request;
    for (int round = 0; round <= MAXIMUM_PENDING_ROUNDS; ++round)
    {
        m_isNavigating = true;
        switch (current.kind)
        {
            case RequestKind::Navigate:
                navigateImpl(current.route, current.mode, current.tab);
                break;
            case RequestKind::Back:
                moveInHistory(current.tab, true);
                break;
            case RequestKind::Forward:
                moveInHistory(current.tab, false);
                break;
        }

        if (!guard)
        {
            return;
        }
        m_isNavigating = false;

        if (m_pendingRequests.isEmpty())
        {
            return;
        }
        current = m_pendingRequests.takeFirst();
        m_pendingRequests.clear();
    }

    // A page, which navigates again and again from its setRoute, is stopped
    qWarning("Navigation stopped after %d redirections by pages", MAXIMUM_PENDING_ROUNDS);
    m_pendingRequests.clear();
}

PDFAppRouteMatch PDFAppNavigator::resolve(PDFAppRoute* route) const
{
    PDFAppRouteMatch match = m_routeTable.match(*route);
    for (int i = 0; i < MAXIMUM_REDIRECTS && match.result == PDFAppRouteMatch::Result::Redirect; ++i)
    {
        *route = match.redirectedRoute;
        match = m_routeTable.match(*route);
    }

    if (match.result == PDFAppRouteMatch::Result::Redirect)
    {
        // A cycle of redirects ends on Page Not Found
        match.result = PDFAppRouteMatch::Result::UnknownRoute;
        match.entry = nullptr;
    }
    return match;
}

PDFAppTab* PDFAppNavigator::chooseTab(const PDFAppRouteMatch& match, PDFAppNavigationMode mode, PDFAppTab* sourceTab)
{
    if (!m_window)
    {
        return nullptr;
    }

    const PDFAppRouteEntry* entry = match.entry;
    const bool isNewTabRoute = entry && entry->isNewTabRoute && match.result != PDFAppRouteMatch::Result::InvalidParameter;

    // Document and tool tabs: the tab of the id (step 3, phase 2)
    if (entry && (entry->tabKind == PDFAppTabKind::Document || entry->tabKind == PDFAppTabKind::Tool) &&
        match.result != PDFAppRouteMatch::Result::InvalidParameter)
    {
        const QString id = entry->tabKind == PDFAppTabKind::Document ? match.getParameter(QStringLiteral("documentId"))
                                                                     : match.getParameter(QStringLiteral("instanceId"));
        if (PDFAppTab* tab = m_window->findDocumentTab(id))
        {
            return tab;
        }
    }

    if (mode == PDFAppNavigationMode::NewTab && isNewTabRoute)
    {
        return m_window->createNewTab();
    }

    // A tab New tab keeps only /open, every other link goes to the Home tab
    if (sourceTab && sourceTab->getKind() == PDFAppTabKind::NewTab && isNewTabRoute && mode != PDFAppNavigationMode::NewTab)
    {
        return sourceTab;
    }

    return m_window->getHomeTab();
}

void PDFAppNavigator::saveViewState(PDFAppTab* tab)
{
    if (!tab)
    {
        return;
    }

    if (PDFAppPage* page = tab->getPage())
    {
        tab->getHistory().setCurrentViewState(page->saveViewState());
    }
}

void PDFAppNavigator::navigateImpl(const PDFAppRoute& route, PDFAppNavigationMode mode, PDFAppTab* sourceTab)
{
    if (!m_window)
    {
        return;
    }

    if (!sourceTab)
    {
        sourceTab = m_window->getCurrentTab();
    }

    PDFAppRoute target = route;
    const PDFAppRouteMatch match = resolve(&target);

    QPointer<PDFAppTab> tab = chooseTab(match, mode, sourceTab);
    if (!tab)
    {
        return;
    }

    // The state of the view of the page, which the tab leaves (page code: the
    // tab may be deleted by it)
    QPointer<PDFAppNavigator> guard(this);
    saveViewState(tab);
    if (!guard || !tab)
    {
        return;
    }

    // The same route again replaces the entry (Go to Home on Home, a link to itself)
    PDFAppHistory& history = tab->getHistory();
    const PDFAppHistoryEntry* currentEntry = history.getCurrentEntry();
    if (mode == PDFAppNavigationMode::Replace || (currentEntry && currentEntry->route == target))
    {
        history.replace(target);
    }
    else
    {
        history.push(target);
    }

    m_window->activateTab(tab);
    if (!guard || !tab)
    {
        return;
    }

    showCurrentEntry(tab, false);
}

bool PDFAppNavigator::moveInHistory(PDFAppTab* tab, bool isBack)
{
    if (!tab)
    {
        return false;
    }

    QPointer<PDFAppNavigator> guard(this);
    QPointer<PDFAppTab> tabGuard(tab);
    saveViewState(tab);
    if (!guard || !tabGuard)
    {
        return false;
    }

    PDFAppHistory& history = tab->getHistory();
    if (!(isBack ? history.goBack() : history.goForward()))
    {
        return false;
    }

    if (m_window)
    {
        m_window->activateTab(tab);
    }
    if (!guard || !tabGuard)
    {
        return false;
    }

    showCurrentEntry(tab, true);
    return true;
}

void PDFAppNavigator::showCurrentEntry(PDFAppTab* tab, bool restoreViewState)
{
    const PDFAppHistoryEntry* entry = tab ? tab->getHistory().getCurrentEntry() : nullptr;
    if (!entry)
    {
        return;
    }

    const PDFAppRoute route = entry->route;
    const QVariantMap viewState = entry->viewState;
    const bool canGoBack = tab->getHistory().canGoBack();

    QPointer<PDFAppNavigator> guard(this);
    QPointer<PDFAppTab> tabGuard(tab);
    PDFAppRoute resolvedRoute = route;
    const PDFAppRouteMatch match = resolve(&resolvedRoute);

    // A route of a document or of a tool is shown only in its own tab (the tab of
    // the id was not found, when another tab got it)
    const bool isTabOfRoute = match.entry && (match.entry->tabKind == tab->getKind() ||
                                              (match.entry->tabKind == PDFAppTabKind::Home && tab->getKind() == PDFAppTabKind::NewTab));

    // Page of the factory (code of the page: it may delete anything)
    QPointer<PDFAppPage> page;
    if (match.isFound() && isTabOfRoute && match.entry->factory)
    {
        std::shared_ptr<const PDFAppPageFactory> factory = match.entry->factory;
        page = factory->createPage(m_context, tab);
        if (!guard || !tabGuard)
        {
            delete page.data();
            return;
        }

        if (page)
        {
            // The page may ask for a route already from setRoute (done after this navigation)
            connectPage(page);
            const bool isAccepted = page->setRoute(resolvedRoute);
            if (!guard || !tabGuard)
            {
                delete page.data();
                return;
            }

            if (!isAccepted)
            {
                delete page.data();
            }
        }
    }

    const bool isNotFound = page.isNull();
    if (isNotFound)
    {
        PDFAppNotFoundRequest request;
        request.route = route;
        request.description = m_routeTable.describe(route);
        request.searchText = m_routeTable.getSearchText(route);
        request.canGoBack = canGoBack;
        if (m_context && m_context->getDatabase())
        {
            const PDFAppCommandRegistry::UserShortcuts shortcuts = m_context->getShortcuts().getShortcuts();
            request.backShortcut = m_context->getCommandRegistry()->getShortcut(PDFAppCommands::back(), shortcuts);
            request.searchShortcut = m_context->getCommandRegistry()->getShortcut(PDFAppCommands::search(), shortcuts);
        }
        else if (m_context)
        {
            request.backShortcut = m_context->getCommandRegistry()->getShortcut(PDFAppCommands::back(), PDFAppCommandRegistry::UserShortcuts());
            request.searchShortcut = m_context->getCommandRegistry()->getShortcut(PDFAppCommands::search(), PDFAppCommandRegistry::UserShortcuts());
        }

        PDFPageNotFoundPage* notFoundPage = new PDFPageNotFoundPage(m_context, tab);
        notFoundPage->setRequest(request);
        page = notFoundPage;
        connectPage(page);
    }

    // The previous page does not navigate any more (it is deleted later)
    if (PDFAppPage* oldPage = tab->getPage())
    {
        oldPage->disconnect(this);
    }

    tab->setPage(page);

    if (restoreViewState && !viewState.isEmpty())
    {
        page->restoreViewState(viewState);
        if (!guard || !tabGuard || !page)
        {
            return;
        }
    }

    tab->setNavItem(isNotFound || !match.entry ? PDFAppNavItem::None : match.entry->navItem);

    if (m_window)
    {
        m_window->updateTab(tab);
    }

    announce(tab, page);

    const PDFAppRoute shownRoute = route;
    Q_EMIT navigated(tab, shownRoute, isNotFound);
}

void PDFAppNavigator::connectPage(PDFAppPage* page)
{
    connect(page, &PDFAppPage::navigationRequested, this, &PDFAppNavigator::onPageNavigationRequested, Qt::UniqueConnection);
    connect(page, &PDFAppPage::backRequested, this, &PDFAppNavigator::onPageBackRequested, Qt::UniqueConnection);
    connect(page, &PDFAppPage::externalUrlRequested, this, &PDFAppNavigator::onPageExternalUrlRequested, Qt::UniqueConnection);
}

void PDFAppNavigator::announce(PDFAppTab* tab, PDFAppPage* page)
{
    if (!page || !m_window || m_window->getCurrentTab() != tab)
    {
        return;
    }

    m_lastAnnouncement = tr("%1 page").arg(page->getTitle());

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Assistive technologies read the new page (the focus stays, where it was)
    if (QAccessible::isActive())
    {
        QAccessibleAnnouncementEvent event(page, m_lastAnnouncement);
        QAccessible::updateAccessibility(&event);
    }
#endif
}

bool PDFAppNavigator::isExternalUrlAllowed(const QUrl& url)
{
    if (!url.isValid())
    {
        return false;
    }

    if (url.scheme() == QLatin1String("https"))
    {
        return !url.host().isEmpty() && url.userInfo().isEmpty();
    }

    // Show in Folder: an existing local folder
    if (url.isLocalFile())
    {
        const QFileInfo info(url.toLocalFile());
        return info.isAbsolute() && info.isDir();
    }

    return false;
}

bool PDFAppNavigator::openExternalUrl(const QUrl& url)
{
    if (!isExternalUrlAllowed(url))
    {
        qWarning("A link to an address, which is not allowed, is refused: %s", qPrintable(url.toString(QUrl::RemoveUserInfo).left(256)));
        return false;
    }

    QPointer<PDFAppNavigator> guard(this);
    Q_EMIT externalUrlOpened(url);
    if (guard && m_externalUrlsEnabled)
    {
        QDesktopServices::openUrl(url);
    }
    return true;
}

void PDFAppNavigator::connectTab(PDFAppTab* tab)
{
    if (!tab)
    {
        return;
    }

    connect(tab, &PDFAppTab::navigationRequested, this, &PDFAppNavigator::onTabNavigationRequested);
    connect(tab, &PDFAppTab::externalUrlRequested, this, &PDFAppNavigator::onTabExternalUrlRequested);
}

PDFAppTab* PDFAppNavigator::findTab(QObject* object)
{
    for (QObject* current = object; current; current = current->parent())
    {
        if (PDFAppTab* tab = qobject_cast<PDFAppTab*>(current))
        {
            return tab;
        }
    }
    return nullptr;
}

void PDFAppNavigator::onPageNavigationRequested(const PDFAppRoute& route, PDFAppNavigationMode mode)
{
    // The page does not identify itself in the signal
    PDFAppPage* page = qobject_cast<PDFAppPage*>(sender());
    const PDFAppRoute routeCopy = route;
    navigate(routeCopy, mode, page ? findTab(page) : nullptr);
}

void PDFAppNavigator::onPageBackRequested()
{
    PDFAppPage* page = qobject_cast<PDFAppPage*>(sender());
    goBack(page ? findTab(page) : nullptr);
}

void PDFAppNavigator::onPageExternalUrlRequested(const QUrl& url)
{
    openExternalUrl(url);
}

void PDFAppNavigator::onTabNavigationRequested(const PDFAppRoute& route, PDFAppNavigationMode mode)
{
    PDFAppTab* tab = qobject_cast<PDFAppTab*>(sender());
    const PDFAppRoute routeCopy = route;
    navigate(routeCopy, mode, tab);
}

void PDFAppNavigator::onTabExternalUrlRequested(const QUrl& url)
{
    openExternalUrl(url);
}

}   // namespace pdf
