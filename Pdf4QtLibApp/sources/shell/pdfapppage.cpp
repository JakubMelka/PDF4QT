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

#include <utility>

namespace pdf
{

PDFAppPage::PDFAppPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{

}

PDFAppPage::~PDFAppPage()
{
    // The page says it is going away while it is still a page (its children exist)
    Q_EMIT aboutToBeDestroyed();
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

bool PDFAppPage::hasSearchHeader() const
{
    return true;
}

QVariantMap PDFAppPage::saveViewState() const
{
    return QVariantMap();
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
    QPointer<PDFAppPage> guard(this);
    Q_EMIT navigationRequested(route, mode);
    return !guard.isNull();
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
    return !guard.isNull();
}

PDFAppPageFactory::~PDFAppPageFactory() = default;

}   // namespace pdf
