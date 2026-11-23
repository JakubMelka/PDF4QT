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

#ifndef PDFAPPPAGE_H
#define PDFAPPPAGE_H

#include "pdfappglobal.h"
#include "pdfappcontext.h"
#include "pdfapproute.h"

#include "pdfaccenthue.h"
#include "pdfdeferreddeletion.h"

#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

#include <vector>

class QBoxLayout;

namespace pdf
{

/// Part of the breadcrumbs of the header of a page (Help › Protect & Sign › Sign
/// with Certificate): a link to the route, or the current page (invalid route)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBreadcrumbItem
{
public:
    QString text;
    PDFAppRoute route;
};

/// Icon of the header of a page before its title and subtitle (Manage collections
/// 283:1932): the Tool icon of the size 44 (PDFComponentPainter::BadgeSize::Large,
/// "44 in the header of a workspace" of the component 89:1289) in its hue, 14 px
/// before the texts. An empty name = no icon (the texts start at the edge).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHeaderIcon
{
public:
    QString iconName;
    PDFAccentHue hue = PDFAccentHue::Neutral;

    bool isValid() const { return !iconName.isEmpty(); }
    bool operator==(const PDFAppHeaderIcon&) const = default;
};

/// How the navigator shows a route
enum class PDFAppNavigationMode
{
    Push,       ///< A new entry of the history of the tab
    Replace,    ///< Replaces the current entry of the history
    NewTab      ///< A new tab New tab (only the route /open stays in it)
};

class PDFAppLeaveFlow;

/// The result of a step of a leave without a nested event loop (A0, RR3-M5: PDFAppLeaveFlow, the request functions
/// of PDFAppPage and of the tools)
enum class PDFAppLeaveResult
{
    Done,       ///< The step has ended at once: the leave may go on
    Refused,    ///< The leave is refused: the page stays (Keep, Cancel, a question gone without an answer)
    Pending     ///< A question is open: the result comes later by PDFAppLeaveFlow::finishStep(reporter, Done / Refused)
};

/// Base of a page of the application (newapp_navigation.md 2). The navigator
/// creates the page, when its route is shown, and destroys it, when the tab
/// shows another route; a page reads its data from the database and keeps
/// nothing else. A page never calls another page: it asks for a route by the
/// signal navigationRequested (the navigator may delete the page then - the
/// page must not touch itself after the emission, see requestNavigation).
///
/// Deferred deletion (step A): a page is closed only by PDFDeferredDeletion::retire (with its tab, or by the
/// navigation, which replaces it); its hook retire() ends the work of the page outside its subtree.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPage : public QWidget, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppPage() override;

    /// Shows the route (its parameters, query and fragment). \returns false, if
    /// the page cannot show it (an unknown value): the navigator shows Page Not
    /// Found instead. The base stores the route and returns true.
    virtual bool setRoute(const PDFAppRoute& route);

    /// Route of the page
    const PDFAppRoute& getRoute() const { return m_route; }

    /// Title of the page (window title, announcement for assistive technologies)
    virtual QString getTitle() const = 0;

    /// Topic of the user manual, which F1 opens on this page (the command Help of the
    /// window). The default topic by default: Home, About, Support, Sponsor and Page
    /// Not Found have no better one. A topic, which the catalog does not have, ends
    /// on Page Not Found in the Help tab: a page returns only an existing topic (the
    /// page of a tool asks PDFAppHelpCatalog::getToolTopic, it may have no topic yet).
    virtual QString getHelpTopic() const;

    /// Title and subtitle of the header of the workspace (empty = none)
    virtual QString getHeaderTitle() const;
    virtual QString getHeaderSubtitle() const;

    /// Part of the subtitle stressed by Semi Bold text/accent (the first occurrence;
    /// empty = none - the subtitle of Home 28:35 stresses "PDF")
    virtual QString getHeaderSubtitleAccent() const;

    /// Breadcrumbs shown above the title of the header (empty = none; pdf4qt-help
    /// 955:152873, pdf4qt-whatsnew-notes 989:152125)
    virtual QList<PDFAppBreadcrumbItem> getHeaderBreadcrumbs() const;

