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

#include "pdfapptooldocumentpage.h"

#include "pdfapphelpcatalog.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptooltab.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolresultwriter.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"

namespace pdf
{

PDFAppToolDocumentPage::PDFAppToolDocumentPage(PDFAppContext* context, const QString& toolId, QWidget* parent) :
    BaseClass(context, parent),
    m_toolId(toolId)
{
    setObjectName(QStringLiteral("toolDocumentPage"));

    if (PDFAppDocumentTab* tab = getDocumentTab())
    {
        m_host = new PDFAppDocumentToolHost(tab, this);
    }
}

PDFAppToolDocumentPage::~PDFAppToolDocumentPage()
{
    // The shell leaves the mode while the page is whole
    PDFDocumentToolShell* shell = m_shell.data();
    m_shell = nullptr;
    delete shell;
}

QString PDFAppToolDocumentPage::getTitle() const
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    return tool ? tool->getTitle() : BaseClass::getTitle();
}

QString PDFAppToolDocumentPage::getHelpTopic() const
{
    // One override for all the tools: the topic of the tool, without it the page Tools
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    const std::shared_ptr<const PDFAppHelpCatalog> catalog = PDFAppHelpCatalog::getBuiltin();
    return tool && catalog ? catalog->getToolTopic(*tool) : QStringLiteral("tools");
}

bool PDFAppToolDocumentPage::prepareLeave(const PDFAppRoute& target)
{
    if (!m_shell)
    {
        return true;
    }

    // The tool, which is open already, stays as it is and its tab comes to the front. A tool
    // closed already goes on (Then continue with the same tool opens it anew).
    if (target == getRoute() && m_shell->getState() != PDFDocumentToolShell::State::Closed)
    {
        if (m_host)
        {
            m_host->activate();
        }
        return false;
    }

    // The shell may ask (a run) and anything may be deleted meanwhile
    QPointer<PDFAppToolDocumentPage> guard(this);
    if (!m_shell->confirmLeave())
    {
        return false;
    }
    if (!guard)
    {
        return false;
    }

    if (m_shell)
    {
        m_shell->close();
    }
    return guard != nullptr;
}

PDFAppRoute PDFAppToolDocumentPage::getCurrentRoute() const
{
    return getRoute();
}

bool PDFAppToolDocumentPage::isRouteOfPage(const PDFAppRoute& route) const
{
    const PDFAppDocumentTab* tab = getDocumentTab();
    const QStringList& segments = route.getSegments();
    if (!tab || segments.size() != 4 || segments[1] != tab->getDocumentId() || segments[3] != m_toolId)
    {
        return false;
    }

    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    if (!tool || tool->getLockedAccess() == PDFDocumentToolLockedAccess::NoDocument)
    {
        return false;
    }

    // T3: the panels and the catalog offer the tool with the reason, a direct route is
    // only a manual one
    return !isRefusedByLock(m_toolId, tab->getDocument());
}

bool PDFAppToolDocumentPage::isRefusedByLock(const QString& toolId, const PDFAppDocument* document)
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    return tool && document && document->isLocked() && tool->getLockedAccess() == PDFDocumentToolLockedAccess::Unavailable;
}

QString PDFAppToolDocumentPage::getLockRefusalText(const QString& toolId)
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    return tr("The document is locked, so %1 cannot change it.").arg(tool ? tool->getTitle() : QString());
}

QString PDFAppToolDocumentPage::getRefusalText() const
{
    const PDFAppDocumentTab* tab = getDocumentTab();
    const PDFAppDocument* document = tab ? tab->getDocument() : nullptr;
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    if (!document || !tool)
    {
        return tr("This tool cannot run on this document.");
    }

    // The lock of a file comes only when the document is shown, after setRoute
    switch (PDFAppToolResultWriter::getDocumentRefusal(tool, document))
    {
        case PDFAppToolResultWriter::DocumentRefusal::Locked:
            return getLockRefusalText(m_toolId);
        case PDFAppToolResultWriter::DocumentRefusal::DisplayVersion:
            return PDFAppDocument::getPagedXFARefusalText();
        case PDFAppToolResultWriter::DocumentRefusal::None:
            break;
    }
    return QString();
}

void PDFAppToolDocumentPage::applyRoute()
{
    // The same route again (a restored entry of the history): the tool stays
    if (m_shell)
    {
        return;
    }

    PDFAppDocumentTab* tab = getDocumentTab();
    const std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(m_toolId);
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(m_toolId);
    if (!tab || !m_host || !tool || !factory)
    {
        leaveToDocument(tr("This tool is not available."));
        return;
    }

    const QString refusal = getRefusalText();
    if (!refusal.isEmpty())
    {
        leaveToDocument(refusal);
        return;
    }

    // A tool with its own tab (Merge, plan of step 2 of phase 2, 4.1): the panel Tools, Then
    // continue with and this route open a new tab of the tool with this document as its
    // first input; the route of the tool leaves the history of the document tab. A tool with a
    // document host (Manage Pages, plan of step 3b, QB1 B) runs here, over this document.
    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(m_toolId);
    if (entry && entry->hasOwnTab() && !entry->hasDocumentHost())
    {
        openOwnTab();
        return;
    }

    m_shell = new PDFDocumentToolShell(m_host, tool, factory->createGUI(), this);
    connect(m_shell.data(), &PDFDocumentToolShell::closeRequested, this, &PDFAppToolDocumentPage::onCloseRequested);

    QPointer<PDFAppToolDocumentPage> guard(this);
    const bool isEntered = m_shell->enter();
    if (guard && !isEntered)
    {
        PDFDocumentToolShell* shell = m_shell.data();
        m_shell = nullptr;
        delete shell;
        if (guard)
        {
            leaveToDocument(tr("This tool cannot run on this document."));
        }
    }
}

