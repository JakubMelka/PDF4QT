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

// Deferred deletion at one place (step A, newapp_robustness.md 12, 13.5, 14.4-14.5, 15.7, 16; p2a_common.md):
// PDFDeferredDeletion::retire, the notifier, the connection scope, the logical owner, the tokens, the event gate and
// the synchronous deletion detector. The expected values come from the contract of retire (the binding order 0-9)
// and from the rules of Qt, never from a run.

#include "pdfdeferreddeletion.h"

#include <QAbstractAnimation>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFutureInterface>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStringListModel>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

#include <functional>
#include <vector>

/// Emits the signals of the tests (a sender outside the subtree of the receivers)
class DeferredDeletionSender : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void fire() { Q_EMIT fired(); }
    void change(int value) { Q_EMIT valueChanged(value); }

Q_SIGNALS:
    void fired();
    void valueChanged(int value);
};

/// Counts the calls of its slots and records the sender
class DeferredDeletionReceiver : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    int calls = 0;
    int valueCalls = 0;
    QObject* lastSender = nullptr;
    std::function<void()> onCall;

public Q_SLOTS:
    void onFired()
    {
        ++calls;
        lastSender = sender();
        if (onCall)
        {
            onCall();
        }
    }

    void onValue(int)
    {
        ++valueCalls;
    }
};

/// A widget with a hook (PDFRetirable)
class RetirableWidget : public QWidget, public pdf::PDFRetirable
{
public:
    explicit RetirableWidget(QWidget* parent = nullptr) :
        QWidget(parent),
        pdf::PDFRetirable(this)
    {

    }

    int hookCalls = 0;
    std::function<void()> hookAction;

protected:
    virtual void retire() override
    {
        ++hookCalls;
        if (hookAction)
        {
            hookAction();
        }
    }
};

/// An object with a hook (PDFRetirable)
class RetirableObject : public QObject, public pdf::PDFRetirable
{
public:
    explicit RetirableObject(QObject* parent = nullptr) :
        QObject(parent),
        pdf::PDFRetirable(this)
    {

    }

    int hookCalls = 0;
    std::function<void()> hookAction;

protected:
    virtual void retire() override
    {
        ++hookCalls;
        if (hookAction)
        {
            hookAction();
        }
    }
};

/// Records the types of the events it receives
class EventRecordingWidget : public QWidget
{
public:
    using QWidget::QWidget;

    QList<QEvent::Type> received;

protected:
    virtual bool event(QEvent* event) override
    {
        received.push_back(event->type());
        return QWidget::event(event);
    }
};

/// Records the types of the events it receives and the calls of its queued slot
class EventRecordingObject : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    QList<QEvent::Type> received;
    int queuedCalls = 0;

    Q_INVOKABLE void onQueued() { ++queuedCalls; }

protected:
    virtual bool event(QEvent* event) override
    {
        received.push_back(event->type());
        return QObject::event(event);
    }
};

/// Queues a call of its own from its hide event (as Qt and the settings page do)
class HideQueuingWidget : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    int queuedCalls = 0;

    Q_INVOKABLE void onHiddenLater() { ++queuedCalls; }

protected:
    virtual void hideEvent(QHideEvent* event) override
    {
        QWidget::hideEvent(event);
        QMetaObject::invokeMethod(this, &HideQueuingWidget::onHiddenLater, Qt::QueuedConnection);
    }
};

/// A dialog whose overrides refuse to close (as the result dialog while it writes, a message box without Escape)
class RefusingDialog : public QDialog
{
public:
    using QDialog::QDialog;

    int rejectCalls = 0;
    int doneCalls = 0;

    virtual void reject() override { ++rejectCalls; }
    virtual void done(int) override { ++doneCalls; }
};

/// An editor, which commits its value in its own focus out event (no signal)
class FocusOutCommitWidget : public QWidget
{
public:
    FocusOutCommitWidget(QString* store, QWidget* parent) :
        QWidget(parent),
        m_store(store)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

    QString value;

protected:
    virtual void focusOutEvent(QFocusEvent* event) override
    {
        *m_store = value;
        QWidget::focusOutEvent(event);
    }

private:
    QString* m_store;
};

/// Counts the events of the watched objects
class EventCountingFilter : public QObject
{
public:
    using QObject::QObject;

    int count = 0;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::User)
        {
            ++count;
        }
        return QObject::eventFilter(watched, event);
    }
};

/// An application filter of the application (as the tooltip, the style, the context): counts the presses it sees
class PressCountingFilter : public QObject
{
public:
    using QObject::QObject;

    int count = 0;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            ++count;
        }
        return QObject::eventFilter(watched, event);
    }
};

/// An application filter owned by another object (as the press filter of a popup), which retires that object when
/// it sees a user event - the retire runs inside the dispatch of the filters of the application
class RetiringFilter : public QObject
{
public:
    explicit RetiringFilter(QObject* retired) :
        m_retired(retired)
    {

    }

    int count = 0;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::User)
        {
            ++count;
            pdf::PDFDeferredDeletion::retire(m_retired.data());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QObject> m_retired;
};

/// A token, which counts its releases
class CountingToken : public pdf::PDFLifetimeToken
{
public:
    CountingToken(QObject* owner, int* releases) :
        pdf::PDFLifetimeToken(owner),
        m_releases(releases)
    {

    }

    virtual ~CountingToken() override
    {
        releaseToken();
    }

protected:
    virtual void release() override
    {
        ++*m_releases;
    }

private:
    int* m_releases;
};

/// Helpers of the test
class DeferredDeletionTestHelper
{
public:
    DeferredDeletionTestHelper() = delete;

    /// The posted events and the deferred deletions of the retired objects (as the event loop delivers them)
    static void settle()
    {
        for (int i = 0; i < 3; ++i)
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
    }

    static int getTimeoutFactor()
    {
        return qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    }

    static QMouseEvent createMouseEvent(QEvent::Type type)
    {
        return QMouseEvent(type, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton, type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    }
};

class DeferredDeletionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    // Connections through the connection scope (RR2-H1, RR2-L4)
    void retireFromScopedSlot();
    void retireLaterKeepsRestOfEmission();
    void uniqueConnectionStaysUnique();
    void targetedDisconnectStillWorks();
    void senderVisibleInScopedSlot();
    void connectToRetiredReceiverIsCut();

    // The steps of retire
    void retireMakesInertAtOnce();
    void ownedPopupClosedOnRetire();
    void overriddenRejectNotCalled();
    void notifierEmitsOnParentDeletion();
    void notifierEmitsOnceOnRetireThenDelete();
    void listenerReentersRetire();
    void queuedByHideIsDropped();
    void focusOutCommitsBeforeRetire();
    void retireFlushesPendingWrite();
    void watcherDisconnectedNotCanceled();
    void destroyedStaysConnected();
    void outgoingSignalsDisconnectedExceptDestroyed();
    void filtersRemovedOnRetire();
    void filterInstalledAgainRememberedOnce();
    void hooksCalledOnceInSubtreeAndOwned();
    void isRetiredFollowsParentAndOwner();
    void retiredOwnerRetiresNewOwnedObject();
    void ownedEntriesOfDeletedObjectsPruned();
    void objectMovedOutStaysWhole();
    void retireInsideRetire();
    void focusMovesToNextOutsideSubtree();

    // The event gate (RR3-H3, QR3.6)
    void gateDropsBlockedEventsAndCounts();
    void gatePassesOtherEvents();
    void focusOutCommitsDespiteGate();
    void childRemovedKeepsLayoutSane();
    void futureCallOutPassesGate();
    void retireStepsAreNotGated();
    void gateRunsBeforeApplicationFilters();
    void retireInsideApplicationFilterSkipsNoFilter();

    // Tokens (15.7)
    void tokenReleasedOnceOnRetire();
    void tokenReleasedOnDestructionWithoutRetire();

    // The synchronous deletion detector (RR-H3) and flush
    void synchronousDeletionReported();
    void deferredDeletionWithoutRetireReported();
    void deletionAfterRetireNotReported();
    void lateChildOfRetiredRootNotReported();
    void deletionInsideScopeOrFlushNotReported();
    void flushRefusedInsideEventLoop();

    // Cost (12.4)
    void retireCostOfLargeSubtree();
};

