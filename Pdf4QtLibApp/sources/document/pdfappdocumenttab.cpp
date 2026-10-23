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
    connect(m_view.data(), &PDFAppDocumentView::currentPageChanged, this, &PDFAppDocumentTab::onCurrentPageChanged);

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

    m_readingStateTimer = new QTimer(this);
    m_readingStateTimer->setSingleShot(true);
    m_readingStateTimer->setInterval(READING_STATE_DELAY);
    connect(m_readingStateTimer.data(), &QTimer::timeout, this, &PDFAppDocumentTab::writeReadingState);

    // One command handler of each package (step 3 plan, chapter 2)
    m_handlers.emplace_back(new PDFAppDocumentModelCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentViewCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentPanelCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentSearchCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentFileCommandHandler(this));
    m_handlers.emplace_back(new PDFAppDocumentPropertiesCommandHandler(this));
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

    if (m_document->getState() == PDFAppDocumentState::Loading && !m_document->getFilePath().isEmpty())
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
        // The tab stays (another question, the window refused it)
        if (guard)
        {
            m_isCloseConfirmed = false;
        }
    }
}

bool PDFAppDocumentTab::confirmClose()
{
    // The questions have been answered by the flow of the save
    if (m_isCloseConfirmed)
    {
        stopOperations(false);
        writeReadingState();
        return true;
    }

    // A tab without its document (deleted by the application) has nothing to ask
    if (!m_document)
    {
        return true;
    }

    // A question is open already (a second close request meanwhile)
    if (m_isAskingClose || m_isClosingAfterSave)
    {
        return false;
    }

    QPointer<PDFAppDocumentTab> guard(this);

    // Running operations (a save, printing, an assistant): pdf4qt-document-close-running
    bool hasAskedOperation = false;
    for (const PDFAppDocumentOperation* operation : m_document->getOperations())
    {
        hasAskedOperation = hasAskedOperation || operation->isAskedBeforeClose();
    }
    if (hasAskedOperation)
    {
        m_isAskingClose = true;
        const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(new PDFAppCloseRunningDialog(m_document, this));
        if (!guard)
        {
            return false;
        }
        m_isAskingClose = false;
        if (choice != PDFAppDocumentChoice::Stop || !m_document)
        {
            return false;
        }
        stopOperations(true);
    }

    // Unsaved changes: pdf4qt-document-close-unsaved
    if (m_document && m_document->isReady() && m_document->isModified())
    {
        m_isAskingClose = true;
        const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(new PDFAppCloseUnsavedDialog(m_document, this));
        if (!guard)
        {
            return false;
        }
        m_isAskingClose = false;

        switch (choice)
        {
            case PDFAppDocumentChoice::Discard:
                break;

            case PDFAppDocumentChoice::Save:
            {
                // The tab closes, when package E has written the file (saveFinished)
                m_isClosingAfterSave = true;
                const bool isStarted = save();
                if (guard && !isStarted)
                {
                    m_isClosingAfterSave = false;
                }
                return false;
            }

            default:
                return false;
        }
    }

    stopOperations(false);
    writeReadingState();
    return true;
}

bool PDFAppDocumentTab::save()
{
    PDFAppDocumentFileCommandHandler* handler = PDFAppDocumentFileCommandHandler::find(this);
    if (!handler)
    {
        return false;
    }

    // The signal may come before the call returns (a canceled file dialog)
    connect(handler, &PDFAppDocumentFileCommandHandler::saveFinished, this, &PDFAppDocumentTab::onFileSaveFinished, Qt::UniqueConnection);
    return handler->save();
}

void PDFAppDocumentTab::onFileSaveFinished(bool isSaved)
{
    const bool isClosing = m_isClosingAfterSave;
    m_isClosingAfterSave = false;

    QPointer<PDFAppDocumentTab> guard(this);
    Q_EMIT saveFinished(isSaved);
    if (!guard)
    {
        return;
    }

    if (!isClosing || !isSaved || !m_document)
    {
        return;
    }

    // Save of close-unsaved: the tab closes only with its changes in the file
    if (!m_document->isModified())
    {
        m_isCloseConfirmed = true;
        requestClose();
        return;
    }

    // The finished save was started before the question (a save, which ran, when
    // the user chose Save, wrote an older version): the current version is saved
    if (m_document->isReady())
    {
        m_isClosingAfterSave = true;
        const bool isStarted = save();
        if (guard && !isStarted)
        {
            m_isClosingAfterSave = false;
        }
    }
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

    delete m_lockPopover.data();
    m_lockPopover = new PDFAppLockPopover(this);
    connect(m_lockPopover.data(), &PDFAppLockPopover::unlockRequested, this, &PDFAppDocumentTab::onUnlockRequested);
    connect(m_lockPopover.data(), &PDFAppLockPopover::editCopyRequested, this, &PDFAppDocumentTab::onEditCopyRequested);
    m_lockPopover->showBelow(anchor);
}

void PDFAppDocumentTab::onUnlockRequested()
{
    if (m_document)
    {
        m_document->setLocked(false);
    }
}

void PDFAppDocumentTab::onEditCopyRequested()
{
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
    if (!m_document)
    {
        return;
    }

    QPointer<PDFAppDocumentTab> guard(this);
    updateStack();

    if (m_document->isReady() && !m_isShown)
    {
        m_isShown = true;
        onDocumentShown();
        if (!guard)
        {
            return;
        }
    }

    Q_EMIT pageTitleChanged();
    if (!guard)
    {
        return;
    }
    Q_EMIT commandStatesChanged();
    if (!guard)
    {
        return;
    }

    if (m_document && m_document->getState() == PDFAppDocumentState::Failed)
    {
        Q_EMIT loadFailed(m_document->getFilePath(), m_document->getErrorMessage());
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
    if (guard)
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
    // The lock applies also at the next opening of the file (recent_file.locked)
    PDFAppContext* context = getContext();
    if (context && context->getDatabase() && m_document && !m_document->getFilePath().isEmpty())
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
    const PDFInteger pageIndex = m_view->getCurrentPageIndex();
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
