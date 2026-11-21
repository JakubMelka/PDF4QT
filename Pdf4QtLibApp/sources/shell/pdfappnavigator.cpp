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
#include "pdfapppageregistry.h"
#include "pdfapppagenotfound.h"
#include "pdfappdocumenttab.h"
#include "pdfapptab.h"
#include "pdfapptooltab.h"

#include "pdfdocumenttoolcatalog.h"

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
    if (!m_context || m_context->isPagesEnabled())
    {
        PDFAppPageRegistry::registerFactories(m_routeTable);
    }
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
        // An overlong address is refused, a shortened one would be another address
        openExternalUrl(link.size() <= PDFAppRoute::MAXIMUM_LENGTH ? QUrl(link, QUrl::StrictMode) : QUrl());
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
            case RequestKind::ContinueNavigate:
                if (current.tab)
                {
                    completeNavigate(current.tab, current.route, current.mode);
                }
                break;
            case RequestKind::ContinueBack:
            case RequestKind::ContinueForward:
                continueMoveInHistory(current);
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

PDFAppTab* PDFAppNavigator::chooseTab(const PDFAppRouteMatch& match, PDFAppNavigationMode mode, PDFAppTab* sourceTab, bool isCreating)
{
    if (!m_window)
    {
        return nullptr;
    }

    const PDFAppRouteEntry* entry = match.entry;
    const bool isNewTabRoute = entry && entry->isNewTabRoute && match.result != PDFAppRouteMatch::Result::InvalidParameter;

    // Document tabs: the tab of the id (step 3)
    if (entry && entry->tabKind == PDFAppTabKind::Document && match.result != PDFAppRouteMatch::Result::InvalidParameter)
    {
        if (PDFAppTab* tab = m_window->findDocumentTab(match.getParameter(QStringLiteral("documentId"))))
        {
            return tab;
        }
    }

    // The own tab of a tool (plan of step 2 of phase 2, 4.1): the tab of the instance; a route
    // without its tab (the history, a link) opens a new tab of that instance with an empty
    // list of inputs (378:1657). A tool without its own tab ends on Page Not Found.
    if (entry && entry->tabKind == PDFAppTabKind::Tool && match.isFound())
    {
        const QString instanceId = match.getParameter(QStringLiteral("instanceId"));
        if (PDFAppTab* tab = m_window->findToolTab(instanceId))
        {
            return tab;
        }

        // A tool with one tab in a window (Manage Certificates, isSingleOwnTab): a route of an older
        // instance (the history, a link) is shown by the tab, which exists - never a second tab
        // (navigateImpl shows the route of that tab)
        const QString toolId = match.getParameter(QStringLiteral("toolId"));
        if (PDFAppTab* tab = m_window->findSingleToolTab(toolId))
        {
            return tab;
        }
        if (!isCreating)
        {
            return nullptr;
        }
        if (PDFAppTab* tab = m_window->addToolTab(toolId, instanceId, nullptr))
        {
            return tab;
        }
    }

    // The manual: the Help tab of the window, a new one at the first use (owner
    // decision O13); other routes from the Help tab go to the Home tab
    if (entry && entry->tabKind == PDFAppTabKind::Help)
    {
        if (PDFAppTab* tab = m_window->findHelpTab())
        {
            return tab;
        }
        if (!isCreating)
        {
            // A new Help tab, or the Home tab, when the window has no place for it (no page with work)
            return nullptr;
        }
        if (PDFAppTab* tab = m_window->createHelpTab())
        {
            return tab;
        }
        return m_window->getHomeTab();
    }

    if (mode == PDFAppNavigationMode::NewTab && isNewTabRoute)
    {
        return isCreating ? m_window->createNewTab() : nullptr;
    }

    // A tab New tab keeps only /open, every other link goes to the Home tab
    if (sourceTab && sourceTab->getKind() == PDFAppTabKind::NewTab && isNewTabRoute && mode != PDFAppNavigationMode::NewTab)
    {
        return sourceTab;
    }

    return m_window->getHomeTab();
}