void DeferredDeletionTest::init()
{
    // An unexpected report of the detector or of the gate fails the test (expected ones are ignored by the test)
    QTest::failOnWarning(QRegularExpression(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::SYNCHRONOUS_DELETION_MESSAGE)))));
    QTest::failOnWarning(QRegularExpression(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::DROPPED_EVENT_MESSAGE)))));
    pdf::PDFDeferredDeletion::resetDroppedEventCounts();
    pdf::PDFDeferredDeletion::resetSynchronousDeletionCount();
    pdf::PDFDeferredDeletion::setTestMode(true);
    pdf::PDFDeferredDeletion::setSynchronousDeletionReported(true);
}

void DeferredDeletionTest::cleanup()
{
    pdf::PDFDeferredDeletion::setTestMode(false);
    pdf::PDFDeferredDeletion::setSynchronousDeletionReported(false);
    pdf::PDFDeferredDeletion::flush();
}

void DeferredDeletionTest::retireFromScopedSlot()
{
    // retire inside the slot of the connection being delivered: the receiver retired in it gets nothing more, another
    // receiver retired in it is not called by the same emission, a third receiver is (RR2-L4)
    DeferredDeletionSender sender;
    QPointer<DeferredDeletionReceiver> first = new DeferredDeletionReceiver();
    QPointer<DeferredDeletionReceiver> second = new DeferredDeletionReceiver();
    QPointer<DeferredDeletionReceiver> third = new DeferredDeletionReceiver();
    QVERIFY(pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, first.data(), &DeferredDeletionReceiver::onFired));
    QVERIFY(pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, second.data(), &DeferredDeletionReceiver::onFired));
    QVERIFY(pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, third.data(), &DeferredDeletionReceiver::onFired));
    first->onCall = [&first, &second]()
    {
        pdf::PDFDeferredDeletion::retire(first.data());
        pdf::PDFDeferredDeletion::retire(second.data());
    };

    sender.fire();
    QCOMPARE(first->calls, 1);
    QCOMPARE(second->calls, 0);
    QCOMPARE(third->calls, 1);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(first));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(second));

    sender.fire();
    QCOMPARE(first->calls, 1);
    QCOMPARE(second->calls, 0);
    QCOMPARE(third->calls, 2);

    DeferredDeletionTestHelper::settle();
    QVERIFY(first.isNull());
    QVERIFY(second.isNull());
    QVERIFY(third);
    delete third.data();
}

void DeferredDeletionTest::retireLaterKeepsRestOfEmission()
{
    // The close of the sender from a slot of its own signal (lesson 1b of step A): retire would cut the rest of the
    // emission (the outgoing signals of the retired subtree are disconnected), retireLater hides the sender now and
    // retires it from the event loop - the later receivers of the emission still run
    QPointer<QWidget> parent = new QWidget();
    QPointer<DeferredDeletionSender> sender = new DeferredDeletionSender(parent);
    QPointer<QWidget> widget = new QWidget(parent);
    parent->show();
    DeferredDeletionReceiver first;
    DeferredDeletionReceiver second;
    QObject::connect(sender.data(), &DeferredDeletionSender::fired, &first, &DeferredDeletionReceiver::onFired);
    QObject::connect(sender.data(), &DeferredDeletionSender::fired, &second, &DeferredDeletionReceiver::onFired);
    first.onCall = [&parent, &widget]()
    {
        pdf::PDFDeferredDeletion::retireLater(parent.data());
        QVERIFY(parent->isHidden());
        QVERIFY(!pdf::PDFDeferredDeletion::isRetired(parent.data()));
    };

    sender->fire();
    QCOMPARE(first.calls, 1);
    QCOMPARE(second.calls, 1);
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(widget.data()));

    // The queued call retires it; a second retireLater meanwhile retires it once
    pdf::PDFDeferredDeletion::retireLater(parent.data());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QVERIFY(parent);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(widget.data()));
    sender->fire();
    QCOMPARE(second.calls, 1);

    DeferredDeletionTestHelper::settle();
    QVERIFY(parent.isNull());
    QVERIFY(sender.isNull());
}

void DeferredDeletionTest::uniqueConnectionStaysUnique()
{
    DeferredDeletionSender sender;
    DeferredDeletionReceiver receiver;
    const QMetaObject::Connection firstConnection = pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, &receiver, &DeferredDeletionReceiver::onFired, Qt::UniqueConnection);
    const QMetaObject::Connection secondConnection = pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, &receiver, &DeferredDeletionReceiver::onFired, Qt::UniqueConnection);
    QVERIFY(firstConnection);
    QVERIFY(!secondConnection);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(&receiver), 1);

    sender.fire();
    QCOMPARE(receiver.calls, 1);
}

void DeferredDeletionTest::targetedDisconnectStillWorks()
{
    // The receiver of the connection is the real receiver: QObject::disconnect by sender / signal / receiver / slot
    // and sender->disconnect(receiver) find it (the pattern of a document or view exchange)
    DeferredDeletionSender sender;
    DeferredDeletionReceiver receiver;
    pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, &receiver, &DeferredDeletionReceiver::onFired);
    pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::valueChanged, &receiver, &DeferredDeletionReceiver::onValue);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(&receiver), 2);

    QVERIFY(QObject::disconnect(&sender, &DeferredDeletionSender::fired, &receiver, &DeferredDeletionReceiver::onFired));
    sender.fire();
    sender.change(1);
    QCOMPARE(receiver.calls, 0);
    QCOMPARE(receiver.valueCalls, 1);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(&receiver), 1);

    QVERIFY(sender.disconnect(&receiver));
    sender.change(2);
    QCOMPARE(receiver.valueCalls, 1);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(&receiver), 0);
}

void DeferredDeletionTest::senderVisibleInScopedSlot()
{
    DeferredDeletionSender sender;
    DeferredDeletionReceiver receiver;
    pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, &receiver, &DeferredDeletionReceiver::onFired);
    sender.fire();
    QCOMPARE(receiver.lastSender, &sender);

    // A queued connection keeps the sender too
    DeferredDeletionReceiver queuedReceiver;
    pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, &queuedReceiver, &DeferredDeletionReceiver::onFired, Qt::QueuedConnection);
    sender.fire();
    QCOMPARE(queuedReceiver.calls, 0);
    QCoreApplication::sendPostedEvents(&queuedReceiver, QEvent::MetaCall);
    QCOMPARE(queuedReceiver.calls, 1);
    QCOMPARE(queuedReceiver.lastSender, &sender);
}

void DeferredDeletionTest::connectToRetiredReceiverIsCut()
{
    DeferredDeletionSender sender;
    QPointer<DeferredDeletionReceiver> receiver = new DeferredDeletionReceiver();
    pdf::PDFDeferredDeletion::retire(receiver.data());

    const QMetaObject::Connection connection = pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, receiver.data(), &DeferredDeletionReceiver::onFired);
    QVERIFY(!connection);
    sender.fire();
    QCOMPARE(receiver->calls, 0);
    DeferredDeletionTestHelper::settle();
    QVERIFY(receiver.isNull());
}

void DeferredDeletionTest::retireMakesInertAtOnce()
{
    // After retire, before the event loop: the object lives, it is hidden, its timers stand, its actions and
    // shortcuts are disabled, its animations stand, its child dialog is closed; a second retire changes nothing
    QWidget window;
    window.resize(400, 300);
    QPointer<QWidget> root = new QWidget(&window);
    QTimer* timer = new QTimer(root);
    timer->start(100000);
    QAction* action = new QAction(QStringLiteral("Action"), root);
    QShortcut* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), root);
    QPropertyAnimation* animation = new QPropertyAnimation(root.data(), "minimumWidth", root);
    animation->setDuration(100000);
    animation->setStartValue(0);
    animation->setEndValue(100);
    QDialog* dialog = new QDialog(root);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    animation->start();
    dialog->open();
    QVERIFY(root->isVisible());
    QVERIFY(dialog->isVisible());

    pdf::PDFDeferredDeletion::retire(root.data());
    QVERIFY(root);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(root));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(timer));
    QVERIFY(!root->isVisible());
    QVERIFY(!timer->isActive());
    QVERIFY(!action->isEnabled());
    QVERIFY(!shortcut->isEnabled());
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    QVERIFY(!dialog->isVisible());
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(&window));

    pdf::PDFDeferredDeletion::retire(root.data());
    QVERIFY(root);

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::ownedPopupClosedOnRetire()
{
    // A dialog and a menu parented to the window, logically owned by a child of the window (RR2-H2): retire of the
    // owner closes them without their signals and deletes them later; the window stays
    QWidget window;
    window.resize(400, 300);
    QPointer<QWidget> owner = new QWidget(&window);
    QPointer<QDialog> dialog = new QDialog(&window);
    QPointer<QMenu> menu = new QMenu(&window);
    menu->addAction(QStringLiteral("Item"));
    pdf::PDFDeferredDeletion::setOwner(dialog.data(), owner.data());
    pdf::PDFDeferredDeletion::setOwner(menu.data(), owner.data());
    QCOMPARE(pdf::PDFDeferredDeletion::getOwner(dialog), owner.data());

    int finished = 0;
    QObject::connect(dialog.data(), &QDialog::finished, &window, [&finished]() { ++finished; });
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    dialog->open();
    menu->popup(window.mapToGlobal(QPoint(10, 10)));
    QVERIFY(dialog->isVisible());
    QVERIFY(menu->isVisible());

    pdf::PDFDeferredDeletion::retire(owner.data());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(dialog));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(menu));
    QVERIFY(!dialog->isVisible());
    QVERIFY(!menu->isVisible());
    QCOMPARE(finished, 0);
    QCOMPARE(dialog->result(), int(QDialog::Rejected));

    DeferredDeletionTestHelper::settle();
    QVERIFY(owner.isNull());
    QVERIFY(dialog.isNull());
    QVERIFY(menu.isNull());
}

