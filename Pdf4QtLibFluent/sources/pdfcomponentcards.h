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

#ifndef PDFCOMPONENTCARDS_H
#define PDFCOMPONENTCARDS_H

#include "pdffluentglobal.h"
#include "pdfcomponentbuttons.h"

namespace pdf
{

/// Common base of the tiles and cards of the design (Quick action tile, Folder
/// card, Pinned card, Tool choice, Reading theme card, ...). A card is a button:
/// the whole card activates it, Space and Enter activate it from the keyboard,
/// its title is the text of the button (the accessible name) and its subtitle
/// the accessible description. The focus ring of 2 px lies outside of the card
/// (drawn by the focus frame of the style around the shape of the radius set by
/// \p setFocusRingRadius, 12 by default), so the card keeps its size and its
/// content in each state. Cards of a choice set \p setSelectionBehavior.
/// Nested widgets (star, menu button) are placed at the trailing side by
/// \p addTrailingWidget, by default as a row 12 px from the trailing edge with
/// gaps of 8 px.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFCardButton : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFCardButton(QWidget* parent = nullptr);
};

}   // namespace pdf

#endif // PDFCOMPONENTCARDS_H
