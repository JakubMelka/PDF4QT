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

#include "pdfappsessionend.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumenttab.h"
#include "pdfappleaveflow.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"

#include <QApplication>
#include <QSessionManager>

#include <vector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace pdf
{

PDFAppSessionManager::~PDFAppSessionManager() = default;

#if QT_CONFIG(sessionmanager)

/// The session manager of Qt behind the interface of the handler
class PDFAppQtSessionManager : public PDFAppSessionManager
{
public:
    explicit PDFAppQtSessionManager(QSessionManager& manager) : m_manager(manager) { }

    virtual bool allowsInteraction() override { return m_manager.allowsInteraction(); }
    virtual void release() override { m_manager.release(); }
    virtual void cancel() override { m_manager.cancel(); }

private:
    QSessionManager& m_manager;
};

#endif

/// Helpers of the end of the session
class PDFAppSessionEndHelper
{
public:
    PDFAppSessionEndHelper() = delete;

    /// The main windows of the process, the active one first
    static std::vector<QPointer<PDFAppMainWindow>> getMainWindows()
    {
        std::vector<QPointer<PDFAppMainWindow>> windows;
        PDFAppMainWindow* activeWindow = qobject_cast<PDFAppMainWindow*>(QApplication::activeWindow());
        if (activeWindow)
        {
            windows.emplace_back(activeWindow);
        }
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(widget);
            if (window && window != activeWindow)
            {
                windows.emplace_back(window);
            }
        }
        return windows;
    }

    /// A document, whose changes the user has discarded at the end of the session
    struct DiscardedDocument
    {
        QPointer<const PDFAppDocument> document;
        quint64 version = 0;
    };

    /// The documents of the last Quit Without Saving at the end of the session
    static std::vector<DiscardedDocument>& getDiscardedDocuments()
    {
        static std::vector<DiscardedDocument> documents;
        return documents;
    }

    /// The work of a tool, which the user has given up at the end of the session (its stamp at the answer)
    struct DiscardedToolWork
    {
        QPointer<const PDFAppPage> page;
        quint64 workStamp = 0;
    };

    /// The work of the tools, which the user has given up at the last end of the session (decision 7)
    static std::vector<DiscardedToolWork>& getDiscardedToolPages()
    {
        static std::vector<DiscardedToolWork> pages;
        return pages;
    }

    static void setDiscardedToolPages(const std::vector<QPointer<const PDFAppPage>>& toolPages)
    {
        std::vector<DiscardedToolWork>& pages = getDiscardedToolPages();
        pages.clear();
        for (const QPointer<const PDFAppPage>& page : toolPages)
        {
            if (page)
            {
                pages.push_back({ page, page->getWorkStamp() });
            }
        }
    }

    /// The last commitData has allowed the end of the session (H1 of the review of A0)
    static bool& getSessionEndingFlag()
    {
        static bool isSessionEnding = false;
        return isSessionEnding;
    }
};

bool PDFAppSessionEnd::isSessionEnding()
{
    return PDFAppSessionEndHelper::getSessionEndingFlag();
}

void PDFAppSessionEnd::onApplicationStateChanged(Qt::ApplicationState state)
{
    // The user works with the application again: the end of the session, which was allowed, has been canceled by
    // the system or another application - a quit asks as always
    if (state == Qt::ApplicationActive)
    {
        PDFAppSessionEndHelper::getSessionEndingFlag() = false;
    }
}

bool PDFAppSessionEnd::isDiscarded(const PDFAppDocument* document)
{
    if (!document)
    {
        return false;
    }

    for (const PDFAppSessionEndHelper::DiscardedDocument& discarded : PDFAppSessionEndHelper::getDiscardedDocuments())
    {
        if (discarded.document == document && discarded.version == document->getVersion())
        {
            return true;
        }
    }
    return false;
}