void DeferredDeletionTest::overriddenRejectNotCalled()
{
    // The dialog is closed by the non-virtual QDialog::done: its refusing overrides do not run (RR2-H2)
    QWidget window;
    QPointer<QWidget> root = new QWidget(&window);
    RefusingDialog* dialog = new RefusingDialog(root);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    dialog->open();
    QVERIFY(dialog->isVisible());

    pdf::PDFDeferredDeletion::retire(root.data());
    QVERIFY(!dialog->isVisible());
    QCOMPARE(dialog->rejectCalls, 0);
    QCOMPARE(dialog->doneCalls, 0);
    QCOMPARE(dialog->result(), int(QDialog::Rejected));
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::notifierEmitsOnParentDeletion()
{
    // A child deleted with its parent without retire: the notifier emits from its destructor, exactly once (RR2-H3)
    QObject* parent = new QObject();
    QObject* child = new QObject(parent);
    pdf::PDFLifetimeNotifier* notifier = pdf::PDFDeferredDeletion::getNotifier(child);
    QVERIFY(notifier);
    QCOMPARE(pdf::PDFDeferredDeletion::getNotifier(child), notifier);
    QCOMPARE(notifier->getObject(), child);

    int emissions = 0;
    const void* argument = nullptr;
    QObject listener;
    QObject::connect(notifier, &pdf::PDFLifetimeNotifier::aboutToBeDeleted, &listener, [&emissions, &argument](QObject* object)
    {
        ++emissions;
        argument = object;
    });

    {
        pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
        delete parent;
    }
    QCOMPARE(emissions, 1);
    QCOMPARE(argument, static_cast<const void*>(child));
}

void DeferredDeletionTest::notifierEmitsOnceOnRetireThenDelete()
{
    // retire emits at once (the objects live), the deletion later emits nothing more
    QPointer<QObject> root = new QObject();
    QPointer<QObject> child = new QObject(root);
    int rootEmissions = 0;
    int childEmissions = 0;
    bool childAliveAtSignal = false;
    QObject listener;
    QObject::connect(pdf::PDFDeferredDeletion::getNotifier(root), &pdf::PDFLifetimeNotifier::aboutToBeDeleted, &listener, [&rootEmissions]() { ++rootEmissions; });
    QObject::connect(pdf::PDFDeferredDeletion::getNotifier(child), &pdf::PDFLifetimeNotifier::aboutToBeDeleted, &listener, [&childEmissions, &childAliveAtSignal, &child]()
    {
        ++childEmissions;
        childAliveAtSignal = !child.isNull();
    });

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(rootEmissions, 1);
    QCOMPARE(childEmissions, 1);
    QVERIFY(childAliveAtSignal);
    QVERIFY(root);

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
    QCOMPARE(rootEmissions, 1);
    QCOMPARE(childEmissions, 1);
}

void DeferredDeletionTest::listenerReentersRetire()
{
    // The flag is set before the signal (RR2-M1): a listener sees isRetired, its nested retire does nothing, the
    // hooks run once
    QPointer<RetirableWidget> root = new RetirableWidget();
    RetirableWidget* child = new RetirableWidget(root);
    bool isRetiredInListener = false;
    int emissions = 0;
    QObject listener;
    QObject::connect(pdf::PDFDeferredDeletion::getNotifier(root), &pdf::PDFLifetimeNotifier::aboutToBeDeleted, &listener, [&isRetiredInListener, &emissions, child](QObject* object)
    {
        ++emissions;
        isRetiredInListener = pdf::PDFDeferredDeletion::isRetired(object);
        pdf::PDFDeferredDeletion::retire(object);
        pdf::PDFDeferredDeletion::retire(child);
    });

    pdf::PDFDeferredDeletion::retire(root.data());
    QVERIFY(isRetiredInListener);
    QCOMPARE(emissions, 1);
    QCOMPARE(root->hookCalls, 1);
    QCOMPARE(child->hookCalls, 1);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::queuedByHideIsDropped()
{
    // A call queued by the hide of the subtree (step 7) is removed by step 8, it never reaches the object (and the
    // gate has nothing to drop)
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPointer<QWidget> root = new QWidget(&window);
    layout->addWidget(root);
    QVBoxLayout* rootLayout = new QVBoxLayout(root);
    HideQueuingWidget* queuing = new HideQueuingWidget(root);
    rootLayout->addWidget(queuing);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(queuing->isVisible());

    pdf::PDFDeferredDeletion::retire(root.data());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(queuing->queuedCalls, 0);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::MetaCall), 0);

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::focusOutCommitsBeforeRetire()
{
    // The focus leaves the subtree (step 5) while the editors are still connected inside it: the typed value is
    // committed by editingFinished (RR2-M2 b)
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPointer<QWidget> root = new QWidget(&window);
    layout->addWidget(root);
    QVBoxLayout* rootLayout = new QVBoxLayout(root);
    QLineEdit* edit = new QLineEdit(root);
    rootLayout->addWidget(edit);
    QLineEdit* outside = new QLineEdit(&window);
    layout->addWidget(outside);

    QString store;
    QObject::connect(edit, &QLineEdit::editingFinished, root.data(), [&store, edit]() { store = edit->text(); });
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    edit->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(edit->hasFocus());
    QTest::keyClicks(edit, QStringLiteral("abc"));
    QVERIFY(store.isEmpty());

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(store, QStringLiteral("abc"));
    QVERIFY(!edit->hasFocus());
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::retireFlushesPendingWrite()
{
    // An active timer marked by setFlushOnRetire fires once before it stops (RR2-M3); an unmarked one only stops
    QPointer<QObject> root = new QObject();
    QTimer* writeTimer = new QTimer(root);
    writeTimer->setSingleShot(true);
    QTimer* otherTimer = new QTimer(root);
    otherTimer->setSingleShot(true);
    int writes = 0;
    int otherCalls = 0;
    QObject::connect(writeTimer, &QTimer::timeout, root.data(), [&writes]() { ++writes; });
    QObject::connect(otherTimer, &QTimer::timeout, root.data(), [&otherCalls]() { ++otherCalls; });
    pdf::PDFDeferredDeletion::setFlushOnRetire(writeTimer);
    writeTimer->start(100000);
    otherTimer->start(100000);

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(writes, 1);
    QCOMPARE(otherCalls, 0);
    QVERIFY(!writeTimer->isActive());
    QVERIFY(!otherTimer->isActive());
    DeferredDeletionTestHelper::settle();
    QCOMPARE(writes, 1);
    QVERIFY(root.isNull());

    // An inactive marked timer has nothing to write
    QPointer<QObject> idleRoot = new QObject();
    QTimer* idleTimer = new QTimer(idleRoot);
    int idleWrites = 0;
    QObject::connect(idleTimer, &QTimer::timeout, idleRoot.data(), [&idleWrites]() { ++idleWrites; });
    pdf::PDFDeferredDeletion::setFlushOnRetire(idleTimer);
    pdf::PDFDeferredDeletion::retire(idleRoot.data());
    QCOMPARE(idleWrites, 0);
    DeferredDeletionTestHelper::settle();
}

void DeferredDeletionTest::watcherDisconnectedNotCanceled()
{
    // A future watcher of the subtree is only disconnected: the computation is not canceled (a write of a worker
    // finishes alone), its end reaches nobody
    QFutureInterface<int> futureInterface;
    futureInterface.reportStarted();
    QPointer<QObject> root = new QObject();
    QFutureWatcher<int>* watcher = new QFutureWatcher<int>(root);
    int finished = 0;
    QObject::connect(watcher, &QFutureWatcher<int>::finished, root.data(), [&finished]() { ++finished; });
    watcher->setFuture(futureInterface.future());

    pdf::PDFDeferredDeletion::retire(root.data());
    QVERIFY(!futureInterface.isCanceled());
    futureInterface.reportFinished();
    QCoreApplication::sendPostedEvents(watcher, QEvent::FutureCallOut);
    QCOMPARE(finished, 0);
    QVERIFY(!futureInterface.isCanceled());
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::destroyedStaysConnected()
{
    // QObject::destroyed is not disconnected (RR2-L8): a model inside with a view outside, a completer inside with
    // its line edit outside, the accessibility cache and a listener of the application forget the deleted objects
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPointer<QWidget> root = new QWidget(&window);
    layout->addWidget(root);
    QStringListModel* model = new QStringListModel(QStringList({ QStringLiteral("a"), QStringLiteral("b") }), root);
    QListView* view = new QListView(&window);
    layout->addWidget(view);
    view->setModel(model);
    QCompleter* completer = new QCompleter(QStringList({ QStringLiteral("alpha"), QStringLiteral("beta") }), root);
    QLineEdit* edit = new QLineEdit(&window);
    layout->addWidget(edit);
    edit->setCompleter(completer);
    QLabel* label = new QLabel(QStringLiteral("label"), root);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAccessibleInterface* accessibleInterface = QAccessible::queryAccessibleInterface(label);
    QVERIFY(accessibleInterface);
    const QAccessible::Id accessibleId = QAccessible::uniqueId(accessibleInterface);
    int modelDestroyed = 0;
    QObject::connect(model, &QObject::destroyed, &window, [&modelDestroyed]() { ++modelDestroyed; });
    const void* oldModel = model;

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(static_cast<const void*>(view->model()), oldModel);
    QCOMPARE(modelDestroyed, 0);

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
    QCOMPARE(modelDestroyed, 1);
    // The view returns to the empty model, model() is nullptr then (QAbstractItemView::model)
    QVERIFY(view->model() == nullptr);
    QVERIFY(QAccessible::accessibleInterface(accessibleId) == nullptr);
    QVERIFY(edit->completer() == nullptr);
    QTest::keyClicks(edit, QStringLiteral("al"));
    QCOMPARE(edit->text(), QStringLiteral("al"));
}

void DeferredDeletionTest::outgoingSignalsDisconnectedExceptDestroyed()
{
    QPointer<QObject> root = new QObject();
    QObject* child = new QObject(root);
    QObject listener;
    int nameChanges = 0;
    int destroyed = 0;
    QObject::connect(child, &QObject::objectNameChanged, &listener, [&nameChanges]() { ++nameChanges; });
    QObject::connect(child, &QObject::destroyed, &listener, [&destroyed]() { ++destroyed; });

    pdf::PDFDeferredDeletion::retire(root.data());
    child->setObjectName(QStringLiteral("renamed"));
    QCOMPARE(nameChanges, 0);
    QCOMPARE(destroyed, 0);
    DeferredDeletionTestHelper::settle();
    QCOMPARE(destroyed, 1);
}

void DeferredDeletionTest::filtersRemovedOnRetire()
{
    // A filter of the subtree on a foreign object and a foreign filter owned by the subtree (remembered at the
    // owner, RR2-M7) are removed at retire
    QObject target;
    QPointer<QObject> root = new QObject();
    EventCountingFilter* childFilter = new EventCountingFilter(root);
    EventCountingFilter ownedFilter;
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&target, childFilter));
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&target, &ownedFilter, root.data()));

    QEvent first(QEvent::User);
    QCoreApplication::sendEvent(&target, &first);
    QCOMPARE(childFilter->count, 1);
    QCOMPARE(ownedFilter.count, 1);

    pdf::PDFDeferredDeletion::retire(root.data());
    QEvent second(QEvent::User);
    QCoreApplication::sendEvent(&target, &second);
    QCOMPARE(childFilter->count, 1);
    QCOMPARE(ownedFilter.count, 1);

    // A retired owner installs nothing
    QVERIFY(!pdf::PDFDeferredDeletion::installEventFilter(&target, &ownedFilter, root.data()));
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::filterInstalledAgainRememberedOnce()
{
    // A filter installed again on the same target (a tooltip on every show, a popup anchor, a rebuilt tile) is
    // remembered once - Qt keeps it once too -, the entry of a deleted target or of a deleted filter is dropped at
    // the next install; retire still removes every remaining filter
    QObject target;
    QObject secondTarget;
    QPointer<QObject> root = new QObject();
    EventCountingFilter* filter = new EventCountingFilter(root);
    for (int i = 0; i < 100; ++i)
    {
        QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&target, filter));
    }
    QCOMPARE(pdf::PDFDeferredDeletion::getEventFilterCount(filter), 1);

    QEvent first(QEvent::User);
    QCoreApplication::sendEvent(&target, &first);
    QCOMPARE(filter->count, 1);

    // Another target is another pair; a deleted target leaves the list at the next install of its owner
    QObject* goneTarget = new QObject();
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(goneTarget, filter));
    QCOMPARE(pdf::PDFDeferredDeletion::getEventFilterCount(filter), 2);
    delete goneTarget;
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&secondTarget, filter));
    QCOMPARE(pdf::PDFDeferredDeletion::getEventFilterCount(filter), 2);

    // A deleted filter remembered at its owner leaves the list at the next install of the owner
    EventCountingFilter* goneFilter = new EventCountingFilter();
    EventCountingFilter ownedFilter;
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&target, goneFilter, root.data()));
    QCOMPARE(pdf::PDFDeferredDeletion::getEventFilterCount(root.data()), 1);
    delete goneFilter;
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&secondTarget, &ownedFilter, root.data()));
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(&secondTarget, &ownedFilter, root.data()));
    QCOMPARE(pdf::PDFDeferredDeletion::getEventFilterCount(root.data()), 1);

    QEvent second(QEvent::User);
    QCoreApplication::sendEvent(&secondTarget, &second);
    QCOMPARE(filter->count, 2);
    QCOMPARE(ownedFilter.count, 1);

    pdf::PDFDeferredDeletion::retire(root.data());
    QEvent third(QEvent::User);
    QCoreApplication::sendEvent(&target, &third);
    QCoreApplication::sendEvent(&secondTarget, &third);
    QCOMPARE(filter->count, 2);
    QCOMPARE(ownedFilter.count, 1);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::hooksCalledOnceInSubtreeAndOwned()
{
    // The hooks of the root, of a descendant and of a logically owned object (without a parent) run once each
    QPointer<RetirableObject> root = new RetirableObject();
    QObject* middle = new QObject(root);
    RetirableObject* descendant = new RetirableObject(middle);
    QPointer<RetirableObject> owned = new RetirableObject();
    pdf::PDFDeferredDeletion::setOwner(owned.data(), descendant);

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(root->hookCalls, 1);
    QCOMPARE(descendant->hookCalls, 1);
    QCOMPARE(owned->hookCalls, 1);
    QVERIFY(root->isRetireHookCalled());

    pdf::PDFDeferredDeletion::retire(owned.data());
    QCOMPARE(owned->hookCalls, 1);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
    QVERIFY(owned.isNull());
}

