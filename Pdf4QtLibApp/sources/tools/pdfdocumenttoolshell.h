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

#include "pdfappdocumentpermissions.h"
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
#include <vector>

class QAction;
class QBoxLayout;
class QEvent;

namespace pdf
{

class PDFAppDocument;
class PDFDocumentToolCatalogEntry;
class PDFAppToolFileConflictDialog;
class PDFModifiedDocument;

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

    /// A result without the result dialog (PDFDocumentToolGUI::isResultDialogSkipped, Blank
    /// PDF, plan of step 2 of phase 2, 4.6): the written new document, which replaces the
    /// own tab of the tool (Accepted); empty = none
    /// Also "Open in this tab" of the result dialog (Images to PDF 336:3572, R16): the new
    /// unsaved document replaces the own tab
    QByteArray newDocumentData;
    QString newDocumentTitle;       ///< Title of the new document ("Untitled"; Save As offers "<title>.pdf")

    /// "Save as a file" of a tool, whose new document replaces its own tab (336:3572,
    /// PDFDocumentToolGUI::isResultInThisTab): the written file, which the page opens in place
    /// of the own tab (Accepted); empty = none
    QString newDocumentFilePath;
};

/// Undo and Redo of a tool, which changes the document of its tab while it is open
/// (PDFDocumentToolGUI::hasDocumentUndoSteps - Redact Content with its marks; plan of step 6 of
/// phase 2, P5 open point 3): the steps of the parameter model and the steps of the document committed
/// by the tool (PDFDocumentToolShell::commitDocumentChange) in one order, so Ctrl+Z takes back the
/// last thing done in the tool, whichever it was. A step of the document is the real step of the undo
/// stack of the document (PDFAppUndoStack::undoToolStep): after the tool the Undo of the document
/// continues where the tool has left it. A change of somebody else (an assistant, a reload) after the
/// last step of the tool ends the steps of the document of the tool - the tool never takes back a
/// change of somebody else; the steps stay in the Undo of the document.
///
/// Ownership: the shell owns the target (a child, deleted before the model); the model and the
/// document are held by QPointer. undo and redo run the slots of the document (a slot may close the
/// tab): the state is changed before, Q_EMIT is the last call.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCompositeUndoTarget : public PDFAppToolUndoTarget
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolUndoTarget;

public:
    explicit PDFAppToolCompositeUndoTarget(PDFAppToolParameterModel* model, PDFAppDocument* document, QObject* parent);
    virtual ~PDFAppToolCompositeUndoTarget() override;

    /// Records a step of the document committed by the tool: \p versionAfter is the version of the
    /// document right after the commit (PDFAppDocument::getVersion of the expected version + 1)
    void recordDocumentStep(quint64 versionAfter);

    virtual bool canUndo() const override;
    virtual bool canRedo() const override;
    virtual void undo() override;
    virtual void redo() override;
    virtual QString getUndoText() const override;
    virtual QString getRedoText() const override;

    /// Steps, which Undo / Redo can reach (tests)
    size_t getUndoStepCount() const { return m_undoSteps.size(); }
    size_t getRedoStepCount() const { return m_redoSteps.size(); }

