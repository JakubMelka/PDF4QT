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

#ifndef PDFAPPLEAVEFLOW_H
#define PDFAPPLEAVEFLOW_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfdeferreddeletion.h"
#include "pdfapproute.h"

#include <QObject>
#include <QPointer>

#include <vector>

class QDialog;

namespace pdf
{

class PDFAppLeaveFlow;

/// The answer of a dialog shown without an event loop of its own (A0, no nested event loop in Pdf4QtLibApp and
/// Pdf4QtLibFluent; newapp_robustness.md 14-16). The dialog is shown by showModal (the modality, which exec() gives it,
/// no loop) and its end is delivered exactly once to a member slot of the receiver:
/// - QDialog::finished(code) -> answered(code) at once, inside the emission of finished: the continuation runs in
///   the slot of the answer (RR3-M5: no moment without the modal dialog, in which an assistant could act);
/// - the dialog deleted without finished (deleted with its parent: QDialog::done is the only source of finished)
///   -> answered(QDialog::Rejected) from the event loop, never inside the destructor, which deleted the dialog;
/// - no dialog (nullptr) -> answered(QDialog::Rejected) from the event loop;
/// - the receiver gone -> nothing: the answer is a child of the receiver and goes with it (the dialog stays as it is);
///   the receiver retired (PDFDeferredDeletion::retire) -> nothing either (the connections from the dialog are cut);
/// - the dialog retired (its logical owner closed, step A) -> answered(QDialog::Rejected) from the event loop, as for
///   a dialog deleted without finished (PDFLifetimeNotifier::aboutToBeDeleted, which also tells a deletion without
///   retire); a dialog retired before the answer watches it -> answered(QDialog::Rejected) from the event loop too.
/// Ownership: the answer belongs to the receiver and deletes itself (deleteLater) when it has delivered. The dialog is
/// not owned; setDeleteDialog(true) deletes it (deleteLater) after its answer (PDFAppDocumentChoiceDialog::ask). GUI
/// thread only. A subclass delivers another type of the answer (emitAnswer).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDialogAnswer : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    virtual ~PDFAppDialogAnswer() override;

    /// Watches the end of \p dialog, which its creator has shown already (open, show), for \p receiver: the code of
    /// QDialog::finished goes to \p slot. \returns the answer, nullptr without a receiver (nothing is watched).
    template<typename Receiver>
    static PDFAppDialogAnswer* watch(QDialog* dialog, Receiver* receiver, void (Receiver::*slot)(int))
    {
        PDFAppDialogAnswer* answer = receiver ? new PDFAppDialogAnswer(dialog, receiver) : nullptr;
        if (answer)
        {
            // A plain connection: the answer is a child of the receiver (retire of the receiver cuts its outgoing
            // signals); the connections from the dialog (outside) go through PDFDeferredDeletion::connect
            QObject::connect(answer, &PDFAppDialogAnswer::answered, receiver, slot);
        }
        return answer;
    }

    /// watch and showModal: the dialog is shown and the call returns at once; the code comes to \p slot later.
    /// Without a receiver the dialog is not shown (nobody would take its answer).
    template<typename Receiver>
    static PDFAppDialogAnswer* show(QDialog* dialog, Receiver* receiver, void (Receiver::*slot)(int))
    {
        PDFAppDialogAnswer* answer = watch(dialog, receiver, slot);
        if (answer)
        {
            showModal(dialog);
        }
        return answer;
    }

    /// Shows the dialog modal as QDialog::exec() shows it - application modal, unless the dialog has its own window
    /// modality - and returns at once (no event loop). The result of the dialog is reset (0) as by exec and open.
    static void showModal(QDialog* dialog);

    /// The watched dialog (nullptr after it has gone)
    QDialog* getDialog() const;

    /// The answer has been delivered or is on its way (the dialog has ended)
    bool isAnswered() const { return m_isAnswered; }

    /// The dialog is deleted (deleteLater) after its answer
    void setDeleteDialog(bool isDeleted) { m_isDialogDeleted = isDeleted; }

Q_SIGNALS:
    /// The end of the dialog: the code of QDialog::finished, QDialog::Rejected for a dialog gone without it
    void answered(int code);

protected:
    /// \param dialog The dialog (nullptr = the answer Rejected from the event loop)
    /// \param receiver The parent of the answer (the receiver of its signal)
    explicit PDFAppDialogAnswer(QDialog* dialog, QObject* receiver);

