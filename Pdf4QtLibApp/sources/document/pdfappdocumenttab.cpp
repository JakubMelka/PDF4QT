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

#include "pdfappdocumenttab.h"
#include "pdfappannotatecommands.h"
#include "pdfappannotationeditcommands.h"
#include "pdfappnotesoutlinecommands.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumentmodelcommands.h"
#include "pdfappdocumentpage.h"
#include "pdfappdocumentpanelcommands.h"
#include "pdfappdocumentproperties.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentview.h"
#include "pdfappdocumentviewcommands.h"
#include "pdfappmainwindow.h"
#include "pdfappviewstate.h"
#include "pdfrecentrepository.h"

#include "pdfdeferreddeletion.h"

#include <QStackedWidget>
#include <QVBoxLayout>

namespace pdf
{

PDFAppDocumentTab::PDFAppDocumentTab(PDFAppContext* context, QWidget* parent) :
    BaseClass(PDFAppTabKind::Document, context, parent)
{
    setObjectName(QStringLiteral("documentTab"));

    m_document = new PDFAppDocument(context, this);
    m_loader = new PDFAppDocumentLoader(m_document);
    if (context)
    {
        context->getDocumentRegistry()->addDocument(m_document);
    }
    connect(m_document.data(), &PDFAppDocument::stateChanged, this, &PDFAppDocumentTab::onDocumentStateChanged);
    connect(m_document.data(), &PDFAppDocument::modifiedChanged, this, &PDFAppDocumentTab::onDocumentTitleChanged);
    connect(m_document.data(), &PDFAppDocument::fileChanged, this, &PDFAppDocumentTab::onDocumentTitleChanged);
    connect(m_document.data(), &PDFAppDocument::loadingProgressChanged, this, &PDFAppDocumentTab::onDocumentProgressChanged);
    connect(m_document.data(), &PDFAppDocument::operationProgressChanged, this, &PDFAppDocumentTab::onDocumentProgressChanged);
    connect(m_document.data(), &PDFAppDocument::operationsChanged, this, &PDFAppDocumentTab::onDocumentTitleChanged);
    connect(m_document.data(), &PDFAppDocument::lockChanged, this, &PDFAppDocumentTab::onLockChanged);
    connect(m_document.data(), &PDFAppDocument::userChangesSuspendedChanged, this, &PDFAppDocumentTab::onHandlerStateChanged);
    connect(m_document.data(), &PDFAppDocument::documentChanged, this, &PDFAppDocumentTab::onHandlerStateChanged);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("documentTabStack"));
    layout->addWidget(m_stack);

    m_view = new PDFAppDocumentView(context, m_stack);
    m_view->setDocument(m_document);
    m_stack->addWidget(m_view);
    connect(m_view.data(), &PDFAppDocumentView::displayedPageChanged, this, &PDFAppDocumentTab::onCurrentPageChanged);

    // The other pages of the tab (properties, Page Not Found)
    m_pageArea = new QWidget(m_stack);
    m_pageArea->setObjectName(QStringLiteral("pageArea"));
    m_pageLayout = new QVBoxLayout(m_pageArea);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(0);
    m_stack->addWidget(m_pageArea);

    // The document, which is not shown yet (loading, password)
    m_stateScreen = new PDFAppDocumentStateScreen(m_stack);
    m_stateScreen->setDocument(m_document);
    m_stack->addWidget(m_stateScreen);
    connect(m_stateScreen.data(), &PDFAppDocumentStateScreen::passwordEntered, this, &PDFAppDocumentTab::onPasswordEntered);
    connect(m_stateScreen.data(), &PDFAppDocumentStateScreen::closeRequested, this, &PDFAppDocumentTab::onStateScreenCloseRequested);

    // A pending reading position is written, when the tab is retired (step A: the timer fires first)
    m_readingStateTimer = new QTimer(this);
    m_readingStateTimer->setSingleShot(true);
    m_readingStateTimer->setInterval(READING_STATE_DELAY);
    connect(m_readingStateTimer.data(), &QTimer::timeout, this, &PDFAppDocumentTab::writeReadingState);
    PDFDeferredDeletion::setFlushOnRetire(m_readingStateTimer.data());

