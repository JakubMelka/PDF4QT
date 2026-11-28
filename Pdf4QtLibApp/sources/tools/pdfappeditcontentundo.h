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

#ifndef PDFAPPEDITCONTENTUNDO_H
#define PDFAPPEDITCONTENTUNDO_H

#include "pdfappglobal.h"
#include "pdfapptoolundotarget.h"

#include <QPointer>

namespace pdf
{

class PDFAppEditContentContext;

// Edit Content, stage E1, package P14 (GUI of the tool). Plan: zadani.md KE.2, RE-16;
// zadani_gui.md 4.14 ("the Undo target must exist before enterMode"), 4.21, 4.27.

/// Undo and Redo of Edit Content: the undo stack of the session, one for the whole tool over all
/// pages (PDFDocumentToolGUI::getOwnUndoTarget gives it to the shell, which reads it once when
/// the tool is entered).
///
/// - undo / redo: the pending value of a card is committed first (PDFAppEditContentContext::
///   commitPendingEdits), then PDFContentEditSession::undo / redo; a step of another page asks
///   for that page first (the rules of zadani_gui.md 4.27: typing ends, the group is left) and
///   the nodes of the step are selected afterwards (PDFContentEditSession::
///   selectionChangeRequested).
/// - canUndo / canRedo are false while the editing is not allowed (Preview, a run).
/// - The texts of the steps are PDFContentUndoStack::getUndoText / getRedoText.
/// - undoStateChanged follows PDFContentEditSession::modelChanged and
///   PDFAppEditContentContext::editingAllowedChanged; emitted last.
/// - Lock and "hidden in the editor" are no steps (RE-16).
///
/// Threads and ownership: a child of the GUI of the tool, created before the shell enters the
/// mode and alive as long as the GUI (the view of the document holds it by a QPointer and gives
/// it up when the mode ends, PDFAppToolModeToken). GUI thread. The slots of undo / redo may close
/// the tool: nothing is touched after them unless the object is not retired.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentUndoTarget : public PDFAppToolUndoTarget
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolUndoTarget;

public:
    explicit PDFAppEditContentUndoTarget(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentUndoTarget() override;

    virtual bool canUndo() const override;
    virtual bool canRedo() const override;
    virtual void undo() override;
    virtual void redo() override;
    virtual QString getUndoText() const override;
    virtual QString getRedoText() const override;

private:
    void onStateChanged();

    /// PDFAppEditContentContext::reloadDocument: the steps of the new session (connected again)
    void onSessionReplaced();

    /// Undo and Redo may act now (see the class)
    bool isAvailable() const;

    /// Undo (true) or Redo (false) of one step: the pending values first, the page of the step, then the session
    void apply(bool isUndo);

    QPointer<PDFAppEditContentContext> m_context;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTUNDO_H
