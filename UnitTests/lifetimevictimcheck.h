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

#ifndef LIFETIMEVICTIMCHECK_H
#define LIFETIMEVICTIMCHECK_H

#include "appdocumentfixture.h"
#include "lifetimedeletionmode.h"

#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumenttab.h"
#include "pdfappleaveflow.h"
#include "pdfappmainwindow.h"
#include "pdfapppage.h"
#include "pdfapptab.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltab.h"
#include "pdfappnavigator.h"
#include "pdfcompiler.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdrawwidget.h"
#include "pdfpagecontentelements.h"

#include <QAbstractEventDispatcher>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHash>
#include <QItemSelectionModel>
#include <QMetaMethod>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <QThread>
#include <QWidget>

#include <functional>
#include <vector>

// Check 9 reads the incoming connections of the retired objects by the private API of QtCore (RR2-M5: the signal spy
// callbacks of Qt do not see the connections to member functions and functors). It is compiled in, when the test links
// Qt6::CorePrivate (its private headers are on the include path); the library itself uses only the public API.
#if __has_include(<QtCore/private/qobject_p.h>) && __has_include(<QtCore/private/qobject_p_p.h>)
#include <QtCore/private/qobject_p.h>
#include <QtCore/private/qobject_p_p.h>
#include <QtCore/private/qthread_p.h>
#define PDF4QT_LIFETIME_CONNECTION_CHECK 1
#else
#define PDF4QT_LIFETIME_CONNECTION_CHECK 0
#endif

// The exact address of the expand animation of a QTreeView (named exception 3 of check 9), when the private headers of
// QtWidgets are on the include path (Qt6::WidgetsPrivate); otherwise the exception is not made (fails closed, RB-L3):
// the connection is reported with the reason
#if PDF4QT_LIFETIME_CONNECTION_CHECK && __has_include(<QtWidgets/private/qtreeview_p.h>)
#include <QtWidgets/private/qtreeview_p.h>
#define PDF4QT_LIFETIME_TREE_VIEW_PRIVATE 1
#else
#define PDF4QT_LIFETIME_TREE_VIEW_PRIVATE 0
#endif

namespace pdftest
{

/// The close of the victim of a lifetime sweep in the modes deferred / retire of LifetimeDeletionMode (step A,
/// newapp_robustness.md 5.1 and 11 point 8, RR-M7, RR2-M5) and the checks of the retired victim. One object per run of
/// a scenario, created before its steps (it filters the events of the application from then on); the sweep calls
/// retire at the site of the emission and finish from the test body after the steps and settle():
///  (1) nothing crashes (ASan);
///  (2) the victim lives after its close (it is no synchronous delete) and is deleted by its deferred deletion (its
///      own DeferredDelete or the one of an ancestor / logical owner), never synchronously outside of a
///      SynchronousDeletionScope;
///  (3) after settle() it is deleted (a retire without the deletion is an error); the close retired it
///      (PDFDeferredDeletion::isRetired - the real close goes through retire);
///  (4) nothing of the retired objects was shown after the close (the Show events dropped by the gate), no window of the
///      subtree of the victim is visible at its deletion;
///  (5) the victim is not in the lists of the window at once (a tab is no tab of the window and not the current one, a
///      page is not shown by its tab; not in the mode retire, which bypasses the close of the window);
///  (6) the version of the document of the victim (its document tab) does not change until its deletion, and no
///      change of it is tried (PDFAppDocument refuses a change of a retired document and counts it);
///  (7) no new child appears in the subtree of the victim until its deletion (except the helper objects of
///      PDFDeferredDeletion);
///  (8) no new top-level window is shown while the code, which was on the stack at the close, runs: until the event
///      dispatcher starts to process events again at the level of the test body (QAbstractEventDispatcher::awake /
///      aboutToBlock) or delivers an event at the outermost level of the event delivery (a posted event, a timer, a
///      spontaneous event: QThreadData::scopeLevel, isTopLevelDispatch) - decided by the stack, not by the order of
///      the events (RB-M4). A call queued at the close stays a fallback;
///  (9) no object of an application class in a retired subtree has an incoming connection from a living sender outside
///      of the retired objects (except QObject::destroyed and the named exceptions), nor an object of Qt from a sender
///      of the application - right after the close (a victim retired later: once the stack of its retire has returned)
///      and at the deletion (only with the private headers of QtCore, PDF4QT_LIFETIME_CONNECTION_CHECK);
/// (10) a close is refused only by a tab, whose leave flow ran before the close (it waited for a visible question, or
///      it was in the middle of a step on the stack of the site; the user cannot close the tab now, the close brings
///      the question forward: Refusal::RefusedWhileLeaving), the refused close opened no question, and that flow is
///      not stuck (it has ended after the steps, or its question is visible); or by a modal dialog
///      of the tab, open before the close and still open, which decides first (the result dialog of a tool, F2), or a
///      tool of the tab, which decides about its result (State::Deciding, its dialog in the middle of its answer on the
///      stack of the site) and is not stuck in it after settle(): Refusal::RefusedByOpenDialog, the close opened
///      nothing. Any other refusal -
///      a question of the close, which stays; a flow without a question; a page, whose navigation back did not replace
///      it although its tab had a previous entry (RB-M2) - is a finding of finish().
/// The checks (6), (7), (9) and the Show count of (4) run only for a retired victim (the failure of (3) says it).
class LifetimeVictimCheck : public QObject
{
private:
    using BaseClass = QObject;

public:
    explicit LifetimeVictimCheck(AppDocumentFixture* fixture) :
        BaseClass(nullptr),
        m_fixture(fixture),
        m_window(fixture ? fixture->window.data() : nullptr),
        m_baseLevel(getScopeLevel())
    {
        qApp->installEventFilter(this);
        reportConnectionCheck();

        // (8) The dispatcher starts to process events: the code on the stack of the close has returned, when no event
        // is being delivered (the level of the test body)
        if (QAbstractEventDispatcher* dispatcher = QAbstractEventDispatcher::instance())
        {
            QObject::connect(dispatcher, &QAbstractEventDispatcher::awake, this, [this]() { onDispatcherStarts(); });
            QObject::connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, [this]() { onDispatcherStarts(); });
        }
    }

    virtual ~LifetimeVictimCheck() override
    {
        qApp->removeEventFilter(this);
    }

    LifetimeVictimCheck(const LifetimeVictimCheck&) = delete;
    LifetimeVictimCheck& operator=(const LifetimeVictimCheck&) = delete;

    /// A named exception of check 9: a connection of a living sender into a retired subtree, which is no error
    struct ConnectionException
    {
        const char* senderClass;        ///< Class name of the sender (exact)
        const char* signal;             ///< Name of the signal
        const char* receiverClass;      ///< Class name of the receiver (exact)
        std::ptrdiff_t receiverSize;    ///< Size of the receiver class: the sender (a value member) lies inside it
        const char* reason;
    };

    /// The named exceptions of check 9 (besides QObject::destroyed, the expand animation of a QTreeView,
    /// isTreeViewOwnAnimation, and the manipulator of a page content scene, isContentSceneOwnManipulator). Each is a
    /// value member of the receiver: the sender has no parent and lies inside the receiver object (RB-L5) - the same
    /// signal of another object of the class is still reported.
    static const std::vector<ConnectionException>& getConnectionExceptions()
    {
        static const std::vector<ConnectionException> exceptions = {
            { "QTimer", "timeout", "pdf::PDFDrawWidget", std::ptrdiff_t(sizeof(pdf::PDFDrawWidget)),
              "the auto scroll and wheel scroll timers are members of the draw widget (no parent, deleted with it; "
              "Pdf4QtLibWidgets is not changed in step A)" },
            { "QFutureWatcherBase", "finished", "pdf::PDFAsynchronousTextLayoutCompiler", std::ptrdiff_t(sizeof(pdf::PDFAsynchronousTextLayoutCompiler)),
              "the watcher of the text layout is a member of the compiler (no parent, deleted with it; Pdf4QtLibWidgets is not "
              "changed in step A)" },
        };
        return exceptions;
    }

