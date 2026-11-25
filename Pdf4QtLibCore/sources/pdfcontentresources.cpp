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

#include "pdfcontentresources.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentResources PDFContentResources::read(const PDFDocument* document, const PDFObject& resources, const PDFContentOperationList& operations)
{
    Q_UNUSED(document);
    Q_UNUSED(resources);
    Q_UNUSED(operations);
    qFatal("PDFContentResources::read is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

const std::vector<PDFContentResources::Entry>& PDFContentResources::getEntries(PDFContentResourceCategory category) const
{
    Q_UNUSED(category);
    qFatal("PDFContentResources::getEntries is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFObject PDFContentResources::getObject(PDFContentResourceCategory category, const QByteArray& name) const
{
    Q_UNUSED(category);
    Q_UNUSED(name);
    qFatal("PDFContentResources::getObject is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

QByteArray PDFContentResources::getName(PDFContentResourceCategory category, const PDFObject& object) const
{
    Q_UNUSED(category);
    Q_UNUSED(object);
    qFatal("PDFContentResources::getName is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

const std::set<QByteArray>& PDFContentResources::getUndefinedNames(PDFContentResourceCategory category) const
{
    Q_UNUSED(category);
    qFatal("PDFContentResources::getUndefinedNames is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

QByteArray PDFContentResources::allocateName(PDFContentResourceCategory category, const QByteArray& prefix) const
{
    Q_UNUSED(category);
    Q_UNUSED(prefix);
    qFatal("PDFContentResources::allocateName is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

QByteArray PDFContentResources::addObject(PDFContentResourceCategory category, const QByteArray& prefix, const PDFObject& object)
{
    Q_UNUSED(category);
    Q_UNUSED(prefix);
    Q_UNUSED(object);
    qFatal("PDFContentResources::addObject is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

bool PDFContentResources::removeAdded(PDFContentResourceCategory category, const QByteArray& name)
{
    Q_UNUSED(category);
    Q_UNUSED(name);
    qFatal("PDFContentResources::removeAdded is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

bool PDFContentResources::setObject(PDFContentResourceCategory category, const QByteArray& name, const PDFObject& object)
{
    Q_UNUSED(category);
    Q_UNUSED(name);
    Q_UNUSED(object);
    qFatal("PDFContentResources::setObject is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

bool PDFContentResources::isChanged() const
{
    qFatal("PDFContentResources::isChanged is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

QByteArray PDFContentResources::getCategoryKey(PDFContentResourceCategory category)
{
    Q_UNUSED(category);
    qFatal("PDFContentResources::getCategoryKey is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

std::vector<PDFContentResourceCategory> PDFContentResources::getCategories()
{
    qFatal("PDFContentResources::getCategories is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

PDFObject PDFContentResources::write(PDFDocumentBuilder* builder) const
{
    Q_UNUSED(builder);
    qFatal("PDFContentResources::write is not implemented (stage E0 skeleton, package P01)");
    std::abort();
}

}   // namespace pdf