    /// Emits the answer; \p dialog is the dialog of a finished answer (alive, deleted later), nullptr when it has
    /// gone. A subclass emits its own signal. It is the last call of the delivery (the slot may delete anything).
    virtual void emitAnswer(const QDialog* dialog, int code);

private:
    void onDialogFinished(int code);
    void onDialogDestroyed();
    void onDialogGone();

    QPointer<QDialog> m_dialog;
    bool m_isAnswered = false;
    bool m_isDialogDeleted = false;
};

/// The waiter of a step of a leave flow, which another object has left Pending (PDFAppLeaveFlow::waitForStep): its
/// result goes to the member slot of the waiter exactly once - by PDFAppLeaveFlow::finishStep of the reporter, or
/// Refused from the event loop, when the reporter is deleted before it reports. A child of the flow (it goes with it).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLeaveFlowStep : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppLeaveFlowStep(QObject* reporter, PDFAppLeaveFlow* flow);
    virtual ~PDFAppLeaveFlowStep() override;

    /// The reporter of the step (an identity: the object may be gone)
    const QObject* getReporterKey() const { return m_reporterKey; }

    /// The result has been delivered (or is on its way after the reporter has gone)
    bool isDelivered() const { return m_isDelivered; }

    /// Delivers the result (the last call: the slot may delete the flow and the step)
    void deliver(PDFAppLeaveResult result);

    /// Ends the step without a delivery (the flow has ended); deleted later
    void drop();

Q_SIGNALS:
    void finished(pdf::PDFAppLeaveResult result);

private:
    void onReporterDestroyed();
    void onReporterGone();

    const QObject* m_reporterKey = nullptr;
    bool m_isDelivered = false;
};

