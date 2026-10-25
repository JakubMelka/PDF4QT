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
    else if (commandId == PDFAppCommands::outlineSetTargetNamed())
    {
        editor->chooseCurrentItemNamedDestination();
    }
    else if (commandId == PDFAppCommands::outlineKeepZoom())
    {
        editor->setCurrentItemKeepZoom(!editor->isCurrentItemKeepingZoom());
    }
    else if (commandId == PDFAppCommands::outlineKeepZoomAll())
    {
        editor->keepZoomForAllItems();
    }
    else if (getTargetType(commandId) != DestinationType::Invalid)
    {
        editor->setCurrentItemTarget(getTargetType(commandId));
    }
}

std::vector<DestinationType> PDFAppNotesOutlineCommandHandler::getTargets()
{
    // The order of the submenu Set Target (pdf4qt-document-outline-menu-target 1236:165602)
    return { DestinationType::Fit, DestinationType::FitH, DestinationType::FitV, DestinationType::FitR, DestinationType::XYZ,
             DestinationType::FitB, DestinationType::FitBH, DestinationType::FitBV };
}

QString PDFAppNotesOutlineCommandHandler::getTargetCommandId(DestinationType type)
{
    switch (type)
    {
        case DestinationType::Fit:
            return PDFAppCommands::outlineSetTargetWholePage();
        case DestinationType::FitH:
            return PDFAppCommands::outlineSetTargetPageWidth();
        case DestinationType::FitV:
            return PDFAppCommands::outlineSetTargetPageHeight();
        case DestinationType::FitR:
            return PDFAppCommands::outlineSetTargetArea();
        case DestinationType::XYZ:
            return PDFAppCommands::outlineSetTargetPosition();
        case DestinationType::FitB:
            return PDFAppCommands::outlineSetTargetContent();
        case DestinationType::FitBH:
            return PDFAppCommands::outlineSetTargetContentWidth();
        case DestinationType::FitBV:
            return PDFAppCommands::outlineSetTargetContentHeight();
        default:
            break;
    }
    return QString();
}

DestinationType PDFAppNotesOutlineCommandHandler::getTargetType(const QString& commandId)
{
    for (DestinationType type : getTargets())
    {
        if (getTargetCommandId(type) == commandId)
        {
            return type;
        }
    }
    return DestinationType::Invalid;
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
        commandId == PDFAppCommands::outlineBold() || commandId == PDFAppCommands::outlineItalic() || commandId == PDFAppCommands::outlineSetTargetNamed() ||
        getTargetType(commandId) != DestinationType::Invalid)
    {
        return editor->hasCurrentItem();
    }
    if (commandId == PDFAppCommands::outlineKeepZoom())
    {
        return editor->isCurrentItemTargetPosition();
    }
    if (commandId == PDFAppCommands::outlineKeepZoomAll())
    {
        return editor->getItemsWithZoomCount() > 0;
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
    if (commandId == PDFAppCommands::outlineKeepZoom())
    {
        return editor->isCurrentItemKeepingZoom();
    }
    return false;
}

}   // namespace pdf