    /// Icon before the title and the subtitle of the header (none by default)
    virtual PDFAppHeaderIcon getHeaderIcon() const;

    /// Tag after the title of the header (the category of a tool 276:1988), empty = none
    virtual QString getHeaderTag() const;

    /// A file opened or dropped while this page is shown goes to this tool, empty = it
    /// only opens (the start page of a tool, 276:1737)
    virtual QString getOpenFilesToolId() const;

    /// The workspace shows its header with the global search (Settings has none)
    virtual bool hasSearchHeader() const;

    /// Layout, at the top of which the Home tab places its storage banner: the main
    /// column of a page with a side column (958:28036 "at the top of the content
    /// column", pdf4qt-recent-portable 955:150880 - the side column starts at the
    /// top of the page). nullptr (the default) = above the whole page. The layout
    /// must belong to a widget of the page; the tab takes the banner back, when the
    /// page is replaced or destroyed (aboutToBeDestroyed).
    virtual QBoxLayout* getBannerLayout() const;

    /// State of the view saved into the history, when the tab leaves the page
    /// (scroll position, expanded sections), and given back, when the history
    /// returns to it
    virtual QVariantMap saveViewState() const;
    virtual void restoreViewState(const QVariantMap& state);

    /// The page holds work of the user, which its close loses and which it would ask about (requestClose):
    /// an assistant never closes it over the work - its close is refused (owner 2026-11-20). Default: false.
    virtual bool hasUnsavedWork() const;

    /// A number, which changes with the work of hasUnsavedWork (an answer about the work holds only while it is the
    /// same: PDFAppSessionEnd::isToolWorkDiscarded). Default: 0 (no work).
    virtual quint64 getWorkStamp() const;

    /// The tab of the page closes now, or the window quits (every question has been answered, the tab is still
    /// whole, its deletion follows): the page records, what it must record before its teardown (the use of an
    /// interactive tool, owner 2026-11-20 - the destructors never read the tab). Nothing is asked, ended or
    /// emitted. The default does nothing.
    virtual void prepareClose();

    /// A question of the page waits for the answer of the user (the question of the unsaved work of a tool, the
    /// question of its run): an assistant changes no tab meanwhile (RESCCODE L6). Default: false.
    virtual bool isQuestionOpen() const;

    /// The page is closed: retired (step A), or replaced by the navigation of its tab and waiting for its retire from
    /// the event loop (PDFAppTab::setPage; the rest of the emission, which replaced it, may still run its slots). A
    /// closed page asks nothing and opens nothing.
    bool isClosed() const;

    /// A navigation of the tab to \p target leaves the page (requestLeave would end what it shows); false = the
    /// page stays (the route of the page itself). An assistant's navigation, which would leave a page with
    /// unsaved work, is refused (RESCCODE L5). Default: every route except the route of the page.
    virtual bool isLeftBy(const PDFAppRoute& target) const;

    /// Ends the leave, which the page has accepted (requestClose, requestDocumentLock returned Done; requestLeave
    /// ends its own leave): the code, which started the leave, calls it exactly once, when it knows the outcome
    /// (owner 2026-11-20, decision 8). \p isProceeding - the leave goes on (the quit and the close of a tab before
    /// the question of the unsaved documents, the lock before the lock): a choice of a question of the page, which
    /// changes the document (Redact "Remove New Marks", Measure "Save and Close"), is applied now; false = the leave
    /// was canceled (a later Keep, Cancel of another question), the page forgets the choice and keeps its work. A page
    /// deleted before the end forgets it too. \returns false, when the applied choice could not keep all of the work
    /// (owner 2026-11-21: a measurement, which cannot be stored): the page stays with it and told the user why, the
    /// caller cancels the leave (the window stays, the tab stays, no lock, no navigation); true otherwise. The page,
    /// its tab or the window may be deleted meanwhile (the caller guards them). The default returns true.
    virtual bool finishLeave(bool isProceeding);

