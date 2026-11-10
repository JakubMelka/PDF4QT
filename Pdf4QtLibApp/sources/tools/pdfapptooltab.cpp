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

#include "pdfapptooltab.h"

#include "pdfappcommand.h"
#include "pdfappdocumentdialogs.h"
#include "pdfapphelpcatalog.h"
#include "pdfappmainwindow.h"
#include "pdfappowntabtoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolpageregistry.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"

#include <QUrl>
#include <QVBoxLayout>

namespace pdf
{

PDFAppToolTab::PDFAppToolTab(PDFAppContext* context, const QString& toolId, const QString& instanceId, PDFAppToolInputList* inputList, QWidget* parent) :
    BaseClass(PDFAppTabKind::Tool, context, parent),
    m_toolId(toolId),
    m_instanceId(instanceId)
{
    setObjectName(QStringLiteral("toolTab"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_pageArea = new QWidget(this);
    m_pageArea->setObjectName(QStringLiteral("pageArea"));
    m_pageLayout = new QVBoxLayout(m_pageArea);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(0);
    layout->addWidget(m_pageArea);

    // The tab owns the inputs (R13: taken over in this one call), a child after the page area:
    // QObject deletes its children in their order, so the page with the shell and the GUI
    // goes first and the inputs after it (the GUI holds the list by a QPointer)
    PDFAppToolInputList* list = inputList ? inputList : new PDFAppToolInputList(this);
    list->setParent(this);
    m_inputList = list;
}

// Nothing is deleted in the body: a signal of a child ending in the body (the shell, the view)
// could reach a slot, which deletes this tab, still of its class (the lifetime sweep found it)
PDFAppToolTab::~PDFAppToolTab() = default;

PDFAppToolInputList* PDFAppToolTab::getInputList() const
{
    return m_inputList.data();
}

PDFAppRoute PDFAppToolTab::getRoute() const
{
    return PDFAppRoutes::toolTab(m_toolId, m_instanceId);
}

void PDFAppToolTab::showRoute()
{
    Q_EMIT navigationRequested(getRoute(), PDFAppNavigationMode::Push);
}

PDFAppToolTabPage* PDFAppToolTab::getToolPage() const
{
    return qobject_cast<PDFAppToolTabPage*>(getPage());
}

PDFDocumentToolShell* PDFAppToolTab::getShell() const
{
    const PDFAppToolTabPage* page = getToolPage();
    return page ? page->getShell() : nullptr;
}

QString PDFAppToolTab::getTabTitle() const
{
    // The GUI of the tool names its tab (Blank PDF "New document", K1), the catalog before it
    const PDFDocumentToolShell* shell = getShell();
    if (const PDFDocumentToolGUI* gui = shell ? shell->getGUI() : nullptr)
    {
        const QString title = gui->getOwnTabTitle();
        if (!title.isEmpty())
        {
            return title;
        }
    }
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    return tool ? tool->getTitle() : m_toolId;
}

QString PDFAppToolTab::getTabIconName() const
{
    const PDFDocumentToolShell* shell = getShell();
    if (const PDFDocumentToolGUI* gui = shell ? shell->getGUI() : nullptr)
    {
        const QString iconName = gui->getOwnTabIconName();
        if (!iconName.isEmpty())
        {
            return iconName;
        }
    }
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    return tool ? tool->getIconName() : QStringLiteral("tools");
}

bool PDFAppToolTab::confirmClose()
{
    if (m_isCloseConfirmed)
    {
        return true;
    }

    PDFAppToolTabPage* page = getToolPage();
    if (!page)
    {
        return true;
    }
    if (m_isAskingClose)
    {
        return false;
    }

    // The page asks (a dialog with its own event loop): the tab may be deleted meanwhile
    QPointer<PDFAppToolTab> guard(this);
    m_isAskingClose = true;
    const bool isConfirmed = page->confirmClose();
    if (!guard)
    {
        return false;
    }
    m_isAskingClose = false;
    return isConfirmed;
}

bool PDFAppToolTab::executeCommand(const QString& commandId)
{
    PDFAppToolTabPage* page = getToolPage();
    PDFAppOwnTabToolHost* host = page ? page->getHost() : nullptr;
    if (!host || !host->isCommandEnabled(commandId))
    {
        return false;
    }

    // The target may change the parameters, whose slots may delete the tab
    host->requestCommand(commandId);
    return true;
}

bool PDFAppToolTab::isCommandEnabled(const QString& commandId) const
{
    const PDFAppToolTabPage* page = getToolPage();
    const PDFAppOwnTabToolHost* host = page ? page->getHost() : nullptr;
    return host && host->isCommandEnabled(commandId);
}

bool PDFAppToolTab::isTabUnsaved() const
{
    return !getLossText().isEmpty();
}

QString PDFAppToolTab::getLossText() const
{
    const PDFDocumentToolShell* shell = getShell();
    const PDFDocumentToolGUI* gui = shell ? shell->getGUI() : nullptr;
    return gui && shell->getState() != PDFDocumentToolShell::State::Closed ? gui->getOwnTabLossText() : QString();
}

qreal PDFAppToolTab::getTabProgress() const
{
    const PDFDocumentToolShell* shell = getShell();
    const PDFDocumentToolRunner* runner = shell ? shell->getRunner() : nullptr;
    if (!runner || shell->getState() != PDFDocumentToolShell::State::Running)
    {
        return -1.0;
    }
    return qBound(0, runner->getProgress(), 100) / 100.0;
}

QWidget* PDFAppToolTab::getPageArea() const
{
    return m_pageArea.data();
}

QVBoxLayout* PDFAppToolTab::getPageLayout() const
{
    return m_pageLayout.data();
}

void PDFAppToolTab::onPageChanged()
{
    // No signal here: the navigator runs on after PDFAppTab::setPage (a slot could delete this
    // tab under it - the lifetime sweep, site of tabStateChanged in setPage) and it updates the
    // tab bar itself (PDFAppMainWindow::updateTab)
    BaseClass::onPageChanged();
    if (PDFAppToolTabPage* page = getToolPage())
    {
        connect(page, &PDFAppToolTabPage::stateChanged, this, &PDFAppToolTab::onPageStateChanged, Qt::UniqueConnection);
    }
}

void PDFAppToolTab::onPageStateChanged()
{
    Q_EMIT tabStateChanged();
}

// ------------------------------------------------------------------ page

PDFAppToolTabPage::PDFAppToolTabPage(PDFAppContext* context, const QString& toolId, QWidget* parent) :
    BaseClass(context, parent),
    m_toolId(toolId)
{
    setObjectName(QStringLiteral("toolTabPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_view = new PDFAppToolTabView(this);
    layout->addWidget(m_view);
}

// The shell goes with the children (after the body, the page is no longer of its class): a signal
// of its end (close, the mode left by the view) could reach a slot, which deletes this page or
// its tab while the body runs (the lifetime sweep found it). The view, an older child, goes first
// with the parts in it; the shell holds the parts and the host by QPointer.
PDFAppToolTabPage::~PDFAppToolTabPage() = default;

PDFAppToolTab* PDFAppToolTabPage::getToolTab() const
{
    for (QObject* object = parent(); object; object = object->parent())
    {
        if (PDFAppToolTab* tab = qobject_cast<PDFAppToolTab*>(object))
        {
            return tab;
        }
    }
    return nullptr;
}

QString PDFAppToolTabPage::getTitle() const
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    return tool ? tool->getTitle() : m_toolId;
}

QString PDFAppToolTabPage::getHelpTopic() const
{
    // F1: the topic of the tool (the row of the first batch, chapter 5, for this route too)
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(m_toolId);
    const std::shared_ptr<const PDFAppHelpCatalog> catalog = PDFAppHelpCatalog::getBuiltin();
    return tool && catalog ? catalog->getToolTopic(*tool) : QStringLiteral("tools");
}

bool PDFAppToolTabPage::setRoute(const PDFAppRoute& route)
{
    if (!BaseClass::setRoute(route))
    {
        return false;
    }

    // Only the route of the instance of this tab, a tool with its own tab and its GUI
    const PDFAppToolTab* tab = getToolTab();
    if (!tab || route != tab->getRoute() || tab->getToolId() != m_toolId || !PDFAppToolTabPageFactory::hasOwnTabPage(m_toolId))
    {
        return false;
    }

    // The same route again (a restored entry of the history): the tool stays
    if (m_shell)
    {
        return true;
    }
    return startShell();
}

bool PDFAppToolTabPage::startShell()
{
    PDFAppToolTab* tab = getToolTab();
    const std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(m_toolId);
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(m_toolId);
    if (!tab || !m_view || !tool || !factory)
    {
        return false;
    }

    if (!m_host)
    {
        m_host = new PDFAppOwnTabToolHost(tab, m_view, this);
    }

    PDFDocumentToolShell* shell = new PDFDocumentToolShell(m_host, tool, factory->createGUI(), this);
    m_shell = shell;
    connect(shell, &PDFDocumentToolShell::closeRequested, this, &PDFAppToolTabPage::onCloseRequested);
    connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppToolTabPage::onShellStateChanged);

    // The entry runs the code of the tool: anything may be deleted meanwhile
    QPointer<PDFAppToolTabPage> guard(this);
    const bool isEntered = shell->enter();
    if (!guard)
    {
        return false;
    }
    if (!isEntered)
    {
        deleteShell();
        return false;
    }

    if (m_shell)
    {
        if (PDFDocumentToolGUI* gui = m_shell->getGUI())
        {
            connect(gui, &PDFDocumentToolGUI::ownTabStateChanged, this, &PDFAppToolTabPage::stateChanged);
        }
        if (PDFDocumentToolRunner* runner = m_shell->getRunner())
        {
            connect(runner, &PDFDocumentToolRunner::runProgress, this, &PDFAppToolTabPage::onProgressChanged);
        }
    }

    // The title and the icon of the GUI: the window reads them (the tab bar)
    Q_EMIT stateChanged();
    return guard != nullptr;
}

void PDFAppToolTabPage::deleteShell()
{
    // Each pointer is cleared first: a slot of the end may ask this page
    PDFDocumentToolShell* shell = m_shell.data();
    m_shell = nullptr;
    if (shell)
    {
        shell->disconnect(this);
        delete shell;
    }
}

bool PDFAppToolTabPage::prepareLeave(const PDFAppRoute& target)
{
    if (!m_shell)
    {
        return true;
    }

    // The tool, which is open already, stays as it is and its tab comes to the front
    if (target == getRoute() && m_shell->getState() != PDFDocumentToolShell::State::Closed)
    {
        if (m_host)
        {
            m_host->activate();
        }
        return false;
    }

    // The shell may ask (a run) and anything may be deleted meanwhile
    QPointer<PDFAppToolTabPage> guard(this);
    if (!m_shell->confirmLeave() || !guard)
    {
        return false;
    }
    if (m_shell)
    {
        m_shell->close();
    }
    return guard != nullptr;
}

bool PDFAppToolTabPage::confirmClose()
{
    if (!m_shell)
    {
        return true;
    }

    QPointer<PDFAppToolTabPage> guard(this);
    switch (m_shell->getState())
    {
        case PDFDocumentToolShell::State::Created:
        case PDFDocumentToolShell::State::Closed:
            return true;

        case PDFDocumentToolShell::State::Deciding:
            // The modal result dialog decides first (F2)
            return false;

        case PDFDocumentToolShell::State::Running:
        {
            // Stop and Leave / Keep Running (F14, close-running 955:145813): Stop stops the run
            // now, the tool ends with the tab (a quit canceled later keeps the tool editing)
            if (!m_shell->confirmLeave() || !guard)
            {
                return false;
            }
            if (m_shell)
            {
                m_shell->stop();
            }
            return guard != nullptr;
        }

        case PDFDocumentToolShell::State::Editing:
            break;
    }

    // K16: the inputs are lost by the close (no frame: a deviation named in the report)
    const PDFAppToolTab* tab = getToolTab();
    const QString lossText = tab ? tab->getLossText() : QString();
    if (!lossText.isEmpty())
    {
        if (m_host)
        {
            m_host->activate();
            if (!guard)
            {
                return false;
            }
        }

        // The question belongs to this page (as the questions of a document tab to their tab):
        // it goes with the page, the tab or the window, never left open for a deleted tool
        const QString title = tab->getTabTitle();
        PDFAppDocumentChoiceDialog* dialog = new PDFAppDocumentChoiceDialog(PDFThemeIcons::Badge::Warning, tr("Close %1?").arg(title.left(256)), lossText, 440, this);
        dialog->setObjectName(QStringLiteral("toolTabCloseDialog"));
        dialog->addChoice(tr("Close Tab"), PDFAppDocumentChoice::Discard, false, true, QStringLiteral("closeTabButton"));
        dialog->addChoice(tr("Cancel"), PDFAppDocumentChoice::Cancel, true, false, QStringLiteral("cancelButton"));
        const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(dialog);
        if (!guard || choice != PDFAppDocumentChoice::Discard)
        {
            return false;
        }
    }

    // The tool ends with the tab (its page deletes the shell); nothing is changed here, so a
    // quit of the window canceled by a later question keeps the tool as it is
    return true;
}

void PDFAppToolTabPage::onShellStateChanged()
{
    // Undo and Redo of the tool are off while it runs and while its result is decided (the
    // host tells it the toolbar and the window: their slots run code of the application)
    QPointer<PDFAppToolTabPage> guard(this);
    if (m_host && m_shell)
    {
        const PDFDocumentToolShell::State state = m_shell->getState();
        m_host->setBusy(state == PDFDocumentToolShell::State::Running || state == PDFDocumentToolShell::State::Deciding);
    }
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppToolTabPage::onProgressChanged()
{
    Q_EMIT stateChanged();
}

void PDFAppToolTabPage::onCloseRequested(const PDFAppToolCloseRequest& request)
{
    // The signal may come from a button of a part or from the result dialog: the tab and
    // the shell go only by deleteLater, the close of the tab is the last statement (R17)
    const PDFAppToolCloseRequest requestCopy = request;
    QPointer<PDFAppToolTabPage> guard(this);
    QPointer<PDFAppToolTab> tab = getToolTab();
    QPointer<PDFAppMainWindow> window = tab ? qobject_cast<PDFAppMainWindow*>(tab->window()) : nullptr;
    if (!tab || !window)
    {
        return;
    }

    switch (requestCopy.reason)
    {
        case PDFAppToolCloseRequest::Reason::Canceled:
        {
            // Cancel closes the tab: the window asks, when the inputs are lost (K16). A refused
            // close keeps the tool, which takes its requests again.
            const bool isClosed = window->closeTab(tab);
            if (!isClosed && guard && m_shell)
            {
                m_shell->resumeEditing();
            }
            return;
        }

        case PDFAppToolCloseRequest::Reason::Discarded:
            // The shell of an own tab keeps the tool after Discard result (Q9): never here
            return;

        case PDFAppToolCloseRequest::Reason::Lost:
        case PDFAppToolCloseRequest::Reason::Refused:
            // Nothing of the tool is left (its parts have gone): the tab goes without a question
            if (!closeShell() || !tab || !window)
            {
                return;
            }
            tab->setCloseConfirmed(true);
            window->closeTab(tab);
            return;

        case PDFAppToolCloseRequest::Reason::Accepted:
            break;
    }

    // Blank PDF (4.6): the new document replaces this tab at its place, unsaved
    if (!requestCopy.newDocumentData.isEmpty())
    {
        if (!closeShell() || !tab || !window)
        {
            return;
        }
        window->replaceToolTab(tab, requestCopy.newDocumentData, requestCopy.newDocumentTitle);
        return;
    }

    // "Save as a file" of a tool, whose result replaces its tab (336:3572, R16): the written file
    // opens right after this tab (the current one), then this tab goes - the document takes its
    // place in the tab bar
    if (!requestCopy.newDocumentFilePath.isEmpty())
    {
        if (!closeShell() || !tab || !window)
        {
            return;
        }
        window->openDocuments(QStringList({ requestCopy.newDocumentFilePath }), 0);
        if (!guard || !tab || !window)
        {
            return;
        }
        tab->setCloseConfirmed(true);
        window->closeTab(tab);
        return;
    }

    // An address of a folder, which the navigator checks (the page is still connected)
    if (!requestCopy.folderToOpen.isEmpty())
    {
        Q_EMIT externalUrlRequested(QUrl::fromLocalFile(requestCopy.folderToOpen));
        if (!guard || !tab || !window)
        {
            return;
        }
    }

    // R17: Then continue with starts on the new tab first, then this tab closes (Q9)
    if (!requestCopy.continueToolId.isEmpty() && !requestCopy.continueDocumentId.isEmpty())
    {
        if (PDFAppNavigator* navigator = window->getNavigator())
        {
            navigator->navigate(PDFAppRoutes::documentTool(requestCopy.continueDocumentId, requestCopy.continueToolId), PDFAppNavigationMode::Push);
        }
        if (!guard || !tab || !window)
        {
            return;
        }
    }

    // The tool ends before its tab is deleted later: nothing of it is reported from the
    // destruction of the tab (a slot of it could delete the tab a second time)
    if (!closeShell() || !tab || !window)
    {
        return;
    }
    tab->setCloseConfirmed(true);
    window->closeTab(tab);
}

bool PDFAppToolTabPage::closeShell()
{
    // The close emits (the mode left, the state): anything may be deleted meanwhile
    QPointer<PDFAppToolTabPage> guard(this);
    if (m_shell)
    {
        m_shell->close();
    }
    return guard != nullptr;
}

// ------------------------------------------------------------------ factory

PDFAppPage* PDFAppToolTabPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    Q_UNUSED(context);
    Q_UNUSED(parent);
    return nullptr;
}

PDFAppPage* PDFAppToolTabPageFactory::createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const
{
    const QString toolId = PDFAppToolRoutePageFactory::getToolId(PDFAppToolPageKind::OwnTab, route);
    if (!hasOwnTabPage(toolId))
    {
        return nullptr;
    }
    return new PDFAppToolTabPage(context, toolId, parent);
}

bool PDFAppToolTabPageFactory::hasOwnTabPage(const QString& toolId)
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId);
    return tool && tool->hasOwnTab() && PDFDocumentToolRegistry::isImplemented(toolId) && PDFDocumentToolGUIRegistry::getFactory(toolId);
}

}   // namespace pdf

#include "moc_pdfapptooltab.cpp"
