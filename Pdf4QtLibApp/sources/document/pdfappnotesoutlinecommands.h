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

#ifndef PDFAPPNOTESOUTLINECOMMANDS_H
#define PDFAPPNOTESOUTLINECOMMANDS_H

#include "pdfappglobal.h"
#include "pdfappdocumentcommands.h"

#include "pdfaction.h"

#include <vector>

namespace pdf
{

class PDFAppOutlineEditor;

/// Commands of package C of step 4 (PDFAppDocumentPackage::NotesOutline): editing
/// of the outline - new item, rename (F2 in the Outline panel, scope Panel),
/// delete, move up / down, indent / outdent, set the target to the current view,
/// bold and italic. The commands act on the current item of the Outline panel
/// through PDFAppOutlineEditor (one undo step each, flag Outline); they are
/// disabled for a locked document and for a document, which does not allow changes.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNotesOutlineCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppNotesOutlineCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppNotesOutlineCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;
    virtual bool isChecked(const QString& commandId) const override;

    /// Targets of Set Target in the order of the submenu: Whole Page, Page Width,
    /// Page Height (the current view), Area…, Position… (picked on a page), Visible
    /// Content, its width and height (the current view)
    static std::vector<DestinationType> getTargets();

    /// Command of a target (empty for another type)
    static QString getTargetCommandId(DestinationType type);

    /// Target of a command of Set Target (Invalid for another command)
    static DestinationType getTargetType(const QString& commandId);

private:
    PDFAppOutlineEditor* getEditor() const;
};

}   // namespace pdf

#endif // PDFAPPNOTESOUTLINECOMMANDS_H
