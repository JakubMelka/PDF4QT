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


#ifndef PDFAPPMCPTOOL_RENDER_H
#define PDFAPPMCPTOOL_RENDER_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

namespace pdf
{

/// Handler of the tool "render" (render.*, plan of step 7, 3.4; package B):
/// an image of a page or of its area with the matrices between the pixels and
/// the points, and an overview sheet of pages - in a worker by PDFAppMcpRenderer.
/// Without page and pageLabel the page shown in the tab is rendered.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpRenderTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("render"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_RENDER_H
