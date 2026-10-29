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

#include "pdfappdocumentpage.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"

namespace pdf
{

PDFAppDocumentPage::PDFAppDocumentPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("documentPage"));

    // The navigator creates the page with its tab as the parent
    for (QObject* object = parent; object; object = object->parent())
    {
        if (PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(object))
        {
            m_tab = tab;
            break;
        }
    }

    m_routeTimer = new QTimer(this);
    m_routeTimer->setSingleShot(true);
    m_routeTimer->setInterval(ROUTE_DELAY);
    connect(m_routeTimer.data(), &QTimer::timeout, this, &PDFAppDocumentPage::updateRoute);

    if (m_tab)
    {
        if (PDFAppDocument* document = m_tab->getDocument())
        {
            connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentPage::onDocumentStateChanged);
        }
        if (PDFAppDocumentView* view = m_tab->getView())
        {
            connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppDocumentPage::onViewChanged);
            connect(view, &PDFAppDocumentView::panelChanged, this, &PDFAppDocumentPage::onViewChanged);
        }
        if (PDFAppDocumentSearchCommandHandler* search = PDFAppDocumentSearchCommandHandler::findHandler(m_tab))
        {
            connect(search, &PDFAppDocumentSearchCommandHandler::searchTextChanged, this, &PDFAppDocumentPage::onViewChanged);
        }
    }
}

PDFAppDocumentPage::~PDFAppDocumentPage() = default;

bool PDFAppDocumentPage::setRoute(const PDFAppRoute& route)
{
    // Only the route of the document of the tab (the segments are validated by
    // the route table: document / {documentId})
    const QStringList& segments = route.getSegments();
    if (!m_tab || segments.size() != 2 || segments[1] != m_tab->getDocumentId())
    {
        return false;
    }

    if (!BaseClass::setRoute(route))
    {
        return false;
    }

    // The document, which is not shown yet, gets the route when it is shown
    PDFAppDocument* document = m_tab->getDocument();
    m_isRoutePending = !document || !document->isReady();
    if (!m_isRoutePending)
    {
        applyRoute();
    }
    return true;
}

void PDFAppDocumentPage::applyRoute()
{
    PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr;
    PDFAppDocument* document = m_tab ? m_tab->getDocument() : nullptr;
    if (!view || !document)
    {
        return;
    }

    const PDFAppRoute route = getRoute();
    QPointer<PDFAppDocumentPage> guard(this);
    m_isApplying = true;

    // ?page= is a physical page from 1; a page out of range is ignored
    bool ok = false;
    const qlonglong page = route.getQueryValue(QStringLiteral("page")).toLongLong(&ok);
    if (ok && page >= 1 && page <= document->getPageCount())
    {
        view->goToPage(PDFInteger(page - 1));
    }

    // ?panel= shows the panel of the sidebar (an unknown panel is ignored)
    const QString panel = route.getQueryValue(QStringLiteral("panel"));
    if (guard && m_tab && !panel.isEmpty())
    {
        view->showPanel(panel);
    }

    // ?q= - PACKAGE D: the search of the text in the document
    const QString query = route.getQueryValue(QStringLiteral("q"));
    if (guard && m_tab && !query.isEmpty())
    {
        PDFAppDocumentSearchCommandHandler::applyRouteQuery(m_tab, query);
    }

    if (guard)
    {
        m_isApplying = false;

        // The route is written again only after a change of the user
        if (m_routeTimer)
        {
            m_routeTimer->stop();
        }
    }
}

void PDFAppDocumentPage::onDocumentStateChanged()
{
    PDFAppDocument* document = m_tab ? m_tab->getDocument() : nullptr;
    if (m_isRoutePending && document && document->isReady())
    {
        m_isRoutePending = false;
        applyRoute();
    }
}

void PDFAppDocumentPage::onViewChanged()
{
    if (m_isApplying || m_isRoutePending || !m_routeTimer)
    {
        return;
    }
    m_routeTimer->start();
}

PDFAppRoute PDFAppDocumentPage::getCurrentRoute() const
{
    PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr;
    PDFAppDocument* document = m_tab ? m_tab->getDocument() : nullptr;
    if (!view || !document || !document->isReady())
    {
        return getRoute();
    }

    QList<PDFAppRoute::QueryItem> query;
    const PDFInteger pageIndex = view->getDisplayedPageIndex();
    if (pageIndex >= 0 && pageIndex < document->getPageCount())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("page"), QString::number(pageIndex + 1)));
    }

    const QString panel = view->getCurrentPanelId();
    if (!panel.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("panel"), panel));
    }

    const QString search = PDFAppDocumentSearchCommandHandler::getRouteQuery(m_tab);
    if (!search.isEmpty())
    {
        query.append(PDFAppRoute::QueryItem(QStringLiteral("q"), search));
    }

    return PDFAppRoute::create({ QStringLiteral("document"), m_tab->getDocumentId() }, query);
}

void PDFAppDocumentPage::updateRoute()
{
    if (m_routeTimer)
    {
        m_routeTimer->stop();
    }
    if (m_isRoutePending || m_isApplying || !m_tab)
    {
        return;
    }

    // A route, which the route table refuses (a search text too long), is not written
    const PDFAppRoute route = getCurrentRoute();
    if (route.isValid() && route != getRoute())
    {
        replaceRoute(route);
    }
}

QString PDFAppDocumentPage::getTitle() const
{
    PDFAppDocument* document = m_tab ? m_tab->getDocument() : nullptr;
    return document ? document->getTitle() : tr("Document");
}

bool PDFAppDocumentPage::hasSearchHeader() const
{
    return false;
}

PDFAppPage* PDFAppDocumentPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppDocumentPage(context, parent);
}

PDFAppDocumentTab* PDFAppDocumentPage::getDocumentTab() const
{
    return m_tab.data();
}

}   // namespace pdf
