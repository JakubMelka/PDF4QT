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

#include "pdfdeferreddeletion.h"

#include <QAbstractAnimation>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QHash>
#include <QMenu>
#include <QMetaMethod>
#include <QSet>
#include <QShortcut>
#include <QThread>
#include <QTimer>
#include <QVarLengthArray>
#include <QWidget>

#include <algorithm>
#include <map>

namespace pdf
{

/// The event gate (newapp_robustness.md 14.5, RR3-H3): an event filter of the application, which drops the blocked
/// types of events for the retired objects until they are deleted
class PDFDeferredDeletionGate : public QObject
{
private:
    using BaseClass = QObject;

public:
    explicit PDFDeferredDeletionGate(QObject* parent);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
};

/// Shared state and the steps of PDFDeferredDeletion::retire. All of it lives in the GUI thread.
class PDFDeferredDeletionHelper
{
public:
    PDFDeferredDeletionHelper() = delete;

    /// Limit of the steps over logical owners in one walk (setOwner may form a cycle by mistake)
    static constexpr int MAXIMUM_OWNER_STEPS = 64;

    /// Number of sends of the posted deferred deletions in flush (a destructor may post another one)
    static constexpr int FLUSH_ROUNDS = 4;

    /// Limit of the steps over the focus chain of a window (it is a ring; the limit only guards against a broken one)
    static constexpr int MAXIMUM_FOCUS_STEPS = 100000;

    struct Data
    {
        QHash<const QObject*, PDFLifetimeNotifier*> notifiers;
        QHash<const QObject*, PDFConnectionScope*> scopes;
        QHash<const QObject*, PDFRetirable*> retirables;
        QSet<const QObject*> retired;   ///< Retired roots and their logically owned objects
        QSet<const QObject*> gated;     ///< Retired roots, whose retire has ended (the gate drops their events)
        std::map<int, qint64> droppedEvents;
        qint64 droppedEventTotal = 0;
        int synchronousDeletionDepth = 0;
        qint64 synchronousDeletionCount = 0;
        bool isSynchronousDeletionReported = false;
        bool isTestMode = false;
        QPointer<PDFDeferredDeletionGate> gate;
        bool isGateMovePending = false;                 ///< moveGateToFrontLater has queued its call
        std::vector<QPointer<QObject>> laterRetires;    ///< retireLater: retired by the queued retireLaterObjects
    };

    /// The queued call of retireLater: retires the objects in the order of the calls
    static void retireLaterObjects();

    /// The state (created once and never deleted: objects deleted after the end of main still find it)
    static Data& getData();

    /// The call is in the GUI thread; otherwise a qWarning (and an assertion in a debug build)
    static bool checkThread(const char* function);

    /// The connection scope of \p object (nullptr = none)
    static PDFConnectionScope* getScope(const QObject* object);
    static PDFConnectionScope* getOrCreateScope(QObject* object);
    static PDFLifetimeNotifier* getOrCreateNotifier(QObject* object);

    /// \p object, an ancestor or a logical owner (and its ancestors) is in \p set
    static bool isInSet(const QSet<const QObject*>& set, const QObject* object);

    /// The subtree of \p root: the root, its descendants and the logically owned objects with their subtrees (each
    /// object once, the root first); \p roots are the root and the owned objects, which are not inside another
    /// collected subtree (they are deleted by deleteLater)
    static void collect(QObject* root, std::vector<QPointer<QObject>>& objects, std::vector<QPointer<QObject>>& roots);

    /// Disconnects the outgoing signals of \p object one by one, except QObject::destroyed (RR2-L1)
    static void disconnectOutgoing(QObject* object);

    /// Closes the dialogs and menus, stops the timers and animations, disables the actions and shortcuts (step 6)
    static void shutDown(QObject* object);

    /// Fires the timeout of an active timer marked by setFlushOnRetire (step 6, before the disconnection)
    static void flushTimer(QObject* object);

    /// Moves the focus out of the retired objects (step 5)
    static void moveFocusOut(const std::vector<QPointer<QObject>>& roots);

    /// Moves the focus from the retired \p widget (the focus widget of the application or of its window) to the next
    /// widget found by findNextFocusWidget, as QWidget::hide moves it (focusNextPrevChild); clearFocus without one
    static void moveFocusFrom(QWidget* widget);

    /// The next widget of the focus chain after \p widget in its window, which is not retired, visible, enabled and
    /// takes the focus by Tab (its focus proxy, when it has one) - the rule of QApplicationPrivate::
    /// focusNextPrevChild_helper; nullptr when there is none
    static QWidget* findNextFocusWidget(QWidget* widget);

    /// An object of the snapshot of retire, which is still retired (RA-M1): an object moved to a parent or an owner
    /// outside the retired subtree by a listener, a hook or a token release (against their contract) is skipped by
    /// the later steps - it stays whole and it is not deleted; an object only taken out of the tree (no parent, no
    /// logical owner) is still shut down (it is not deleted by retire either)
    static bool isStillRetired(const QPointer<QObject>& item);

    /// The entry of a logically owned object, which has been deleted
    static bool isDeadOwned(const QPointer<QObject>& owned) { return owned.isNull(); }

    /// \p parent deletes its children (QObjectPrivate::deleteChildren has cleared the entry of \p child, which is
    /// being destroyed) and some of them are still left
    static bool hasChildrenLeftAfter(const QObject* parent, const QObject* child);

