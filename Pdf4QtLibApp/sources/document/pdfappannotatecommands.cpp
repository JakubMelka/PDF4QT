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

#include "pdfappannotatecommands.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreator.h"
#include "pdfappcommand.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappmarkresults.h"

namespace pdf
{

PDFAppAnnotateCommandHandler::PDFAppAnnotateCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::Annotate, tab)
{
    // The commands follow the lock, the permissions and the document
    PDFAppDocumentView* view = getView();
    if (PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr)
    {
        connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppAnnotateCommandHandler::stateChanged);
    }

    if (view)
    {
        m_creator = new PDFAppAnnotationCreator(view, this);
        connect(m_creator.data(), &PDFAppAnnotationCreator::activeKindChanged, this, &PDFAppAnnotateCommandHandler::onCreatorStateChanged);
        connect(m_creator.data(), &PDFAppAnnotationCreator::toolStateChanged, this, &PDFAppAnnotateCommandHandler::onCreatorStateChanged);

        m_markResults = new PDFAppMarkResults(view, this);
        connect(m_markResults.data(), &PDFAppMarkResults::stateChanged, this, &PDFAppAnnotateCommandHandler::onCreatorStateChanged);
    }
}

PDFAppAnnotateCommandHandler::~PDFAppAnnotateCommandHandler()
{
    // Mark as annotations first (it uses the creator), then the creator (its tools
    // leave the tool manager of the view, which is still whole)
    delete m_markResults.data();
    delete m_creator.data();
}

PDFAppAnnotationCreator* PDFAppAnnotateCommandHandler::getCreator() const
{
    return m_creator.data();
}

PDFAppMarkResults* PDFAppAnnotateCommandHandler::getMarkResults() const
{
    return m_markResults.data();
}

PDFAppAnnotateCommandHandler* PDFAppAnnotateCommandHandler::findHandler(const PDFAppDocumentView* view)
{
    const PDFAppDocumentTab* tab = view ? view->getTab() : nullptr;
    return tab ? qobject_cast<PDFAppAnnotateCommandHandler*>(tab->findHandler(PDFAppCommands::annotatePanel())) : nullptr;
}

PDFAppAnnotationCreator* PDFAppAnnotateCommandHandler::findCreator(const PDFAppDocumentView* view)
{
    const PDFAppAnnotateCommandHandler* handler = findHandler(view);
    return handler ? handler->getCreator() : nullptr;
}

void PDFAppAnnotateCommandHandler::onCreatorStateChanged()
{
    Q_EMIT stateChanged();
}

void PDFAppAnnotateCommandHandler::execute(const QString& commandId)
{
    PDFAppDocumentView* view = getView();
    if (!view || !isEnabled(commandId))
    {
        return;
    }

    // The panel opens below its menu button (the command toggles it, as the button)
    if (commandId == PDFAppCommands::annotatePanel() || commandId == PDFAppCommands::insertPanel())
    {
        PDFAppDocumentToolbar* toolbar = view->getToolbar();
        if (!toolbar)
        {
            return;
        }

        const PDFAppDocumentToolbar::Panel panel = commandId == PDFAppCommands::annotatePanel() ? PDFAppDocumentToolbar::Panel::Annotate : PDFAppDocumentToolbar::Panel::Insert;
        PDFAppDocumentPanelPopup* popup = toolbar->findPanel(panel);
        if (popup && popup->isVisible())
        {
            popup->close();
        }
        else
        {
            toolbar->openPanel(panel);
        }
        return;
    }

    if (commandId == PDFAppCommands::annotateMarkResults())
    {
        if (m_markResults)
        {
            m_markResults->openMenu();
        }
        return;
    }

    PDFAppAnnotationCreator* creator = m_creator.data();
    const PDFAppAnnotationToolKind kind = PDFAppAnnotationTools::getKind(commandId);
    if (!creator || kind == PDFAppAnnotationToolKind::None)
    {
        return;
    }

    // Selection first: a markup marks the selected text at once (tools.md 9.8)
    if (PDFAppAnnotationTools::isMarkup(kind) && creator->getActiveKind() == PDFAppAnnotationToolKind::None && creator->hasTextSelection())
    {
        if (kind == PDFAppAnnotationToolKind::HighlightComment)
        {
            creator->commentTextSelection();
        }
        else
        {
            creator->markTextSelection(PDFAppAnnotationTools::getMarkupType(kind));
        }
        return;
    }

    // A command toggles its tool
    if (creator->getActiveKind() == kind)
    {
        creator->endTool();
    }
    else
    {
        creator->startTool(kind);
    }
}

bool PDFAppAnnotateCommandHandler::isEnabled(const QString& commandId) const
{
    if (!BaseClass::isEnabled(commandId))
    {
        return false;
    }

    // Locked document, no permission (O8, O20): Annotate and Insert are disabled
    const PDFAppDocumentView* view = getView();
    const PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!controller || !controller->isAnnotationEditingAllowed())
    {
        return false;
    }

    if (commandId == PDFAppCommands::annotatePanel() || commandId == PDFAppCommands::insertPanel())
    {
        return true;
    }
    if (commandId == PDFAppCommands::annotateMarkResults())
    {
        return m_markResults && m_markResults->canMark();
    }
    return m_creator && PDFAppAnnotationTools::getKind(commandId) != PDFAppAnnotationToolKind::None;
}

bool PDFAppAnnotateCommandHandler::isChecked(const QString& commandId) const
{
    const PDFAppAnnotationToolKind kind = PDFAppAnnotationTools::getKind(commandId);
    return m_creator && kind != PDFAppAnnotationToolKind::None && m_creator->getActiveKind() == kind;
}

}   // namespace pdf
