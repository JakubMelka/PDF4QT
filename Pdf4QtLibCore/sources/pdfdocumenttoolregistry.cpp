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

#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolcatalog.h"

#include <QMutex>
#include <QMutexLocker>

#include <atomic>
#include <map>

namespace pdf
{

/// Storage of the registry: tools by their index in the catalog
class PDFDocumentToolRegistryStorage
{
public:
    QMutex mutex;
    std::map<int, std::shared_ptr<const PDFDocumentTool>> tools;
    std::atomic<quint64> generation = 0;

    static PDFDocumentToolRegistryStorage& getInstance()
    {
        static PDFDocumentToolRegistryStorage storage;
        return storage;
    }

    /// Removes the tool of the index and returns it, so the caller releases it
    /// outside of the lock (its destructor is its own code)
    std::shared_ptr<const PDFDocumentTool> take(int index)
    {
        QMutexLocker locker(&mutex);
        auto it = tools.find(index);
        if (it == tools.end())
        {
            return nullptr;
        }

        std::shared_ptr<const PDFDocumentTool> tool = std::move(it->second);
        tools.erase(it);
        ++generation;
        return tool;
    }
};

PDFDocumentToolRegistration PDFDocumentToolRegistry::registerTool(std::shared_ptr<const PDFDocumentTool> tool)
{
    if (!tool)
    {
        return PDFDocumentToolRegistration::NullTool;
    }

    // The tool is asked outside of the lock (its own code)
    const int index = PDFDocumentToolCatalog::getToolIndex(tool->getId());
    if (index < 0)
    {
        return PDFDocumentToolRegistration::UnknownTool;
    }

    if (!PDFDocumentToolParameters::isValid(tool->getParameters()))
    {
        return PDFDocumentToolRegistration::InvalidParameters;
    }

    PDFDocumentToolRegistryStorage& storage = PDFDocumentToolRegistryStorage::getInstance();
    QMutexLocker locker(&storage.mutex);
    if (storage.tools.count(index))
    {
        return PDFDocumentToolRegistration::Duplicate;
    }
    storage.tools.emplace(index, std::move(tool));
    ++storage.generation;
    return PDFDocumentToolRegistration::Registered;
}

bool PDFDocumentToolRegistry::unregisterTool(QStringView id)
{
    const int index = PDFDocumentToolCatalog::getToolIndex(id);
    if (index < 0)
    {
        return false;
    }

    return PDFDocumentToolRegistryStorage::getInstance().take(index) != nullptr;
}

std::shared_ptr<const PDFDocumentTool> PDFDocumentToolRegistry::getTool(QStringView id)
{
    const int index = PDFDocumentToolCatalog::getToolIndex(id);
    if (index < 0)
    {
        return nullptr;
    }

    PDFDocumentToolRegistryStorage& storage = PDFDocumentToolRegistryStorage::getInstance();
    QMutexLocker locker(&storage.mutex);
    auto it = storage.tools.find(index);
    return it != storage.tools.end() ? it->second : nullptr;
}

bool PDFDocumentToolRegistry::isImplemented(QStringView id)
{
    return getTool(id) != nullptr;
}

QStringList PDFDocumentToolRegistry::getImplementedTools()
{
    std::vector<int> indices;
    {
        PDFDocumentToolRegistryStorage& storage = PDFDocumentToolRegistryStorage::getInstance();
        QMutexLocker locker(&storage.mutex);
        for (const auto& item : storage.tools)
        {
            indices.push_back(item.first);
        }
    }

    QStringList ids;
    const std::vector<PDFDocumentToolCatalogEntry>& tools = PDFDocumentToolCatalog::getTools();
    for (const int index : indices)
    {
        ids << tools[size_t(index)].getId();
    }
    return ids;
}

void PDFDocumentToolRegistry::clearForTests()
{
    std::map<int, std::shared_ptr<const PDFDocumentTool>> tools;
    {
        PDFDocumentToolRegistryStorage& storage = PDFDocumentToolRegistryStorage::getInstance();
        QMutexLocker locker(&storage.mutex);
        tools.swap(storage.tools);
        ++storage.generation;
    }
}

quint64 PDFDocumentToolRegistry::getGeneration()
{
    return PDFDocumentToolRegistryStorage::getInstance().generation.load();
}

}   // namespace pdf
