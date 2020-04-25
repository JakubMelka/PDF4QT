//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

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

signals:
    /// This signals are emitted, when undo/redo action availability has
    /// been changed (for example, user pressed undo/redo action)
    void undoRedoStateChanged();

    /// This signal is being emitted, when user performs undo/redo action.
    /// Before signal is emitted, this object is in corrected state, as action
    /// is performed.
    /// \param document Active document
    /// \param flags Change flags
    void documentChangeRequest(pdf::PDFDocumentPointer document, pdf::PDFModifiedDocument::ModificationFlags flags);

private:
    /// Clamps undo/redo steps so they fit the limits
    void clampUndoRedoSteps();

    struct UndoRedoItem
    {
        pdf::PDFDocumentPointer document;
        pdf::PDFModifiedDocument::ModificationFlags flags = pdf::PDFModifiedDocument::None;
    };

    size_t m_undoLimit = 0;
    size_t m_redoLimit = 0;
    std::vector<UndoRedoItem> m_undoSteps;
    std::vector<UndoRedoItem> m_redoSteps;
};

}   // namespace pdfviewer

#endif // PDFUNDOREDOMANAGER_H
