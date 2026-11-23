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

#ifndef PDFDEFERREDDELETION_H
#define PDFDEFERREDDELETION_H

#include "pdffluentglobal.h"

#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <vector>

class QTimer;

namespace pdf
{

class PDFDeferredDeletion;
class PDFDeferredDeletionHelper;

/// Tells that an object is closed (step A of the robustness plan, newapp_robustness.md 12-16). The notifier is a
/// child of the watched object, created on demand by PDFDeferredDeletion::getNotifier. It emits aboutToBeDeleted
/// exactly once: from PDFDeferredDeletion::retire of the object (or of an ancestor, or of its logical owner) -
/// before the object is disconnected and long before it is deleted -, otherwise from its own destructor, when the
/// object is deleted without retire (a child deleted with its parent, a synchronous delete). A listener only forgets
/// the pointer, removes the object from its lists and ends what waits for it; it never calls the object and it never
/// moves an object out of the retired subtree. In the fallback emission the object is in its destructor: for a widget
/// it runs inside ~QWidget, before ~QObject clears the QPointers, so a QPointer to the object may still be set - never
/// dereference it, compare it only. The connections to QObject::destroyed stay as they are. GUI thread only; never
/// deleted by hand.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFLifetimeNotifier : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    virtual ~PDFLifetimeNotifier() override;

    /// The watched object (an identity: compare it, do not call it after the signal)
    QObject* getObject() const { return m_object; }

    /// aboutToBeDeleted has been emitted
    bool isEmitted() const { return m_isEmitted; }

Q_SIGNALS:
    /// The watched object is retired (and deleted later), or it is being deleted
    void aboutToBeDeleted(QObject* object);

private:
    friend class PDFDeferredDeletion;
    friend class PDFDeferredDeletionHelper;

    explicit PDFLifetimeNotifier(QObject* object);

    /// Emits aboutToBeDeleted, unless it has been emitted (the flag first, the emission is the last call)
    void emitOnce();

    QObject* m_object = nullptr;
    bool m_isEmitted = false;
};

/// A hook of an object with work outside its own subtree, which the generic steps of PDFDeferredDeletion::retire
/// cannot see: an overlay on a canvas, a document operation, a worker, a registration in a foreign object. Most
/// objects need none. The hook is called exactly once, by retire of the object, of an ancestor or of its logical
/// owner (step 4), after the incoming connections were cut and before the focus leaves and the object is disconnected,
/// hidden and deleted later. Its contract: it ends the outside work synchronously; it emits none of the object's own
/// signals (foreign signals it causes - the end of an operation, a write of a setting - are allowed); it opens no
/// dialog and waits for nothing; it may call PDFDeferredDeletion::retire of another object (nested retire is allowed).
/// It never moves an object out of the retired subtree: what an owner lent to the object (a banner, a part) is given
/// back by the owner before retire (PDFAppTab::setPage takes the storage banner back before it retires the page). An
/// object moved anyway (by a hook, a token release or a listener) to a parent or an owner outside the subtree is
/// skipped by the later steps: it stays whole - connected, shown, its timers running - and it is not deleted; an
/// object only taken out of the tree (setParent(nullptr), no logical owner - deleted later by the code, which took it
/// out) is still shut down. An override calls the hook of its base class (BaseClass::retire()) first.
///
/// The class passes its QObject to the constructor (PDFRetirable(this) after the QObject base in the list of the
/// bases): it registers the hook and lets the synchronous deletion detector name the object. Detector: an object with
/// a hook deleted while neither it nor an ancestor nor its owner is retired, and no flush / SynchronousDeletionScope
/// runs, is counted and - when PDFDeferredDeletion::setSynchronousDeletionReported is on - reported by qCritical
/// (PDFDeferredDeletion::SYNCHRONOUS_DELETION_MESSAGE; the tests fail on it).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFRetirable
{
public:
    /// \param object The object of the hook (the QObject base of the class, already constructed)
    explicit PDFRetirable(QObject* object);
    virtual ~PDFRetirable();

    PDFRetirable(const PDFRetirable&) = delete;
    PDFRetirable& operator=(const PDFRetirable&) = delete;

    /// The hook has been called (the object is retired)
    bool isRetireHookCalled() const { return m_isRetireHookCalled; }

protected:
    /// The hook (see the class); called only by PDFDeferredDeletion::retire, never directly
    virtual void retire() = 0;

private:
    friend class PDFDeferredDeletion;
    friend class PDFDeferredDeletionHelper;

    QObject* m_retirableObject = nullptr;
    bool m_isRetireHookCalled = false;
};