PDFAppDocumentTab* PDFAppNavigator::findOwnTabToolDocument(const PDFAppRouteMatch& match) const
{
    // A tool with a document host (Manage Pages, plan of step 3b, QB1 B) stays in the tab of the document:
    // started over a document, it changes that document
    if (!m_window || !match.isFound() || !match.entry || match.entry->tabKind != PDFAppTabKind::Document || match.entry->subject != PDFAppRouteSubject::Tool)
    {
        return nullptr;
    }

    const QString toolId = match.getParameter(QStringLiteral("toolId"));
    const PDFDocumentToolCatalogEntry* toolEntry = PDFDocumentToolCatalog::findTool(toolId);
    const bool hasDocumentHost = toolEntry && toolEntry->hasDocumentHost();
    PDFAppDocumentTab* documentTab = qobject_cast<PDFAppDocumentTab*>(m_window->findDocumentTab(match.getParameter(QStringLiteral("documentId"))));
    return documentTab && !hasDocumentHost && PDFAppToolTabPageFactory::hasOwnTabPage(toolId) ? documentTab : nullptr;
}

PDFAppPage* PDFAppNavigator::findLeftPage(const PDFAppRoute& route, PDFAppNavigationMode mode, PDFAppTab* sourceTab, PDFAppTab** targetTab)
{
    if (targetTab)
    {
        *targetTab = nullptr;
    }
    if (!m_window)
    {
        return nullptr;
    }

    // The same decisions as navigateImpl, without creating anything
    if (!sourceTab)
    {
        sourceTab = m_window->getCurrentTab();
    }
    PDFAppRoute target = route;
    const PDFAppRouteMatch match = resolve(&target);
    if (findOwnTabToolDocument(match))
    {
        return nullptr;
    }

    PDFAppTab* tab = chooseTab(match, mode, sourceTab, false);
    if (!tab)
    {
        return nullptr;
    }
    if (targetTab)
    {
        *targetTab = tab;
    }
    if (match.isFound() && match.entry && match.entry->tabKind == PDFAppTabKind::Tool)
    {
        const PDFAppToolTab* toolTab = qobject_cast<const PDFAppToolTab*>(tab);
        if (toolTab && toolTab->getInstanceId() != match.getParameter(QStringLiteral("instanceId")))
        {
            target = toolTab->getRoute();
        }
    }

    PDFAppPage* page = tab->getPage();
    return page && page->isLeftBy(target) ? page : nullptr;
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

    // A tool with its own tab asked for over a document (the panel Tools, Then continue with,
    // a link): a new tab of the tool with the document as its first input; the route leaves
    // no entry in the history of the document tab (plan of step 2 of phase 2, 4.1)
    if (PDFAppDocumentTab* documentTab = findOwnTabToolDocument(match))
    {
        m_window->createToolTabForDocument(match.getParameter(QStringLiteral("toolId")), documentTab);
        return;
    }

    QPointer<PDFAppTab> tab = chooseTab(match, mode, sourceTab);
    if (!tab)
    {
        return;
    }

    // The one tab of a tool chosen for a route of another instance (chooseTab): its own route
    if (match.isFound() && match.entry && match.entry->tabKind == PDFAppTabKind::Tool)
    {
        const PDFAppToolTab* toolTab = qobject_cast<const PDFAppToolTab*>(tab.data());
        if (toolTab && toolTab->getInstanceId() != match.getParameter(QStringLiteral("instanceId")))
        {
            target = toolTab->getRoute();
        }
    }

    // The shown page may refuse (a running tool asks, F14) or it ends its work (page
    // code: anything may be deleted). A0: a question of the page is shown, the navigation goes on after its answer
    // (onLeaveStepFinished)
    QPointer<PDFAppNavigator> guard(this);
    if (PDFAppPage* page = tab->getPage())
    {
        const PDFAppLeaveResult result = leavePage(tab, page, target, mode, RequestKind::ContinueNavigate, -1);
        if (!guard || !tab || result != PDFAppLeaveResult::Done)
        {
            return;
        }
    }

    completeNavigate(tab, target, mode);
}