    /// Creates the gate and installs it on the application, once (the first filter installed on the application
    /// through the helper, or the first retire)
    static void ensureGate();

    /// Moves the gate in front of the other filters of the application now (after a filter was installed on the
    /// application through PDFDeferredDeletion::installEventFilter); nothing while there is no gate
    static void moveGateToFront();

    /// Moves the gate in front from the event loop (a queued call, coalesced): retire may run inside the dispatch of
    /// the application filters, which Qt iterates by index - a reinstall there cleans the list and shifts it, a later
    /// filter would miss the event (RA-L4); a filter installed directly (a test harness, the tooltip label of Qt) is
    /// behind the gate again after the event loop
    static void moveGateToFrontLater();

    /// The queued call of moveGateToFrontLater
    static void onGateMoveRequested();

    /// The gate filter
    static bool filterGateEvent(QObject* watched, QEvent* event);

    /// The handle is disconnected (by a targeted disconnect, a sender gone, retire)
    static bool isStale(const QMetaObject::Connection& connection) { return !connection; }

    /// The target or the filter of a remembered event filter is gone
    static bool isStaleEventFilter(const PDFConnectionScope::EventFilter& eventFilter) { return !eventFilter.target || !eventFilter.filter; }
};

PDFDeferredDeletionHelper::Data& PDFDeferredDeletionHelper::getData()
{
    static Data* data = new Data();
    return *data;
}

bool PDFDeferredDeletionHelper::checkThread(const char* function)
{
    if (QThread::isMainThread())
    {
        return true;
    }

    qWarning("PDFDeferredDeletion::%s was called outside of the GUI thread - nothing was done", function);
    Q_ASSERT_X(false, function, "PDFDeferredDeletion works only in the GUI thread");
    return false;
}

PDFConnectionScope* PDFDeferredDeletionHelper::getScope(const QObject* object)
{
    return object ? getData().scopes.value(object, nullptr) : nullptr;
}

PDFConnectionScope* PDFDeferredDeletionHelper::getOrCreateScope(QObject* object)
{
    if (!object)
    {
        return nullptr;
    }

    if (PDFConnectionScope* scope = getScope(object))
    {
        return scope;
    }
    return new PDFConnectionScope(object);
}

PDFLifetimeNotifier* PDFDeferredDeletionHelper::getOrCreateNotifier(QObject* object)
{
    if (!object)
    {
        return nullptr;
    }

    if (PDFLifetimeNotifier* notifier = getData().notifiers.value(object, nullptr))
    {
        return notifier;
    }
    return new PDFLifetimeNotifier(object);
}

bool PDFDeferredDeletionHelper::isInSet(const QSet<const QObject*>& set, const QObject* object)
{
    if (set.isEmpty() || !object)
    {
        return false;
    }

    // Each start walks its parents; a logical owner met on the way is a new start
    QVarLengthArray<const QObject*, 4> starts;
    starts.push_back(object);
    int ownerSteps = 0;
    while (!starts.isEmpty())
    {
        const QObject* current = starts.back();
        starts.pop_back();
        for (; current; current = current->parent())
        {
            if (set.contains(current))
            {
                return true;
            }

            const PDFConnectionScope* scope = getScope(current);
            const QObject* owner = scope ? scope->m_owner.data() : nullptr;
            if (owner && ownerSteps < MAXIMUM_OWNER_STEPS)
            {
                ++ownerSteps;
                starts.push_back(owner);
            }
        }
    }
    return false;
}

void PDFDeferredDeletionHelper::collect(QObject* root, std::vector<QPointer<QObject>>& objects, std::vector<QPointer<QObject>>& roots)
{
    const Data& data = getData();
    QSet<const QObject*> visited;
    std::vector<QObject*> pendingRoots;
    pendingRoots.push_back(root);
    for (size_t rootIndex = 0; rootIndex < pendingRoots.size(); ++rootIndex)
    {
        QObject* subtreeRoot = pendingRoots[rootIndex];
        if (visited.contains(subtreeRoot))
        {
            // An owned object inside a collected subtree goes with its parent
            continue;
        }
        roots.emplace_back(subtreeRoot);

        QList<QObject*> subtree = subtreeRoot->findChildren<QObject*>();
        subtree.prepend(subtreeRoot);
        for (QObject* object : subtree)
        {
            if (visited.contains(object))
            {
                continue;
            }
            visited.insert(object);
            objects.emplace_back(object);

            if (const PDFConnectionScope* scope = getScope(object))
            {
                for (const QPointer<QObject>& owned : scope->m_ownedObjects)
                {
                    // An owned object retired on its own is on its way already
                    if (owned && !data.retired.contains(owned.data()))
                    {
                        pendingRoots.push_back(owned.data());
                    }
                }
            }
        }
    }
}

void PDFDeferredDeletionHelper::disconnectOutgoing(QObject* object)
{
    // QObject::destroyed stays connected: the cleanup of Qt across the subtree (QAccessible cache, QCompleter,
    // QScroller, models of views outside) and the listeners of the application, which forget the pointer, need it
    static const int destroyedIndex = QMetaMethod::fromSignal(&QObject::destroyed).methodIndex();

    const QMetaObject* metaObject = object->metaObject();
    const int methodCount = metaObject->methodCount();
    for (int i = 0; i < methodCount; ++i)
    {
        const QMetaMethod method = metaObject->method(i);
        if (method.methodType() != QMetaMethod::Signal || i == destroyedIndex || (method.attributes() & QMetaMethod::Cloned))
        {
            // A clone (a signal with default arguments) has the index of its original in the connections
            continue;
        }
        QObject::disconnect(object, method, nullptr, QMetaMethod());
    }
}

void PDFDeferredDeletionHelper::flushTimer(QObject* object)
{
    QTimer* timer = qobject_cast<QTimer*>(object);
    const PDFConnectionScope* scope = timer ? getScope(timer) : nullptr;
    if (scope && scope->m_isFlushOnRetire && timer->isActive())
    {
        // The pending write runs now, through the connections of the timer, which still exist
        timer->stop();
        QMetaMethod::fromSignal(&QTimer::timeout).invoke(timer, Qt::DirectConnection);
    }
}

void PDFDeferredDeletionHelper::shutDown(QObject* object)
{
    if (QDialog* dialog = qobject_cast<QDialog*>(object))
    {
        if (dialog->isVisible())
        {
            // Not virtual: an override of reject / done does not run (it may refuse or emit its answer), the
            // signals of QDialog::done reach nobody (disconnected before), an exec of the dialog ends
            dialog->QDialog::done(QDialog::Rejected);
        }
    }
    else if (QMenu* menu = qobject_cast<QMenu*>(object))
    {
        if (menu->isVisible())
        {
            menu->close();
        }
    }
    else if (QTimer* timer = qobject_cast<QTimer*>(object))
    {
        timer->stop();
    }
    else if (QAction* action = qobject_cast<QAction*>(object))
    {
        action->setEnabled(false);
    }
    else if (QShortcut* shortcut = qobject_cast<QShortcut*>(object))
    {
        shortcut->setEnabled(false);
    }
    else if (QAbstractAnimation* animation = qobject_cast<QAbstractAnimation*>(object))
    {
        animation->stop();
    }
}

void PDFDeferredDeletionHelper::moveFocusOut(const std::vector<QPointer<QObject>>& roots)
{
    // The focus widget of the application, then the focus widgets of the windows of the roots (an inactive window
    // keeps its focus widget for its next activation)
    QWidget* focusWidget = QApplication::focusWidget();
    if (focusWidget && PDFDeferredDeletion::isRetired(focusWidget))
    {
        moveFocusFrom(focusWidget);
    }

    for (const QPointer<QObject>& root : roots)
    {
        QWidget* widget = qobject_cast<QWidget*>(root.data());
        QWidget* windowFocusWidget = widget ? widget->window()->focusWidget() : nullptr;
        if (windowFocusWidget && PDFDeferredDeletion::isRetired(windowFocusWidget))
        {
            moveFocusFrom(windowFocusWidget);
        }
    }
}

void PDFDeferredDeletionHelper::moveFocusFrom(QWidget* widget)
{
    // RA-L6: before step A, hide() of the closed widget moved the focus on (QWidgetPrivate::hide_helper ->
    // focusNextPrevChild); a keyboard user keeps a focus in the window. setFocus of an inactive window only makes the
    // widget the focus widget of its window. The new focus widget gets its FocusIn (code of the application runs)
    QPointer<QWidget> window = widget->window();
    QWidget* next = findNextFocusWidget(widget);
    if (next)
    {
        next->setFocus(Qt::TabFocusReason);
    }

    // No widget outside, or the focus stayed in the retired objects
    QWidget* windowFocusWidget = window ? window->focusWidget() : nullptr;
    if (windowFocusWidget && PDFDeferredDeletion::isRetired(windowFocusWidget))
    {
        windowFocusWidget->clearFocus();
    }
}

QWidget* PDFDeferredDeletionHelper::findNextFocusWidget(QWidget* widget)
{
    QWidget* window = widget->window();
    int steps = 0;
    for (QWidget* candidate = widget->nextInFocusChain(); candidate && candidate != widget && steps < MAXIMUM_FOCUS_STEPS; candidate = candidate->nextInFocusChain(), ++steps)
    {
        if (candidate->window() != window || !candidate->isEnabled() || !candidate->isVisibleTo(window) || PDFDeferredDeletion::isRetired(candidate))
        {
            continue;
        }

        // The focus goes to the deepest focus proxy (QWidget::setFocus)
        QWidget* target = candidate;
        for (int proxySteps = 0; target->focusProxy() && proxySteps < MAXIMUM_OWNER_STEPS; ++proxySteps)
        {
            target = target->focusProxy();
        }
        if (target != widget && target->isEnabled() && (target->focusPolicy() & Qt::TabFocus) == Qt::TabFocus && !PDFDeferredDeletion::isRetired(target))
        {
            return candidate;
        }
    }
    return nullptr;
}

bool PDFDeferredDeletionHelper::isStillRetired(const QPointer<QObject>& item)
{
    if (!item)
    {
        return false;
    }

    // An object only taken out of the tree (no parent, no logical owner) belongs to nobody alive: the code, which
    // took it out, deletes it (setParent(nullptr) + deleteLater of a tool inside its own call) - it is still shut
    // down. An object moved to a parent or an owner outside the subtree stays whole.
    const QObject* object = item.data();
    return PDFDeferredDeletion::isRetired(object) || (!object->parent() && !PDFDeferredDeletion::getOwner(object));
}

bool PDFDeferredDeletionHelper::hasChildrenLeftAfter(const QObject* parent, const QObject* child)
{
    if (!parent || child->parent() != parent)
    {
        return false;
    }

    // QObjectPrivate::deleteChildren sets the entry of the child being deleted to nullptr (the entries before it are
    // nullptr already); the child is still listed, when it is deleted alone
    const QObjectList& children = parent->children();
    if (children.contains(child))
    {
        return false;
    }
    for (const QObject* sibling : children)
    {
        if (sibling)
        {
            return true;
        }
    }
    return false;
}

void PDFDeferredDeletionHelper::ensureGate()
{
    QCoreApplication* application = QCoreApplication::instance();
    Data& data = getData();
    if (!application || data.gate)
    {
        return;
    }

    // The last installed filter of the application runs first
    data.gate = new PDFDeferredDeletionGate(application);
    application->installEventFilter(data.gate.data());
}

void PDFDeferredDeletionHelper::moveGateToFront()
{
    // A filter installed again moves to the front (qobject.cpp: the list is cleaned of the empty entries and the
    // filter is prepended). After QObject::installEventFilter of another filter, which has cleaned and shifted the
    // list already, the reinstall of the gate shifts nothing behind the two
    QCoreApplication* application = QCoreApplication::instance();
    Data& data = getData();
    if (application && data.gate)
    {
        application->installEventFilter(data.gate.data());
    }
}

void PDFDeferredDeletionHelper::moveGateToFrontLater()
{
    Data& data = getData();
    if (data.gate && !data.isGateMovePending)
    {
        data.isGateMovePending = true;
        QMetaObject::invokeMethod(data.gate.data(), &PDFDeferredDeletionHelper::onGateMoveRequested, Qt::QueuedConnection);
    }
}

void PDFDeferredDeletionHelper::onGateMoveRequested()
{
    // From the event loop: no dispatch of the application filters is in progress
    getData().isGateMovePending = false;
    moveGateToFront();
}

bool PDFDeferredDeletionHelper::filterGateEvent(QObject* watched, QEvent* event)
{
    Data& data = getData();
    if (data.gated.isEmpty() || !PDFDeferredDeletion::isEventBlocked(event->type()) || !isInSet(data.gated, watched))
    {
        return false;
    }

    ++data.droppedEvents[int(event->type())];
    ++data.droppedEventTotal;
    if (data.isTestMode)
    {
        qWarning("%s: type %d, object %s \"%s\"", PDFDeferredDeletion::DROPPED_EVENT_MESSAGE, int(event->type()),
                 watched->metaObject()->className(), qPrintable(watched->objectName()));
    }
    return true;
}

PDFDeferredDeletionGate::PDFDeferredDeletionGate(QObject* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("pdf_deferred_deletion_gate"));
}

