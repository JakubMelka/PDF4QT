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


#ifndef PDFAPPMCPTOOL_VIEW_H
#define PDFAPPMCPTOOL_VIEW_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

namespace pdf
{

/// Handler of the tool "view" (view.*, plan of step 7, 3.5; package B): switch
/// to a tab, go to a page / label / outline item / named destination with a fit
/// or a zoom, point at an area (PDFAppMcpAreaHighlight), open the search panel,
/// navigate like a link and close a tab (refused with unsaved changes, a running
/// operation or a confirmation of another assistant). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpViewTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("view"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_VIEW_H
