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

#include "pdfappannotationeditcommands.h"
#include "pdfappannotationbar.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationgeometrydialog.h"
#include "pdfappannotationmenus.h"
#include "pdfappannotationoperations.h"
#include "pdfappannotationpropertiesdialog.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappcopytopagesdialog.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"

namespace pdf
{

/// Tables of the commands of the package
class PDFAppAnnotationEditCommandHandlerHelper
{
public:
    PDFAppAnnotationEditCommandHandlerHelper() = delete;

    /// Alignment of an align command (false for another command)
    static bool getAlignment(const QString& commandId, PDFWidgetAnnotationManager::Alignment* alignment)
    {
        using C = PDFAppCommands;
        using A = PDFWidgetAnnotationManager::Alignment;

        const std::pair<QString, A> table[] = { { C::annotationAlignLeft(), A::Left }, { C::annotationAlignCenter(), A::HorizontalCenter },
                                                { C::annotationAlignRight(), A::Right }, { C::annotationAlignTop(), A::Top },
                                                { C::annotationAlignMiddle(), A::VerticalCenter }, { C::annotationAlignBottom(), A::Bottom } };
        for (const auto& item : table)
        {
            if (item.first == commandId)
            {
                *alignment = item.second;
                return true;
            }
        }
        return false;
    }

    /// Angle of a rotate command (clockwise as the user sees the page; 0 for another command)
    static double getRotation(const QString& commandId)
    {
        using C = PDFAppCommands;
        if (commandId == C::annotationRotateClockwise())
        {
            return 90.0;
        }
        if (commandId == C::annotationRotateCounterclockwise())
        {
            return -90.0;
        }
        return commandId == C::annotationRotate180() ? 180.0 : 0.0;
    }

    /// The command changes the selected annotations (refused for a locked
    /// document and without the permission)
    static bool isModifying(const QString& commandId)
    {
        using C = PDFAppCommands;
        return commandId != C::annotationProperties() && commandId != C::annotationComment() && commandId != C::annotationSelectAll();
    }

    /// The command changes the geometry or the look of the annotations (refused
    /// for a selection of Locked annotations only - ISO 32000-2 Table 167)
    static bool isGeometry(const QString& commandId)
    {
        using C = PDFAppCommands;
        return commandId == C::cut() || commandId == C::deleteSelection() || commandId == C::annotationGeometry() || commandId == C::annotationHide() ||
               commandId == C::annotationFlipHorizontal() || commandId == C::annotationFlipVertical() || getRotation(commandId) != 0.0 ||
               commandId == C::annotationDistributeHorizontally() || commandId == C::annotationDistributeVertically();
    }
};

PDFAppAnnotationEditCommandHandler::PDFAppAnnotationEditCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::AnnotationEdit, tab)
{
    PDFAppDocumentView* view = getView();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!view || !controller)
    {
        return;
    }

    // The commands follow the selection, the lock and the permissions
    connect(controller, &PDFAppAnnotationController::selectionChanged, this, &PDFAppAnnotationEditCommandHandler::stateChanged);
    connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppAnnotationEditCommandHandler::stateChanged);
    connect(controller, &PDFAppAnnotationController::changeCommitted, this, &PDFAppAnnotationEditCommandHandler::stateChanged);

    // The requests of the panels, of the commands and of the engine (they come
    // from the event loop): the comment popup, the dialogs, the menus
    connect(controller, &PDFAppAnnotationController::commentRequested, this, &PDFAppAnnotationEditCommandHandler::onCommentRequested);
    connect(controller, &PDFAppAnnotationController::replyRequested, this, &PDFAppAnnotationEditCommandHandler::onReplyRequested);
    connect(controller, &PDFAppAnnotationController::popupRequested, this, &PDFAppAnnotationEditCommandHandler::onPopupRequested);
    connect(controller, &PDFAppAnnotationController::propertiesRequested, this, &PDFAppAnnotationEditCommandHandler::onPropertiesRequested);
    connect(controller, &PDFAppAnnotationController::geometryRequested, this, &PDFAppAnnotationEditCommandHandler::onGeometryRequested);
    connect(controller, &PDFAppAnnotationController::copyToPagesRequested, this, &PDFAppAnnotationEditCommandHandler::onCopyToPagesRequested);
    connect(controller, &PDFAppAnnotationController::menuRequested, this, &PDFAppAnnotationEditCommandHandler::onMenuRequested);

    // The annotation bar lies over the canvas (a child of the canvas area of the
    // view - it dies with the view), the menus are a child of the handler
    m_bar = new PDFAppAnnotationBar(view, view->getCanvasArea());
    m_menus = new PDFAppAnnotationMenus(view, this);
}