bool PDFDeferredDeletionGate::eventFilter(QObject* watched, QEvent* event)
{
    return PDFDeferredDeletionHelper::filterGateEvent(watched, event);
}

// ------------------------------------------------------------------ PDFLifetimeNotifier

PDFLifetimeNotifier::PDFLifetimeNotifier(QObject* object) :
    BaseClass(object),
    m_object(object)
{
    setObjectName(QStringLiteral("pdf_lifetime_notifier"));
    PDFDeferredDeletionHelper::getData().notifiers.insert(object, this);
}

PDFLifetimeNotifier::~PDFLifetimeNotifier()
{
    // The object goes (the notifier dies with it): its address may be used by a new object, so a retired root is
    // forgotten - after the last of its children (RA-L5): the parent deletes its children in their order (~QWidget,
    // ~QObject), a child created after the notifier (by a hook, a token release) is destroyed later and must still
    // be in a retired subtree for the detector and the gate. With children left, a successor at the end of the
    // children forgets the root (it has emitted already; a helper object, as this one)
    PDFDeferredDeletionHelper::Data& data = PDFDeferredDeletionHelper::getData();
    if (data.notifiers.value(m_object, nullptr) == this)
    {
        data.notifiers.remove(m_object);
    }
    const bool isRetiredRoot = data.retired.contains(m_object) || data.gated.contains(m_object);
    if (isRetiredRoot && PDFDeferredDeletionHelper::hasChildrenLeftAfter(m_object, this))
    {
        PDFLifetimeNotifier* successor = new PDFLifetimeNotifier(m_object);
        successor->m_isEmitted = true;
    }
    else
    {
        data.retired.remove(m_object);
        data.gated.remove(m_object);
    }

    // The fallback: an object deleted without retire (with its parent, synchronously) - the listener only forgets
    emitOnce();
}

