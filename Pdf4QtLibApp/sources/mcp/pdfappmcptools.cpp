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


#include "pdfappmcptools.h"
#include "pdfappmcptool_application.h"
#include "pdfappmcptool_document.h"
#include "pdfappmcptool_text.h"
#include "pdfappmcptool_render.h"
#include "pdfappmcptool_view.h"
#include "pdfappmcptool_annotations.h"
#include "pdfappmcptool_forms.h"
#include "pdfappmcptool_outline.h"
#include "pdfappmcptool_files.h"

namespace pdf
{

std::vector<std::unique_ptr<PDFAppMcpToolHandler>> PDFAppMcpTools::createHandlers()
{
    std::vector<std::unique_ptr<PDFAppMcpToolHandler>> handlers;
    handlers.push_back(std::make_unique<PDFAppMcpApplicationTool>());
    handlers.push_back(std::make_unique<PDFAppMcpDocumentTool>());
    handlers.push_back(std::make_unique<PDFAppMcpTextTool>());
    handlers.push_back(std::make_unique<PDFAppMcpRenderTool>());
    handlers.push_back(std::make_unique<PDFAppMcpViewTool>());
    handlers.push_back(std::make_unique<PDFAppMcpAnnotationsTool>());
    handlers.push_back(std::make_unique<PDFAppMcpFormsTool>());
    handlers.push_back(std::make_unique<PDFAppMcpOutlineTool>());
    handlers.push_back(std::make_unique<PDFAppMcpFilesTool>());
    return handlers;
}

}   // namespace pdf
