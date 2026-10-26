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

#include "pdfapptoolpageregistry.h"

#include "pdfdocumenttoolcatalog.h"

#include <QMutex>
#include <QMutexLocker>

#include <map>
#include <utility>

namespace pdf
{

/// Storage of the registry
class PDFAppToolPageRegistryHelper
{
public:
    PDFAppToolPageRegistryHelper() = delete;

    using Key = std::pair<int, QString>;
    using Factories = std::map<Key, std::shared_ptr<const PDFAppPageFactory>>;

    static QMutex& getMutex()
    {
        static QMutex mutex;
        return mutex;
    }

    /// Factories by the kind and the tool id (guarded by getMutex)
    static Factories& getFactories()
    {
        static Factories factories;
        return factories;
    }
};

bool PDFAppToolPageRegistry::registerFactory(PDFAppToolPageKind kind, const QString& toolId, std::shared_ptr<const PDFAppPageFactory> factory)
{
    // The route factory itself would create its page again and again
    if (!factory || !PDFDocumentToolCatalog::isValidToolId(toolId) || dynamic_cast<const PDFAppToolRoutePageFactory*>(factory.get()))
    {
        return false;
    }

    // A replaced factory is released outside of the lock (its destructor is code of
    // the application, which may use the registry)
    std::shared_ptr<const PDFAppPageFactory> replacedFactory;
    {
        QMutexLocker locker(&PDFAppToolPageRegistryHelper::getMutex());
        std::shared_ptr<const PDFAppPageFactory>& entry = PDFAppToolPageRegistryHelper::getFactories()[PDFAppToolPageRegistryHelper::Key(int(kind), toolId)];
        replacedFactory = std::move(entry);
        entry = std::move(factory);
    }
    return true;
}

void PDFAppToolPageRegistry::unregisterFactory(PDFAppToolPageKind kind, const QString& toolId)
{
    // The factory is released outside of the lock (its destructor is code of the application)
    std::shared_ptr<const PDFAppPageFactory> factory;
    {
        QMutexLocker locker(&PDFAppToolPageRegistryHelper::getMutex());
        PDFAppToolPageRegistryHelper::Factories& factories = PDFAppToolPageRegistryHelper::getFactories();
        auto it = factories.find(PDFAppToolPageRegistryHelper::Key(int(kind), toolId));
        if (it != factories.end())
        {
            factory = std::move(it->second);
            factories.erase(it);
        }
    }
}

std::shared_ptr<const PDFAppPageFactory> PDFAppToolPageRegistry::getFactory(PDFAppToolPageKind kind, const QString& toolId)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return nullptr;
    }

    QMutexLocker locker(&PDFAppToolPageRegistryHelper::getMutex());
    const PDFAppToolPageRegistryHelper::Factories& factories = PDFAppToolPageRegistryHelper::getFactories();
    auto it = factories.find(PDFAppToolPageRegistryHelper::Key(int(kind), toolId));
    return it != factories.end() ? it->second : nullptr;
}

bool PDFAppToolPageRegistry::hasPage(PDFAppToolPageKind kind, const QString& toolId)
{
    return getFactory(kind, toolId) != nullptr;
}

QStringList PDFAppToolPageRegistry::getToolsWithPage(PDFAppToolPageKind kind)
{
    QStringList tools;
    for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
    {
        if (hasPage(kind, tool.getId()))
        {
            tools << tool.getId();
        }
    }
    return tools;
}

void PDFAppToolPageRegistry::clear()
{
    PDFAppToolPageRegistryHelper::Factories factories;
    {
        QMutexLocker locker(&PDFAppToolPageRegistryHelper::getMutex());
        factories.swap(PDFAppToolPageRegistryHelper::getFactories());
    }
}

PDFAppToolRoutePageFactory::PDFAppToolRoutePageFactory(PDFAppToolPageKind kind) :
    m_kind(kind)
{

}

PDFAppPage* PDFAppToolRoutePageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    Q_UNUSED(context);
    Q_UNUSED(parent);
    return nullptr;
}

PDFAppPage* PDFAppToolRoutePageFactory::createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const
{
    const std::shared_ptr<const PDFAppPageFactory> factory = PDFAppToolPageRegistry::getFactory(m_kind, getToolId(m_kind, route));
    return factory ? factory->createPageForRoute(context, parent, route) : nullptr;
}

bool PDFAppToolRoutePageFactory::isSamePage(const PDFAppRoute& pageRoute, const PDFAppRoute& route) const
{
    // Both segments of the document kind: the tool and the document of the tab
    const QString toolId = getToolId(m_kind, pageRoute);
    if (toolId.isEmpty() || toolId != getToolId(m_kind, route) || pageRoute.getSegments().mid(0, 2) != route.getSegments().mid(0, 2))
    {
        return false;
    }

    // The registered factory creates the page: its parameters may choose the page too
    const std::shared_ptr<const PDFAppPageFactory> factory = PDFAppToolPageRegistry::getFactory(m_kind, toolId);
    return !factory || factory->isSamePage(pageRoute, route);
}

QString PDFAppToolRoutePageFactory::getToolId(PDFAppToolPageKind kind, const PDFAppRoute& route)
{
    if (!route.isValid())
    {
        return QString();
    }

    const QStringList& segments = route.getSegments();
    switch (kind)
    {
        case PDFAppToolPageKind::Start:
            return segments.size() == 2 && segments[0] == QLatin1String("tools") ? segments[1] : QString();
        case PDFAppToolPageKind::Document:
            return segments.size() == 4 && segments[0] == QLatin1String("document") && segments[2] == QLatin1String("tool") ? segments[3] : QString();
    }
    return QString();
}

}   // namespace pdf
