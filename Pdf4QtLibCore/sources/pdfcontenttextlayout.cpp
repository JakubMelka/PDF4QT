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

// Edit Content, stage E0. This file belongs to package P06 (text layout, reading only). The bodies
// below are stubs of the skeleton PE0SKEL: each one ends the process with the name of its function
// (qFatal), until the package implements it. Real code of the skeleton: the constructor (the
// matrices are identity and the layout is complete, as the header initializes them).

#include "pdfcontenttextlayout.h"

#include <QtGlobal>

#include <cstdlib>  // std::abort after qFatal: Qt does not declare qFatal noreturn for MSVC (error C4716)

namespace pdf
{

PDFContentTextLayout::PDFContentTextLayout() = default;

void PDFContentTextLayout::beginText()
{
    qFatal("PDFContentTextLayout::beginText is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

void PDFContentTextLayout::moveByOffset(PDFReal tx, PDFReal ty)
{
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    qFatal("PDFContentTextLayout::moveByOffset is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

void PDFContentTextLayout::setMatrix(const QTransform& matrix)
{
    Q_UNUSED(matrix);
    qFatal("PDFContentTextLayout::setMatrix is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

void PDFContentTextLayout::moveByLeading(PDFReal leading)
{
    Q_UNUSED(leading);
    qFatal("PDFContentTextLayout::moveByLeading is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

std::vector<PDFContentTextItem> PDFContentTextLayout::showString(const QByteArray& string, const PDFContentTextState& state)
{
    Q_UNUSED(string);
    Q_UNUSED(state);
    qFatal("PDFContentTextLayout::showString is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

PDFContentTextItem PDFContentTextLayout::applyAdjustment(PDFReal number, const PDFContentTextState& state)
{
    Q_UNUSED(number);
    Q_UNUSED(state);
    qFatal("PDFContentTextLayout::applyAdjustment is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

std::vector<PDFContentTextLayout::Code> PDFContentTextLayout::decodeString(const PDFFont* font, const QByteArray& string)
{
    Q_UNUSED(font);
    Q_UNUSED(string);
    qFatal("PDFContentTextLayout::decodeString is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

std::optional<PDFReal> PDFContentTextLayout::getDictionaryWidth(const PDFFont* font, const Code& code)
{
    Q_UNUSED(font);
    Q_UNUSED(code);
    qFatal("PDFContentTextLayout::getDictionaryWidth is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

bool PDFContentTextLayout::isEditable(const PDFFont* font)
{
    Q_UNUSED(font);
    qFatal("PDFContentTextLayout::isEditable is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

QString PDFContentTextLayout::getUnicode(const PDFFont* font, const Code& code)
{
    Q_UNUSED(font);
    Q_UNUSED(code);
    qFatal("PDFContentTextLayout::getUnicode is not implemented (stage E0 skeleton, package P06)");
    std::abort();
}

}   // namespace pdf
