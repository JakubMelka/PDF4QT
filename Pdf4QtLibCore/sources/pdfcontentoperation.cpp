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

// Edit Content, stage E0. This file belongs to package P01 (operations, resources, stream writer,
// ExtGState). The bodies below are stubs of the skeleton PE0SKEL: each one ends the process with
// the name of its function (qFatal), until the package implements it.

#include "pdfcontentoperation.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentOperation PDFContentOperation::createOriginal(PDFContentOperator contentOperator,
                                                        QByteArray rawBytes,
                                                        PDFContentByteRange range,
                                                        Operands operands,
                                                        std::vector<PDFContentResourceName> resourceNames,
                                                        Flags flags)
{
    Q_UNUSED(contentOperator);
    Q_UNUSED(rawBytes);
    Q_UNUSED(range);
    Q_UNUSED(operands);
    Q_UNUSED(resourceNames);
    Q_UNUSED(flags);
    qFatal("PDFContentOperation::createOriginal is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFContentOperation PDFContentOperation::createGenerated(PDFContentOperator contentOperator,
                                                         QByteArray canonicalBytes,
                                                         Operands operands,
                                                         std::vector<PDFContentResourceName> resourceNames)
{
    Q_UNUSED(contentOperator);
    Q_UNUSED(canonicalBytes);
    Q_UNUSED(operands);
    Q_UNUSED(resourceNames);
    qFatal("PDFContentOperation::createGenerated is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFContentOperation PDFContentOperation::createCopy(const PDFContentOperation& source)
{
    Q_UNUSED(source);
    qFatal("PDFContentOperation::createCopy is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFContentOperation PDFContentOperation::withFlags(Flags flags) const
{
    Q_UNUSED(flags);
    qFatal("PDFContentOperation::withFlags is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

size_t PDFContentOperationList::getCount() const
{
    qFatal("PDFContentOperationList::getCount is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

const PDFContentOperation& PDFContentOperationList::getOperation(size_t index) const
{
    Q_UNUSED(index);
    qFatal("PDFContentOperationList::getOperation is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

size_t PDFContentOperationList::getOriginalCount() const
{
    qFatal("PDFContentOperationList::getOriginalCount is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

const QByteArray& PDFContentOperationList::getTail() const
{
    qFatal("PDFContentOperationList::getTail is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

QByteArray PDFContentOperationList::getOriginalContent() const
{
    qFatal("PDFContentOperationList::getOriginalContent is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

size_t PDFContentOperationList::append(PDFContentOperation operation)
{
    Q_UNUSED(operation);
    qFatal("PDFContentOperationList::append is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

void PDFContentOperationList::setFlags(size_t index, PDFContentOperation::Flags flags)
{
    Q_UNUSED(index);
    Q_UNUSED(flags);
    qFatal("PDFContentOperationList::setFlags is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

void PDFContentOperationList::finishOriginal(QByteArray tail)
{
    Q_UNUSED(tail);
    qFatal("PDFContentOperationList::finishOriginal is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

}   // namespace pdf
