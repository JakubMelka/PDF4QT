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


#ifndef PDFAPPMCPTOOL_TEXT_H
#define PDFAPPMCPTOOL_TEXT_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

namespace pdf
{

/// Handler of the tool "text" (text.*, plan of step 7, 3.3; package B): the
/// text of pages with the geometry of lines or words, the search (quads usable
/// by annotations.add) and the text of an area - in a worker by
/// PDFAppMcpTextService (the text index of the document or the layout of a page).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpTextTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("text"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_TEXT_H
