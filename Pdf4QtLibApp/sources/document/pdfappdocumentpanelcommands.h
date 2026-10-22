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

#ifndef PDFAPPDOCUMENTPANELCOMMANDS_H
#define PDFAPPDOCUMENTPANELCOMMANDS_H

#include "pdfappdocumentcommands.h"

#include "pdfglobal.h"

#include <vector>

namespace pdf
{

/// Commands of package C (navigation panels): Add bookmark (Ctrl+M), Next /
/// Previous bookmark (Ctrl+. / Ctrl+,), Export / Import bookmarks (personal
/// bookmarks of PDFAppBookmarks, also without the panel) and Rotate pages left /
/// right (the pages selected in the thumbnails, else the current page; one undo
/// step, refused for a locked document).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPanelCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppDocumentPanelCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentPanelCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;

    /// Rotates the pages by 90 degrees (right = clockwise) as one undo step;
    /// false, when the document cannot be changed
    bool rotatePages(const std::vector<PDFInteger>& pages, bool isRight);
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELCOMMANDS_H
