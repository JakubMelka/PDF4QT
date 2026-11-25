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
// until the package implements it. Real code of the skeleton: the constructor of
// PDFContentResolvedState from a state of the processor.

#include "pdfcontentstate.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentResolvedState::PDFContentResolvedState(const PDFPageContentProcessorState& state) :
    m_state(state),
    m_hasSoftMask(state.getSoftMask() != nullptr)
{
    // The soft mask dictionary of the processor state is a raw pointer into the document: the
    // resolved state must not point there (the mask is an owned object of the symbolic state)
    m_state.setSoftMask(nullptr);
}

const PDFContentPropertySource& PDFContentSymbolicState::getSource(PDFContentStateProperty property) const
{
    Q_UNUSED(property);
    qFatal("PDFContentSymbolicState::getSource is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentSymbolicState PDFContentSymbolicState::withSource(PDFContentStateProperty property, PDFContentPropertySource source) const
{
    Q_UNUSED(property);
    Q_UNUSED(source);
    qFatal("PDFContentSymbolicState::withSource is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

PDFContentSymbolicState PDFContentSymbolicState::withSoftMaskMatrix(const QTransform& matrix) const
{
    Q_UNUSED(matrix);
    qFatal("PDFContentSymbolicState::withSoftMaskMatrix is not implemented (stage E0 skeleton, package P02)");
    std::abort();
}

}   // namespace pdf