PDFAppLeaveResult PDFAppNavigator::leavePage(PDFAppTab* tab, PDFAppPage* page, const PDFAppRoute& target, PDFAppNavigationMode mode, RequestKind continuation, int historyIndex)
{
    // One flow of the tab (A0): a navigation while a question of the tab is open (its close, another navigation, the
    // lock) is refused and the question comes forward
    PDFAppLeaveFlow* flow = tab->getLeaveFlow();
    if (!flow->start(PDFAppLeaveFlow::Goal::Navigate))
    {
        flow->raiseQuestion();
        return PDFAppLeaveResult::Refused;
    }
    flow->setTarget(target, mode);
    flow->setSubject(page);
    flow->setStep(int(continuation));
    flow->setValue(historyIndex);

    QPointer<PDFAppNavigator> guard(this);
    QPointer<PDFAppLeaveFlow> flowGuard(flow);
    QPointer<PDFAppPage> pageGuard(page);
    PDFAppLeaveResult result = page->requestLeave(target, flow);
    if (!guard || !flowGuard)
    {
        return PDFAppLeaveResult::Refused;
    }
    if (result == PDFAppLeaveResult::Pending)
    {
        if (pageGuard && flow->waitForStep(pageGuard.data(), this, &PDFAppNavigator::onLeaveStepFinished))
        {
            return PDFAppLeaveResult::Pending;
        }
        result = PDFAppLeaveResult::Refused;
    }

    // The page has ended its leave itself (Done) or stays (Refused): the flow ends (no page waits for its end)
    flow->finish(result == PDFAppLeaveResult::Done);
    return guard ? result : PDFAppLeaveResult::Refused;
}

void PDFAppNavigator::onLeaveStepFinished(PDFAppLeaveResult result)
{
    // The step belongs to the flow of a tab (the waiter is a child of the flow, the flow a child of the tab)
    QObject* step = sender();
    QPointer<PDFAppLeaveFlow> flow = step ? qobject_cast<PDFAppLeaveFlow*>(step->parent()) : nullptr;
    if (!flow || !flow->isRunning() || flow->getGoal() != PDFAppLeaveFlow::Goal::Navigate)
    {
        return;
    }

    // The tab, the page and the history are read again: the navigation goes on only from the page, which was asked
    Request request;
    request.kind = RequestKind(flow->getStep());
    request.route = flow->getTargetRoute();
    request.mode = flow->getTargetMode();
    request.tab = qobject_cast<PDFAppTab*>(flow->parent());
    request.historyIndex = int(flow->getValue());
    const QPointer<QObject> page = flow->getSubject();
    const bool isDone = result == PDFAppLeaveResult::Done;
    QPointer<PDFAppNavigator> guard(this);
    flow->finish(isDone);
    if (!guard || !isDone || !request.tab || !page || request.tab->getPage() != page.data())
    {
        return;
    }
    if (request.kind != RequestKind::ContinueNavigate && request.kind != RequestKind::ContinueBack && request.kind != RequestKind::ContinueForward)
    {
        return;
    }
    process(request);
}

void PDFAppNavigator::completeNavigate(PDFAppTab* tabPointer, const PDFAppRoute& target, PDFAppNavigationMode mode)
{
    if (!m_window)
    {
        return;
    }

    // The state of the view of the page, which the tab leaves (page code: the
    // tab may be deleted by it)
    QPointer<PDFAppNavigator> guard(this);
    QPointer<PDFAppTab> tab(tabPointer);
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

    // Nothing to do without an entry there (the page is not asked, it stays)
    const PDFAppHistoryEntry* targetEntry = tab->getHistory().getEntry(tab->getHistory().getCurrentIndex() + (isBack ? -1 : 1));
    if (!targetEntry)
    {
        return false;
    }

    // The shown page may refuse (a running tool asks, F14) or it ends its work. A0: a question of the page is shown,
    // the step in the history goes on after its answer (onLeaveStepFinished, continueMoveInHistory)
    if (PDFAppPage* page = tab->getPage())
    {
        const PDFAppRoute target = targetEntry->route;
        const int index = tab->getHistory().getCurrentIndex();
        const PDFAppLeaveResult result = leavePage(tab, page, target, PDFAppNavigationMode::Push, isBack ? RequestKind::ContinueBack : RequestKind::ContinueForward, index);
        if (!guard || !tabGuard || result != PDFAppLeaveResult::Done)
        {
            return false;
        }
    }

    return completeMoveInHistory(tab, isBack);
}