bool PDFAppSessionEnd::isToolWorkDiscarded(const PDFAppPage* page)
{
    if (!page)
    {
        return false;
    }

    // The work, which has changed since the answer, is asked about again
    for (const PDFAppSessionEndHelper::DiscardedToolWork& discarded : PDFAppSessionEndHelper::getDiscardedToolPages())
    {
        if (discarded.page == page && discarded.workStamp == page->getWorkStamp())
        {
            return true;
        }
    }
    return false;
}

QMetaObject::Connection& PDFAppSessionEnd::getConnection()
{
    static QMetaObject::Connection connection;
    return connection;
}

void PDFAppSessionEnd::install()
{
#if QT_CONFIG(sessionmanager)
    // The application is the context (no object of its own: the handler is a static
    // function); direct, the session ends after the handler (Windows: WM_QUERYENDSESSION)
    QGuiApplication* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (application && !getConnection())
    {
        getConnection() = QObject::connect(application, &QGuiApplication::commitDataRequest, application, &PDFAppSessionEnd::onCommitDataRequest, Qt::DirectConnection);
        QObject::connect(application, &QGuiApplication::applicationStateChanged, application, &PDFAppSessionEnd::onApplicationStateChanged);
    }
#endif
}

bool PDFAppSessionEnd::isInstalled()
{
    return bool(getConnection());
}

void PDFAppSessionEnd::onCommitDataRequest(QSessionManager& manager)
{
#if QT_CONFIG(sessionmanager)
    PDFAppQtSessionManager sessionManager(manager);
    const Outcome outcome = commitData(&sessionManager);
    qInfo("The end of the session of the system: %s", outcome == Outcome::Canceled || outcome == Outcome::CanceledWithoutInteraction ? "canceled" : "allowed");
#else
    Q_UNUSED(manager);
#endif
}

PDFAppSessionEnd::Outcome PDFAppSessionEnd::commitData(PDFAppSessionManager* manager)
{
    // The decision of an earlier end of the session holds only for it
    PDFAppSessionEndHelper::getSessionEndingFlag() = false;
    if (!manager)
    {
        return Outcome::Canceled;
    }

    const std::vector<QPointer<PDFAppMainWindow>> windows = PDFAppSessionEndHelper::getMainWindows();

    // Open editors of form fields are changes too; a slot may delete a tab or a window
    std::vector<QPointer<PDFAppDocument>> pendingDocuments;
    for (const QPointer<PDFAppMainWindow>& window : windows)
    {
        for (PDFAppDocumentTab* tab : window ? window->getDocumentTabs() : QList<PDFAppDocumentTab*>())
        {
            pendingDocuments.emplace_back(tab->getDocument());
        }
    }
    for (const QPointer<PDFAppDocument>& document : pendingDocuments)
    {
        if (document)
        {
            document->commitPendingEdits();
        }
    }

    // The unsaved work of all windows (the active window first), without the work discarded at an earlier end of
    // the session and not changed since
    const UnsavedWork work = collectUnsavedWork();
    if (work.documents.empty())
    {
        // The windows, which Qt closes now, do not ask about the work discarded at an earlier end of the session
        PDFAppSessionEndHelper::getSessionEndingFlag() = true;
        storeSessionState();
        return Outcome::NoChanges;
    }

    // A0 (owner 2026-11-21, QR3.1 changed): never a question inside the handler (the system wants its answer now,
    // no nested event loop). The changes are never lost silently: the end of the session is canceled and the
    // question follows from the event loop (on X11 the session manager accepts the cancel only with an interaction:
    // allowsInteraction is the named exception of Qt, its own loop)
    const bool isInteractive = manager->allowsInteraction();
    manager->cancel();
    PDFAppSessionEndQuestion::post(work.window.data());
    return isInteractive ? Outcome::Canceled : Outcome::CanceledWithoutInteraction;
}

