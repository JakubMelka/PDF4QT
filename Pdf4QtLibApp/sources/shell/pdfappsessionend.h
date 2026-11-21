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

#ifndef PDFAPPSESSIONEND_H
#define PDFAPPSESSIONEND_H

#include "pdfappglobal.h"

#include <QObject>
#include <QPointer>

#include <vector>

class QSessionManager;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppMainWindow;
class PDFAppPage;
class PDFAppQuitUnsavedDialog;

/// The choice of a choice dialog (pdfappdocumentdialogs.h)
enum class PDFAppDocumentChoice;

/// The session manager of the system at the end of the session (QSessionManager;
/// the tests use a substitute)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSessionManager
{
public:
    virtual ~PDFAppSessionManager();

    /// The application may ask the user (QSessionManager::allowsInteraction)
    virtual bool allowsInteraction() = 0;

    /// The interaction with the user has ended, other applications may ask now
    virtual void release() = 0;

    /// Asks the system to cancel the end of the session
    virtual void cancel() = 0;
};

/// The end of the session of the system - logoff, shutdown, restart (step 9, N7; A0 owner 2026-11-21, QR3.1 changed,
/// QR3.4). Qt neither closes the windows nor asks them: on Windows it allows the end of the session at
/// WM_QUERYENDSESSION and the process ends after aboutToQuit. The handler of QGuiApplication::commitDataRequest
/// never asks inside (no nested event loop):
/// - without unsaved work (no document with unsaved changes, no tool with unsaved work, except what the user has
///   discarded at an earlier end of the session and has not changed since) the session may end; the last session
///   and the geometry of the active (else the last active) main window are stored as at a quit (O29);
/// - with unsaved work the end of the session is canceled at once (the system shows its own screen) and the
///   question pdf4qt-quit-unsaved 955:145378 about the documents of every main window follows from the event loop
///   (PDFAppSessionEndQuestion, the flow of the window): Save All saves the documents, Quit Without Saving
///   remembers the discarded versions (the next end of the session does not cancel for them, the windows, which Qt
///   closes then, do not ask again - isDiscarded), Cancel keeps everything. The application never quits by itself
///   after the answer: the user ends the session again. A second end of the session while the question is open
///   (or while a flow of the window runs) is canceled again without a second question.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSessionEnd
{
public:
    PDFAppSessionEnd() = delete;

    /// What the handler has done
    enum class Outcome
    {
        NoChanges,                  ///< No unsaved work: the session may end
        Canceled,                   ///< Unsaved work: the end canceled, the question follows from the event loop
        CanceledWithoutInteraction  ///< The same, the session manager has not allowed an interaction
    };

    /// The unsaved work, which the end of the session asks about (all main windows, the active one first)
    class UnsavedWork
    {
    public:
        std::vector<const PDFAppDocument*> documents;           ///< The rows of the question
        std::vector<const PDFAppDocument*> toolWorkDocuments;   ///< The documents, whose tab has a tool with unsaved work
        std::vector<QPointer<PDFAppDocumentTab>> tabs;          ///< The tabs of the documents
        std::vector<QPointer<const PDFAppPage>> toolPages;      ///< The pages of the tools with unsaved work
        QPointer<PDFAppMainWindow> window;                      ///< The first window with unsaved work (the parent of the question)
    };

    /// The unsaved work of all main windows without the work discarded earlier (isDiscarded, isToolWorkDiscarded)
    static UnsavedWork collectUnsavedWork();

    /// Quit Without Saving of the question: the versions of the documents of \p tabs and the work of \p toolPages are
    /// not asked about by the next end of the session, nor by the windows Qt closes then (replaces the earlier decision)
    static void rememberDiscarded(const std::vector<QPointer<PDFAppDocumentTab>>& tabs, const std::vector<QPointer<const PDFAppPage>>& toolPages);

    /// Save All of the question has saved the documents: the work of the tools, which the list has named, is not
    /// asked about again (a save does not keep the work of a tool; owner 2026-11-20, decision 7)
    static void rememberToolWorkDiscarded(const std::vector<QPointer<const PDFAppPage>>& toolPages);

    /// Installs the handler of commitDataRequest once per process (the main window
    /// calls it; the connection lives as long as the application, no object is created)
    static void install();

    /// The handler is connected to commitDataRequest of the application
    static bool isInstalled();

    /// Handles the end of the session for every main window of the process
    static Outcome commitData(PDFAppSessionManager* manager);

    /// The user has discarded the changes of \p document at an earlier end of the
    /// session (Quit Without Saving of its question) and the document has not changed
    /// since: the next end of the session does not cancel for it, and when Qt closes the
    /// windows after the session is allowed to end (Windows: quit() at WM_ENDSESSION, X11:
    /// Die of the session manager), a window does not ask about these changes again
    /// (PDFAppMainWindow::requestQuit).
    static bool isDiscarded(const PDFAppDocument* document);

    /// The unsaved work of the tool of \p page (PDFAppPage::hasUnsavedWork) was listed by the question of an earlier
    /// end of the session and the user has given it up (Quit Without Saving, Save All - a save does not keep the
    /// work of a tool) and the work has not changed since (PDFAppPage::getWorkStamp): the next end of the session and
    /// the windows, which close then, do not ask the tool again (owner 2026-11-20, decision 7)
    static bool isToolWorkDiscarded(const PDFAppPage* page);

    /// The last commitData has allowed the end of the session: only the windows, which Qt closes now, skip the work
    /// discarded at an earlier end of the session (isDiscarded, isToolWorkDiscarded); a quit of the user and the close
    /// of a tab ask as always (H1 of the review of A0). Cleared by the next commitData and when the application becomes
    /// active again (the end has been canceled by the system or another application).
    static bool isSessionEnding();

    /// The last session and the geometry of the window of the quit (O29)
    static void storeSessionState();

    /// Saves of one document by Save All of the question (a running save wrote an older version, the newer one is
    /// saved again)
    static constexpr int MAXIMUM_SAVE_ATTEMPTS = 3;

private:
    static QMetaObject::Connection& getConnection();

    static void onCommitDataRequest(QSessionManager& manager);
    static void onApplicationStateChanged(Qt::ApplicationState state);
};