    /// Closes \p victim at the site of an emission by the mode of LifetimeDeletionMode (deferred: the real close of
    /// AppDocumentFixture::retire - the own tab of a tool closed as confirmed (setCloseConfirmed, as the application
    /// closes a tab, whose result it has taken), a question of the close of a document tab answered by Don't Save /
    /// Stop and Close, a page, which its tab still shows after its close because the tab has no previous entry, by
    /// PDFAppTab::setPage(nullptr) - the single place of the page; retire: PDFDeferredDeletion::retire) and starts the
    /// checks. It never settles. A tab or page, whose close goes on from the event loop (the leave flow of this close
    /// runs and waits for no question, the navigation back is queued behind the navigation on the stack), is a deferred
    /// close: the checks start at its later retire, finish() fails unless settle() has retired and deleted it.
    /// \returns false, when the close was refused (getRefusal: check 10 - a tab, whose leave flow waited for its visible
    /// question before the close, or a finding of finish(); the new questions of the close are canceled, the tab
    /// stays) or the victim is nullptr / a second one.
    bool retire(QObject* victim) { return retire(victim, LifetimeDeletionMode::getMode()); }

    /// retire in \p mode (Deferred or Retire; Synchronous is refused)
    bool retire(QObject* victim, LifetimeDeletionMode::Mode mode)
    {
        if (!victim || m_isStarted || mode == LifetimeDeletionMode::Mode::Synchronous)
        {
            return false;
        }

        // The Home tab is closed only with its window (the victim "end")
        if (pdf::PDFAppTab* tab = qobject_cast<pdf::PDFAppTab*>(victim); tab && !tab->isClosable())
        {
            m_refusal = Refusal::NotClosable;
            ++s_notClosableCount;
            return false;
        }

        m_mode = mode;
        QPointer<QObject> guard(victim);
        m_victimName = describe(victim);
        if (mode == LifetimeDeletionMode::Mode::Retire)
        {
            pdf::PDFDeferredDeletion::retire(victim);
        }
        else
        {
            const QList<QDialog*> dialogsBefore = getVisibleDialogs();
            const bool isTab = qobject_cast<pdf::PDFAppTab*>(victim) != nullptr;

            // The tab, whose leave flow decides the close: the tab itself, or the tab, which shows the page of the victim
            pdf::PDFAppPage* victimPage = isTab ? nullptr : findAncestor<pdf::PDFAppPage>(victim);
            QPointer<pdf::PDFAppTab> flowTab = isTab ? static_cast<pdf::PDFAppTab*>(victim) : getShowingTab(victimPage);
            m_flowTab = flowTab;

            // (10) A tab, whose leave flow runs already (a question of its tool page, its own close, a navigation): the
            // close is refused and brings the question forward - it counts only, when that question was visible
            const bool wasLeaving = flowTab && flowTab->isLeaving();
            const QPointer<const QDialog> questionBefore = wasLeaving ? getWaitedQuestion(flowTab.data()) : nullptr;
            const bool hadVisibleQuestion = questionBefore && questionBefore->isVisible() && dialogsBefore.contains(const_cast<QDialog*>(questionBefore.data()));

            // RB-M2: the navigation back of a page can replace it only with a previous entry of its tab
            const bool canGoBackBefore = !isTab && flowTab && flowTab->getHistory().canGoBack();

            // (10) The modal dialogs of the tab (its subtree or owned by it), open before the close: such a dialog
            // decides first (the result dialog of a tool, F2) - the user cannot reach the close of its tab or page now
            QList<QPointer<QDialog>> modalBefore;
            for (QDialog* dialog : dialogsBefore)
            {
                if (dialog->isModal() && isInside(dialog, flowTab ? static_cast<QObject*>(flowTab.data()) : victim))
                {
                    modalBefore << QPointer<QDialog>(dialog);
                }
            }

            // (10) A tool of the tab decides about its result (PDFDocumentToolShell::State::Deciding: its result dialog
            // is open, or it is in the middle of its answer on the stack of the site - nothing goes around it, F2)
            const bool wasDeciding = flowTab && isToolDeciding(flowTab.data());

            if (pdf::PDFAppToolTab* toolTab = findAncestor<pdf::PDFAppToolTab>(victim))
            {
                toolTab->setCloseConfirmed(true);
            }
            bool isDone = false;
            if (m_closeFunction)
            {
                isDone = m_closeFunction(victim);
            }
            else if (m_fixture)
            {
                isDone = m_fixture->retire(victim);
            }

            // A tab, whose close is done (PDFAppMainWindow::closeTab true), is out of the lists of the window at once
            // (check 5 below); otherwise the questions of the close are answered - only the questions of this close
            // (RB-L9), never the question of a flow, which ran before the close (RB-L4: the user did not choose it)
            const bool isTabDone = isTab && isDone;
            for (int round = 0; round < MAXIMUM_ANSWER_ROUNDS && !wasLeaving && !isTabDone && guard && isListed(guard.data()); ++round)
            {
                if (!answerCloseQuestion(dialogsBefore, flowTab ? getWaitedQuestion(flowTab.data()) : nullptr, flowTab ? static_cast<QObject*>(flowTab.data()) : guard.data()))
                {
                    break;
                }
            }

            if (!isTabDone && guard && isListed(guard.data()))
            {
                if (wasLeaving)
                {
                    // (10) The flow, which ran before the close, decides: the close is refused and brings its question
                    // forward (A0: one flow of the tab). It counts, when the close has opened no question of its own; a
                    // stray question is a finding. The flow waited for its visible question, or it is in the middle of a
                    // step on the stack of the site (its question answered, its next step not yet started): finish()
                    // fails, when it still runs without a question after settle() (stuck - the tab could never close).
                    const QString questions = describeNewDialogs(dialogsBefore);
                    rejectNewDialogs(dialogsBefore);
                    if (!questions.isEmpty())
                    {
                        return refuse(QStringLiteral("the leave flow of %1 ran before the close (its question %2), the refused close opened questions [%3]")
                                          .arg(describe(flowTab.data()), hadVisibleQuestion ? describe(questionBefore.data()) : QStringLiteral("not visible"), questions));
                    }
                    m_refusal = Refusal::RefusedWhileLeaving;
                    ++s_refusedWhileLeavingCount;
                    return false;
                }

                if (isTab)
                {
                    if (isPendingTabClose(static_cast<pdf::PDFAppTab*>(guard.data())))
                    {
                        // A deferred close: the leave flow of this close runs on without a question (a step applied
                        // from the event loop - the close choice of a tool requested inside a change of its document):
                        // the tab is closed and retired later, like a page replaced by its navigation - the checks start
                        // at its retire, finish() fails when settle() has not retired and deleted it
                        m_isPendingClose = true;
                    }
                    else if (isRefusedByOpenDialog(modalBefore, dialogsBefore, wasDeciding, flowTab.data()))
                    {
                        // (10) A modal dialog of the tab, open before the close, decides first: no victim
                        return refuseByOpenDialog();
                    }
                    else
                    {
                        // The tab stays: the questions of the close are canceled (the user's Cancel) - a finding
                        const QString questions = describeNewDialogs(dialogsBefore);
                        rejectNewDialogs(dialogsBefore);
                        return refuse(QStringLiteral("the tab stays, questions [%1]").arg(questions));
                    }
                }
                else if (!victimPage || !flowTab)
                {
                    return refuse(QStringLiteral("no tab shows its page"));
                }
                else if (isPendingPageClose(flowTab.data(), canGoBackBefore))
                {
                    // RB-M2: the navigation back is pending - queued behind the navigation on the stack (the last
                    // request wins), or the page answered later and the navigation goes on from the event loop: the
                    // page is replaced and retired later - the checks start at its retire, finish() fails when settle()
                    // has not retired and deleted it
                    m_isPendingClose = true;
                }
                else if (isRefusedByOpenDialog(modalBefore, dialogsBefore, wasDeciding, flowTab.data()))
                {
                    // (10) A modal dialog of the tab, open before the close, decides first: the page stays, no victim
                    return refuseByOpenDialog();
                }
                else if (!canGoBackBefore)
                {
                    // The tab has no previous entry: the single place of the page (the navigation calls it too)
                    ++s_pageFallbackCount;
                    flowTab->setPage(nullptr);
                }
                else
                {
                    // RB-M2: the tab had a previous entry, the navigation back should have replaced the page
                    const QString questions = describeNewDialogs(dialogsBefore);
                    rejectNewDialogs(dialogsBefore);
                    return refuse(QStringLiteral("the navigation back did not replace the page, although its tab had a previous entry (questions [%1])").arg(questions));
                }
            }
        }

        m_isStarted = true;
        if (!guard)
        {
            addFinding(QStringLiteral("(2) %1 was deleted synchronously by its close").arg(m_victimName));
            return true;
        }

        m_victim = guard.data();

        // (5) out of the lists of the window at once (a deferred close: at its deletion, lastLook)
        if (m_mode == LifetimeDeletionMode::Mode::Deferred && !m_isPendingClose && isListed(victim))
        {
            addFinding(QStringLiteral("(5) %1 is still in the lists of the window after its close").arg(m_victimName));
        }

        // (6) the document of the victim
        if (pdf::PDFAppDocumentTab* documentTab = findAncestor<pdf::PDFAppDocumentTab>(victim))
        {
            m_document = documentTab->getDocument();
        }

        // The checks of a retired victim start now, or when the close retires it later (a page replaced by the
        // navigation is retired from the event loop, PDFAppTab::setPage - it is out of the lists and hidden at once).
        // The notifier emits from its destructor too (a victim deleted without retire): it is not retired then.
        m_shownBefore = pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::Show);

