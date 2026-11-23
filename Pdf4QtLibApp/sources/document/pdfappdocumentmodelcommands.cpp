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
#include "pdfappleaveflow.h"
#include "pdfapppage.h"
#include "pdfapptoolundotarget.h"
#include "pdfappundostack.h"

#include "pdfdeferreddeletion.h"

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
        // 2026-11-20, decision 6): Keep cancels the lock. A0: the question is shown in the
        // flow of the tab and the lock goes on after its answer (onLockPageStepFinished)
        if (!document->isLocked())
        {
            requestLock();
        }
        else
        {
            document->setLocked(false);
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

void PDFAppDocumentModelCommandHandler::requestLock()
{
    QPointer<PDFAppDocumentModelCommandHandler> guard(this);
    PDFAppDocumentTab* tab = getTab();
    PDFAppDocument* document = getDocument();
    QPointer<PDFAppPage> page = tab ? tab->getPage() : nullptr;
    if (!tab || !document)
    {
        return;
    }
    if (!page)
    {
        document->setLocked(true);
        return;
    }

    // One flow of the tab: a lock while a question of the tab is open is refused (the question comes forward)
    PDFAppLeaveFlow* flow = tab->getLeaveFlow();
    if (!flow->start(PDFAppLeaveFlow::Goal::LockDocument))
    {
        flow->raiseQuestion();
        return;
    }
    flow->setSubject(document);
    flow->setStep(int(LockStep::Page));

    // The page asks (its code may close the tab: a retired handler, flow or page locks nothing)
    QPointer<PDFAppLeaveFlow> flowGuard(flow);
    PDFAppLeaveResult result = page->requestDocumentLock(flow);
    if (!guard || !flowGuard || PDFDeferredDeletion::isRetired(this) || PDFDeferredDeletion::isRetired(flow))
    {
        return;
    }
    if (result == PDFAppLeaveResult::Pending)
    {
        if (page && !PDFDeferredDeletion::isRetired(page.data()) && flow->waitForStep(page.data(), this, &PDFAppDocumentModelCommandHandler::onLockStepFinished))
        {
            m_lockPage = page;
            return;
        }
        result = PDFAppLeaveResult::Refused;
    }
    if (result != PDFAppLeaveResult::Done)
    {
        flow->finish(false);
        return;
    }
    flow->addAcceptedPage((page && !PDFDeferredDeletion::isRetired(page.data())) ? page.data() : nullptr);
    continueLock();
}

void PDFAppDocumentModelCommandHandler::continueLock()
{
    QPointer<PDFAppDocumentModelCommandHandler> guard(this);
    PDFAppDocumentTab* tab = getTab();
    PDFAppLeaveFlow* flow = tab ? tab->findLeaveFlow() : nullptr;
    if (!flow || !flow->isRunning())
    {
        return;
    }

    // The lock goes on, unless the document has gone or has been locked meanwhile: a choice of the question of the
    // tool, which changes the document (Redact "Remove New Marks", Measure "Save and Close"), is applied before the
    // lock while the tool still edits (one Undo step; decision 8). The slots of the commit may delete anything. A
    // tool, which could not keep all of its work (a locked mark), keeps the document unlocked (owner 2026-11-21);
    // its question about the rest is a step of the flow.
    QPointer<PDFAppDocument> document = qobject_cast<PDFAppDocument*>(flow->getSubject());
    const bool isProceeding = document && !document->isLocked();
    flow->setStep(int(LockStep::FinishPages));
    QPointer<PDFAppLeaveFlow> flowGuard(flow);
    PDFAppLeaveResult result = flow->requestFinishPages(isProceeding);
    if (!guard || !flowGuard || PDFDeferredDeletion::isRetired(this) || PDFDeferredDeletion::isRetired(flow))
    {
        return;
    }
    if (result == PDFAppLeaveResult::Pending)
    {
        if (flow->waitForStep(flow, this, &PDFAppDocumentModelCommandHandler::onLockStepFinished))
        {
            return;
        }
        result = PDFAppLeaveResult::Refused;
    }
    finishLock(result == PDFAppLeaveResult::Done);
}

void PDFAppDocumentModelCommandHandler::finishLock(bool isProceeding)
{
    PDFAppDocumentTab* tab = getTab();
    PDFAppLeaveFlow* flow = tab ? tab->findLeaveFlow() : nullptr;
    if (!flow || !flow->isRunning())
    {
        return;
    }

    // The flow ends first; the lock is the last call (the slots of the lock may close the tab: a retired
    // document is not locked)
    QPointer<PDFAppDocument> document = qobject_cast<PDFAppDocument*>(flow->getSubject());
    flow->finish(isProceeding);
    if (isProceeding && document && !PDFDeferredDeletion::isRetired(document.data()) && !document->isLocked())
    {
        document->setLocked(true);
    }
}

void PDFAppDocumentModelCommandHandler::onLockStepFinished(PDFAppLeaveResult result)
{
    PDFAppDocumentTab* tab = getTab();
    PDFAppLeaveFlow* flow = tab ? tab->findLeaveFlow() : nullptr;
    if (!flow || !flow->isRunning() || flow->getGoal() != PDFAppLeaveFlow::Goal::LockDocument)
    {
        return;
    }

    if (flow->getStep() == int(LockStep::Page))
    {
        // The page has answered: Keep cancels the lock; the page, which has accepted, waits for the end of the leave
        // (the page asked, never the current page of the tab, which may have changed meanwhile)
        PDFAppPage* page = m_lockPage.data();
        m_lockPage = nullptr;
        if (result != PDFAppLeaveResult::Done || !page)
        {
            flow->finish(false);
            return;
        }
        flow->addAcceptedPage(page);
        continueLock();
        return;
    }
    finishLock(result == PDFAppLeaveResult::Done);
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