/// The question of the unsaved work after the end of the session of the system has been canceled (A0, owner
/// 2026-11-21, QR3.1 changed and QR3.4): pdf4qt-quit-unsaved 955:145378 with the texts, buttons and defaults of
/// the question of the end of the session (the documents of all main windows, the work of the tools named in their
/// rows), shown from the event loop after the handler of commitDataRequest has returned, as a step of the flow of
/// the window (PDFAppLeaveFlow::Goal::SessionEnd: a close of the window and an assistant are refused meanwhile).
/// - Save All saves the documents one by one (the save of a tab and its signal saveFinished; a canceled or failed
///   save ends the step, the window stays), then the work of the tools named is not asked again;
/// - Quit Without Saving remembers the discarded versions and the work of the tools (PDFAppSessionEnd::
///   rememberDiscarded);
/// - Cancel, the question deleted without an answer: nothing is remembered.
/// The application never quits by itself. On Windows the system shows the reason of the blocked end of the session
/// while the question is open (ShutdownBlockReasonCreate). Ownership: a child of the window (it goes with it, its
/// question too); it deletes itself (deleteLater) when the step ends. One at a time in the process.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSessionEndQuestion : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppSessionEndQuestion(PDFAppMainWindow* window);
    virtual ~PDFAppSessionEndQuestion() override;

    /// Posts the question for \p window: it is shown from the event loop (never inside the caller, the handler of
    /// the end of the session). A question, which exists already, or a flow of the window, which runs, is brought
    /// to the front instead (no second question). \returns the question, which waits or is open, nullptr = none.
    static PDFAppSessionEndQuestion* post(PDFAppMainWindow* window);

    /// The question of the process, which waits or is open (nullptr = none)
    static PDFAppSessionEndQuestion* getCurrent();

    /// The open dialog (nullptr before it is shown and after its answer)
    PDFAppQuitUnsavedDialog* getDialog() const;

    /// Save All of the question runs
    bool isSaving() const { return m_isSaving; }

private:
    void ask();
    void onChosen(pdf::PDFAppDocumentChoice choice);
    void saveNext();
    void onSaveFinished(bool isSaved);

    /// Ends the step of the flow (deleteLater; the end of the flow is the last call)
    void end(bool isDone);

    /// The reason of the blocked end of the session shown by the system (Windows)
    void setShutdownBlocked(bool isBlocked);

    static QPointer<PDFAppSessionEndQuestion>& getCurrentPointer();

    QPointer<PDFAppMainWindow> m_window;
    QPointer<PDFAppQuitUnsavedDialog> m_dialog;
    std::vector<QPointer<PDFAppDocumentTab>> m_tabs;
    std::vector<QPointer<const PDFAppPage>> m_toolPages;
    std::vector<QPointer<PDFAppDocumentTab>> m_saveQueue;
    QPointer<PDFAppDocumentTab> m_savingTab;
    int m_saveAttempts = 0;
    bool m_isSaving = false;
    bool m_isShutdownBlocked = false;
    bool m_isEnded = false;
};

}   // namespace pdf

#endif // PDFAPPSESSIONEND_H
