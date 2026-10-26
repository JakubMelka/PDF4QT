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

#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

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

/// Base of a page of the application (newapp_navigation.md 2). The navigator
/// creates the page, when its route is shown, and destroys it, when the tab
/// shows another route; a page reads its data from the database and keeps
/// nothing else. A page never calls another page: it asks for a route by the
/// signal navigationRequested (the navigator may delete the page then - the
/// page must not touch itself after the emission, see requestNavigation).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPage : public QWidget
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
    /// Emits navigationRequested. The page may be deleted by the navigation:
    /// \returns false, if the page does not exist any more (do not touch members then)
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
    /// \returns false, if the page does not exist any more (do not touch members then)
    bool replaceRoute(const PDFAppRoute& route);

    /// The page is the shown page of its tab and the entry of the history before
    /// the current one has the route \p route: a filter of the page cleared to
    /// that route goes Back (requestBack) instead of replacing or pushing its
    /// route, so the history never has two equal entries next to each other
    /// (owner decision O25, the Tools and Settings pages)
    bool isPreviousRoute(const PDFAppRoute& route) const;

    /// Emits backRequested (the tab shows the previous entry of its history and
    /// deletes this page). \returns false, if the page does not exist any more
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