void PDFAppToolDocumentPage::openOwnTab()
{
    PDFAppDocumentTab* tab = getDocumentTab();
    PDFAppMainWindow* mainWindow = tab ? qobject_cast<PDFAppMainWindow*>(tab->window()) : nullptr;
    if (!tab || !mainWindow)
    {
        leaveToDocument(tr("This tool is not available."));
        return;
    }

    // The new tab shows its route after this navigation; a slot may delete this page
    QPointer<PDFAppToolDocumentPage> guard(this);
    PDFAppToolTab* toolTab = mainWindow->createToolTabForDocument(m_toolId, tab);
    if (guard)
    {
        leaveToDocument(toolTab ? QString() : tr("This tool is not available."));
    }
}

void PDFAppToolDocumentPage::leaveToDocument(const QString& message)
{
    PDFAppToolCloseRequest request;
    request.reason = PDFAppToolCloseRequest::Reason::Refused;
    request.statusMessage = message;
    onCloseRequested(request);
}

void PDFAppToolDocumentPage::onCloseRequested(const PDFAppToolCloseRequest& request)
{
    // The signal may come from a button of a part: the parts live until the page goes
    const PDFAppToolCloseRequest requestCopy = request;
    QPointer<PDFAppToolDocumentPage> guard(this);
    QPointer<PDFAppDocumentTab> tab = getDocumentTab();
    if (!tab)
    {
        return;
    }

    // The view gets its state back first: the route of the document is read from it
    if (m_shell)
    {
        m_shell->close();
        if (!guard || !tab)
        {
            return;
        }
    }

    if (m_host && !requestCopy.statusMessage.isEmpty())
    {
        m_host->showTransientMessage(requestCopy.statusMessage);
    }

    // An address of a folder, which the navigator checks (the page is still connected)
    if (!requestCopy.folderToOpen.isEmpty())
    {
        Q_EMIT externalUrlRequested(QUrl::fromLocalFile(requestCopy.folderToOpen));
        if (!guard || !tab)
        {
            return;
        }
    }

    const QString documentId = tab->getDocumentId();
    const bool isContinuedHere = !requestCopy.continueToolId.isEmpty() && requestCopy.continueDocumentId == documentId;
    if (isContinuedHere)
    {
        // F15: the next tool takes the place of this one in the history
        requestNavigation(PDFAppRoutes::documentTool(documentId, requestCopy.continueToolId), PDFAppNavigationMode::Replace);
        return;
    }

    // The route of the document as the view shows it now, without the route of the tool
    const PDFAppRoute documentRoute = PDFAppDocumentPage::getCurrentRoute();
    const PDFAppRoute route = documentRoute.isValid() && documentRoute.getSegments().size() == 2 ? documentRoute : PDFAppRoutes::document(documentId);

    // Back never leads to this tool (F15): the entry is replaced by the document, or the
    // entry of the document before it is taken - the history never has the document
    // twice next to each other (O25)
    const bool isContinuedElsewhere = !requestCopy.continueToolId.isEmpty() && !requestCopy.continueDocumentId.isEmpty();
    const PDFAppRoute continueRoute = isContinuedElsewhere ? PDFAppRoutes::documentTool(requestCopy.continueDocumentId, requestCopy.continueToolId) : PDFAppRoute();
    const PDFAppHistory& history = tab->getHistory();
    const PDFAppHistoryEntry* previousEntry = history.getEntry(history.getCurrentIndex() - 1);
    if (previousEntry && previousEntry->route.getSegments() == route.getSegments())
    {
        if (!requestBack())
        {
            return;
        }
    }
    else if (!requestNavigation(route, PDFAppNavigationMode::Replace))
    {
        return;
    }

    // The page is not connected to the navigator any more: the tab asks for the route
    if (isContinuedElsewhere && tab)
    {
        Q_EMIT tab->navigationRequested(continueRoute, PDFAppNavigationMode::Push);
    }
}

PDFAppPage* PDFAppToolDocumentPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    Q_UNUSED(context);
    Q_UNUSED(parent);
    return nullptr;
}

PDFAppPage* PDFAppToolDocumentPageFactory::createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const
{
    const QString toolId = PDFAppToolRoutePageFactory::getToolId(PDFAppToolPageKind::Document, route);
    if (!PDFDocumentToolRegistry::isImplemented(toolId) || !PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        return nullptr;
    }
    return new PDFAppToolDocumentPage(context, toolId, parent);
}

}   // namespace pdf

#include "moc_pdfapptooldocumentpage.cpp"
