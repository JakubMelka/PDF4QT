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

#ifndef PDFAPPEDITCONTENTCARDS_GEOMETRY_H
#define PDFAPPEDITCONTENTCARDS_GEOMETRY_H

#include "pdfappglobal.h"

namespace pdf
{

class PDFAppEditContentRegistry;

// Edit Content, stage E1, package P13 (cards). Plan: zadani_gui.md 4.19, the rows Position and
// size (X, Y, W, H, Rotation, the flips, Matrix; command Transform) and Arrange (Align 3 x 3,
// Distribute 2 x 2, Match size, Order, Group, Path operations from stage E3; commands Transform,
// Reorder, Group through the actions of PDFAppEditContentActionIds); the frames -transform,
// -multi-selected, -nothing-selected. The geometry of Align / Distribute / Match size is the core
// file pdfcontentarrange.* of the same package. The card classes are private to the .cpp.

/// The entries of this file for the registry: the cards Position and size and Arrange
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentGeometryCards
{
public:
    PDFAppEditContentGeometryCards() = delete;

    static void registerParts(PDFAppEditContentRegistry* registry);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTCARDS_GEOMETRY_H
