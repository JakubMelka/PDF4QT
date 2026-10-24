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

#ifndef PDFAPPANNOTATECOMMANDS_H
#define PDFAPPANNOTATECOMMANDS_H

#include "pdfappglobal.h"
#include "pdfappdocumentcommands.h"

#include <QPointer>

namespace pdf
{

class PDFAppAnnotationCreator;
class PDFAppMarkResults;

/// Commands of package A of step 4 (PDFAppDocumentPackage::Annotate): the Annotate
/// and Insert panels, the text markups (Ctrl+Shift+H / L / K / Q / M - with a text
/// selection they mark it at once, without one they start the tool), the creation
/// tools of Insert (a command toggles its tool), Delete annotations and Mark as
/// annotations. All of them are disabled for a locked document and for a document
/// without the permission to change annotations (PDFAppAnnotationController::
/// isAnnotationEditingAllowed, O8 / O20).
///
/// The handler owns the creator of annotations of the view of its tab
/// (PDFAppAnnotationCreator) and Mark as annotations (PDFAppMarkResults); they
/// live as long as the tab.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotateCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppAnnotateCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppAnnotateCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;
    virtual bool isChecked(const QString& commandId) const override;

    PDFAppAnnotationCreator* getCreator() const;
    PDFAppMarkResults* getMarkResults() const;

    /// Handler of the tab of the view (nullptr, when there is none)
    static PDFAppAnnotateCommandHandler* findHandler(const PDFAppDocumentView* view);

    /// Creator of annotations of the view (nullptr, when there is none)
    static PDFAppAnnotationCreator* findCreator(const PDFAppDocumentView* view);

private:
    void onCreatorStateChanged();

    QPointer<PDFAppAnnotationCreator> m_creator;
    QPointer<PDFAppMarkResults> m_markResults;
};

}   // namespace pdf

#endif // PDFAPPANNOTATECOMMANDS_H