void PDFLifetimeNotifier::emitOnce()
{
    if (m_isEmitted)
    {
        return;
    }

    m_isEmitted = true;
    Q_EMIT aboutToBeDeleted(m_object);
}

// ------------------------------------------------------------------ PDFRetirable

PDFRetirable::PDFRetirable(QObject* object) :
    m_retirableObject(object)
{
    if (object && QThread::isMainThread())
    {
        PDFDeferredDeletionHelper::getData().retirables.insert(object, this);
    }
}

PDFRetirable::~PDFRetirable()
{
    if (!m_retirableObject || !QThread::isMainThread())
    {
        return;
    }

    PDFDeferredDeletionHelper::Data& data = PDFDeferredDeletionHelper::getData();
    if (data.retirables.value(m_retirableObject, nullptr) == this)
    {
        data.retirables.remove(m_retirableObject);
    }

    // The detector (RR-H3). The QObject base is still whole (only its non-virtual functions are used): the parents,
    // the name and the logical owner are read
    if (!m_isRetireHookCalled && data.synchronousDeletionDepth == 0 && !PDFDeferredDeletion::isRetired(m_retirableObject))
    {
        ++data.synchronousDeletionCount;
        if (data.isSynchronousDeletionReported)
        {
            const QObject* parent = m_retirableObject->parent();
            qCritical("%s: object \"%s\" (parent \"%s\") was deleted without PDFDeferredDeletion::retire", PDFDeferredDeletion::SYNCHRONOUS_DELETION_MESSAGE,
                      qPrintable(m_retirableObject->objectName()), parent ? qPrintable(parent->objectName()) : "");
        }
    }
}