/// A registration or a possession outside the subtree of its owner (14.4, 15.7): an input / draw interface of a
/// canvas, a cursor on a foreign widget, a mouse / keyboard grab, a running drag, a shown tooltip, an entry in a
/// registry. The token is a child of the registering object; release() ends the registration. It is called exactly
/// once: by PDFDeferredDeletion::retire of the owner (step 4, after the hooks), by releaseToken(), or by the
/// destruction of the token. The destructor of a base class cannot call an override, so a concrete token calls
/// releaseToken() in its own destructor (the base reports an unreleased token by qWarning). release() gives back
/// what is outside the subtree; like a hook it never moves an object out of the retired subtree. GUI thread only.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFLifetimeToken : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// \param owner The registering object (the parent of the token)
    explicit PDFLifetimeToken(QObject* owner);
    virtual ~PDFLifetimeToken() override;

    /// Ends the registration now (release(), once); nothing when it has ended already
    void releaseToken();

    /// The registration has ended
    bool isReleased() const { return m_isReleased; }

protected:
    /// Ends the registration (called exactly once, by releaseToken)
    virtual void release() = 0;

private:
    bool m_isReleased = false;
};

/// The connection scope of an object (a child created on demand): the handles of the connections whose sender is
/// outside the object's subtree (PDFDeferredDeletion::connect), the event filters installed on foreign objects
/// (PDFDeferredDeletion::installEventFilter, remembered at the owner of the filter), the logically owned objects
/// (setOwner), the logical owner of the object itself and the flush-on-retire mark of a timer. Created and read only
/// by PDFDeferredDeletion; it is a QObject so that it dies with its object. GUI thread only.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFConnectionScope : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    virtual ~PDFConnectionScope() override;

    /// The object of the scope (its parent)
    QObject* getObject() const { return m_object; }

    /// Number of the stored connections, which are still connected
    int getConnectionCount() const;

    /// Number of the remembered event filters (each (target, filter) pair once)
    int getEventFilterCount() const { return int(m_eventFilters.size()); }

private:
    friend class PDFDeferredDeletion;
    friend class PDFDeferredDeletionHelper;

    explicit PDFConnectionScope(QObject* object);

    /// A filter installed on a foreign object
    struct EventFilter
    {
        QPointer<QObject> target;
        QPointer<QObject> filter;
    };

    /// Stores a handle; the stale (disconnected) handles are dropped from time to time
    void addConnection(const QMetaObject::Connection& connection);

    /// Disconnects all stored handles (step 3)
    void disconnectAll();

    /// Remembers a filter installed on a foreign object: an identical (target, filter) pair is kept once (a filter
    /// is installed again on every show of a tooltip / popup), the entries whose target or filter is gone are dropped
    void addEventFilter(QObject* target, QObject* filter);

    /// Removes the remembered filters (step 3)
    void removeEventFilters();

    QObject* m_object = nullptr;
    std::vector<QMetaObject::Connection> m_connections;
    size_t m_pruneLimit = 32;
    std::vector<EventFilter> m_eventFilters;
    std::vector<QPointer<QObject>> m_ownedObjects;
    QPointer<QObject> m_owner;
    bool m_isFlushOnRetire = false;
};