/// One leave of a tab or of a window without a nested event loop (A0, RR3-H2, RR3-M5): the close of a tab, the quit
/// of a window, a navigation, the lock of a document. It holds the state of the flow (the flags of the former
/// synchronous flows with their own event loops): the goal, the step of its owner, the target of a navigation, a
/// queue of objects to go through, the pages, which have accepted the leave (PDFAppPage::finishLeave at the end of
/// the transaction), the open question and the steps left Pending by other objects. The owner drives the steps in
/// its member slots; a step, which asks the user, shows its question (show / open, never exec) and continues in the
/// slot of its answer.
///
/// Rules:
/// - one flow per tab and one per window (a child of its owner, GUI thread only); start refuses a second request
///   while a flow runs (as the former synchronous flows did; the open question stays, raiseQuestion brings it to the
///   front);
/// - every end of the flow is finish(isDone): the accepted pages, which the owner has not finished, and on a canceled
///   end also the pages finished by true learn that the leave does not go on (finishLeave(false): a tool, which
///   answered its question by closing, applies its choice and closes - owner 2026-11-24), the state is cleared,
///   finished(isDone) is emitted last. A flow deleted with its owner ends silently (no slot runs);
/// - a question (waitForQuestion) continues on QDialog::finished and also on its deletion without finished (Rejected,
///   from the event loop): a question deleted with its parent never leaves the flow stuck;
/// - a step left Pending by another object (a page, a shell, a GUI) is reported by finishStep(reporter, Done /
///   Refused) exactly once; a reporter deleted before it reports gives Refused (from the event loop). A Pending step
///   is never reported inside the call, which returned Pending: the caller registers its waiter (waitForStep) right
///   after that call;
/// - an assistant (MCP) refuses close / navigate while a flow runs in the window (isRunningIn) or a modal dialog is
///   open (DeniedDialog / DeniedBusy); a Pending answer in MCP is an error: the flow is ended by finish(false).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLeaveFlow : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// What the flow does
    enum class Goal
    {
        None,           ///< No flow runs
        CloseTab,       ///< The close of a tab (a document tab, the own tab of a tool)
        QuitWindow,     ///< The quit of a main window
        Navigate,       ///< A navigation of a tab (a route, a step in the history)
        LockDocument,   ///< The lock of the document of a tab
        SessionEnd      ///< The question of the unsaved work after the end of the session was canceled (PDFAppSessionEnd)
    };

    /// \param owner The tab or the window (the parent of the flow)
    explicit PDFAppLeaveFlow(QObject* owner);
    virtual ~PDFAppLeaveFlow() override;

    /// The flow of \p owner (its direct child), nullptr = none
    static PDFAppLeaveFlow* find(const QObject* owner);

    /// A flow, which runs, in the subtree of \p root (a window: its own flow and the flows of its tabs), nullptr = none
    static PDFAppLeaveFlow* findRunning(const QObject* root);

    /// A flow runs in the subtree of \p root
    static bool isRunningIn(const QObject* root) { return findRunning(root) != nullptr; }

    // ------------------------------------------------------------- the flow

    /// Starts the flow of \p goal (not None). \returns false, when a flow runs already (the second request is
    /// refused, nothing changes).
    bool start(Goal goal);

    /// Ends the flow: the accepted pages, which the owner has not finished (requestFinishPages), are finished by false,
    /// for \p isDone false also the pages finished by true (the leave stops after all: owner 2026-11-24), the state is
    /// cleared, the waiting steps are dropped (no delivery), finished(\p isDone) is the last call. Nothing when no
    /// flow runs.
    void finish(bool isDone);

    bool isRunning() const { return m_goal != Goal::None; }
    Goal getGoal() const { return m_goal; }

    /// The step of the owner (its own numbering, 0 after start)
    int getStep() const { return m_step; }
    void setStep(int step) { m_step = step; }

    /// The target of a navigation (Goal::Navigate)
    void setTarget(const PDFAppRoute& route, PDFAppNavigationMode mode);
    const PDFAppRoute& getTargetRoute() const { return m_targetRoute; }
    PDFAppNavigationMode getTargetMode() const { return m_targetMode; }

    /// The object, which the flow is about (the tab of a navigation of a window, the document of a lock); nullptr
    /// after it has gone - the continuation checks the state again (RR3-L8)
    void setSubject(QObject* subject) { m_subject = subject; }
    QObject* getSubject() const { return m_subject.data(); }

    /// A number of the owner (a step in the history, a version of a document)
    void setValue(qint64 value) { m_value = value; }
    qint64 getValue() const { return m_value; }

    // ------------------------------------------------------------- queue of objects

    /// Objects, which the flow goes through one by one (the pages of the tools, the own tabs of the tools, the tabs to
    /// save); replaces the queue
    void setQueue(const std::vector<QPointer<QObject>>& objects);

    /// The next object of the queue, which still exists (removed from the queue), nullptr = the queue is empty
    QObject* takeNext();

    /// Objects of the queue, which still exist
    int getQueueSize() const;

    // ------------------------------------------------------------- the transaction of the pages

    /// The page has accepted the leave (PDFAppPage::requestClose / requestDocumentLock returned Done, or its Pending
    /// step finished Done): it waits for finishLeave. A page is recorded once.
    void addAcceptedPage(PDFAppPage* page);

    /// The accepted pages, which still exist
    std::vector<QPointer<PDFAppPage>> getAcceptedPages() const;

    /// The pages ended by requestFinishPages(true) (finish(false) ends them by false; finish(true) forgets them - the
    /// owner reads them before, when its own end may still keep the tab: a failed save of the close)
    const std::vector<QPointer<PDFAppPage>>& getFinishedPages() const { return m_finishedPages; }

    /// The end of the transaction of the pages (A0, P1): PDFAppPage::requestFinishLeave(\p isProceeding, this flow) of
    /// the accepted pages one by one (the list is empty afterwards; the pages ended by true are remembered for a
    /// canceled finish); after a page has refused, the rest are ended by false. Done = every page has finished, the
    /// leave goes on; Refused = a page has refused (the caller cancels the leave by finish(false)); Pending = a page
    /// asks about the work, which its applied choice could not keep (Measure "Keep Measuring / Discard and Close"): the
    /// result comes later to the waiter of this flow as the reporter - the caller waits by waitForStep(this flow,
    /// caller, slot) right after the call. The pages, which have not been finished yet, are finished by false, when the
    /// flow ends meanwhile (finish). The pages run code of the application: the flow, its owner and anything may be
    /// deleted meanwhile - guard them.
    PDFAppLeaveResult requestFinishPages(bool isProceeding);

    // ------------------------------------------------------------- the question

    /// The question of the flow (shown by its creator): an assistant refuses close / navigate while it is open, a
    /// second request brings it to the front (raiseQuestion). Forgotten, when it goes. Nothing when no flow runs.
    void setQuestion(QDialog* question);

    /// The question, while it is shown, nullptr otherwise
    QDialog* getQuestion() const;

    /// A question of the flow is shown
    bool isQuestionOpen() const { return getQuestion() != nullptr; }

    /// Brings the open question to the front (activates it); nothing without one
    void raiseQuestion();

    /// The question of a step (shown by its creator: a question of a GUI, PDFDocumentToolGUI::askClose): setQuestion
    /// and PDFAppDialogAnswer::watch - its end goes to \p slot exactly once (the code of finished; Rejected when it
    /// is deleted without an answer, from the event loop). \returns the answer, nullptr when no flow runs or without
    /// a receiver (nothing is watched then).
    template<typename Receiver>
    PDFAppDialogAnswer* waitForQuestion(QDialog* question, Receiver* receiver, void (Receiver::*slot)(int))
    {
        if (!isRunning())
        {
            return nullptr;
        }
        setQuestion(question);
        return PDFAppDialogAnswer::watch(question, receiver, slot);
    }

    // ------------------------------------------------------------- steps of other objects

    /// \p reporter (a page, a shell, a GUI) has left its step Pending: \p receiver waits for its result in \p slot
    /// (exactly once: the result of finishStep, or Refused when the reporter is deleted first). \returns false, when
    /// no flow runs, the reporter or the receiver is missing, or a waiter of the reporter exists already.
    template<typename Receiver>
    bool waitForStep(QObject* reporter, Receiver* receiver, void (Receiver::*slot)(PDFAppLeaveResult))
    {
        PDFAppLeaveFlowStep* step = receiver ? createStep(reporter) : nullptr;
        if (step)
        {
            // The step is a child of the flow, the receiver may be outside its subtree (the navigator): the
            // connection is cut, when the receiver is retired (step A)
            PDFDeferredDeletion::connect(step, &PDFAppLeaveFlowStep::finished, receiver, slot);
        }
        return step != nullptr;
    }

    /// The reporter reports the result of its Pending step (Done or Refused): its waiter gets it now, in this call (the
    /// last call: the waiter may delete anything, the flow too). \returns false, when nobody waits for the reporter or
    /// the result is Pending.
    bool finishStep(QObject* reporter, PDFAppLeaveResult result);

    /// A waiter of the step of \p reporter exists
    bool isWaitingFor(const QObject* reporter) const;

    /// A step of another object is Pending
    bool hasPendingSteps() const;

