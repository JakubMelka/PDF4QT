//    Copyright (C) 2020-2023 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfundoredomanager.h"
#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFUndoRedoManager::PDFUndoRedoManager(QObject* parent) :
    BaseClass(parent)
{

}

PDFUndoRedoManager::~PDFUndoRedoManager()
{

}

void PDFUndoRedoManager::doUndo()
{
    if (!canUndo())
    {
        // Undo operation can't be performed
        return;
    }

    UndoRedoItem item = m_undoSteps.back();
    m_undoSteps.pop_back();
    m_isCurrentSaved = false;
    m_redoSteps.insert(m_redoSteps.begin(), item);
    clampUndoRedoSteps();

    Q_EMIT undoRedoStateChanged();
    Q_EMIT documentChangeRequest(pdf::PDFModifiedDocument(item.oldDocument, nullptr, item.flags));
}

void PDFUndoRedoManager::doRedo()
{
    if (!canRedo())
    {
        // Redo operation can't be performed
        return;
    }

    UndoRedoItem item = m_redoSteps.front();
    m_redoSteps.erase(m_redoSteps.begin());
    m_isCurrentSaved = false;
    m_undoSteps.push_back(item);
    clampUndoRedoSteps();

    Q_EMIT undoRedoStateChanged();
    Q_EMIT documentChangeRequest(pdf::PDFModifiedDocument(item.newDocument, nullptr, item.flags));
}

void PDFUndoRedoManager::clear()
{
    if (canUndo() || canRedo())
    {
        m_undoSteps.clear();
        m_redoSteps.clear();
        Q_EMIT undoRedoStateChanged();
    }
}

void PDFUndoRedoManager::createUndo(pdf::PDFModifiedDocument document, pdf::PDFDocumentPointer oldDocument)
{
    m_undoSteps.emplace_back(oldDocument, document, document.getFlags());
    m_redoSteps.clear();
    m_isCurrentSaved = false;
    clampUndoRedoSteps();
    Q_EMIT undoRedoStateChanged();
}

void PDFUndoRedoManager::setMaximumSteps(size_t undoLimit, size_t redoLimit)
{
    if (m_undoLimit != undoLimit || m_redoLimit != redoLimit)
    {
        m_undoLimit = undoLimit;
        m_redoLimit = redoLimit;
        clampUndoRedoSteps();
        Q_EMIT undoRedoStateChanged();
    }
}

void PDFUndoRedoManager::clampUndoRedoSteps()
{
    if (m_undoSteps.size() > m_undoLimit)
    {
        // We erase from oldest steps to newest
        m_undoSteps.erase(m_undoSteps.begin(), std::next(m_undoSteps.begin(), m_undoSteps.size() - m_undoLimit));
    }
    if (m_redoSteps.size() > m_redoLimit)
    {
        // Newest steps are erased
        m_redoSteps.resize(m_redoLimit);
    }
}

bool PDFUndoRedoManager::isCurrentSaved() const
{
    return m_isCurrentSaved;
}

void PDFUndoRedoManager::setIsCurrentSaved(bool newIsCurrentSaved)
{
    m_isCurrentSaved = newIsCurrentSaved;
}

}   // namespace pdfviewer