PDFAppSessionEnd::UnsavedWork PDFAppSessionEnd::collectUnsavedWork()
{
    // A tool with unsaved work of the user in the tab of a document lists its document too (owner 2026-11-20,
    // decision 7: one question at the end of the session, never a nested question of the tool when the windows
    // close afterwards)
    UnsavedWork work;
    for (const QPointer<PDFAppMainWindow>& window : PDFAppSessionEndHelper::getMainWindows())
    {
        for (PDFAppDocumentTab* tab : window ? window->getDocumentTabs() : QList<PDFAppDocumentTab*>())
        {
            const PDFAppDocument* document = tab->getDocument();
            const PDFAppPage* page = tab->getPage();
            const bool isModified = document && document->isReady() && document->isModified() && !isDiscarded(document);
            const bool hasToolWork = document && page && page->hasUnsavedWork() && !isToolWorkDiscarded(page);
            if (isModified || hasToolWork)
            {
                work.documents.push_back(document);
                work.tabs.emplace_back(tab);
                if (!work.window)
                {
                    work.window = window;
                }
            }
            if (hasToolWork)
            {
                work.toolWorkDocuments.push_back(document);
                work.toolPages.emplace_back(page);
            }
        }
    }
    return work;
}

void PDFAppSessionEnd::rememberDiscarded(const std::vector<QPointer<PDFAppDocumentTab>>& tabs, const std::vector<QPointer<const PDFAppPage>>& toolPages)
{
    // The windows, which Qt closes when the session ends, and the next end of the session do not ask again about the
    // versions, which the user has discarded now, nor about the work of the tools
    std::vector<PDFAppSessionEndHelper::DiscardedDocument>& documents = PDFAppSessionEndHelper::getDiscardedDocuments();
    documents.clear();
    for (const QPointer<PDFAppDocumentTab>& tab : tabs)
    {
        if (const PDFAppDocument* document = tab ? tab->getDocument() : nullptr)
        {
            documents.push_back({ QPointer<const PDFAppDocument>(document), document->getVersion() });
        }
    }
    PDFAppSessionEndHelper::setDiscardedToolPages(toolPages);
}

void PDFAppSessionEnd::rememberToolWorkDiscarded(const std::vector<QPointer<const PDFAppPage>>& toolPages)
{
    PDFAppSessionEndHelper::setDiscardedToolPages(toolPages);
}

void PDFAppSessionEnd::storeSessionState()
{
    // The pages record what they must while the windows are whole (the use of an interactive tool): the process may
    // end without closing them
    for (const QPointer<PDFAppMainWindow>& mainWindow : PDFAppSessionEndHelper::getMainWindows())
    {
        if (mainWindow)
        {
            mainWindow->prepareTabsClose();
        }
    }

    // The window of the quit (O29): the active main window, else the one active last
    PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(QApplication::activeWindow());
    if (!window || !window->isVisible())
    {
        window = PDFAppMainWindow::getLastActiveMainWindow();
    }
    if (!window || !window->isVisible())
    {
        window = nullptr;
        for (const QPointer<PDFAppMainWindow>& mainWindow : PDFAppSessionEndHelper::getMainWindows())
        {
            if (mainWindow && mainWindow->isVisible())
            {
                window = mainWindow.data();
                break;
            }
        }
    }

    if (window)
    {
        window->saveLastSession();
        window->storeGeometry();
    }
}

// ---------------------------------------------------------------- the question after the canceled end

PDFAppSessionEndQuestion::PDFAppSessionEndQuestion(PDFAppMainWindow* window) :
    BaseClass(window),
    m_window(window)
{
    setObjectName(QStringLiteral("sessionEndQuestion"));
}

PDFAppSessionEndQuestion::~PDFAppSessionEndQuestion()
{
    // The window, which has gone with the question, ends its reason of the blocked end of the session (Windows
    // destroys it with the window; nothing else is read here)
    if (m_isShutdownBlocked && m_window)
    {
        setShutdownBlocked(false);
    }
}

QPointer<PDFAppSessionEndQuestion>& PDFAppSessionEndQuestion::getCurrentPointer()
{
    static QPointer<PDFAppSessionEndQuestion> current;
    return current;
}