    // One command handler of each package (step 3 plan, chapter 2)
    m_handlers.emplace_back(new PDFAppDocumentModelCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentViewCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentPanelCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentSearchCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentFileCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentPropertiesCommandHandler(this));
    m_handlers.emplace_back(new PDFAppAnnotateCommandHandler(this));
    m_handlers.emplace_back(new PDFAppAnnotationEditCommandHandler(this));
    m_handlers.emplace_back(new PDFAppNotesOutlineCommandHandler(this));
    for (const QPointer<PDFAppDocumentCommandHandler>& handler : m_handlers)
    {
        connect(handler.data(), &PDFAppDocumentCommandHandler::stateChanged, this, &PDFAppDocumentTab::onHandlerStateChanged);
    }

    updateStack();
}

PDFAppDocumentTab::~PDFAppDocumentTab()
{
    // The reading position of the file is kept (the view is still whole)
    if (m_isShown)
    {
        writeReadingState();
    }

    delete m_lockPopover.data();

    // The handlers go first (they use the document and the view), the view before
    // the document (it follows the document)
    for (const QPointer<PDFAppDocumentCommandHandler>& handler : m_handlers)
    {
        delete handler.data();
    }
    m_handlers.clear();

    if (m_document)
    {
        m_document->disconnect(this);
    }
    if (m_stateScreen)
    {
        m_stateScreen->setDocument(nullptr);
    }
    delete m_view.data();
}

QString PDFAppDocumentTab::getDocumentId() const
{
    return m_document ? m_document->getId() : QString();
}

bool PDFAppDocumentTab::open(const QString& filePath)
{
    if (!m_loader || !m_document || m_loader->isRunning() || m_document->getState() != PDFAppDocumentState::Loading || !m_document->getFilePath().isEmpty())
    {
        return false;
    }

    m_loader->start(filePath);
    return true;
}

bool PDFAppDocumentTab::openCopyOf(const PDFAppDocument* source)
{
    if (!m_document || (m_loader && m_loader->isRunning()) || !m_document->getFilePath().isEmpty())
    {
        return false;
    }
    return m_document->setCopyOf(source);
}

bool PDFAppDocumentTab::openFromMemory(QByteArray bytes, const QString& title, const QString& originFilePath)
{
    if (!m_loader || !m_document || m_loader->isRunning() || m_document->getState() != PDFAppDocumentState::Loading ||
        !m_document->getFilePath().isEmpty() || m_document->isCopy())
    {
        return false;
    }

    return m_loader->startFromMemory(std::move(bytes), title, originFilePath);
}

bool PDFAppDocumentTab::isViewShown() const
{
    return m_stack && m_view && m_stack->currentWidget() == m_view;
}

bool PDFAppDocumentTab::isStateScreenShown() const
{
    return m_stack && m_stateScreen && m_stack->currentWidget() == m_stateScreen;
}

std::vector<PDFAppDocumentCommandHandler*> PDFAppDocumentTab::getHandlers() const
{
    std::vector<PDFAppDocumentCommandHandler*> handlers;
    for (const QPointer<PDFAppDocumentCommandHandler>& handler : m_handlers)
    {
        if (handler)
        {
            handlers.push_back(handler.data());
        }
    }
    return handlers;
}

PDFAppDocumentCommandHandler* PDFAppDocumentTab::findHandler(const QString& commandId) const
{
    for (const QPointer<PDFAppDocumentCommandHandler>& handler : m_handlers)
    {
        if (handler && handler->handles(commandId))
        {
            return handler.data();
        }
    }
    return nullptr;
}

bool PDFAppDocumentTab::isTabLocked() const
{
    return m_document && m_document->isReady() && m_document->isLocked();
}

bool PDFAppDocumentTab::isTabUnsaved() const
{
    return m_document && m_document->isReady() && m_document->isModified();
}

bool PDFAppDocumentTab::isTabPasswordProtected() const
{
    return m_document && m_document->isPasswordProtected();
}

