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
#include "pdfdeferreddeletion.h"
#include "pdfappleaveflow.h"
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
class QTimer;
class QBoxLayout;
class QEvent;

namespace pdf
{

class PDFAppDocument;
class PDFDocumentToolCatalogEntry;
class PDFAppToolFileConflictDialog;
class PDFModifiedDocument;

/// The choice of a choice dialog (pdfappdocumentdialogs.h)
enum class PDFAppDocumentChoice;

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

    /// The save, which the user asked for by the choice of PDFAppToolSaveQuestion, which runs the tool first
    /// ("Apply and Save", zadani_gui.md 4.21): the command of the document (PDFAppCommands::save / saveAs), which the
    /// page executes on the tab after the tool has closed with its result kept in the tab (Accepted); empty = none
    QString saveCommandId;
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

/// The clock of the visible time of an interactive tool (PDFDocumentToolShell::accountVisibleTime): monotonic
/// milliseconds. The tests replace it (PDFDocumentToolShell::setUseClock), so no test waits 10 real seconds.
class PDF4QTLIBAPPSHARED_EXPORT IPDFAppToolUseClock
{
public:
    virtual ~IPDFAppToolUseClock();
    virtual qint64 getMilliseconds() const = 0;
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
///
/// Deferred deletion (step A): the shell is closed only by PDFDeferredDeletion::retire (by closing its page or
/// tab, PDFAppToolTabPage / PDFAppToolDocumentPage retire a shell, which could not enter); its hook retire() ends
/// the mode of the tool at once. Ownership: the GUI, the model, the runner, the undo target and the actions are
/// children; the holder of the parts, the parts (toolbar, panels, canvas page - in the layout of the view during the
/// mode), the overlay (a child of the canvas area) and the dialogs of the tool parented to the window (the result
/// dialog, the refusal of a permission, the question of a file, the question of a leave during a run) belong to the
/// shell logically (PDFDeferredDeletion::setOwner) and are retired with it. The tool mode of the host is a token
/// (a child), released by close, by retire after the hooks, or with the shell. close retires the runner and the
/// overlay (never deletes them: their code may be on the stack - the input of the overlay, a signal of the runner);
/// inside of an emission of the runner (a slot of the shell connected to it runs) the runner's work ends at once
/// (PDFDocumentToolRunner::shutDown) and the runner is retired from the event loop, so the later receivers of the
/// emission still run (RA-L7). Code after a foreign call checks PDFDeferredDeletion::isRetired (PA6): a retired
/// shell lives until the event loop and goes on with nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolShell : public QObject, public PDFRetirable
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
    /// be retired meanwhile by a slot (the caller checks it).
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
    /// outside of the state Editing. While a leave waits for the answer, the leave is accepted instead and the
    /// answer is final (owner 2026-11-24): a leave canceled later closes the tool (requestFinishLeave(false)).
    /// The shell, the GUI and the tab may be deleted by it (the last call).
    void closeAfterQuestion();

    /// The question of PDFDocumentToolGUI::askClose has been answered by its choice, which changes the document
    /// (PDFDocumentToolGUI::applyCloseQuestionAction: Redact "Remove New Marks", Measure "Save and Close"). While a
    /// leave waits for the answer (requestLeave), the choice is only remembered and the leave accepted (owner
    /// 2026-11-20, decision 8); the code, which started the leave, ends it by requestFinishLeave - the choice is
    /// applied either way (owner 2026-11-24: the answer is final; a leave canceled later applies it and closes the
    /// tool). \returns true = remembered, the GUI does nothing more (the waiter of the leave has run in this call:
    /// anything may have been deleted); false = no leave waits (Esc, Close in the tool), the GUI applies the choice at
    /// once.
    bool deferCloseQuestionAction();