        // (4) The shows in progress at the close (QWidgetPrivate::show_helper sets QWidgetData::in_show from its start
        // to its end): Qt finishes them on the same stack - it shows the children and sends the Show events - and the
        // gate drops those events. This filter moves in front of the gate to see them.
        m_showsAtClose.clear();
        for (QWidget* widget : QApplication::allWidgets())
        {
            if (qt_qwidget_data(widget)->in_show)
            {
                m_showsAtClose << QPointer<QWidget>(widget);
            }
        }
        qApp->installEventFilter(this);

        if (pdf::PDFDeferredDeletion::isRetired(victim))
        {
            startRetiredChecks();
            checkConnections("after the close");
        }
        else
        {
            QObject::connect(pdf::PDFDeferredDeletion::getNotifier(victim), &pdf::PDFLifetimeNotifier::aboutToBeDeleted, this, [this]()
            {
                if (!m_isRetired && m_victim && pdf::PDFDeferredDeletion::isRetired(m_victim.data()))
                {
                    startRetiredChecks();

                    // (9) after the close (RB-L10): the notifier emits inside the retire (its step 2, before the
                    // incoming connections are disconnected) - the check runs, when the stack of that retire has returned
                    m_isRetireCheckDue = true;
                    m_retireLevel = getScopeLevel();
                }
            });
        }

        QObject::connect(victim, &QObject::destroyed, this, [this]()
        {
            if (!m_isLastLookDone && !pdf::PDFDeferredDeletion::isSynchronousDeletionAllowed())
            {
                addFinding(QStringLiteral("(2) %1 was deleted synchronously, not by its deferred deletion").arg(m_victimName));
            }
        });