    /// finishLeave(\p isProceeding) of every page of \p pages, which still exists (each may delete the others). After
    /// a page has refused (false), the rest are ended by false: the leave is canceled, no further choice is applied.
    /// \returns false, when a page has refused.
    static bool finishLeaves(const std::vector<QPointer<PDFAppPage>>& pages, bool isProceeding);

    // ------------------------------------------------------------- leave without a nested event loop (A0)
    //
    // A page, which asks the user, shows its question (show / open, never exec; PDFAppDialogAnswer,
    // PDFAppDocumentChoiceDialog::ask) and returns Pending; it reports the answer exactly once by
    // PDFAppLeaveFlow::finishStep(this page, Done / Refused) - never inside the call, which returned Pending - and the
    // caller waits for it by PDFAppLeaveFlow::waitForStep(page, caller, slot) right after the call. A question
    // deleted without an answer is Refused (the flow continues on its deletion); a page deleted before it reports is
    // Refused (PDFAppLeaveFlowStep). The page reads the state again when its answer comes (the tab, the document, the
    // tool may have changed meanwhile). The calls may run code of the application (the activation of the tab of the
    // page): the caller guards itself after them.

    /// The navigator asks before the tab leaves the shown page for \p target (a navigation or a step in the history,
    /// F14 of the plan of the first batch of phase 2). Done = the page has ended its leave itself (the transaction
    /// finishLeave of a tool page is the page's own here), the page is replaced, the navigation goes on; Refused =
    /// the page stays and the history does not change (a running tool: the user has chosen Keep Running); Pending =
    /// its question is open. The default is Done.
    virtual PDFAppLeaveResult requestLeave(const PDFAppRoute& target, PDFAppLeaveFlow* flow);

    /// The tab of the page closes, or the window quits (owner 2026-11-20: a tool with unsaved work of the user asks
    /// its question, also when its tab or the window closes). Done = the page may go; the caller records it by
    /// PDFAppLeaveFlow::addAcceptedPage and ends it by finishLeave at the end of the transaction
    /// (PDFAppLeaveFlow::requestFinishPages / finish); nothing is ended here: a close canceled by a later question
    /// keeps the page as it is. Refused = the page stays. Pending = its question is open. The default is Done.
    virtual PDFAppLeaveResult requestClose(PDFAppLeaveFlow* flow);

    /// The document of the tab of the page is being locked by the user (owner 2026-11-20, decision 6): a tool, which
    /// the lock ends, asks its question of the unsaved work first. Done = the lock may go on (the caller records the
    /// page as requestClose); Refused = the user keeps the tool (the document stays unlocked); Pending = its question
    /// is open. The default is Done.
    virtual PDFAppLeaveResult requestDocumentLock(PDFAppLeaveFlow* flow);

    /// finishLeave, which may ask (A0, P1; PDFAppLeaveFlow::requestFinishPages calls it). Done = the
    /// leave goes on (finishLeave true); Refused = the applied choice could not keep all of the work, the caller cancels
    /// the leave (finishLeave false); Pending = the page asks about the rest (Measure "Keep Measuring / Discard and
    /// Close", its question open): its result comes by \p flow->finishStep(this page, Done / Refused), never inside the
    /// call. \p isProceeding false never asks. The default calls finishLeave.
    virtual PDFAppLeaveResult requestFinishLeave(bool isProceeding, PDFAppLeaveFlow* flow);

    PDFAppContext* getContext() const { return m_context; }

    /// Name of an icon of the design for the layout direction: Qt does not mirror
    /// icons, so a directional icon (chevron, arrow) is replaced by its mirror in
    /// right-to-left (chevron-left <-> chevron-right, chevron-first <-> chevron-last,
    /// arrow-left <-> arrow-right); other icons are returned unchanged
    static QString getDirectionalIconName(const QString& iconName, Qt::LayoutDirection direction);

Q_SIGNALS:
    /// The page asks for a route (only the navigator shows routes)
    void navigationRequested(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode);

    /// The page asks for the previous entry of the history of its tab
    void backRequested();

    /// The page asks to open an external address (https only, by the system)
    void externalUrlRequested(const QUrl& url);

