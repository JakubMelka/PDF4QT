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

// Edit Content, stage E0. This file belongs to package P03 (session, undo, basic commands). The
// bodies below are stubs of the skeleton PE0SKEL: each one ends the process with the name of its
// function (qFatal), until the package implements it. Real code of the skeleton: the constructor
// and the destructor (the session holds the document and the color management and owns its undo
// stack), getDocument and getUndoStack. The skeleton emits no signal.

#include "pdfcontenteditsession.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)
#include <memory>
#include <utility>

namespace pdf
{

/// The state of a session. The skeleton has only what its real functions need; the package adds
/// the containers, the structure model, the requests of the workers and the state of the editor.
///
/// Ownership: owned by its session and destroyed with it; it co-owns the document and the color
/// management (shared pointers) and owns the undo stack, whose commands die with it.
class PDFContentEditSession::Impl
{
public:
    explicit Impl(PDFDocumentPointer sessionDocument, PDFCMSPointer sessionCms) :
        document(std::move(sessionDocument)),
        cms(std::move(sessionCms))
    {

    }

    PDFDocumentPointer document;
    PDFCMSPointer cms;
    PDFContentUndoStack undoStack;
};

PDFContentEditSession::PDFContentEditSession(PDFDocumentPointer document, PDFCMSPointer cms, QObject* parent) :
    QObject(parent),
    m_impl(std::make_unique<Impl>(std::move(document), std::move(cms)))
{

}

PDFContentEditSession::~PDFContentEditSession() = default;

const PDFDocument* PDFContentEditSession::getDocument() const
{
    return m_impl->document.data();
}

PDFContentContainerPointer PDFContentEditSession::getContainer(const PDFContentContainerKey& key) const
{
    Q_UNUSED(key);
    qFatal("PDFContentEditSession::getContainer is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentContainerKey PDFContentEditSession::getPageKey(PDFInteger pageIndex) const
{
    Q_UNUSED(pageIndex);
    qFatal("PDFContentEditSession::getPageKey is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::requestContainer(const PDFContentContainerKey& key)
{
    Q_UNUSED(key);
    qFatal("PDFContentEditSession::requestContainer is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentEditSession::loadContainer(const PDFContentContainerKey& key, const PDFOperationControl* control, QString* errorText)
{
    Q_UNUSED(key);
    Q_UNUSED(control);
    Q_UNUSED(errorText);
    qFatal("PDFContentEditSession::loadContainer is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentEditSession::execute(const PDFContentCommandPointer& command, QString* errorText)
{
    Q_UNUSED(command);
    Q_UNUSED(errorText);
    qFatal("PDFContentEditSession::execute is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

std::optional<PDFInteger> PDFContentEditSession::undo()
{
    qFatal("PDFContentEditSession::undo is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

std::optional<PDFInteger> PDFContentEditSession::redo()
{
    qFatal("PDFContentEditSession::redo is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentEditSession::isExecuting() const
{
    qFatal("PDFContentEditSession::isExecuting is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentUndoStack* PDFContentEditSession::getUndoStack()
{
    return &m_impl->undoStack;
}

const PDFContentUndoStack* PDFContentEditSession::getUndoStack() const
{
    return &m_impl->undoStack;
}

PDFContentStructureModelPointer PDFContentEditSession::getStructure() const
{
    qFatal("PDFContentEditSession::getStructure is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::requestStructure()
{
    qFatal("PDFContentEditSession::requestStructure is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentEditSession::loadStructure(const PDFOperationControl* control)
{
    Q_UNUSED(control);
    qFatal("PDFContentEditSession::loadStructure is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentEditSnapshotPointer PDFContentEditSession::createSnapshot()
{
    qFatal("PDFContentEditSession::createSnapshot is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

std::vector<PDFInteger> PDFContentEditSession::getDirtyPages() const
{
    qFatal("PDFContentEditSession::getDirtyPages is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::setContainer(const PDFContentContainerPointer& container)
{
    Q_UNUSED(container);
    qFatal("PDFContentEditSession::setContainer is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::setStructure(const PDFContentStructureModelPointer& structure)
{
    Q_UNUSED(structure);
    qFatal("PDFContentEditSession::setStructure is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentNodeId PDFContentEditSession::allocateNodeId()
{
    qFatal("PDFContentEditSession::allocateNodeId is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

quint64 PDFContentEditSession::getNodeIdCounter() const
{
    qFatal("PDFContentEditSession::getNodeIdCounter is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::setNodeIdCounter(quint64 counter)
{
    Q_UNUSED(counter);
    qFatal("PDFContentEditSession::setNodeIdCounter is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFObjectReference PDFContentEditSession::addNewObject(const PDFObject& object)
{
    Q_UNUSED(object);
    qFatal("PDFContentEditSession::addNewObject is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFObject PDFContentEditSession::getNewObject(PDFObjectReference reference) const
{
    Q_UNUSED(reference);
    qFatal("PDFContentEditSession::getNewObject is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::setScriptName(const QString& name, const PDFContentNodeReference& node)
{
    Q_UNUSED(name);
    Q_UNUSED(node);
    qFatal("PDFContentEditSession::setScriptName is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

PDFContentNodeReference PDFContentEditSession::getScriptName(const QString& name) const
{
    Q_UNUSED(name);
    qFatal("PDFContentEditSession::getScriptName is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::clearScriptNames()
{
    qFatal("PDFContentEditSession::clearScriptNames is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentEditSession::isNodeLocked(const PDFContentNodeReference& node) const
{
    Q_UNUSED(node);
    qFatal("PDFContentEditSession::isNodeLocked is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::setNodeLocked(const PDFContentNodeReference& node, bool locked)
{
    Q_UNUSED(node);
    Q_UNUSED(locked);
    qFatal("PDFContentEditSession::setNodeLocked is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentEditSession::isNodeHidden(const PDFContentNodeReference& node) const
{
    Q_UNUSED(node);
    qFatal("PDFContentEditSession::isNodeHidden is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::setNodeHidden(const PDFContentNodeReference& node, bool hidden)
{
    Q_UNUSED(node);
    Q_UNUSED(hidden);
    qFatal("PDFContentEditSession::setNodeHidden is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::onContainerBuilt(quint64 requestId)
{
    Q_UNUSED(requestId);
    qFatal("PDFContentEditSession::onContainerBuilt is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentEditSession::onStructureRead(quint64 requestId)
{
    Q_UNUSED(requestId);
    qFatal("PDFContentEditSession::onStructureRead is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

}   // namespace pdf

#include "moc_pdfcontenteditsession.cpp"
