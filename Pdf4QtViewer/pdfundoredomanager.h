//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFUNDOREDOMANAGER_H
#define PDFUNDOREDOMANAGER_H

#include "pdfdocument.h"

#include <QObject>

namespace pdfviewer
{

/// Undo/Redo document manager, it is managing undo and redo steps,
/// when document is modified.
class PDFUndoRedoManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFUndoRedoManager(QObject* parent);
    virtual ~PDFUndoRedoManager() override;

    bool canUndo() const { return !m_undoSteps.empty(); }
    bool canRedo() const { return !m_redoSteps.empty(); }

    /// Performs single undo step. If Undo action can't be performed,
    /// then nothing happens.
    void doUndo();

    /// Performs single redo step. If Redo action can't be performed,
    /// then nothing happens.
    void doRedo();

    /// Clears all undo/redo steps
    void clear();

    /// Create undo action (after document modification)
    /// \param document Document created by modification
    /// \param oldDocument Old document
    void createUndo(pdf::PDFModifiedDocument document, pdf::PDFDocumentPointer oldDocument);

    /// Sets maximum steps for undo/redo
    /// \param undoLimit Maximum undo steps
    /// \param redoLimit Maximum redo steps
    void setMaximumSteps(size_t undoLimit, size_t redoLimit);

signals:
    /// This signals are emitted, when undo/redo action availability has
    /// been changed (for example, user pressed undo/redo action)
    void undoRedoStateChanged();

    /// This signal is being emitted, when user performs undo/redo action.
    /// Before signal is emitted, this object is in corrected state, as action
    /// is performed.
    /// \param document Document
    void documentChangeRequest(pdf::PDFModifiedDocument document);

private:
    /// Clamps undo/redo steps so they fit the limits
    void clampUndoRedoSteps();

    struct UndoRedoItem
    {
        explicit inline UndoRedoItem() = default;
        explicit inline UndoRedoItem(pdf::PDFDocumentPointer oldDocument, pdf::PDFDocumentPointer newDocument, pdf::PDFModifiedDocument::ModificationFlags flags) :
            oldDocument(qMove(oldDocument)),
            newDocument(qMove(newDocument)),
            flags(flags)
        {

        }

        pdf::PDFDocumentPointer oldDocument;
        pdf::PDFDocumentPointer newDocument;
        pdf::PDFModifiedDocument::ModificationFlags flags = pdf::PDFModifiedDocument::None;
    };

    size_t m_undoLimit = 0;
    size_t m_redoLimit = 0;
    std::vector<UndoRedoItem> m_undoSteps;
    std::vector<UndoRedoItem> m_redoSteps;
};

}   // namespace pdfviewer

#endif // PDFUNDOREDOMANAGER_H