// ------------------------------------------------------------------ PDFLifetimeToken

PDFLifetimeToken::PDFLifetimeToken(QObject* owner) :
    BaseClass(owner)
{

}

PDFLifetimeToken::~PDFLifetimeToken()
{
    if (!m_isReleased)
    {
        qWarning("PDFLifetimeToken \"%s\" was destroyed unreleased: its class must call releaseToken() in its destructor", qPrintable(objectName()));
    }
}

void PDFLifetimeToken::releaseToken()
{
    if (m_isReleased)
    {
        return;
    }

    m_isReleased = true;
    release();
}

// ------------------------------------------------------------------ PDFConnectionScope

PDFConnectionScope::PDFConnectionScope(QObject* object) :
    BaseClass(object),
    m_object(object)
{
    setObjectName(QStringLiteral("pdf_connection_scope"));
    PDFDeferredDeletionHelper::getData().scopes.insert(object, this);
}

PDFConnectionScope::~PDFConnectionScope()
{
    PDFDeferredDeletionHelper::Data& data = PDFDeferredDeletionHelper::getData();
    if (data.scopes.value(m_object, nullptr) == this)
    {
        data.scopes.remove(m_object);
    }

    // The object goes without retire (with its parent, synchronously): the remembered filters go too, the owned
    // objects follow later (no retire from a destructor - the detector reports a retirable deleted this way)
    removeEventFilters();
    const std::vector<QPointer<QObject>> ownedObjects = std::move(m_ownedObjects);
    for (const QPointer<QObject>& owned : ownedObjects)
    {
        if (owned)
        {
            owned->deleteLater();
        }
    }
}

int PDFConnectionScope::getConnectionCount() const
{
    return int(m_connections.size()) - int(std::count_if(m_connections.cbegin(), m_connections.cend(), &PDFDeferredDeletionHelper::isStale));
}

void PDFConnectionScope::addConnection(const QMetaObject::Connection& connection)
{
    if (m_connections.size() >= m_pruneLimit)
    {
        // A handle of a connection disconnected elsewhere (a targeted disconnect, a sender gone) is dropped; the
        // limit grows with the live handles, so the pruning is amortized
        auto it = std::remove_if(m_connections.begin(), m_connections.end(), &PDFDeferredDeletionHelper::isStale);
        m_connections.erase(it, m_connections.end());
        m_pruneLimit = std::max<size_t>(32, 2 * m_connections.size());
    }
    m_connections.push_back(connection);
}

void PDFConnectionScope::disconnectAll()
{
    const std::vector<QMetaObject::Connection> connections = std::move(m_connections);
    m_connections.clear();
    for (const QMetaObject::Connection& connection : connections)
    {
        // A stale handle is harmless (false)
        QObject::disconnect(connection);
    }
}

void PDFConnectionScope::addEventFilter(QObject* target, QObject* filter)
{
    // Qt keeps a filter installed again once (it moves to the front), so does the list; the entries of a deleted
    // target or filter are dropped, so a filter installed on every show does not grow the list
    auto it = std::remove_if(m_eventFilters.begin(), m_eventFilters.end(), &PDFDeferredDeletionHelper::isStaleEventFilter);
    m_eventFilters.erase(it, m_eventFilters.end());

    for (const EventFilter& eventFilter : m_eventFilters)
    {
        if (eventFilter.target == target && eventFilter.filter == filter)
        {
            return;
        }
    }
    m_eventFilters.push_back({ QPointer<QObject>(target), QPointer<QObject>(filter) });
}

void PDFConnectionScope::removeEventFilters()
{
    const std::vector<EventFilter> eventFilters = std::move(m_eventFilters);
    m_eventFilters.clear();
    for (const EventFilter& eventFilter : eventFilters)
    {
        if (eventFilter.target && eventFilter.filter)
        {
            eventFilter.target->removeEventFilter(eventFilter.filter.data());
        }
    }
}

// ------------------------------------------------------------------ PDFDeferredDeletion

PDFLifetimeNotifier* PDFDeferredDeletion::getNotifier(QObject* object)
{
    if (!object || !PDFDeferredDeletionHelper::checkThread("getNotifier"))
    {
        return nullptr;
    }
    return PDFDeferredDeletionHelper::getOrCreateNotifier(object);
}

