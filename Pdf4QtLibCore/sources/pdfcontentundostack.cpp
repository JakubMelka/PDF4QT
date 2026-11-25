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
// and the destructor (an empty Impl).

#include "pdfcontentundostack.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)
#include <memory>

namespace pdf
{

/// The state of the undo stack: empty in the skeleton, the package fills it (the steps, the
/// position, the clean point, the open macros)
class PDFContentUndoStack::Impl
{
public:
    Impl() = default;
};

PDFContentUndoStack::PDFContentUndoStack() :
    m_impl(std::make_unique<Impl>())
{

}

PDFContentUndoStack::~PDFContentUndoStack() = default;

void PDFContentUndoStack::push(PDFContentCommandPointer command)
{
    Q_UNUSED(command);
    qFatal("PDFContentUndoStack::push is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentUndoStack::beginMacro(const QString& text)
{
    Q_UNUSED(text);
    qFatal("PDFContentUndoStack::beginMacro is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentUndoStack::endMacro()
{
    qFatal("PDFContentUndoStack::endMacro is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentUndoStack::isMacroOpen() const
{
    qFatal("PDFContentUndoStack::isMacroOpen is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentUndoStack::canUndo() const
{
    qFatal("PDFContentUndoStack::canUndo is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentUndoStack::canRedo() const
{
    qFatal("PDFContentUndoStack::canRedo is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

QString PDFContentUndoStack::getUndoText() const
{
    qFatal("PDFContentUndoStack::getUndoText is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

QString PDFContentUndoStack::getRedoText() const
{
    qFatal("PDFContentUndoStack::getRedoText is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

const PDFContentUndoStack::Step* PDFContentUndoStack::getUndoStep() const
{
    qFatal("PDFContentUndoStack::getUndoStep is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

const PDFContentUndoStack::Step* PDFContentUndoStack::getRedoStep() const
{
    qFatal("PDFContentUndoStack::getRedoStep is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentUndoStack::moveToPreviousStep()
{
    qFatal("PDFContentUndoStack::moveToPreviousStep is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentUndoStack::moveToNextStep()
{
    qFatal("PDFContentUndoStack::moveToNextStep is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

size_t PDFContentUndoStack::getCount() const
{
    qFatal("PDFContentUndoStack::getCount is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

size_t PDFContentUndoStack::getPosition() const
{
    qFatal("PDFContentUndoStack::getPosition is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentUndoStack::setClean()
{
    qFatal("PDFContentUndoStack::setClean is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

bool PDFContentUndoStack::isClean() const
{
    qFatal("PDFContentUndoStack::isClean is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

void PDFContentUndoStack::clear()
{
    qFatal("PDFContentUndoStack::clear is not implemented (stage E0 skeleton, package P03)");
    std::abort();
}

}   // namespace pdf
