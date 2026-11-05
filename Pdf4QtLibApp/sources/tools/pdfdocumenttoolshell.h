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

#ifndef PDFDOCUMENTTOOLSHELL_H
#define PDFDOCUMENTTOOLSHELL_H

#include "pdfappglobal.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolrunner.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <optional>

class QAction;
class QBoxLayout;
class QEvent;

namespace pdf
{

class PDFDocumentToolCatalogEntry;

/// How a shell asks its page to end (the page chooses the route, F15)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCloseRequest
{
public:
    enum class Reason
    {
        Canceled,   ///< Cancel, Esc, Close of the refusal: the document is unchanged
        Discarded,  ///< Discard result of the result dialog: the document is unchanged
        Accepted,   ///< The result was accepted
        Lost,       ///< The document has left the state Ready (F2)
        Refused     ///< The tool cannot work on the document any more (a lock, T3)
    };

    Reason reason = Reason::Canceled;
    QString continueToolId;         ///< Then continue with: the next tool, empty = none
    QString continueDocumentId;     ///< The document of the next tool (F11)
    QString statusMessage;          ///< Message of the status bar after the end, empty = none
    QString folderToOpen;           ///< Open the folder afterwards (an export), empty = no
};

/// The common part of a tool in a tab (plan of the first batch, chapters 2.1 and 2.4).
/// The flow is one state machine:
///
///     Editing --confirm--> Running --Success--> Deciding --accepted--> closeRequested
///        ^                    |                     |
///        +--Stopped, Failed---+    +---Discard------+-->  closeRequested
///        +--NothingToDo-------+    +---Rerun: Running (a new version of the document)
///
/// The shell owns the model of the parameters, the runner, the GUI and the parts of the
/// mode (the view deletes the parts, when it dies first, so they are held in QPointer).
/// \p enter saves the state of the view, \p close gives it back. The keys are Esc
/// (Stop of a run, otherwise Cancel) and Ctrl+Enter (the Primary button). The shell never
/// navigates: its page does, after \p closeRequested. Every signal may delete the shell.
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolShell : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    enum class State
    {
        Created,    ///< \p enter has not been called
        Editing,    ///< The user changes the parameters
        Running,    ///< The tool computes its result, the document is busy
        Deciding,   ///< The result dialog is open
        Closed      ///< The mode has been left
    };
    Q_ENUM(State)

    /// \param host Places of the tool, must outlive the shell
    /// \param tool The tool, never null
    /// \param gui GUI of the tool, the shell takes it over
    /// \param parent Parent object
    explicit PDFDocumentToolShell(PDFAppToolHost* host, std::shared_ptr<const PDFDocumentTool> tool, PDFDocumentToolGUI* gui, QObject* parent);

    /// Leaves the mode and deletes the parts; nothing waits for a worker
    virtual ~PDFDocumentToolShell() override;

    /// Enters the tool: the parts are created and shown, the preview starts. \returns
    /// false, when the mode cannot be entered (nothing is left of it). The shell may
    /// be deleted meanwhile by a slot (the caller guards it).
    bool enter();

    /// Ends the tool at once, without a question: a run is stopped, the mode is left and
    /// the state of the view given back. The document stays as it is.
    void close();

    /// Confirms the tool (the Primary button, Ctrl+Enter): a finished preview, which is
    /// the result, opens the dialog at once, otherwise the tool runs. Nothing in another
    /// state than Editing or while the button is disabled.
    void confirm();

    /// Stops the run (Stop, Esc): nothing is kept, the user edits again
    void stop();

    /// Cancel, Esc: the tool ends, the document stays as it is
    void cancel();

    /// The tab leaves the tool by a navigation (F14). A run asks as close-running:
    /// Stop and Leave stops it, Keep Running refuses. \returns the tool can be left.
    /// The shell may be deleted meanwhile.
    bool confirmLeave();

    State getState() const { return m_state; }

    PDFAppToolHost* getHost() const { return m_host.data(); }
    PDFAppToolParameterModel* getModel() const { return m_model.data(); }
    PDFDocumentToolRunner* getRunner() const { return m_runner.data(); }
    PDFDocumentToolGUI* getGUI() const { return m_gui.data(); }
    const std::shared_ptr<const PDFDocumentTool>& getTool() const { return m_tool; }
    const PDFDocumentToolCatalogEntry* getCatalogEntry() const;

    /// Parts of the mode (nullptr = none or gone)
    PDFAppToolToolbar* getToolbar() const { return m_toolbar.data(); }
    PDFAppToolSidePanel* getRightPanel() const { return m_rightPanel.data(); }
    PDFAppToolSidePanel* getLeftPanel() const { return m_leftPanel.data(); }
    QWidget* getCanvasPage() const { return m_canvasPage.data(); }
    QWidget* getOverlay() const { return m_overlay.data(); }
    QWidget* getToolActions() const { return m_toolActions.data(); }
    QWidget* getRightContent() const { return m_rightContent.data(); }
    QWidget* getLeftContent() const { return m_leftContent.data(); }

