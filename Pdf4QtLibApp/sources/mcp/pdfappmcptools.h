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


#ifndef PDFAPPMCPTOOLS_H
#define PDFAPPMCPTOOLS_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

#include <memory>
#include <vector>

namespace pdf
{

/// The handlers of the tools of phase 1 (one file pdfappmcptool_<tool> per
/// tool: B owns application, document, text, render, view; C owns
/// annotations, forms, outline, files)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpTools
{
public:
    PDFAppMcpTools() = delete;

    /// One handler per tool of PDFAppMcpToolSet
    static std::vector<std::unique_ptr<PDFAppMcpToolHandler>> createHandlers();
};

}   // namespace pdf

#endif // PDFAPPMCPTOOLS_H