qreal PDFAppDocumentTab::getTabProgress() const
{
    if (!m_document)
    {
        return -1.0;
    }

    if (m_document->getState() == PDFAppDocumentState::Loading && (!m_document->getFilePath().isEmpty() || m_document->isCopy()))
    {
        return qMax(0, m_document->getLoadingProgress()) / 100.0;
    }

    // An operation, which is asked about before the tab closes, first; then any other
    // running operation (the text index), which reports its progress
    const std::vector<PDFAppDocumentOperation*> operations = m_document->getOperations();
    for (const PDFAppDocumentOperation* operation : operations)
    {
        if (operation->isAskedBeforeClose())
        {
            return qMax(0, operation->getProgress()) / 100.0;
        }
    }
    for (const PDFAppDocumentOperation* operation : operations)
    {
        if (operation->getProgress() >= 0)
        {
            return operation->getProgress() / 100.0;
        }
    }
    return -1.0;
}

QString PDFAppDocumentTab::getTabTitle() const
{
    return m_document ? m_document->getTitle() : tr("Document");
}

QString PDFAppDocumentTab::getTabIconName() const
{
    // icon/file of the Tab component (48:170): file-text has too many strokes for 14 px
    return QStringLiteral("file");
}

void PDFAppDocumentTab::requestClose()
{
    PDFAppMainWindow* mainWindow = qobject_cast<PDFAppMainWindow*>(window());
    QPointer<PDFAppDocumentTab> guard(this);
    if (!mainWindow || !mainWindow->closeTab(this))
    {
        // The tab stays (another question, the window refused it): only the own flag
        if (guard)
        {
            m_isCloseConfirmed = false;
        }
    }
}

PDFAppLeaveResult PDFAppDocumentTab::requestTabClose()
{
    QPointer<PDFAppDocumentTab> guard(this);
    // The questions have been answered by the flow of the close (closing after its last answer or its save)
    if (m_isCloseConfirmed)
    {
        stopOperations(false);
        writeReadingState();
        return PDFAppLeaveResult::Done;
    }

    // A tab without its document (deleted by the application) has nothing to ask
    if (!m_document)
    {
        return PDFAppLeaveResult::Done;
    }

    // The save of the close runs (a second close request meanwhile): refused, the tab closes after the save
    if (m_isClosingAfterSave)
    {
        return PDFAppLeaveResult::Refused;
    }

    // A question is open already: refused, the question comes forward (A0: one flow of the tab)
    PDFAppLeaveFlow* flow = getLeaveFlow();
    if (!flow->start(PDFAppLeaveFlow::Goal::CloseTab))
    {
        flow->raiseQuestion();
        return PDFAppLeaveResult::Refused;
    }
    m_closeFinishedPages.clear();

    // A tool of the tab with unsaved work of the user asks first (owner 2026-11-20: the question of the
    // tool, as at its Esc); its answer may run code of the tool - the tab may be deleted meanwhile. The page,
    // which has accepted, waits for the end of the leave (PDFAppPage::finishLeave, decision 8; a close canceled later
    // keeps the tab and the tool honors its closing answer, owner 2026-11-24). A retired tab (or page) goes on with
    // nothing.
    if (QPointer<PDFAppPage> page = getPage())
    {
        flow->setStep(int(CloseStep::Page));
        flow->setSubject(page.data());
        PDFAppLeaveResult result = page->requestClose(flow);
        if (!guard || PDFDeferredDeletion::isRetired(this))
        {
            return PDFAppLeaveResult::Refused;
        }
        if (result == PDFAppLeaveResult::Pending)
        {
            if (page && !PDFDeferredDeletion::isRetired(page.data()) && flow->waitForStep(page.data(), this, &PDFAppDocumentTab::onClosePageStepFinished))
            {
                return PDFAppLeaveResult::Pending;
            }
            result = PDFAppLeaveResult::Refused;
        }
        if (result != PDFAppLeaveResult::Done)
        {
            flow->finish(false);
            return PDFAppLeaveResult::Refused;
        }
        flow->addAcceptedPage((page && !PDFDeferredDeletion::isRetired(page.data())) ? page.data() : nullptr);
    }

    const PDFAppLeaveResult result = continueClose(CloseStep::Running);
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return PDFAppLeaveResult::Refused;
    }
    if (result == PDFAppLeaveResult::Done)
    {
        stopOperations(false);
        writeReadingState();
    }
    return result;
}