/// Deferred deletion at one place (step A, newapp_robustness.md 12, 13, 14.4-14.5, 15.7, 16): a tab, a page, a tool
/// shell / GUI / runner / overlay / part, a panel, a popup and a rebuilt row are closed only by retire. The object
/// becomes inert at once - nothing reaches it, it sends nothing, it shows nothing - and it is deleted later by the
/// rule of Qt (deleteLater), when no code of it is on the stack (A0: no nested event loop in App / Fluent).
///
/// retire(object) - the order is binding (p2a_common.md):
/// 0. already retired (the object, an ancestor or its logical owner) -> nothing; nested retire from a hook or a
///    listener is allowed;
/// 1. the retired flag of the object and of its logically owned objects (setOwner, with their subtrees);
/// 2. PDFLifetimeNotifier::aboutToBeDeleted of every notifier of the subtree (a snapshot of QPointers), each once;
/// 3. incoming cut: the stored connection handles of every connection scope of the subtree are disconnected, the
///    event filters installed through installEventFilter by an object of the subtree are removed;
/// 4. hooks PDFRetirable::retire (each once), then PDFLifetimeToken::releaseToken of the tokens of the subtree;
/// 5. the focus leaves the subtree (an owner, which moves it elsewhere, does it before retire or in a hook;
///    otherwise it goes to the next widget of the focus chain of the window outside the retired objects, which takes
///    the focus by Tab, as QWidget::hide moves it; without one clearFocus) - the editors commit through their
///    internal connections;
/// 6. generic shutdown of every object of the subtree: an active timer marked by setFlushOnRetire fires its timeout
///    synchronously; the outgoing signals except QObject::destroyed are disconnected one by one (QMetaMethod, never
///    a wildcard disconnect); a visible dialog is closed by the non-virtual QDialog::done(QDialog::Rejected) (an
///    override of reject / done is not called, its signals reach nobody); a visible menu is closed; timers stop;
///    actions and shortcuts are disabled; animations stop; a QFutureWatcher is only disconnected (a hook cancels a
///    pure computation);
/// 7. hide() of the object, of its owned objects and of the visible windows of the subtree;
/// 8. QCoreApplication::removePostedEvents(object, QEvent::MetaCall) of every object of the subtree;
/// 9. the event gate is on for the retired roots, then deleteLater() of the object and of its owned roots.
/// The steps 2-8 work on the snapshot of the subtree taken before step 1; an object, which a listener, a hook or a
/// token release has moved to a parent or an owner outside the retired subtree (against their contract), is not
/// retired any more and the later steps skip it; an object only taken out of the tree (no parent, no owner) is still
/// shut down.
///
/// The event gate is an event filter of the application, installed once (by the first filter installed on the
/// application through installEventFilter, or by the first retire) and kept in front of the other filters (it runs
/// first): installEventFilter on the application moves it in front at once, retire moves it in front from the event
/// loop. retire itself never reorders the filters of the application: Qt iterates them by index
/// (qcoreapplication.cpp), so a reinstall inside an application filter, which retires an object, would make a later
/// filter miss the event (RA-L4). Until that queued call a filter, which was installed directly (QObject::
/// installEventFilter: a test harness, the tooltip label of Qt) after the gate, runs before it. While a retired object
/// waits for its deletion it drops the blocked types for it and its descendants (isEventBlocked: input, shortcuts,
/// Timer, MetaCall, Paint / UpdateRequest, Show; everything else passes - ChildRemoved, Hide, FocusOut,
/// DeferredDelete, FutureCallOut...) and counts them per type; in the test mode it reports each one by qWarning.
/// With nothing retired it costs one virtual call per event. It does not see direct calls of slots (the connection
/// handles cut them), native messages and the queries of accessibility.
///
/// Connections: a connection whose sender is neither the receiver nor its descendant goes through connect (plain
/// QObject::connect - same overloads, queues, Qt::UniqueConnection, sender(), targeted disconnect - plus the handle
/// stored at the receiver's scope). A filter on a foreign object goes through installEventFilter, an object without
/// a parent or parented to the window gets setOwner, a write on a timer gets setFlushOnRetire.
///
/// flush() and SynchronousDeletionScope are the only deliberate synchronous deletions: main.cpp after the window is
/// reset, the destructors of test fixtures (loop level 0 only). Everything is GUI thread only (Q_ASSERT + qWarning).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFDeferredDeletion
{
public:
    PDFDeferredDeletion() = delete;

    /// Message of the synchronous deletion detector (qCritical; a test fails on it by QTest::failOnWarning)
    static constexpr const char* SYNCHRONOUS_DELETION_MESSAGE = "synchronous deletion of a retirable";

    /// Message prefix of an event dropped by the gate in the test mode (qWarning)
    static constexpr const char* DROPPED_EVENT_MESSAGE = "PDFDeferredDeletion: an event of a retired object was dropped";

    /// Message prefix of setOwner with an owner retired already in the test mode (qWarning): the caller should
    /// have checked isRetired before it created or connected the object
    static constexpr const char* RETIRED_OWNER_MESSAGE = "PDFDeferredDeletion: an object was given a retired owner";

    /// The notifier of \p object (created on demand as its child); connect to aboutToBeDeleted. For an object, which
    /// is retired already, the notifier has emitted (or emits from its destructor only, when it is new): check
    /// isRetired before connecting. nullptr for nullptr or outside of the GUI thread.
    static PDFLifetimeNotifier* getNotifier(QObject* object);

    /// Retires \p object (steps 0-9 above). Reentrant; nothing for nullptr, a retired object or outside of the GUI
    /// thread.
    static void retire(QObject* object);

    /// Retires \p object from the event loop (a queued call) and hides it now, when it is a widget: the close of an
    /// object from a slot of a signal of its own subtree (a button inside its click, a bar inside its close, a dialog
    /// inside its answer). retire now would cut the outgoing signals of the subtree in the middle of the emission -
    /// its later receivers would never run (step A, lesson 1b). Until the queued call the object is not retired (a
    /// retire of it, of an ancestor or of its owner meanwhile makes the queued call nothing); nothing for nullptr or
    /// outside of the GUI thread.
    static void retireLater(QObject* object);

    /// \p object, an ancestor of it or its logical owner (and their ancestors) is retired. GUI thread only.
    static bool isRetired(const QObject* object);

    /// A connection from a sender outside the receiver's subtree: QObject::connect with the same arguments (every
    /// overload of QObject::connect with a receiver / context object: member slot, string based, QMetaMethod), the
    /// handle is stored at the connection scope of \p receiver and disconnected by retire of the receiver (step 3).
    /// \returns the connection; an invalid one when QObject::connect fails (Qt::UniqueConnection of an existing one)
    /// or when the receiver is retired already (it is disconnected at once).
    template<typename Sender, typename Signal, typename Receiver, typename Slot>
    static QMetaObject::Connection connect(const Sender* sender, Signal signal, const Receiver* receiver, Slot slot, Qt::ConnectionType type = Qt::AutoConnection)
    {
        QMetaObject::Connection connection = QObject::connect(sender, signal, receiver, slot, type);
        addConnection(receiver, connection);
        return connection;
    }

    /// Stores the handle of a connection made elsewhere at the scope of \p receiver (as connect does). A retired
    /// receiver: the connection is disconnected at once.
    static void addConnection(const QObject* receiver, QMetaObject::Connection& connection);

    /// Number of the stored connections of \p receiver, which are still connected (tests, diagnostics)
    static int getConnectionCount(const QObject* receiver);

    /// Installs \p filter on \p target (a foreign object, also the application) and remembers it at \p owner (nullptr
    /// = the filter itself): retire of the owner removes it (step 3). A filter installed on the application keeps the
    /// gate in front of it. \returns false (nothing installed) for nullptr, a retired owner or outside of the GUI
    /// thread.
    static bool installEventFilter(QObject* target, QObject* filter, QObject* owner = nullptr);

    /// Number of the event filters remembered at \p owner (tests, diagnostics): a (target, filter) pair installed
    /// again is remembered once, a pair whose target or filter is gone is dropped at the next install
    static int getEventFilterCount(const QObject* owner);

    /// \p object (a dialog, a popup or a menu parented to the window, an object without a parent: a part holder, a
    /// part, a canvas, an overlay, a tool action) belongs logically to \p owner: retire of the owner retires it with
    /// the subtree of the owner (steps 1-9, it is deleted later too) and isRetired of it follows the owner. nullptr
    /// owner = the object has no logical owner. An owner retired already (itself, an ancestor or its owner chain)
    /// retires the object at once (the full retire: its connections cut, hidden, deleted later; in the test mode a
    /// qWarning RETIRED_OWNER_MESSAGE names both). A logical owner deleted without retire deletes its owned objects
    /// later (deleteLater).
    static void setOwner(QObject* object, QObject* owner);

    /// The logical owner of \p object (nullptr = none)
    static QObject* getOwner(const QObject* object);

    /// Number of the entries of the logically owned objects remembered at \p owner (tests, diagnostics): the entries
    /// of the deleted objects are dropped at the next setOwner of the owner
    static int getOwnedObjectCount(const QObject* owner);

    /// \p timer delays a write (a commit, a store): retire fires its timeout synchronously, when it is active, before
    /// it is stopped (step 6), so the write is not lost. The timer must be in the subtree of the retired object.
    static void setFlushOnRetire(QTimer* timer, bool isFlushed = true);

    /// The deliberate synchronous deletion of the retired objects: sends the posted DeferredDelete events (inside a
    /// SynchronousDeletionScope). Only at the event loop level 0 - main.cpp after the window is reset, the
    /// destructors of test fixtures; refused by qWarning inside an event loop.
    static void flush();

    /// A deliberate synchronous deletion (RAII): the detector is quiet while a scope exists - main.cpp at the end,
    /// the destructors of test fixtures, a synchronous delete in the body of a test
    class SynchronousDeletionScope
    {
    public:
        SynchronousDeletionScope() { enterSynchronousDeletion(); }
        ~SynchronousDeletionScope() { leaveSynchronousDeletion(); }

        SynchronousDeletionScope(const SynchronousDeletionScope&) = delete;
        SynchronousDeletionScope& operator=(const SynchronousDeletionScope&) = delete;
    };

    /// A flush or a SynchronousDeletionScope runs
    static bool isSynchronousDeletionAllowed();

    /// The detector reports each synchronous deletion of a retirable by qCritical (SYNCHRONOUS_DELETION_MESSAGE).
    /// Off by default until the packages of step A have converted the deletions of the application (the lifetime
    /// sweeps of today collect every critical message as an error); the tests of the packages turn it on.
    static void setSynchronousDeletionReported(bool isReported);
    static bool isSynchronousDeletionReported();

    /// Number of the synchronous deletions of retirables found by the detector (counted also while they are not
    /// reported), since the start or resetSynchronousDeletionCount
    static qint64 getSynchronousDeletionCount();
    static void resetSynchronousDeletionCount();

    // ------------------------------------------------------------- the event gate

    /// The gate drops an event of this type for a retired object
    static bool isEventBlocked(QEvent::Type type);

    /// Number of the events of \p type dropped by the gate (since the start or resetDroppedEventCounts)
    static qint64 getDroppedEventCount(QEvent::Type type);

    /// Number of all events dropped by the gate
    static qint64 getDroppedEventCount();

    /// Sets all counters of the dropped events to zero
    static void resetDroppedEventCounts();

    /// Test mode: each event dropped by the gate is reported by qWarning (DROPPED_EVENT_MESSAGE), so a test with
    /// QTest::failOnWarning finds a missing step of the shutdown
    static void setTestMode(bool isTestMode);
    static bool isTestMode();

    /// \p object is a helper child of this class (a notifier, a connection scope): the checks of the lifetime tests
    /// skip it (no new child appears in a retired subtree)
    static bool isHelperObject(const QObject* object);

private:
    friend class PDFDeferredDeletionHelper;

    static void enterSynchronousDeletion();
    static void leaveSynchronousDeletion();
};

}   // namespace pdf

#endif // PDFDEFERREDDELETION_H
