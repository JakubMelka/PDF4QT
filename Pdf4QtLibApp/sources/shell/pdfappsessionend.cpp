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
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"

#include <QApplication>
#include <QSessionManager>

#include <vector>

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

    /// Saves of one document at the end of a session (a running save wrote an
    /// older version, the newer one is saved again)
    static constexpr int MAXIMUM_SAVE_ATTEMPTS = 3;

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
};

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
    if (!manager)
    {
        return Outcome::Canceled;
    }

    // A new end of the session: an earlier decision is over
    PDFAppSessionEndHelper::getDiscardedDocuments().clear();
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

    // The documents with unsaved changes of all windows (the active window first)
    std::vector<const PDFAppDocument*> documents;
    std::vector<QPointer<PDFAppDocumentTab>> tabs;
    QWidget* dialogParent = nullptr;
    for (const QPointer<PDFAppMainWindow>& window : windows)
    {
        for (PDFAppDocumentTab* tab : window ? window->getDocumentTabs() : QList<PDFAppDocumentTab*>())
        {
            const PDFAppDocument* document = tab->getDocument();
            if (document && document->isReady() && document->isModified())
            {
                documents.push_back(document);
                tabs.emplace_back(tab);
                dialogParent = dialogParent ? dialogParent : window.data();
            }
        }
    }

    if (documents.empty())
    {
        storeSessionState();
        return Outcome::NoChanges;
    }

    if (!manager->allowsInteraction())
    {
        // The changes are never lost silently: the end of the session is canceled
        manager->cancel();
        return Outcome::CanceledWithoutInteraction;
    }

    // The windows may be deleted while the dialog runs (D10: Cancel then)
    const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(new PDFAppQuitUnsavedDialog(documents, dialogParent));
    switch (choice)
    {
        case PDFAppDocumentChoice::Discard:
        {
            // Qt closes the windows, when the session ends: they do not ask again
            // about the versions, which the user has discarded now
            for (const QPointer<PDFAppDocumentTab>& tab : tabs)
            {
                if (const PDFAppDocument* document = tab ? tab->getDocument() : nullptr)
                {
                    PDFAppSessionEndHelper::getDiscardedDocuments().push_back({ QPointer<const PDFAppDocument>(document), document->getVersion() });
                }
            }
            manager->release();
            storeSessionState();
            return Outcome::Discarded;
        }

        case PDFAppDocumentChoice::Save:
        {
            // The files are written before the handler returns; a save may ask (Save
            // As of a copy, a signed document), so the interaction lasts until the end
            for (const QPointer<PDFAppDocumentTab>& tab : tabs)
            {
                if (!saveAndWait(tab.data()))
                {
                    manager->cancel();
                    return Outcome::Canceled;
                }
            }
            manager->release();
            storeSessionState();
            return Outcome::Saved;
        }

        default:
            break;
    }

    manager->cancel();
    return Outcome::Canceled;
}

bool PDFAppSessionEnd::saveAndWait(PDFAppDocumentTab* tab)
{
    for (int attempt = 0; attempt < PDFAppSessionEndHelper::MAXIMUM_SAVE_ATTEMPTS; ++attempt)
    {
        // A tab closed meanwhile has not saved its changes
        const PDFAppDocument* document = tab ? tab->getDocument() : nullptr;
        if (!document)
        {
            return false;
        }
        if (!document->isModified())
        {
            return true;
        }

        QPointer<PDFAppDocumentTab> guard(tab);
        PDFAppSessionSaveWaiter waiter(tab);
        if (!waiter.saveAndWait() || !guard)
        {
            return false;
        }
    }

    const PDFAppDocument* document = tab ? tab->getDocument() : nullptr;
    return document && !document->isModified();
}

void PDFAppSessionEnd::storeSessionState()
{
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

PDFAppSessionSaveWaiter::PDFAppSessionSaveWaiter(PDFAppDocumentTab* tab) :
    BaseClass(nullptr),
    m_tab(tab)
{
    if (tab)
    {
        connect(tab, &PDFAppDocumentTab::saveFinished, this, &PDFAppSessionSaveWaiter::onSaveFinished);
        connect(tab, &QObject::destroyed, this, &PDFAppSessionSaveWaiter::onTabDestroyed);
    }
}

PDFAppSessionSaveWaiter::~PDFAppSessionSaveWaiter() = default;

bool PDFAppSessionSaveWaiter::saveAndWait()
{
    if (!m_tab)
    {
        return false;
    }

    // A save, which runs already, ends by its own signal (the tab starts no second one)
    if (!m_tab->save())
    {
        return false;
    }

    // saveFinished may come before save returns (a canceled file dialog)
    if (!m_isFinished && m_tab)
    {
        m_loop.exec(QEventLoop::DialogExec);
    }
    return m_isFinished && m_isSaved;
}

void PDFAppSessionSaveWaiter::onSaveFinished(bool isSaved)
{
    m_isFinished = true;
    m_isSaved = isSaved;
    m_loop.quit();
}

void PDFAppSessionSaveWaiter::onTabDestroyed()
{
    // The slot only forgets the tab (its derived part has gone)
    m_tab = nullptr;
    m_isFinished = true;
    m_isSaved = false;
    m_loop.quit();
}

}   // namespace pdf