PDFAppSessionEndQuestion* PDFAppSessionEndQuestion::getCurrent()
{
    PDFAppSessionEndQuestion* question = getCurrentPointer().data();
    return question && !question->m_isEnded ? question : nullptr;
}

PDFAppSessionEndQuestion* PDFAppSessionEndQuestion::post(PDFAppMainWindow* window)
{
    // One question in the process: a second end of the session brings it to the front (QR3.4)
    if (PDFAppSessionEndQuestion* current = getCurrent())
    {
        if (QDialog* dialog = current->getDialog())
        {
            dialog->raise();
            dialog->activateWindow();
        }
        return current;
    }
    if (!window)
    {
        return nullptr;
    }

    // A flow of the window or of one of its tabs runs (its quit, the close of a tab asks already): its question comes
    // forward, no second one (RR3-M1)
    if (PDFAppLeaveFlow* runningFlow = PDFAppLeaveFlow::findRunning(window))
    {
        runningFlow->raiseQuestion();
        return nullptr;
    }

    // From the event loop: the handler of the end of the session has returned before the question is shown
    PDFAppSessionEndQuestion* question = new PDFAppSessionEndQuestion(window);
    getCurrentPointer() = question;
    QMetaObject::invokeMethod(question, &PDFAppSessionEndQuestion::ask, Qt::QueuedConnection);
    return question;
}

PDFAppQuitUnsavedDialog* PDFAppSessionEndQuestion::getDialog() const
{
    return m_dialog && m_dialog->isVisible() ? m_dialog.data() : nullptr;
}

void PDFAppSessionEndQuestion::ask()
{
    // The state is read again: the work may have been saved, the window may quit meanwhile
    PDFAppLeaveFlow* flow = m_window ? m_window->getLeaveFlow() : nullptr;
    const PDFAppSessionEnd::UnsavedWork work = PDFAppSessionEnd::collectUnsavedWork();
    if (!flow || work.documents.empty())
    {
        m_isEnded = true;
        deleteLater();
        return;
    }
    PDFAppLeaveFlow* runningFlow = PDFAppLeaveFlow::findRunning(m_window.data());
    if (runningFlow || !flow->start(PDFAppLeaveFlow::Goal::SessionEnd))
    {
        // A flow has started meanwhile (a quit, the close of a tab): its question is the one shown
        if (runningFlow)
        {
            runningFlow->raiseQuestion();
        }
        m_isEnded = true;
        deleteLater();
        return;
    }

    // The texts, buttons and defaults of the question of the end of the session (pdf4qt-quit-unsaved with the work
    // of the tools in its rows); shown as exec showed it, no event loop: the answer comes to onChosen
    m_tabs = work.tabs;
    m_toolPages = work.toolPages;
    PDFAppQuitUnsavedDialog* dialog = new PDFAppQuitUnsavedDialog(work.documents, m_window.data(), work.toolWorkDocuments);
    m_dialog = dialog;
    flow->setQuestion(dialog);
    setShutdownBlocked(true);
    PDFAppDocumentChoiceDialog::ask(dialog, this, &PDFAppSessionEndQuestion::onChosen);
}

void PDFAppSessionEndQuestion::onChosen(PDFAppDocumentChoice choice)
{
    m_dialog = nullptr;
    setShutdownBlocked(false);
    switch (choice)
    {
        case PDFAppDocumentChoice::Discard:
            // Quit Without Saving: the next end of the session goes on without these changes (QR3.4); the
            // application does not quit by itself
            PDFAppSessionEnd::rememberDiscarded(m_tabs, m_toolPages);
            end(true);
            return;

        case PDFAppDocumentChoice::Save:
            // Save All: the documents one by one (a save may ask: Save As of a copy, a signed document)
            m_isSaving = true;
            m_saveQueue = m_tabs;
            saveNext();
            return;

        default:
            break;
    }

    // Cancel (also the question gone without an answer): nothing is remembered
    end(false);
}

