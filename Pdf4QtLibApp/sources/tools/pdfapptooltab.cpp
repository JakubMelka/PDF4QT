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
#include "pdfapptoolowntabquestion.h"
#include "pdfapptoolpageregistry.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfthemeiconengine.h"

#include <QPushButton>
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

PDFAppLeaveResult PDFAppToolTab::requestTabClose()
{
    if (m_isCloseConfirmed)
    {
        return PDFAppLeaveResult::Done;
    }

    QPointer<PDFAppToolTabPage> page = getToolPage();
    if (!page)
    {
        return PDFAppLeaveResult::Done;
    }

    // One flow of the tab (A0): a second request while a question is open is refused, the question comes forward
    PDFAppLeaveFlow* flow = getLeaveFlow();
    if (!flow->start(PDFAppLeaveFlow::Goal::CloseTab))
    {
        flow->raiseQuestion();
        return PDFAppLeaveResult::Refused;
    }

    // The page asks (a run, the own question of the tool, K16; shown, never exec): it may run code of the
    // application, the tab may be closed meanwhile (retired with its flow, a child: refused). The guards stay
    // (RC6-M1): the activation of the shell may delete the tab synchronously in a test.
    QPointer<PDFAppToolTab> guard(this);
    QPointer<PDFAppLeaveFlow> flowGuard(flow);
    const PDFAppLeaveResult result = page->requestClose(flow);
    if (!guard || !flowGuard || PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppLeaveResult::Refused;
    }

    switch (result)
    {
        case PDFAppLeaveResult::Pending:
            if (page && flow->waitForStep(page.data(), this, &PDFAppToolTab::onPageCloseStepFinished))
            {
                return PDFAppLeaveResult::Pending;
            }
            flow->finish(false);
            return PDFAppLeaveResult::Refused;

        case PDFAppLeaveResult::Done:
            flow->finish(true);
            return !guard || PDFDeferredDeletion::isRetired(this) ? PDFAppLeaveResult::Refused : PDFAppLeaveResult::Done;

        case PDFAppLeaveResult::Refused:
            break;
    }

    flow->finish(false);
    return PDFAppLeaveResult::Refused;
}

void PDFAppToolTab::onPageCloseStepFinished(PDFAppLeaveResult result)
{
    PDFAppLeaveFlow* flow = getLeaveFlow();
    if (!flow->isRunning())
    {
        return;
    }
    if (result != PDFAppLeaveResult::Done)
    {
        flow->finish(false);
        return;
    }

    // Every question has been answered: the tab closes by its window now (the last call; a tab closed meanwhile by a
    // slot of the end of the flow is not closed again)
    flow->finish(true);
    PDFAppMainWindow* mainWindow = PDFDeferredDeletion::isRetired(this) ? nullptr : qobject_cast<PDFAppMainWindow*>(window());
    if (!mainWindow)
    {
        return;
    }
    m_isCloseConfirmed = true;
    if (!mainWindow->closeTab(this))
    {
        m_isCloseConfirmed = false;
    }
}

bool PDFAppToolTab::isAskingClose() const
{
    return isLeaving();
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
    // A retired page (its tab closed, step A) opens nothing
    PDFAppToolTab* tab = getToolTab();
    const std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(m_toolId);
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(m_toolId);
    if (!tab || !m_view || !tool || !factory || PDFDeferredDeletion::isRetired(this))
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

    // The entry runs the code of the tool: anything may be closed meanwhile (a retired page opens nothing)
    const bool isEntered = shell->enter();
    if (PDFDeferredDeletion::isRetired(this))
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
    return !PDFDeferredDeletion::isRetired(this);
}

void PDFAppToolTabPage::deleteShell()
{
    // Each pointer is cleared first: a slot of the end may ask this page. Retired, never deleted here (step A): its
    // parts and its runner go with it from the event loop.
    PDFDocumentToolShell* shell = m_shell.data();
    m_shell = nullptr;
    if (shell)
    {
        shell->disconnect(this);
        PDFDeferredDeletion::retire(shell);
    }
}