void PDFAppNavigator::continueMoveInHistory(const Request& request)
{
    // The answer of the page came later: the step goes on only from the same entry of the history (the tab, its
    // history may have changed meanwhile)
    PDFAppTab* tab = request.tab.data();
    if (!tab || tab->getHistory().getCurrentIndex() != request.historyIndex)
    {
        return;
    }
    const bool isBack = request.kind == RequestKind::ContinueBack;
    const PDFAppHistoryEntry* targetEntry = tab->getHistory().getEntry(request.historyIndex + (isBack ? -1 : 1));
    if (!targetEntry || !(targetEntry->route == request.route))
    {
        return;
    }
    completeMoveInHistory(tab, isBack);
}

bool PDFAppNavigator::completeMoveInHistory(PDFAppTab* tab, bool isBack)
{
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
    // the id was not found, when another tab got it); the manual is shown in the Home
    // tab, when the window has no place for the Help tab (chooseTab)
    const bool isTabOfRoute = match.entry && (match.entry->tabKind == tab->getKind() ||
                                              (match.entry->tabKind == PDFAppTabKind::Home && tab->getKind() == PDFAppTabKind::NewTab) ||
                                              (match.entry->tabKind == PDFAppTabKind::Help && tab->getKind() == PDFAppTabKind::Home));

    // Page of the factory (code of the page: it may delete anything)
    QPointer<PDFAppPage> page;
    if (match.isFound() && isTabOfRoute && match.entry->factory)
    {
        std::shared_ptr<const PDFAppPageFactory> factory = match.entry->factory;
        page = factory->createPageForRoute(m_context, tab, resolvedRoute);
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
    connect(page, &PDFAppPage::routeReplaced, this, &PDFAppNavigator::onPageRouteReplaced, Qt::UniqueConnection);
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

    // Show in Folder: an existing local folder. A folder of another computer
    // (file://server/share, UNC path //server/share, \\?\ device path) is refused
    // before it is touched: an access to it connects to the server (SMB) and
    // gives it the credentials of the user
    if (url.isLocalFile())
    {
        const QString path = url.toLocalFile();
        if (!url.host().isEmpty() || path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\")))
        {
            return false;
        }

        const QFileInfo info(path);
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

void PDFAppNavigator::onPageRouteReplaced(const PDFAppRoute& route)
{
    // The page does not identify itself in the signal. Only the shown page of a
    // tab changes the history: a page, which is being shown (its setRoute runs
    // during a navigation) or which has been replaced already, has no entry.
    PDFAppPage* page = qobject_cast<PDFAppPage*>(sender());
    PDFAppTab* tab = page ? findTab(page) : nullptr;
    if (!tab || tab->getPage() != page || !m_window)
    {
        return;
    }

    PDFAppHistory& history = tab->getHistory();
    const PDFAppHistoryEntry* entry = history.getCurrentEntry();
    if (!entry)
    {
        return;
    }

    // The route of another page would show a page, which is not there
    PDFAppRoute currentRoute = entry->route;
    PDFAppRoute replacedRoute = route;
    const PDFAppRouteMatch currentMatch = resolve(&currentRoute);
    const PDFAppRouteMatch replacedMatch = resolve(&replacedRoute);
    // (a factory, whose page depends on the route, compares the parameters of its page)
    const bool isSamePage = currentMatch.entry && (!currentMatch.entry->factory || currentMatch.entry->factory->isSamePage(currentRoute, replacedRoute));
    if (!replacedMatch.isFound() || !currentMatch.entry || replacedMatch.entry != currentMatch.entry || replacedRoute != route || !isSamePage)
    {
        qWarning("A page has replaced its route by a route of another page, it is refused: %s", qPrintable(route.toString().left(256)));
        return;
    }

    history.replace(route);

    // The title of the window follows the page (no announcement: the page stays)
    QPointer<PDFAppNavigator> guard(this);
    QPointer<PDFAppTab> tabGuard(tab);
    m_window->updateTab(tab);
    if (guard && tabGuard)
    {
        const PDFAppRoute replaced = route;
        Q_EMIT routeReplaced(tab, replaced);
    }
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

#include "moc_pdfappnavigator.cpp"