PDFAppLeaveResult PDFAppDocumentTab::continueClose(CloseStep from)
{
    QPointer<PDFAppDocumentTab> guard(this);
    // The slots of the questions and of the commits may close the tab: a retired tab (or flow) goes on with nothing
    QPointer<PDFAppLeaveFlow> flow = getLeaveFlow();

    // Running operations (a save, printing, an assistant): pdf4qt-document-close-running (A0: shown, never exec;
    // the answer comes to onCloseRunningChosen)
    if (from <= CloseStep::Running && m_document)
    {
        bool hasAskedOperation = false;
        for (const PDFAppDocumentOperation* operation : m_document->getOperations())
        {
            hasAskedOperation = hasAskedOperation || operation->isAskedBeforeClose();
        }
        if (hasAskedOperation)
        {
            flow->setStep(int(CloseStep::Running));
            PDFAppCloseRunningDialog* dialog = new PDFAppCloseRunningDialog(m_document, this);
            flow->setQuestion(dialog);
            PDFAppDocumentChoiceDialog::ask(dialog, this, &PDFAppDocumentTab::onCloseRunningChosen);
            return PDFAppLeaveResult::Pending;
        }
    }

    // The close goes on: the choice of the question of the tool, which changes the document (Redact "Remove New
    // Marks", Measure "Save and Close"), is applied before the question of the unsaved changes - Save then saves
    // it; Cancel there keeps the tab with the applied change (Undo takes it back) and the tool closes (owner
    // 2026-11-24: its answer is final; PDFAppLeaveFlow::finish ends it by false). A tool, which could not keep all
    // of its work (a measurement, which cannot be stored), keeps the tab (owner 2026-11-21); its question about the
    // rest is a step of this flow. The slots of the commit may delete the tab.
    if (from <= CloseStep::FinishPages)
    {
        flow->setStep(int(CloseStep::FinishPages));
        PDFAppLeaveResult result = flow->requestFinishPages(true);
        if (!guard || !flow || PDFDeferredDeletion::isRetired(this) || PDFDeferredDeletion::isRetired(flow.data()))
        {
            return PDFAppLeaveResult::Refused;
        }
        if (result == PDFAppLeaveResult::Pending)
        {
            if (flow->waitForStep(flow.data(), this, &PDFAppDocumentTab::onCloseFinishStepFinished))
            {
                return PDFAppLeaveResult::Pending;
            }
            result = PDFAppLeaveResult::Refused;
        }
        if (result != PDFAppLeaveResult::Done)
        {
            flow->finish(false);
            return PDFAppLeaveResult::Refused;
        }
    }

    // An open editor of a form field is a change too (step 4, package D)
    if (from <= CloseStep::Unsaved && m_document)
    {
        m_document->commitPendingEdits();
        if (!guard || !flow || PDFDeferredDeletion::isRetired(this) || PDFDeferredDeletion::isRetired(flow.data()))
        {
            return PDFAppLeaveResult::Refused;
        }
    }

    // Unsaved changes: pdf4qt-document-close-unsaved (the answer comes to onCloseUnsavedChosen)
    if (from <= CloseStep::Unsaved && m_document && m_document->isReady() && m_document->isModified())
    {
        flow->setStep(int(CloseStep::Unsaved));
        PDFAppCloseUnsavedDialog* dialog = new PDFAppCloseUnsavedDialog(m_document, this);
        flow->setQuestion(dialog);
        PDFAppDocumentChoiceDialog::ask(dialog, this, &PDFAppDocumentTab::onCloseUnsavedChosen);
        return PDFAppLeaveResult::Pending;
    }

    // Every question has been answered: the flow ends (the caller closes the tab)
    finishCloseFlow(flow.data());
    return (!guard || PDFDeferredDeletion::isRetired(this)) ? PDFAppLeaveResult::Refused : PDFAppLeaveResult::Done;
}