PDFAppAnnotationEditCommandHandler::~PDFAppAnnotationEditCommandHandler()
{
    // The bar belongs to the canvas area; a handler deleted before its view takes it
    delete m_bar.data();
}

PDFAppAnnotationBar* PDFAppAnnotationEditCommandHandler::getAnnotationBar() const
{
    return m_bar.data();
}

PDFAppAnnotationMenus* PDFAppAnnotationEditCommandHandler::getMenus() const
{
    return m_menus.data();
}

QWidget* PDFAppAnnotationEditCommandHandler::getOpenDialog() const
{
    return m_dialog && m_dialog->isVisible() ? m_dialog.data() : nullptr;
}

QWidget* PDFAppAnnotationEditCommandHandler::getDialogParent() const
{
    PDFAppDocumentTab* tab = getTab();
    return tab ? tab->window() : nullptr;
}

void PDFAppAnnotationEditCommandHandler::execute(const QString& commandId)
{
    using C = PDFAppCommands;
    using H = PDFAppAnnotationEditCommandHandlerHelper;

    PDFAppDocumentView* view = getView();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    if (!controller || !manager || !isEnabled(commandId))
    {
        return;
    }

    const std::vector<PDFObjectReference> selection = controller->getSelectedAnnotations();
    const PDFObjectReference single = selection.size() == 1 ? selection.front() : PDFObjectReference();

    // A change of the engine is announced by beginEngineChange (its description,
    // never a part of a merge session of the bar) and finished below
    QPointer<PDFAppAnnotationController> controllerGuard(controller);

    PDFWidgetAnnotationManager::Alignment alignment = PDFWidgetAnnotationManager::Alignment::Left;
    if (commandId == C::cut())
    {
        controller->cutSelection();
    }
    else if (commandId == C::paste())
    {
        controller->paste(false);
    }
    else if (commandId == C::pasteInPlace())
    {
        controller->paste(true);
    }
    else if (commandId == C::deleteSelection())
    {
        controller->deleteSelection();
    }
    else if (commandId == C::annotationProperties())
    {
        controller->requestProperties(single);
    }
    else if (commandId == C::annotationComment())
    {
        controller->requestComment(single, false);
    }
    else if (commandId == C::annotationReply())
    {
        controller->requestReply(single);
    }
    else if (commandId == C::annotationGeometry())
    {
        controller->requestGeometry(single);
    }
    else if (commandId == C::annotationCopyToPages())
    {
        controller->requestCopyToPages();
    }
    else if (commandId == C::annotationHide())
    {
        controller->beginEngineChange(tr("Hide annotations"));
        manager->setAnnotationsHidden(selection, true);
    }
    else if (commandId == C::annotationSelectAll())
    {
        controller->selectAll();
    }
    else if (const double angle = H::getRotation(commandId); angle != 0.0)
    {
        controller->beginEngineChange(tr("Rotate annotations"));
        manager->rotateSelectedAnnotations(angle);
    }
    else if (commandId == C::annotationFlipHorizontal())
    {
        controller->beginEngineChange(tr("Flip annotations"));
        manager->flipSelectedAnnotations(Qt::Horizontal);
    }
    else if (commandId == C::annotationFlipVertical())
    {
        controller->beginEngineChange(tr("Flip annotations"));
        manager->flipSelectedAnnotations(Qt::Vertical);
    }
    else if (commandId == C::annotationDistributeHorizontally())
    {
        controller->beginEngineChange(tr("Distribute annotations"));
        manager->distributeSelectedAnnotations(Qt::Horizontal);
    }
    else if (commandId == C::annotationDistributeVertically())
    {
        controller->beginEngineChange(tr("Distribute annotations"));
        manager->distributeSelectedAnnotations(Qt::Vertical);
    }
    else if (H::getAlignment(commandId, &alignment))
    {
        controller->beginEngineChange(tr("Align annotations"));
        manager->alignSelectedAnnotations(alignment);
    }

    // The call of the engine has returned: a description, which no commit has
    // taken (nothing has changed), does not name the next change
    if (controllerGuard)
    {
        controllerGuard->endEngineChange();
    }
}

