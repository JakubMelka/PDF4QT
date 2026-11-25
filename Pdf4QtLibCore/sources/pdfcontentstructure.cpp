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

// Edit Content, stage E0. This file belongs to package P04 (structure: reading and consistency).
// The bodies below are stubs of the skeleton PE0SKEL: each one ends the process with the name of
// its function (qFatal), until the package implements it.

#include "pdfcontentstructure.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentStructureModelPointer PDFContentStructureModel::read(const PDFDocument* document, const PDFOperationControl* control)
{
    Q_UNUSED(document);
    Q_UNUSED(control);
    qFatal("PDFContentStructureModel::read is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

bool PDFContentStructureModel::isEmpty() const
{
    qFatal("PDFContentStructureModel::isEmpty is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

const std::vector<PDFContentStructureElement>& PDFContentStructureModel::getElements() const
{
    qFatal("PDFContentStructureModel::getElements is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

const PDFContentStructureElement* PDFContentStructureModel::getElement(PDFContentStructureElementId id) const
{
    Q_UNUSED(id);
    qFatal("PDFContentStructureModel::getElement is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

const std::map<PDFInteger, PDFContentParentTreeItem>& PDFContentStructureModel::getParentTree() const
{
    qFatal("PDFContentStructureModel::getParentTree is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFInteger PDFContentStructureModel::getNextParentTreeKey() const
{
    qFatal("PDFContentStructureModel::getNextParentTreeKey is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFContentStructureElementId PDFContentStructureModel::getElementOfMarkedContent(PDFInteger structuralParentKey, PDFInteger mcid) const
{
    Q_UNUSED(structuralParentKey);
    Q_UNUSED(mcid);
    qFatal("PDFContentStructureModel::getElementOfMarkedContent is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFInteger PDFContentStructureModel::allocateMarkedContentId(PDFInteger structuralParentKey, PDFInteger largestUsedInContent) const
{
    Q_UNUSED(structuralParentKey);
    Q_UNUSED(largestUsedInContent);
    qFatal("PDFContentStructureModel::allocateMarkedContentId is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

const std::vector<QString>& PDFContentStructureModel::getIssues() const
{
    qFatal("PDFContentStructureModel::getIssues is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

std::vector<PDFInteger> PDFContentStructureModel::getReadingOrder(PDFInteger structuralParentKey) const
{
    Q_UNUSED(structuralParentKey);
    qFatal("PDFContentStructureModel::getReadingOrder is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFContentStructureChange PDFContentStructureModel::getChangeForDelete(PDFInteger structuralParentKey, PDFInteger mcid, bool isSequenceEmptyAfterwards) const
{
    Q_UNUSED(structuralParentKey);
    Q_UNUSED(mcid);
    Q_UNUSED(isSequenceEmptyAfterwards);
    qFatal("PDFContentStructureModel::getChangeForDelete is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFContentStructureChange PDFContentStructureModel::getChangeForMoveOut(PDFInteger structuralParentKey, PDFInteger mcid, bool isOnlyContent, bool isInMiddle, PDFInteger largestUsedInContent) const
{
    Q_UNUSED(structuralParentKey);
    Q_UNUSED(mcid);
    Q_UNUSED(isOnlyContent);
    Q_UNUSED(isInMiddle);
    Q_UNUSED(largestUsedInContent);
    qFatal("PDFContentStructureModel::getChangeForMoveOut is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFContentStructureChange PDFContentStructureModel::getChangeForReorder(PDFInteger structuralParentKey, PDFInteger mcid, bool isOnlyContent, PDFInteger largestUsedInContent) const
{
    Q_UNUSED(structuralParentKey);
    Q_UNUSED(mcid);
    Q_UNUSED(isOnlyContent);
    Q_UNUSED(largestUsedInContent);
    qFatal("PDFContentStructureModel::getChangeForReorder is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

PDFContentStructureModelPointer PDFContentStructureModel::applied(const std::vector<PDFContentStructureChange>& changes) const
{
    Q_UNUSED(changes);
    qFatal("PDFContentStructureModel::applied is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

bool PDFContentStructureModel::isModified() const
{
    qFatal("PDFContentStructureModel::isModified is not implemented (stage E0 skeleton, package P04)");
    std::abort();
}

}   // namespace pdf
