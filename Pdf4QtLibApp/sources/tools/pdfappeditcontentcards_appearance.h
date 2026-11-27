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

#ifndef PDFAPPEDITCONTENTCARDS_APPEARANCE_H
#define PDFAPPEDITCONTENTCARDS_APPEARANCE_H

#include "pdfappglobal.h"

namespace pdf
{

class PDFAppEditContentRegistry;

// Edit Content, stage E1, package P13 (cards). Plan: zadani_gui.md 4.19, the rows Fill and
// stroke (Device color spaces in stage E1; commands SetAppearance) and Transparency (collapsed
// and the basic rows; command SetTransparency); the frames -path-selected, -fill-and-stroke,
// -color-cmyk, -transparency, -multi-selected. The card classes are private to the .cpp: no
// other package calls them (stage E4, P41, replaces the entry of Fill and stroke by its own with
// the same identifier, PDFAppEditContentRegistry::addCard with isReplacing).

/// The entries of this file for the registry: the cards Fill and stroke and Transparency
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentAppearanceCards
{
public:
    PDFAppEditContentAppearanceCards() = delete;

    static void registerParts(PDFAppEditContentRegistry* registry);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTCARDS_APPEARANCE_H