void PDFDeferredDeletionHelper::retireLaterObjects()
{
    Data& data = getData();
    const std::vector<QPointer<QObject>> objects = std::move(data.laterRetires);
    data.laterRetires.clear();
    for (const QPointer<QObject>& object : objects)
    {
        // A retire meanwhile (also of an ancestor / owner) makes it nothing (step 0)
        PDFDeferredDeletion::retire(object.data());
    }
}

void PDFDeferredDeletion::retireLater(QObject* object)
{
    if (!object || !PDFDeferredDeletionHelper::checkThread("retireLater") || isRetired(object))
    {
        return;
    }

    QCoreApplication* application = QCoreApplication::instance();
    if (!application)
    {
        retire(object);
        return;
    }

    // Nothing of it is shown from now on; it is still connected until the event loop retires it
    if (object->isWidgetType())
    {
        static_cast<QWidget*>(object)->hide();
    }

    PDFDeferredDeletionHelper::Data& data = PDFDeferredDeletionHelper::getData();
    const bool isQueued = !data.laterRetires.empty();
    data.laterRetires.push_back(object);
    if (!isQueued)
    {
        QMetaObject::invokeMethod(application, &PDFDeferredDeletionHelper::retireLaterObjects, Qt::QueuedConnection);
    }
}

void PDFDeferredDeletion::retire(QObject* object)
{
    // 0. Nothing for a retired object (reentrant: a hook or a listener may call retire again)
    if (!object || !PDFDeferredDeletionHelper::checkThread("retire") || isRetired(object))
    {
        return;
    }

    PDFDeferredDeletionHelper::Data& data = PDFDeferredDeletionHelper::getData();
    std::vector<QPointer<QObject>> objects;
    std::vector<QPointer<QObject>> roots;
    PDFDeferredDeletionHelper::collect(object, objects, roots);

    // 1. The flag before any signal (RR2-M1): the listeners and hooks see isRetired; every retired root gets
    //    a notifier, whose destructor forgets the root (a new object may get its address)
    for (const QPointer<QObject>& root : roots)
    {
        data.retired.insert(root.data());
    }
    for (const QPointer<QObject>& root : roots)
    {
        if (root)
        {
            PDFDeferredDeletionHelper::getOrCreateNotifier(root.data());
        }
    }

    // 2. aboutToBeDeleted from a snapshot (a listener may delete anything; it never moves an object out of the
    //    subtree - an object moved out anyway is skipped from here on, isStillRetired, RA-M1)
    std::vector<QPointer<PDFLifetimeNotifier>> notifiers;
    for (const QPointer<QObject>& item : objects)
    {
        if (PDFLifetimeNotifier* notifier = item ? data.notifiers.value(item.data(), nullptr) : nullptr)
        {
            notifiers.emplace_back(notifier);
        }
    }
    for (const QPointer<PDFLifetimeNotifier>& notifier : notifiers)
    {
        if (notifier && PDFDeferredDeletion::isRetired(notifier->getObject()))
        {
            notifier->emitOnce();
        }
    }

    // 3. Incoming: the stored connection handles and the filters installed on foreign objects
    for (const QPointer<QObject>& item : objects)
    {
        PDFConnectionScope* scope = PDFDeferredDeletionHelper::isStillRetired(item) ? PDFDeferredDeletionHelper::getScope(item.data()) : nullptr;
        if (scope)
        {
            scope->disconnectAll();
            scope->removeEventFilters();
        }
    }

    // 4. Hooks (each once), then the tokens
    for (const QPointer<QObject>& item : objects)
    {
        PDFRetirable* retirable = PDFDeferredDeletionHelper::isStillRetired(item) ? data.retirables.value(item.data(), nullptr) : nullptr;
        if (retirable && !retirable->m_isRetireHookCalled)
        {
            retirable->m_isRetireHookCalled = true;
            retirable->retire();
        }
    }
    for (const QPointer<QObject>& item : objects)
    {
        PDFLifetimeToken* token = PDFDeferredDeletionHelper::isStillRetired(item) ? qobject_cast<PDFLifetimeToken*>(item.data()) : nullptr;
        if (token)
        {
            token->releaseToken();
        }
    }

    // 5. The focus leaves the subtree while the editors are still connected inside it (RR2-M2 b)
    PDFDeferredDeletionHelper::moveFocusOut(roots);

    // 6. Generic shutdown: the pending writes, the outgoing signals, then the dialogs, menus, timers, actions,
    //    shortcuts and animations
    for (const QPointer<QObject>& item : objects)
    {
        if (PDFDeferredDeletionHelper::isStillRetired(item))
        {
            PDFDeferredDeletionHelper::flushTimer(item.data());
        }
    }
    for (const QPointer<QObject>& item : objects)
    {
        if (PDFDeferredDeletionHelper::isStillRetired(item))
        {
            PDFDeferredDeletionHelper::disconnectOutgoing(item.data());
        }
    }
    for (const QPointer<QObject>& item : objects)
    {
        if (PDFDeferredDeletionHelper::isStillRetired(item))
        {
            PDFDeferredDeletionHelper::shutDown(item.data());
        }
    }

    // 7. Hidden: the roots and the windows of the subtree (a child window is not hidden with its parent)
    for (const QPointer<QObject>& item : objects)
    {
        QWidget* widget = PDFDeferredDeletionHelper::isStillRetired(item) ? qobject_cast<QWidget*>(item.data()) : nullptr;
        if (widget && widget->isWindow() && widget->isVisible())
        {
            widget->hide();
        }
    }
    for (const QPointer<QObject>& root : roots)
    {
        if (QWidget* widget = qobject_cast<QWidget*>(root.data()))
        {
            widget->hide();
        }
    }

    // 8. The calls queued for the subtree, also those queued by the steps 4-7, are dropped (last)
    for (const QPointer<QObject>& item : objects)
    {
        if (PDFDeferredDeletionHelper::isStillRetired(item))
        {
            QCoreApplication::removePostedEvents(item.data(), QEvent::MetaCall);
        }
    }

    // 9. The gate, then the deletion by the rule of Qt. A retire inside an application filter must not reorder the
    //    filters, which Qt iterates by index: the gate is installed once and moved in front from the event loop (RA-L4)
    for (const QPointer<QObject>& root : roots)
    {
        if (root)
        {
            data.gated.insert(root.data());
        }
    }
    PDFDeferredDeletionHelper::ensureGate();
    PDFDeferredDeletionHelper::moveGateToFrontLater();
    for (const QPointer<QObject>& root : roots)
    {
        if (root)
        {
            root->deleteLater();
        }
    }
}

