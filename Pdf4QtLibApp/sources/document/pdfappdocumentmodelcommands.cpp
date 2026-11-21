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

#include "pdfappdocumentmodelcommands.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapppage.h"
#include "pdfapptoolundotarget.h"
#include "pdfappundostack.h"

#include <QPointer>

namespace pdf
{

PDFAppDocumentModelCommandHandler::PDFAppDocumentModelCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::Model, tab)
{
    // Undo and Redo of an open tool follow its target
    if (PDFAppDocumentView* view = getView())
    {
        connect(view, &PDFAppDocumentView::toolModeChanged, this, &PDFAppDocumentModelCommandHandler::stateChanged);
        connect(view, &PDFAppDocumentView::toolUndoStateChanged, this, &PDFAppDocumentModelCommandHandler::stateChanged);
    }
}

PDFAppDocumentModelCommandHandler::~PDFAppDocumentModelCommandHandler() = default;

void PDFAppDocumentModelCommandHandler::execute(const QString& commandId)
{
    PDFAppDocument* document = getDocument();
    if (!document)
    {
        return;
    }

    // While a tool is open, Undo and Redo act only on the tool, never on the document
    // (owner decision of 2026-11-01); the target may delete the tab
    const PDFAppDocumentView* view = getView();
    if (view && view->isToolModeActive() && (commandId == PDFAppCommands::undo() || commandId == PDFAppCommands::redo()))
    {
        if (PDFAppToolUndoTarget* target = view->getToolUndoTarget())
        {
            if (commandId == PDFAppCommands::undo())
            {
                target->undo();
            }
            else
            {
                target->redo();
            }
        }
        return;
    }

    // Undo and redo may delete the tab (slots of the signals of the document)
    if (commandId == PDFAppCommands::undo())
    {
        document->getUndoStack()->undo();
    }
    else if (commandId == PDFAppCommands::redo())
    {
        document->getUndoStack()->redo();
    }
    else if (commandId == PDFAppCommands::lockDocument())
    {
        // Lock document of the File panel; the lock of the tab opens the popover
        // 266:12435 (Unlock / Edit a Copy), recent_file.locked is written by the tab.
        // A tool, which the lock ends, asks about the unsaved work of the user first (owner
        // 2026-11-20, decision 6): Keep cancels the lock; the question waits in its own loop
        // and anything may be deleted meanwhile
        const bool isLocking = !document->isLocked();
        QPointer<PDFAppDocument> documentGuard(document);
        if (isLocking)
        {
            PDFAppDocumentTab* tab = getTab();
            QPointer<PDFAppPage> page = tab ? tab->getPage() : nullptr;
            if (page && !page->confirmDocumentLock())
            {
                return;
            }

            // The lock goes on, unless the document has gone or has been locked meanwhile: a choice of the question
            // of the tool, which changes the document (Redact "Remove New Marks", Measure "Save and Close"), is
            // applied before the lock while the tool still edits (one Undo step; decision 8). The slots of the
            // commit may delete anything. A tool, which could not keep all of its work (a locked mark), keeps the
            // document unlocked (owner 2026-11-21).
            const bool isProceeding = documentGuard && !documentGuard->isLocked();
            if (!PDFAppPage::finishLeaves({ page }, isProceeding))
            {
                return;
            }
        }
        if (documentGuard && documentGuard->isLocked() != isLocking)
        {
            documentGuard->setLocked(isLocking);
        }
    }
    else if (commandId == PDFAppCommands::editCopy())
    {
        // The window opens the copy in a new tab
        if (PDFAppDocumentTab* tab = getTab())
        {
            Q_EMIT tab->editCopyRequested();
        }
    }
}

bool PDFAppDocumentModelCommandHandler::isEnabled(const QString& commandId) const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return false;
    }

    // An open tool: the state of its target; the document (also a locked one) is not
    // involved. A running operation (the job of the tool) stops the changes of the parameters.
    const PDFAppDocumentView* view = getView();
    if (view && view->isToolModeActive() && (commandId == PDFAppCommands::undo() || commandId == PDFAppCommands::redo()))
    {
        const PDFAppToolUndoTarget* target = view->getToolUndoTarget();
        return target && !document->isBlockedByOperation() && (commandId == PDFAppCommands::undo() ? target->canUndo() : target->canRedo());
    }

    // A locked document cannot be changed: undo and redo are off. Undo of an open
    // editor of a form field commits it and undoes it (step 4, package D).
    if (commandId == PDFAppCommands::undo())
    {
        return !document->isLocked() && !document->isBlockedByOperation() && (document->getUndoStack()->canUndo() || document->hasPendingEdits());
    }
    if (commandId == PDFAppCommands::redo())
    {
        return !document->isLocked() && !document->isBlockedByOperation() && document->getUndoStack()->canRedo();
    }
    if (commandId == PDFAppCommands::editCopy())
    {
        // A copy of a display version of an XFA form (step 8) could not be edited either;
        // a document with encrypted attachments has no copy (isAttachmentsLocked)
        return !document->isPagedXFA() && !document->isAttachmentsLocked();
    }
    if (commandId == PDFAppCommands::lockDocument())
    {
        // Encrypted attachments keep the document locked until their password (Q5.9 c)
        return !document->isLocked() || !document->isAttachmentsLocked();
    }
    return false;
}

bool PDFAppDocumentModelCommandHandler::isChecked(const QString& commandId) const
{
    const PDFAppDocument* document = getDocument();
    return commandId == PDFAppCommands::lockDocument() && document && document->isLocked();
}

}   // namespace pdf

#include "moc_pdfappdocumentmodelcommands.cpp"
