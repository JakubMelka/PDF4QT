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

#ifndef PDFAPPDOCUMENTMODELCOMMANDS_H
#define PDFAPPDOCUMENTMODELCOMMANDS_H

#include "pdfappdocumentcommands.h"

#include <QPointer>

namespace pdf
{
class PDFAppPage;

/// The result of a step of a leave (pdfapppage.h)
enum class PDFAppLeaveResult;

/// Commands of package A (model): Undo, Redo, Lock document, Edit a copy
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentModelCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppDocumentModelCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentModelCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;
    virtual bool isChecked(const QString& commandId) const override;

private:
    /// The steps of the lock in the flow of the tab (PDFAppLeaveFlow::getStep)
    enum class LockStep
    {
        Page,           ///< The page asks (PDFAppPage::requestDocumentLock)
        FinishPages     ///< The end of the leave of the page (its choice, its question about the rest)
    };

    /// Lock document (A0, owner 2026-11-20 decision 6): the page of a tool, which the lock ends, asks in the flow of
    /// the tab; the lock follows its answer, the end of its leave and then the lock (the last call)
    void requestLock();
    void continueLock();
    void finishLock(bool isProceeding);
    void onLockStepFinished(pdf::PDFAppLeaveResult result);

    QPointer<PDFAppPage> m_lockPage;    ///< The page asked by the lock, whose answer the flow waits for; not owned
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTMODELCOMMANDS_H