void DeferredDeletionTest::isRetiredFollowsParentAndOwner()
{
    QPointer<QObject> owner = new QObject();
    QPointer<QObject> owned = new QObject();
    QObject* ownedChild = new QObject(owned);
    QPointer<QObject> ownedOfOwned = new QObject();
    QPointer<QObject> unrelated = new QObject();
    pdf::PDFDeferredDeletion::setOwner(owned.data(), owner.data());
    pdf::PDFDeferredDeletion::setOwner(ownedOfOwned.data(), ownedChild);
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(ownedChild));
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(ownedOfOwned));

    // A cycle of owners by mistake ends the walk (no hang); a new owner replaces the previous one
    QPointer<QObject> cycleFirst = new QObject();
    QPointer<QObject> cycleSecond = new QObject();
    pdf::PDFDeferredDeletion::setOwner(cycleFirst.data(), cycleSecond.data());
    pdf::PDFDeferredDeletion::setOwner(cycleSecond.data(), cycleFirst.data());
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(cycleFirst));
    pdf::PDFDeferredDeletion::setOwner(unrelated.data(), owner.data());
    pdf::PDFDeferredDeletion::setOwner(unrelated.data(), nullptr);
    QVERIFY(pdf::PDFDeferredDeletion::getOwner(unrelated) == nullptr);

    pdf::PDFDeferredDeletion::retire(owner.data());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(owner));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(owned));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(ownedChild));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(ownedOfOwned));
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(unrelated));

    pdf::PDFDeferredDeletion::retire(cycleFirst.data());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(cycleFirst));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(cycleSecond));

    // A new owned object of a retired owner is retired at once (reported in the test mode)
    QPointer<QObject> late = new QObject();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::RETIRED_OWNER_MESSAGE)))));
    pdf::PDFDeferredDeletion::setOwner(late.data(), owner.data());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(late));

    DeferredDeletionTestHelper::settle();
    QVERIFY(owner.isNull());
    QVERIFY(owned.isNull());
    QVERIFY(ownedOfOwned.isNull());
    QVERIFY(late.isNull());
    QVERIFY(cycleFirst.isNull());
    QVERIFY(cycleSecond.isNull());
    QVERIFY(unrelated);
    delete unrelated.data();
}

