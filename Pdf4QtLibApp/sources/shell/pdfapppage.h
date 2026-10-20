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

#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

namespace pdf
{

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

    /// The workspace shows its header with the global search (Settings has none)
    virtual bool hasSearchHeader() const;

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

protected:
    /// Emits navigationRequested. The page may be deleted by the navigation:
    /// \returns false, if the page does not exist any more (do not touch members then)
    bool requestNavigation(const PDFAppRoute& route, PDFAppNavigationMode mode = PDFAppNavigationMode::Push);

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
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppNavigationMode)

#endif // PDFAPPPAGE_H
