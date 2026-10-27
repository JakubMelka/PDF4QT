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


#ifndef PDFAPPMCPRESOURCES_H
#define PDFAPPMCPRESOURCES_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"
#include "pdfmcpcatalog.h"

#include <memory>
#include <vector>

namespace pdf
{

class PDFAppMcpHost;

/// Resources of the server (plan 4.1, 4.2). The static files (docs, skills,
/// workflows, help) are OWNED BY PACKAGE D (resources/mcp, mcp.qrc), the
/// document resources BY PACKAGE B. WAVE 0 STUB: no resources, no templates,
/// every read of a document resource is "not found".
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpResources
{
public:
    PDFAppMcpResources() = delete;

    /// Instructions of server/discover and initialize (plan 4.4; the first 512
    /// characters are self-contained)
    static QString getInstructions();

    /// Static resources with their contents (package D)
    static std::vector<PDFMcpResourceDefinition> createStaticResources();

    /// Listed resources of the open documents: info, outline, annotations (package B)
    static std::vector<PDFMcpResourceDefinition> createDocumentResources(const PDFAppMcpHost* host);

    /// Templates of the resources (packages B and D)
    static std::vector<PDFMcpResourceTemplateDefinition> createTemplates();

    /// resources/read of a document resource (package B): -32602 with data.uri
    /// in the modern era, -32002 in the legacy eras for an unknown URI
    static void readDocumentResource(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call);

    /// The error of a resource, which does not exist, in the era of the call
    static PDFMcpCallResult createNotFound(const PDFMcpCall& call);
};

}   // namespace pdf

#endif // PDFAPPMCPRESOURCES_H