PDFAppLeaveResult PDFAppToolTabPage::requestLeave(const PDFAppRoute& target, PDFAppLeaveFlow* flow)
{
    if (!m_shell)
    {
        return PDFAppLeaveResult::Done;
    }

    // The tool, which is open already, stays as it is and its tab comes to the front
    if (target == getRoute() && m_shell->getState() != PDFDocumentToolShell::State::Closed)
    {
        if (m_host)
        {
            m_host->activate();
        }
        return PDFAppLeaveResult::Refused;
    }
    if (checkLeaveWait())
    {
        // A question of this page is open already: the first leave decides
        return PDFAppLeaveResult::Refused;
    }

    // The page asks as at the close of its tab (a run as close-running, the inputs by K16 - owner
    // 2026-11-20: every way of leaving the tool asks the same question); anything may be deleted meanwhile
    // (only the tool closes, the tab stays: the question names the tool, not the tab); a page retired meanwhile
    // refuses (the guard: the activation of the shell may delete the tab synchronously in a test, RC6-M1)
    QPointer<PDFAppToolTabPage> guard(this);
    m_isLeavingTool = true;
    const PDFAppLeaveResult result = requestClose(flow);
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppLeaveResult::Refused;
    }
    if (result == PDFAppLeaveResult::Pending)
    {
        // The tool closes after the answer (finishCloseStep)
        return result;
    }
    m_isLeavingTool = false;
    if (result != PDFAppLeaveResult::Done)
    {
        return PDFAppLeaveResult::Refused;
    }
    if (m_shell)
    {
        m_shell->close();
    }
    return PDFDeferredDeletion::isRetired(this) ? PDFAppLeaveResult::Refused : PDFAppLeaveResult::Done;
}

bool PDFAppToolTabPage::isQuestionOpen() const
{
    return (m_leaveFlow && m_leaveFlow->isWaitingFor(this)) || (m_shell && m_shell->isQuestionOpen());
}

bool PDFAppToolTabPage::checkLeaveWait()
{
    if (m_leaveFlow && m_leaveFlow->isWaitingFor(this))
    {
        return true;
    }

    // The flow has ended without the step of this page (canceled from outside, L1 of the review of A0): the wait is
    // forgotten, the next leave asks anew (a late answer of its question finds no stage)
    m_leaveFlow = nullptr;
    m_leaveStage = LeaveStage::None;
    m_isLeavingTool = false;
    return false;
}

bool PDFAppToolTabPage::hasUnsavedWork() const
{
    const PDFAppToolTab* tab = getToolTab();
    return m_shell && m_shell->getState() == PDFDocumentToolShell::State::Editing && tab && !tab->getLossText().isEmpty();
}

PDFAppToolCloseQuestion PDFAppToolTabPage::createListCloseQuestion(int count, bool isListChanged, bool isPagesChanged, const QString& keepText, bool isTabClosing)
{
    // Owner 2026-11-20 (decision 5; RESCUX H1: the changes of the pages are lost too). The English .ts has no
    // numerus forms: the singular is a text of its own.
    PDFAppToolCloseQuestion question;
    question.title = count == 1 ? tr("Discard the list of 1 file?") : tr("Discard the list of %Ln files?", nullptr, count);
    if (isListChanged && isPagesChanged)
    {
        question.text = isTabClosing ? tr("Closing the tab removes the files you have added, their order and your changes to their pages. The files stay as they are.")
                                     : tr("Closing the tool removes the files you have added, their order and your changes to their pages. The files stay as they are.");
    }
    else if (isPagesChanged)
    {
        question.text = isTabClosing ? tr("Closing the tab removes your changes to the pages of the files. The files stay as they are.")
                                     : tr("Closing the tool removes your changes to the pages of the files. The files stay as they are.");
    }
    else
    {
        question.text = isTabClosing ? tr("Closing the tab removes the files you have added and their order. The files stay as they are.")
                                     : tr("Closing the tool removes the files you have added and their order. The files stay as they are.");
    }
    question.keepText = keepText;
    question.discardText = tr("Discard List");
    return question;
}

PDFAppToolCloseQuestion PDFAppToolTabPage::createChoiceCloseQuestion(const QString& keepText, bool isTabClosing)
{
    // Owner 2026-11-20 (decision 5): Compare
    PDFAppToolCloseQuestion question;
    question.title = tr("Discard the chosen documents?");
    question.text = isTabClosing ? tr("Closing the tab removes your choice of the documents. The files stay as they are.")
                                 : tr("Closing the tool removes your choice of the documents. The files stay as they are.");
    question.keepText = keepText;
    question.discardText = tr("Discard List");
    return question;
}