Q_SIGNALS:
    /// The flow has ended (finish): \p isDone - the goal has been reached
    void finished(bool isDone);

private:
    PDFAppLeaveFlowStep* createStep(QObject* reporter);
    PDFAppLeaveFlowStep* findStep(const QObject* reporter) const;

    /// The next pages of requestFinishPages (Pending = a page asks)
    PDFAppLeaveResult continueFinishPages();
    void onPageFinished(PDFAppLeaveResult result);

    Goal m_goal = Goal::None;
    int m_step = 0;
    PDFAppRoute m_targetRoute;
    PDFAppNavigationMode m_targetMode = PDFAppNavigationMode::Push;
    QPointer<QObject> m_subject;
    qint64 m_value = 0;
    std::vector<QPointer<QObject>> m_queue;
    std::vector<QPointer<PDFAppPage>> m_acceptedPages;
    std::vector<QPointer<PDFAppPage>> m_finishingPages;    ///< The pages of requestFinishPages, which have not been finished yet
    std::vector<QPointer<PDFAppPage>> m_finishedPages;     ///< The pages ended by requestFinishLeave(true): finish(false) ends them by false
    bool m_isFinishProceeding = false;  ///< requestFinishPages(true)
    bool m_isFinishRefused = false;     ///< A page of requestFinishPages has refused
    QPointer<QDialog> m_question;
    std::vector<QPointer<PDFAppLeaveFlowStep>> m_steps;
};

}   // namespace pdf

#endif // PDFAPPLEAVEFLOW_H