void DeferredDeletionTest::retiredOwnerRetiresNewOwnedObject()
{
    // setOwner with an owner retired already: the object is retired at once - the full retire (its foreign
    // connections cut, hidden, deleted later), not only counted as retired through its owner - and the test mode
    // names both objects (a caller should have checked isRetired before it created the object)
    const QRegularExpression reported(QStringLiteral("^%1: object QWidget \"dialog\", owner QWidget \"owner\"$").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::RETIRED_OWNER_MESSAGE))));
    QPointer<QWidget> window = new QWidget();
    DeferredDeletionSender sender;
    QPointer<QWidget> owner = new QWidget();
    owner->setObjectName(QStringLiteral("owner"));
    pdf::PDFDeferredDeletion::retire(owner.data());

    // A dialog parented only to the window, connected to a foreign sender, then given the retired owner
    QPointer<QWidget> dialog = new QWidget(window);
    dialog->setObjectName(QStringLiteral("dialog"));
    window->show();
    QVERIFY(dialog->isVisible());
    QPointer<DeferredDeletionReceiver> receiver = new DeferredDeletionReceiver(dialog);
    pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, receiver.data(), &DeferredDeletionReceiver::onFired);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(receiver), 1);
    QTest::ignoreMessage(QtWarningMsg, reported);
    pdf::PDFDeferredDeletion::setOwner(dialog.data(), owner.data());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(dialog));
    QCOMPARE(pdf::PDFDeferredDeletion::getOwner(dialog), owner.data());
    QVERIFY(dialog->isHidden());
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(receiver), 0);
    sender.fire();
    QCOMPARE(receiver->calls, 0);

    // An owner retired through its parent counts too
    QPointer<QObject> retiredParent = new QObject();
    QObject* ownerChild = new QObject(retiredParent);
    pdf::PDFDeferredDeletion::retire(retiredParent.data());
    QPointer<DeferredDeletionReceiver> second = new DeferredDeletionReceiver();
    pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, second.data(), &DeferredDeletionReceiver::onFired);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::RETIRED_OWNER_MESSAGE)))));
    pdf::PDFDeferredDeletion::setOwner(second.data(), ownerChild);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(second));
    sender.fire();
    QCOMPARE(second->calls, 0);

    // Deleted later with the rest; the window stays
    DeferredDeletionTestHelper::settle();
    QVERIFY(dialog.isNull());
    QVERIFY(receiver.isNull());
    QVERIFY(owner.isNull());
    QVERIFY(second.isNull());
    QVERIFY(retiredParent.isNull());
    QVERIFY(window);
    delete window.data();
}

void DeferredDeletionTest::ownedEntriesOfDeletedObjectsPruned()
{
    // RA-L3: a long-lived owner of transient objects (a menu per context menu, a question per close) keeps no entry
    // of a deleted owned object - setOwner drops them; the live owned object is still retired with its owner
    QPointer<QObject> owner = new QObject();
    for (int i = 0; i < 8; ++i)
    {
        QObject* transient = new QObject();
        pdf::PDFDeferredDeletion::setOwner(transient, owner.data());
        QCOMPARE(pdf::PDFDeferredDeletion::getOwnedObjectCount(owner), 1);
        delete transient;
    }
    QPointer<QObject> live = new QObject();
    pdf::PDFDeferredDeletion::setOwner(live.data(), owner.data());
    QCOMPARE(pdf::PDFDeferredDeletion::getOwnedObjectCount(owner), 1);

    pdf::PDFDeferredDeletion::retire(owner.data());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(live));
    DeferredDeletionTestHelper::settle();
    QVERIFY(owner.isNull());
    QVERIFY(live.isNull());
}

void DeferredDeletionTest::objectMovedOutStaysWhole()
{
    // RA-M1: the steps 2-8 work on the snapshot of the subtree; an object, which a hook moves out of the retired
    // subtree (against the hook contract), is not retired any more and the later steps skip it - its outgoing
    // signals still reach their receivers, its timer runs, its action stays enabled, its queued call is delivered,
    // its own hook is not called and it is not deleted with the root
    QWidget foreign;
    DeferredDeletionReceiver receiver;
    QPointer<RetirableWidget> root = new RetirableWidget();
    QPointer<RetirableWidget> moved = new RetirableWidget(root);
    DeferredDeletionSender* movedSender = new DeferredDeletionSender(moved);
    QObject::connect(movedSender, &DeferredDeletionSender::fired, &receiver, &DeferredDeletionReceiver::onFired);
    QTimer* timer = new QTimer(moved);
    timer->start(100000);
    QAction* action = new QAction(moved);
    EventRecordingObject* queued = new EventRecordingObject(moved);
    QMetaObject::invokeMethod(queued, &EventRecordingObject::onQueued, Qt::QueuedConnection);
    root->hookAction = [&moved, &foreign]()
    {
        moved->setParent(&foreign);
    };

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(root->hookCalls, 1);
    QCOMPARE(moved->hookCalls, 0);
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(moved));
    movedSender->fire();
    QCOMPARE(receiver.calls, 1);
    QVERIFY(timer->isActive());
    QVERIFY(action->isEnabled());
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
    QVERIFY(moved);
    QCOMPARE(queued->queuedCalls, 1);

    // A listener of the notifier moves a child out before the incoming cut (step 3): the connection from a sender
    // outside stays, the hook of the child is not called
    DeferredDeletionSender outsideSender;
    QPointer<QWidget> secondRoot = new QWidget();
    QPointer<RetirableWidget> movedByListener = new RetirableWidget(secondRoot);
    DeferredDeletionReceiver* inner = new DeferredDeletionReceiver(movedByListener);
    QVERIFY(pdf::PDFDeferredDeletion::connect(&outsideSender, &DeferredDeletionSender::fired, inner, &DeferredDeletionReceiver::onFired));
    QObject listener;
    QObject::connect(pdf::PDFDeferredDeletion::getNotifier(secondRoot), &pdf::PDFLifetimeNotifier::aboutToBeDeleted, &listener, [&movedByListener, &foreign]()
    {
        movedByListener->setParent(&foreign);
    });
    pdf::PDFDeferredDeletion::retire(secondRoot.data());
    QCOMPARE(movedByListener->hookCalls, 0);
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(inner), 1);
    outsideSender.fire();
    QCOMPARE(inner->calls, 1);
    DeferredDeletionTestHelper::settle();
    QVERIFY(secondRoot.isNull());
    QVERIFY(movedByListener);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 0);

    // A hook only takes a child out of the tree to delete it later (no parent, no owner: the tools of the drawing
    // part, deleted inside their own call) - nobody alive holds it, it is still shut down: its signals reach nobody
    DeferredDeletionReceiver detachedReceiver;
    QPointer<RetirableWidget> thirdRoot = new RetirableWidget();
    QPointer<DeferredDeletionSender> detached = new DeferredDeletionSender(thirdRoot);
    QObject::connect(detached.data(), &DeferredDeletionSender::fired, &detachedReceiver, &DeferredDeletionReceiver::onFired);
    thirdRoot->hookAction = [&detached]()
    {
        detached->setParent(nullptr);
        detached->deleteLater();
    };
    pdf::PDFDeferredDeletion::retire(thirdRoot.data());
    QVERIFY(detached);
    detached->fire();
    QCOMPARE(detachedReceiver.calls, 0);
    DeferredDeletionTestHelper::settle();
    QVERIFY(thirdRoot.isNull());
    QVERIFY(detached.isNull());

    // The moved objects are whole objects of the foreign parent (deleted with it, deliberately synchronously)
    pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
    delete moved.data();
    delete movedByListener.data();
}

