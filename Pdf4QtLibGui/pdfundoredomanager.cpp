// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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