        // (8) the code on the stack at the close runs until that stack returns (onDispatcherStarts, eventFilter: decided
        // by the scope level of the event delivery, RB-M4); the queued call is only a fallback
        m_isStackOpen = true;
        m_closeLevel = getScopeLevel();
        QMetaObject::invokeMethod(this, [this]() { endStackOfClose(); }, Qt::QueuedConnection);
        return true;
    }

    /// The self test of the checks only: \p close replaces the close of the application (AppDocumentFixture::retire;
    /// it returns the close is done) in the mode Deferred - a close, which deletes synchronously, does not retire,
    /// leaves the victim listed
    void setCloseFunction(std::function<bool(QObject*)> close) { m_closeFunction = std::move(close); }

    /// The victim has been closed by retire
    bool isStarted() const { return m_isStarted; }

    /// Why retire returned false
    enum class Refusal
    {
        None,
        NotClosable,            ///< The Home tab: the application closes it only with its window (the victim "end")
        RefusedWhileLeaving,    ///< (10) The leave flow of the tab ran before the close (finish: not stuck)
        RefusedByOpenDialog,    ///< (10) A modal dialog of the tab (its subtree or owned by it), open before the close and
                                ///< still open, decides first (the result dialog of a tool, F2); the close opened nothing
        Refused                 ///< (10) Any other refusal (a question stays, a flow without a question, a page not
                                ///< replaced by the navigation back): a finding of finish()
    };

    Refusal getRefusal() const { return m_refusal; }

    /// After the steps and settle() of the test body: check (3), (10) and the end of the checks. \returns the findings.
    QStringList finish()
    {
        if (!m_isStarted)
        {
            if (m_refusal == Refusal::Refused)
            {
                addFinding(QStringLiteral("(10) the close of %1 was refused: %2").arg(m_victimName, m_refusalReason));
            }
            else if (m_refusal == Refusal::RefusedWhileLeaving && m_flowTab && m_flowTab->isLeaving() && !getWaitedQuestion(m_flowTab.data()))
            {
                // The flow, which refused the close by its visible question, runs on without a question after the steps
                addFinding(QStringLiteral("(10) the leave flow of %1, which refused the close of %2, still runs without a question after settle() (stuck: the tab could never close)")
                               .arg(describe(m_flowTab.data()), m_victimName));
            }
            else if (m_refusal == Refusal::RefusedByOpenDialog && m_flowTab && isToolDeciding(m_flowTab.data()) && !hasVisibleModalDialog(m_flowTab.data()))
            {
                // The tool, which refused the close while deciding, decides on without its dialog after the steps
                addFinding(QStringLiteral("(10) a tool of %1, which refused the close of %2 while deciding, still decides without its dialog after settle() (stuck)")
                               .arg(describe(m_flowTab.data()), m_victimName));
            }
            return m_findings;
        }

        if (!m_isRetired && m_isPendingClose)
        {
            // The deferred close did not complete: still listed, its flow still running, or ended refused
            const pdf::PDFAppTab* tab = m_flowTab.data();
            const QString reason = !m_victim ? QStringLiteral("deleted without retire")
                                             : (!tab ? QStringLiteral("its tab is gone")
                                                     : (tab->isLeaving() ? QStringLiteral("its leave flow still runs")
                                                                         : (m_victim.data() == tab ? QStringLiteral("its leave flow ended, the tab stays")
                                                                                                   : QStringLiteral("its leave flow ended, the page stays"))));
            addFinding(QStringLiteral("(3) the deferred close of %1 did not complete after settle() (%2)").arg(m_victimName, reason));
        }
        else if (!m_isRetired)
        {
            addFinding(QStringLiteral("(3) the close did not retire %1 (PDFDeferredDeletion::isRetired was never true)").arg(m_victimName));
        }
        if (m_victim)
        {
            addFinding(QStringLiteral("(3) %1 was not deleted by settle() after its close").arg(m_victimName));
        }

        const qint64 shown = pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::Show) - m_shownBefore - m_finishedShows;
        if (m_isRetired && shown > 0)
        {
            addFinding(QStringLiteral("(4) a retired object was shown after the close of %1 (%2 Show events dropped by the gate)").arg(m_victimName).arg(shown));
        }
        return m_findings;
    }

    const QStringList& getFindings() const { return m_findings; }

    /// The end of the application after a step (the victim "end", RR-M7): the window is closed as the user closes it
    /// (its questions answered by Quit Without Saving / Stop, new ones only) and the events are processed; the fixture
    /// then ends the application (its destructor: the window deleted in a SynchronousDeletionScope as main.cpp does,
    /// flush, the context closed). \returns the window has closed.
    static bool closeApplication(AppDocumentFixture& fixture)
    {
        if (!fixture.window)
        {
            return true;
        }

        const QList<QDialog*> dialogsBefore = getVisibleDialogs();
        fixture.window->close();
        AppDocumentFixture::settle();
        for (int round = 0; round < MAXIMUM_ANSWER_ROUNDS && fixture.window && fixture.window->isVisible(); ++round)
        {
            if (!answerCloseQuestion(dialogsBefore, getWaitedQuestion(fixture.window.data()), fixture.window.data()))
            {
                break;
            }
            AppDocumentFixture::settle();
        }

        // The result dialog of a tool, open before the close (modal), decides first (an own tab refuses
        // the quit meanwhile, PDFAppToolTabPage::requestClose): the user leaves it without keeping the result - its
        // Discard, else Close (the written files) - and closes the window again. The same for the question, which a
        // running leave flow of a tab of the window waits for (its own close, started before the quit: "the first
        // leave decides", the quit is refused meanwhile) - answered by Don't Save / Stop.
        for (int round = 0; round < MAXIMUM_ANSWER_ROUNDS && fixture.window && fixture.window->isVisible(); ++round)
        {
            if (!answerResultDialog(dialogsBefore) && !answerTabLeaveQuestion(fixture.window.data()))
            {
                break;
            }
            AppDocumentFixture::settle();
            if (fixture.window)
            {
                const QList<QDialog*> dialogsBeforeAgain = getVisibleDialogs();
                fixture.window->close();
                AppDocumentFixture::settle();
                for (int answer = 0; answer < MAXIMUM_ANSWER_ROUNDS && fixture.window && fixture.window->isVisible(); ++answer)
                {
                    if (!answerCloseQuestion(dialogsBeforeAgain, getWaitedQuestion(fixture.window.data()), fixture.window.data()))
                    {
                        break;
                    }
                    AppDocumentFixture::settle();
                }
            }
        }

        const bool isClosed = !fixture.window || !fixture.window->isVisible();
        if (!isClosed)
        {
            // Diagnostics of the failure: the questions, which stay open
            QStringList dialogs;
            for (QDialog* dialog : getVisibleDialogs())
            {
                dialogs << describe(dialog);
            }
            qInfo().noquote() << QStringLiteral("closeApplication: the window stays open, visible dialogs [%1]").arg(dialogs.join(QStringLiteral(", ")));
        }
        return isClosed;
    }

    /// Number of the pages closed by PDFAppTab::setPage(nullptr): their tab had no previous entry, the navigation back
    /// could not replace them (RB-M2)
    static int getPageFallbackCount() { return s_pageFallbackCount; }

    /// Number of the closes refused in the process by a tab, whose leave flow ran before the close and waited for a
    /// visible question (Refusal::RefusedWhileLeaving): a site without a victim, as the user cannot close the tab now
    static int getRefusedWhileLeavingCount() { return s_refusedWhileLeavingCount; }

    /// Number of the closes refused in the process by an open modal dialog of the tab (Refusal::RefusedByOpenDialog)
    static int getRefusedByOpenDialogCount() { return s_refusedByOpenDialogCount; }

    /// Number of the closes refused in the process, which are no finding: the user cannot close the tab or page now
    /// (a question of its leave flow or a modal dialog of the tab decides first) - a site without a victim
    static int getAcceptedRefusalCount() { return s_refusedWhileLeavingCount + s_refusedByOpenDialogCount; }

    /// Number of the closes refused for the Home tab (Refusal::NotClosable) in the process: a site, which comes
    /// while the victim tab is the Home tab, has no victim (counted apart from the sites not reproduced)
    static int getNotClosableCount() { return s_notClosableCount; }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        // (8), (9) An event, which the dispatcher delivers at the outermost level: the stack of a close or of a retire
        // has returned (RB-M4, RB-L10)
        if (m_isStackOpen && isTopLevelDispatch(event, m_closeLevel > m_baseLevel))
        {
            endStackOfClose();
        }
        if (m_isRetireCheckDue && isTopLevelDispatch(event, m_retireLevel > m_baseLevel))
        {
            endStackOfRetire();
        }

        switch (event->type())
        {
            case QEvent::DeferredDelete:
                if (m_victim && !m_isLastLookDone && isVictimOrAncestor(watched))
                {
                    lastLook();
                }
                break;

            case QEvent::ChildAdded:
                // (7) a worker thread of a page compiler started on the stack of the close (isCompilerThreadOfClose); the
                // child is not constructed yet here, its class is read at the last look
                if (m_isStackOpen && watched && qstrcmp(watched->metaObject()->className(), "pdf::PDFAsynchronousPageCompiler") == 0)
                {
                    // RB-L3: a guarded pointer - a later child at the address of a dead one is not taken for it
                    m_childrenAddedOnStack << QPointer<QObject>(static_cast<QChildEvent*>(event)->child());
                }
                break;

            case QEvent::Show:
                if (m_isStackOpen && watched && watched->isWidgetType() && isShowFinishedByQt(static_cast<QWidget*>(watched)))
                {
                    // (4) the gate drops it: the end of a show started before the close, not a new show
                    ++m_finishedShows;
                }
                if (m_isPendingClose && !m_isRetired && m_victim && m_flowTab && watched && watched == getWaitedQuestion(m_flowTab.data()))
                {
                    // A question of the deferred close of the tab or page (its flow goes on from the event loop: Don't
                    // Save of the unsaved changes, the question of a tool), shown by a step of that flow, not by the
                    // code of the site: answered from the event loop as the user closes - only that question (RB-L9)
                    QMetaObject::invokeMethod(this, [this]()
                    {
                        if (m_victim && !m_isRetired && m_flowTab)
                        {
                            const QDialog* question = getWaitedQuestion(m_flowTab.data());
                            answerCloseQuestion(getVisibleDialogs(), question, m_flowTab.data());
                        }
                    }, Qt::QueuedConnection);
                }
                else if (m_isStackOpen && watched && watched->isWidgetType() && static_cast<QWidget*>(watched)->isWindow())
                {
                    addFinding(QStringLiteral("(8) the window %1 was shown after the close of %2, while the code of the site was on the stack")
                                   .arg(describe(watched), m_victimName));
                }
                break;

            default:
                break;
        }
        return false;
    }