void DeferredDeletionTest::retireInsideRetire()
{
    // The hook contract (pdfdeferreddeletion.h, newapp_robustness.md 11 point 4): a hook may retire another object.
    // An unrelated object retired from the hook is retired fully inside it (its hook once, hidden, its incoming
    // connections cut), the outer retire ends its own steps, both are deleted from the event loop, nothing reported
    DeferredDeletionSender sender;
    QPointer<RetirableWidget> outer = new RetirableWidget();
    RetirableWidget* outerChild = new RetirableWidget(outer);
    DeferredDeletionReceiver* outerReceiver = new DeferredDeletionReceiver(outerChild);
    QVERIFY(pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, outerReceiver, &DeferredDeletionReceiver::onFired));
    QPointer<RetirableWidget> other = new RetirableWidget();
    DeferredDeletionReceiver* otherReceiver = new DeferredDeletionReceiver(other);
    QVERIFY(pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, otherReceiver, &DeferredDeletionReceiver::onFired));
    other->show();
    QVERIFY(other->isVisible());

    bool isOtherRetiredInHook = false;
    outer->hookAction = [&other, &isOtherRetiredInHook]()
    {
        pdf::PDFDeferredDeletion::retire(other.data());
        isOtherRetiredInHook = other && pdf::PDFDeferredDeletion::isRetired(other.data()) && other->hookCalls == 1 && !other->isVisible();
    };

    pdf::PDFDeferredDeletion::retire(outer.data());
    QVERIFY(isOtherRetiredInHook);
    QCOMPARE(outer->hookCalls, 1);
    QCOMPARE(outerChild->hookCalls, 1);
    QCOMPARE(other->hookCalls, 1);
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(&sender));
    sender.fire();
    QCOMPARE(outerReceiver->calls, 0);
    QCOMPARE(otherReceiver->calls, 0);

    DeferredDeletionTestHelper::settle();
    QVERIFY(outer.isNull());
    QVERIFY(other.isNull());
    QCOMPARE(pdf::PDFDeferredDeletion::getSynchronousDeletionCount(), 0);
}

void DeferredDeletionTest::focusMovesToNextOutsideSubtree()
{
    // RA-L6: a focus in the retired subtree, which its owner did not move, goes to the next widget of the focus chain
    // of the window outside the retired objects, which takes the focus by Tab - as QWidget::hide moved it before step
    // A (QWidgetPrivate::hide_helper -> focusNextPrevChild), a keyboard user keeps a focus. A retired widget, a
    // widget without Tab focus and a disabled one are skipped; without any the focus is cleared
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QLineEdit* before = new QLineEdit(&window);
    layout->addWidget(before);
    QPointer<QWidget> root = new QWidget(&window);
    layout->addWidget(root);
    QVBoxLayout* rootLayout = new QVBoxLayout(root);
    QLineEdit* inner = new QLineEdit(root);
    rootLayout->addWidget(inner);
    rootLayout->addWidget(new QLineEdit(root));
    layout->addWidget(new QLabel(QStringLiteral("No focus"), &window));
    QLineEdit* after = new QLineEdit(&window);
    layout->addWidget(after);
    QPointer<QWidget> second = new QWidget(&window);
    layout->addWidget(second);
    QVBoxLayout* secondLayout = new QVBoxLayout(second);
    QLineEdit* secondEdit = new QLineEdit(second);
    secondLayout->addWidget(secondEdit);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    inner->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(inner->hasFocus());

    pdf::PDFDeferredDeletion::retire(root.data());
    QVERIFY(after->hasFocus());
    QCOMPARE(QApplication::focusWidget(), after);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());

    // Nothing outside takes the focus by Tab: the focus is cleared
    before->setEnabled(false);
    after->setEnabled(false);
    secondEdit->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(secondEdit->hasFocus());
    pdf::PDFDeferredDeletion::retire(second.data());
    QVERIFY(!QApplication::focusWidget());
    QVERIFY(!window.focusWidget());
    DeferredDeletionTestHelper::settle();
    QVERIFY(second.isNull());
}

void DeferredDeletionTest::gateDropsBlockedEventsAndCounts()
{
    // Input, timer and queued call to a retired object are dropped by the gate, counted and reported in the test mode
    QPointer<QWidget> root = new QWidget();
    EventRecordingWidget* widget = new EventRecordingWidget(root);
    EventRecordingObject* object = new EventRecordingObject(root);
    pdf::PDFDeferredDeletion::retire(root.data());
    widget->received.clear();
    object->received.clear();

    // Six drops: the press, the shortcut override of the key (QApplication::notify asks the shortcut map first; the
    // override is ignored by default, so it propagates to the retired parent too: 2), the key, the timer, the call
    const QRegularExpression dropped(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::DROPPED_EVENT_MESSAGE))));
    for (int i = 0; i < 6; ++i)
    {
        QTest::ignoreMessage(QtWarningMsg, dropped);
    }

    QMouseEvent press = DeferredDeletionTestHelper::createMouseEvent(QEvent::MouseButtonPress);
    QVERIFY(QCoreApplication::sendEvent(widget, &press));
    QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    QCoreApplication::sendEvent(widget, &key);
    QTimerEvent timerEvent(1);
    QCoreApplication::sendEvent(object, &timerEvent);
    QMetaObject::invokeMethod(object, &EventRecordingObject::onQueued, Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(object, QEvent::MetaCall);

    QVERIFY(!widget->received.contains(QEvent::MouseButtonPress));
    QVERIFY(!widget->received.contains(QEvent::KeyPress));
    QVERIFY(!object->received.contains(QEvent::Timer));
    QCOMPARE(object->queuedCalls, 0);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::MouseButtonPress), 1);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::KeyPress), 1);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::Timer), 1);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::MetaCall), 1);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::ShortcutOverride), 2);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 6);

    // An object outside gets its events
    EventRecordingWidget outside;
    QMouseEvent outsidePress = DeferredDeletionTestHelper::createMouseEvent(QEvent::MouseButtonPress);
    QCoreApplication::sendEvent(&outside, &outsidePress);
    QVERIFY(outside.received.contains(QEvent::MouseButtonPress));
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 6);

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::gatePassesOtherEvents()
{
    // Everything outside the blocked list passes (RR3-H3: a list of blocked types, not of allowed ones)
    QPointer<QWidget> root = new QWidget();
    EventRecordingWidget* widget = new EventRecordingWidget(root);
    pdf::PDFDeferredDeletion::retire(root.data());
    widget->received.clear();

    const std::vector<QEvent::Type> types = { QEvent::FocusOut, QEvent::FocusIn, QEvent::LayoutRequest, QEvent::StyleChange, QEvent::FontChange,
                                              QEvent::Hide, QEvent::Close, QEvent::Polish, QEvent::User };
    for (QEvent::Type type : types)
    {
        QVERIFY(!pdf::PDFDeferredDeletion::isEventBlocked(type));
        if (type == QEvent::FocusOut || type == QEvent::FocusIn)
        {
            QFocusEvent event(type, Qt::OtherFocusReason);
            QCoreApplication::sendEvent(widget, &event);
        }
        else
        {
            QEvent event(type);
            QCoreApplication::sendEvent(widget, &event);
        }
        QVERIFY2(widget->received.contains(type), qPrintable(QStringLiteral("event type %1").arg(int(type))));
    }
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 0);
    DeferredDeletionTestHelper::settle();
}

