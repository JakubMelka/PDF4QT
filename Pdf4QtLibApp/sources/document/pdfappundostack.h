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


#ifndef PDFAPPUNDOSTACK_H
#define PDFAPPUNDOSTACK_H

#include "pdfappglobal.h"
#include "pdfappdocument.h"

#include <deque>

namespace pdf
{

/// One step of the undo stack: the document before the change (undo) or after it
/// (redo), the flags of the change and its description
class PDF4QTLIBAPPSHARED_EXPORT PDFAppUndoStep
{
public:
    PDFDocumentPointer document;
    PDFModifiedDocument::ModificationFlags flags = PDFModifiedDocument::Reset;
    QString description;
    PDFAppChangeSource source = PDFAppChangeSource::User;

    /// Key of a merge session (step 4, owner decision O20): consecutive commits
    /// with the same key - a live change of the color or of the width of the
    /// selected annotations - are one undo step (empty = the step is never merged)
    QString mergeKey;

    /// Estimate of the memory, which the step holds beyond the document of the
    /// neighboring version (objects, which the two documents do not share)
    qint64 memory = 0;
};

/// Undo / redo of a document by snapshots of the immutable documents (decision D9):
/// each commit pushes the document before the change. The number of steps is
/// limited (editing.undoLimit, read by the document at each commit, default 100)
/// and so is the memory held by the steps (getMemoryLimit): the oldest steps are
/// dropped first. The snapshots share the unchanged objects (PDFObject is
/// implicitly shared), so a step holds only the objects, which the change has
/// replaced - estimateMemory measures them.
///
/// The stack remembers the position of the saved version: undo and redo back to
/// it make the document unmodified again, and getUnsavedChangeCount says the
/// number of steps between the current and the saved version.
///
/// The stack belongs to its PDFAppDocument; undo and redo set the document by
/// PDFAppDocument::restore (the version is incremented).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppUndoStack
{
public:
    /// Default maximal number of undo steps (decision D9)
    static constexpr int DEFAULT_LIMIT = 100;

    /// Default memory held by the steps (bytes)
    static constexpr qint64 DEFAULT_MEMORY_LIMIT = qint64(512) * 1024 * 1024;

    explicit PDFAppUndoStack(PDFAppDocument* document);

    PDFAppUndoStack(const PDFAppUndoStack&) = delete;
    PDFAppUndoStack& operator=(const PDFAppUndoStack&) = delete;

    /// Records a change: \p step holds the document before the change. The redo
    /// steps are removed, the oldest steps over the limits are dropped.
    void push(PDFAppUndoStep step);

    /// A commit with the key \p mergeKey can be merged into the top undo step: the
    /// key is not empty, it equals the key of the top step and nothing has happened
    /// since that step was pushed (no other commit, undo, redo, save, closeMerge)
    bool canMerge(const QString& mergeKey) const;

    /// Merges a change into the top undo step (canMerge must be true): the step
    /// keeps the document before the first change of the session, its flags are
    /// joined with \p flags and its memory is estimated against \p newDocument
    void merge(PDFModifiedDocument::ModificationFlags flags, const PDFDocument* newDocument);

    /// Ends the merge session: the next commit is a new step, whatever its key
    void closeMerge() { m_isMergeOpen = false; }

    /// A merge session is open (the top step can take a commit of its key)
    bool isMergeOpen() const { return m_isMergeOpen; }

    bool canUndo() const { return !m_undoSteps.empty(); }
    bool canRedo() const { return !m_redoSteps.empty(); }

    /// Descriptions of the next undo / redo step (empty = none)
    QString getUndoDescription() const;
    QString getRedoDescription() const;

    /// Undoes / redoes one step. \returns false, if there is none or the document
    /// refuses it (not ready, locked, a blocking operation)
    bool undo();
    bool redo();

    /// Removes all steps (a reload of the file); the current version is the saved one
    void clear();

    /// Maximal number of undo steps (0 = no undo)
    int getLimit() const { return m_limit; }
    void setLimit(int limit);

    /// Maximal number of redo steps (0 = no redo): after an undo the redo steps
    /// farthest from the current version over the limit are dropped
    int getRedoLimit() const { return m_redoLimit; }
    void setRedoLimit(int limit);

    /// Maximal memory held by the undo and redo steps in bytes (at least one
    /// undo step is kept, unless the limit is 0)
    qint64 getMemoryLimit() const { return m_memoryLimit; }
    void setMemoryLimit(qint64 memoryLimit);

    /// Estimated memory held by the steps
    qint64 getMemoryUsage() const;

    size_t getUndoCount() const { return m_undoSteps.size(); }
    size_t getRedoCount() const { return m_redoSteps.size(); }

    /// The current version is saved (PDFAppDocument::setSaved); ends the merge session
    void setSavedPosition();

    /// The saved version is not a version of the stack (an older version written
    /// by a save, while the document was changed); ends the merge session
    void setSavedPositionUnknown() { m_savedPosition = -1; m_isMergeOpen = false; }

    /// The current position is the saved version
    bool isAtSavedPosition() const { return m_savedPosition == int(m_undoSteps.size()); }

    /// Number of steps between the current and the saved version, -1 when the
    /// saved version is not reachable by undo / redo (dropped by the limits, or
    /// replaced by a change after an undo)
    int getUnsavedChangeCount() const;

    /// Estimated memory of the objects of \p document, which \p other does not
    /// share with it (a null document holds nothing, a null \p other shares nothing)
    static qint64 estimateMemory(const PDFDocument* document, const PDFDocument* other);

private:
    void trim();

    PDFAppDocument* m_document;
    std::deque<PDFAppUndoStep> m_undoSteps;
    std::deque<PDFAppUndoStep> m_redoSteps;
    int m_limit = DEFAULT_LIMIT;
    int m_redoLimit = DEFAULT_LIMIT;
    qint64 m_memoryLimit = DEFAULT_MEMORY_LIMIT;
    int m_savedPosition = 0;    ///< Number of undo steps of the saved version, -1 = unreachable
    bool m_isMergeOpen = false; ///< The top undo step takes the commits of its merge key
};

}   // namespace pdf

#endif // PDFAPPUNDOSTACK_H
