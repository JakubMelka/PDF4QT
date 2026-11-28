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

#include "pdfappeditcontentundo.h"

#include "pdfappeditcontentcontext.h"

#include "pdfcontentundostack.h"

#include <QPointer>

namespace pdf
{

// Undo and Redo of Edit Content (P14): the undo stack of the session, the page of a step first (zadani_gui.md 4.27)

PDFAppEditContentUndoTarget::PDFAppEditContentUndoTarget(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{
    // The context and its session live as long as this target (siblings under the GUI, the session a child of the
    // context deleted after this object)
    if (context)
    {
        connect(context->getSession(), &PDFContentEditSession::modelChanged, this, &PDFAppEditContentUndoTarget::onStateChanged);
        connect(context, &PDFAppEditContentContext::editingAllowedChanged, this, &PDFAppEditContentUndoTarget::onStateChanged);
        connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentUndoTarget::onStateChanged);
        connect(context, &PDFAppEditContentContext::sessionReplaced, this, &PDFAppEditContentUndoTarget::onSessionReplaced);
    }
}

void PDFAppEditContentUndoTarget::onSessionReplaced()
{
    // reloadDocument: the steps are the ones of the new session (a clean stack); the old session is disconnected
    if (m_context)
    {
        connect(m_context->getSession(), &PDFContentEditSession::modelChanged, this, &PDFAppEditContentUndoTarget::onStateChanged);
    }
    onStateChanged();
}

PDFAppEditContentUndoTarget::~PDFAppEditContentUndoTarget() = default;

bool PDFAppEditContentUndoTarget::isAvailable() const
{
    // Paused in Preview and during a run (zadani_gui.md 4.15); a page, which cannot be edited, does not stop Undo of a
    // step of another page. Never inside of a change of the model.
    return m_context && !PDFDeferredDeletion::isRetired(m_context.data()) && !m_context->isPreviewActive() && !m_context->isRunning() &&
           !m_context->getSession()->isExecuting() && !m_context->getSession()->getUndoStack()->isMacroOpen();
}

bool PDFAppEditContentUndoTarget::canUndo() const
{
    return isAvailable() && m_context->getSession()->getUndoStack()->canUndo();
}

bool PDFAppEditContentUndoTarget::canRedo() const
{
    return isAvailable() && m_context->getSession()->getUndoStack()->canRedo();
}

void PDFAppEditContentUndoTarget::undo()
{
    apply(true);
}

void PDFAppEditContentUndoTarget::redo()
{
    apply(false);
}

void PDFAppEditContentUndoTarget::apply(bool isUndo)
{
    if (!canUndo() && isUndo)
    {
        return;
    }
    if (!canRedo() && !isUndo)
    {
        return;
    }

    // A value typed into a card and not confirmed yet is a step of its own, which Undo takes back first
    // (zadani_gui.md 4.19): the slots of the cards execute it
    QPointer<PDFAppEditContentUndoTarget> guard(this);
    QPointer<PDFAppEditContentContext> context = m_context;
    context->commitPendingEdits();
    if (!guard || PDFDeferredDeletion::isRetired(this) || !context || !(isUndo ? canUndo() : canRedo()))
    {
        return;
    }

    // A step of another page shows that page first: the GUI applies the rules of the switch (typing ends, the group
    // is left, the selection is cleared), then the session selects the nodes of the step (selectionChangeRequested)
    PDFContentUndoStack* stack = context->getSession()->getUndoStack();
    const PDFContentUndoStack::Step* step = isUndo ? stack->getUndoStep() : stack->getRedoStep();
    const PDFInteger pageIndex = step && !step->commands.empty() && step->commands.front() ? step->commands.front()->getPageIndex() : -1;
    if (pageIndex >= 0 && pageIndex != context->getPageIndex())
    {
        context->requestPage(pageIndex);
        if (!guard || PDFDeferredDeletion::isRetired(this) || !context || !(isUndo ? canUndo() : canRedo()))
        {
            return;
        }
    }

    // The last call: the slots of modelChanged may close the tool
    if (isUndo)
    {
        context->getSession()->undo();
    }
    else
    {
        context->getSession()->redo();
    }
}

QString PDFAppEditContentUndoTarget::getUndoText() const
{
    return m_context ? m_context->getSession()->getUndoStack()->getUndoText() : QString();
}

QString PDFAppEditContentUndoTarget::getRedoText() const
{
    return m_context ? m_context->getSession()->getUndoStack()->getRedoText() : QString();
}

void PDFAppEditContentUndoTarget::onStateChanged()
{
    Q_EMIT undoStateChanged();
}

}   // namespace pdf

#include "moc_pdfappeditcontentundo.cpp"