PDFAppLeaveResult PDFAppToolTabPage::requestClose(PDFAppLeaveFlow* flow)
{
    if (!m_shell)
    {
        return PDFAppLeaveResult::Done;
    }
    if (m_leaveStage != LeaveStage::None && checkLeaveWait())
    {
        // A question of this page is open already: the first leave decides
        return PDFAppLeaveResult::Refused;
    }

    // A new leave asks anew: an answer given to an earlier leave is not this one's (owner 2026-11-24)
    m_isLeaveAnswered = false;
    m_isLeaveRunStopped = false;

    // The calls run code of the application: a page retired meanwhile refuses (step A). The guard stays (RC6-M1): the
    // activation of the shell may delete the tab synchronously in a test.
    QPointer<PDFAppToolTabPage> guard(this);
    switch (m_shell->getState())
    {
        case PDFDocumentToolShell::State::Created:
        case PDFDocumentToolShell::State::Closed:
            return PDFAppLeaveResult::Done;

        case PDFDocumentToolShell::State::Deciding:
            // The modal result dialog decides first (F2)
            return PDFAppLeaveResult::Refused;

        case PDFDocumentToolShell::State::Running:
        {
            // Stop and Leave / Keep Running (F14, close-running 955:145813): Stop stops the run
            // now, the tool ends with the tab (a quit canceled later closes the tab too, finishLeave)
            QPointer<PDFDocumentToolShell> shell = m_shell;
            const PDFAppLeaveResult result = m_shell->requestLeave(flow);
            if (!guard || PDFDeferredDeletion::isRetired(this))
            {
                return PDFAppLeaveResult::Refused;
            }
            switch (result)
            {
                case PDFAppLeaveResult::Pending:
                    return waitForStep(flow, shell.data(), LeaveStage::Run);
                case PDFAppLeaveResult::Done:
                    if (m_shell)
                    {
                        m_shell->stop();
                    }
                    return !guard || PDFDeferredDeletion::isRetired(this) ? PDFAppLeaveResult::Refused : PDFAppLeaveResult::Done;
                case PDFAppLeaveResult::Refused:
                    break;
            }
            return PDFAppLeaveResult::Refused;
        }

        case PDFDocumentToolShell::State::Editing:
            break;
    }

    // RB3 c of step 3b: the tool asks its own question first (Manage Pages 405:30894 "Save the
    // assembly …?"; shown, never exec); its call runs foreign code - the page, the shell and the GUI may be gone.
    // Done = Close (nothing to lose, Don't Save, a saved assembly); Refused = Cancel, or Saving (the tool closes
    // the tab after its save, PDFDocumentToolGUI::closeOwnTab).
    if (PDFDocumentToolGUI* gui = m_shell->getGUI(); gui && gui->hasOwnTabCloseQuestion())
    {
        QPointer<PDFDocumentToolGUI> guiGuard(gui);
        const PDFAppLeaveResult result = gui->requestOwnTabClose(flow);
        if (!guard || PDFDeferredDeletion::isRetired(this) || !m_shell)
        {
            return PDFAppLeaveResult::Refused;
        }
        return result == PDFAppLeaveResult::Pending ? waitForStep(flow, guiGuard.data(), LeaveStage::OwnQuestion) : result;
    }

    return askLossQuestion(flow);
}

PDFAppLeaveResult PDFAppToolTabPage::waitForStep(PDFAppLeaveFlow* flow, QObject* reporter, LeaveStage stage)
{
    if (!flow || !reporter || !flow->waitForStep(reporter, this, &PDFAppToolTabPage::onCloseStepFinished))
    {
        return PDFAppLeaveResult::Refused;
    }
    m_leaveFlow = flow;
    m_leaveStage = stage;
    return PDFAppLeaveResult::Pending;
}

void PDFAppToolTabPage::onCloseStepFinished(PDFAppLeaveResult result)
{
    finishCloseStep(result == PDFAppLeaveResult::Done);
}

void PDFAppToolTabPage::onLossQuestionChosen(PDFAppDocumentChoice choice)
{
    if (m_leaveStage != LeaveStage::LossQuestion)
    {
        return;
    }
    finishCloseStep(choice == PDFAppDocumentChoice::Discard);
}