    /// Ends the leave, which requestLeave has accepted (PDFAppPage::requestFinishLeave; called by the code, which
    /// started the leave, also when nothing was remembered). \p isProceeding - the leave goes on: the choice
    /// remembered by deferCloseQuestionAction is applied now (PDFDocumentToolGUI::applyCloseQuestionAction, while the
    /// tool still edits: the commit of the tool, one Undo step), the caller then goes on with the leave; false = the
    /// leave was canceled by a later step (another question, a save, a lock): when the question of the tool was
    /// answered by closing for this leave, the answer still stands (owner 2026-11-24) - from the event loop the
    /// remembered choice is applied (if it was not yet) and the tool closes as by its answer without a leave
    /// (onCanceledLeaveQueued) with a message in the status bar (the change of the answer can be undone); without such
    /// an answer the tool stays as it is. Called again with false after a true (the tab or the window stays after
    /// all), it closes the tool the same way. The answer covers only the work, which existed when it was given (and
    /// its applied choice): work of the user made later (during a save of the close or of Save All) keeps the tool
    /// open, so does a choice, which the document cannot take now (locked, an operation blocks the changes - the
    /// message says why). When the applied choice could not keep all of the work (owner 2026-11-21: a measurement,
    /// which cannot be stored, a locked mark), the tab of the tool is shown and its question about the rest
    /// (askIncompleteQuestion) is opened - after a canceled leave one question of the tools at a time (the next one
    /// waits for the answer of the open one). \returns Refused = the
    /// caller cancels the leave (no such question - the tool stays with the rest and has told why); Done = the leave
    /// goes on (nothing to apply, all kept; always for false); Pending = the question about the rest is open: its
    /// answer comes by \p flow->finishStep(this shell, Done / Refused) - Discard and Close Done, Keep Measuring
    /// Refused (A0: no event loop; without a running flow the question stays as the question of the tool and the
    /// result is Refused). The slots of the commit may delete the shell, its tab or the window (the caller guards
    /// them); false runs no foreign code in the call.
    PDFAppLeaveResult requestFinishLeave(bool isProceeding, PDFAppLeaveFlow* flow);

    /// requestFinishLeave without a flow: \returns it is Done (\p isProceeding false never asks)
    bool finishLeave(bool isProceeding);

    /// A leave accepts the page of the tool without asking it (the tool is not edited, or it works on a locked
    /// document): an answer given to an earlier leave is not this leave's - its end applies and closes nothing (owner
    /// 2026-11-24, L1 of the review RLC1). Nothing while a leave waits for the answer of the tool.
    void forgetLeaveAnswer();

    /// The applied choice of the question (PDFDocumentToolGUI::applyCloseQuestionAction) could not keep all of the
    /// work: the question of the GUI about the rest (PDFDocumentToolGUI::askIncompleteClose: Measure "Keep
    /// Measuring" / "Discard and Close", owner 2026-11-21) becomes the open question of the tool - its Keep leaves
    /// the tool open, its closing answer calls closeAfterQuestion (outside of a wait the tool closes, during a wait
    /// of finishLeave the leave goes on). \returns the open question, nullptr = the GUI asks nothing. No signal; the
    /// GUI never deletes the shell in it.
    QDialog* askIncompleteQuestion();

    /// A choice of the question waits for the end of the leave (deferCloseQuestionAction, finishLeave)
    bool hasDeferredCloseQuestionAction() const { return m_isCloseQuestionActionDeferred; }

    /// The question of PDFDocumentToolGUI::askClose is being asked (the re-entry guard)
    bool isAskingClose() const { return m_isAskingClose; }

    /// The open question of PDFDocumentToolGUI::askClose (nullptr = none)
    QDialog* getCloseQuestion() const { return m_closeQuestion.data(); }

    /// The tool in the tab of a document has work of the user, which leaving it loses (PDFDocumentToolGUI::
    /// hasUnsavedWork while it is edited, or its question is open; owner 2026-11-20). An assistant never
    /// closes such a tool (its close is refused, never asked over the user's work).
    bool hasUnsavedWork() const;

    /// A number, which changes with the state of the GUI (its Primary button, its status, its own tab, a parameter):
    /// an answer about the unsaved work holds only while the number is the same (PDFAppSessionEnd)
    quint64 getWorkStamp() const { return m_workStamp; }

