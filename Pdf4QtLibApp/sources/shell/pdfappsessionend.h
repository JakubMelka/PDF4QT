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

#include <QEventLoop>
#include <QObject>
#include <QPointer>

class QSessionManager;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTab;

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

/// The end of the session of the system - logoff, shutdown, restart (step 9, N7).
/// Qt neither closes the windows nor asks them: on Windows it allows the end of the
/// session at WM_QUERYENDSESSION and the process ends after aboutToQuit. The handler
/// of QGuiApplication::commitDataRequest asks about the documents with unsaved
/// changes of every main window of the process at once by pdf4qt-quit-unsaved
/// 955:145378, as a quit does:
/// - Cancel cancels the end of the session;
/// - Quit Without Saving lets the session end (the windows, which Qt closes then,
///   do not ask about the same changes again - isDiscarded);
/// - Save All saves the documents before it returns (the session ends after the
///   handler; a save, which runs already, is waited for), a failed or canceled
///   save cancels the end of the session.
/// Without the permission to ask the user the end of the session is canceled (no
/// design rule of an automatic save or a recovery exists - owner question of
/// step 9). When the session may end, the last session and the geometry of the
/// active (else the last active) main window are stored as at a quit (O29).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSessionEnd
{
public:
    PDFAppSessionEnd() = delete;

    /// What the handler has done
    enum class Outcome
    {
        NoChanges,                  ///< No unsaved changes: the session may end
        Discarded,                  ///< Quit Without Saving: the session may end
        Saved,                      ///< Save All has saved every document: the session may end
        Canceled,                   ///< Cancel, a failed save, the dialog gone: the end canceled
        CanceledWithoutInteraction  ///< Unsaved changes and no interaction allowed: the end canceled
    };

    /// Installs the handler of commitDataRequest once per process (the main window
    /// calls it; the connection lives as long as the application, no object is created)
    static void install();

    /// The handler is connected to commitDataRequest of the application
    static bool isInstalled();

    /// Handles the end of the session for every main window of the process
    static Outcome commitData(PDFAppSessionManager* manager);

    /// The user has discarded the changes of \p document at the last end of the
    /// session (Quit Without Saving) and the document has not changed since. Qt
    /// closes the windows after the session is allowed to end (Windows: quit() at
    /// WM_ENDSESSION, X11: Die of the session manager): a window does not ask about
    /// these changes again (PDFAppMainWindow::confirmClose).
    static bool isDiscarded(const PDFAppDocument* document);

private:
    static QMetaObject::Connection& getConnection();

    /// Saves the document of the tab and waits for the file (the version of the
    /// question, a newer version is saved again). \returns false, when the save
    /// has failed, has been canceled or the tab has gone meanwhile.
    static bool saveAndWait(PDFAppDocumentTab* tab);

    /// The last session and the geometry of the window of the quit (O29)
    static void storeSessionState();

    static void onCommitDataRequest(QSessionManager& manager);
};

/// Waits in its own event loop for the end of a save of a document tab (Save All of
/// the end of the session must finish before the handler returns)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSessionSaveWaiter : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppSessionSaveWaiter(PDFAppDocumentTab* tab);
    virtual ~PDFAppSessionSaveWaiter() override;

    /// Starts the save of the tab and waits for its end. \returns true, when the
    /// version of the start is in the file.
    bool saveAndWait();

private:
    void onSaveFinished(bool isSaved);
    void onTabDestroyed();

    QPointer<PDFAppDocumentTab> m_tab;
    QEventLoop m_loop;
    bool m_isFinished = false;
    bool m_isSaved = false;
};

}   // namespace pdf

#endif // PDFAPPSESSIONEND_H