void PDFAppToolTabPage::finishCloseStep(bool isAccepted)
{
    // The wait ends first; the tool, the tab and the flow may have changed meanwhile (read again)
    QPointer<PDFAppLeaveFlow> flow = m_leaveFlow;
    const LeaveStage stage = m_leaveStage;
    const bool isLeavingTool = m_isLeavingTool;
    m_leaveFlow = nullptr;
    m_leaveStage = LeaveStage::None;
    m_isLeavingTool = false;
    if (!flow || !flow->isWaitingFor(this))
    {
        // Nobody waits any more (the flow has ended meanwhile): the answer changes nothing
        return;
    }

    // A page retired by the calls (step A) closes and reports nothing more (the end of the page refuses its step)
    if (isAccepted && stage == LeaveStage::Run && m_shell)
    {
        // Stop and Leave: the run stops now (the tool ends with the tab)
        m_shell->stop();
    }
    if (isAccepted && isLeavingTool && m_shell && !PDFDeferredDeletion::isRetired(this))
    {
        // A navigation leaves only the tool: it closes, the tab stays
        m_shell->close();
    }
    else if (isAccepted && m_shell && !PDFDeferredDeletion::isRetired(this))
    {
        // The closing answer of the close of the tab is final (owner 2026-11-24): a later step, which cancels the
        // leave (another question of the quit, Save All), still closes the tab (finishLeave) - with the work, which the
        // answer has seen (the end of a stopped run is no work of the user)
        m_isLeaveAnswered = true;
        m_isLeaveRunStopped = stage == LeaveStage::Run && m_shell->getState() == PDFDocumentToolShell::State::Running;
        m_leaveAnsweredWork = getLeaveWorkSignature();
    }

    // The waiter of the flow is the last call
    if (flow && !PDFDeferredDeletion::isRetired(this))
    {
        flow->finishStep(this, isAccepted ? PDFAppLeaveResult::Done : PDFAppLeaveResult::Refused);
    }
}

PDFAppLeaveResult PDFAppToolTabPage::askLossQuestion(PDFAppLeaveFlow* flow)
{
    // K16: the inputs are lost by the close (no frame: a deviation named in the report)
    const PDFAppToolTab* tab = getToolTab();
    const QString lossText = tab ? tab->getLossText() : QString();
    if (!lossText.isEmpty())
    {
        if (!flow || !flow->isRunning())
        {
            // Nobody could take the answer: the work stays
            return PDFAppLeaveResult::Refused;
        }
        if (m_host)
        {
            // The guard stays (RC6-M1): a slot of the switch of the tabs may delete the tab synchronously in a test
            QPointer<PDFAppToolTabPage> activateGuard(this);
            m_host->activate();
            if (!activateGuard || PDFDeferredDeletion::isRetired(this))
            {
                return PDFAppLeaveResult::Refused;
            }
        }

        // Owner 2026-11-20 (decision 5): the question of the tool says what is lost, in the pattern of the tools
        // in the tab of a document (435:10500: Keep as the Primary button, Discard with the icon trash); a tool
        // without its own texts keeps the question of the tab
        const bool isTabClosing = !m_isLeavingTool;
        PDFAppToolCloseQuestion question;
        if (const IPDFAppToolOwnTabQuestion* ownTabQuestion = dynamic_cast<const IPDFAppToolOwnTabQuestion*>(m_shell->getGUI()))
        {
            question = ownTabQuestion->getOwnTabCloseQuestion(isTabClosing);
        }
        if (!question.isValid())
        {
            question.title = isTabClosing ? tr("Close %1?").arg(tab->getTabTitle().left(256)) : tr("Leave %1?").arg(tab->getTabTitle().left(256));
            question.text = lossText;
            question.keepText = tr("Cancel");
            question.discardText = isTabClosing ? tr("Close Tab") : tr("Discard");
        }

        // The question belongs to this page (as the questions of a document tab to their tab):
        // it goes with the page, the tab or the window, never left open for a deleted tool. The object
        // names stay those of the question of the tab (the Discard button "closeTabButton").
        PDFAppDocumentChoiceDialog* dialog = new PDFAppDocumentChoiceDialog(PDFThemeIcons::Badge::Warning, question.title, question.text, 440, this);
        dialog->setObjectName(QStringLiteral("toolTabCloseDialog"));
        dialog->addChoice(question.keepText, PDFAppDocumentChoice::Cancel, true, false, QStringLiteral("cancelButton"));
        QPushButton* discardButton = dialog->addChoice(question.discardText, PDFAppDocumentChoice::Discard, false, false, QStringLiteral("closeTabButton"));
        discardButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("trash")));

        // A0: shown (exec's modality, no event loop); the answer comes to onLossQuestionChosen (Cancel when the
        // dialog goes with the page without an answer)
        m_leaveFlow = flow;
        m_leaveStage = LeaveStage::LossQuestion;
        flow->setQuestion(dialog);
        PDFAppDocumentChoiceDialog::ask(dialog, this, &PDFAppToolTabPage::onLossQuestionChosen);
        return PDFAppLeaveResult::Pending;
    }

    // The tool ends with the tab (its page deletes the shell); nothing is changed here, so a
    // quit of the window canceled by a later question keeps the tool as it is
    return PDFAppLeaveResult::Done;
}

