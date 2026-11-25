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

// Edit Content, stage E0. This file belongs to package P05 (document writer, tool, JSON). The
// bodies below are stubs of the skeleton PE0SKEL: each one ends the process with the name of its
// function (qFatal), until the package implements it.

#include "pdfcontentdocumentwriter.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentWriteResult PDFContentDocumentWriter::write(const PDFDocument* document,
                                                      const PDFContentEditSnapshot& snapshot,
                                                      PDFDocumentBuilder* builder,
                                                      const Options& options,
                                                      const PDFOperationControl* control,
                                                      PDFProgressSink* progress)
{
    Q_UNUSED(document);
    Q_UNUSED(snapshot);
    Q_UNUSED(builder);
    Q_UNUSED(options);
    Q_UNUSED(control);
    Q_UNUSED(progress);
    qFatal("PDFContentDocumentWriter::write is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

PDFDocumentPointer PDFContentDocumentWriter::writePagePreview(const PDFDocument* document,
                                                              const PDFContentEditSnapshot& snapshot,
                                                              PDFInteger pageIndex,
                                                              const PDFOperationControl* control,
                                                              PDFContentWriteResult* result)
{
    Q_UNUSED(document);
    Q_UNUSED(snapshot);
    Q_UNUSED(pageIndex);
    Q_UNUSED(control);
    Q_UNUSED(result);
    qFatal("PDFContentDocumentWriter::writePagePreview is not implemented (stage E0 skeleton, package P05)");
    std::abort();
}

}   // namespace pdf