bool PDFDeferredDeletion::isRetired(const QObject* object)
{
    if (!object || !PDFDeferredDeletionHelper::checkThread("isRetired"))
    {
        return false;
    }
    return PDFDeferredDeletionHelper::isInSet(PDFDeferredDeletionHelper::getData().retired, object);
}

void PDFDeferredDeletion::addConnection(const QObject* receiver, QMetaObject::Connection& connection)
{
    if (!receiver || !connection || !PDFDeferredDeletionHelper::checkThread("connect"))
    {
        return;
    }

    if (isRetired(receiver))
    {
        // Nothing reaches a retired object
        QObject::disconnect(connection);
        return;
    }

    if (PDFConnectionScope* scope = PDFDeferredDeletionHelper::getOrCreateScope(const_cast<QObject*>(receiver)))
    {
        scope->addConnection(connection);
    }
}

int PDFDeferredDeletion::getConnectionCount(const QObject* receiver)
{
    const PDFConnectionScope* scope = PDFDeferredDeletionHelper::getScope(receiver);
    return scope ? scope->getConnectionCount() : 0;
}

int PDFDeferredDeletion::getEventFilterCount(const QObject* owner)
{
    const PDFConnectionScope* scope = PDFDeferredDeletionHelper::getScope(owner);
    return scope ? scope->getEventFilterCount() : 0;
}

bool PDFDeferredDeletion::installEventFilter(QObject* target, QObject* filter, QObject* owner)
{
    if (!owner)
    {
        owner = filter;
    }

    if (!target || !filter || !PDFDeferredDeletionHelper::checkThread("installEventFilter") || isRetired(owner))
    {
        return false;
    }

    PDFConnectionScope* scope = PDFDeferredDeletionHelper::getOrCreateScope(owner);
    if (!scope)
    {
        return false;
    }

    target->installEventFilter(filter);
    scope->addEventFilter(target, filter);

    // The gate stays in front of every filter of the application installed through the helper (RR3-L4); the first
    // such filter (at the start of the application) creates it
    if (target == QCoreApplication::instance())
    {
        PDFDeferredDeletionHelper::ensureGate();
        PDFDeferredDeletionHelper::moveGateToFront();
    }
    return true;
}

void PDFDeferredDeletion::setOwner(QObject* object, QObject* owner)
{
    if (!object || object == owner || !PDFDeferredDeletionHelper::checkThread("setOwner"))
    {
        return;
    }

    // The previous owner forgets the object
    PDFConnectionScope* objectScope = PDFDeferredDeletionHelper::getScope(object);
    if (QObject* previousOwner = objectScope ? objectScope->m_owner.data() : nullptr)
    {
        if (PDFConnectionScope* previousScope = PDFDeferredDeletionHelper::getScope(previousOwner))
        {
            std::vector<QPointer<QObject>> owned;
            for (const QPointer<QObject>& item : previousScope->m_ownedObjects)
            {
                if (item && item.data() != object)
                {
                    owned.push_back(item);
                }
            }
            previousScope->m_ownedObjects = std::move(owned);
        }
        objectScope->m_owner = nullptr;
    }

    if (!owner)
    {
        return;
    }

    // A retired owner (itself, an ancestor or its owner chain): the object goes with it at once. It is retired
    // before it is linked - linked first, it would count as retired already (isRetired follows the owner) and
    // retire would do nothing (step 0), so it would stay live with its foreign connections
    if (isRetired(owner) && !isRetired(object))
    {
        if (PDFDeferredDeletionHelper::getData().isTestMode)
        {
            qWarning("%s: object %s \"%s\", owner %s \"%s\"", RETIRED_OWNER_MESSAGE, object->metaObject()->className(), qPrintable(object->objectName()),
                     owner->metaObject()->className(), qPrintable(owner->objectName()));
        }
        retire(object);
    }

    // The entries of the deleted owned objects are dropped (RA-L3: a long-lived owner of transient menus, popups and
    // questions would keep a dead QPointer of each, and collect walks them all)
    objectScope = PDFDeferredDeletionHelper::getOrCreateScope(object);
    PDFConnectionScope* ownerScope = PDFDeferredDeletionHelper::getOrCreateScope(owner);
    objectScope->m_owner = owner;
    std::vector<QPointer<QObject>>& ownedObjects = ownerScope->m_ownedObjects;
    ownedObjects.erase(std::remove_if(ownedObjects.begin(), ownedObjects.end(), &PDFDeferredDeletionHelper::isDeadOwned), ownedObjects.end());
    ownedObjects.emplace_back(object);
}