void DeferredDeletionTest::focusOutCommitsDespiteGate()
{
    // An editor, which commits in its own focus out event, gets the focus out also while it waits for its deletion
    // behind the gate (RR3-H3)
    QString store;
    QPointer<QWidget> root = new QWidget();
    FocusOutCommitWidget* editor = new FocusOutCommitWidget(&store, root);
    editor->value = QStringLiteral("typed");
    pdf::PDFDeferredDeletion::retire(root.data());

    QFocusEvent focusOut(QEvent::FocusOut, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(editor, &focusOut);
    QCOMPARE(store, QStringLiteral("typed"));
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 0);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::childRemovedKeepsLayoutSane()
{
    // A child of a retired widget deleted before it: ChildRemoved passes the gate, the layout forgets the child
    // (the filter of the application runs before QLayout::widgetEvent)
    QPointer<QWidget> root = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(root);
    QLabel* first = new QLabel(QStringLiteral("first"), root);
    QLabel* second = new QLabel(QStringLiteral("second"), root);
    layout->addWidget(first);
    layout->addWidget(second);
    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(layout->count(), 2);

    {
        pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
        delete first;
    }
    QCOMPARE(layout->count(), 1);
    QCOMPARE(layout->itemAt(0)->widget(), second);
    layout->activate();
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 0);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::futureCallOutPassesGate()
{
    // The call out events of a retired watcher pass the gate: the throttling of the computation is released
    // (qfuturewatcher.cpp: a ResultsReady event delivered decreases the pending count)
    QFutureInterface<int> futureInterface;
    futureInterface.reportStarted();
    QPointer<QObject> root = new QObject();
    QFutureWatcher<int>* watcher = new QFutureWatcher<int>(root);
    watcher->setPendingResultsLimit(1);
    watcher->setFuture(futureInterface.future());
    QCoreApplication::sendPostedEvents(watcher, QEvent::FutureCallOut);

    pdf::PDFDeferredDeletion::retire(root.data());
    futureInterface.reportResult(1, 0);
    futureInterface.reportResult(2, 1);
    QVERIFY(futureInterface.isThrottled());
    QCoreApplication::sendPostedEvents(watcher, QEvent::FutureCallOut);
    QVERIFY(!futureInterface.isThrottled());
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 0);

    futureInterface.reportFinished();
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::retireStepsAreNotGated()
{
    // The gate is on only after step 8: an event, which a hook sends to its subtree (the release of a pressed
    // button, which ends a drag of an overlay), still arrives
    QPointer<RetirableWidget> root = new RetirableWidget();
    EventRecordingWidget* child = new EventRecordingWidget(root);
    root->hookAction = [child]()
    {
        QMouseEvent release = DeferredDeletionTestHelper::createMouseEvent(QEvent::MouseButtonRelease);
        QCoreApplication::sendEvent(child, &release);
    };

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(root->hookCalls, 1);
    QVERIFY(child->received.contains(QEvent::MouseButtonRelease));
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(), 0);
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::gateRunsBeforeApplicationFilters()
{
    // The gate runs before every filter of the application (RR3-L4, the four of the application: the context, the
    // search popup, the tooltip, the style): it is moved in front after each retire from the event loop (RA-L4: not
    // inside the retire, which may run inside the dispatch of the application filters), and at once when a filter is
    // installed on the application through PDFDeferredDeletion::installEventFilter
    QPointer<QObject> created = new QObject();
    pdf::PDFDeferredDeletion::retire(created.data());
    DeferredDeletionTestHelper::settle();
    QVERIFY(created.isNull());

    // Installed directly after the gate exists (as a test harness, the tooltip label of Qt)
    PressCountingFilter earlyFilter;
    PressCountingFilter lateFilter;
    QCoreApplication::instance()->installEventFilter(&earlyFilter);
    QCoreApplication::instance()->installEventFilter(&lateFilter);

    QPointer<QWidget> root = new QWidget();
    EventRecordingWidget* widget = new EventRecordingWidget(root);
    EventRecordingWidget outside;
    QObject owner;
    pdf::PDFDeferredDeletion::retire(root.data());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);

    const QRegularExpression dropped(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::DROPPED_EVENT_MESSAGE))));
    QTest::ignoreMessage(QtWarningMsg, dropped);
    QMouseEvent press = DeferredDeletionTestHelper::createMouseEvent(QEvent::MouseButtonPress);
    QCoreApplication::sendEvent(widget, &press);
    QCOMPARE(earlyFilter.count, 0);
    QCOMPARE(lateFilter.count, 0);

    // A filter installed on the application through the helper after the retire: the gate stays in front
    PressCountingFilter helperFilter;
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(QCoreApplication::instance(), &helperFilter, &owner));
    QTest::ignoreMessage(QtWarningMsg, dropped);
    QMouseEvent secondPress = DeferredDeletionTestHelper::createMouseEvent(QEvent::MouseButtonPress);
    QCoreApplication::sendEvent(widget, &secondPress);
    QCOMPARE(helperFilter.count, 0);
    QCOMPARE(pdf::PDFDeferredDeletion::getDroppedEventCount(QEvent::MouseButtonPress), 2);

    // The filters work for the objects, which are not retired
    QMouseEvent outsidePress = DeferredDeletionTestHelper::createMouseEvent(QEvent::MouseButtonPress);
    QCoreApplication::sendEvent(&outside, &outsidePress);
    QCOMPARE(earlyFilter.count, 1);
    QCOMPARE(lateFilter.count, 1);
    QCOMPARE(helperFilter.count, 1);

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

void DeferredDeletionTest::retireInsideApplicationFilterSkipsNoFilter()
{
    // RA-L4: Qt iterates the filters of the application by index (qcoreapplication.cpp, sendThroughApplication-
    // EventFilters); a filter, which retires an object while it filters an event (a press filter of a popup closes
    // the popup), removes its own entry (step 3, QObject::removeEventFilter clears it); every later filter still sees
    // the event - retire never reinstalls the gate inside the call, which would clean the list and shift it (the gate
    // moves in front from the event loop)
    QPointer<QObject> created = new QObject();
    pdf::PDFDeferredDeletion::retire(created.data());
    DeferredDeletionTestHelper::settle();
    QVERIFY(created.isNull());

    EventCountingFilter first;
    EventCountingFilter last;
    QObject firstOwner;
    QObject lastOwner;
    QPointer<QObject> popup = new QObject();
    RetiringFilter retiring(popup.data());
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(QCoreApplication::instance(), &last, &lastOwner));
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(QCoreApplication::instance(), &retiring, popup.data()));
    QVERIFY(pdf::PDFDeferredDeletion::installEventFilter(QCoreApplication::instance(), &first, &firstOwner));

    // The order: the gate, first, retiring, last
    QObject target;
    QEvent event(QEvent::User);
    QCoreApplication::sendEvent(&target, &event);
    QCOMPARE(first.count, 1);
    QCOMPARE(retiring.count, 1);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(popup));
    QCOMPARE(last.count, 1);

    // The retiring filter is removed, the others stay
    QEvent secondEvent(QEvent::User);
    QCoreApplication::sendEvent(&target, &secondEvent);
    QCOMPARE(first.count, 2);
    QCOMPARE(retiring.count, 1);
    QCOMPARE(last.count, 2);
    DeferredDeletionTestHelper::settle();
    QVERIFY(popup.isNull());
}

void DeferredDeletionTest::tokenReleasedOnceOnRetire()
{
    int releases = 0;
    QPointer<QObject> root = new QObject();
    QObject* child = new QObject(root);
    CountingToken* token = new CountingToken(child, &releases);

    pdf::PDFDeferredDeletion::retire(root.data());
    QCOMPARE(releases, 1);
    QVERIFY(token->isReleased());
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
    QCOMPARE(releases, 1);
}

void DeferredDeletionTest::tokenReleasedOnDestructionWithoutRetire()
{
    int releases = 0;
    QObject* parent = new QObject();
    new CountingToken(parent, &releases);
    delete parent;
    QCOMPARE(releases, 1);

    // Released early: the destruction releases nothing more
    int earlyReleases = 0;
    QObject* earlyParent = new QObject();
    CountingToken* earlyToken = new CountingToken(earlyParent, &earlyReleases);
    earlyToken->releaseToken();
    earlyToken->releaseToken();
    QCOMPARE(earlyReleases, 1);
    delete earlyParent;
    QCOMPARE(earlyReleases, 1);
}

void DeferredDeletionTest::synchronousDeletionReported()
{
    // A retirable deleted synchronously without retire (directly, and with its parent) is reported (RR-H3)
    const QRegularExpression message(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::SYNCHRONOUS_DELETION_MESSAGE))));
    RetirableObject* object = new RetirableObject();
    QTest::ignoreMessage(QtCriticalMsg, message);
    delete object;

    QObject* parent = new QObject();
    new RetirableObject(parent);
    QTest::ignoreMessage(QtCriticalMsg, message);
    delete parent;
    QCOMPARE(pdf::PDFDeferredDeletion::getSynchronousDeletionCount(), 2);

    // Not reported (the default until the packages convert the application), but counted
    pdf::PDFDeferredDeletion::setSynchronousDeletionReported(false);
    delete new RetirableObject();
    QCOMPARE(pdf::PDFDeferredDeletion::getSynchronousDeletionCount(), 3);
}

