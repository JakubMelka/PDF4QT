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

#ifndef PDFAPPTOOLPAGEREGISTRY_H
#define PDFAPPTOOLPAGEREGISTRY_H

#include "pdfappglobal.h"
#include "pdfapppage.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace pdf
{

/// Route of the page of a tool
enum class PDFAppToolPageKind
{
    Start,      ///< /tools/{toolId}: the introduction page of the tool in the Home tab (276:1737)
    Document    ///< /document/{documentId}/tool/{toolId}: the tool in the tab of the document (291:994)
};

/// Pages of the tools of the catalog (plan of step 6, chapter 3): the hook of the
/// routes /tools/{toolId} and /document/{documentId}/tool/{toolId}. A tool has a
/// page only, when a factory is registered for its id (phase 2 registers the
/// implemented tools; the tests register a test page); otherwise the route ends
/// on Page Not Found ("Tool “Merge”", 940:134845). Only identifiers of the catalog
/// are registered. Process wide; the functions are thread safe.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageRegistry
{
public:
    PDFAppToolPageRegistry() = delete;

    /// Registers the factory of the page of the tool (a factory of the same kind
    /// and tool is replaced). \returns false for an unknown tool or a null factory.
    static bool registerFactory(PDFAppToolPageKind kind, const QString& toolId, std::shared_ptr<const PDFAppPageFactory> factory);

    /// Removes the factory of the tool (nothing, when there is none)
    static void unregisterFactory(PDFAppToolPageKind kind, const QString& toolId);

    /// Factory of the page of the tool, nullptr = the tool has no page
    static std::shared_ptr<const PDFAppPageFactory> getFactory(PDFAppToolPageKind kind, const QString& toolId);

    /// The tool has a page of the kind
    static bool hasPage(PDFAppToolPageKind kind, const QString& toolId);

    /// Tools, which have a page of the kind (in the order of the catalog)
    static QStringList getToolsWithPage(PDFAppToolPageKind kind);

    /// Removes every factory (tests)
    static void clear();
};

/// Factory of the routes of the tool pages (registered by PDFAppPageRegistry for
/// /tools/{toolId} and /document/{documentId}/tool/{toolId}): it creates the page
/// of PDFAppToolPageRegistry for the tool of the route, nullptr (Page Not Found)
/// when the tool has none
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolRoutePageFactory : public PDFAppPageFactory
{
public:
    explicit PDFAppToolRoutePageFactory(PDFAppToolPageKind kind);

    /// The page depends on the route: nullptr
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;

    virtual PDFAppPage* createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const override;

    /// The page of a tool replaces its route only by a route of the same tool
    virtual bool isSamePage(const PDFAppRoute& pageRoute, const PDFAppRoute& route) const override;

    /// Tool id of the route of the kind (the parameter toolId), empty = none
    static QString getToolId(PDFAppToolPageKind kind, const PDFAppRoute& route);

private:
    PDFAppToolPageKind m_kind;
};

}   // namespace pdf

#endif // PDFAPPTOOLPAGEREGISTRY_H