private:
    enum class Source
    {
        Parameters, ///< A step of the parameter model
        Document    ///< A step of the undo stack of the document committed by the tool
    };

    /// The step can be taken back (\p isUndo) or done again now
    bool isAvailable(Source source, bool isUndo) const;

    /// The next step of the document of the tool is on the top of its undo stack, but the document takes
    /// no change now (an operation blocking the changes, its lock): the step waits, it is not skipped
    bool isWaitingForDocument(bool isUndo) const;

    /// The index of the step, which Undo (\p isUndo) or Redo acts on, -1 = none
    int findStep(bool isUndo) const;

    /// Undo or Redo of the step found by findStep
    void apply(bool isUndo);

    /// The model has a new step of the user, or it has done Undo / Redo itself
    void onModelUndoStateChanged();

    QPointer<PDFAppToolParameterModel> m_model;
    QPointer<PDFAppDocument> m_document;
    std::vector<Source> m_undoSteps;
    std::vector<Source> m_redoSteps;
    quint64 m_modelStepCount = 0;       ///< PDFAppToolParameterModel::getRecordedStepCount seen last
    quint64 m_documentVersion = 0;      ///< The version of the document after the last step of the tool
    bool m_isApplying = false;          ///< undo / redo of this target runs
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

    /// Cancel, Esc: the tool ends, the document stays as it is; after the run (Deciding) it is
    /// Cancel / Close of the result dialog first (cancelResult). In the tab of a document the GUI may
    /// ask first (PDFDocumentToolGUI::askClose: Sign by Hand 435:10500, Redact 448:17473): the tool
    /// stays until the answer (closeAfterQuestion).
    void cancel();

    /// The question of PDFDocumentToolGUI::askClose has been answered by closing (Discard Drawing, Keep
    /// Marks and Close, Remove Marks): the tool ends as Cancel ends it, without asking again. Nothing
    /// outside of the state Editing. The shell, the GUI and the tab may be deleted by it (the last call).
    void closeAfterQuestion();

    /// The question of PDFDocumentToolGUI::askClose is being asked (the re-entry guard)
    bool isAskingClose() const { return m_isAskingClose; }

    /// Commits a change of the document of the tab as a change of the tool (PDFDocumentToolGUI::
    /// commitDocumentChange): PDFAppChangeSource::Tool, a step of Undo of the tool in its order
    /// (PDFAppToolCompositeUndoTarget) for a GUI with hasDocumentUndoSteps. Only in the state Editing.
    /// \returns the commit was accepted; the slots of the document may delete the shell meanwhile.
    bool commitDocumentChange(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion);

    /// What Undo and Redo of the tool act on: the composite target of a GUI with hasDocumentUndoSteps,
    /// else the model (nullptr before enter)
    PDFAppToolUndoTarget* getUndoTarget() const;

    /// The tab leaves the tool by a navigation (F14). A run asks as close-running:
    /// Stop and Leave stops it, Keep Running refuses. \returns the tool can be left.
    /// The shell may be deleted meanwhile.
    bool confirmLeave();

    /// The page has refused a close request of the state Editing (the own tab of a tool
    /// stays, the user has canceled the question K16): the shell takes requests again. A
    /// refused close of a decided result (its written file has not opened in the own tab)
    /// returns the tool to Editing with its inputs.
    void resumeEditing();

    /// The answer of the question of PDFDocumentToolGUI::confirmResult (307:6388): true = the result
    /// dialog opens with the result, false = the result is discarded as by Discard of the dialog
    /// (the decision of the runner ends; a document tab leaves the tool, the own tab returns to
    /// editing). Nothing, when no result waits for an answer (a stopped run). The shell, the GUI
    /// and the tab may be deleted meanwhile: the last call of the caller.
    void answerResult(bool isAccepted);

    /// A result waits for the answer of the question of the GUI (confirmResult)
    bool isConfirmingResult() const { return m_confirmingResult.has_value(); }

    /// The state of the result kept in the tool (PDFDocumentToolGUI::isResultKeptInTool; plan of step 7 of
    /// phase 2, 4.1, R7-2): Current after a successful run - the Primary button Disabled -, Stale after a change
    /// of the document (the signal of its tab) or of a parameter, which the GUI names (isResultStaleBy), or by
    /// markResultStale - the amber callout and the Primary button again; None before the first result
    PDFAppToolResultState getResultState() const { return m_resultState; }

    /// A Current result becomes Stale (PDFDocumentToolGUI::markResultStale); nothing in another state
    void markResultStale();

    /// Opens the dialog of an export of the kept result (PDFDocumentToolGUI::openExport; R7-13: the controls of
    /// the file only, 480:15368, 478:15674): \p exportResult has exactly one output file and no document
    /// (PDFAppToolResultDialog::isAnalysisExport). Only in the state Editing, never during a run; the state is
    /// Deciding until the decision, which returns to Editing (the tool stays; the result keeps its state) and
    /// then tells the GUI (PDFDocumentToolGUI::onExportDecided). \returns the dialog was opened.
    bool openExportDialog(const PDFDocumentToolResult& exportResult);

    /// The amber callout of a stale result (492:109399), nullptr = none
    PDFAppToolCallout* getStaleCallout() const { return m_staleCallout.data(); }

    /// Computes the preview again (the inputs of a tool of more inputs have changed under
    /// the same parameters: a password accepted, a reading finished). Nothing outside of
    /// the state Editing.
    void refreshPreview();

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

    /// The callout of a certified document (owner decision of 13. 11. 2026): the run of the values
    /// of now would remove the certification (isCertificationRemoved), the user learns it while
    /// working in the tool, not only in the result dialog; nullptr = none. Amber with the icon of a
    /// certificate in the compact style of the callouts of Manage Pages (1479:35631), at the place
    /// of PDFDocumentToolGUI::getCertificationCalloutPlace; it follows the document and the
    /// parameters. Running stays allowed.
    PDFAppToolCallout* getCertificationCallout() const { return m_certificationCallout.data(); }

    /// A run of the tool with the values of now would remove the certification of the document of
    /// the tool: PDFDocMDPPermissions::isCertificationRemovedBy - the rule of the run itself
    bool isCertificationRemoved() const;

    /// The result dialog and the refusal of a missing permission (nullptr = none is open)
    PDFAppToolResultDialog* getResultDialog() const { return m_resultDialog.data(); }
    PDFMessageBox* getPermissionDialog() const { return m_permissionDialog.data(); }

    /// The question of a permission, which the values of the run need (PDFDocumentToolGUI::getRunPermissions):
    /// the owner password dialog or the refusal, nullptr = none is open; it deletes itself when closed
    QDialog* getRunPermissionDialog() const { return m_runPermissionDialog.data(); }

    /// The question of the permissions, which the values of the run need, asked when the Primary button is
    /// pressed: those of PDFDocumentToolGUI::getRunPermissions and, of a tool, whose run needs one of more
    /// permissions (PDFDocumentTool::getAnyOfPermissions - Sign with Certificate into an existing empty field,
    /// bit 9 or bit 6 of ISO 32000-2, Table 22), those alternatives, all with the parameters of the run - the
    /// rule of the validation of the core (PDFDocumentTool::isPermittedRun). std::nullopt = the run needs
    /// nothing besides the catalog entry, which the entry has checked.
    std::optional<PDFAppPermissionRequest> getRunPermissionRequest() const;

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

    /// Shows or removes the callout of a certified document by isCertificationRemoved; \p isPlacedAgain
    /// creates it again at its place (a new content of the right panel). No signal, no foreign code.
    void updateCertificationCallout(bool isPlacedAgain);
    /// \param isStoppedFilesResult The written files of a stopped run (403:29978): the tool returns
    ///        to its settings after the dialog; the flag is set only, when the dialog is opened
    void openResultDialog(const PDFDocumentToolResult& result, bool isStoppedFilesResult);

    /// Cancel of the toolbar after the run (owner 2026-11-09): the decision ends as Cancel / Close
    /// of the result dialog (383:11374, 403:29978); where the frames say nothing more, the tool
    /// is then left as Cancel leaves it in Editing
    void cancelResult();
    bool waitForModalDialog(const PDFDocumentToolResult& result);
    void watchModalDialog(QWidget* dialog);
    void onWatchedModalClosed();
    void showPermissionRefusal();

    /// The permissions of the document do not allow the tool (PDFAppDocumentPermissions)
    bool isRefusedByPermissions() const;

    /// Asks PDFDocumentToolGUI::askClose in the tab of a document (re-entry guarded). \returns true =
    /// the tool stays (a question is open, or the shell has gone meanwhile)
    bool askCloseQuestion();
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
    void recordUse();
    void commitPendingValues();

    /// Retry N Files / Choose Folder… of the result dialog (K4.11, R4-20): runs the tool again with
    /// the parameters of its panel, only for the names (onlyOutputs), into \p folder when it is set
    void retryFiles(const QStringList& fileNames, const QString& folder);
    void onDocumentLost();
    void onDocumentLostQueued();
    void onDocumentChanged();
    void onDialogDecided(const pdf::PDFAppToolResultOutcome& outcome);
    void onDialogStatusChanged();
    void onModeLeft();
    void onLockChanged(bool isLocked);
    void onPermissionDialogFinished();
    void onInputsChanged();

    /// A result, which needs no decision (PDFDocumentToolGUI::isResultDialogSkipped, plan of
    /// step 2 of phase 2, 4.6): the new document is written (its bytes from the worker of the
    /// run, else by the writer in a worker) and the own tab is replaced by it
    void acceptWithoutDialog(const PDFDocumentToolResult& result);
    void onSkippedWriteFinished(const pdf::PDFAppToolResultWriter::Result& result);
    void finishAccepted(const QByteArray& data);

    /// Discard result of the own tab of a tool (Q9): the tool stays with its inputs
    void returnToEditing();

    /// A successful result of a GUI, which keeps it (PDFDocumentToolGUI::isResultKeptInTool): the use, the last
    /// values, the result to the GUI, the dialog of its export or the end of the decision (see getResultState)
    void keepResult(const PDFDocumentToolResult& result);

    /// Sets the state of the kept result: the callout, the toolbar and the status follow (no signal)
    void setResultState(PDFAppToolResultState state);

    /// Shows or removes the callout of a stale result; \p isPlacedAgain creates it again at its place
    void updateStaleCallout(bool isPlacedAgain);

    /// The decision of the dialog of an export: the tool returns to Editing, the GUI learns the outcome last. The
    /// file of a run, which was not saved (Discard), keeps nothing: the result state is None again.
    void finishExport(const PDFAppToolResultOutcome& outcome);

    /// The question of a file of the run (356:11997, R3): the conflict dialog is shown or closed
    void onFileQuestionChanged();
    void onFileConflictAnswered(pdf::PDFAppToolFileSink::Answer answer, bool isForAll);
    void closeFileConflictDialog();

    QPointer<PDFAppToolHost> m_host;
    std::shared_ptr<const PDFDocumentTool> m_tool;
    QPointer<PDFDocumentToolGUI> m_gui;
    QPointer<PDFAppToolParameterModel> m_model;
    QPointer<PDFDocumentToolRunner> m_runner;
    QPointer<PDFAppToolCompositeUndoTarget> m_undoTarget;  ///< A child (PDFDocumentToolGUI::hasDocumentUndoSteps), nullptr = the model is the target

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
    QPointer<PDFAppToolCallout> m_certificationCallout;    ///< Owned by the right panel or by the content of the GUI (its layout); dies with them
    QPointer<PDFAppToolResultDialog> m_resultDialog;
    QPointer<PDFAppToolFileConflictDialog> m_fileConflictDialog;   ///< The question of an existing file during a run (new, deletes itself)
    bool m_isStoppedFilesResult = false;    ///< The result dialog shows the written files of a stopped run: the tool returns to its settings (403:29978)
    QPointer<PDFMessageBox> m_permissionDialog;
    QPointer<QDialog> m_runPermissionDialog;    ///< Not owned: the dialog of PDFAppDocumentPermissions::request deletes itself
    QPointer<QDialog> m_leaveDialog;   ///< The question of the leaving during a run (F14)
    QPointer<PDFAppToolResultWriter> m_skippedWriter;   ///< Writes the new document of a result without the dialog (4.6)
    QPointer<QWidget> m_watchedModal;   ///< The modal dialog of the application, for which a result waits
    QPointer<QAction> m_escapeAction;
    QPointer<QAction> m_confirmAction;
    QPointer<QAction> m_confirmKeypadAction;

    QElapsedTimer m_runTimer;
    std::optional<PDFDocumentToolResult> m_pendingResult;   ///< A result, which came while the user is asked (F14) or while a modal dialog is open
    std::optional<PDFDocumentToolResult> m_confirmingResult;    ///< A result, about which the GUI asks before the dialog (confirmResult, 307:6388)
    QString m_stepText;
    int m_percentage = 0;
    State m_state = State::Created;
    bool m_isAskingLeave = false;
    bool m_isWaitingForModal = false;   ///< m_pendingResult waits for m_watchedModal
    bool m_isPermissionRefused = false;
    bool m_isRebuilding = false;
    bool m_isCloseRequested = false;
    bool m_isLeavingMode = false;   ///< The host is moving the parts back (close)
    bool m_isAskingClose = false;   ///< PDFDocumentToolGUI::askClose runs (re-entry guard)
    bool m_isExportDialog = false;  ///< The result dialog is the export of a kept result: its decision returns to Editing
    bool m_isRunExportDialog = false;   ///< The export is the file of a run (keepResult): not saved, nothing is kept
    PDFAppToolResultState m_resultState = PDFAppToolResultState::None;
    QPointer<PDFAppToolCallout> m_staleCallout;     ///< Owned by the right panel or by the content of the GUI (its layout); dies with them
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppToolCloseRequest)

#endif // PDFDOCUMENTTOOLSHELL_H
