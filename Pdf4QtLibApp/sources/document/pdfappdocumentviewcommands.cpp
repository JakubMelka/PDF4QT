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

#include "pdfappdocumentviewcommands.h"
#include "pdfappannotationcontroller.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfsettingdefinitions.h"

#include "pdfdrawspacecontroller.h"
#include "pdfwidgettool.h"

namespace pdf
{

/// Values of the commands of the view
class PDFAppDocumentViewCommandsHelper
{
public:
    PDFAppDocumentViewCommandsHelper() = delete;

    /// Value of viewing.colorMode of a color mode command (empty for another command)
    static QString getColorMode(const QString& commandId)
    {
        using C = PDFAppCommands;

        if (commandId == C::colorModeNormal()) { return QStringLiteral("normal"); }
        if (commandId == C::colorModeInverted()) { return QStringLiteral("inverted"); }
        if (commandId == C::colorModeGrayscale()) { return QStringLiteral("grayscale"); }
        if (commandId == C::colorModeHighContrast()) { return QStringLiteral("highContrast"); }
        if (commandId == C::colorModeBitonal()) { return QStringLiteral("bitonal"); }
        if (commandId == C::colorModeCustom()) { return QStringLiteral("customColors"); }
        return QString();
    }

    /// The command moves in the document (needs pages)
    static bool isNavigation(const QString& commandId)
    {
        using C = PDFAppCommands;
        return commandId == C::firstPage() || commandId == C::lastPage() || commandId == C::nextPage() || commandId == C::previousPage() ||
               commandId == C::nextLine() || commandId == C::previousLine() || commandId == C::goToPage();
    }
};

PDFAppDocumentViewCommandHandler::PDFAppDocumentViewCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::View, tab)
{
    // The view of the tab exists before its handlers and lives longer than them
    if (PDFAppDocumentView* view = getView())
    {
        connect(view, &PDFAppDocumentView::viewStateChanged, this, &PDFAppDocumentViewCommandHandler::stateChanged);
    }
}

PDFAppDocumentViewCommandHandler::~PDFAppDocumentViewCommandHandler() = default;

int PDFAppDocumentViewCommandHandler::getProxyOperation(const QString& commandId)
{
    using C = PDFAppCommands;
    using Operation = PDFDrawWidgetProxy::Operation;

    if (commandId == C::zoomIn()) { return Operation::ZoomIn; }
    if (commandId == C::zoomOut()) { return Operation::ZoomOut; }
    if (commandId == C::fitPage()) { return Operation::ZoomFit; }
    if (commandId == C::fitWidth()) { return Operation::ZoomFitWidth; }
    if (commandId == C::fitHeight()) { return Operation::ZoomFitHeight; }
    if (commandId == C::rotateViewLeft()) { return Operation::RotateLeft; }
    if (commandId == C::rotateViewRight()) { return Operation::RotateRight; }
    if (commandId == C::firstPage()) { return Operation::NavigateDocumentStart; }
    if (commandId == C::lastPage()) { return Operation::NavigateDocumentEnd; }
    if (commandId == C::nextPage()) { return Operation::NavigateNextPage; }
    if (commandId == C::previousPage()) { return Operation::NavigatePreviousPage; }
    if (commandId == C::nextLine()) { return Operation::NavigateNextStep; }
    if (commandId == C::previousLine()) { return Operation::NavigatePreviousStep; }
    return -1;
}

int PDFAppDocumentViewCommandHandler::getViewLayout(const QString& commandId)
{
    using C = PDFAppCommands;

    if (commandId == C::layoutSinglePage()) { return int(PDFAppViewLayout::SinglePage); }
    if (commandId == C::layoutContinuous()) { return int(PDFAppViewLayout::Continuous); }
    if (commandId == C::layoutTwoPages()) { return int(PDFAppViewLayout::TwoPages); }
    if (commandId == C::layoutTwoColumns()) { return int(PDFAppViewLayout::TwoColumns); }
    return -1;
}

int PDFAppDocumentViewCommandHandler::getCanvasTool(const QString& commandId)
{
    using C = PDFAppCommands;

    if (commandId == C::handTool()) { return PDFToolManager::ToolEnd; }
    if (commandId == C::selectText()) { return PDFToolManager::SelectTextTool; }
    if (commandId == C::selectTable()) { return PDFToolManager::SelectTableTool; }
    if (commandId == C::screenshot()) { return PDFToolManager::ScreenshotTool; }
    if (commandId == C::extractImage()) { return PDFToolManager::ExtractImageTool; }
    if (commandId == C::magnifier()) { return PDFToolManager::MagnifierTool; }
    return -1;
}