    /// Title or header of the page has changed
    void titleChanged();

    /// The page has replaced its route (replaceRoute): the navigator puts the
    /// route into the current entry of the history of the tab of the page
    void routeReplaced(const pdf::PDFAppRoute& route);

    /// Emitted by the destructor of the base, before the children of the page are
    /// deleted (the derived parts do not exist any more): a widget placed into the
    /// page by another object (the storage banner) is taken out. A slot must not
    /// call the page.
    void aboutToBeDestroyed();

protected:
    /// The hook of PDFDeferredDeletion::retire (step 4 of retire, PDFRetirable): the page ends what it has outside
    /// its subtree - popups parented to the window (setOwner), pending values written (setFlushOnRetire). It never
    /// moves an object out of its subtree: what the tab lent to the page (the storage banner) is taken back by the tab
    /// before the page is retired (PDFAppTab::setPage -> onPageChanged); an object moved out by the hook would stay
    /// whole and would never be deleted. It emits none of the page's signals, opens no dialog and waits for nothing.
    /// The base has nothing outside its subtree; an override calls BaseClass::retire() first.
    virtual void retire() override;

    /// Emits navigationRequested. The page may be closed by the navigation (its tab closed - retired, step A) or
    /// deleted by a foreign receiver: \returns false, if the page has been closed or deleted (change nothing
    /// outside the page, touch no member after a deletion)
    bool requestNavigation(const PDFAppRoute& route, PDFAppNavigationMode mode = PDFAppNavigationMode::Push);

    /// Replaces the route of the shown page without showing it again: a filter, a
    /// sort or a search text of the page, which is a part of its route (Recent,
    /// Support, the groups of the search). The page stays (the focus stays in its
    /// field), getRoute returns the new route and the navigator replaces the current
    /// entry of the history of the tab, so Back, Forward and a copied link give the
    /// filtered page; the window title follows, there is no announcement of a new
    /// page. The route must be a route of the same page (the same pattern of the
    /// route table) - the navigator refuses another one and keeps the history; an
    /// invalid route is ignored.
    /// \returns false, if the page has been closed (retired, step A) or deleted (as requestNavigation)
    bool replaceRoute(const PDFAppRoute& route);

    /// The page is the shown page of its tab and the entry of the history before
    /// the current one has the route \p route: a filter of the page cleared to
    /// that route goes Back (requestBack) instead of replacing or pushing its
    /// route, so the history never has two equal entries next to each other
    /// (owner decision O25, the Tools and Settings pages)
    bool isPreviousRoute(const PDFAppRoute& route) const;

    /// Emits backRequested (the tab shows the previous entry of its history and
    /// retires this page). \returns false, if the page has been closed (retired, step A) or deleted
    bool requestBack();

private:
    QPointer<PDFAppContext> m_context;
    PDFAppRoute m_route;
};

/// Factory of a page: one class per page (no lambdas). Factories are registered
/// in PDFAppRouteTable for the patterns of their routes.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageFactory
{
public:
    PDFAppPageFactory() = default;
    virtual ~PDFAppPageFactory();

    PDFAppPageFactory(const PDFAppPageFactory&) = delete;
    PDFAppPageFactory& operator=(const PDFAppPageFactory&) = delete;

    /// Creates the page (it is shown by setRoute afterwards); nullptr = Page Not Found
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const = 0;

    /// Creates the page of the route (the navigator calls this function); the
    /// default calls createPage. A factory, whose page depends on the route (the
    /// tool pages of PDFAppToolPageRegistry, step 6), overrides it; nullptr = Page
    /// Not Found.
    virtual PDFAppPage* createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const;

    /// The page shown for \p pageRoute is also the page of \p route (both routes
    /// match the entry of this factory): PDFAppNavigator refuses PDFAppPage::replaceRoute
    /// to a route of another page. The default is true; a factory, whose page depends
    /// on the route (createPageForRoute), compares the parameters, which choose the page.
    virtual bool isSamePage(const PDFAppRoute& pageRoute, const PDFAppRoute& route) const;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppNavigationMode)

#endif // PDFAPPPAGE_H
