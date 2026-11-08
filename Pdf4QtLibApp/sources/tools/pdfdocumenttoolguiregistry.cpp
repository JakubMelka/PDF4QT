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

#include "pdfdocumenttoolguiregistry.h"

#include "pdfapptooldocumentpage.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptooltab.h"
#include "pdftoolstartpage.h"

#include "pdfdocumenttoolcatalog.h"

#include <QMutex>
#include <QMutexLocker>

#include <map>
#include <utility>

namespace pdf
{

/// Storage of the registry
class PDFDocumentToolGUIRegistryHelper
{
public:
    PDFDocumentToolGUIRegistryHelper() = delete;

    using Factories = std::map<QString, std::shared_ptr<const PDFDocumentToolGUIFactory>>;

    static QMutex& getMutex()
    {
        static QMutex mutex;
        return mutex;
    }

    /// Factories by the tool id (guarded by getMutex)
    static Factories& getFactories()
    {
        static Factories factories;
        return factories;
    }
};

bool PDFDocumentToolGUIRegistry::registerFactory(const QString& toolId, std::shared_ptr<const PDFDocumentToolGUIFactory> factory)
{
    if (!factory || !PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return false;
    }

    // A replaced factory is released outside of the lock (code of the application)
    std::shared_ptr<const PDFDocumentToolGUIFactory> replacedFactory;
    {
        QMutexLocker locker(&PDFDocumentToolGUIRegistryHelper::getMutex());
        std::shared_ptr<const PDFDocumentToolGUIFactory>& entry = PDFDocumentToolGUIRegistryHelper::getFactories()[toolId];
        replacedFactory = std::move(entry);
        entry = std::move(factory);
    }

    if (!PDFAppToolPageRegistry::hasPage(PDFAppToolPageKind::Document, toolId))
    {
        PDFAppToolPageRegistry::registerFactory(PDFAppToolPageKind::Document, toolId, std::make_shared<PDFAppToolDocumentPageFactory>());
    }
    if (!PDFAppToolPageRegistry::hasPage(PDFAppToolPageKind::Start, toolId))
    {
        PDFAppToolPageRegistry::registerFactory(PDFAppToolPageKind::Start, toolId, std::make_shared<PDFToolStartPageFactory>());
    }

    // A tool with its own tab (plan of step 2 of phase 2, 4.1): /tool/{toolId}/{instanceId}
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    if (tool && tool->hasOwnTab() && !PDFAppToolPageRegistry::hasPage(PDFAppToolPageKind::OwnTab, toolId))
    {
        PDFAppToolPageRegistry::registerFactory(PDFAppToolPageKind::OwnTab, toolId, std::make_shared<PDFAppToolTabPageFactory>());
    }
    return true;
}

void PDFDocumentToolGUIRegistry::unregisterFactory(const QString& toolId)
{
    std::shared_ptr<const PDFDocumentToolGUIFactory> factory;
    {
        QMutexLocker locker(&PDFDocumentToolGUIRegistryHelper::getMutex());
        PDFDocumentToolGUIRegistryHelper::Factories& factories = PDFDocumentToolGUIRegistryHelper::getFactories();
        auto it = factories.find(toolId);
        if (it != factories.end())
        {
            factory = std::move(it->second);
            factories.erase(it);
        }
    }

    // Only the page of the registration, a page registered by another one stays
    const std::shared_ptr<const PDFAppPageFactory> page = PDFAppToolPageRegistry::getFactory(PDFAppToolPageKind::Document, toolId);
    if (dynamic_cast<const PDFAppToolDocumentPageFactory*>(page.get()))
    {
        PDFAppToolPageRegistry::unregisterFactory(PDFAppToolPageKind::Document, toolId);
    }
    const std::shared_ptr<const PDFAppPageFactory> startPage = PDFAppToolPageRegistry::getFactory(PDFAppToolPageKind::Start, toolId);
    if (dynamic_cast<const PDFToolStartPageFactory*>(startPage.get()))
    {
        PDFAppToolPageRegistry::unregisterFactory(PDFAppToolPageKind::Start, toolId);
    }
    const std::shared_ptr<const PDFAppPageFactory> ownTabPage = PDFAppToolPageRegistry::getFactory(PDFAppToolPageKind::OwnTab, toolId);
    if (dynamic_cast<const PDFAppToolTabPageFactory*>(ownTabPage.get()))
    {
        PDFAppToolPageRegistry::unregisterFactory(PDFAppToolPageKind::OwnTab, toolId);
    }
}

std::shared_ptr<const PDFDocumentToolGUIFactory> PDFDocumentToolGUIRegistry::getFactory(const QString& toolId)
{
    QMutexLocker locker(&PDFDocumentToolGUIRegistryHelper::getMutex());
    const PDFDocumentToolGUIRegistryHelper::Factories& factories = PDFDocumentToolGUIRegistryHelper::getFactories();
    auto it = factories.find(toolId);
    return it != factories.end() ? it->second : nullptr;
}

QStringList PDFDocumentToolGUIRegistry::getTools()
{
    QStringList tools;
    for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
    {
        if (getFactory(tool.getId()))
        {
            tools << tool.getId();
        }
    }
    return tools;
}

}   // namespace pdf
