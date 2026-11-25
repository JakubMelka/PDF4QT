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

// Edit Content, stage E0. This file belongs to package P02 (tree and state). The bodies below are
// stubs of the skeleton PE0SKEL: each one ends the process with the name of its function (qFatal),
// until the package implements it.

#include "pdfcontenttreebuilder.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentBuildResult PDFContentTreeBuilder::build(const PDFDocument* document,
                                                   const PDFContentContainerKey& key,
                                                   const PDFContentResolvedStatePointer& initialState,
                                                   const PDFContentBuildContext& context,
                                                   const PDFContentBuildOptions& options,
                                                   const PDFOperationControl* control,
                                                   PDFProgressSink* progress)
{
    Q_UNUSED(document);
    Q_UNUSED(key);
    Q_UNUSED(initialState);
    Q_UNUSED(context);
    Q_UNUSED(options);
    Q_UNUSED(control);
    Q_UNUSED(progress);
    qFatal("PDFContentTreeBuilder::build is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

QByteArray PDFContentTreeBuilder::computeFingerprint(const PDFDocument* document, const PDFContentContainerKey& key)
{
    Q_UNUSED(document);
    Q_UNUSED(key);
    qFatal("PDFContentTreeBuilder::computeFingerprint is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

}   // namespace pdf
