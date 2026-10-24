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

#include "pdfappnotesoutlinecommands.h"
#include "pdfappcommand.h"
#include "pdfappdocumentview.h"
#include "pdfappoutlineeditor.h"

namespace pdf
{

PDFAppNotesOutlineCommandHandler::PDFAppNotesOutlineCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::NotesOutline, tab)
{
    // The state follows the current item of the Outline panel and the document
    if (PDFAppOutlineEditor* editor = getEditor())
    {
        connect(editor, &PDFAppOutlineEditor::stateChanged, this, &PDFAppNotesOutlineCommandHandler::stateChanged);
    }
}

PDFAppNotesOutlineCommandHandler::~PDFAppNotesOutlineCommandHandler() = default;

PDFAppOutlineEditor* PDFAppNotesOutlineCommandHandler::getEditor() const
{
    const PDFAppDocumentView* view = getView();
    return view ? view->getOutlineEditor() : nullptr;
}

void PDFAppNotesOutlineCommandHandler::execute(const QString& commandId)
{
    // The commands change the document, whose listeners may delete the tab:
    // nothing of the handler is used after them
    PDFAppOutlineEditor* editor = getEditor();
    if (!editor || !isEnabled(commandId))
    {
        return;
    }

    if (commandId == PDFAppCommands::outlineNewItem())
    {
        editor->createItemAtCurrentView();
    }
    else if (commandId == PDFAppCommands::outlineRename())
    {
        editor->renameCurrentItem();
    }
    else if (commandId == PDFAppCommands::outlineDelete())
    {
        editor->deleteCurrentItem();
    }
    else if (commandId == PDFAppCommands::outlineMoveUp())
    {
        editor->moveCurrentItemUp();
    }
    else if (commandId == PDFAppCommands::outlineMoveDown())
    {
        editor->moveCurrentItemDown();
    }
    else if (commandId == PDFAppCommands::outlineIndent())
    {
        editor->indentCurrentItem();
    }
    else if (commandId == PDFAppCommands::outlineOutdent())
    {
        editor->outdentCurrentItem();
    }
    else if (commandId == PDFAppCommands::outlineSetTarget())
    {
        editor->setCurrentItemTargetToCurrentView();
    }
    else if (commandId == PDFAppCommands::outlineBold())
    {
        editor->setCurrentItemBold(!editor->isCurrentItemBold());
    }
    else if (commandId == PDFAppCommands::outlineItalic())
    {
        editor->setCurrentItemItalic(!editor->isCurrentItemItalic());
    }
}

bool PDFAppNotesOutlineCommandHandler::isEnabled(const QString& commandId) const
{
    const PDFAppOutlineEditor* editor = getEditor();
    if (!editor || !editor->isEditingAllowed())
    {
        return false;
    }

    if (commandId == PDFAppCommands::outlineNewItem())
    {
        return true;
    }
    if (commandId == PDFAppCommands::outlineMoveUp())
    {
        return editor->canMoveUp();
    }
    if (commandId == PDFAppCommands::outlineMoveDown())
    {
        return editor->canMoveDown();
    }
    if (commandId == PDFAppCommands::outlineIndent())
    {
        return editor->canIndent();
    }
    if (commandId == PDFAppCommands::outlineOutdent())
    {
        return editor->canOutdent();
    }
    if (commandId == PDFAppCommands::outlineRename() || commandId == PDFAppCommands::outlineDelete() || commandId == PDFAppCommands::outlineSetTarget() ||
        commandId == PDFAppCommands::outlineBold() || commandId == PDFAppCommands::outlineItalic())
    {
        return editor->hasCurrentItem();
    }
    return false;
}

bool PDFAppNotesOutlineCommandHandler::isChecked(const QString& commandId) const
{
    const PDFAppOutlineEditor* editor = getEditor();
    if (!editor)
    {
        return false;
    }
    if (commandId == PDFAppCommands::outlineBold())
    {
        return editor->isCurrentItemBold();
    }
    if (commandId == PDFAppCommands::outlineItalic())
    {
        return editor->isCurrentItemItalic();
    }
    return false;
}

}   // namespace pdf