bool PDFAppDocumentTab::isCloseStep(CloseStep step) const
{
    const PDFAppLeaveFlow* flow = findLeaveFlow();
    return flow && flow->isRunning() && flow->getGoal() == PDFAppLeaveFlow::Goal::CloseTab && flow->getStep() == int(step);
}

void PDFAppDocumentTab::completeClose(PDFAppLeaveResult result)
{
    // The last answer has come later than the request: the tab closes by its window now (the last call; a tab, which
    // the window refuses, stays - a tool, which answered its question by closing, closes)
    if (result == PDFAppLeaveResult::Done)
    {
        QPointer<PDFAppDocumentTab> guard(this);
        m_isCloseConfirmed = true;
        requestClose();
        if (guard && !PDFDeferredDeletion::isRetired(this))
        {
            endUnfinishedClose();
        }
    }
}

void PDFAppDocumentTab::endUnfinishedClose()
{
    // The pages, which the close has ended by true (the current page may be another one: a navigation during the save
    // of the close - its tool has closed by its own leave); a flow running meanwhile asks its pages anew
    std::vector<QPointer<PDFAppPage>> pages = std::move(m_closeFinishedPages);
    m_closeFinishedPages.clear();
    const PDFAppLeaveFlow* flow = findLeaveFlow();
    if (!(flow && flow->isRunning()))
    {
        PDFAppPage::finishLeaves(pages, false);
    }
}

void PDFAppDocumentTab::finishCloseFlow(PDFAppLeaveFlow* flow)
{
    m_closeFinishedPages = flow->getFinishedPages();
    flow->finish(true);
}

void PDFAppDocumentTab::onClosePageStepFinished(PDFAppLeaveResult result)
{
    if (!isCloseStep(CloseStep::Page))
    {
        return;
    }

    PDFAppLeaveFlow* flow = getLeaveFlow();
    PDFAppPage* page = qobject_cast<PDFAppPage*>(flow->getSubject());
    if (result != PDFAppLeaveResult::Done || !page)
    {
        flow->finish(false);
        return;
    }
    flow->addAcceptedPage(page);
    flow->setSubject(nullptr);
    completeClose(continueClose(CloseStep::Running));
}

void PDFAppDocumentTab::onCloseRunningChosen(PDFAppDocumentChoice choice)
{
    QPointer<PDFAppDocumentTab> guard(this);
    if (!isCloseStep(CloseStep::Running))
    {
        return;
    }

    PDFAppLeaveFlow* flow = getLeaveFlow();
    if (choice != PDFAppDocumentChoice::Stop || !m_document)
    {
        flow->finish(false);
        return;
    }

    stopOperations(true);
    if (guard && !PDFDeferredDeletion::isRetired(this) && isCloseStep(CloseStep::Running))
    {
        completeClose(continueClose(CloseStep::FinishPages));
    }
}

void PDFAppDocumentTab::onCloseFinishStepFinished(PDFAppLeaveResult result)
{
    if (!isCloseStep(CloseStep::FinishPages))
    {
        return;
    }
    if (result != PDFAppLeaveResult::Done)
    {
        getLeaveFlow()->finish(false);
        return;
    }
    completeClose(continueClose(CloseStep::Unsaved));
}

void PDFAppDocumentTab::onCloseUnsavedChosen(PDFAppDocumentChoice choice)
{
    QPointer<PDFAppDocumentTab> guard(this);
    if (!isCloseStep(CloseStep::Unsaved))
    {
        return;
    }

    PDFAppLeaveFlow* flow = getLeaveFlow();
    switch (choice)
    {
        case PDFAppDocumentChoice::Discard:
            finishCloseFlow(flow);
            completeClose(PDFAppLeaveResult::Done);
            return;

        case PDFAppDocumentChoice::Save:
        {
            // The tab closes, when package E has written the file. The questions have been answered: the flow ends
            // and the save is no step of it (M3 of the review of A0: a quit, the end of the session, a navigation or
            // a lock go on meanwhile as before A0); a second close is refused until the save ends.
            m_isClosingAfterSave = true;
            finishCloseFlow(flow);
            if (guard && !PDFDeferredDeletion::isRetired(this))
            {
                startCloseSave();
            }
            return;
        }

        default:
            break;
    }
    flow->finish(false);
}

