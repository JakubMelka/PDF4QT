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

#include "pdfapppage.h"
#include "pdfappcontext.h"
#include "pdfapptab.h"

#include <utility>

namespace pdf
{

PDFAppPage::PDFAppPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{

}

PDFAppPage::~PDFAppPage()
{
    // The page says it is going away while it is still a page (its children exist)
    Q_EMIT aboutToBeDestroyed();
}

void PDFAppPage::retire()
{
    // The base page has nothing outside its subtree (step A, the packages add the hooks of the pages)
}

QBoxLayout* PDFAppPage::getBannerLayout() const
{
    return nullptr;
}

bool PDFAppPage::setRoute(const PDFAppRoute& route)
{
    m_route = route;
    return true;
}

QString PDFAppPage::getHelpTopic() const
{
    return PDFAppRoutes::getDefaultHelpTopic();
}

QString PDFAppPage::getHeaderTitle() const
{
    return QString();
}

QString PDFAppPage::getHeaderSubtitle() const
{
    return QString();
}

QString PDFAppPage::getHeaderSubtitleAccent() const
{
    return QString();
}

QList<PDFAppBreadcrumbItem> PDFAppPage::getHeaderBreadcrumbs() const
{
    return QList<PDFAppBreadcrumbItem>();
}

PDFAppHeaderIcon PDFAppPage::getHeaderIcon() const
{
    return PDFAppHeaderIcon();
}

QString PDFAppPage::getHeaderTag() const
{
    return QString();
}

QString PDFAppPage::getOpenFilesToolId() const
{
    return QString();
}

bool PDFAppPage::hasSearchHeader() const
{
    return true;
}

QVariantMap PDFAppPage::saveViewState() const
{
    return QVariantMap();
}

bool PDFAppPage::hasUnsavedWork() const
{
    return false;
}

quint64 PDFAppPage::getWorkStamp() const
{
    return 0;
}

void PDFAppPage::prepareClose()
{

}

bool PDFAppPage::isClosed() const
{
    if (PDFDeferredDeletion::isRetired(this))
    {
        return true;
    }

    // The replaced page is still a child of its tab (hidden, out of the layout)
    for (const QObject* parent = this->parent(); parent; parent = parent->parent())
    {
        if (const PDFAppTab* tab = qobject_cast<const PDFAppTab*>(parent))
        {
            return tab->isPageReplaced(this);
        }
    }
    return false;
}

bool PDFAppPage::isQuestionOpen() const
{
    return false;
}

bool PDFAppPage::isLeftBy(const PDFAppRoute& target) const
{
    return !(target == m_route);
}

bool PDFAppPage::finishLeave(bool isProceeding)
{
    Q_UNUSED(isProceeding);
    return true;
}

bool PDFAppPage::finishLeaves(const std::vector<QPointer<PDFAppPage>>& pages, bool isProceeding)
{
    // A page applies its choice by a commit: its slots may close the other pages (retired with their tabs, step A -
    // nothing to finish there). A refusal cancels the leave - the other pages are ended by false (each honors its own
    // closing answer from the event loop, owner 2026-11-24).
    bool isFinished = true;
    for (const QPointer<PDFAppPage>& page : pages)
    {
        if (page && !PDFDeferredDeletion::isRetired(page) && !page->finishLeave(isProceeding && isFinished))
        {
            isFinished = false;
        }
    }
    return isFinished;
}

PDFAppLeaveResult PDFAppPage::requestLeave(const PDFAppRoute& target, PDFAppLeaveFlow* flow)
{
    Q_UNUSED(target);
    Q_UNUSED(flow);
    return PDFAppLeaveResult::Done;
}

PDFAppLeaveResult PDFAppPage::requestClose(PDFAppLeaveFlow* flow)
{
    Q_UNUSED(flow);
    return PDFAppLeaveResult::Done;
}

PDFAppLeaveResult PDFAppPage::requestDocumentLock(PDFAppLeaveFlow* flow)
{
    Q_UNUSED(flow);
    return PDFAppLeaveResult::Done;
}

PDFAppLeaveResult PDFAppPage::requestFinishLeave(bool isProceeding, PDFAppLeaveFlow* flow)
{
    Q_UNUSED(flow);
    return finishLeave(isProceeding) ? PDFAppLeaveResult::Done : PDFAppLeaveResult::Refused;
}

void PDFAppPage::restoreViewState(const QVariantMap& state)
{
    Q_UNUSED(state);
}

QString PDFAppPage::getDirectionalIconName(const QString& iconName, Qt::LayoutDirection direction)
{
    if (direction != Qt::RightToLeft)
    {
        return iconName;
    }

    static constexpr std::pair<const char*, const char*> MIRRORED_ICONS[] = {
        { "chevron-left", "chevron-right" },
        { "chevron-first", "chevron-last" },
        { "arrow-left", "arrow-right" },
        { "corner-down-left", "corner-down-right" }
    };

    for (const auto& [first, second] : MIRRORED_ICONS)
    {
        if (iconName == QLatin1String(first))
        {
            return QString::fromLatin1(second);
        }
        if (iconName == QLatin1String(second))
        {
            return QString::fromLatin1(first);
        }
    }
    return iconName;
}

bool PDFAppPage::requestNavigation(const PDFAppRoute& route, PDFAppNavigationMode mode)
{
    // The navigation may close the tab of the page (retired, step A; the replaced page is retired from the event loop);
    // the contract also covers a receiver outside of the application, which deletes the page (the guard stays)
    QPointer<PDFAppPage> guard(this);
    Q_EMIT navigationRequested(route, mode);
    return !guard.isNull() && !PDFDeferredDeletion::isRetired(this);
}

bool PDFAppPage::replaceRoute(const PDFAppRoute& route)
{
    if (!route.isValid())
    {
        qWarning("A page has replaced its route by an invalid route, it is ignored: %s", qPrintable(route.getRequestedText().left(256)));
        return true;
    }

    if (route == m_route)
    {
        return true;
    }

    m_route = route;
    QPointer<PDFAppPage> guard(this);
    Q_EMIT routeReplaced(route);
    return !guard.isNull() && !PDFDeferredDeletion::isRetired(this);
}

bool PDFAppPage::isPreviousRoute(const PDFAppRoute& route) const
{
    // The tab of the page is one of its ancestors (as PDFAppNavigator::findTab)
    const PDFAppTab* tab = nullptr;
    for (const QObject* current = parent(); current && !tab; current = current->parent())
    {
        tab = qobject_cast<const PDFAppTab*>(current);
    }

    // A page, which is being shown or which has been replaced, has no entry
    if (!tab || tab->getPage() != this || !route.isValid())
    {
        return false;
    }

    const PDFAppHistory& history = tab->getHistory();
    const PDFAppHistoryEntry* previous = history.getEntry(history.getCurrentIndex() - 1);
    return previous && previous->route == route;
}

bool PDFAppPage::requestBack()
{
    QPointer<PDFAppPage> guard(this);
    Q_EMIT backRequested();
    return !guard.isNull() && !PDFDeferredDeletion::isRetired(this);
}

PDFAppPageFactory::~PDFAppPageFactory() = default;

PDFAppPage* PDFAppPageFactory::createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const
{
    Q_UNUSED(route);
    return createPage(context, parent);
}

bool PDFAppPageFactory::isSamePage(const PDFAppRoute& pageRoute, const PDFAppRoute& route) const
{
    Q_UNUSED(pageRoute);
    Q_UNUSED(route);
    return true;
}

}   // namespace pdf

#include "moc_pdfapppage.cpp"