    // ------------------------------------------------------------- use of an interactive tool (owner 2026-11-20)

    /// The visible time, after which an interactive tool is a use of the tool (Recent tools) when it is left
    static constexpr qint64 INTERACTIVE_USE_VISIBLE_MS = 10000;

    /// Replaces the clock of the visible time (tests); nullptr = the monotonic clock of the application. The
    /// clock must outlive its use.
    static void setUseClock(const IPDFAppToolUseClock* clock);

    /// Adds the time since the last accounting to the visible time, when the tool was visible since then (its tab
    /// is the current one and the window is not minimized), and notes whether it is visible now. No timer: the
    /// events, which change the visibility, call it while an interactive tool is open (Show / Hide / ParentChange of
    /// the key widget, WindowStateChange / Show / Hide of its window - the event filter of the shell), the leave
    /// once more (recordInteractiveUse).
    void accountVisibleTime();

    /// The time the interactive tool has been visible in this opening (ms)
    qint64 getVisibleTime() const { return m_visibleTime; }

    /// The user has done something in an interactive tool (PDFDocumentToolGUI::markUsed: a measurement, an export,
    /// a toggle, a selected object): leaving it records the use
    void markInteractiveUse();

    /// The use of the tool has been recorded in this opening (at most one record per opening)
    bool isUseRecorded() const { return m_isUseRecorded; }

    /// The tool is left (close) or its tab or window is about to be torn down (PDFAppPage::prepareClose, while the
    /// tab is whole): an interactive tool, whose use has not been recorded in this opening, records it, when the
    /// user did something in it or it was visible for INTERACTIVE_USE_VISIBLE_MS (owner 2026-11-20). Nothing
    /// before enter and after close; the destructor never records (the tab is no longer whole then). No signal.
    void recordInteractiveUse();

    /// A question of the tool waits for the answer of the user: the question of PDFDocumentToolGUI::askClose
    /// (open, being opened, or waited for by a leave) or the question of a run (close-running). An assistant
    /// changes no tab while it is open (owner 2026-11-20, RESCCODE L6).
    bool isQuestionOpen() const;

    /// Commits a change of the document of the tab as a change of the tool (PDFDocumentToolGUI::
    /// commitDocumentChange): PDFAppChangeSource::Tool, a step of Undo of the tool in its order
    /// (PDFAppToolCompositeUndoTarget) for a GUI with hasDocumentUndoSteps. Only in the state Editing.
    /// \returns the commit was accepted; the slots of the document may delete the shell meanwhile.
    bool commitDocumentChange(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion);

    /// What Undo and Redo of the tool act on: the own target of the GUI (PDFDocumentToolGUI::getOwnUndoTarget,
    /// read once in enter; nullptr again, when the GUI has deleted it), else the composite target of a GUI with
    /// hasDocumentUndoSteps, else the model (nullptr before enter)
    PDFAppToolUndoTarget* getUndoTarget() const;

    /// The state of a command of the document for the open tool (PDFDocumentToolGUI::getDocumentCommandState;
    /// zadani_gui.md 4.20, way 2): NotHandled before enter, after close, for a tool in its own tab, for Undo and
    /// Redo (the GUI is not asked: they act on the undo target of the mode, getUndoTarget) and for a command
    /// the GUI does not handle; a command the GUI handles is Disabled outside of the state Editing (a run, a
    /// decision) and while the GUI says so. No side effects. The actions of the window follow the state of the
    /// shell by themselves (refreshDocumentCommands), a change inside of the GUI by
    /// PDFDocumentToolGUI::updateDocumentCommands.
    PDFAppToolCommandState getDocumentCommandState(const QString& commandId) const;

    /// Gives the command to the GUI (PDFDocumentToolGUI::handleDocumentCommand), when its state is Enabled.
    /// \returns true = the tool has taken it. The GUI may close the tool or the tab: the last call of the caller.
    bool handleDocumentCommand(const QString& commandId);