bool PDFAppDocumentTab::save()
{
    PDFAppDocumentFileCommandHandler* handler = prepareSave();
    return handler && handler->save();
}

PDFAppDocumentFileCommandHandler* PDFAppDocumentTab::prepareSave()
{
    PDFAppDocumentFileCommandHandler* handler = PDFAppDocumentFileCommandHandler::find(this);
    if (handler)
    {
        // The tab reports the end of the save by saveFinished (it may come before the save call returns)
        connect(handler, &PDFAppDocumentFileCommandHandler::saveFinished, this, &PDFAppDocumentTab::onFileSaveFinished, Qt::UniqueConnection);
    }
    return handler;
}

void PDFAppDocumentTab::startCloseSave()
{
    // A0: the save of the close is a request of package E (its end exactly once, never inside the call; false, when
    // nothing can be saved); the tab closes in onCloseSaveFinished
    m_isClosingAfterSave = true;
    PDFAppDocumentFileCommandHandler* handler = prepareSave();
    if (!handler || !handler->requestSave(this, &PDFAppDocumentTab::onCloseSaveFinished))
    {
        m_isClosingAfterSave = false;
        endUnfinishedClose();
    }
}

void PDFAppDocumentTab::onCloseSaveFinished(bool isSaved)
{
    if (!m_isClosingAfterSave)
    {
        return;
    }

    // A canceled or failed save keeps the tab (its error has been shown); the answers of the questions of the close
    // stay valid (owner 2026-11-24): a tool, which answered by closing, closes
    if (!isSaved || !m_document)
    {
        m_isClosingAfterSave = false;
        endUnfinishedClose();
        return;
    }

    // Save of close-unsaved: the tab closes only with its changes in the file
    if (!m_document->isModified())
    {
        m_isClosingAfterSave = false;
        completeClose(PDFAppLeaveResult::Done);
        return;
    }

    // The finished save was started before the question (a save, which ran, when the user chose Save, wrote an older
    // version): the current version is saved
    if (m_document->isReady())
    {
        startCloseSave();
        return;
    }
    m_isClosingAfterSave = false;
    endUnfinishedClose();
}

void PDFAppDocumentTab::onFileSaveFinished(bool isSaved)
{
    // The end of a save of package E (the flow of the close waits for its own request, onCloseSaveFinished)
    Q_EMIT saveFinished(isSaved);
}

bool PDFAppDocumentTab::isClosingAfterSave() const
{
    return m_isClosingAfterSave;
}

void PDFAppDocumentTab::stopOperations(bool askedOnly)
{
    if (!m_document)
    {
        return;
    }

    for (PDFAppDocumentOperation* operation : m_document->getOperations())
    {
        if (!askedOnly || operation->isAskedBeforeClose())
        {
            operation->requestCancel();
        }
    }
}

bool PDFAppDocumentTab::executeCommand(const QString& commandId)
{
    // The shown page is asked first (a tool with its own canvas takes the zoom, the pages, the clipboard and Find
    // while it is open; zadani_gui.md 4.20, way 2): a command the page handles is the page's alone - disabled, it
    // is not executed by the handler of the document either. A page, which handles nothing (every page but such a
    // tool), changes nothing here.
    if (PDFAppPage* page = getPage())
    {
        bool isEnabled = false;
        if (page->isDocumentCommandHandled(commandId, &isEnabled))
        {
            // The last call: the page may delete itself, the tab or the window
            return isEnabled && page->handleDocumentCommand(commandId);
        }
    }

    PDFAppDocumentCommandHandler* handler = findHandler(commandId);
    if (!handler || !handler->isEnabled(commandId))
    {
        return false;
    }

    // The command may delete the tab (nothing of the tab is used afterwards)
    handler->execute(commandId);
    return true;
}

bool PDFAppDocumentTab::isCommandEnabled(const QString& commandId) const
{
    // The shown page first, as executeCommand
    if (const PDFAppPage* page = getPage())
    {
        bool isEnabled = false;
        if (page->isDocumentCommandHandled(commandId, &isEnabled))
        {
            return isEnabled;
        }
    }

    PDFAppDocumentCommandHandler* handler = findHandler(commandId);
    return handler && handler->isEnabled(commandId);
}