bool PDFAppAnnotationEditCommandHandler::isEnabled(const QString& commandId) const
{
    using C = PDFAppCommands;
    using H = PDFAppAnnotationEditCommandHandlerHelper;

    if (!BaseClass::isEnabled(commandId))
    {
        return false;
    }

    const PDFAppDocumentView* view = getView();
    const PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!controller || !controller->getAnnotationManager())
    {
        return false;
    }

    const bool isEditingAllowed = controller->isAnnotationEditingAllowed();
    if (H::isModifying(commandId) && !isEditingAllowed)
    {
        return false;
    }

    const std::vector<PDFObjectReference> selection = controller->getSelectedAnnotations();
    const size_t count = selection.size();
    if (commandId == C::paste() || commandId == C::pasteInPlace())
    {
        return controller->canPaste();
    }
    if (commandId == C::annotationSelectAll())
    {
        return view->getPageCount() > 0;
    }

    // A selection of Locked annotations keeps its geometry and its look (the
    // engine skips the locked ones of a mixed selection)
    if (H::isGeometry(commandId) || commandId == C::annotationAlignLeft() || commandId == C::annotationAlignCenter() || commandId == C::annotationAlignRight() ||
        commandId == C::annotationAlignTop() || commandId == C::annotationAlignMiddle() || commandId == C::annotationAlignBottom())
    {
        const PDFAppDocument* document = controller->getDocument();
        bool isAnyModifiable = false;
        for (const PDFObjectReference& reference : selection)
        {
            isAnyModifiable = isAnyModifiable || (document && PDFAppAnnotationOperations::canModifyAnnotation(document->getDocument(), reference));
        }
        if (!isAnyModifiable)
        {
            return false;
        }
    }

    if (commandId == C::annotationProperties() || commandId == C::annotationComment() || commandId == C::annotationReply() || commandId == C::annotationGeometry())
    {
        return count == 1;
    }

    PDFWidgetAnnotationManager::Alignment alignment = PDFWidgetAnnotationManager::Alignment::Left;
    if (H::getAlignment(commandId, &alignment))
    {
        return count >= 2;
    }
    if (commandId == C::annotationDistributeHorizontally() || commandId == C::annotationDistributeVertically())
    {
        return count >= 3;
    }
    return count >= 1;
}

void PDFAppAnnotationEditCommandHandler::onCommentRequested(PDFObjectReference annotation, bool compose)
{
    // A request of a comment of an existing annotation (compose = the focus goes
    // into the reply field); a new comment is composed by package A directly
    PDFAppDocumentView* view = getView();
    if (PDFAppCommentPopup* popup = view ? view->getCommentPopup() : nullptr)
    {
        popup->showThread(annotation, compose);
    }
}

void PDFAppAnnotationEditCommandHandler::onReplyRequested(PDFObjectReference annotation)
{
    PDFAppDocumentView* view = getView();
    if (PDFAppCommentPopup* popup = view ? view->getCommentPopup() : nullptr)
    {
        popup->showThread(annotation, true);
    }
}

void PDFAppAnnotationEditCommandHandler::onPopupRequested(PDFObjectReference annotation, PDFObjectReference page, QPoint globalPosition)
{
    // Double click on a markup annotation: its comment thread
    Q_UNUSED(page);
    Q_UNUSED(globalPosition);
    onCommentRequested(annotation, false);
}

void PDFAppAnnotationEditCommandHandler::onPropertiesRequested(PDFObjectReference annotation)
{
    PDFAppDocumentView* view = getView();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!controller || getOpenDialog())
    {
        return;
    }
    m_dialog = PDFAppAnnotationPropertiesDialog::open(controller, annotation, getDialogParent());
}

void PDFAppAnnotationEditCommandHandler::onGeometryRequested(PDFObjectReference annotation)
{
    PDFAppDocumentView* view = getView();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!controller || getOpenDialog())
    {
        return;
    }
    m_dialog = PDFAppAnnotationGeometryDialog::open(controller, annotation, getDialogParent());
}

void PDFAppAnnotationEditCommandHandler::onCopyToPagesRequested()
{
    PDFAppDocumentView* view = getView();
    PDFAppAnnotationController* controller = view ? view->getAnnotationController() : nullptr;
    if (!controller || getOpenDialog())
    {
        return;
    }
    m_dialog = PDFAppCopyToPagesDialog::open(controller, getDialogParent());
}

void PDFAppAnnotationEditCommandHandler::onMenuRequested(const PDFAnnotationMenuRequest& request)
{
    if (m_menus && !getOpenDialog())
    {
        m_menus->showMenu(request);
    }
}

}   // namespace pdf