    /// The document of the tab is about to be saved by the command \p commandId (PDFAppCommands::save / saveAs)
    /// of the user (zadani.md KE.3, zadani_gui.md 4.21): a tool, which is edited and holds unsaved work, asks the
    /// question of its GUI (PDFDocumentToolGUI::getSaveQuestion; a message box of the window, open - never exec).
    /// \returns true = the question is open (or was open already and came forward): the caller saves nothing, the
    /// answer goes on - Cancel nothing; the save choice executes the save without asking again
    /// (PDFAppDocumentFileCommandHandler::save / saveAs; Save of a document, which has nothing of its own to save -
    /// PDFAppDocumentSaver::canSave - writes nothing) and the tool stays open; the apply choice confirms the tool
    /// (confirm(): the usual run with the result dialog) and, when its result is kept in the tab, the page saves
    /// after the tool has closed (PDFAppToolCloseRequest::saveCommandId) - any other end of the run saves nothing.
    /// false = nothing to ask, the caller saves.
    bool requestSaveQuestion(const QString& commandId);

    /// requestSaveQuestion would ask now: the tool is edited (not retired), it holds unsaved work (hasUnsavedWork)
    /// and its GUI has a question (PDFDocumentToolGUI::getSaveQuestion is valid). The handler of Save enables the
    /// command by it also for a document without changes of its own (PDFAppPage::hasSaveQuestion). No side effects;
    /// when the value changes, the shell refreshes the actions of the window (updateSaveQuestionState).
    bool hasSaveQuestion() const;

    /// The open question of requestSaveQuestion (nullptr = none)
    PDFMessageBox* getSaveQuestion() const { return m_saveQuestion.data(); }

    /// The automatic refresh does not reload the document of the tab from its changed file now: the GUI holds work,
    /// which belongs to the version of the document it was made on (PDFDocumentToolGUI::isDocumentReloadDeferred;
    /// Edit Content, zadani_gui.md 4.21; R3-6). false before enter, after close, when retired and for a GUI without
    /// the hook (every tool but Edit Content). No side effects; PDFAppToolDocumentPage passes it on.
    bool isDocumentReloadDeferred() const;

    /// The save, which follows a result kept in the tab (the apply choice of the save question); empty = none
    const QString& getSaveAfterResultCommandId() const { return m_saveAfterResultCommandId; }

    /// The tab leaves the tool by a navigation (F14), its close or the close of the window (A0: without a nested
    /// event loop, a step of \p flow). A run asks as close-running: Stop and Leave = Done (the caller stops the run),
    /// Keep Running = Refused. A tool with unsaved work in the tab of a document (owner 2026-11-20) asks its question
    /// (PDFDocumentToolGUI::askClose): Discard (or its third choice, which keeps the work) lets the tool go, Keep
    /// refuses; a question open already (Esc) is waited for the same way. \returns Done = the tool can be left (it is
    /// not closed here), Refused = it stays, Pending = its question is open: the answer comes by \p flow->finishStep(
    /// this shell, Done / Refused) exactly once, never inside the call (the caller waits by PDFAppLeaveFlow::
    /// waitForStep(shell, caller, slot) right after it; a shell deleted first is Refused). A second leave while one
    /// waits is Refused. The activation of the tab runs code of the application: the shell may be deleted meanwhile.
    PDFAppLeaveResult requestLeave(PDFAppLeaveFlow* flow);

    /// A leave waits for the answer of a question of the tool (requestLeave, requestFinishLeave); a flow, which has
    /// ended meanwhile (canceled from outside), waits no more
    bool isWaitingForLeave() const;

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