bool PDFAppDocumentTab::isCommandChecked(const QString& commandId) const
{
    PDFAppDocumentCommandHandler* handler = findHandler(commandId);
    return handler && handler->isChecked(commandId);
}

void PDFAppDocumentTab::showLockPopover(const QRect& anchor)
{
    if (!m_document || !m_document->isReady() || !m_document->isLocked())
    {
        return;
    }

    // The previous popover is retired (step A: it may be inside its own signal), deleted later
    if (PDFAppLockPopover* previous = m_lockPopover.data())
    {
        m_lockPopover = nullptr;
        PDFDeferredDeletion::retire(previous);
    }
    m_lockPopover = new PDFAppLockPopover(this);
    connect(m_lockPopover.data(), &PDFAppLockPopover::unlockRequested, this, &PDFAppDocumentTab::onUnlockRequested);
    connect(m_lockPopover.data(), &PDFAppLockPopover::editCopyRequested, this, &PDFAppDocumentTab::onEditCopyRequested);
    m_lockPopover->setPagedXFA(m_document->isPagedXFA());
    m_lockPopover->setAttachmentsLocked(m_document->isAttachmentsLocked());
    m_lockPopover->showBelow(anchor);
}

void PDFAppDocumentTab::onUnlockRequested()
{
    // Refused by the document while its attachments are encrypted (isAttachmentsLocked)
    if (m_document)
    {
        m_document->setLocked(false);
    }
}

void PDFAppDocumentTab::onEditCopyRequested()
{
    // A display version of an XFA form (step 8) has no editable copy, a document with
    // encrypted attachments no copy at all (PDFAppDocument::setCopyOf)
    if (m_document && (m_document->isPagedXFA() || m_document->isAttachmentsLocked()))
    {
        return;
    }
    Q_EMIT editCopyRequested();
}

QWidget* PDFAppDocumentTab::getPageArea() const
{
    return m_pageArea;
}

QVBoxLayout* PDFAppDocumentTab::getPageLayout() const
{
    return m_pageLayout;
}

void PDFAppDocumentTab::onPageChanged()
{
    updateStack();
}

void PDFAppDocumentTab::updateStack()
{
    if (!m_stack)
    {
        return;
    }

    // The document page is the route adapter of the view (the state screen until the
    // document is shown); any other page is shown in the page area
    QWidget* widget = m_pageArea.data();
    if (!getPage() || qobject_cast<PDFAppDocumentPage*>(getPage()))
    {
        widget = m_document && m_document->isReady() ? static_cast<QWidget*>(m_view.data()) : static_cast<QWidget*>(m_stateScreen.data());
    }

    if (widget && m_stack->currentWidget() != widget)
    {
        m_stack->setCurrentWidget(widget);
    }
}

