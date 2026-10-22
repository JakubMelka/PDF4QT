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

#include "pdfappdocumentpanel.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"

#include <algorithm>

namespace pdf
{

PDFAppDocumentPanel::PDFAppDocumentPanel(QWidget* parent) :
    BaseClass(parent)
{

}

PDFAppDocumentPanel::~PDFAppDocumentPanel() = default;

QString PDFAppDocumentPanel::getSubtitle() const
{
    return QString();
}

void PDFAppDocumentPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    m_document = document;
    m_view = view;
}

bool PDFAppDocumentPanel::hasOwnHeader() const
{
    return false;
}

QWidget* PDFAppDocumentPanel::createHeaderWidget(QWidget* parent)
{
    Q_UNUSED(parent);
    return nullptr;
}

PDFAppDocumentPanelFactory::~PDFAppDocumentPanelFactory() = default;

QString PDFAppDocumentPanelFactory::getTitle() const
{
    return QString();
}

QString PDFAppDocumentPanelFactory::getIconName() const
{
    return QString();
}

bool PDFAppDocumentPanelFactory::isAvailable(const PDFAppDocument* document) const
{
    Q_UNUSED(document);
    return true;
}

bool PDFAppDocumentPanelFactory::hasSidebarTab() const
{
    return true;
}

PDFAppDocumentPanelRegistry PDFAppDocumentPanelRegistry::createDefault()
{
    PDFAppDocumentPanelRegistry registry;
    registerNavigationPanels(&registry);
    registerSearchPanels(&registry);
    return registry;
}

bool PDFAppDocumentPanelRegistry::registerFactory(std::shared_ptr<const PDFAppDocumentPanelFactory> factory)
{
    if (!factory || factory->getId().isEmpty() || findFactory(factory->getId()))
    {
        return false;
    }
    m_factories.push_back(std::move(factory));
    return true;
}

const PDFAppDocumentPanelFactory* PDFAppDocumentPanelRegistry::findFactory(const QString& id) const
{
    for (const std::shared_ptr<const PDFAppDocumentPanelFactory>& factory : m_factories)
    {
        if (factory->getId() == id)
        {
            return factory.get();
        }
    }
    return nullptr;
}

/// Comparator of the factories by their order
class PDFAppDocumentPanelRegistryHelper
{
public:
    PDFAppDocumentPanelRegistryHelper() = delete;

    static bool isBefore(const PDFAppDocumentPanelFactory* left, const PDFAppDocumentPanelFactory* right)
    {
        return left->getOrder() < right->getOrder();
    }
};

std::vector<const PDFAppDocumentPanelFactory*> PDFAppDocumentPanelRegistry::getFactories() const
{
    std::vector<const PDFAppDocumentPanelFactory*> factories;
    factories.reserve(m_factories.size());
    for (const std::shared_ptr<const PDFAppDocumentPanelFactory>& factory : m_factories)
    {
        factories.push_back(factory.get());
    }
    std::stable_sort(factories.begin(), factories.end(), &PDFAppDocumentPanelRegistryHelper::isBefore);
    return factories;
}

PDFAppDocument* PDFAppDocumentPanel::getDocument() const
{
    return m_document.data();
}

PDFAppDocumentView* PDFAppDocumentPanel::getView() const
{
    return m_view.data();
}

}   // namespace pdf
