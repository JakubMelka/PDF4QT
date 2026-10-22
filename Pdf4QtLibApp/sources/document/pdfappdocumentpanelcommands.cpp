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

#include "pdfappdocumentpanelcommands.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_bookmarks.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfmessagebox.h"

#include <QCoreApplication>
#include <QPointer>

#include <algorithm>

namespace pdf
{

PDFAppDocumentPanelCommandHandler::PDFAppDocumentPanelCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::Panels, tab)
{

}

PDFAppDocumentPanelCommandHandler::~PDFAppDocumentPanelCommandHandler() = default;

void PDFAppDocumentPanelCommandHandler::execute(const QString& commandId)
{
    // The commands may run dialogs, which may delete the tab: nothing of the
    // handler is used after them
    PDFAppDocumentView* view = getView();
    if (!view || !isEnabled(commandId))
    {
        return;
    }

    if (commandId == PDFAppCommands::addBookmark())
    {
        PDFAppBookmarks::addCurrentPage(view, view);
    }
    else if (commandId == PDFAppCommands::nextBookmark())
    {
        PDFAppBookmarks::goToNext(view, true);
    }
    else if (commandId == PDFAppCommands::previousBookmark())
    {
        PDFAppBookmarks::goToNext(view, false);
    }
    else if (commandId == PDFAppCommands::exportBookmarks())
    {
        PDFAppBookmarks::exportBookmarks(view, view);
    }
    else if (commandId == PDFAppCommands::importBookmarks())
    {
        PDFAppBookmarks::importBookmarks(view, view);
    }
    else if (commandId == PDFAppCommands::rotatePagesLeft() || commandId == PDFAppCommands::rotatePagesRight())
    {
        rotatePages(view->getSelectedPages(), commandId == PDFAppCommands::rotatePagesRight());
    }
}

bool PDFAppDocumentPanelCommandHandler::isEnabled(const QString& commandId) const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return false;
    }

    if (commandId == PDFAppCommands::addBookmark() || commandId == PDFAppCommands::exportBookmarks() || commandId == PDFAppCommands::importBookmarks())
    {
        const PDFAppDocumentView* view = getView();
        return PDFAppBookmarks::canStore(view ? view->getContext() : nullptr, document);
    }
    if (commandId == PDFAppCommands::nextBookmark() || commandId == PDFAppCommands::previousBookmark())
    {
        return true;
    }
    if (commandId == PDFAppCommands::rotatePagesLeft() || commandId == PDFAppCommands::rotatePagesRight())
    {
        // A locked document is not changed (also not by a running operation)
        return !document->isLocked() && !document->isBlockedByOperation() && document->getPageCount() > 0;
    }
    return false;
}

bool PDFAppDocumentPanelCommandHandler::rotatePages(const std::vector<PDFInteger>& pages, bool isRight)
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer current = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    if (!current || document->isLocked() || document->isBlockedByOperation())
    {
        return false;
    }

    const PDFCatalog* catalog = current->getCatalog();
    std::vector<PDFInteger> validPages;
    for (const PDFInteger page : pages)
    {
        if (page >= 0 && page < PDFInteger(catalog->getPageCount()))
        {
            validPages.push_back(page);
        }
    }
    std::sort(validPages.begin(), validPages.end());
    validPages.erase(std::unique(validPages.begin(), validPages.end()), validPages.end());
    if (validPages.empty())
    {
        return false;
    }

    PDFDocumentPointer rotated;
    try
    {
        PDFDocumentBuilder builder(current.data());
        for (const PDFInteger pageIndex : validPages)
        {
            const PDFPage* page = catalog->getPage(size_t(pageIndex));
            const PageRotation rotation = isRight ? getPageRotationRotatedRight(page->getPageRotation()) : getPageRotationRotatedLeft(page->getPageRotation());
            builder.setPageRotation(page->getPageReference(), rotation);
        }
        rotated = PDFDocumentPointer(new PDFDocument(builder.build()));
    }
    catch (const PDFException& exception)
    {
        PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, QCoreApplication::translate("pdf::PDFAppDocumentPanelCommandHandler", "The pages were not rotated"),
                                               exception.getMessage(), QDialogButtonBox::Ok, getView() ? getView()->window() : nullptr);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return false;
    }

    const QString description = isRight ? QCoreApplication::translate("pdf::PDFAppDocumentPanelCommandHandler", "Rotate pages right")
                                        : QCoreApplication::translate("pdf::PDFAppDocumentPanelCommandHandler", "Rotate pages left");
    const PDFModifiedDocument modified(rotated, document->getOptionalContentActivity(), PDFModifiedDocument::Reset);
    return document->commit(modified, description, document->getVersion(), PDFAppChangeSource::User);
}

}   // namespace pdf