void PDFAppDocumentTab::onDocumentStateChanged()
{
    QPointer<PDFAppDocumentTab> guard(this);
    if (!m_document)
    {
        return;
    }

    updateStack();

    if (m_document->isReady() && !m_isShown)
    {
        m_isShown = true;
        onDocumentShown();
        if (!guard || PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }

    Q_EMIT pageTitleChanged();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    Q_EMIT commandStatesChanged();
    if (!guard || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    if (m_document && m_document->getState() == PDFAppDocumentState::Failed)
    {
        // A document from the memory (an attachment) is reported by its title
        const QString name = m_document->getFilePath().isEmpty() ? m_document->getTitle() : m_document->getFilePath();
        Q_EMIT loadFailed(name, m_document->getErrorMessage());
    }
}

void PDFAppDocumentTab::onDocumentShown()
{
    // The lock and the view state of the file in Recent (rows are untrusted: the
    // view state is parsed by PDFAppViewState)
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || !m_document || m_document->getFilePath().isEmpty())
    {
        return;
    }

    const std::optional<PDFRecentFile> file = context->getRecent().getFile(m_document->getFilePath());
    if (!file)
    {
        return;
    }

    if (m_view && !file->viewState.isEmpty())
    {
        m_view->applyViewState(PDFAppViewState::fromText(file->viewState));
    }
    if (file->locked && m_document)
    {
        m_document->setLocked(true);
    }
}

void PDFAppDocumentTab::onDocumentTitleChanged()
{
    QPointer<PDFAppDocumentTab> guard(this);
    Q_EMIT pageTitleChanged();
    if (guard && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT commandStatesChanged();
    }
}

void PDFAppDocumentTab::onDocumentProgressChanged()
{
    // Only the progress of the tab changes (no command changes with a progress, the
    // window does not update its actions at each step)
    Q_EMIT pageTitleChanged();
}

void PDFAppDocumentTab::onLockChanged()
{
    // The lock applies also at the next opening of the file (recent_file.locked); the lock of
    // encrypted attachments (Q5.9 c) is not the user's and is not stored
    PDFAppContext* context = getContext();
    if (context && context->getDatabase() && m_document && !m_document->getFilePath().isEmpty() && !m_document->isAttachmentsLocked())
    {
        const PDFDatabaseError error = context->getRecent().setLocked(m_document->getFilePath(), m_document->isLocked());
        if (error.isError())
        {
            qWarning("The lock of the document cannot be stored: %s", qPrintable(error.toString()));
        }
    }

    // An unlocked document has no popover
    if (m_lockPopover && m_document && !m_document->isLocked())
    {
        m_lockPopover->close();
    }
    onDocumentTitleChanged();
}

void PDFAppDocumentTab::onHandlerStateChanged()
{
    Q_EMIT commandStatesChanged();
}

void PDFAppDocumentTab::onCurrentPageChanged()
{
    m_isReadingStatePending = true;
    if (m_readingStateTimer)
    {
        m_readingStateTimer->start();
    }
}

void PDFAppDocumentTab::writeReadingState()
{
    m_isReadingStatePending = false;
    if (m_readingStateTimer)
    {
        m_readingStateTimer->stop();
    }

    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || !m_document || !m_document->isReady() || m_document->getFilePath().isEmpty() || !m_view)
    {
        return;
    }

    // The last page shown, Finished at the last page; the state of the view
    const PDFInteger pageCount = m_document->getPageCount();
    const PDFInteger pageIndex = m_view->getDisplayedPageIndex();
    if (pageIndex >= 0 && pageCount > 0)
    {
        const PDFInteger maximumPage = PDFRecentRepository::MAXIMUM_PAGE;
        const int page = int(qBound(PDFInteger(1), pageIndex + 1, maximumPage));
        const int count = int(qBound(PDFInteger(1), pageCount, maximumPage));
        const PDFDatabaseError error = context->getRecent().setReadingState(m_document->getFilePath(), page, count, pageIndex + 1 >= pageCount);
        if (error.isError())
        {
            qWarning("The reading state cannot be stored: %s", qPrintable(error.toString()));
        }
    }

    const PDFDatabaseError viewStateError = context->getRecent().setViewState(m_document->getFilePath(), m_view->getViewState().toText());
    if (viewStateError.isError())
    {
        qWarning("The view state cannot be stored: %s", qPrintable(viewStateError.toString()));
    }
}

void PDFAppDocumentTab::onPasswordEntered(const QString& password)
{
    if (m_loader)
    {
        m_loader->setPassword(password);
    }
}

void PDFAppDocumentTab::onStateScreenCloseRequested()
{
    requestClose();
}

PDFAppDocument* PDFAppDocumentTab::getDocument() const
{
    return m_document.data();
}

PDFAppDocumentView* PDFAppDocumentTab::getView() const
{
    return m_view.data();
}

PDFAppDocumentLoader* PDFAppDocumentTab::getLoader() const
{
    return m_loader.data();
}

PDFAppLockPopover* PDFAppDocumentTab::getLockPopover() const
{
    return m_lockPopover.data();
}

PDFAppDocumentStateScreen* PDFAppDocumentTab::getStateScreen() const
{
    return m_stateScreen.data();
}

}   // namespace pdf

#include "moc_pdfappdocumenttab.cpp"