void PDFAppSessionEndQuestion::saveNext()
{
    while (!m_saveQueue.empty())
    {
        QPointer<PDFAppDocumentTab> tab = m_saveQueue.front();
        m_saveQueue.erase(m_saveQueue.begin());

        // A tab closed meanwhile (retired, step A) has not saved its changes: the step ends without the saves
        PDFAppDocument* document = tab && !PDFDeferredDeletion::isRetired(tab) ? tab->getDocument() : nullptr;
        if (!document)
        {
            end(false);
            return;
        }
        if (!document->isModified())
        {
            m_saveAttempts = 0;
            continue;
        }

        // A save request of package E: its end exactly once, never inside the call (false also when nothing can be
        // saved or the tab goes first)
        m_savingTab = tab;
        PDFAppDocumentFileCommandHandler* handler = tab->prepareSave();
        if (!handler || !handler->requestSave(this, &PDFAppSessionEndQuestion::onSaveFinished))
        {
            m_savingTab = nullptr;
            end(false);
        }
        return;
    }

    // Every document is saved: the work of the tools, which the list has named, is not asked again (a save does not
    // keep it); the application does not quit by itself
    PDFAppSessionEnd::rememberToolWorkDiscarded(m_toolPages);
    end(true);
}

void PDFAppSessionEndQuestion::onSaveFinished(bool isSaved)
{
    if (m_isEnded)
    {
        return;
    }
    QPointer<PDFAppDocumentTab> tab = m_savingTab;
    m_savingTab = nullptr;

    // A canceled or failed save ends the step (its error has been shown), also a tab closed (retired, step A) or
    // deleted during its save
    if (!isSaved || !tab || PDFDeferredDeletion::isRetired(tab))
    {
        end(false);
        return;
    }

    // A save, which was running before Save All, has written an older version: the document is saved again (a few
    // times at most)
    if (tab->getDocument() && tab->getDocument()->isModified())
    {
        if (++m_saveAttempts >= PDFAppSessionEnd::MAXIMUM_SAVE_ATTEMPTS)
        {
            end(false);
            return;
        }
        m_saveQueue.insert(m_saveQueue.begin(), tab);
    }
    else
    {
        m_saveAttempts = 0;
    }

    // The next document from the event loop (the saver of the tab finishes its signal first)
    QMetaObject::invokeMethod(this, &PDFAppSessionEndQuestion::saveNext, Qt::QueuedConnection);
}

void PDFAppSessionEndQuestion::end(bool isDone)
{
    if (m_isEnded)
    {
        return;
    }
    m_isEnded = true;
    m_isSaving = false;
    m_saveQueue.clear();
    setShutdownBlocked(false);
    deleteLater();

    // The step of the flow of the window ends (the last call: the slots of its end run code of the application)
    PDFAppLeaveFlow* flow = m_window ? m_window->findLeaveFlow() : nullptr;
    if (flow && flow->isRunning() && flow->getGoal() == PDFAppLeaveFlow::Goal::SessionEnd)
    {
        flow->finish(isDone);
    }
}

void PDFAppSessionEndQuestion::setShutdownBlocked(bool isBlocked)
{
    if (m_isShutdownBlocked == isBlocked)
    {
        return;
    }
    m_isShutdownBlocked = isBlocked;

#ifdef Q_OS_WIN
    // Windows names the reason on its screen of the blocked end of the session while the question is open (only the
    // native window of the platform plugin windows has a handle of the system)
    const WId windowId = m_window ? m_window->internalWinId() : 0;
    if (!windowId || QGuiApplication::platformName() != QLatin1String("windows"))
    {
        return;
    }
    HWND handle = reinterpret_cast<HWND>(windowId);
    if (isBlocked)
    {
        const QString reason = tr("Unsaved changes – waiting for your answer");
        ShutdownBlockReasonCreate(handle, reinterpret_cast<LPCWSTR>(reason.utf16()));
    }
    else
    {
        ShutdownBlockReasonDestroy(handle);
    }
#endif
}

}   // namespace pdf

#include "moc_pdfappsessionend.cpp"
