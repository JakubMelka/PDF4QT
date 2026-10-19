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

#ifndef PDFAPPNAVIGATOR_H
#define PDFAPPNAVIGATOR_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfapproutetable.h"
#include "pdfapptab.h"

#include <QObject>
#include <QPointer>
#include <QUrl>

namespace pdf
{

class PDFAppMainWindow;

/// The only way to show a route (newapp_navigation.md 2): navigate(route, mode)
/// decides the tab, creates the page by the factory of the route table, shows
/// Page Not Found for an unknown route, an unknown parameter or a page, which is
/// not implemented yet, highlights the item of the navigation, updates the
/// titles and announces the new page to assistive technologies. External
/// addresses (https) are opened by the system, nothing else leaves the navigator.
///
/// Tabs: a route of the Home tab is shown in the Home tab; a tab New tab shows
/// only /open, every other link from it switches to the Home tab. Mode NewTab
/// opens a new tab New tab for /open (any other route goes to the Home tab).
///
/// A navigation started while another one runs (a page asks for a route from
/// its constructor or from setRoute) is done after it; the last such request
/// wins. Pages, tabs and the window may be deleted by the code of a page during
/// the navigation: the navigator survives it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNavigator : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Maximal number of redirects of one navigation
    static constexpr int MAXIMUM_REDIRECTS = 4;

    /// Maximal number of navigations requested during a navigation, which are done after it
    static constexpr int MAXIMUM_PENDING_ROUNDS = 8;

    explicit PDFAppNavigator(PDFAppMainWindow* window, PDFAppContext* context);
    virtual ~PDFAppNavigator() override;

    /// Table of the routes (later steps set the factories of their pages)
    PDFAppRouteTable& getRouteTable() { return m_routeTable; }
    const PDFAppRouteTable& getRouteTable() const { return m_routeTable; }

    /// Shows the route; \p sourceTab is the tab, where the link was activated
    /// (nullptr = the current tab)
    void navigate(const PDFAppRoute& route, PDFAppNavigationMode mode = PDFAppNavigationMode::Push, PDFAppTab* sourceTab = nullptr);

    /// A link: an https address is opened by the system, anything else is a route
    void navigateLink(const QString& link, PDFAppNavigationMode mode = PDFAppNavigationMode::Push, PDFAppTab* sourceTab = nullptr);

    /// Previous / next entry of the history of the tab (nullptr = the current
    /// tab). \returns false, if there is none.
    bool goBack(PDFAppTab* tab = nullptr);
    bool goForward(PDFAppTab* tab = nullptr);

    /// Opens an external address by the system: https, or a local folder (Show in
    /// Folder). \returns false for anything else.
    bool openExternalUrl(const QUrl& url);

    /// The address may be opened by the system
    static bool isExternalUrlAllowed(const QUrl& url);

    /// External addresses are given to the system (tests turn it off and watch
    /// externalUrlOpened only)
    void setExternalUrlsEnabled(bool enabled) { m_externalUrlsEnabled = enabled; }

    /// Connects the parts of the tab (sidebar, search, banner) to the navigator
    void connectTab(PDFAppTab* tab);

    /// The window is being destroyed: the navigator does nothing any more
    void detachWindow() { m_window = nullptr; }

    /// Text of the last announcement of a page change (tests)
    const QString& getLastAnnouncement() const { return m_lastAnnouncement; }

Q_SIGNALS:
    /// The route has been shown in the tab (Page Not Found, when isNotFound)
    void navigated(pdf::PDFAppTab* tab, const pdf::PDFAppRoute& route, bool isNotFound);

    /// An external address has been given to the system
    void externalUrlOpened(const QUrl& url);

private:
    enum class RequestKind
    {
        Navigate,
        Back,
        Forward
    };

    struct Request
    {
        RequestKind kind = RequestKind::Navigate;
        PDFAppRoute route;
        PDFAppNavigationMode mode = PDFAppNavigationMode::Push;
        QPointer<PDFAppTab> tab;
    };

    void process(const Request& request);
    void navigateImpl(const PDFAppRoute& route, PDFAppNavigationMode mode, PDFAppTab* sourceTab);
    bool moveInHistory(PDFAppTab* tab, bool isBack);
    PDFAppRouteMatch resolve(PDFAppRoute* route) const;
    PDFAppTab* chooseTab(const PDFAppRouteMatch& match, PDFAppNavigationMode mode, PDFAppTab* sourceTab);
    void showCurrentEntry(PDFAppTab* tab, bool restoreViewState);
    void announce(PDFAppTab* tab, PDFAppPage* page);
    void connectPage(PDFAppPage* page);
    void saveViewState(PDFAppTab* tab);

    /// Tab, which holds the page (nullptr, if it is not in a tab)
    static PDFAppTab* findTab(QObject* object);

    void onPageNavigationRequested(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode);
    void onPageBackRequested();
    void onPageExternalUrlRequested(const QUrl& url);
    void onTabNavigationRequested(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode);
    void onTabExternalUrlRequested(const QUrl& url);

    /// The parent window (detached by its destructor)
    PDFAppMainWindow* m_window = nullptr;
    QPointer<PDFAppContext> m_context;
    PDFAppRouteTable m_routeTable;
    bool m_isNavigating = false;
    bool m_externalUrlsEnabled = true;
    QList<Request> m_pendingRequests;
    QString m_lastAnnouncement;
};

}   // namespace pdf

#endif // PDFAPPNAVIGATOR_H