    /// The hook of PDFDeferredDeletion::retire (step 4, PDFRetirable): the inert part of close, without the signals of
    /// the shell and without foreign answers - the state Closed (no stateChanged, no closeRequested), no question is
    /// answered and no leave is reported (the flow, which waits for this shell, refuses its step by the end of the
    /// shell, PDFAppLeaveFlowStep), the watching of the visibility and of a modal dialog ends, a result waiting for a
    /// decision is dropped, the info bar of the result goes. The rest is done by retire itself: the runner (a child)
    /// cancels its workers and ends the operation of the document in its hook, the GUI has its hook, the parts, the
    /// overlay and the dialogs of the tool are retired with the shell (setOwner, the dialogs closed without their
    /// signals), and the token of the mode gives the view its parts and its state back right after the hooks (step 4)
    /// - another tool never finds the view in the mode or the document busy after retire. Use of an interactive tool
    /// is not recorded here (PDFAppPage::prepareClose and close do it, as the destructor never did). No dialog, no
    /// wait.
    virtual void retire() override;

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

    /// requestLeave in the state Editing: the question of the unsaved work
    PDFAppLeaveResult requestLeaveWork(PDFAppLeaveFlow* flow);

    /// The step of \p flow waits for the answer of the open question \p question (requestLeaveWork,
    /// requestFinishLeave): Pending; its closing answer (closeAfterQuestion, deferCloseQuestionAction) reports Done,
    /// its end without it Refused (from the event loop; Done when the tool has ended meanwhile)
    PDFAppLeaveResult waitForLeaveQuestion(QDialog* question, PDFAppLeaveFlow* flow);

    /// The question waited for has ended (finished, retired or destroyed): the answer is read from the event loop
    void onLeaveQuestionEnded();

    /// The connections of the question waited for (m_leaveQuestion) to this shell are cut
    void disconnectLeaveQuestion();
    void onLeaveQuestionSettled();

    /// The choice of the close question, which a leave applies, from the event loop: the leave was asked inside of
    /// the change notification of the document (a slot of documentChanged closed the tab), whose version this shell
    /// and its GUI have not seen yet - the choice would miss the change being announced (a mark just added). Applies
    /// it once and reports the step of the flow (the last call).
    void onFinishLeaveQueued();

    /// The leave, to which the question of the tool was answered by closing, was canceled by a later step
    /// (requestFinishLeave(false); owner 2026-11-24): from the event loop the remembered choice is applied (one Undo
    /// step) and the tool closes as by its closing answer without a leave; a choice, which could not keep all of the
    /// work, keeps the tool open with its question about the rest (askIncompleteQuestion, owner 2026-11-21)
    void onCanceledLeaveQueued();

    /// The answer of the leave is forgotten (the flags of requestFinishLeave(false), the signature of the work)
    void clearLeaveAnswer();

    /// The work of the user, which leaving the tool loses, as its question names it (PDFDocumentToolGUI::hasUnsavedWork
    /// and the texts of PDFDocumentToolGUI::getCloseQuestion): equal before and after = no work of the user since
    QString getLeaveWorkSignature() const;

    /// The message of the status bar of a tool closed by its answer after a canceled leave (\p isChangeApplied: the
    /// applied choice has changed the document - Undo takes it back)
    QString getCanceledLeaveMessage(bool isChangeApplied) const;

    /// The question about the rest of the work after a canceled leave (onCanceledLeaveQueued): one question of the
    /// tools at a time - while the question of another tool of the window is open, this one waits for its end
    /// (onOtherQuestionEnded); then the tab of the tool is shown and the question opened (the last call)
    void askIncompleteAfterCanceledLeave();

    /// The question of another tool, for which askIncompleteAfterCanceledLeave waits, has ended: the next try from
    /// the event loop (the other tool closes or stays first)
    void onOtherQuestionEnded();

    /// Stops the wait of askIncompleteAfterCanceledLeave (the connections to the other question are cut)
    void stopWaitingForOtherQuestion();

    /// The open question of another tool of the window of this tool (askClose, the question about the rest), nullptr =
    /// none
    QDialog* findOtherOpenQuestion() const;

    /// The answer of the question of a run (close-running)
    void onLeaveRunningChosen(PDFAppDocumentChoice choice);

    /// Ends the wait of the leave and reports \p result to the flow (the last call)
    void reportLeave(PDFAppLeaveResult result);