private:
    static constexpr int MAXIMUM_ANSWER_ROUNDS = 4;
    static constexpr qsizetype MAXIMUM_FINDINGS = 24;

    template<typename T>
    static T* findAncestor(QObject* object)
    {
        for (QObject* current = object; current; current = current->parent())
        {
            if (T* typed = qobject_cast<T*>(current))
            {
                return typed;
            }
        }
        return nullptr;
    }

    static QString describe(const QObject* object)
    {
        if (!object)
        {
            return QStringLiteral("(none)");
        }
        const QObject* parent = object->parent();
        return QStringLiteral("%1 '%2' (parent %3)").arg(QString::fromLatin1(object->metaObject()->className()), object->objectName(),
                                                         parent ? QString::fromLatin1(parent->metaObject()->className()) : QStringLiteral("none"));
    }

    void addFinding(const QString& finding)
    {
        if (!m_findings.contains(finding) && m_findings.size() < MAXIMUM_FINDINGS)
        {
            m_findings << finding;
        }
    }

    static void reportConnectionCheck()
    {
        static bool isReported = false;
        if (!isReported && !PDF4QT_LIFETIME_CONNECTION_CHECK && LifetimeDeletionMode::isRetiring())
        {
            isReported = true;
            qInfo("LifetimeVictimCheck: check 9 (incoming connections of the retired objects) is not compiled in - the test does not link Qt6::CorePrivate");
        }
    }

    /// The visible dialogs (windows and children of the dialog host of the window)
    static QList<QDialog*> getVisibleDialogs()
    {
        QList<QDialog*> dialogs;
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            if (QDialog* dialog = qobject_cast<QDialog*>(topLevel); dialog && dialog->isVisible())
            {
                dialogs << dialog;
            }
            for (QDialog* dialog : topLevel->findChildren<QDialog*>())
            {
                if (dialog->isVisible() && !dialogs.contains(dialog))
                {
                    dialogs << dialog;
                }
            }
        }
        return dialogs;
    }

    /// A close prompt of a tool: the dialog (its object name) and the button of the user's "discard and close"
    struct ClosePrompt
    {
        const char* dialog;
        const char* discardButton;
    };

    /// The own close prompts of the tools (PDFDocumentToolGUI::askClose and its overrides, askIncompleteClose); the
    /// questions of the tabs and of the window are PDFAppDocumentChoiceDialog (manageCloseDialog, toolTabCloseDialog,
    /// the quit), answered by Don't Save / Stop
    static const std::vector<ClosePrompt>& getClosePrompts()
    {
        static const std::vector<ClosePrompt> prompts = {
            { "toolCloseQuestion", "toolCloseDiscard" },                    // PDFDocumentToolGUI::askClose
            { "redactClosePrompt", "redactCloseRemove" },                   // PDFAppToolGUIRedact::askClose
            { "signByHandDiscardDialog", "signByHandDiscard" },             // PDFAppToolGUISignByHand::askClose
            { "measureIncompleteQuestion", "measureIncompleteDiscard" },    // PDFAppToolGUIMeasure::askIncompleteClose
        };
        return prompts;
    }

    /// A question of the close is answered as the user closes: Don't Save (Quit Without Saving), else Stop and Close;
    /// the close prompt of a tool (getClosePrompts, visible and modal) by its discard. A question of the close is
    /// \p waitedQuestion - the question, which the leave flow of the close waits for (an open question of the tool,
    /// which the close has brought forward) - or a question opened after \p dialogsBefore inside \p owner (the closed
    /// tab, page or window: its subtree or a logical owner of it, PDFDeferredDeletion::getOwner; RB-L9 - a question
    /// about another tab is not answered). \returns false without such a question.
    static bool answerCloseQuestion(const QList<QDialog*>& dialogsBefore, const QDialog* waitedQuestion, const QObject* owner)
    {
        for (QDialog* dialog : getVisibleDialogs())
        {
            if (dialog != waitedQuestion && (dialogsBefore.contains(dialog) || !isInside(dialog, owner)))
            {
                continue;
            }

            bool isPrompt = false;
            for (const ClosePrompt& prompt : getClosePrompts())
            {
                if (dialog->objectName() != QLatin1String(prompt.dialog) || !dialog->isModal())
                {
                    continue;
                }
                isPrompt = true;
                QPushButton* discard = dialog->findChild<QPushButton*>(QString::fromLatin1(prompt.discardButton));
                if (discard && discard->isVisible() && discard->isEnabled())
                {
                    discard->click();
                    return true;
                }
            }
            if (isPrompt)
            {
                continue;
            }

            pdf::PDFAppDocumentChoiceDialog* question = qobject_cast<pdf::PDFAppDocumentChoiceDialog*>(dialog);
            if (!question)
            {
                continue;
            }
            for (pdf::PDFAppDocumentChoice choice : { pdf::PDFAppDocumentChoice::Discard, pdf::PDFAppDocumentChoice::Stop })
            {
                QPushButton* button = question->getChoiceButton(choice);
                if (button && button->isVisible() && button->isEnabled())
                {
                    button->click();
                    return true;
                }
            }
        }
        return false;
    }

    /// A visible modal result dialog of a tool among \p dialogsBefore (open before the close) is left
    /// without keeping the result: its Discard, else its Close (the written files stay written). \returns false
    /// without such a dialog.
    static bool answerResultDialog(const QList<QDialog*>& dialogsBefore)
    {
        for (QDialog* dialog : getVisibleDialogs())
        {
            pdf::PDFAppToolResultDialog* resultDialog = qobject_cast<pdf::PDFAppToolResultDialog*>(dialog);
            if (!resultDialog || !dialogsBefore.contains(dialog) || !resultDialog->isModal())
            {
                continue;
            }
            for (QPushButton* button : { resultDialog->getDiscardButton(), resultDialog->getCloseButton() })
            {
                if (button && button->isVisible() && button->isEnabled())
                {
                    button->click();
                    return true;
                }
            }
        }
        return false;
    }

    /// The visible question, which a running leave flow of a tab of \p window waits for, is answered as the user
    /// closes the tab without saving (Don't Save, else Stop; a close prompt of a tool by its discard) - only that
    /// question. \returns false without such a question.
    static bool answerTabLeaveQuestion(pdf::PDFAppMainWindow* window)
    {
        const QList<QDialog*> allVisible = getVisibleDialogs();
        for (pdf::PDFAppTab* tab : window ? window->getTabs() : QList<pdf::PDFAppTab*>())
        {
            const QDialog* question = tab ? getWaitedQuestion(tab) : nullptr;
            if (question && question->isVisible() && answerCloseQuestion(allVisible, question, tab))
            {
                return true;
            }
        }
        return false;
    }

    /// The close of \p tab goes on later: its leave flow (of this close, the goal CloseTab) runs and waits for no
    /// question - a step of the flow comes from the event loop (the deferred close of retire)
    static bool isPendingTabClose(pdf::PDFAppTab* tab)
    {
        const pdf::PDFAppLeaveFlow* flow = tab ? tab->findLeaveFlow() : nullptr;
        return flow && flow->isRunning() && flow->getGoal() == pdf::PDFAppLeaveFlow::Goal::CloseTab && !flow->getQuestion();
    }

    /// The question, which the leave flow of \p object (a tab, the window) waits for, nullptr = none
    static const QDialog* getWaitedQuestion(QObject* object)
    {
        pdf::PDFAppLeaveFlow* flow = nullptr;
        if (pdf::PDFAppTab* tab = qobject_cast<pdf::PDFAppTab*>(object))
        {
            flow = tab->findLeaveFlow();
        }
        else if (pdf::PDFAppMainWindow* window = qobject_cast<pdf::PDFAppMainWindow*>(object))
        {
            flow = window->findLeaveFlow();
        }
        return flow && flow->isRunning() ? flow->getQuestion() : nullptr;
    }

    /// The tab, which shows \p page (nullptr = none)
    static pdf::PDFAppTab* getShowingTab(pdf::PDFAppPage* page)
    {
        pdf::PDFAppTab* tab = page ? findAncestor<pdf::PDFAppTab>(page->parent()) : nullptr;
        return tab && tab->getPage() == page ? tab : nullptr;
    }

    /// RB-M2: the close of the page of \p tab goes on later - its navigation back is queued behind the navigation on the
    /// stack (PDFAppNavigator::isNavigating: done after it, the last request wins; only with a previous entry
    /// \p canGoBack - goBack queues nothing without it), or the flow of the tab, which this close started, runs without
    /// a question (the page answered later, the navigation goes on from the event loop; the page of the own tab of a
    /// tool closes with its tab, isPendingTabClose)
    bool isPendingPageClose(pdf::PDFAppTab* tab, bool canGoBack) const
    {
        pdf::PDFAppMainWindow* window = qobject_cast<pdf::PDFAppMainWindow*>(m_window.data());
        if (canGoBack && window && window->getNavigator()->isNavigating())
        {
            return true;
        }
        const pdf::PDFAppLeaveFlow* flow = tab ? tab->findLeaveFlow() : nullptr;
        return (flow && flow->isRunning() && flow->getGoal() == pdf::PDFAppLeaveFlow::Goal::Navigate && !flow->getQuestion()) || isPendingTabClose(tab);
    }

    /// \p object lies inside \p owner: its subtree, or the subtree of a logical owner (PDFDeferredDeletion::getOwner)
    /// of one of its ancestors, which lies inside \p owner (the question of a leave flow, parented to the window)
    static bool isInside(const QObject* object, const QObject* owner)
    {
        int ownerSteps = 0;
        for (const QObject* start = object; owner && start && ownerSteps < 16; ++ownerSteps)
        {
            const QObject* logicalOwner = nullptr;
            for (const QObject* current = start; current; current = current->parent())
            {
                if (current == owner)
                {
                    return true;
                }
                logicalOwner = logicalOwner ? logicalOwner : pdf::PDFDeferredDeletion::getOwner(current);
            }
            start = logicalOwner;
        }
        return false;
    }

    /// The visible dialogs, which are not among \p dialogsBefore
    static QString describeNewDialogs(const QList<QDialog*>& dialogsBefore)
    {
        QStringList dialogs;
        for (QDialog* dialog : getVisibleDialogs())
        {
            if (!dialogsBefore.contains(dialog))
            {
                dialogs << describe(dialog);
            }
        }
        return dialogs.join(QStringLiteral(", "));
    }

    /// The questions of a refused close are canceled (the user's Cancel)
    static void rejectNewDialogs(const QList<QDialog*>& dialogsBefore)
    {
        for (QDialog* dialog : getVisibleDialogs())
        {
            if (!dialogsBefore.contains(dialog))
            {
                dialog->reject();
            }
        }
    }

    /// (10) The close was refused, because a modal dialog of the tab, open before the close (\p modalBefore), decides
    /// first - it is still visible -, or a tool of \p tab decided about its result before the close and still does
    /// (\p wasDeciding: its result dialog in the middle of its answer on the stack of the site); the close has opened no
    /// new dialog. finish() fails a tool, which still decides without a visible dialog after settle() (stuck).
    static bool isRefusedByOpenDialog(const QList<QPointer<QDialog>>& modalBefore, const QList<QDialog*>& dialogsBefore, bool wasDeciding, const QObject* tab)
    {
        bool isOpen = wasDeciding && tab && isToolDeciding(tab);
        for (const QPointer<QDialog>& dialog : modalBefore)
        {
            isOpen = isOpen || (dialog && dialog->isVisible() && dialog->isModal());
        }
        return isOpen && describeNewDialogs(dialogsBefore).isEmpty();
    }

    /// A tool shell in \p tab decides about its result (PDFDocumentToolShell::State::Deciding)
    static bool isToolDeciding(const QObject* tab)
    {
        for (const pdf::PDFDocumentToolShell* shell : tab->findChildren<const pdf::PDFDocumentToolShell*>())
        {
            if (shell->getState() == pdf::PDFDocumentToolShell::State::Deciding)
            {
                return true;
            }
        }
        return false;
    }

    /// A visible modal dialog lies inside \p tab (its subtree or owned by it)
    static bool hasVisibleModalDialog(const QObject* tab)
    {
        for (QDialog* dialog : getVisibleDialogs())
        {
            if (dialog->isModal() && isInside(dialog, tab))
            {
                return true;
            }
        }
        return false;
    }

    /// (10) The close refused by an open modal dialog of the tab (Refusal::RefusedByOpenDialog, counted): \returns false
    bool refuseByOpenDialog()
    {
        m_refusal = Refusal::RefusedByOpenDialog;
        ++s_refusedByOpenDialogCount;
        return false;
    }

    /// (10) A refused close, which is a finding of finish(): \returns false (the result of retire)
    bool refuse(const QString& reason)
    {
        m_refusal = Refusal::Refused;
        m_refusalReason = reason;
        return false;
    }

    /// The scope level of the event delivery of the GUI thread (QThreadData::scopeLevel: the number of the events
    /// being delivered on the stack), -1 without the private headers of QtCore
    static int getScopeLevel()
    {
#if PDF4QT_LIFETIME_CONNECTION_CHECK
        const QThreadData* data = QObjectPrivate::get(QCoreApplication::instance())->threadData.loadRelaxed();
        return data ? data->scopeLevel : -1;
#else
        return -1;
#endif
    }

    /// (8), (9) \p event is delivered by the event dispatcher at the outermost level of the event delivery (the level
    /// of the test body + 1): a posted event (sendPostedEvents runs, QPostEventList::recursion), a timer, or - when
    /// \p isSpontaneousEnding (the close was inside an event) - a spontaneous event of the window system or of the input
    /// of QTest. No code, which was on the stack at the close, runs then: the code of an event at that level has
    /// returned to the dispatcher, the code of the test body has gone on. An event sent synchronously at that level
    /// (Qt finishes QWidget::show of the test body: the Show events of the children) does not end it - the code,
    /// which sends it, was on the stack at the close.
    bool isTopLevelDispatch(const QEvent* event, bool isSpontaneousEnding) const
    {
#if PDF4QT_LIFETIME_CONNECTION_CHECK
        const QThreadData* data = QObjectPrivate::get(QCoreApplication::instance())->threadData.loadRelaxed();
        if (!data || m_baseLevel < 0 || data->scopeLevel > m_baseLevel + 1)
        {
            return false;
        }
        return data->postEventList.recursion > 0 || event->type() == QEvent::Timer || (isSpontaneousEnding && event->spontaneous());
#else
        Q_UNUSED(event);
        Q_UNUSED(isSpontaneousEnding);
        return false;
#endif
    }

    /// (8), (9) The event dispatcher starts to process events: at the level of the test body (no event delivered on the
    /// stack; unknown without the private headers of QtCore) the stacks of the close and of a retire have returned
    void onDispatcherStarts()
    {
        const int level = getScopeLevel();
        if (level < 0 || level <= m_baseLevel)
        {
            endStackOfClose();
            endStackOfRetire();
        }
    }

    /// (8) The code on the stack of the close has returned
    void endStackOfClose() { m_isStackOpen = false; }

    /// (9) The stack of the later retire of the victim has returned: its incoming connections after the close
    void endStackOfRetire()
    {
        if (m_isRetireCheckDue)
        {
            m_isRetireCheckDue = false;
            if (m_victim && m_isRetired && !m_isLastLookDone)
            {
                checkConnections("after the close");
            }
        }
    }

    /// (5) The victim is in the lists of the window: a tab of the window (or its current tab), a page shown by a tab
    /// of the window
    bool isListed(QObject* victim) const
    {
        pdf::PDFAppMainWindow* window = qobject_cast<pdf::PDFAppMainWindow*>(m_window.data());
        if (!window || !victim)
        {
            return false;
        }

        if (pdf::PDFAppTab* tab = qobject_cast<pdf::PDFAppTab*>(victim))
        {
            return window->getTabs().contains(tab) || window->getCurrentTab() == tab;
        }

        pdf::PDFAppPage* page = findAncestor<pdf::PDFAppPage>(victim);
        pdf::PDFAppTab* tab = page ? findAncestor<pdf::PDFAppTab>(page->parent()) : nullptr;
        return tab && tab->getPage() == page && window->getTabs().contains(tab);
    }

    /// (4) A Show event of \p widget, which the gate drops (a retired object), ends a show in progress at the close: the
    /// widget or an ancestor was in QWidgetPrivate::show_helper then (QWidgetData::in_show) and is in it still - Qt
    /// shows the children of a widget in its show_helper and sends their Show events and its own on the same stack. A
    /// show, which starts after the close outside of such a show, is still counted.
    bool isShowFinishedByQt(QWidget* widget) const
    {
        if (m_showsAtClose.isEmpty() || !pdf::PDFDeferredDeletion::isEventBlocked(QEvent::Show) || !pdf::PDFDeferredDeletion::isRetired(widget))
        {
            return false;
        }
        for (QWidget* current = widget; current; current = current->parentWidget())
        {
            if (qt_qwidget_data(current)->in_show && m_showsAtClose.contains(QPointer<QWidget>(current)))
            {
                return true;
            }
        }
        return false;
    }

    /// (7) The named exception of the new children: the worker thread of a page compiler of the victim, created on the
    /// stack of the close. PDFDrawWidgetProxy::setDocument (Pdf4QtLibWidgets, not changed in step A) emits the change
    /// of the draw space (the view sends viewChanged, whose slot closes the tab) and then starts its compiler
    /// (PDFAsynchronousPageCompiler::start, a new PDFAsynchronousPageCompilerWorkerThread) - no code of the application
    /// runs after the close; the compiler stops the thread in its destructor (and ~PDFDrawWidgetProxy before it). A
    /// compiler thread started later (from the event loop) in a retired subtree is still a finding.
    bool isCompilerThreadOfClose(const QObject* object) const
    {
        const QObject* parent = object->parent();
        bool isAddedOnStack = false;
        for (const QPointer<QObject>& child : m_childrenAddedOnStack)
        {
            isAddedOnStack = isAddedOnStack || child.data() == object;
        }
        return isAddedOnStack && qstrcmp(object->metaObject()->className(), "pdf::PDFAsynchronousPageCompilerWorkerThread") == 0 && parent &&
               qstrcmp(parent->metaObject()->className(), "pdf::PDFAsynchronousPageCompiler") == 0;
    }

    /// (7) \p object was in the subtree of the victim at its retire - the same object, not a new one at the address of
    /// a dead one (RB-L3: the snapshot holds guarded pointers)
    bool isInRetiredSnapshot(const QObject* object) const
    {
        const auto it = m_subtree.constFind(object);
        return it != m_subtree.cend() && it.value().data() == object;
    }

    bool isVictimOrAncestor(const QObject* object) const
    {
        // The victim, an ancestor of it or a logical owner (and its ancestors) of one of them
        int ownerSteps = 0;
        for (const QObject* start = m_victim.data(); start && ownerSteps < 16; ++ownerSteps)
        {
            const QObject* owner = nullptr;
            for (const QObject* current = start; current; current = current->parent())
            {
                if (current == object)
                {
                    return true;
                }
                owner = owner ? owner : pdf::PDFDeferredDeletion::getOwner(current);
            }
            start = owner;
        }
        return false;
    }

    /// The victim is retired: the version of its document (6) and its subtree (7) at that moment
    void startRetiredChecks()
    {
        m_isRetired = true;
        m_documentVersion = m_document ? m_document->getVersion() : 0;
        m_documentRefusals = m_document ? m_document->getRetiredChangeRefusalCount() : 0;
        m_subtree.insert(m_victim.data(), m_victim);
        for (QObject* child : m_victim->findChildren<QObject*>())
        {
            m_subtree.insert(child, QPointer<QObject>(child));
        }
    }

    /// The last look at the victim, when its deferred deletion comes: (4), (5), (6), (7), (9)
    void lastLook()
    {
        m_isLastLookDone = true;
        QObject* victim = m_victim.data();

        if (m_mode == LifetimeDeletionMode::Mode::Deferred && isListed(victim))
        {
            addFinding(QStringLiteral("(5) %1 is in the lists of the window at its deletion").arg(m_victimName));
        }

        QList<QObject*> subtree = victim->findChildren<QObject*>();
        subtree.prepend(victim);
        for (QObject* object : subtree)
        {
            QWidget* widget = qobject_cast<QWidget*>(object);
            if (widget && (widget == victim || widget->isWindow()) && widget->isVisible())
            {
                addFinding(QStringLiteral("(4) %1 is visible at the deletion of %2").arg(describe(widget), m_victimName));
            }
        }

        if (!m_isRetired)
        {
            return;
        }

        if (m_document && m_document->getVersion() != m_documentVersion)
        {
            addFinding(QStringLiteral("(6) the document of %1 changed after its close (version %2 -> %3)").arg(m_victimName).arg(m_documentVersion).arg(m_document->getVersion()));
        }

        // (6) also a change refused by the retired document (PDFAppDocument's guard): its caller went on after the close
        if (m_document && m_document->getRetiredChangeRefusalCount() != m_documentRefusals)
        {
            addFinding(QStringLiteral("(6) a change of the document of %1 was tried after its close (%2 refused, last by %3)").arg(m_victimName)
                           .arg(m_document->getRetiredChangeRefusalCount() - m_documentRefusals).arg(QString::fromLatin1(m_document->getLastRetiredChangeRefusal())));
        }

        for (const QObject* object : std::as_const(subtree))
        {
            if (!isInRetiredSnapshot(object) && !pdf::PDFDeferredDeletion::isHelperObject(object) && !isCompilerThreadOfClose(object))
            {
                addFinding(QStringLiteral("(7) a new child %1 appeared in %2 after its close").arg(describe(object), m_victimName));
            }
        }

        checkConnections("at the deletion");
    }

    /// (9) The incoming connections of the retired objects (the victim and every retired subtree of the application)
    void checkConnections(const char* when)
    {
#if PDF4QT_LIFETIME_CONNECTION_CHECK
        QList<QObject*> roots;
        if (m_victim)
        {
            roots << m_victim.data();
        }
        for (QWidget* widget : QApplication::allWidgets())
        {
            if (pdf::PDFDeferredDeletion::isRetired(widget) && !(widget->parent() && pdf::PDFDeferredDeletion::isRetired(widget->parent())))
            {
                roots << widget;
            }
        }

        QSet<const QObject*> visited;
        for (QObject* root : std::as_const(roots))
        {
            QList<QObject*> objects = root->findChildren<QObject*>();
            objects.prepend(root);
            for (QObject* object : std::as_const(objects))
            {
                if (visited.contains(object) || pdf::PDFDeferredDeletion::isHelperObject(object))
                {
                    continue;
                }
                visited.insert(object);
                checkIncoming(object, when);
            }
        }
#else
        Q_UNUSED(when);
#endif
    }