void PDFAppDocumentViewCommandHandler::execute(const QString& commandId)
{
    using C = PDFAppCommands;

    PDFAppDocumentView* view = getView();
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    if (!view || !proxy)
    {
        return;
    }

    // The view emits viewStateChanged, when a state of a command changes
    const int operation = getProxyOperation(commandId);
    if (operation >= 0)
    {
        proxy->performOperation(static_cast<PDFDrawWidgetProxy::Operation>(operation));
        return;
    }

    const int layout = getViewLayout(commandId);
    if (layout >= 0)
    {
        view->setViewLayout(static_cast<PDFAppViewLayout>(layout));
        return;
    }

    const int tool = getCanvasTool(commandId);
    if (tool >= 0)
    {
        view->setCanvasTool(tool);
        return;
    }

    const QString colorMode = PDFAppDocumentViewCommandsHelper::getColorMode(commandId);
    if (!colorMode.isEmpty())
    {
        view->setViewSetting(PDFSettingKeys::viewingColorMode(), colorMode);
        return;
    }

    // Step 4: with selected annotations, Copy / Select all / Deselect work on them
    // (the keys reach the annotation manager first - its shortcut override - so a
    // key is never handled twice)
    PDFAppAnnotationController* annotations = view->getAnnotationController();
    const bool hasAnnotationSelection = annotations && annotations->hasAnnotationSelection();

    if (commandId == C::copy())
    {
        if (hasAnnotationSelection)
        {
            annotations->copySelection();
        }
        else
        {
            view->copySelectedText();
        }
    }
    else if (commandId == C::selectAll())
    {
        if (hasAnnotationSelection)
        {
            annotations->selectAll();
        }
        else
        {
            view->selectAllText();
        }
    }
    else if (commandId == C::deselect())
    {
        if (hasAnnotationSelection)
        {
            annotations->clearAnnotationSelection();
        }
        else
        {
            view->deselectText();
        }
    }
    else if (commandId == C::actualSize())
    {
        view->setZoom(1.0);
    }
    else if (commandId == C::firstPageRight())
    {
        view->setFirstPageRight(!view->isFirstPageRight());
    }
    else if (commandId == C::showAnnotations())
    {
        view->setViewSetting(PDFSettingKeys::viewingShowAnnotations(), !view->isShowingAnnotations());
    }
    else if (commandId == C::toggleSidebar())
    {
        view->setSidebarVisible(!view->isSidebarVisible());
    }
    else if (commandId == C::fullScreen())
    {
        view->setFullScreen(!view->isFullScreen());
    }
    else if (commandId == C::goToPage())
    {
        view->openGoToPage();
    }
}

bool PDFAppDocumentViewCommandHandler::isEnabled(const QString& commandId) const
{
    using C = PDFAppCommands;

    if (!BaseClass::isEnabled(commandId))
    {
        return false;
    }

    const PDFAppDocumentView* view = getView();
    if (!view || !view->getProxy())
    {
        return false;
    }

    const bool hasPages = view->getPageCount() > 0;
    if (commandId == C::copy() || commandId == C::deselect())
    {
        const PDFAppAnnotationController* annotations = view->getAnnotationController();
        return view->hasTextSelection() || (annotations && annotations->hasAnnotationSelection());
    }
    if (commandId == C::selectAll() || PDFAppDocumentViewCommandsHelper::isNavigation(commandId) || getCanvasTool(commandId) >= 0)
    {
        return hasPages;
    }
    if (commandId == C::showAnnotations() || !PDFAppDocumentViewCommandsHelper::getColorMode(commandId).isEmpty())
    {
        // The settings of the application
        return view->getContext() != nullptr;
    }
    if (commandId == C::fullScreen())
    {
        // Only a view inside of a main window covers the screen
        return view->isFullScreen() || (qobject_cast<const PDFAppMainWindow*>(view->window()) && view->isVisible());
    }
    return true;
}

bool PDFAppDocumentViewCommandHandler::isChecked(const QString& commandId) const
{
    using C = PDFAppCommands;

    const PDFAppDocumentView* view = getView();
    if (!view)
    {
        return false;
    }

    const int layout = getViewLayout(commandId);
    if (layout >= 0)
    {
        return int(view->getViewLayout()) == layout;
    }

    const int tool = getCanvasTool(commandId);
    if (tool >= 0)
    {
        return view->isCanvasToolActive(tool);
    }

    const QString colorMode = PDFAppDocumentViewCommandsHelper::getColorMode(commandId);
    if (!colorMode.isEmpty())
    {
        return view->getColorMode() == colorMode;
    }

    if (commandId == C::firstPageRight())
    {
        return view->isFirstPageRight();
    }
    if (commandId == C::showAnnotations())
    {
        return view->isShowingAnnotations();
    }
    if (commandId == C::toggleSidebar())
    {
        return view->isSidebarVisible();
    }
    if (commandId == C::fullScreen())
    {
        return view->isFullScreen();
    }
    return false;
}

}   // namespace pdf