void DeferredDeletionTest::deferredDeletionWithoutRetireReported()
{
    // deleteLater is not retire: a retirable deleted by the event loop without retire is reported too
    const QRegularExpression message(QStringLiteral("^%1").arg(QRegularExpression::escape(QString::fromLatin1(pdf::PDFDeferredDeletion::SYNCHRONOUS_DELETION_MESSAGE))));
    QPointer<RetirableObject> object = new RetirableObject();
    object->deleteLater();
    QTest::ignoreMessage(QtCriticalMsg, message);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(object.isNull());
}

void DeferredDeletionTest::deletionAfterRetireNotReported()
{
    // A retired root, a retirable child of a retired parent and a retirable owned by a retired owner are quiet
    // (init: the test fails on the message)
    QPointer<RetirableObject> root = new RetirableObject();
    new RetirableObject(root);
    QPointer<QObject> parent = new QObject();
    QObject* middle = new QObject(parent);
    new RetirableObject(middle);
    QPointer<RetirableObject> owned = new RetirableObject();
    pdf::PDFDeferredDeletion::setOwner(owned.data(), parent.data());

    pdf::PDFDeferredDeletion::retire(root.data());
    pdf::PDFDeferredDeletion::retire(parent.data());
    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
    QVERIFY(parent.isNull());
    QVERIFY(owned.isNull());
    QCOMPARE(pdf::PDFDeferredDeletion::getSynchronousDeletionCount(), 0);
}

void DeferredDeletionTest::lateChildOfRetiredRootNotReported()
{
    // RA-L5: the notifier of a retired root is an early child (a listener asked for it); a hook creates a retirable
    // child after it (outside the snapshot - its hook is never called). The parent deletes its children in their
    // order (~QWidget, ~QObject): the late child is destroyed after the notifier and is still in a retired subtree -
    // the detector is quiet (init: the test fails on the message); a widget root and a plain object root
    QPointer<RetirableWidget> widgetRoot = new RetirableWidget();
    QVERIFY(pdf::PDFDeferredDeletion::getNotifier(widgetRoot));
    QPointer<RetirableObject> widgetChild;
    widgetRoot->hookAction = [&widgetRoot, &widgetChild]()
    {
        widgetChild = new RetirableObject(widgetRoot.data());
    };
    QPointer<RetirableObject> objectRoot = new RetirableObject();
    QVERIFY(pdf::PDFDeferredDeletion::getNotifier(objectRoot));
    QPointer<RetirableObject> objectChild;
    objectRoot->hookAction = [&objectRoot, &objectChild]()
    {
        objectChild = new RetirableObject(objectRoot.data());
    };

    pdf::PDFDeferredDeletion::retire(widgetRoot.data());
    pdf::PDFDeferredDeletion::retire(objectRoot.data());
    QVERIFY(widgetChild && objectChild);
    QVERIFY(!widgetChild->isRetireHookCalled());
    QVERIFY(!objectChild->isRetireHookCalled());
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(widgetChild));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(objectChild));

    DeferredDeletionTestHelper::settle();
    QVERIFY(widgetRoot.isNull());
    QVERIFY(widgetChild.isNull());
    QVERIFY(objectRoot.isNull());
    QVERIFY(objectChild.isNull());
    QCOMPARE(pdf::PDFDeferredDeletion::getSynchronousDeletionCount(), 0);

    // The roots are forgotten after their deletion: a new object is not retired
    QObject fresh;
    QVERIFY(!pdf::PDFDeferredDeletion::isRetired(&fresh));
}

void DeferredDeletionTest::deletionInsideScopeOrFlushNotReported()
{
    // A deliberate synchronous deletion (scope) and flush are quiet (init: the test fails on the message)
    RetirableObject* object = new RetirableObject();
    {
        pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
        QVERIFY(pdf::PDFDeferredDeletion::isSynchronousDeletionAllowed());
        delete object;
    }
    QVERIFY(!pdf::PDFDeferredDeletion::isSynchronousDeletionAllowed());

    QPointer<RetirableObject> later = new RetirableObject();
    later->deleteLater();
    pdf::PDFDeferredDeletion::flush();
    QVERIFY(later.isNull());
    QVERIFY(!pdf::PDFDeferredDeletion::isSynchronousDeletionAllowed());
}

void DeferredDeletionTest::flushRefusedInsideEventLoop()
{
    // flush only at the loop level 0: inside an event loop it deletes nothing
    QPointer<QObject> object;
    bool isAliveAfterFlush = false;
    QEventLoop loop;
    QTimer::singleShot(0, &loop, [&object, &isAliveAfterFlush, &loop]()
    {
        object = new QObject();
        object->deleteLater();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^PDFDeferredDeletion::flush was called inside an event loop")));
        pdf::PDFDeferredDeletion::flush();
        isAliveAfterFlush = !object.isNull();
        loop.quit();
    });
    loop.exec();
    QVERIFY(isAliveAfterFlush);
    pdf::PDFDeferredDeletion::flush();
    QVERIFY(object.isNull());
}

void DeferredDeletionTest::retireCostOfLargeSubtree()
{
    // The cost of retire of a subtree of ~2,000 objects (12.4: milliseconds): widgets in layouts, timers, actions,
    // shortcuts, connections from a sender outside; all of it inert at once
    QWidget window;
    window.resize(800, 600);
    QVBoxLayout* windowLayout = new QVBoxLayout(&window);
    QPointer<QWidget> root = new QWidget(&window);
    windowLayout->addWidget(root);
    QVBoxLayout* rootLayout = new QVBoxLayout(root);
    DeferredDeletionSender sender;
    std::vector<QTimer*> timers;
    std::vector<QAction*> actions;
    for (int group = 0; group < 40; ++group)
    {
        QWidget* groupWidget = new QWidget(root);
        rootLayout->addWidget(groupWidget);
        QVBoxLayout* groupLayout = new QVBoxLayout(groupWidget);
        for (int i = 0; i < 10; ++i)
        {
            groupLayout->addWidget(new QLabel(QStringLiteral("Label %1").arg(i), groupWidget));
            groupLayout->addWidget(new QLineEdit(groupWidget));
            QPushButton* button = new QPushButton(QStringLiteral("Button %1").arg(i), groupWidget);
            groupLayout->addWidget(button);
            DeferredDeletionReceiver* receiver = new DeferredDeletionReceiver(button);
            pdf::PDFDeferredDeletion::connect(&sender, &DeferredDeletionSender::fired, receiver, &DeferredDeletionReceiver::onFired);
        }
        for (int i = 0; i < 4; ++i)
        {
            QTimer* timer = new QTimer(groupWidget);
            timer->start(100000);
            timers.push_back(timer);
            QAction* action = new QAction(QStringLiteral("Action %1").arg(i), groupWidget);
            actions.push_back(action);
        }
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), groupWidget);
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const qsizetype objectCount = root->findChildren<QObject*>().size() + 1;
    QVERIFY2(objectCount >= 2000, qPrintable(QStringLiteral("%1 objects").arg(objectCount)));

    QElapsedTimer clock;
    clock.start();
    pdf::PDFDeferredDeletion::retire(root.data());
    const qint64 elapsed = clock.nsecsElapsed();
    qInfo("retire of %lld objects took %.3f ms", qlonglong(objectCount), double(elapsed) / 1.0e6);

    QVERIFY(!root->isVisible());
    for (QTimer* timer : timers)
    {
        QVERIFY(!timer->isActive());
    }
    for (QAction* action : actions)
    {
        QVERIFY(!action->isEnabled());
    }
    sender.fire();
    for (DeferredDeletionReceiver* receiver : root->findChildren<DeferredDeletionReceiver*>())
    {
        QCOMPARE(receiver->calls, 0);
    }
    QVERIFY2(elapsed < qint64(1000) * 1000 * 1000 * DeferredDeletionTestHelper::getTimeoutFactor(), "retire of the subtree took more than a second");

    DeferredDeletionTestHelper::settle();
    QVERIFY(root.isNull());
}

QTEST_MAIN(DeferredDeletionTest)

#include "tst_deferreddeletiontest.moc"