    /// The wait of a leave, whose flow has ended without the answer (its step dropped, L1 of the review of A0), is
    /// forgotten: the question stays as the question of the tool and its answer acts as without a leave.
    /// \returns true, when a leave still waits for the answer
    bool checkLeaveWait();

    /// The question of askClose is open (shown, not answered yet)
    bool isCloseQuestionOpen() const;
    void requestClose(const PDFAppToolCloseRequest& request);
    void onLanguageChanged();
    void deleteParts();

    void onParameterChanged(const QString& name);
    void onConfirmStateChanged();
    void onWorkChanged();
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

    /// The tool is shown to the user now: the key widget is visible (the current tab) in a window, which is not
    /// minimized. Reads only the watched widgets (also from an event of a widget being torn down: never the host).
    bool isToolVisible() const;

    /// Watches the events of the key widget of the host and of its window, which change the visibility of an
    /// interactive tool (the event filter of the shell; again when the key widget gets another window). No signal.
    void watchVisibility();

    /// Watches the window of the watched key widget (again after its Show or ParentChange: another window); reads
    /// only the key widget, never the host
    void watchVisibleWindow();

    /// Ends the watching of watchVisibility (the leave)
    void unwatchVisibility();

    /// The event \p event of \p watched may change the visibility of the tool (watchVisibility)
    bool isVisibilityEvent(const QObject* watched, const QEvent* event) const;

    /// The time of the clock of the visible time (setUseClock)
    static qint64 getUseClockTime();

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
    QPointer<PDFAppToolUndoTarget> m_ownUndoTarget;         ///< Not owned: PDFDocumentToolGUI::getOwnUndoTarget (the GUI's), nullptr = none
    QPointer<PDFMessageBox> m_saveQuestion;                 ///< The open question of requestSaveQuestion (new, WA_DeleteOnClose, owned logically by the shell)
    QString m_saveQuestionCommandId;                        ///< The command the open question was asked for
    QString m_saveAfterResultCommandId;                     ///< The apply choice of the save question: the save after a result kept in the tab

    /// The buttons of the save question (the answer is the last call: the save and the run execute foreign code)
    void onSaveQuestionSaveClicked();
    void onSaveQuestionApplyClicked();

    /// PDFDocumentToolGUI::documentCommandStatesChanged: the actions of the window follow
    void onDocumentCommandStatesChanged();

    /// The state of a command by the GUI alone (NotHandled for Undo and Redo, which the GUI is never asked about)
    PDFAppToolCommandState getGUICommandState(const QString& commandId) const;

    /// The GUI takes a command of the application at all: it answers Enabled or Disabled for a command of the
    /// registry of the context (bounded by the registry, about 150 commands; no side effects)
    bool hasDocumentCommands() const;

    /// Remembers hasSaveQuestion. \returns the value has changed since the last call
    bool rememberSaveQuestion();

    /// The work of the user may have changed (the Primary button, the status, a parameter): when hasSaveQuestion
    /// has another value than the last time, the command states of the tab are refreshed (Save of a document
    /// without changes follows the question). The refresh is the last call: a slot of the tab runs.
    void updateSaveQuestionState();

    /// The state of the shell has changed (entered, a run, a decision, editing again): the answers of
    /// getDocumentCommandState and hasSaveQuestion follow it, so the command states of the tab are refreshed for a
    /// tool, whose GUI takes commands (hasDocumentCommands), or whose save question has come or gone - nothing for
    /// any other tool. The refresh is the last call: a slot of the tab runs. After the entry it runs from the
    /// event loop (enter: the page of the tool is the page of its tab only then); a shell closed or retired
    /// meanwhile refreshes nothing.
    void refreshDocumentCommands();

    bool m_hadSaveQuestion = false;                         ///< hasSaveQuestion at the last rememberSaveQuestion

