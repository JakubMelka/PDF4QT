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
// until the package implements it. Real code of the skeleton: the constructors of PDFContentNode
// and PDFContentContainer (they store their data), the constructors and the destructor of
// PDFContentContainerEditor (an empty Impl).

#include "pdfcontentnode.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)
#include <memory>
#include <utility>

namespace pdf
{

void PDFContentPath::moveTo(QPointF point)
{
    Q_UNUSED(point);
    qFatal("PDFContentPath::moveTo is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

void PDFContentPath::lineTo(QPointF point)
{
    Q_UNUSED(point);
    qFatal("PDFContentPath::lineTo is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

void PDFContentPath::curveTo(QPointF control1, QPointF control2, QPointF point)
{
    Q_UNUSED(control1);
    Q_UNUSED(control2);
    Q_UNUSED(point);
    qFatal("PDFContentPath::curveTo is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

void PDFContentPath::close()
{
    qFatal("PDFContentPath::close is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

void PDFContentPath::addRectangle(const QRectF& rectangle)
{
    Q_UNUSED(rectangle);
    qFatal("PDFContentPath::addRectangle is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentPath::isRectangle(QRectF* rectangle) const
{
    Q_UNUSED(rectangle);
    qFatal("PDFContentPath::isRectangle is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

QRectF PDFContentPath::getBoundingBox() const
{
    qFatal("PDFContentPath::getBoundingBox is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentPath PDFContentPath::transformed(const QTransform& matrix) const
{
    Q_UNUSED(matrix);
    qFatal("PDFContentPath::transformed is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentNode::PDFContentNode(Data data, std::vector<PDFContentNodePointer> children) :
    m_data(std::move(data)),
    m_children(std::move(children))
{

}

bool PDFContentNode::isLeafKind(PDFContentNodeKind kind)
{
    Q_UNUSED(kind);
    qFatal("PDFContentNode::isLeafKind is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentNode::isWrappableKind(PDFContentNodeKind kind)
{
    Q_UNUSED(kind);
    qFatal("PDFContentNode::isWrappableKind is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentContainer::PDFContentContainer(Data data) :
    m_data(std::move(data))
{

}

PDFContentNodePointer PDFContentContainer::getNode(PDFContentNodeId id) const
{
    Q_UNUSED(id);
    qFatal("PDFContentContainer::getNode is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentNodePointer PDFContentContainer::getParent(PDFContentNodeId id) const
{
    Q_UNUSED(id);
    qFatal("PDFContentContainer::getParent is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFInteger PDFContentContainer::getIndexInParent(PDFContentNodeId id) const
{
    Q_UNUSED(id);
    qFatal("PDFContentContainer::getIndexInParent is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentNodeId PDFContentContainer::getMaximumNodeId() const
{
    qFatal("PDFContentContainer::getMaximumNodeId is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

size_t PDFContentContainer::getNodeCount() const
{
    qFatal("PDFContentContainer::getNodeCount is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentContainer::isUnchanged() const
{
    qFatal("PDFContentContainer::isUnchanged is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

/// The state of the editor of a container: empty in the skeleton, the package fills it
class PDFContentContainerEditor::Impl
{
public:
    Impl() = default;
};

PDFContentContainerEditor::PDFContentContainerEditor(const PDFContentContainerKey& key) :
    m_impl(std::make_unique<Impl>())
{
    Q_UNUSED(key);
}

PDFContentContainerEditor::PDFContentContainerEditor(const PDFContentContainerPointer& container) :
    m_impl(std::make_unique<Impl>())
{
    Q_UNUSED(container);
}

PDFContentContainerEditor::~PDFContentContainerEditor() = default;

PDFContentOperationList& PDFContentContainerEditor::getOperations()
{
    qFatal("PDFContentContainerEditor::getOperations is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentResources& PDFContentContainerEditor::getResources()
{
    qFatal("PDFContentContainerEditor::getResources is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentContainer::Data& PDFContentContainerEditor::getContainerData()
{
    qFatal("PDFContentContainerEditor::getContainerData is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentNodePointer PDFContentContainerEditor::getNode(PDFContentNodeId id) const
{
    Q_UNUSED(id);
    qFatal("PDFContentContainerEditor::getNode is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentContainerEditor::insertNode(PDFContentNode::Data data, PDFContentNodeId parentId, PDFInteger index)
{
    Q_UNUSED(data);
    Q_UNUSED(parentId);
    Q_UNUSED(index);
    qFatal("PDFContentContainerEditor::insertNode is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentContainerEditor::setNodeData(PDFContentNodeId id, PDFContentNode::Data data)
{
    Q_UNUSED(id);
    Q_UNUSED(data);
    qFatal("PDFContentContainerEditor::setNodeData is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentContainerEditor::removeNode(PDFContentNodeId id)
{
    Q_UNUSED(id);
    qFatal("PDFContentContainerEditor::removeNode is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

bool PDFContentContainerEditor::moveNode(PDFContentNodeId id, PDFContentNodeId newParentId, PDFInteger index)
{
    Q_UNUSED(id);
    Q_UNUSED(newParentId);
    Q_UNUSED(index);
    qFatal("PDFContentContainerEditor::moveNode is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentContainerPointer PDFContentContainerEditor::build()
{
    qFatal("PDFContentContainerEditor::build is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

}   // namespace pdf
