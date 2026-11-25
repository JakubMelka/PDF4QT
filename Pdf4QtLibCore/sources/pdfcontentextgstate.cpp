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

#include "pdfcontentextgstate.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

bool PDFContentExtGStateWriter::isExtGStateProperty(PDFContentStateProperty property)
{
    Q_UNUSED(property);
    qFatal("PDFContentExtGStateWriter::isExtGStateProperty is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

QByteArray PDFContentExtGStateWriter::getKey(PDFContentStateProperty property)
{
    Q_UNUSED(property);
    qFatal("PDFContentExtGStateWriter::getKey is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFObject PDFContentExtGStateWriter::createDictionary(const std::map<PDFContentStateProperty, PDFContentStateValue>& properties)
{
    Q_UNUSED(properties);
    qFatal("PDFContentExtGStateWriter::createDictionary is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFObject PDFContentExtGStateWriter::createSoftMaskDictionary(const PDFObject& softMask)
{
    Q_UNUSED(softMask);
    qFatal("PDFContentExtGStateWriter::createSoftMaskDictionary is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

bool PDFContentExtGStateWriter::isEqual(const PDFObject& left, const PDFObject& right)
{
    Q_UNUSED(left);
    Q_UNUSED(right);
    qFatal("PDFContentExtGStateWriter::isEqual is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

}   // namespace pdf