    QPointer<PDFAppToolToolbar> m_toolbar;
    QPointer<PDFAppToolSidePanel> m_rightPanel;
    QPointer<PDFAppToolSidePanel> m_leftPanel;
    QPointer<QWidget> m_canvasPage;
    QPointer<QWidget> m_overlay;
    QPointer<QWidget> m_toolActions;
    QPointer<QWidget> m_rightContent;
    QPointer<QWidget> m_leftContent;
    QPointer<QWidget> m_partsParent;    ///< Holds the parts, while they are not shown
    QPointer<PDFLifetimeToken> m_modeToken; ///< The tool mode of the host (a child token): its release gives the view back
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
    QPointer<QDialog> m_closeQuestion;  ///< The open question of PDFDocumentToolGUI::askClose (the GUI's, it deletes itself)
    QPointer<PDFAppLeaveFlow> m_leaveFlow;  ///< A leave waits for an answer (the question of a run, m_leaveQuestion); not owned (the flow of a tab / window)
    QPointer<QDialog> m_leaveQuestion;      ///< The question of the GUI, which the leave waits for; not owned (the GUI's)
    QMetaObject::Connection m_leaveQuestionEndConnection;   ///< aboutToBeDeleted of the notifier of m_leaveQuestion
    quint64 m_leaveWaitId = 0;              ///< The current wait of a leave (a queued end of an older wait is ignored)
    quint64 m_endedLeaveWaitId = 0;         ///< The wait, whose question has ended
    bool m_isCloseQuestionActionDeferred = false;   ///< The choice of the question waits for finishLeave (decision 8)
    bool m_isLeaveAnswered = false;         ///< The question of the tool was answered by closing for the leave, which waited (owner 2026-11-24: final)
    bool m_isCanceledLeaveQueued = false;   ///< onCanceledLeaveQueued is posted
    bool m_isLeaveChoiceApplied = false;    ///< The remembered choice of the answer has changed the document in this leave (the message names Undo)
    QString m_leaveAnsweredWork;            ///< getLeaveWorkSignature at the answer and after its applied choice: later work keeps the tool (RLC1 M1)
    bool m_isIncompleteQuestionPending = false;     ///< askIncompleteAfterCanceledLeave waits for the question of another tool
    QMetaObject::Connection m_otherQuestionFinishedConnection;  ///< finished of the question of another tool waited for
    QMetaObject::Connection m_otherQuestionEndConnection;       ///< aboutToBeDeleted of the notifier of that question
    quint64 m_knownDocumentVersion = 0;     ///< The version of the document, which this shell has seen (entry, documentChanged)
    QPointer<PDFAppContext> m_context;  ///< The context of the entry (recordUse after the host has gone)
    QPointer<QWidget> m_visibleKeyWidget;   ///< The key widget of the host watched by the event filter (an interactive tool); not owned
    QPointer<QWidget> m_visibleWindow;      ///< Its window watched by the event filter (nullptr = the key widget is the window); not owned
    qint64 m_visibleTime = 0;           ///< The visible time of this opening (ms)
    qint64 m_lastVisibleCheck = 0;      ///< The clock at the last accounting
    bool m_wasVisible = false;          ///< The tool was visible at the last accounting
    bool m_isInteractiveUsed = false;   ///< markInteractiveUse
    bool m_isUseRecorded = false;       ///< recordUse ran in this opening
    bool m_isDestroying = false;        ///< The destructor runs: the host and its tab are not read (their tab may be half destroyed)
    quint64 m_workStamp = 0;            ///< getWorkStamp
    static const IPDFAppToolUseClock* s_useClock;
    bool m_isExportDialog = false;  ///< The result dialog is the export of a kept result: its decision returns to Editing
    bool m_isRunExportDialog = false;   ///< The export is the file of a run (keepResult): not saved, nothing is kept
    PDFAppToolResultState m_resultState = PDFAppToolResultState::None;
    QPointer<PDFAppToolCallout> m_staleCallout;     ///< Owned by the right panel or by the content of the GUI (its layout); dies with them
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppToolCloseRequest)

#endif // PDFDOCUMENTTOOLSHELL_H
