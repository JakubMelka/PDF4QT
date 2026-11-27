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

#ifndef PDFCONTENTUNDOSTACK_H
#define PDFCONTENTUNDOSTACK_H

#include "pdfcontentcommands.h"

#include <QString>

#include <vector>

namespace pdf
{

// Edit Content, stage E0, package P03. Plan: zadani_jadro.md 4.5 (undo stack); zadani.md QE.12,
// RE-16.

/// The undo stack of a session: one stack for the whole tool over all pages. It only stores
/// the executed commands and the position; applying and reverting is done by the session
/// (PDFContentEditSession::execute, undo, redo), which tells the changes.
///
/// - A macro (beginMacro ... endMacro) is one step: one gesture of the user.
/// - A pushed command is first offered to the last command of the stack (mergeWith), unless a
///   macro is open or the stack is at its clean point.
/// - At most MAXIMUM_COMMAND_COUNT steps are kept: a safety limit of the undo steps (owner QE.12
///   speaks of commands; a macro is one step of the user, so the limit counts steps). The oldest
///   step is dropped then and the clean point is lost, when it was before it.
/// - The clean point: the tool has unsaved work exactly when the stack is not at its clean point.
///
/// Threads and ownership: owned by its session, used only in the thread of the session. It
/// owns the commands (shared pointers; a command dropped from the stack is destroyed).
class PDF4QTLIBCORESHARED_EXPORT PDFContentUndoStack
{
public:
    static constexpr size_t MAXIMUM_COMMAND_COUNT = 10000;

    /// One step of the stack: a command, or the commands of a macro in the order they were
    /// applied (a plain value; it co-owns its commands)
    struct Step
    {
        QString text;
        std::vector<PDFContentCommandPointer> commands;
    };

    explicit PDFContentUndoStack();
    ~PDFContentUndoStack();

    PDFContentUndoStack(const PDFContentUndoStack&) = delete;
    PDFContentUndoStack& operator=(const PDFContentUndoStack&) = delete;

    /// Adds an applied command: the steps, which were undone, are dropped; the command is merged
    /// into the last one, added to the open macro, or becomes a new step
    void push(PDFContentCommandPointer command);

    /// Opens a macro: the commands pushed until endMacro are one step with this text. Macros nest
    /// (the outermost one is the step).
    void beginMacro(const QString& text);

    /// Closes a macro; a macro without a command adds no step
    void endMacro();

    bool isMacroOpen() const;

    bool canUndo() const;
    bool canRedo() const;

    /// The text of the step undo / redo would do (empty, when there is none)
    QString getUndoText() const;
    QString getRedoText() const;

    /// The step to undo: its commands are reverted by the session in the reverse order, then
    /// the position moves (nullptr, when there is none or a macro is open)
    const Step* getUndoStep() const;

    /// The step to redo (nullptr, when there is none or a macro is open)
    const Step* getRedoStep() const;

    /// The session has reverted the step of getUndoStep / applied the step of getRedoStep
    void moveToPreviousStep();
    void moveToNextStep();

    /// Number of the steps and the position (steps before it are done)
    size_t getCount() const;
    size_t getPosition() const;

    /// Marks the current position as the clean point (the work was applied to the document)
    void setClean();

    /// The stack is at its clean point (a new stack is clean)
    bool isClean() const;

    /// Drops everything; the stack is clean
    void clear();

    /// The sum of PDFContentCommand::getStoredItemCount of every command of the stack: what the
    /// undo history keeps, in items (see there)
    size_t getStoredItemCount() const;

    /// The new objects (PDFContentCommand::getNewObjects) of the commands, which push has
    /// dropped from behind the position since the last call (the undone steps a new command
    /// replaces); the list is emptied by the call. The model does not show the change of those
    /// commands, so nothing refers to the objects any more and the session drops them. The
    /// objects of a step dropped at the limit of the count are not among them: its change is
    /// a part of the model.
    std::vector<PDFObjectReference> takeDroppedNewObjects();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}   // namespace pdf

#endif // PDFCONTENTUNDOSTACK_H
