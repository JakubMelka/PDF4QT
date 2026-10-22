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
#include "pdfappundostack.h"

namespace pdf
{

PDFAppDocumentModelCommandHandler::PDFAppDocumentModelCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::Model, tab)
{

}

PDFAppDocumentModelCommandHandler::~PDFAppDocumentModelCommandHandler() = default;

void PDFAppDocumentModelCommandHandler::execute(const QString& commandId)
{
    PDFAppDocument* document = getDocument();
    if (!document)
    {
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
        // 266:12435 (Unlock / Edit a Copy), recent_file.locked is written by the tab
        document->setLocked(!document->isLocked());
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

    // A locked document cannot be changed: undo and redo are off
    if (commandId == PDFAppCommands::undo())
    {
        return !document->isLocked() && !document->isBlockedByOperation() && document->getUndoStack()->canUndo();
    }
    if (commandId == PDFAppCommands::redo())
    {
        return !document->isLocked() && !document->isBlockedByOperation() && document->getUndoStack()->canRedo();
    }
    return commandId == PDFAppCommands::lockDocument() || commandId == PDFAppCommands::editCopy();
}

bool PDFAppDocumentModelCommandHandler::isChecked(const QString& commandId) const
{
    const PDFAppDocument* document = getDocument();
    return commandId == PDFAppCommands::lockDocument() && document && document->isLocked();
}

}   // namespace pdf