QObject* PDFDeferredDeletion::getOwner(const QObject* object)
{
    const PDFConnectionScope* scope = PDFDeferredDeletionHelper::getScope(object);
    return scope ? scope->m_owner.data() : nullptr;
}

int PDFDeferredDeletion::getOwnedObjectCount(const QObject* owner)
{
    const PDFConnectionScope* scope = PDFDeferredDeletionHelper::getScope(owner);
    return scope ? int(scope->m_ownedObjects.size()) : 0;
}

void PDFDeferredDeletion::setFlushOnRetire(QTimer* timer, bool isFlushed)
{
    if (!timer || !PDFDeferredDeletionHelper::checkThread("setFlushOnRetire"))
    {
        return;
    }

    if (PDFConnectionScope* scope = PDFDeferredDeletionHelper::getOrCreateScope(timer))
    {
        scope->m_isFlushOnRetire = isFlushed;
    }
}

void PDFDeferredDeletion::flush()
{
    if (!PDFDeferredDeletionHelper::checkThread("flush"))
    {
        return;
    }

    if (QThread::currentThread()->loopLevel() != 0)
    {
        qWarning("PDFDeferredDeletion::flush was called inside an event loop - nothing was done (only main.cpp after the loop and the destructors of test fixtures flush)");
        return;
    }

    SynchronousDeletionScope scope;
    for (int i = 0; i < PDFDeferredDeletionHelper::FLUSH_ROUNDS; ++i)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

bool PDFDeferredDeletion::isSynchronousDeletionAllowed()
{
    return PDFDeferredDeletionHelper::getData().synchronousDeletionDepth > 0;
}

void PDFDeferredDeletion::setSynchronousDeletionReported(bool isReported)
{
    PDFDeferredDeletionHelper::getData().isSynchronousDeletionReported = isReported;
}

bool PDFDeferredDeletion::isSynchronousDeletionReported()
{
    return PDFDeferredDeletionHelper::getData().isSynchronousDeletionReported;
}

qint64 PDFDeferredDeletion::getSynchronousDeletionCount()
{
    return PDFDeferredDeletionHelper::getData().synchronousDeletionCount;
}

void PDFDeferredDeletion::resetSynchronousDeletionCount()
{
    PDFDeferredDeletionHelper::getData().synchronousDeletionCount = 0;
}

void PDFDeferredDeletion::enterSynchronousDeletion()
{
    ++PDFDeferredDeletionHelper::getData().synchronousDeletionDepth;
}

void PDFDeferredDeletion::leaveSynchronousDeletion()
{
    --PDFDeferredDeletionHelper::getData().synchronousDeletionDepth;
}

bool PDFDeferredDeletion::isEventBlocked(QEvent::Type type)
{
    switch (type)
    {
        // Input
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::NonClientAreaMouseButtonPress:
        case QEvent::NonClientAreaMouseButtonRelease:
        case QEvent::NonClientAreaMouseButtonDblClick:
        case QEvent::NonClientAreaMouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::InputMethod:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::TabletEnterProximity:
        case QEvent::TabletLeaveProximity:
        case QEvent::Gesture:
        case QEvent::GestureOverride:
        case QEvent::NativeGesture:
        case QEvent::ContextMenu:
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
        case QEvent::HoverMove:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::DragLeave:
        case QEvent::Drop:
        case QEvent::ToolTip:
        case QEvent::WhatsThis:
        // Shortcuts
        case QEvent::Shortcut:
        case QEvent::ShortcutOverride:
        // Timers and queued calls
        case QEvent::Timer:
        case QEvent::MetaCall:
        // Painting and showing
        case QEvent::Paint:
        case QEvent::UpdateRequest:
        case QEvent::Show:
            return true;

        default:
            return false;
    }
}

qint64 PDFDeferredDeletion::getDroppedEventCount(QEvent::Type type)
{
    const std::map<int, qint64>& droppedEvents = PDFDeferredDeletionHelper::getData().droppedEvents;
    auto it = droppedEvents.find(int(type));
    return it != droppedEvents.cend() ? it->second : 0;
}

qint64 PDFDeferredDeletion::getDroppedEventCount()
{
    return PDFDeferredDeletionHelper::getData().droppedEventTotal;
}

void PDFDeferredDeletion::resetDroppedEventCounts()
{
    PDFDeferredDeletionHelper::Data& data = PDFDeferredDeletionHelper::getData();
    data.droppedEvents.clear();
    data.droppedEventTotal = 0;
}

void PDFDeferredDeletion::setTestMode(bool isTestMode)
{
    PDFDeferredDeletionHelper::getData().isTestMode = isTestMode;
}

bool PDFDeferredDeletion::isTestMode()
{
    return PDFDeferredDeletionHelper::getData().isTestMode;
}

bool PDFDeferredDeletion::isHelperObject(const QObject* object)
{
    return qobject_cast<const PDFLifetimeNotifier*>(object) || qobject_cast<const PDFConnectionScope*>(object);
}

}   // namespace pdf

#include "moc_pdfdeferreddeletion.cpp"
