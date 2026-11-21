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

#ifndef PDFAPPTOOLOWNTABQUESTION_H
#define PDFAPPTOOLOWNTABQUESTION_H

#include "pdfappglobal.h"
#include "pdfdocumenttoolgui.h"

namespace pdf
{

/// The question of the own tab of a tool before its tool closes with work of the user (K16; owner 2026-11-20,
/// decision 5 of P2ESCFIX): the pattern of the questions of a tool in the tab of a document (435:10500: Keep as the
/// Primary button, Discard with the icon trash), "Discard the list of 4 files?" with "Keep Merging" and "Discard
/// List". A GUI of a tool with its own tab implements it beside PDFDocumentToolGUI; PDFAppToolTabPage asks it,
/// while PDFAppToolTab::getLossText is not empty (the close of the tab, the quit, another route of the tab).
/// The texts are made by PDFAppToolTabPage::createListCloseQuestion / createChoiceCloseQuestion.
class IPDFAppToolOwnTabQuestion
{
public:
    virtual ~IPDFAppToolOwnTabQuestion() = default;

    /// \param isTabClosing true = the tab closes (the close of the tab, of the window); false = only the tool
    ///        closes, the tab shows another page (another route: "Closing the tool removes …", no wording of the tab)
    virtual PDFAppToolCloseQuestion getOwnTabCloseQuestion(bool isTabClosing) const = 0;
};

}   // namespace pdf

#endif // PDFAPPTOOLOWNTABQUESTION_H