#if PDF4QT_LIFETIME_CONNECTION_CHECK
    /// The signal of the index in the signal range of the meta object (QObjectPrivate::Connection::signal_index)
    static QMetaMethod getSignal(const QMetaObject* metaObject, int signalIndex)
    {
        int index = 0;
        for (int i = 0; i < metaObject->methodCount(); ++i)
        {
            const QMetaMethod method = metaObject->method(i);
            if (method.methodType() == QMetaMethod::Signal)
            {
                if (index == signalIndex)
                {
                    return method;
                }
                ++index;
            }
        }
        return QMetaMethod();
    }

    /// The named exception 3 of check 9 (besides getConnectionExceptions): QTreeView connects QVariantAnimation::finished
    /// of its expand animation (QTreeViewPrivate::animatedOperation, a value member of its private part without a
    /// parent) to its own private slot QTreeViewPrivate::endAnimatedOperation (QTreeViewPrivate::initialize,
    /// QObjectPrivate::connect, the context is the view). The sender is a part of the view itself, unreachable without
    /// the private API of QtWidgets, and the slot only ends the view's own expand animation - harmless. Matched as
    /// narrowly as the public API allows: a QVariantAnimation without a parent, the signal finished, a receiver, which
    /// is a QTreeView, a functor / member function connection (QObjectPrivate::connect), and the sender is exactly
    /// QTreeViewPrivate::animatedOperation of the receiver (the private headers of QtWidgets) - another animation
    /// connected to the view is still reported. Without the private headers of QtWidgets the exception is not made
    /// (RB-L3: fails closed, isTreeViewAnimationCandidate names the reason in the finding).
    static bool isTreeViewOwnAnimation(const QObject* sender, const QByteArray& signal, QObject* receiver, bool isSlotObject)
    {
        if (!isTreeViewAnimationCandidate(sender, signal, receiver, isSlotObject))
        {
            return false;
        }

#if PDF4QT_LIFETIME_TREE_VIEW_PRIVATE
        const QTreeViewPrivate* treeViewPrivate = static_cast<const QTreeViewPrivate*>(QObjectPrivate::get(static_cast<const QObject*>(receiver)));
        return sender == static_cast<const QObject*>(&treeViewPrivate->animatedOperation);
#else
        return false;
#endif
    }

    /// The connection has the shape of the own expand animation of a QTreeView (isTreeViewOwnAnimation)
    static bool isTreeViewAnimationCandidate(const QObject* sender, const QByteArray& signal, const QObject* receiver, bool isSlotObject)
    {
        return isSlotObject && signal == "finished" && !sender->parent() && sender->inherits("QVariantAnimation") && receiver->inherits("QTreeView");
    }

    /// The named exception 4 of check 9: PDFPageContentScene connects PDFPageContentElementManipulator::selectionChanged
    /// of its manipulator (m_manipulator, a value member of the scene without a parent, deleted with it) to its own slot
    /// onSelectionChanged (pdfpagecontentelements.cpp, the constructor of the scene). The sender is a part of the scene
    /// itself and Pdf4QtLibWidgets is not changed in step A (owner QR.5) - harmless. Matched narrowly: the exact sender
    /// class, the signal selectionChanged, a receiver, which is a PDFPageContentScene, a sender without a parent, whose
    /// address lies inside the scene object (its value member); a manipulator outside the scene connected to a retired
    /// scene is still reported.
    static bool isContentSceneOwnManipulator(const QObject* sender, const QByteArray& signal, QObject* receiver)
    {
        const pdf::PDFPageContentScene* scene = qobject_cast<const pdf::PDFPageContentScene*>(receiver);
        if (!scene || signal != "selectionChanged" || sender->parent() || qstrcmp(sender->metaObject()->className(), "pdf::PDFPageContentElementManipulator") != 0)
        {
            return false;
        }

        const char* sceneStart = reinterpret_cast<const char*>(scene);
        const char* address = reinterpret_cast<const char*>(sender);
        return address > sceneStart && address - sceneStart < std::ptrdiff_t(sizeof(pdf::PDFPageContentScene));
    }

    /// A named exception of getConnectionExceptions: the exact classes and signal, the sender without a parent lies
    /// inside the receiver object (its value member, RB-L5)
    static bool isConnectionException(const QObject* sender, const QByteArray& signal, const QObject* receiver)
    {
        const char* address = reinterpret_cast<const char*>(sender);
        const char* receiverStart = reinterpret_cast<const char*>(receiver);
        for (const ConnectionException& exception : getConnectionExceptions())
        {
            if (qstrcmp(exception.senderClass, sender->metaObject()->className()) == 0 && signal == exception.signal &&
                qstrcmp(exception.receiverClass, receiver->metaObject()->className()) == 0 && !sender->parent() && address > receiverStart &&
                address - receiverStart < exception.receiverSize)
            {
                return true;
            }
        }
        return false;
    }

    /// A connection, which Qt makes from a model or a selection model to its views (QAbstractItemView::setModel,
    /// QHeaderView, QItemSelectionModel): into an object of Qt, Qt's own business
    static bool isQtModelViewConnection(const QObject* sender, const QObject* receiver)
    {
        const bool isModel = qobject_cast<const QAbstractItemModel*>(sender) || qobject_cast<const QItemSelectionModel*>(sender);
        const bool isView = qobject_cast<const QAbstractItemView*>(receiver) || qobject_cast<const QItemSelectionModel*>(receiver);
        return isModel && isView;
    }

    void checkIncoming(QObject* receiver, const char* when)
    {
        // The slots of the application (the classes of PDF4QT); an object of Qt in the subtree is checked for the
        // senders of the application (a functor with a context of Qt runs code of the application, RB-L5) - a
        // connection of Qt into its own object (a view of a model outside) is Qt's own business
        const char* receiverClass = receiver->metaObject()->className();
        const bool isApplicationReceiver = qstrncmp(receiverClass, "pdf::", 5) == 0;

        const QObjectPrivate::ConnectionData* data = QObjectPrivate::get(receiver)->connections.loadAcquire();
        if (!data)
        {
            return;
        }

        const QThread* mainThread = QCoreApplication::instance()->thread();
        for (const QObjectPrivate::Connection* connection = data->senders; connection; connection = connection->next)
        {
            const QObject* sender = connection->sender;
            if (!sender || connection->receiver.loadRelaxed() != receiver || sender == receiver)
            {
                continue;
            }
            if (sender->thread() == mainThread && pdf::PDFDeferredDeletion::isRetired(sender))
            {
                continue;
            }

            const char* senderClass = sender->metaObject()->className();
            if (!isApplicationReceiver && (qstrncmp(senderClass, "pdf::", 5) != 0 || isQtModelViewConnection(sender, receiver)))
            {
                continue;
            }

            const QMetaMethod signal = getSignal(sender->metaObject(), connection->signal_index);
            const QByteArray signalName = signal.isValid() ? signal.name() : QByteArray("?");
            if (signalName == "destroyed" || isConnectionException(sender, signalName, receiver) || isTreeViewOwnAnimation(sender, signalName, receiver, connection->isSlotObject) ||
                isContentSceneOwnManipulator(sender, signalName, receiver))
            {
                continue;
            }

            const bool isUnmatchedTreeViewAnimation = !PDF4QT_LIFETIME_TREE_VIEW_PRIVATE && isTreeViewAnimationCandidate(sender, signalName, receiver, connection->isSlotObject);
            addFinding(QStringLiteral("(9) %1::%2 of a living sender %3 reaches %4 of a retired subtree (%5 of %6)%7")
                           .arg(QString::fromLatin1(senderClass), QString::fromLatin1(signalName), describe(sender), describe(receiver), QString::fromLatin1(when),
                                m_victimName,
                                isUnmatchedTreeViewAnimation ? QStringLiteral(" - the own expand animation of a QTreeView is recognized only with Qt6::WidgetsPrivate") : QString()));
        }
    }
