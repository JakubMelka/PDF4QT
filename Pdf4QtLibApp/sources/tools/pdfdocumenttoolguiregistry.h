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

#ifndef PDFDOCUMENTTOOLGUIREGISTRY_H
#define PDFDOCUMENTTOOLGUIREGISTRY_H

#include "pdfappglobal.h"
#include "pdfdocumenttoolgui.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace pdf
{

/// The GUI of the tools of the catalog (plan of the first batch, chapter 2.1): id of a
/// tool -> the factory of its GUI. A tool has its page in a document tab, when it has a
/// GUI here and an implementation in PDFDocumentToolRegistry; the registration registers
/// the page PDFAppToolDocumentPage in PDFAppToolPageRegistry, unless a page of the tool
/// is registered already (a page of a test). Only identifiers of the catalog are
/// registered. Process wide; the functions are thread safe.
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolGUIRegistry
{
public:
    PDFDocumentToolGUIRegistry() = delete;

    /// Registers the factory (the factory of the same tool is replaced). \returns false
    /// for an unknown tool or a null factory.
    static bool registerFactory(const QString& toolId, std::shared_ptr<const PDFDocumentToolGUIFactory> factory);

    /// Removes the factory and the page registered by \p registerFactory
    static void unregisterFactory(const QString& toolId);

    /// Factory of the GUI of the tool, nullptr = the tool has none
    static std::shared_ptr<const PDFDocumentToolGUIFactory> getFactory(const QString& toolId);

    /// Tools with a GUI (in the order of the catalog)
    static QStringList getTools();
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLGUIREGISTRY_H