    /// Card of the progress and the callout The document is busy of a run
    PDFAppToolProgressCard* getProgressCard() const { return m_progressCard.data(); }
    PDFAppToolCallout* getBusyCallout() const { return m_busyCallout.data(); }

    /// The result dialog and the refusal of a missing permission (nullptr = none is open)
    PDFAppToolResultDialog* getResultDialog() const { return m_resultDialog.data(); }
    PDFMessageBox* getPermissionDialog() const { return m_permissionDialog.data(); }

    /// The document does not give the permission of the tool (339:10328)
    bool isPermissionRefused() const { return m_isPermissionRefused; }

    /// Message of the status bar of the shell
    QString getStatusMessage() const;

    /// Time left of the run in seconds by its progress, -1 = not known yet
    int getRemainingSeconds(int percentage) const;

    /// Text of the time left ("About 40 seconds left", 295:2155), empty = not known yet
    static QString formatRemainingTime(int seconds);

Q_SIGNALS:
    /// The state has changed
    void stateChanged(pdf::PDFDocumentToolShell::State state);

    /// The tool wants to end: the page closes the shell and shows the next route
    void closeRequested(const pdf::PDFAppToolCloseRequest& request);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Creates the parts of the GUI (at the entry, again after a change of the language)
    void createContent();

    void setState(State state);
    void updateToolbar();
    void updateStatus();
    void updatePreview();
    void applyEnvironment();
    void showProgress(bool isShown);
    void updateProgress(int percentage, const QString& stepText);
    void openResultDialog(const PDFDocumentToolResult& result);
    bool waitForModalDialog(const PDFDocumentToolResult& result);
    void watchModalDialog(QWidget* dialog);
    void onWatchedModalClosed();
    void showPermissionRefusal();
    void requestClose(const PDFAppToolCloseRequest& request);
    void onLanguageChanged();
    void deleteParts();

    void onParameterChanged(const QString& name);
    void onConfirmStateChanged();
    void onEscape();
    void onPreviewProgress(int percentage, const QString& stepText);
    void onPreviewFinished(const pdf::PDFDocumentToolResult& result);
    void onPreviewDropped();
    void onRunStarted();
    void onRunProgress(int percentage, const QString& stepText);
    void onRunFinished(const pdf::PDFDocumentToolResult& result);
    void finishRun(const PDFDocumentToolResult& result);
    void recordNothingToDo();
    void commitPendingValues();
    void onDocumentLost();
    void onDocumentLostQueued();
    void onDocumentChanged();
    void onDialogDecided(const pdf::PDFAppToolResultOutcome& outcome);
    void onDialogStatusChanged();
    void onModeLeft();
    void onLockChanged(bool isLocked);
    void onPermissionDialogFinished();

    QPointer<PDFAppToolHost> m_host;
    std::shared_ptr<const PDFDocumentTool> m_tool;
    QPointer<PDFDocumentToolGUI> m_gui;
    QPointer<PDFAppToolParameterModel> m_model;
    QPointer<PDFDocumentToolRunner> m_runner;

    QPointer<PDFAppToolToolbar> m_toolbar;
    QPointer<PDFAppToolSidePanel> m_rightPanel;
    QPointer<PDFAppToolSidePanel> m_leftPanel;
    QPointer<QWidget> m_canvasPage;
    QPointer<QWidget> m_overlay;
    QPointer<QWidget> m_toolActions;
    QPointer<QWidget> m_rightContent;
    QPointer<QWidget> m_leftContent;
    QPointer<QWidget> m_partsParent;    ///< Holds the parts, while they are not shown
    QPointer<PDFAppToolProgressCard> m_progressCard;
    QPointer<PDFAppToolCallout> m_busyCallout;
    QPointer<PDFAppToolResultDialog> m_resultDialog;
    QPointer<PDFMessageBox> m_permissionDialog;
    QPointer<QDialog> m_leaveDialog;    ///< The question of the leaving during a run (F14)
    QPointer<QWidget> m_watchedModal;   ///< The modal dialog of the application, for which a result waits
    QPointer<QAction> m_escapeAction;
    QPointer<QAction> m_confirmAction;
    QPointer<QAction> m_confirmKeypadAction;

    QElapsedTimer m_runTimer;
    std::optional<PDFDocumentToolResult> m_pendingResult;   ///< A result, which came while the user is asked (F14) or while a modal dialog is open
    QString m_stepText;
    int m_percentage = 0;
    State m_state = State::Created;
    bool m_isAskingLeave = false;
    bool m_isWaitingForModal = false;   ///< m_pendingResult waits for m_watchedModal
    bool m_isPermissionRefused = false;
    bool m_isRebuilding = false;
    bool m_isCloseRequested = false;
    bool m_isLeavingMode = false;   ///< The host is moving the parts back (close)
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppToolCloseRequest)

#endif // PDFDOCUMENTTOOLSHELL_H