#endif

    AppDocumentFixture* m_fixture = nullptr;
    std::function<bool(QObject*)> m_closeFunction;
    LifetimeDeletionMode::Mode m_mode = LifetimeDeletionMode::Mode::Deferred;
    QPointer<QWidget> m_window;
    QPointer<QObject> m_victim;
    QString m_victimName;
    bool m_isStarted = false;
    Refusal m_refusal = Refusal::None;
    bool m_isRetired = false;
    QString m_refusalReason;        ///< (10) Why the close was refused (Refusal::Refused)
    QPointer<pdf::PDFAppTab> m_flowTab; ///< The tab, whose leave flow decides the close (the victim tab, the tab of the page)
    bool m_isPendingClose = false;  ///< A tab or page, whose close goes on from the event loop (retired later)
    bool m_isLastLookDone = false;
    bool m_isStackOpen = false;
    int m_baseLevel = -1;           ///< (8) The scope level of the event delivery of the test body (-1 unknown)
    int m_closeLevel = -1;          ///< (8) The scope level at the close
    bool m_isRetireCheckDue = false;    ///< (9) The check after a later retire waits for the end of its stack
    int m_retireLevel = -1;         ///< (9) The scope level at the later retire
    QPointer<pdf::PDFAppDocument> m_document;
    quint64 m_documentVersion = 0;
    int m_documentRefusals = 0;
    QHash<const QObject*, QPointer<QObject>> m_subtree; ///< (7) the subtree at the retire, guarded (RB-L3)
    qint64 m_shownBefore = 0;
    QList<QPointer<QWidget>> m_showsAtClose;   ///< (4) the widgets in a show in progress at the close
    qint64 m_finishedShows = 0;                ///< (4) the Show events of the shows in progress at the close, dropped by the gate
    QList<QPointer<QObject>> m_childrenAddedOnStack; ///< (7) the children added to a page compiler on the stack of the close
    QStringList m_findings;

    static inline int s_pageFallbackCount = 0;
    static inline int s_notClosableCount = 0;
    static inline int s_refusedWhileLeavingCount = 0;
    static inline int s_refusedByOpenDialogCount = 0;
};

}   // namespace pdftest

#endif // LIFETIMEVICTIMCHECK_H