void PDFAppToolTabPage::onShellStateChanged()
{
    // Undo and Redo of the tool are off while it runs and while its result is decided (the
    // host tells it the toolbar and the window: their slots run code of the application; a retired page tells nothing)
    if (m_host && m_shell)
    {
        const PDFDocumentToolShell::State state = m_shell->getState();
        m_host->setBusy(state == PDFDocumentToolShell::State::Running || state == PDFDocumentToolShell::State::Deciding);
    }

    // The run stopped by Stop and Leave has ended: the work after it is the one, which the answer covers
    if (m_isLeaveRunStopped && m_shell && m_shell->getState() != PDFDocumentToolShell::State::Running)
    {
        m_isLeaveRunStopped = false;
        m_leaveAnsweredWork = getLeaveWorkSignature();
    }
    if (!PDFDeferredDeletion::isRetired(this))
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
    // the shell go only by retire, the close of the tab is the last statement (R17). A page retired by foreign code
    // (step A, also with its tab) goes on with nothing; the tab and the window are checked first (a window-lifetime
    // object, a test may delete it synchronously - the page goes with it).
    const PDFAppToolCloseRequest requestCopy = request;
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
            // close keeps the tool, which takes its requests again; a close, whose question is open
            // (Pending), resumes it when its flow ends without the close (onCanceledCloseFinished).
            const PDFAppLeaveResult result = window->requestCloseTab(tab);
            if (!tab || PDFDeferredDeletion::isRetired(this) || !m_shell)
            {
                return;
            }
            if (result == PDFAppLeaveResult::Refused)
            {
                m_shell->resumeEditing();
            }
            else if (result == PDFAppLeaveResult::Pending && tab)
            {
                if (PDFAppLeaveFlow* flow = tab->findLeaveFlow())
                {
                    // The flow belongs to the tab (step A: the handle is cut by retire of this page)
                    PDFDeferredDeletion::connect(flow, &PDFAppLeaveFlow::finished, this, &PDFAppToolTabPage::onCanceledCloseFinished, Qt::SingleShotConnection);
                }
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
    // place in the tab bar. A file, which does not open (the window lists it in open-failed),
    // leaves the tool in this tab with its inputs (RS3APP): the written file stays on the disk.
    if (!requestCopy.newDocumentFilePath.isEmpty())
    {
        // The opened file becomes the current tab (a new one, or its tab, when it is open already)
        const QPointer<PDFAppTab> currentBefore = window->getCurrentTab();
        window->openDocuments(QStringList({ requestCopy.newDocumentFilePath }), 0);
        if (!tab || !window || PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
        if (window->getCurrentTab() == currentBefore.data())
        {
            if (m_shell)
            {
                m_shell->resumeEditing();
            }
            return;
        }
        if (!closeShell() || !tab || !window)
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
        if (!tab || !window || PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    // RB3 b of step 3b (Manage Pages K31, 396:27563 "The assembly stays open"): the result is
    // out (opened, saved, the folder asked for); the tool returns to editing with its inputs, Then
    // continue with opens its tool beside it
    const PDFDocumentToolGUI* gui = m_shell ? m_shell->getGUI() : nullptr;
    const bool isTabKept = gui && gui->isTabKeptAfterResult();

    // R17: Then continue with starts on the new tab first, then this tab closes (Q9)
    if (!requestCopy.continueToolId.isEmpty() && !requestCopy.continueDocumentId.isEmpty())
    {
        if (PDFAppNavigator* navigator = window->getNavigator())
        {
            navigator->navigate(PDFAppRoutes::documentTool(requestCopy.continueDocumentId, requestCopy.continueToolId), PDFAppNavigationMode::Push);
        }
        if (!tab || !window || PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    if (isTabKept)
    {
        if (m_shell)
        {
            m_shell->resumeEditing();
        }
        return;
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

void PDFAppToolTabPage::onCanceledCloseFinished(bool isDone)
{
    // The close of the tab by Cancel of the tool has been refused by its question: the tool takes its requests again
    if (!isDone && m_shell)
    {
        m_shell->resumeEditing();
    }
}

QString PDFAppToolTabPage::getLeaveWorkSignature() const
{
    // What closing the tab loses, as the tab names it (K16: the loss text, the number of the inputs); a reading of an
    // input, which ends later, changes neither (RLC1 M1)
    const PDFAppToolTab* tab = getToolTab();
    const PDFAppToolInputList* list = tab ? tab->getInputList() : nullptr;
    return (tab ? tab->getLossText() : QString()) + QLatin1Char('\n') + QString::number(list ? list->getCount() : -1);
}

bool PDFAppToolTabPage::finishLeave(bool isProceeding)
{
    if (isProceeding)
    {
        // The leave goes on (the questions of the quit are modal: no work of the user since the answer); the work of
        // the tool is read again for a later cancel (a stopped run, which is still ending, is read at its end)
        if (m_isLeaveAnswered && m_shell && !m_isLeaveRunStopped)
        {
            m_leaveAnsweredWork = getLeaveWorkSignature();
        }
        return true;
    }

    // Owner 2026-11-24: the closing answer is final - the tab closes from the event loop (the flow, which calls this,
    // ends first); a page without such an answer stays as it is
    if (m_isLeaveAnswered && !m_isCanceledLeaveQueued)
    {
        m_isCanceledLeaveQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolTabPage::onCanceledLeaveQueued, Qt::QueuedConnection);
    }
    return true;
}

void PDFAppToolTabPage::onCanceledLeaveQueued()
{
    // A leave, which waits for this page again, owns the answer (requestClose has forgotten the old one); every other
    // end forgets it too. A retired page (its posted call is removed) does nothing.
    m_isCanceledLeaveQueued = false;
    if (!m_isLeaveAnswered || checkLeaveWait())
    {
        return;
    }
    const bool isWorkUnchanged = !m_shell || m_isLeaveRunStopped || getLeaveWorkSignature() == m_leaveAnsweredWork;
    m_isLeaveAnswered = false;
    m_isLeaveRunStopped = false;
    QPointer<PDFAppToolTab> tab = getToolTab();
    QPointer<PDFAppMainWindow> window = tab ? qobject_cast<PDFAppMainWindow*>(tab->window()) : nullptr;
    if (!tab || !window || PDFDeferredDeletion::isRetired(this) || PDFDeferredDeletion::isRetired(tab.data()))
    {
        return;
    }

    // RLC1 M1: work made after the answer (inputs added while Save All of the quit ran) was never answered - the tab
    // stays with it; so does a tool, whose result dialog or question is open (the user decides there)
    if (!isWorkUnchanged || (m_shell && (m_shell->getState() == PDFDocumentToolShell::State::Deciding || m_shell->isQuestionOpen())))
    {
        return;
    }

    // The tab closes as by the answer in a quit, which goes on: the tool ends first, then the tab goes without a second
    // question; the window says why the tab has gone (decision 3 of 24. 11.) - the last call
    const QString message = tr("%1 has closed as you chose in its question.").arg(getTitle());
    if (!closeShell() || !tab || !window)
    {
        return;
    }
    tab->setCloseConfirmed(true);
    if (window->closeTab(tab) && window)
    {
        window->showTransientMessage(message);
    }
}

bool PDFAppToolTabPage::closeShell()
{
    // The close emits (the mode left, the state): anything may be closed meanwhile (a retired page goes on with
    // nothing)
    if (m_shell)
    {
        m_shell->close();
    }
    return !PDFDeferredDeletion::isRetired(this);
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
