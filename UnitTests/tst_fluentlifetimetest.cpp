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

// Generic test of the destruction order of the widgets of Pdf4QtLibFluent.
//
// Every page of the gallery (GalleryPages) is created and its widgets are
// destroyed in every order and situation: the page deleted while a widget is
// hovered, focused, pressed, with a timer running or a tooltip shown; each
// widget deleted alone (child first) followed by the use of the rest of the
// page; deletion in the reverse order of creation and parents before children;
// deletion from inside of a signal (the signals of all objects of the page and
// of the shown popups found by QMetaObject and by the user input on any
// widget); popups open while their owner, the popup itself or the window is
// deleted or hidden; models, delegates, views and viewports of item views in
// every order. A widget of the library is covered just by being placed on
// a page of the gallery (librarySamples checks it for every class of the
// library), what the generic input cannot do comes from the hints of its
// sample (GalleryLifetimeHints).
//
// Detection, which works also in a Release build on Windows: the objects are
// tracked by the hooks of QtCore (qtHookData, the signal spy callback), so
// a double destruction, an event delivered to a destroyed object and a signal
// emitted by a destroyed object are errors (heuristics: an address reused at
// once by a new object is not seen; a slot of a destroyed receiver cannot be
// called, Qt removes its connections); after each instance of a page no widget,
// window or object created by it may remain, no long-lived object (application,
// theme manager, style, ...) may keep more connections of its signals or more
// event filters than before, no popup may stay open and no warning of Qt may be
// printed. Use-after-free, which reads freed memory without such a side effect,
// is found by running the test under valgrind (CLAUDE.md) - the real detector.
//
// QtTest's option -vs (QSignalDumper) replaces the signal spy callbacks, the
// test refuses it.

#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthememanager.h"
#include "pdftooltip.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include "gallerypages.h"

#include "fluentlifetimeclasses.h"

#include <QtTest>

#include <QAbstractButton>
#include <QAccessible>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QApplication>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QWheelEvent>
#include <QFocusFrame>
#include <QGroupBox>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QHelpEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaMethod>
#include <QPointer>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QHeaderView>
#include <QTreeView>
#include <tuple>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <QtCore/private/qhooks_p.h>
#include <QtCore/private/qmetaobject_p.h>
#include <QtCore/private/qobject_p.h>
#include <QtCore/private/qobject_p_p.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using ObjectSet = std::unordered_set<const QObject*>;

/// Signals of the classes of Qt, which the library emits itself (found in its
/// sources at the build, FLUENT_LIBRARY_EMITS)
class LibrarySignals
{
public:
    LibrarySignals() = delete;

    /// Returns true, if a class of the library in the chain of \p metaObject emits the signal of the name
    static bool isEmittedByLibrary(const QMetaObject* metaObject, const QByteArray& signalName)
    {
        static const QSet<QPair<QByteArray, QByteArray>> libraryEmits = []()
        {
            QSet<QPair<QByteArray, QByteArray>> result;
            for (const FluentLibraryEmit& item : FLUENT_LIBRARY_EMITS)
            {
                result.insert(qMakePair(QByteArray(item.className), QByteArray(item.signalName)));
            }
            return result;
        }();

        for (; metaObject; metaObject = metaObject->superClass())
        {
            if (QByteArrayView(metaObject->className()).startsWith("pdf::") && libraryEmits.contains(qMakePair(QByteArray(metaObject->className()), signalName)))
            {
                return true;
            }
        }
        return false;
    }

    /// Returns true, if Qt guards itself against the deletion of the sender from
    /// inside of the signal (QAbstractButtonPrivate::emitPressed, emitReleased,
    /// emitClicked, emitToggled and their callers of Qt 6.11; not the toggled of
    /// a check box: QCheckBox::nextCheckState calls checkStateSet after it)
    static bool isGuardedByQt(const QObject* sender, const QMetaMethod& method)
    {
        // QAbstractButtonPrivate::emitClicked and the others emit the signals of the
        // group (idClicked, buttonClicked, ...) with a guard of the button and of the
        // group (the destructor of a QButtonGroup takes itself from its buttons)
        if (method.enclosingMetaObject() == &QButtonGroup::staticMetaObject)
        {
            return true;
        }
        if (method.enclosingMetaObject() != &QAbstractButton::staticMetaObject)
        {
            return false;
        }
        const QByteArray name = method.name();
        return name == "pressed" || name == "released" || name == "clicked" || (name == "toggled" && !sender->inherits("QCheckBox"));
    }
};

/// An assistive technology: accessibility is active (as on Windows with a client
/// of UI Automation), the objects of the updates of the accessibility are asked
/// for their interfaces, texts, state and children - later, from the event loop,
/// as a client in another process does (an interface asked synchronously from
/// inside of an update would meet objects of Qt under construction)
class AccessibilityClient
{
public:
    AccessibilityClient() = delete;

    static void install()
    {
        s_timer = new QTimer(qApp);
        s_timer->setSingleShot(true);
        s_timer->setInterval(0);
        QObject::connect(s_timer, &QTimer::timeout, s_timer, &AccessibilityClient::queryPending);
        QAccessible::installUpdateHandler(&AccessibilityClient::onUpdate);
        QAccessible::setActive(true);
    }

    static int getUpdateCount() { return s_updateCount; }

private:
    static void onUpdate(QAccessibleEvent* event)
    {
        ++s_updateCount;
        if (event->type() == QAccessible::ObjectDestroyed || !event->object() || s_pending.size() >= 64)
        {
            return;
        }

        s_pending << QPointer<QObject>(event->object());
        if (s_timer && !s_timer->isActive())
        {
            s_timer->start();
        }
    }

    static void queryPending()
    {
        QList<QPointer<QObject>> pending;
        pending.swap(s_pending);
        for (const QPointer<QObject>& object : std::as_const(pending))
        {
            // Not the menus of Qt: QAccessibleMenuItem of Qt 6.11 keeps a deleted
            // action (QAction::associatedObjects of a freed action, found by valgrind)
            if (!object || object->inherits("QAction") || object->inherits("QMenu") || object->inherits("QMenuBar"))
            {
                continue;
            }

            QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(object.data());
            if (!accessible || !accessible->isValid())
            {
                continue;
            }

            Q_UNUSED(accessible->text(QAccessible::Name));
            Q_UNUSED(accessible->text(QAccessible::Description));
            Q_UNUSED(accessible->state());
            Q_UNUSED(accessible->rect());
            const int childCount = qMin(accessible->childCount(), 16);
            for (int i = 0; i < childCount; ++i)
            {
                if (QAccessibleInterface* child = accessible->child(i))
                {
                    Q_UNUSED(child->text(QAccessible::Name));
                    Q_UNUSED(child->state());
                }
            }
            if (QAccessibleInterface* parent = accessible->parent())
            {
                Q_UNUSED(parent->text(QAccessible::Name));
            }
            if (QAccessibleActionInterface* actions = accessible->actionInterface())
            {
                Q_UNUSED(actions->actionNames());
            }
        }
    }

    static int s_updateCount;
    static QPointer<QTimer> s_timer;
    static QList<QPointer<QObject>> s_pending;
};

QPointer<QTimer> AccessibilityClient::s_timer;
QList<QPointer<QObject>> AccessibilityClient::s_pending;
int AccessibilityClient::s_updateCount = 0;

/// Sets a flag for the lifetime of the object
class ScopedFlag
{
public:
    explicit ScopedFlag(bool& flag, bool value) :
        m_flag(flag),
        m_previous(flag)
    {
        m_flag = value;
    }

    ~ScopedFlag()
    {
        m_flag = m_previous;
    }

    ScopedFlag(const ScopedFlag&) = delete;
    ScopedFlag& operator=(const ScopedFlag&) = delete;

private:
    bool& m_flag;
    bool m_previous;
};

/// Description of the current step of the test, a part of the error messages
class LifetimeContext
{
public:
    LifetimeContext() = delete;

    static void setStep(const QString& step)
    {
        s_step = step;
        if (!step.isEmpty())
        {
            write("STEP", step);
        }
    }

    static const QString& getStep() { return s_step; }

    /// Writes a line of the trace (environment variable PDF4QT_LIFETIME_TRACE)
    static void trace(const QString& text)
    {
        write("TRACE", text);
    }

    static void write(const char* kind, const QString& text)
    {
        static const bool isTraced = qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE");
        if (isTraced)
        {
            static QElapsedTimer clock;
            if (!clock.isValid())
            {
                clock.start();
            }
            fprintf(stderr, "%s %lld %s\n", kind, clock.elapsed(), qPrintable(text));
            fflush(stderr);
        }
    }

private:
    static QString s_step;
};

QString LifetimeContext::s_step;

/// Tracks all objects by the hooks of QtCore. A destroyed object is remembered,
/// until its address is used by a new object, so a second destruction of the
/// same object, an event delivered to it and a signal emitted by it are found,
/// although the freed memory is still readable in a Release build.
class ObjectTracker
{
public:
    ObjectTracker() = delete;

    /// Installs the hooks; called before the application object is created
    static void install()
    {
        qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(&ObjectTracker::onAdd);
        qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(&ObjectTracker::onRemove);
        // Only the signal callbacks: the slot callback is called for connections
        // by strings and QMetaMethod only (not for the functors of the library),
        // and Qt removes the connections of a destroyed receiver
        s_callbacks.signal_begin_callback = &ObjectTracker::onSignal;
        s_callbacks.slot_begin_callback = nullptr;
        s_callbacks.signal_end_callback = &ObjectTracker::onSignalEnd;
        s_callbacks.slot_end_callback = nullptr;
        qt_register_signal_spy_callbacks(&s_callbacks);
    }

    /// Removes the hooks; called after the application object is destroyed (the
    /// static data of the tracker are destroyed before the objects of libraries)
    static void uninstall()
    {
        qt_register_signal_spy_callbacks(nullptr);
        qtHookData[QHooks::AddQObject] = 0;
        qtHookData[QHooks::RemoveQObject] = 0;
    }

    /// Returns the set of the living objects
    static ObjectSet getLiveObjects()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_live;
    }

    static bool isDestroyed(const QObject* object)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_destroyed.count(object) > 0;
    }

    /// Returns true, if the deletion of the object is not supported by Qt now:
    /// Qt is emitting a signal of its own class of the object or of its
    /// descendant, and it uses the object after the signal (an application
    /// cannot delete it synchronously, the library cannot prevent the crash)
    /// \param reason Description of the signal
    static bool isDeletionUnsupported(const QObject* victim, QString* reason)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        for (const Emission& emission : s_emissions)
        {
            if (s_destroyed.count(emission.sender))
            {
                continue;
            }

            bool isInside = false;
            for (const QObject* object = emission.sender; object && !isInside; object = object->parent())
            {
                isInside = object == victim;
            }
            if (!isInside)
            {
                continue;
            }

            const QMetaMethod method = QMetaObjectPrivate::signal(emission.sender->metaObject(), emission.signal);
            const bool isLibrarySignal = QByteArrayView(method.enclosingMetaObject()->className()).startsWith("pdf::");
            if (isLibrarySignal || LibrarySignals::isEmittedByLibrary(emission.sender->metaObject(), method.name()) || LibrarySignals::isGuardedByQt(emission.sender, method))
            {
                continue;
            }

            if (reason)
            {
                *reason = QStringLiteral("Qt emits %1::%2 of %3").arg(QString::fromLatin1(method.enclosingMetaObject()->className()), QString::fromLatin1(method.methodSignature()),
                                                                     QString::fromLatin1(emission.sender->metaObject()->className()));
            }
            return true;
        }
        return false;
    }

    /// Forgets the destroyed objects (their addresses can be reused by other allocations)
    static void forgetDestroyed()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_destroyed.clear();
    }

    static void reportError(const QString& error)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        addError(error);
    }

    static QStringList takeErrors()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        QStringList errors;
        errors.swap(s_errors);
        return errors;
    }

    /// Describes the living objects of the main thread, which are not in \p before.
    /// An object of a class of \p singletons is not reported, while it is the only
    /// living object of its class (a lazily created window of the library or of Qt).
    static QStringList describeNewObjects(const ObjectSet& before, const QSet<QByteArray>& singletons, int limit)
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        // The objects are inspected under the lock: an object of another thread
        // cannot finish its destruction meanwhile
        std::vector<const QObject*> created;
        for (const QObject* object : s_live)
        {
            if (!before.count(object) && object->thread() == QCoreApplication::instance()->thread())
            {
                created.push_back(object);
            }
        }

        QStringList result;
        if (created.empty())
        {
            return result;
        }

        // Classes of the objects of the main thread only: an object of another
        // thread can be under construction (its class is not known yet)
        std::map<QByteArray, int> classCounts;
        for (const QObject* object : s_live)
        {
            if (object->thread() != QCoreApplication::instance()->thread())
            {
                continue;
            }
            const QByteArray className = object->metaObject()->className();
            if (singletons.contains(className))
            {
                ++classCounts[className];
            }
        }

        for (const QObject* object : created)
        {
            const QByteArray className = object->metaObject()->className();
            if (singletons.contains(className) && classCounts[className] <= 1)
            {
                continue;
            }

            // A pending single shot timer of Qt deletes itself at its timeout
            // (cleanupTestCase waits for them)
            if (className == "QSingleShotTimer" && s_allowPendingTimers)
            {
                continue;
            }

            // Threads of the global thread pool of Qt (QImage scaling, ...),
            // any number of them, an idle thread expires after 30 s
            if (className == "QThreadPoolThread")
            {
                continue;
            }

            // The renderers of the icons of the library are a global cache
            // (PDFThemeIconsHelper::getRenderer: one renderer for an icon and
            // a width of its strokes), created when an icon of a new size is drawn
            if (className == "QSvgRenderer" && !object->parent())
            {
                continue;
            }

            if (result.size() < limit)
            {
                const QObject* parent = object->parent();
                result << QStringLiteral("%1 '%2' (parent %3 '%4')").arg(QString::fromLatin1(className), object->objectName(),
                                                                            parent ? QString::fromLatin1(parent->metaObject()->className()) : QStringLiteral("none"),
                                                                            parent ? parent->objectName() : QString());
            }
            else
            {
                result << QStringLiteral("...");
                break;
            }
        }
        return result;
    }

    /// Connections of the signals and event filters of a long-lived object
    struct Census
    {
        QByteArray className;
        int connections = 0;        ///< Connections of its signals (the receiver lives)
        int eventFilters = 0;       ///< Event filters installed on it (the filter lives)
        const QMetaObject* metaObject = nullptr;
        std::vector<std::pair<int, int>> signalCounts;  ///< Signal index and the number of its connections (for the message)
    };
    using CensusMap = std::unordered_map<const QObject*, Census>;

    /// Counts the connections of the signals and the event filters of the living
    /// objects of the main thread, which are in \p objects. A connection, whose
    /// receiver is an object of a class of \p ignoredReceivers (a lazily created
    /// singleton), is not counted.
    static CensusMap takeCensus(const ObjectSet& objects, const QSet<QByteArray>& ignoredReceivers)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        CensusMap result;
        const QThread* mainThread = QCoreApplication::instance()->thread();
        for (const QObject* object : objects)
        {
            if (!s_live.count(object) || object->thread() != mainThread)
            {
                continue;
            }

            Census census;
            census.metaObject = object->metaObject();
            census.className = census.metaObject->className();
            const QObjectPrivate* d = QObjectPrivate::get(object);
            if (const QObjectPrivate::ConnectionData* data = d->connections.loadAcquire())
            {
                if (const QObjectPrivate::SignalVector* vector = data->signalVector.loadAcquire())
                {
                    // Index -1 is the list of the connections to all signals
                    for (int i = -1; i < vector->count(); ++i)
                    {
                        int count = 0;
                        for (const QObjectPrivate::Connection* connection = vector->at(i).first.loadRelaxed(); connection; connection = connection->nextConnectionList.loadRelaxed())
                        {
                            const QObject* receiver = connection->receiver.loadRelaxed();
                            if (receiver && (!s_live.count(receiver) || receiver->thread() != mainThread || !ignoredReceivers.contains(receiver->metaObject()->className())))
                            {
                                ++count;
                            }
                        }
                        if (count > 0)
                        {
                            census.connections += count;
                            census.signalCounts.emplace_back(i, count);
                        }
                    }
                }
            }
            if (d->extraData)
            {
                for (const QPointer<QObject>& filter : d->extraData->eventFilters)
                {
                    if (filter)
                    {
                        ++census.eventFilters;
                    }
                }
            }
            result.emplace(object, std::move(census));
        }
        return result;
    }

    static QString describeSignals(const Census& census)
    {
        QStringList names;
        for (const auto& [index, count] : census.signalCounts)
        {
            const QMetaMethod method = index >= 0 ? QMetaObjectPrivate::signal(census.metaObject, index) : QMetaMethod();
            names << QStringLiteral("%1 x%2").arg(method.isValid() ? QString::fromLatin1(method.methodSignature()) : QStringLiteral("(all)")).arg(count);
        }
        return names.join(QStringLiteral(", "));
    }

    /// Compares two counts of the same objects
    static QStringList compareCensus(const CensusMap& before, const CensusMap& after)
    {
        QStringList problems;
        for (const auto& [object, census] : after)
        {
            auto it = before.find(object);
            if (it == before.end() || it->second.className != census.className)
            {
                // Destroyed meanwhile, a new object at the same address
                continue;
            }

            const Census& previous = it->second;
            if (census.connections > previous.connections)
            {
                problems << QStringLiteral("connections of the signals of the long-lived %1 '%2': %3 -> %4 (now: %5; before: %6)")
                                .arg(QString::fromLatin1(census.className), object->objectName()).arg(previous.connections).arg(census.connections)
                                .arg(describeSignals(census), describeSignals(previous));
            }
            if (census.eventFilters > previous.eventFilters)
            {
                problems << QStringLiteral("event filters of the long-lived %1 '%2': %3 -> %4")
                                .arg(QString::fromLatin1(census.className), object->objectName()).arg(previous.eventFilters).arg(census.eventFilters);
            }
        }
        return problems;
    }

private:
    static void addError(const QString& error)
    {
        if (s_errors.size() < 20)
        {
            s_errors << error;
        }
    }

    static QString getStepText()
    {
        // The step is written by the main thread only
        return QThread::isMainThread() ? LifetimeContext::getStep() : QStringLiteral("(other thread)");
    }

    static void onAdd(QObject* object)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_destroyed.erase(object);
        s_live.insert(object);
    }

    static void onRemove(QObject* object)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_live.erase(object) == 0 && s_destroyed.count(object) > 0)
        {
            addError(QStringLiteral("object 0x%1 destroyed twice, step: %2").arg(quintptr(object), 0, 16).arg(getStepText()));
        }
        s_destroyed.insert(object);

        // Qt does not call the end callback of a signal, whose sender has been destroyed
        s_emissions.erase(std::remove_if(s_emissions.begin(), s_emissions.end(), [object](const Emission& emission) { return emission.sender == object; }), s_emissions.end());
    }

    static void onSignal(QObject* caller, int index, void** argv)
    {
        Q_UNUSED(argv);
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_destroyed.count(caller) > 0)
        {
            addError(QStringLiteral("signal %1 emitted by destroyed object 0x%2, step: %3").arg(index).arg(quintptr(caller), 0, 16).arg(getStepText()));
        }
        if (QThread::isMainThread())
        {
            s_emissions.push_back(Emission{ caller, index });
        }
    }

    static void onSignalEnd(QObject* caller, int index)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!QThread::isMainThread())
        {
            return;
        }
        for (auto it = s_emissions.rbegin(); it != s_emissions.rend(); ++it)
        {
            if (it->sender == caller && it->signal == index)
            {
                s_emissions.erase(std::next(it).base());
                break;
            }
        }
    }

    /// Signal being emitted on the main thread
    struct Emission
    {
        const QObject* sender = nullptr;
        int signal = -1;            ///< Index of the signal (QObjectPrivate::signalIndex, not the method index)
    };

public:
    static bool s_allowPendingTimers;

private:
    static std::mutex s_mutex;
    static ObjectSet s_live;
    static ObjectSet s_destroyed;
    static QStringList s_errors;
    static QSignalSpyCallbackSet s_callbacks;
    static std::vector<Emission> s_emissions;
};

std::vector<ObjectTracker::Emission> ObjectTracker::s_emissions;

std::mutex ObjectTracker::s_mutex;
bool ObjectTracker::s_allowPendingTimers = true;
ObjectSet ObjectTracker::s_live;
ObjectSet ObjectTracker::s_destroyed;
QStringList ObjectTracker::s_errors;
QSignalSpyCallbackSet ObjectTracker::s_callbacks = { nullptr, nullptr, nullptr, nullptr };

/// Collects the warnings of Qt; a warning is an error of the test, unless it is
/// in the short list of the expected ones
class MessageCollector
{
public:
    MessageCollector() = delete;

    static void install()
    {
        s_previous = qInstallMessageHandler(&MessageCollector::handler);
    }

    static QStringList takeMessages()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        QStringList messages;
        messages.swap(s_messages);
        return messages;
    }

    static bool isAllowed(const QString& message)
    {
        // The offscreen platform plugin cannot propagate the size hints of
        // a window to the windowing system, raise a window (a tooltip) and grab
        // the keyboard (a popup) - QPlatformWindow::propagateSizeHints, raise,
        // setKeyboardGrabEnabled
        //
        // The offscreen platform plugin on Windows looks for the fonts in the
        // directory lib/fonts of Qt (QOffscreenIntegration uses the fontconfig
        // database only on Unix), the library has its own fonts
        //
        // A model deleted from inside of a signal of its view (clicked, pressed):
        // the view of Qt keeps its selection model, whose model is gone, and
        // selects by the rest of the mouse event (QAbstractItemView of Qt 6.11)
        return message.startsWith(QLatin1String("QFontDatabase: Cannot find font directory")) ||
               message == QLatin1String("This plugin does not support propagateSizeHints()") ||
               message == QLatin1String("This plugin does not support raise()") ||
               message == QLatin1String("This plugin does not support grabbing the keyboard") ||
               (s_allowSelectionWithoutModel && message == QLatin1String("QItemSelectionModel: Selecting when no model has been set will result in a no-op."));
    }

    /// The warning of a selection without a model is expected (a model deleted by the test)
    static bool s_allowSelectionWithoutModel;

private:
    static void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        const bool isWarning = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
        if (isWarning && isAllowed(message))
        {
            // Expected warnings are not passed to QtTest (thousands of them would
            // exceed its limit of messages, which hides the rest of the output)
            return;
        }

        if (isWarning)
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_messages.size() < 20)
            {
                s_messages << QStringLiteral("warning '%1', step: %2").arg(message, QThread::isMainThread() ? LifetimeContext::getStep() : QStringLiteral("(other thread)"));
            }
        }

        if (s_previous)
        {
            s_previous(type, context, message);
        }
    }

    static std::mutex s_mutex;
    static QStringList s_messages;
    static QtMessageHandler s_previous;
};

bool MessageCollector::s_allowSelectionWithoutModel = false;
std::mutex MessageCollector::s_mutex;
QStringList MessageCollector::s_messages;
QtMessageHandler MessageCollector::s_previous = nullptr;

/// Filter of the application: finds events delivered to destroyed objects and
/// operates the popups (menus, dialogs, palettes), which are shown by the input
/// of the test, also inside of their own event loops (QMenu::exec, QDialog::exec)
class EventWatcher : public QObject
{
    Q_OBJECT

public:
    /// What is done with a popup, which has been shown
    enum class Policy
    {
        Close,      ///< Closed by Esc (or by close())
        Accept,     ///< The first action of a menu is activated, Return is pressed in other popups
        Keep        ///< Left open
    };

    using Action = std::function<void(QWidget* popup)>;

    static EventWatcher* getInstance()
    {
        static EventWatcher* watcher = new EventWatcher(qApp);
        return watcher;
    }

    void setSpareWindow(QWidget* window) { m_spare = window; }
    void setHostWindow(QWidget* window) { m_host = window; }
    void setPolicy(Policy policy) { m_policy = policy; }
    Policy getPolicy() const { return m_policy; }

    /// Sets the action, which is performed once with the next shown popup
    /// instead of the policy. The action is performed only while \p context
    /// lives (the action refers to the data of the caller).
    void setAction(Action action, QObject* context = nullptr)
    {
        m_action = std::move(action);
        m_actionContext = context;
    }
    bool hasAction() const { return static_cast<bool>(m_action); }

    /// Returns the number of the windows shown since the start of the test
    int getShownCount() const { return m_shownCount; }

    /// Function called with each shown window (not a tooltip) at its show,
    /// before the policy or the action is performed; an empty function removes it
    using ShownHook = std::function<void(QWidget* window)>;
    void setShownHook(ShownHook hook) { m_shownHook = std::move(hook); }

    /// Window shown by the input of the test
    struct ShownWindow
    {
        QPointer<QWidget> window;   ///< Null, when the window has been deleted meanwhile (a menu deleted on close)
        QString className;
        bool isToolTip = false;
    };

    /// Returns the windows shown since the given count (also the deleted ones)
    QList<ShownWindow> getShownSince(int count) const
    {
        return m_shown.mid(qMax(0, count - m_shownBase));
    }

    void clearShown()
    {
        m_shownBase = m_shownCount;
        m_shown.clear();
    }

    /// Performs the policy with the popup
    static void applyPolicy(QWidget* popup, Policy policy)
    {
        if (!popup || !popup->isVisible())
        {
            return;
        }

        QPointer<QWidget> guard(popup);
        switch (policy)
        {
            case Policy::Close:
            {
                if (popup->windowHandle())
                {
                    QTest::keyClick(popup->windowHandle(), Qt::Key_Escape);
                }
                LifetimeContext::trace(QStringLiteral("after Escape: visible %1").arg(guard && guard->isVisible()));
                if (guard && guard->isVisible())
                {
                    guard->close();
                }
                LifetimeContext::trace(QStringLiteral("after close: visible %1").arg(guard && guard->isVisible()));
                break;
            }

            case Policy::Accept:
            {
                if (QMenu* menu = qobject_cast<QMenu*>(popup))
                {
                    for (QAction* action : menu->actions())
                    {
                        if (action->isEnabled() && !action->isSeparator() && !action->menu() && action->isVisible())
                        {
                            menu->setActiveAction(action);
                            break;
                        }
                    }
                }
                if (popup->windowHandle())
                {
                    QTest::keyClick(popup->windowHandle(), Qt::Key_Return);
                }
                if (guard && guard->isVisible() && (guard->windowType() == Qt::Popup || qobject_cast<QDialog*>(guard.data())))
                {
                    guard->close();
                }
                break;
            }

            case Policy::Keep:
                break;
        }
    }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (ObjectTracker::isDestroyed(watched))
        {
            ObjectTracker::reportError(QStringLiteral("event %1 delivered to destroyed object 0x%2, step: %3").arg(int(event->type())).arg(quintptr(watched), 0, 16).arg(LifetimeContext::getStep()));
        }

        if (event->type() == QEvent::Show && watched->isWidgetType())
        {
            QWidget* widget = static_cast<QWidget*>(watched);
            if (widget->isWindow() && widget != m_spare && widget != m_host)
            {
                onWindowShown(widget);
            }
        }

        return false;
    }

private:
    explicit EventWatcher(QObject* parent) :
        QObject(parent)
    {
    }

    void onWindowShown(QWidget* window)
    {
        LifetimeContext::trace(QStringLiteral("window shown: %1").arg(QString::fromLatin1(window->metaObject()->className())));
        ++m_shownCount;
        m_shown << ShownWindow{ window, QString::fromLatin1(window->metaObject()->className()), window->windowType() == Qt::ToolTip };

        if (window->windowType() == Qt::ToolTip)
        {
            return;
        }

        if (m_shownHook)
        {
            m_shownHook(window);
        }

        // The popup is operated from the event loop: a modal popup runs its own loop
        QPointer<QWidget> guard(window);
        Action action;
        action.swap(m_action);
        QObject* context = (action && m_actionContext) ? m_actionContext.data() : this;
        m_actionContext = nullptr;
        const Policy policy = m_policy;
        QTimer::singleShot(0, context, [guard, action, policy]()
        {
            LifetimeContext::trace(QStringLiteral("popup action, visible %1, loop level %2").arg(guard && guard->isVisible()).arg(QThread::currentThread()->loopLevel()));
            if (!guard || !guard->isVisible())
            {
                return;
            }

            if (action)
            {
                action(guard.data());
            }
            else
            {
                applyPolicy(guard.data(), policy);
            }

            // The event loop of a closed modal popup (QDialog::exec, QMenu::exec)
            // waits for the next event of the event dispatcher of Windows,
            // before it sees, that it has been exited
            QMetaObject::invokeMethod(qApp, []() { }, Qt::QueuedConnection);

            // A modal popup, which the action has left open in its own loop (its
            // buttons deleted by a signal), is closed later, so the test goes on
            if (guard && guard->isVisible() && QThread::currentThread()->loopLevel() > 0 && policy != Policy::Keep)
            {
                getInstance()->closeLater(guard, 0);
            }
        });
    }

    /// Closes a popup left open (Esc, close(), at last hide(), which ends the loop of QDialog::exec)
    void closeLater(QPointer<QWidget> popup, int attempt)
    {
        QTimer::singleShot(0, this, [this, popup, attempt]()
        {
            if (!popup || !popup->isVisible())
            {
                return;
            }
            if (attempt < 2)
            {
                applyPolicy(popup.data(), Policy::Close);
            }
            else
            {
                popup->hide();
            }
            QMetaObject::invokeMethod(qApp, []() { }, Qt::QueuedConnection);
            if (popup && popup->isVisible())
            {
                closeLater(popup, attempt + 1);
            }
        });
    }

    QPointer<QWidget> m_spare;
    QPointer<QWidget> m_host;
    Policy m_policy = Policy::Close;
    Action m_action;
    QPointer<QObject> m_actionContext;
    ShownHook m_shownHook;
    QList<ShownWindow> m_shown;
    int m_shownCount = 0;
    int m_shownBase = 0;
};

/// Path of a widget: the indices of the child widgets from a root widget
class WidgetPath
{
public:
    WidgetPath() = delete;

    /// Returns the widget at the path (nullptr, when there is none)
    static QWidget* resolve(QWidget* root, const QList<int>& path)
    {
        QWidget* widget = root;
        for (int index : path)
        {
            if (!widget)
            {
                return nullptr;
            }

            QWidget* found = nullptr;
            int current = 0;
            for (QObject* child : widget->children())
            {
                if (child->isWidgetType())
                {
                    if (current == index)
                    {
                        found = static_cast<QWidget*>(child);
                        break;
                    }
                    ++current;
                }
            }
            widget = found;
        }
        return widget;
    }

    /// Returns the path of the widget from the root (the root is an ancestor of the widget or the widget itself)
    static QList<int> get(const QWidget* root, const QWidget* widget)
    {
        QList<int> path;
        for (const QWidget* current = widget; current && current != root; current = current->parentWidget())
        {
            const QWidget* parent = current->parentWidget();
            if (!parent)
            {
                return QList<int>();
            }
            int index = 0;
            for (const QObject* child : parent->children())
            {
                if (child == current)
                {
                    break;
                }
                if (child->isWidgetType())
                {
                    ++index;
                }
            }
            path.prepend(index);
        }
        return path;
    }
};

/// Window with a page of the gallery in a scroll area (as the gallery shows it)
class PageHost
{
public:
    explicit PageHost(int pageIndex)
    {
        m_window = new QWidget(nullptr);
        m_window->setObjectName(QStringLiteral("lifetime_host"));
        QVBoxLayout* layout = new QVBoxLayout(m_window);
        layout->setContentsMargins(0, 0, 0, 0);
        m_scrollArea = new QScrollArea(m_window);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_scrollArea->setWidgetResizable(true);
        layout->addWidget(m_scrollArea);

        pdffluentgallery::GalleryPages::Page page = pdffluentgallery::GalleryPages::createPage(pageIndex, nullptr);
        m_id = page.id;
        m_page = page.widget;
        m_scrollArea->setWidget(page.widget);

        m_window->resize(WIDTH, HEIGHT);
        EventWatcher::getInstance()->setHostWindow(m_window);
        m_window->show();
        if (QTest::qWaitForWindowExposed(m_window))
        {
            m_window->activateWindow();
            [[maybe_unused]] const bool isActive = QTest::qWaitForWindowActive(m_window, 1000);
        }
        QCoreApplication::processEvents();
    }

    ~PageHost()
    {
        destroy();
    }

    PageHost(const PageHost&) = delete;
    PageHost& operator=(const PageHost&) = delete;

    /// Size of the window (the page is scrolled to the widgets)
    static constexpr int WIDTH = 960;
    static constexpr int HEIGHT = 640;

    QWidget* getWindow() const { return m_window.data(); }
    QWidget* getPage() const { return m_page.data(); }
    QScrollArea* getScrollArea() const { return m_scrollArea.data(); }
    const QString& getId() const { return m_id; }

    /// Returns the widget at the path of indices of child widgets from the page
    QWidget* resolve(const QList<int>& path) const
    {
        return WidgetPath::resolve(m_page.data(), path);
    }

    void destroy()
    {
        delete m_window.data();
    }

    /// Applies the hints of the samples of the gallery (GalleryLifetimeHints::prepare):
    /// what an application does with a widget, which the generic input of the
    /// test cannot do (the "more" buttons get their menus - every second one
    /// a child of the button)
    void applyHints()
    {
        if (!m_page)
        {
            return;
        }

        QList<QWidget*> widgets = m_page->findChildren<QWidget*>();
        widgets.prepend(m_page);
        for (const QPointer<QWidget>& widget : QList<QPointer<QWidget>>(widgets.cbegin(), widgets.cend()))
        {
            if (widget)
            {
                const pdffluentgallery::GalleryLifetimeHints hints = pdffluentgallery::GalleryLifetimeHints::get(widget);
                if (hints.prepare)
                {
                    hints.prepare(widget);
                }
            }
        }
    }

    /// Shows the window again and makes it active
    void activate()
    {
        if (m_window)
        {
            m_window->show();
            m_window->activateWindow();
            [[maybe_unused]] const bool isActive = QTest::qWaitForWindowActive(m_window, 1000);
        }
    }

private:
    QPointer<QWidget> m_window;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_page;
    QString m_id;
};

/// Widget of a page of the gallery
struct WidgetInfo
{
    QPointer<QWidget> widget;
    QList<int> path;            ///< Indices of child widgets from the page
    int parent = -1;            ///< Index of the information of the parent, -1 for children of the page
    QString className;
    QString description;        ///< Page, sample of the gallery, class, object name and path
    QString variant;            ///< Key of the deduplication: class, parent, children and state
    bool isTarget = false;      ///< Can be destroyed by an application (not an internal widget of Qt)
    bool isLibrary = false;     ///< Class of the library (namespace pdf)
    bool isTrivial = false;     ///< Caption or sample drawn by a function, covered by a few instances only
    bool isVisible = false;
    bool isEnabled = false;
    bool isFocusable = false;   ///< Takes the focus by the Tab key
    bool hasToolTip = false;
    bool hasSignals = false;    ///< Has signals tested by the deletion from inside of them (or its delegate has)
    bool isView = false;        ///< Item view with a model, which has items
    QString classKey;           ///< Class and the class of the parent
};

/// Finds the widgets of a page and decides, which of them an application may destroy
class WidgetCatalog
{
public:
    WidgetCatalog() = delete;

    static std::vector<WidgetInfo> build(QWidget* page, const QString& pageId)
    {
        std::vector<WidgetInfo> infos;
        visit(page, QList<int>(), -1, false, false, pageId, infos);

        // A few instances of the trivial classes are kept
        QHash<QString, int> trivialCounts;
        for (WidgetInfo& info : infos)
        {
            if (info.className == QLatin1String("pdffluentgallery::GallerySpecimen") || info.className == QLatin1String("pdf::PDFLabel"))
            {
                info.isTrivial = ++trivialCounts[info.className] > 4;
            }
        }
        return infos;
    }

    /// Returns true, if the information \p ancestor is an ancestor of \p descendant
    static bool isAncestor(const std::vector<WidgetInfo>& infos, int ancestor, int descendant)
    {
        for (int i = infos[descendant].parent; i >= 0; i = infos[i].parent)
        {
            if (i == ancestor)
            {
                return true;
            }
        }
        return false;
    }

    static QString pathText(const QList<int>& path)
    {
        QStringList parts;
        for (int index : path)
        {
            parts << QString::number(index);
        }
        return parts.join(QLatin1Char('.'));
    }

private:
    static bool isComposite(const QWidget* widget)
    {
        // Qt widgets, whose child widgets are their own parts
        return widget->inherits("QAbstractSpinBox") || widget->inherits("QComboBox") || widget->inherits("QCalendarWidget") ||
               widget->inherits("QTabBar") || widget->inherits("QTabWidget") || widget->inherits("QAbstractScrollArea") ||
               widget->inherits("QDialogButtonBox") || widget->inherits("QMenuBar") || widget->inherits("QMenu") ||
               widget->inherits("QLineEdit") || widget->inherits("QToolBox");
    }

    /// Returns true, if the child of a toolbar is a part of it: the buttons,
    /// which QToolBar creates for its actions (QToolBarLayout crashes, when one
    /// of them is deleted directly), not a widget of the application added by
    /// QToolBar::addWidget (a QWidgetAction finds it gone)
    static bool isToolBarPart(const QWidget* parent, const QWidget* child)
    {
        const QToolBar* toolBar = qobject_cast<const QToolBar*>(parent);
        if (!toolBar)
        {
            return false;
        }
        for (QAction* action : toolBar->actions())
        {
            const QWidgetAction* widgetAction = qobject_cast<const QWidgetAction*>(action);
            if (widgetAction && widgetAction->defaultWidget() == child)
            {
                return false;
            }
        }
        return true;
    }

    static bool isInternalClass(const QString& className)
    {
        static const QSet<QString> classes =
        {
            QStringLiteral("QFocusFrame"), QStringLiteral("QLineEditIconButton"), QStringLiteral("QToolBarExtension"),
            QStringLiteral("QToolBarSeparator"), QStringLiteral("QSplitterHandle"), QStringLiteral("QDockWidgetTitleButton"),
            QStringLiteral("CloseButton"), QStringLiteral("QTableCornerButton"), QStringLiteral("QSizeGrip"),
            QStringLiteral("QRubberBand"), QStringLiteral("QCalendarPopup"), QStringLiteral("QtPrivate::QCalendarView"),
            QStringLiteral("QtPrivate::QPrevNextCalButton"), QStringLiteral("QComboBoxPrivateContainer"), QStringLiteral("QComboBoxListView"),
            QStringLiteral("QToolBoxButton"), QStringLiteral("QMainWindowLayoutSeparatorHelper")
        };
        return classes.contains(className);
    }

    static void visit(QWidget* parent, const QList<int>& parentPath, int parentIndex, bool parentInternal, bool parentContentHolder, const QString& pageId, std::vector<WidgetInfo>& infos)
    {
        int childIndex = 0;
        for (QObject* object : parent->children())
        {
            if (!object->isWidgetType())
            {
                continue;
            }

            QWidget* child = static_cast<QWidget*>(object);
            QList<int> path = parentPath;
            path << childIndex++;

            const QString className = QString::fromLatin1(child->metaObject()->className());
            const bool isLibrary = className.startsWith(QLatin1String("pdf::"));
            const bool isQtName = child->objectName().startsWith(QLatin1String("qt_"));

            bool isInternal = isQtName || isInternalClass(className) || (parentInternal && !parentContentHolder) || isToolBarPart(parent, child);
            if (!isInternal && isComposite(parent) && !isLibrary)
            {
                isInternal = true;
            }

            // Viewport of a scroll area and the stack of a tab widget hold the widgets of the application
            const QScrollArea* scrollArea = qobject_cast<const QScrollArea*>(parent);
            const bool isContentHolder = (scrollArea && scrollArea->viewport() == child) || child->objectName() == QLatin1String("qt_tabwidget_stackedwidget");

            WidgetInfo info;
            info.widget = child;
            info.path = path;
            info.parent = parentIndex;
            info.className = className;
            info.isLibrary = isLibrary;
            info.isTarget = !isInternal;
            info.description = describe(child, path, pageId);
            info.variant = getVariant(child, className);
            info.isVisible = child->isVisible();
            info.isEnabled = child->isEnabled();
            info.isFocusable = (child->focusPolicy() & Qt::TabFocus) == Qt::TabFocus;
            info.hasToolTip = !child->toolTip().isEmpty();
            info.classKey = QStringLiteral("%1<%2").arg(className, QString::fromLatin1(parent->metaObject()->className()));
            if (const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(child))
            {
                info.isView = view->model() && view->model()->rowCount(view->rootIndex()) > 0 && view->model()->columnCount(view->rootIndex()) > 0;
            }
            info.hasSignals = hasTestedSignals(child);
            infos.push_back(info);

            visit(child, path, static_cast<int>(infos.size()) - 1, isInternal, isContentHolder, pageId, infos);
        }
    }

    static bool hasTestedSignals(const QWidget* widget);

    static QString describe(QWidget* widget, const QList<int>& path, const QString& pageId)
    {
        QString sample;
        for (QWidget* current = widget; current; current = current->parentWidget())
        {
            const QVariant name = current->property("galleryName");
            if (name.isValid())
            {
                sample = name.toString();
                break;
            }
        }

        return QStringLiteral("%1 / '%2' / %3 '%4' [%5]").arg(pageId, sample, QString::fromLatin1(widget->metaObject()->className()), widget->objectName(), pathText(path));
    }

    static QString getVariant(QWidget* widget, const QString& className)
    {
        QStringList children;
        for (QObject* object : widget->children())
        {
            if (object->isWidgetType())
            {
                children << QString::fromLatin1(object->metaObject()->className());
            }
        }
        children.sort();
        children.removeDuplicates();

        const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget);
        const QString parentClass = widget->parentWidget() ? QString::fromLatin1(widget->parentWidget()->metaObject()->className()) : QString();

        // The context, which changes the behavior: the nearest ancestor of
        // a class of the library (a part of a composite widget), the delegate
        // of a view and the number of the connections of the signals (a widget,
        // to which the application or a composite widget has connected)
        QString libraryAncestor;
        for (const QWidget* ancestor = widget->parentWidget(); ancestor && libraryAncestor.isEmpty(); ancestor = ancestor->parentWidget())
        {
            if (QByteArrayView(ancestor->metaObject()->className()).startsWith("pdf::"))
            {
                libraryAncestor = QString::fromLatin1(ancestor->metaObject()->className());
            }
        }
        const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(widget);
        const QString delegateClass = (view && view->itemDelegate()) ? QString::fromLatin1(view->itemDelegate()->metaObject()->className()) : QString();

        return QStringLiteral("%1<%2|%3|%4%5%6%7%8|in %9").arg(className, parentClass, children.join(QLatin1Char(',')))
                .arg(widget->isEnabled() ? 1 : 0)
                .arg(button && button->isCheckable() ? 1 : 0)
                .arg(button && button->isChecked() ? 1 : 0)
                .arg(widget->focusPolicy() != Qt::NoFocus ? 1 : 0)
                .arg(widget->toolTip().isEmpty() ? 0 : 1)
                .arg(libraryAncestor) + QStringLiteral("|%1|%2").arg(delegateClass).arg(countConnections(widget));
    }

    /// Returns the number of the connections of the signals of the object
    static int countConnections(const QObject* object)
    {
        int count = 0;
        const QObjectPrivate::ConnectionData* data = QObjectPrivate::get(object)->connections.loadAcquire();
        const QObjectPrivate::SignalVector* vector = data ? data->signalVector.loadAcquire() : nullptr;
        for (int i = -1; vector && i < vector->count(); ++i)
        {
            for (const QObjectPrivate::Connection* connection = vector->at(i).first.loadRelaxed(); connection; connection = connection->nextConnectionList.loadRelaxed())
            {
                if (connection->receiver.loadRelaxed())
                {
                    ++count;
                }
            }
        }
        return count;
    }
};

/// Input of the user sent through the windows (as the platform sends it)
class Input
{
public:
    Input() = delete;

    /// Processes the events and the deferred deletes
    static void flush()
    {
        for (int i = 0; i < 3; ++i)
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    /// Makes the point of the widget visible and returns it in the coordinates of its window
    static std::optional<QPoint> reach(QWidget* widget, QPoint local)
    {
        if (!widget || !widget->isVisible())
        {
            return std::nullopt;
        }

        // Scroll areas from the inner one
        QPointer<QWidget> guard(widget);
        for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
        {
            if (QScrollArea* scrollArea = qobject_cast<QScrollArea*>(parent))
            {
                if (scrollArea->widget() && scrollArea->widget()->isAncestorOf(widget))
                {
                    scrollArea->ensureWidgetVisible(widget, 40, 40);
                }
            }
        }

        if (!guard)
        {
            return std::nullopt;
        }

        // The scrolled widget is painted (a delegate starts to watch its view at the first paint)
        QCoreApplication::processEvents();
        if (!guard)
        {
            return std::nullopt;
        }

        const QRegion visible = widget->visibleRegion();
        if (visible.isEmpty())
        {
            return std::nullopt;
        }
        if (!visible.contains(local))
        {
            local = visible.boundingRect().center();
        }

        QWidget* window = widget->window();
        const QPoint position = widget->mapTo(window, local);
        if (!window->windowHandle() || !window->rect().contains(position))
        {
            return std::nullopt;
        }
        return position;
    }

    static bool mouse(QWidget* widget, QPoint local, QTest::MouseAction action, Qt::MouseButton button = Qt::LeftButton)
    {
        const std::optional<QPoint> position = reach(widget, local);
        if (!position)
        {
            return false;
        }

        QWindow* window = widget->window()->windowHandle();
        switch (action)
        {
            case QTest::MouseMove:
                QTest::mouseMove(window, *position);
                break;
            case QTest::MousePress:
                QTest::mousePress(window, button, Qt::NoModifier, *position);
                s_pressed = true;
                break;
            case QTest::MouseRelease:
                QTest::mouseRelease(window, button, Qt::NoModifier, *position);
                s_pressed = false;
                break;
            case QTest::MouseClick:
                QTest::mouseClick(window, button, Qt::NoModifier, *position);
                break;
            case QTest::MouseDClick:
                QTest::mouseDClick(window, button, Qt::NoModifier, *position);
                break;
        }
        LifetimeContext::trace(QStringLiteral("mouse done"));
        return true;
    }

    /// Releases a held mouse button (its widget may have been destroyed)
    static void releaseAll()
    {
        if (s_pressed && s_spare && s_spare->windowHandle())
        {
            QTest::mouseRelease(s_spare->windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
            QTest::mouseRelease(s_spare->windowHandle(), Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
        }
        s_pressed = false;
    }

    /// Gives the keyboard focus to the widget, as the Tab key does
    static bool focus(QWidget* widget)
    {
        if (!widget || !widget->isVisible() || !widget->isEnabled() || widget->focusPolicy() == Qt::NoFocus)
        {
            return false;
        }

        // A closed popup may leave no active window: the keys would go nowhere
        QPointer<QWidget> guard(widget);
        QWidget* window = widget->window();
        if (!window->isActiveWindow())
        {
            window->activateWindow();
            [[maybe_unused]] const bool isActive = QTest::qWaitForWindowActive(window, 500);
            if (!guard)
            {
                return false;
            }
        }

        widget->setFocus(Qt::TabFocusReason);
        QWidget* focusWidget = QApplication::focusWidget();
        return focusWidget && (focusWidget == widget || widget->isAncestorOf(focusWidget) || widget->focusProxy() == focusWidget);
    }

    static void key(QWidget* widget, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = QString())
    {
        if (!widget || !widget->window()->windowHandle())
        {
            return;
        }

        QWindow* window = widget->window()->windowHandle();
        if (QWidget* popup = QApplication::activePopupWidget())
        {
            window = popup->windowHandle();
        }
        if (!window)
        {
            return;
        }

        if (text.isEmpty())
        {
            QTest::keyClick(window, key, modifiers);
        }
        else
        {
            QTest::sendKeyEvent(QTest::Click, window, key, text, modifiers);
        }
    }

    /// Turns the wheel over the point of the widget (one step down)
    static bool wheel(QWidget* widget, QPoint local)
    {
        if (!reach(widget, local))
        {
            return false;
        }
        QWheelEvent event(QPointF(local), QPointF(widget->mapToGlobal(local)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(widget, &event);
        return true;
    }

    static void sendToolTip(QWidget* widget, QPoint local)
    {
        if (widget && widget->isVisible())
        {
            QHelpEvent help(QEvent::ToolTip, local, widget->mapToGlobal(local));
            QApplication::sendEvent(widget, &help);
        }
    }

    /// Deletes the gestures of destroyed widgets, which the gesture manager of Qt
    /// keeps until it filters the next event of a widget with a gesture
    /// (QGestureManager::cleanupCachedGestures); the viewport of a scroll area grabs
    /// the pan gesture
    static void flushGestures()
    {
        if (s_gestureViewport)
        {
            QEvent event(QEvent::User);
            QApplication::sendEvent(s_gestureViewport, &event);
        }
    }

    static QPointer<QWidget> s_spare;
    static QPointer<QWidget> s_gestureViewport;
    static bool s_pressed;
};

QPointer<QWidget> Input::s_spare;
QPointer<QWidget> Input::s_gestureViewport;
bool Input::s_pressed = false;

/// Steps of the user input, which trigger the signals of the widgets
enum class Step
{
    Hover,
    Click,
    DoubleClick,
    RightClickClose,
    RightClickAccept,
    ClickLeading,
    ClickTrailing,
    ClickTrailingInner,
    ClickAction0,
    ClickAction1,
    ClickAction2,
    ToolTip,
    KeySpace,
    KeyReturn,
    KeyEnter,
    KeyEscape,
    KeyLeft,
    KeyRight,
    KeyUp,
    KeyDown,
    KeyHome,
    KeyEnd,
    KeyPageDown,
    KeyF2,
    KeyF4,
    KeyAltDown,
    KeyTypeA,
    KeyType1,
    KeyBackspace,
    KeyDelete,
    KeyMenuClose,
    KeyMenuAccept,
    KeyCopy,
    ClickAccept,        ///< Click, the popup opened by it is accepted (a color of a palette selected)
    KeyTypeReturn,      ///< "1" typed and Return (a field, a nested field with the focus)
    Wheel,
    Drag,               ///< Press, move by 30 px, release
    FocusOut,           ///< The focus given to the widget and moved away by Tab
    KeyF2CtrlO,         ///< F2 and Ctrl+S, F2 and Ctrl+O into an editor of an item (a shortcut, which another row has)
    Count
};

/// Performs the steps of the user input on a widget or on the first items of an item view
class StepRunner
{
public:
    StepRunner() = delete;

    static const char* getName(Step step)
    {
        static const char* names[] =
        {
            "Hover", "Click", "DoubleClick", "RightClickClose", "RightClickAccept", "ClickLeading", "ClickTrailing", "ClickTrailingInner",
            "ClickAction0", "ClickAction1", "ClickAction2", "ToolTip", "Space", "Return", "Enter", "Escape", "Left", "Right", "Up", "Down",
            "Home", "End", "PageDown", "F2", "F4", "Alt+Down", "TypeA", "Type1", "Backspace", "Delete", "MenuClose", "MenuAccept", "Ctrl+C",
            "ClickAccept", "Type1+Return", "Wheel", "Drag", "FocusOut", "F2+Ctrl+S/O"
        };
        static_assert(sizeof(names) / sizeof(names[0]) == size_t(Step::Count));
        return names[int(step)];
    }

    /// Target of the mouse: the widget, or the viewport and a rectangle of an item of a view
    static QWidget* getMouseTarget(QWidget* widget, QRect& rect, int row)
    {
        if (QAbstractItemView* view = qobject_cast<QAbstractItemView*>(widget))
        {
            QAbstractItemModel* model = view->model();
            if (!model || model->rowCount(view->rootIndex()) <= row || model->columnCount(view->rootIndex()) <= 0)
            {
                return nullptr;
            }
            const QModelIndex index = model->index(row, 0, view->rootIndex());
            rect = view->visualRect(index).intersected(view->viewport()->rect());
            return rect.isEmpty() ? nullptr : view->viewport();
        }

        rect = widget->rect();
        return widget;
    }

    static QPoint getActionPoint(QWidget* widget, int action)
    {
        QAbstractItemView* view = qobject_cast<QAbstractItemView*>(widget);
        pdf::PDFRowDelegate* delegate = view ? qobject_cast<pdf::PDFRowDelegate*>(view->itemDelegate()) : nullptr;
        if (!delegate || !view->model() || view->model()->rowCount(view->rootIndex()) == 0)
        {
            return QPoint(-1, -1);
        }

        const QModelIndex index = view->model()->index(0, 0, view->rootIndex());
        QStyleOptionViewItem option;
        option.initFrom(view->viewport());
        option.rect = view->visualRect(index);
        const QList<pdf::PDFRowDelegate::Action> actions = delegate->getActions(option, index);
        int count = 0;
        for (const pdf::PDFRowDelegate::Action& item : actions)
        {
            if (!item.rect.isEmpty() && count++ == action)
            {
                return item.rect.center().toPoint();
            }
        }
        return QPoint(-1, -1);
    }

    /// Performs the step
    /// \returns False, if the step cannot be performed on the widget
    static bool run(QWidget* widget, Step step)
    {
        if (!widget || !widget->isVisible())
        {
            return false;
        }

        QPointer<QWidget> guard(widget);
        LifetimeContext::setStep(QStringLiteral("%1 on %2 '%3'").arg(QString::fromLatin1(getName(step)), QString::fromLatin1(widget->metaObject()->className()), widget->objectName()));

        bool performed = false;
        QRect rect;
        switch (step)
        {
            case Step::Hover:
            case Step::Click:
            case Step::DoubleClick:
            case Step::RightClickClose:
            case Step::RightClickAccept:
            case Step::ClickLeading:
            case Step::ClickTrailing:
            case Step::ClickTrailingInner:
            case Step::ClickAccept:
            case Step::Wheel:
            case Step::Drag:
            {
                const int row = (step == Step::DoubleClick) ? 1 : 0;
                QWidget* target = getMouseTarget(widget, rect, row);
                if (!target && step == Step::DoubleClick)
                {
                    target = getMouseTarget(widget, rect, 0);
                }
                if (!target)
                {
                    break;
                }

                QPoint point = rect.center();
                if (step == Step::ClickLeading)
                {
                    point.setX(rect.left() + qMin(8, rect.width() / 2));
                }
                else if (step == Step::ClickTrailing)
                {
                    point.setX(rect.right() - qMin(12, rect.width() / 2));
                }
                else if (step == Step::ClickTrailingInner)
                {
                    point.setX(rect.right() - qMin(40, rect.width() / 2));
                }

                // The policy of the caller (Close by default, Keep in the states of
                // the item views), Accept for the step, which accepts the popup
                const EventWatcher::Policy previous = EventWatcher::getInstance()->getPolicy();
                const EventWatcher::Policy policy = (step == Step::RightClickAccept || step == Step::ClickAccept) ? EventWatcher::Policy::Accept : previous;
                EventWatcher::getInstance()->setPolicy(policy);
                switch (step)
                {
                    case Step::Hover:
                        performed = Input::mouse(target, point, QTest::MouseMove);
                        break;
                    case Step::DoubleClick:
                        performed = Input::mouse(target, point, QTest::MouseDClick);
                        break;
                    case Step::RightClickClose:
                    case Step::RightClickAccept:
                        performed = Input::mouse(target, point, QTest::MouseClick, Qt::RightButton);
                        break;
                    case Step::Wheel:
                        performed = Input::wheel(target, point);
                        break;
                    case Step::Drag:
                    {
                        QPointer<QWidget> targetGuard(target);
                        performed = Input::mouse(target, point, QTest::MousePress);
                        if (performed && targetGuard)
                        {
                            Input::mouse(targetGuard, point + QPoint(30, 0), QTest::MouseMove);
                        }
                        if (performed && targetGuard)
                        {
                            Input::mouse(targetGuard, point + QPoint(30, 0), QTest::MouseRelease);
                        }
                        Input::releaseAll();
                        break;
                    }
                    default:
                        performed = Input::mouse(target, point, QTest::MouseClick);
                        break;
                }
                EventWatcher::getInstance()->setPolicy(previous);
                break;
            }

            case Step::ClickAction0:
            case Step::ClickAction1:
            case Step::ClickAction2:
            {
                const QPoint point = getActionPoint(widget, int(step) - int(Step::ClickAction0));
                if (point.x() >= 0)
                {
                    QAbstractItemView* view = static_cast<QAbstractItemView*>(widget);
                    performed = Input::mouse(view->viewport(), point, QTest::MouseMove) && Input::mouse(view->viewport(), point, QTest::MouseClick);
                }
                break;
            }

            case Step::ToolTip:
            {
                // The tooltip of the first action of an item of a delegate, or of the item / widget
                QWidget* target = getMouseTarget(widget, rect, 0);
                const QPoint actionPoint = getActionPoint(widget, 0);
                if (target)
                {
                    Input::sendToolTip(target, actionPoint.x() >= 0 ? actionPoint : rect.center());
                    performed = true;
                }
                break;
            }

            default:
            {
                // A nested field with the focus keeps it (a field of a composite
                // widget, the inline editor of a row, an editor of an item of
                // a view), the keys go to it; a nested button gives it back
                QWidget* current = QApplication::focusWidget();
                const QWidget* currentParent = current ? current->parentWidget() : nullptr;
                const bool isEditor = currentParent && qobject_cast<const QAbstractItemView*>(currentParent->parentWidget()) &&
                                      static_cast<const QAbstractItemView*>(currentParent->parentWidget())->viewport() == currentParent;
                const bool isNestedFocus = current && current != widget && widget->isAncestorOf(current) && current->isVisible() &&
                                           (current->testAttribute(Qt::WA_InputMethodEnabled) || isEditor);
                if (!isNestedFocus && !Input::focus(widget))
                {
                    break;
                }

                const EventWatcher::Policy previous = EventWatcher::getInstance()->getPolicy();
                if (step == Step::KeyMenuAccept)
                {
                    EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Accept);
                }

                performed = true;
                switch (step)
                {
                    case Step::KeySpace:        Input::key(widget, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")); break;
                    case Step::KeyReturn:       Input::key(widget, Qt::Key_Return); break;
                    case Step::KeyEnter:        Input::key(widget, Qt::Key_Enter, Qt::KeypadModifier); break;
                    case Step::KeyEscape:       Input::key(widget, Qt::Key_Escape); break;
                    case Step::KeyLeft:         Input::key(widget, Qt::Key_Left); break;
                    case Step::KeyRight:        Input::key(widget, Qt::Key_Right); break;
                    case Step::KeyUp:           Input::key(widget, Qt::Key_Up); break;
                    case Step::KeyDown:         Input::key(widget, Qt::Key_Down); break;
                    case Step::KeyHome:         Input::key(widget, Qt::Key_Home); break;
                    case Step::KeyEnd:          Input::key(widget, Qt::Key_End); break;
                    case Step::KeyPageDown:     Input::key(widget, Qt::Key_PageDown); break;
                    case Step::KeyF2:           Input::key(widget, Qt::Key_F2); break;
                    case Step::KeyF4:           Input::key(widget, Qt::Key_F4); break;
                    case Step::KeyAltDown:      Input::key(widget, Qt::Key_Down, Qt::AltModifier); break;
                    case Step::KeyTypeA:        Input::key(widget, Qt::Key_A, Qt::NoModifier, QStringLiteral("a")); break;
                    case Step::KeyType1:        Input::key(widget, Qt::Key_1, Qt::NoModifier, QStringLiteral("1")); break;
                    case Step::KeyBackspace:    Input::key(widget, Qt::Key_Backspace); break;
                    case Step::KeyDelete:       Input::key(widget, Qt::Key_Delete); break;
                    case Step::KeyMenuClose:
                    case Step::KeyMenuAccept:   Input::key(widget, Qt::Key_Menu); break;
                    case Step::KeyCopy:         Input::key(widget, Qt::Key_C, Qt::ControlModifier); break;
                    case Step::KeyTypeReturn:
                    {
                        QPointer<QWidget> widgetGuard(widget);
                        Input::key(widget, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
                        if (widgetGuard)
                        {
                            Input::key(widgetGuard, Qt::Key_Return);
                        }
                        break;
                    }
                    case Step::FocusOut:        Input::key(widget, Qt::Key_Tab); break;
                    case Step::KeyF2CtrlO:
                    {
                        // F2 opens an editor of an item, which records the
                        // shortcuts Ctrl+S and Ctrl+O (common shortcuts, one of
                        // them belongs to another row); without an editor the
                        // keys are not sent (the actions of the window would get them)
                        QPointer<QWidget> widgetGuard(widget);
                        for (const Qt::Key shortcutKey : { Qt::Key_S, Qt::Key_O })
                        {
                            if (!widgetGuard)
                            {
                                break;
                            }
                            Input::key(widgetGuard, Qt::Key_F2);
                            QCoreApplication::processEvents();
                            QWidget* editor = QApplication::focusWidget();
                            QWidget* editorParent = editor ? editor->parentWidget() : nullptr;
                            const QAbstractItemView* editorView = editorParent ? qobject_cast<const QAbstractItemView*>(editorParent->parentWidget()) : nullptr;
                            if (widgetGuard && editorView && editorView->viewport() == editorParent)
                            {
                                Input::key(editor, shortcutKey, Qt::ControlModifier);
                            }
                        }
                        break;
                    }
                    default:                    performed = false; break;
                }
                EventWatcher::getInstance()->setPolicy(previous);
                break;
            }
        }

        QCoreApplication::processEvents();
        if (QWidget* focusWidget = QApplication::focusWidget())
        {
            LifetimeContext::trace(QStringLiteral("focus after the step: %1 '%2'").arg(QString::fromLatin1(focusWidget->metaObject()->className()), focusWidget->objectName()));
        }
        return performed;
    }
};

/// Generic receiver of signals: records them, or deletes an object from inside of the signal
class SignalReceiver : public QObject
{
    Q_OBJECT

public:
    explicit SignalReceiver() = default;

    /// Signal, which has been fired: the identifier of the emitter (setEmitterId) and the method index
    struct Fired
    {
        int emitter = -1;
        int method = -1;
    };

    /// Connects the signals of the object
    void connectSignals(QObject* sender, const QList<QMetaMethod>& signalList)
    {
        const QMetaMethod slot = staticMetaObject.method(staticMetaObject.indexOfSlot("onSignal()"));
        for (const QMetaMethod& signal : signalList)
        {
            m_connections << QObject::connect(sender, signal, this, slot, Qt::DirectConnection);
        }
    }

    void disconnectAll()
    {
        for (const QMetaObject::Connection& connection : std::as_const(m_connections))
        {
            QObject::disconnect(connection);
        }
        m_connections.clear();
    }

    /// The fired signals of the object are recorded with the identifier
    void setEmitterId(const QObject* object, int emitter) { m_emitterIds[object] = emitter; }

    /// The object is deleted at the first signal
    void setVictim(QObject* object)
    {
        m_victim = object;
        m_deleted = false;
        m_skipReason.clear();
    }

    bool hasDeleted() const { return m_deleted; }

    /// A disarmed receiver only records the signals
    void setArmed(bool armed) { m_armed = armed; }

    /// The victim has not been deleted, because Qt does not support it in the signal
    bool hasSkipped() const { return !m_skipReason.isEmpty(); }
    const QString& getSkipReason() const { return m_skipReason; }

    /// Signals fired since the last call
    QList<Fired> takeFired()
    {
        QList<Fired> fired;
        fired.swap(m_fired);
        return fired;
    }

    /// Returns the signals of the object tested by deletion from inside of them:
    /// the signals of the classes of the library, of QAbstractButton and of
    /// QAbstractItemView, and the signals of the other bases of Qt of a class of
    /// the library, which the library emits itself (FLUENT_LIBRARY_EMITS, found
    /// in its sources at the build: Qt itself uses many of its objects after
    /// emitting its own signals, which the library cannot prevent)
    static QList<QMetaMethod> getTestedSignals(const QObject* object)
    {
        QList<QMetaMethod> result;
        for (const QMetaObject* metaObject = object->metaObject(); metaObject; metaObject = metaObject->superClass())
        {
            const bool isLibrary = QByteArrayView(metaObject->className()).startsWith("pdf::");
            const bool isStandard = metaObject == &QAbstractButton::staticMetaObject || metaObject == &QAbstractItemView::staticMetaObject;
            for (int i = metaObject->methodOffset(); i < metaObject->methodCount(); ++i)
            {
                const QMetaMethod method = metaObject->method(i);
                if (method.methodType() != QMetaMethod::Signal)
                {
                    continue;
                }

                if (isLibrary || isStandard || LibrarySignals::isEmittedByLibrary(object->metaObject(), method.name()))
                {
                    result << method;
                }
            }
        }
        return result;
    }

public Q_SLOTS:
    void onSignal()
    {
        m_fired << Fired{ m_emitterIds.value(sender(), -1), senderSignalIndex() };

        if (m_armed && m_victim && !m_deleted && m_skipReason.isEmpty())
        {
            // Synchronous deletion from inside of the signal: an application may do it,
            // unless Qt itself is inside of its own signal of the object (or of
            // a descendant) and uses the object after it
            QString reason;
            if (ObjectTracker::isDeletionUnsupported(m_victim, &reason))
            {
                m_skipReason = reason;
                return;
            }

            m_deleted = true;
            QObject* victim = m_victim.data();
            m_victim = nullptr;
            delete victim;
        }
    }

private:
    QList<QMetaObject::Connection> m_connections;
    QHash<const QObject*, int> m_emitterIds;
    QPointer<QObject> m_victim;
    bool m_deleted = false;
    bool m_armed = true;
    QString m_skipReason;
    QList<Fired> m_fired;
};

bool WidgetCatalog::hasTestedSignals(const QWidget* widget)
{
    if (!SignalReceiver::getTestedSignals(widget).isEmpty())
    {
        return true;
    }

    const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(widget);
    return view && view->itemDelegate() && !SignalReceiver::getTestedSignals(view->itemDelegate()).isEmpty();
}

/// Finds out, whether an arrow key reaches the widget behind all event filters
/// of the library (the library moves the focus in its groups itself, then the
/// navigation of Qt in QAbstractButton::keyPressEvent is not reached): the
/// probe is the last filter of the widget
class ArrowProbe : public QObject
{
public:
    explicit ArrowProbe() = default;

    void attach(QWidget* widget)
    {
        if (!widget || m_widgets.contains(widget))
        {
            return;
        }
        widget->installEventFilter(this);
        QObjectPrivate* d = QObjectPrivate::get(widget);
        if (d->extraData)
        {
            // installEventFilter puts the filter first, the probe is moved to the end
            QList<QPointer<QObject>>& filters = d->extraData->eventFilters;
            filters.removeAll(QPointer<QObject>(this));
            filters.append(QPointer<QObject>(this));
        }
        m_widgets << widget;
    }

    void detach()
    {
        for (const QPointer<QWidget>& widget : std::as_const(m_widgets))
        {
            if (widget)
            {
                widget->removeEventFilter(this);
            }
        }
        m_widgets.clear();
    }

    /// Returns the widgets, which an arrow key has reached
    const QList<QPointer<QWidget>>& getReached() const { return m_reached; }

    ~ArrowProbe() override { detach(); }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress && watched->isWidgetType())
        {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)
            {
                m_reached << QPointer<QWidget>(static_cast<QWidget*>(watched));
            }
        }
        return false;
    }

private:
    QList<QPointer<QWidget>> m_widgets;
    QList<QPointer<QWidget>> m_reached;
};

/// Closes the popups and the tooltips left by a step of the test
class PopupCloser
{
public:
    PopupCloser() = delete;

    static void closeAll()
    {
        for (int i = 0; i < 5 && QApplication::activePopupWidget(); ++i)
        {
            QApplication::activePopupWidget()->close();
        }
        for (int i = 0; i < 5 && QApplication::activeModalWidget(); ++i)
        {
            QApplication::activeModalWidget()->close();
        }
        pdf::PDFToolTip::hideText();
        QToolTip::hideText();
        Input::releaseAll();
        Input::flush();
    }
};

/// Checks the state of the application after an instance of a page
class InstanceCheck
{
public:
    explicit InstanceCheck(QString description) :
        m_description(std::move(description))
    {
        Input::flush();
        m_objects = ObjectTracker::getLiveObjects();
        m_census = ObjectTracker::takeCensus(m_objects, getSingletons());
        m_widgetCount = int(QApplication::allWidgets().size());
        m_topLevelCount = int(QApplication::topLevelWidgets().size());

        // Errors and warnings of the previous instance have been taken by its
        // finish; what is here now happened outside of any instance (creation of
        // a catalog, a step of the test function) and belongs to this one
        for (const QString& problem : ObjectTracker::takeErrors() + MessageCollector::takeMessages())
        {
            m_previousProblems << QStringLiteral("before the instance: %1").arg(problem);
        }
        EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Close);
        EventWatcher::getInstance()->setAction(EventWatcher::Action());
    }

    /// Ends the instance: the windows of the page must be gone
    /// \returns Description of the problems (empty, when there is none)
    QString finish()
    {
        Input::releaseAll();
        EventWatcher::getInstance()->setAction(EventWatcher::Action());
        EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Close);
        Input::flush();

        QStringList problems = m_previousProblems;

        // A popup or a window, whose owner has been destroyed, must be gone
        for (QWidget* window : QApplication::topLevelWidgets())
        {
            if (window->isVisible() && window != Input::s_spare)
            {
                problems << QStringLiteral("window left open: %1 '%2'").arg(QString::fromLatin1(window->metaObject()->className()), window->objectName());
            }
        }

        // Close what is left, so the next instances are not affected
        for (int i = 0; i < 5 && QApplication::activePopupWidget(); ++i)
        {
            QApplication::activePopupWidget()->close();
        }
        for (QWidget* window : QApplication::topLevelWidgets())
        {
            if (window->isVisible() && window != Input::s_spare)
            {
                window->hide();
            }
        }
        pdf::PDFToolTip::hideText();
        QToolTip::hideText();
        Input::flush();
        Input::flushGestures();
        Input::flush();

        problems << ObjectTracker::takeErrors();
        problems << MessageCollector::takeMessages();

        const int widgetCount = int(QApplication::allWidgets().size());
        const int topLevelCount = int(QApplication::topLevelWidgets().size());
        if (widgetCount > m_widgetCount || topLevelCount > m_topLevelCount)
        {
            problems << QStringLiteral("widgets left: %1 -> %2, top level widgets %3 -> %4").arg(m_widgetCount).arg(widgetCount).arg(m_topLevelCount).arg(topLevelCount);
        }

        const QStringList created = ObjectTracker::describeNewObjects(m_objects, getSingletons(), 10);
        if (!created.isEmpty())
        {
            problems << QStringLiteral("objects left: %1").arg(created.join(QStringLiteral(", ")));
        }

        // A connection to a long-lived object without a context object of the
        // page (a lambda connected by two arguments), or an event filter left
        // on it, stays after the page has been destroyed
        problems << ObjectTracker::compareCensus(m_census, ObjectTracker::takeCensus(m_objects, getSingletons()));

        ObjectTracker::forgetDestroyed();
        EventWatcher::getInstance()->clearShown();
        LifetimeContext::setStep(QString());

        if (problems.isEmpty())
        {
            return QString();
        }
        return QStringLiteral("%1:\n  %2").arg(m_description, problems.join(QStringLiteral("\n  ")));
    }

    static QSet<QByteArray> getSingletons()
    {
        // Objects created once, when they are needed for the first time, and
        // kept for the whole application: the window of the tooltip of the
        // library and its platform window, the input device of Qt (created at
        // the first input event), the clipboard, the style of the application
        // (replaced by the test) and its focus frame, the data of the clipboard
        // (the value copied by a property row), the drag manager of Qt, the
        // global objects of Qt - the pixmap cache, the global thread pool, the
        // empty model of the views without a model, the timers and the driver of
        // the animations of the thread - and the event loop of QTest::qWait. A process runs any
        // single function:tag, so they cannot be created in advance by the pages
        return { QByteArrayLiteral("pdf::PDFToolTipWindow"), QByteArrayLiteral("QWidgetWindow"), QByteArrayLiteral("QTipLabel"),
                 QByteArrayLiteral("QInputDevice"), QByteArrayLiteral("QPointingDevice"), QByteArrayLiteral("QClipboard"),
                 QByteArrayLiteral("pdf::PDFWidgetStyle"), QByteArrayLiteral("pdf::PDFWidgetStyleImpl"), QByteArrayLiteral("QFocusFrame"),
                 QByteArrayLiteral("QMimeData"), QByteArrayLiteral("QDragManager"), QByteArrayLiteral("QPMCache"),
                 QByteArrayLiteral("QThreadPool"), QByteArrayLiteral("QEmptyItemModel"), QByteArrayLiteral("QAnimationTimer"),
                 QByteArrayLiteral("QUnifiedTimer"), QByteArrayLiteral("QDefaultAnimationDriver"),
                 QByteArrayLiteral("QTestEventLoop") };
    }

private:
    QString m_description;
    QStringList m_previousProblems;
    ObjectSet m_objects;
    ObjectTracker::CensusMap m_census;
    int m_widgetCount = 0;
    int m_topLevelCount = 0;
};

/// Information about a destroyed widget, used afterwards
struct DeletedWidget
{
    QPointer<QWidget> parent;
    QRect geometry;
};

class LifetimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void pageIdentifiers();
    void librarySamples();
    void detectorSelfTest();

    void pageDeletedWhileActive_data() { addPageRows(); }
    void pageDeletedWhileActive();

    void childDeletedFirst_data() { addPageRows(); }
    void childDeletedFirst();

    void deletionOrders_data() { addPageRows(); }
    void deletionOrders();

    void deletedFromOwnSignal_data() { addPageRows(); }
    void deletedFromOwnSignal();

    void popupsOpen_data() { addPageRows(); }
    void popupsOpen();

    void itemViews_data() { addPageRows(); }
    void itemViews();

    // Regressions of the bugs found by the test
    void regressionPasswordButtonDeleted();
    void regressionMessageBoxParts();
    void regressionMessageBoxParentDeleted();
    void regressionTabCloseButtonDeleted();
    void regressionKeyValueListMenu();
    void regressionMessageBoxWithoutButtons();
    void regressionMessageBoxDeletedInExec();
    void regressionStyleReplacedInForwardedClick();
    void regressionCalendarViewportReplaced();
    void regressionPaletteDeletedBySwatch();
    void regressionFocusFrameReparented();

private:
    /// One data row for each page of the gallery, named by the identifier of
    /// the page; known without initTestCase, so -datatags lists them and every
    /// function:tag runs as its own process
    static void addPageRows();

    /// Uses what is left of the page after deletions: repaint, hover, keyboard,
    /// Tab, show / hide, resize, theme, direction of the layout, font and style
    void useRest(PageHost& host, const std::vector<DeletedWidget>& deleted, bool full);

    /// Light use after a single deletion: events, repaint and hover around the widget
    static void useAround(const DeletedWidget& deleted);

    /// The objects of the application, which are not widgets (groups, actions,
    /// models, delegates, effects, timers, layouts), deleted from a page
    void childObjectsDeleted(int page, const std::vector<WidgetInfo>& catalog);

    /// Widgets of the library moved to another window and deleted in both orders
    void reparented(int page, const std::vector<WidgetInfo>& catalog);

    static DeletedWidget remember(QWidget* widget);

    /// Returns the widget of the catalog in an instance of the page; an error
    /// (and nullptr), when its path leads to a widget of another class
    static QWidget* resolve(const PageHost& host, const WidgetInfo& info);

    /// Restriction of the widgets by the environment variable PDF4QT_LIFETIME_ONLY
    /// (paths separated by commas, for the investigation of a failure)
    static bool isSelected(const WidgetInfo& info);

    /// Creates the page once and finds its widgets (a failure of the test, when
    /// the instance has a problem)
    static std::vector<WidgetInfo> buildCatalog(int page);

    /// Key of the deduplication of the widgets
    enum class Key
    {
        Variant,        ///< Class, parent, children and state
        ClassAndParent, ///< Class and the class of the parent
        Class           ///< Class
    };

    /// Returns the targets passing the filter, one for each key
    static std::vector<int> selectVariants(const std::vector<WidgetInfo>& catalog, const std::function<bool(const WidgetInfo&)>& filter, Key key);

    /// Returns the index of the nearest ancestor, which can be destroyed (-1 for the page)
    static int getTargetAncestor(const std::vector<WidgetInfo>& catalog, int index);

    /// Starts the timers of the library on the page by the hints of the samples
    static void startTimers(QWidget* page);

    /// Returns the visible focus frame of the style attached to the widget
    static QFocusFrame* findFocusFrame(QWidget* widget);

    /// Checks, that the focus ring of the widget with the keyboard focus is shown:
    /// the keyboard modality of the style is on, and a widget with the ring
    /// outside of its shape has the focus frame of the style
    /// \returns Description of the problem, an empty string when there is none
    static QString checkFocusRing(QWidget* focusWidget);

    /// Object, which emits a signal: a widget of the page, a delegate of a view
    /// of the page, or a widget of a popup of a class of the library
    struct Emitter
    {
        enum class Kind
        {
            Widget,
            Delegate,
            Popup
        };

        Kind kind = Kind::Widget;
        int widget = -1;                ///< Widget: the emitter, Delegate: its view (index in the catalog)
        int column = -1;                ///< Delegate: the delegate of the column (setItemDelegateForColumn)
        int row = -1;                   ///< Delegate: the delegate of the row (setItemDelegateForRow)
        QString popupClass;             ///< Popup: class of the window of the popup
        QList<int> popupPath;           ///< Popup: path of the widget from the window
        const QMetaObject* metaObject = nullptr;
        QString description;
    };

    /// Signal of an object of the page or of a popup found by the user input on a widget
    struct SignalTrial
    {
        int input = -1;                 ///< Widget, which gets the input (index in the catalog)
        QList<Step> steps;              ///< Steps of the input, the last one emits the signal
        Emitter emitter;
        bool isViewSignal = false;      ///< Signal of QAbstractItemView
        bool isQtUnsafe = false;        ///< Qt uses the sender after the signal (see runSignalTrials)
        int method = -1;                ///< Index of the method of the signal
        QString signature;
        int history = 0;                ///< Number of the inputs of the discovery before this one (m_signalHistory)
        QString popupInputClass;        ///< Input in a popup: the steps open the popup of this class, its widget is clicked
        QList<int> popupInputPath;      ///< Input in a popup: path of the clicked widget from the window of the popup
    };

    /// Runs the steps on the owner, the last one opens a popup of the class (the
    /// popup is kept open), and clicks the widget of the popup at the path
    /// \returns True, if the widget has been clicked
    /// \param deleter Receiver of the signal, disarmed for the steps before the opening one (or nullptr)
    static bool runPopupInput(QWidget* owner, const QList<Step>& steps, const QString& popupClass, const QList<int>& path, SignalReceiver* deleter);

    /// Inputs of the discovery of the signals in their order: the widget and
    /// its performed steps (a signal, which depends on the state left by the
    /// inputs before, is repeated after them)
    std::vector<std::pair<int, QList<Step>>> m_signalHistory;

    /// Signals of the classes of the library declared by the visible and enabled
    /// objects of the page and of its popups (signal -> an object, which has it)
    std::map<QString, QString> m_librarySignals;

    /// Who is deleted from inside of a signal
    enum class Victim
    {
        Sender,     ///< The object, which emits the signal
        Parent,     ///< The nearest ancestor of the emitter, which an application may delete (for a delegate: its view, for a popup: its owner)
        Host,       ///< The window with the page
        Delegate,   ///< The delegate of the view (signals of QAbstractItemView)
        Model       ///< The model of the view (signals of QAbstractItemView)
    };

    std::vector<SignalTrial> discoverSignals(int page, const std::vector<WidgetInfo>& catalog);
    void runSignalTrials(int page, const std::vector<WidgetInfo>& catalog, const std::vector<SignalTrial>& trials, Victim victim);

    /// Returns the delegate of the view of the emitter
    static QAbstractItemDelegate* getDelegate(QAbstractItemView* view, const Emitter& emitter);

    /// Returns true, if the button is in a group of buttons, where an arrow key
    /// reaching QAbstractButton::keyPressEvent moves the focus by Qt
    /// (QAbstractButtonPrivate::moveFocus)
    static bool reachesButtonMoveFocus(const QWidget* widget);

    /// Popup found by the user input
    struct PopupCase
    {
        enum class Kind
        {
            Window,     ///< Menu, dialog, palette, popup of a combo box, calendar
            ToolTip,    ///< Tooltip window
            Editor      ///< Editor of an item of a view
        };

        int widget = -1;
        QList<Step> steps;              ///< Steps of the input, the last one opens the popup
        Kind kind = Kind::Window;
        QString popupClass;
    };

    /// What is done, while the popup is open
    enum class PopupAction
    {
        DeleteOwner,
        DeletePopup,
        DeleteHost,
        HideHost,
        Accept,
        Environment,    ///< Theme, direction, style of the application and language changed, then accepted, the owner deleted
        MarginClick     ///< A click into the shadow margin of the popup window, then the owner deleted
    };

    std::vector<PopupCase> discoverPopups(int page, const std::vector<WidgetInfo>& catalog);
    /// \param ownerWidget Owner of the popup (resolved before any deletion on the page)
    /// \param isPopupOpened The popup has been opened again and the action performed
    QString runPopupCase(PageHost& host, QWidget* ownerWidget, const std::vector<WidgetInfo>& catalog, const PopupCase& popupCase, PopupAction action, bool* isPopupOpened);

    QPointer<QWidget> m_spare;
    ObjectSet m_initialObjects;     ///< Objects living at the end of initTestCase
    QElapsedTimer m_clock;
    QElapsedTimer m_functionClock;
    std::map<QString, int> m_counters;

    /// Page created after each function to check, that the application still
    /// works (progress bars: the animations of the style)
    static constexpr int CHECK_PAGE = 8;
};

void LifetimeTest::addPageRows()
{
    QTest::addColumn<int>("page");
    const int count = pdffluentgallery::GalleryPages::getPageCount();
    for (int i = 0; i < count; ++i)
    {
        QTest::newRow(qPrintable(pdffluentgallery::GalleryPages::getPageId(i))) << i;
    }
}

void LifetimeTest::initTestCase()
{
    MessageCollector::install();
    AccessibilityClient::install();
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QVERIFY(pdf::PDFWidgetStyle::isActive());
    qApp->installEventFilter(EventWatcher::getInstance());

    // Small window, which stays for the whole test (release of the mouse buttons,
    // a viewport with a gesture)
    m_spare = new QWidget();
    m_spare->setObjectName(QStringLiteral("lifetime_spare"));
    QScrollArea* spareScrollArea = new QScrollArea(m_spare);
    spareScrollArea->setGeometry(0, 0, 64, 64);
    Input::s_gestureViewport = spareScrollArea->viewport();
    m_spare->resize(64, 64);
    Input::s_spare = m_spare;
    EventWatcher::getInstance()->setSpareWindow(m_spare);
    m_spare->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_spare));

    // The lazily created singletons (the window of the tooltip, the caches of
    // the page used by cleanup) are created here, before the counting starts.
    // Every function creates its page once before its first check.
    {
        PageHost host(CHECK_PAGE);
        QVERIFY(host.getWindow());
        host.getWindow()->grab();
    }
    pdf::PDFToolTip::showText(QPoint(10, 10), QStringLiteral("Warm-up"));
    pdf::PDFToolTip::hideText();
    QToolTip::showText(QPoint(10, 10), QStringLiteral("Warm-up"));
    QToolTip::hideText();
    Input::flush();
    const QStringList warmUpProblems = ObjectTracker::takeErrors() + MessageCollector::takeMessages();
    QVERIFY2(warmUpProblems.isEmpty(), qPrintable(warmUpProblems.join(QStringLiteral("\n  "))));
    m_initialObjects = ObjectTracker::getLiveObjects();
    m_clock.start();
}

void LifetimeTest::cleanupTestCase()
{
    for (const auto& counter : m_counters)
    {
        qInfo("%s: %d", qPrintable(counter.first), counter.second);
    }
    qInfo("total time %lld ms, updates of the accessibility %d", m_clock.elapsed(), AccessibilityClient::getUpdateCount());

    // The delayed actions (single shot timers of the style and of the library)
    // run, when everything is destroyed, and nothing created by the functions
    // of this process is left (the process may run a single function:tag)
    QTest::qWait(400);
    Input::flush();
    ObjectTracker::s_allowPendingTimers = false;
    const QStringList left = ObjectTracker::describeNewObjects(m_initialObjects, InstanceCheck::getSingletons(), 10);
    ObjectTracker::s_allowPendingTimers = true;
    const QStringList errors = ObjectTracker::takeErrors() + MessageCollector::takeMessages();
    delete m_spare.data();
    QVERIFY2(left.isEmpty(), qPrintable(QStringLiteral("objects left at the end: %1").arg(left.join(QStringLiteral(", ")))));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("\n  "))));
}

void LifetimeTest::pageIdentifiers()
{
    // The identifiers of the data rows (GalleryPages::getPageId) are the
    // identifiers of the created pages
    const int count = pdffluentgallery::GalleryPages::getPageCount();
    QVERIFY(count > CHECK_PAGE);
    QSet<QString> ids;
    InstanceCheck check(QStringLiteral("identifiers of the pages"));
    for (int i = 0; i < count; ++i)
    {
        const QString id = pdffluentgallery::GalleryPages::getPageId(i);
        QString createdId;
        {
            PageHost host(i);
            createdId = host.getId();
        }
        QCOMPARE(createdId, id);
        QVERIFY(!id.isEmpty() && !ids.contains(id));
        ids.insert(id);
    }
    QVERIFY(pdffluentgallery::GalleryPages::getPageId(-1).isEmpty());
    QVERIFY(pdffluentgallery::GalleryPages::getPageId(count).isEmpty());
    QVERIFY(!pdffluentgallery::GalleryPages::createPage(count, nullptr).widget);

    QStringList createdIds;
    for (const pdffluentgallery::GalleryPages::Page& page : pdffluentgallery::GalleryPages::createPages(nullptr))
    {
        createdIds << page.id;
        delete page.widget;
    }
    QCOMPARE(createdIds.size(), count);
    for (int i = 0; i < count; ++i)
    {
        QCOMPARE(createdIds[i], pdffluentgallery::GalleryPages::getPageId(i));
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::detectorSelfTest()
{
    // The checks of an instance find, what they are made for (also in Release on Windows)
    auto expectProblem = [](const QString& problems, const QString& expected)
    {
        if (!problems.contains(expected))
        {
            QTest::qFail(qPrintable(QStringLiteral("expected '%1' in the problems: '%2'").arg(expected, problems)), __FILE__, __LINE__);
        }
    };

    // A lambda connected to a long-lived object without a context object of the page
    {
        InstanceCheck check(QStringLiteral("self test: connection left"));
        QMetaObject::Connection connection;
        {
            QWidget widget;
            QWidget* pointer = &widget;
            connection = QObject::connect(pdf::PDFThemeManager::getInstance(), &pdf::PDFThemeManager::themeChanged, [pointer]() { Q_UNUSED(pointer); });
        }
        const QString problems = check.finish();
        QObject::disconnect(connection);
        expectProblem(problems, QStringLiteral("connections of the signals of the long-lived pdf::PDFThemeManager"));
        if (QTest::currentTestFailed())
        {
            return;
        }
    }

    // An event filter left on the application (the filter itself lives on)
    {
        QObject filter;
        InstanceCheck check(QStringLiteral("self test: event filter left"));
        qApp->installEventFilter(&filter);
        const QString problems = check.finish();
        qApp->removeEventFilter(&filter);
        expectProblem(problems, QStringLiteral("event filters of the long-lived QApplication"));
        if (QTest::currentTestFailed())
        {
            return;
        }
    }

    // A widget left, a window left open, a warning of Qt
    {
        InstanceCheck check(QStringLiteral("self test: widget, window and warning"));
        QPointer<QWidget> window = new QWidget();
        window->setObjectName(QStringLiteral("selfTestWindow"));
        EventWatcher::getInstance()->setHostWindow(window);    // not closed as a popup
        window->show();
        qWarning("self test warning");
        const QString problems = check.finish();
        delete window.data();
        Input::flush();
        expectProblem(problems, QStringLiteral("window left open: QWidget 'selfTestWindow'"));
        expectProblem(problems, QStringLiteral("objects left: "));
        expectProblem(problems, QStringLiteral("QWidget 'selfTestWindow' (parent none"));
        expectProblem(problems, QStringLiteral("warning 'self test warning'"));
        if (QTest::currentTestFailed())
        {
            return;
        }
    }

    // A problem outside of any instance is reported by the next instance
    {
        qWarning("self test warning outside");
        InstanceCheck check(QStringLiteral("self test: problem before the instance"));
        expectProblem(check.finish(), QStringLiteral("before the instance: warning 'self test warning outside'"));
    }
}

void LifetimeTest::librarySamples()
{
    // Every class of the library with a meta object (the list is generated from
    // the sources by UnitTests/fluentclasses.cmake) has a live sample on a page
    // of the gallery, which the other functions destroy in every order. The
    // exceptions are the classes, which cannot be a sample: bases, singletons,
    // and popups / editors, which the test opens from their owner on a page.
    struct Exclusion
    {
        const char* className;
        const char* owner;      ///< Class on a page, which opens the popup / editor (nullptr: none)
        const char* reason;
    };
    static const Exclusion exclusions[] =
    {
        { "pdf::PDFComponentButton", nullptr, "base of the rows and cards, which are on the pages" },
        { "pdf::PDFRowButton", nullptr, "base of the rows, which are on the pages" },
        { "pdf::PDFCardButton", nullptr, "base of the tiles and cards, which are on the pages" },
        { "pdf::PDFRowDelegate", nullptr, "abstract base of the delegates, which are on the pages" },
        { "pdf::PDFThemeManager", nullptr, "singleton of the application, not a widget" },
        { "pdf::PDFWidgetStyle", nullptr, "style of the application, replaced by pageDeletedWhileActive" },
        { "pdf::PDFWidgetStyleImpl", nullptr, "state of the style, replaced with it" },
        { "pdf::PDFToolTipWindow", "pdf::PDFIconButton", "window of PDFToolTip, shown by popupsOpen for every widget with a tooltip" },
        { "pdf::PDFShortcutRecorder", "pdf::PDFShortcutDelegate", "editor of the shortcut delegate (popupsOpen, itemViews)" },
        { "pdf::PDFBooleanComboBox", "pdf::PDFItemDelegate", "editor of a boolean of PDFItemEditorFactory (itemViews, popupsOpen)" },
        { "pdf::PDFUIntSpinBox", "pdf::PDFItemDelegate", "editor of an unsigned number of PDFItemEditorFactory (itemViews, popupsOpen)" },
    };

    std::map<QString, int> onPages;
    InstanceCheck check(QStringLiteral("classes of the library on the pages"));
    const int count = pdffluentgallery::GalleryPages::getPageCount();
    for (int i = 0; i < count; ++i)
    {
        PageHost host(i);
        QVERIFY(host.getPage());
        for (const QObject* object : host.getPage()->findChildren<QObject*>())
        {
            ++onPages[QString::fromLatin1(object->metaObject()->className())];
        }
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));

    QSet<QString> libraryClasses;
    for (const FluentLibraryClass& item : FLUENT_LIBRARY_CLASSES)
    {
        libraryClasses.insert(QString::fromLatin1(item.className));
    }
    QVERIFY(libraryClasses.size() > 40);

    QStringList errors;
    QSet<QString> excluded;
    for (const Exclusion& exclusion : exclusions)
    {
        const QString className = QString::fromLatin1(exclusion.className);
        excluded.insert(className);
        if (!libraryClasses.contains(className))
        {
            errors << QStringLiteral("%1 is excluded, but it is not a class of the library (remove the exclusion)").arg(className);
        }
        if (onPages.count(className))
        {
            errors << QStringLiteral("%1 is excluded, but it has a sample (remove the exclusion)").arg(className);
        }
        if (exclusion.owner && !onPages.count(QString::fromLatin1(exclusion.owner)))
        {
            errors << QStringLiteral("%1 is opened by %2, which has no sample").arg(className, QString::fromLatin1(exclusion.owner));
        }
    }

    for (const FluentLibraryClass& item : FLUENT_LIBRARY_CLASSES)
    {
        const QString className = QString::fromLatin1(item.className);
        if (!onPages.count(className) && !excluded.contains(className))
        {
            errors << QStringLiteral("%1 (%2) has no sample on a page of the gallery: add a live sample (CLAUDE.md, lifetime rule)").arg(className, QString::fromLatin1(item.fileName));
        }
    }

    QStringList classes;
    for (const auto& item : onPages)
    {
        if (item.first.startsWith(QLatin1String("pdf::")))
        {
            classes << QStringLiteral("%1 %2").arg(item.first).arg(item.second);
        }
    }
    qInfo("classes of the library on the pages (%d): %s", int(classes.size()), qPrintable(classes.join(QStringLiteral(", "))));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("\n  "))));
}

void LifetimeTest::init()
{
    m_functionClock.start();
}

void LifetimeTest::cleanup()
{
    m_counters[QStringLiteral("time [ms] of %1").arg(QString::fromLatin1(QTest::currentTestFunction()))] += int(m_functionClock.elapsed());

    // The application still works: a theme change and a repaint of a fresh page
    InstanceCheck check(QStringLiteral("application after the test"));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    {
        PageHost host(CHECK_PAGE);
        QVERIFY(host.getWindow());
        const QImage image = host.getWindow()->grab().toImage();
        QVERIFY(!image.isNull());
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

bool LifetimeTest::isSelected(const WidgetInfo& info)
{
    static const QStringList only = qEnvironmentVariable("PDF4QT_LIFETIME_ONLY").split(QLatin1Char(','), Qt::SkipEmptyParts);
    return only.isEmpty() || only.contains(WidgetCatalog::pathText(info.path));
}

QWidget* LifetimeTest::resolve(const PageHost& host, const WidgetInfo& info)
{
    QWidget* widget = host.resolve(info.path);
    const QString className = widget ? QString::fromLatin1(widget->metaObject()->className()) : QString();
    if (widget && className != info.className)
    {
        // Each instance of a page has the same widgets in the same order; another
        // class means a child created in another order (lazily), the widget of
        // the description would not be tested
        ObjectTracker::reportError(QStringLiteral("the path of %1 leads to %2 in another instance of the page").arg(info.description, className));
        return nullptr;
    }
    return widget;
}

DeletedWidget LifetimeTest::remember(QWidget* widget)
{
    DeletedWidget deleted;
    deleted.parent = widget->parentWidget();
    deleted.geometry = widget->geometry();
    return deleted;
}

void LifetimeTest::useAround(const DeletedWidget& deleted)
{
    // The events of the deletion (layout, the deferred deletes), one round
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QWidget* parent = deleted.parent.data();
    if (!parent || !parent->isVisible())
    {
        return;
    }

    const QRect area = deleted.geometry.adjusted(-8, -8, 8, 8).intersected(parent->rect());
    if (!area.isEmpty())
    {
        parent->grab(area);
    }

    QPointer<QWidget> guard(parent);
    Input::mouse(parent, deleted.geometry.center(), QTest::MouseMove);
    if (guard)
    {
        Input::mouse(guard.data(), deleted.geometry.topLeft() + QPoint(2, 2), QTest::MouseMove);
    }
}

void LifetimeTest::useRest(PageHost& host, const std::vector<DeletedWidget>& deleted, bool full)
{
    Input::flush();

    // Around the deleted widgets: repaint, hover, Tab from a sibling
    int tabs = 0;
    for (const DeletedWidget& item : deleted)
    {
        useAround(item);

        QWidget* parent = item.parent.data();
        if (parent && parent->isVisible() && tabs < 48)
        {
            const QList<QWidget*> children = parent->findChildren<QWidget*>();
            for (QWidget* child : children)
            {
                if (Input::focus(child))
                {
                    QPointer<QWidget> guard(child);
                    for (int i = 0; i < 3 && guard; ++i, ++tabs)
                    {
                        Input::key(guard.data(), Qt::Key_Tab);
                    }
                    if (guard)
                    {
                        Input::key(guard.data(), Qt::Key_Backtab, Qt::ShiftModifier);
                    }
                    break;
                }
            }
        }
    }

    if (!host.getWindow())
    {
        return;
    }

    QWidget* window = host.getWindow();
    window->grab();

    if (!full)
    {
        return;
    }

    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: show / hide / resize").arg(host.getId()));
    window->hide();
    Input::flush();
    host.activate();
    window = host.getWindow();
    if (!window)
    {
        return;
    }
    window->resize(PageHost::WIDTH * 3 / 4, PageHost::HEIGHT * 3 / 4);
    Input::flush();
    window->resize(PageHost::WIDTH, PageHost::HEIGHT);
    Input::flush();

    // Hover over the visible part of the page
    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: hover").arg(host.getId()));
    for (int y = 20; y < PageHost::HEIGHT && host.getWindow(); y += 130)
    {
        for (int x = 20; x < PageHost::WIDTH && host.getWindow(); x += 180)
        {
            QTest::mouseMove(host.getWindow()->windowHandle(), QPoint(x, y));
        }
    }

    // Tab through the beginning of the page
    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: Tab").arg(host.getId()));
    if (host.getPage())
    {
        host.getPage()->setFocus(Qt::TabFocusReason);
        for (int i = 0; i < 24 && host.getWindow(); ++i)
        {
            Input::key(host.getWindow(), Qt::Key_Tab);
        }
    }

    // Theme, direction, font and style
    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: theme").arg(host.getId()));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    Input::flush();
    if (host.getWindow())
    {
        host.getWindow()->grab();
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: layout direction").arg(host.getId()));
    QApplication::setLayoutDirection(Qt::RightToLeft);
    Input::flush();
    if (host.getWindow())
    {
        host.getWindow()->grab();
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    Input::flush();

    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: language, palette and font of the application").arg(host.getId()));
    {
        // A new language (the texts and sizes of the widgets), the palette of the
        // application (the style fills it with the leak color again) and its font
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(qApp, &languageChange);
        Input::flush();
        QApplication::setPalette(QPalette());
        Input::flush();
        const QFont applicationFont = QApplication::font();
        QFont largerFont = applicationFont;
        largerFont.setPixelSize(15);
        QApplication::setFont(largerFont);
        Input::flush();
        if (host.getWindow())
        {
            host.getWindow()->grab();
        }
        QApplication::setFont(applicationFont);
        Input::flush();
    }

    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: font").arg(host.getId()));
    if (host.getWindow())
    {
        QFont font = host.getWindow()->font();
        font.setPixelSize(17);
        host.getWindow()->setFont(font);
        Input::flush();
        if (host.getWindow())
        {
            host.getWindow()->setFont(QFont());
        }
        Input::flush();
    }

    LifetimeContext::setStep(QStringLiteral("use of the rest of %1: style").arg(host.getId()));
    if (host.getWindow())
    {
        // Style change of all widgets of the page (a style sheet proxies the style of the library)
        host.getWindow()->setStyleSheet(QStringLiteral("QWidget { }"));
        Input::flush();
        if (host.getWindow())
        {
            host.getWindow()->setStyleSheet(QString());
        }
        Input::flush();
    }

    if (host.getWindow())
    {
        host.getWindow()->grab();
    }
    Input::flush();
}

void LifetimeTest::childDeletedFirst()
{
    QFETCH(int, page);

    const int batchSize = qEnvironmentVariableIsSet("PDF4QT_LIFETIME_BATCH") ? qMax(1, qEnvironmentVariableIntValue("PDF4QT_LIFETIME_BATCH")) : 60;

    // Candidates: widgets, which an application may destroy (every one of
    // a class of the library, a few captions and samples)
    const std::vector<WidgetInfo> catalog = buildCatalog(page);
    if (QTest::currentTestFailed())
    {
        return;
    }

    std::vector<int> remaining;
    for (int i = 0; i < int(catalog.size()); ++i)
    {
        if (catalog[i].isTarget && !catalog[i].isTrivial && isSelected(catalog[i]))
        {
            remaining.push_back(i);
        }
    }
    m_counters["childDeletedFirst: widgets"] += int(remaining.size());

    while (!remaining.empty())
    {
        // Batch: no widget of it is an ancestor of another one
        std::vector<int> batch;
        std::vector<int> postponed;
        for (int candidate : remaining)
        {
            bool isCompatible = int(batch.size()) < batchSize;
            for (int i = 0; isCompatible && i < int(batch.size()); ++i)
            {
                isCompatible = !WidgetCatalog::isAncestor(catalog, batch[i], candidate) && !WidgetCatalog::isAncestor(catalog, candidate, batch[i]);
            }
            (isCompatible ? batch : postponed).push_back(candidate);
        }
        remaining = std::move(postponed);

        QStringList names;
        for (int index : batch)
        {
            names << catalog[index].description;
        }

        InstanceCheck check(QStringLiteral("child deleted first, batch: %1").arg(names.join(QStringLiteral("; "))));
        ++m_counters["childDeletedFirst: page instances"];
        {
            PageHost host(page);

            // Paths are resolved before the first deletion changes the indices
            std::vector<QPointer<QWidget>> widgets;
            for (int index : batch)
            {
                widgets.emplace_back(resolve(host, catalog[index]));
            }

            std::vector<DeletedWidget> deleted;
            for (size_t i = 0; i < batch.size(); ++i)
            {
                const int index = batch[i];
                QWidget* widget = widgets[i].data();
                if (!widget)
                {
                    continue;
                }

                // The widget is deleted in a state: hovered, pressed (not released) or
                // focused by the keyboard (the focus frame of the style), in turn
                QPointer<QWidget> guard(widget);
                LifetimeContext::setStep(QStringLiteral("activating %1").arg(catalog[index].description));
                switch (i % 3)
                {
                    case 0:
                        Input::mouse(widget, widget->rect().center(), QTest::MouseMove);
                        break;
                    case 1:
                        Input::mouse(widget, widget->rect().center(), QTest::MousePress);
                        break;
                    case 2:
                        if (Input::focus(widget) && guard)
                        {
                            Input::key(guard, Qt::Key_F24);
                        }
                        break;
                }
                PopupCloser::closeAll();
                widget = guard.data();
                if (!widget)
                {
                    continue;
                }

                LifetimeContext::setStep(QStringLiteral("deleting %1").arg(catalog[index].description));
                const DeletedWidget item = remember(widget);
                delete widget;
                Input::releaseAll();
                deleted.push_back(item);
                useAround(item);
            }

            useRest(host, deleted, true);
            LifetimeContext::setStep(QStringLiteral("deleting the page %1").arg(host.getId()));
        }

        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }

    childObjectsDeleted(page, catalog);
}

void LifetimeTest::childObjectsDeleted(int page, const std::vector<WidgetInfo>& catalog)
{
    // Objects, which are not widgets, of the application: button groups, action
    // groups, actions, models, delegates, effects, timers and layouts (not the
    // inner objects of the widgets of the library, which an application cannot
    // reach, nor the parts of the widgets of Qt)
    QSet<const QObject*> internalParents;
    InstanceCheck check(QStringLiteral("objects, which are not widgets, deleted"));
    {
        PageHost host(page);
        // Containers of the application (plain widgets, frames, group boxes, the
        // samples of the gallery), whose objects are the objects of the application,
        // and the actions of menus, toolbars and menu bars; the other widgets of
        // Qt and the widgets of the library own their objects (their layouts,
        // groups, timers are their parts)
        std::vector<QPointer<QWidget>> widgets;
        for (const WidgetInfo& info : catalog)
        {
            QWidget* widget = info.isTarget ? resolve(host, info) : nullptr;
            if (widget && !info.isLibrary)
            {
                widgets.emplace_back(widget);
            }
        }
        widgets.emplace_back(host.getPage());

        std::vector<QPointer<QObject>> objects;
        for (const QPointer<QWidget>& widget : widgets)
        {
            const QByteArrayView className(widget->metaObject()->className());
            const bool isContainer = className == "QWidget" || className == "QFrame" || className == "QGroupBox" || className.startsWith("pdffluentgallery::");
            const bool isActionHolder = widget->inherits("QMenu") || widget->inherits("QToolBar") || widget->inherits("QMenuBar");
            for (QObject* child : widget->children())
            {
                const bool isAction = child->inherits("QAction") || child->inherits("QActionGroup");
                const bool isKind = child->inherits("QButtonGroup") || isAction || child->inherits("QAbstractItemModel") || child->inherits("QAbstractItemDelegate") ||
                                    child->inherits("QGraphicsEffect") || child->inherits("QTimer") || child->inherits("QLayout");
                // An action, which the application has added (not the toggle action of
                // a toolbar, not the action of a menu itself: parts of Qt)
                QAction* action = qobject_cast<QAction*>(child);
                const QMenu* menu = qobject_cast<const QMenu*>(widget.data());
                const bool isAddedAction = action && widget->actions().contains(action) && !(menu && menu->menuAction() == action);
                const bool isOfApplication = isContainer || (isActionHolder && isAddedAction);
                if (!child->isWidgetType() && isKind && isOfApplication && !child->objectName().startsWith(QLatin1String("qt_")))
                {
                    objects.emplace_back(child);
                }
            }
        }
        m_counters["childDeletedFirst: objects, which are not widgets"] += int(objects.size());

        std::vector<DeletedWidget> deleted;
        for (const QPointer<QObject>& object : objects)
        {
            if (!object)
            {
                continue;
            }
            QWidget* parent = qobject_cast<QWidget*>(object->parent());
            LifetimeContext::setStep(QStringLiteral("deleting %1 '%2' of %3").arg(QString::fromLatin1(object->metaObject()->className()), object->objectName(),
                                                                                  parent ? QString::fromLatin1(parent->metaObject()->className()) : QString()));
            DeletedWidget item;
            item.parent = parent;
            item.geometry = parent ? parent->rect() : QRect();
            delete object.data();
            useAround(item);
            if (deleted.size() < 16)
            {
                deleted.push_back(item);
            }
        }

        useRest(host, deleted, true);
        LifetimeContext::setStep(QStringLiteral("deleting the page %1").arg(host.getId()));
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::reparented(int page, const std::vector<WidgetInfo>& catalog)
{
    // A widget of each class of the library moved to another window (setParent,
    // as an application moves a panel), both windows used, then the page and the
    // moved widgets deleted in both orders
    const std::vector<int> classes = selectVariants(catalog, [](const WidgetInfo& info) { return info.isLibrary && info.isVisible; }, Key::Class);
    std::vector<int> movable;
    for (int candidate : classes)
    {
        bool isIndependent = true;
        for (int other : movable)
        {
            isIndependent = isIndependent && !WidgetCatalog::isAncestor(catalog, other, candidate) && !WidgetCatalog::isAncestor(catalog, candidate, other);
        }
        if (isIndependent)
        {
            movable.push_back(candidate);
        }
    }
    m_counters["deletionOrders: widgets moved to another window"] += int(movable.size());

    for (int order = 0; order < 2; ++order)
    {
        InstanceCheck check(order == 0 ? QStringLiteral("widgets moved to another window, the page deleted first") : QStringLiteral("widgets moved to another window, deleted before the page"));
        {
            PageHost host(page);
            std::vector<QPointer<QWidget>> widgets;
            for (int index : movable)
            {
                widgets.emplace_back(resolve(host, catalog[index]));
            }

            QPointer<QWidget> other = new QWidget();
            other->setObjectName(QStringLiteral("lifetime_other"));
            other->resize(640, 480);
            QVBoxLayout* layout = new QVBoxLayout(other);
            for (size_t i = 0; i < widgets.size(); ++i)
            {
                if (widgets[i])
                {
                    LifetimeContext::setStep(QStringLiteral("moving %1").arg(catalog[movable[i]].description));
                    widgets[i]->setParent(other);
                    layout->addWidget(widgets[i]);
                    widgets[i]->show();
                }
            }
            // The other window is not a popup, which the watcher closes
            EventWatcher::getInstance()->setHostWindow(other);
            other->show();
            [[maybe_unused]] const bool isExposed = QTest::qWaitForWindowExposed(other);
            EventWatcher::getInstance()->setHostWindow(host.getWindow());
            Input::flush();
            other->grab();
            QTest::mouseMove(other->windowHandle(), QPoint(100, 100));
            useRest(host, std::vector<DeletedWidget>(), false);

            LifetimeContext::setStep(QStringLiteral("deleting the moved widgets and the windows, order %1").arg(order));
            if (order == 0)
            {
                host.destroy();
                Input::flush();
                if (other)
                {
                    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
                    other->grab();
                    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
                }
                delete other.data();
            }
            else
            {
                for (const QPointer<QWidget>& widget : widgets)
                {
                    delete widget.data();
                    Input::flush();
                }
                delete other.data();
                useRest(host, std::vector<DeletedWidget>(), false);
            }
        }
        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }
}

std::vector<WidgetInfo> LifetimeTest::buildCatalog(int page)
{
    // The creation and the deletion of a page alone is an instance too
    std::vector<WidgetInfo> catalog;
    InstanceCheck check(QStringLiteral("catalog of the page"));
    {
        PageHost host(page);
        catalog = WidgetCatalog::build(host.getPage(), host.getId());
    }
    const QString problems = check.finish();
    if (!problems.isEmpty())
    {
        QTest::qFail(qPrintable(problems), __FILE__, __LINE__);
    }
    return catalog;
}

std::vector<int> LifetimeTest::selectVariants(const std::vector<WidgetInfo>& catalog, const std::function<bool(const WidgetInfo&)>& filter, Key key)
{
    std::vector<int> result;
    QSet<QString> seen;
    for (int i = 0; i < int(catalog.size()); ++i)
    {
        const WidgetInfo& info = catalog[i];
        if (!info.isTarget || info.isTrivial || !isSelected(info) || !filter(info))
        {
            continue;
        }

        const QString& text = key == Key::Variant ? info.variant : (key == Key::ClassAndParent ? info.classKey : info.className);
        if (!seen.contains(text))
        {
            seen.insert(text);
            result.push_back(i);
        }
    }
    return result;
}

int LifetimeTest::getTargetAncestor(const std::vector<WidgetInfo>& catalog, int index)
{
    for (int i = catalog[index].parent; i >= 0; i = catalog[i].parent)
    {
        if (catalog[i].isTarget)
        {
            return i;
        }
    }
    return -1;
}

void LifetimeTest::startTimers(QWidget* page)
{
    if (!page)
    {
        return;
    }

    // The hints of the samples (GalleryLifetimeHints::startTimers): the state
    // Copied and the inline editor of the property rows, ...
    QList<QWidget*> widgets = page->findChildren<QWidget*>();
    widgets.prepend(page);
    for (const QPointer<QWidget>& widget : QList<QPointer<QWidget>>(widgets.cbegin(), widgets.cend()))
    {
        if (widget)
        {
            const pdffluentgallery::GalleryLifetimeHints hints = pdffluentgallery::GalleryLifetimeHints::get(widget);
            if (hints.startTimers)
            {
                hints.startTimers(widget);
            }
        }
    }
}

QFocusFrame* LifetimeTest::findFocusFrame(QWidget* widget)
{
    if (!widget)
    {
        return nullptr;
    }

    // The focus frame is a child of the parent of the widget (QFocusFrame::setWidget)
    for (QWidget* candidate : QApplication::allWidgets())
    {
        QFocusFrame* frame = qobject_cast<QFocusFrame*>(candidate);
        if (frame && frame->widget() == widget && frame->isVisible())
        {
            return frame;
        }
    }
    return nullptr;
}

QString LifetimeTest::checkFocusRing(QWidget* focusWidget)
{
    if (!focusWidget)
    {
        return QStringLiteral("no widget has the focus");
    }

    const QString name = QStringLiteral("%1 '%2'").arg(QString::fromLatin1(focusWidget->metaObject()->className()), focusWidget->objectName());
    if (!pdf::PDFWidgetStyle::isFocusRingVisible(focusWidget))
    {
        return QStringLiteral("the focus ring of %1 is not shown after a key (keyboard modality of the style)").arg(name);
    }

    // The widgets with the ring outside of their shape (PDFWidgetStyleImpl::isOutsideFocusRingWidget)
    const QToolButton* toolButton = qobject_cast<const QToolButton*>(focusWidget);
    const QGroupBox* groupBox = qobject_cast<const QGroupBox*>(focusWidget);
    const bool isOutside = focusWidget->property(pdf::PDFWidgetStyleProperties::RADIUS).isValid() ||
                           qobject_cast<const QPushButton*>(focusWidget) || qobject_cast<const QCheckBox*>(focusWidget) || qobject_cast<const QRadioButton*>(focusWidget) ||
                           (toolButton && !(toolButton->parentWidget() && toolButton->parentWidget()->inherits("QLineEdit")) && !toolButton->property(pdf::PDFWidgetStyleProperties::STATUS_ITEM).isValid()) ||
                           (groupBox && groupBox->isCheckable());
    // The style updates the frame by a queued call
    if (isOutside && !QTest::qWaitFor([focusWidget]() { return findFocusFrame(focusWidget) != nullptr; }, 1000))
    {
        QStringList frames;
        for (QWidget* candidate : QApplication::allWidgets())
        {
            if (QFocusFrame* frame = qobject_cast<QFocusFrame*>(candidate))
            {
                frames << QStringLiteral("%1 visible %2").arg(frame->widget() ? QString::fromLatin1(frame->widget()->metaObject()->className()) : QStringLiteral("none")).arg(frame->isVisible());
            }
        }
        return QStringLiteral("the focus frame of the style is not attached to %1 (frames: %2)").arg(name, frames.join(QStringLiteral(", ")));
    }
    return QString();
}

void LifetimeTest::pageDeletedWhileActive()
{
    QFETCH(int, page);
    const std::vector<WidgetInfo> catalog = buildCatalog(page);
    if (QTest::currentTestFailed())
    {
        return;
    }

    // One widget of each class is pressed, focused
    // by the keyboard and shows its tooltip, while the page is deleted
    const std::vector<int> pressable = selectVariants(catalog, [](const WidgetInfo& info) { return info.isVisible && info.isEnabled; }, Key::Class);
    const std::vector<int> focusable = selectVariants(catalog, [](const WidgetInfo& info) { return info.isVisible && info.isEnabled && info.isFocusable; }, Key::Class);
    const std::vector<int> tips = selectVariants(catalog, [](const WidgetInfo& info) { return info.isVisible && info.hasToolTip; }, Key::Class);
    const int rounds = int(std::max({ pressable.size(), focusable.size(), tips.size(), size_t(7) }));
    m_counters["pageDeletedWhileActive: pressed or hovered widgets"] += int(pressable.size());
    m_counters["pageDeletedWhileActive: focused widgets"] += int(focusable.size());
    m_counters["pageDeletedWhileActive: widgets with a tooltip"] += int(tips.size());

    static const char* modeNames[] = { "window deleted", "page deleted, window used", "window hidden, then deleted", "style of the application replaced",
                                       "theme and direction changed, page deleted", "style Fusion of Qt and back", "proxy style over the style and back" };
    constexpr int MODE_COUNT = 7;

    for (int round = 0; round < rounds; ++round)
    {
        const int pressIndex = pressable.empty() ? -1 : pressable[size_t(round) % pressable.size()];
        const int focusIndex = focusable.empty() ? -1 : focusable[size_t(round + 1) % focusable.size()];
        const int tipIndex = tips.empty() ? -1 : tips[size_t(round) % tips.size()];
        const int mode = round % MODE_COUNT;
        const bool isPressed = (round / MODE_COUNT) % 2 == 0;

        const QString description = QStringLiteral("page deleted while active (%1), %2: %3; focused: %4; tooltip: %5")
                .arg(QString::fromLatin1(modeNames[mode]), isPressed ? QStringLiteral("pressed") : QStringLiteral("hovered"),
                     pressIndex >= 0 ? catalog[pressIndex].description : QString(),
                     focusIndex >= 0 ? catalog[focusIndex].description : QString(),
                     tipIndex >= 0 ? catalog[tipIndex].description : QString());

        InstanceCheck check(description);
        ++m_counters["pageDeletedWhileActive: page instances"];
        {
            PageHost host(page);
            QPointer<QWidget> pressed = pressIndex >= 0 ? resolve(host, catalog[pressIndex]) : nullptr;
            QPointer<QWidget> focused = focusIndex >= 0 ? resolve(host, catalog[focusIndex]) : nullptr;
            QPointer<QWidget> tip = tipIndex >= 0 ? resolve(host, catalog[tipIndex]) : nullptr;

            LifetimeContext::setStep(QStringLiteral("activating: %1").arg(description));
            startTimers(host.getPage());
            if (pressed)
            {
                Input::mouse(pressed, pressed->rect().center(), isPressed ? QTest::MousePress : QTest::MouseMove);
            }

            // The keyboard after the mouse (a press turns the keyboard modality
            // of the style off): a key, which no widget handles, shows the focus
            // ring and the focus frame of the style (modifiers do not count)
            if (focused && Input::reach(focused, focused->rect().center()) && focused && Input::focus(focused))
            {
                Input::key(focused, Qt::Key_F24);
                QCoreApplication::processEvents();
                const QString focusProblem = checkFocusRing(QApplication::focusWidget());
                QVERIFY2(focusProblem.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(description, focusProblem)));
                ++m_counters[findFocusFrame(QApplication::focusWidget()) ? "pageDeletedWhileActive: focus frame of the style attached" : "pageDeletedWhileActive: focus ring drawn by the widget"];
            }
            if (tip)
            {
                Input::sendToolTip(tip, tip->rect().center());
            }

            // Alt held (the mnemonics of the window are shown) while the window is deleted
            const bool isAltHeld = (round % 2) == 1 && host.getWindow() && host.getWindow()->windowHandle();
            if (isAltHeld)
            {
                QTest::keyPress(host.getWindow()->windowHandle(), Qt::Key_Alt);
            }
            QCoreApplication::processEvents();

            LifetimeContext::setStep(QStringLiteral("deleting: %1").arg(description));
            switch (mode)
            {
                case 0:
                    host.destroy();
                    break;

                case 1:
                    delete host.getPage();
                    useRest(host, std::vector<DeletedWidget>(), false);
                    break;

                case 2:
                    if (host.getWindow())
                    {
                        host.getWindow()->hide();
                    }
                    Input::flush();
                    host.destroy();
                    break;

                case 3:
                    // The old style is deleted, while it draws the focus frame, animates and tracks the hover
                    QApplication::setStyle(new pdf::PDFWidgetStyle());
                    Input::flush();
                    if (host.getWindow())
                    {
                        host.getWindow()->grab();
                    }
                    host.destroy();
                    break;

                case 4:
                    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
                    QApplication::setLayoutDirection(Qt::RightToLeft);
                    Input::flush();
                    delete host.getPage();
                    QApplication::setLayoutDirection(Qt::LeftToRight);
                    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
                    Input::flush();
                    host.destroy();
                    break;

                case 5:
                case 6:
                {
                    // Another style while the widgets live (the library unpolishes
                    // them, the item editor factory of Qt comes back), then the
                    // style of the library again
                    QApplication::setStyle(mode == 5 ? QStyleFactory::create(QStringLiteral("Fusion")) : new QProxyStyle(new pdf::PDFWidgetStyle()));
                    Input::flush();
                    if (host.getWindow())
                    {
                        host.getWindow()->grab();
                        QTest::mouseMove(host.getWindow()->windowHandle(), QPoint(PageHost::WIDTH / 2, PageHost::HEIGHT / 2));
                    }
                    QApplication::setStyle(new pdf::PDFWidgetStyle());
                    Input::flush();
                    if (host.getWindow())
                    {
                        host.getWindow()->grab();
                    }
                    host.destroy();
                    break;
                }

                default:
                    Q_ASSERT(false);
                    break;
            }

            if (isAltHeld && Input::s_spare && Input::s_spare->windowHandle())
            {
                // The release of Alt goes to another window
                QTest::keyRelease(Input::s_spare->windowHandle(), Qt::Key_Alt);
            }
        }

        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }
}

void LifetimeTest::deletionOrders()
{
    QFETCH(int, page);
    const std::vector<WidgetInfo> catalog = buildCatalog(page);
    if (QTest::currentTestFailed())
    {
        return;
    }

    std::vector<int> targets;
    for (int i = 0; i < int(catalog.size()); ++i)
    {
        if (catalog[i].isTarget && isSelected(catalog[i]))
        {
            targets.push_back(i);
        }
    }
    m_counters["deletionOrders: widgets"] += int(targets.size());

    for (int order = 0; order < 2; ++order)
    {
        // 0: reverse order of the creation (children before parents, later siblings first),
        // 1: order of the creation (parents first, with a descendant focused and hovered)
        InstanceCheck check(order == 0 ? QStringLiteral("deletion in the reverse order of creation") : QStringLiteral("deletion of parents first, a child focused and hovered"));
        ++m_counters["deletionOrders: page instances"];
        {
            PageHost host(page);
            std::vector<QPointer<QWidget>> widgets;
            for (int index : targets)
            {
                widgets.emplace_back(resolve(host, catalog[index]));
            }

            std::vector<DeletedWidget> deleted;
            for (size_t step = 0; step < targets.size(); ++step)
            {
                const size_t i = order == 0 ? targets.size() - 1 - step : step;
                QWidget* widget = widgets[i].data();
                if (!widget)
                {
                    continue;
                }

                if (order == 1)
                {
                    LifetimeContext::setStep(QStringLiteral("focusing a child of %1").arg(catalog[targets[i]].description));
                    QPointer<QWidget> guard(widget);
                    const QList<QWidget*> descendants = widget->findChildren<QWidget*>();
                    for (auto it = descendants.crbegin(); it != descendants.crend(); ++it)
                    {
                        if (Input::focus(*it))
                        {
                            break;
                        }
                    }
                    if (!descendants.isEmpty() && guard)
                    {
                        QWidget* first = descendants.front();
                        Input::mouse(first, first->rect().center(), QTest::MouseMove);
                    }
                    widget = guard.data();
                    if (!widget)
                    {
                        continue;
                    }
                }

                LifetimeContext::setStep(QStringLiteral("deleting %1").arg(catalog[targets[i]].description));
                const DeletedWidget item = remember(widget);
                delete widget;
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                if (step % 8 == 0)
                {
                    // Events, layout and repaint after every eighth deletion (a page
                    // with hundreds of widgets would be laid out after each of them)
                    useAround(item);
                }
                if (deleted.size() < 16)
                {
                    deleted.push_back(item);
                }
            }

            useRest(host, deleted, false);
            LifetimeContext::setStep(QStringLiteral("deleting the page %1").arg(host.getId()));
        }

        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }

    reparented(page, catalog);
}

bool LifetimeTest::runPopupInput(QWidget* owner, const QList<Step>& steps, const QString& popupClass, const QList<int>& path, SignalReceiver* deleter)
{
    QPointer<QWidget> guard(owner);
    if (deleter)
    {
        deleter->setArmed(false);
    }
    for (int i = 0; i + 1 < steps.size() && guard; ++i)
    {
        StepRunner::run(guard, steps[i]);
        PopupCloser::closeAll();
    }
    if (deleter)
    {
        deleter->setArmed(true);
    }
    if (!guard || steps.isEmpty())
    {
        return false;
    }

    // The popup stays open after the last step
    const int shownBefore = EventWatcher::getInstance()->getShownCount();
    EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Keep);
    StepRunner::run(guard, steps.back());
    EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Close);
    QCoreApplication::processEvents();

    QWidget* popup = nullptr;
    for (const EventWatcher::ShownWindow& window : EventWatcher::getInstance()->getShownSince(shownBefore))
    {
        if (window.window && window.window->isVisible() && window.className == popupClass)
        {
            popup = window.window.data();
        }
    }
    QWidget* widget = popup ? WidgetPath::resolve(popup, path) : nullptr;
    const bool isClicked = widget && widget->isVisible() && StepRunner::run(widget, Step::Click);
    PopupCloser::closeAll();
    return isClicked;
}

QAbstractItemDelegate* LifetimeTest::getDelegate(QAbstractItemView* view, const Emitter& emitter)
{
    if (!view)
    {
        return nullptr;
    }
    if (emitter.column >= 0)
    {
        return view->itemDelegateForColumn(emitter.column);
    }
    if (emitter.row >= 0)
    {
        return view->itemDelegateForRow(emitter.row);
    }
    return view->itemDelegate();
}

bool LifetimeTest::reachesButtonMoveFocus(const QWidget* widget)
{
    // QAbstractButton::keyPressEvent of Qt 6.11 calls moveFocus for a button
    // with auto exclusivity, in a group, or in the viewport of a view
    const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget);
    if (!button)
    {
        return false;
    }
    const QWidget* parent = button->parentWidget();
    return button->autoExclusive() || button->group() || (parent && qobject_cast<const QAbstractItemView*>(parent->parentWidget()));
}

std::vector<LifetimeTest::SignalTrial> LifetimeTest::discoverSignals(int page, const std::vector<WidgetInfo>& catalog)
{
    // The input goes to each widget, which can get it; the signals of all
    // objects of the page (widgets, delegates of the views) and of the shown
    // popups of the library are recorded, so a signal emitted by a composite
    // widget, a sibling, a delegate or a popup because of the input is found
    std::vector<SignalTrial> trials;
    const std::vector<int> candidates = selectVariants(catalog, [](const WidgetInfo& info) { return info.isVisible && info.isEnabled && (info.hasSignals || info.isFocusable); }, Key::Variant);
    m_counters["deletedFromOwnSignal: widgets with the input (variants)"] += int(candidates.size());

    InstanceCheck check(QStringLiteral("discovery of the signals"));
    {
        PageHost host(page);
        std::vector<Emitter> emitters;
        std::unordered_set<const QObject*> connected;
        SignalReceiver recorder;

        m_librarySignals.clear();
        auto addEmitter = [this, &emitters, &connected, &recorder](QObject* object, Emitter emitter)
        {
            const QList<QMetaMethod> signalList = SignalReceiver::getTestedSignals(object);
            if (signalList.isEmpty() || connected.count(object))
            {
                return;
            }

            const QWidget* widget = qobject_cast<const QWidget*>(object);
            const QWidget* owner = widget ? widget : qobject_cast<const QWidget*>(object->parent());
            if (!owner || (owner->isVisible() && owner->isEnabled()))
            {
                for (const QMetaMethod& method : signalList)
                {
                    const QString className = QString::fromLatin1(method.enclosingMetaObject()->className());
                    if (className.startsWith(QLatin1String("pdf::")))
                    {
                        m_librarySignals.emplace(QStringLiteral("%1::%2").arg(className, QString::fromLatin1(method.methodSignature())), emitter.description);
                    }
                }
            }
            connected.insert(object);
            emitter.metaObject = object->metaObject();
            recorder.setEmitterId(object, int(emitters.size()));
            recorder.connectSignals(object, signalList);
            emitters.push_back(std::move(emitter));
        };

        // The widgets and the delegates of the page (resolved before any input)
        std::vector<QPointer<QWidget>> widgets(catalog.size());
        for (int i = 0; i < int(catalog.size()); ++i)
        {
            const WidgetInfo& info = catalog[i];
            if (!info.isTarget)
            {
                continue;
            }
            QWidget* widget = resolve(host, info);
            widgets[size_t(i)] = widget;
            if (!widget)
            {
                continue;
            }

            Emitter emitter;
            emitter.widget = i;
            emitter.description = info.description;
            addEmitter(widget, emitter);

            if (QAbstractItemView* view = qobject_cast<QAbstractItemView*>(widget))
            {
                emitter.kind = Emitter::Kind::Delegate;
                emitter.description = QStringLiteral("delegate of %1").arg(info.description);
                addEmitter(view->itemDelegate(), emitter);
                QAbstractItemModel* model = view->model();
                for (int column = 0; model && column < qMin(model->columnCount(view->rootIndex()), 32); ++column)
                {
                    if (QAbstractItemDelegate* delegate = view->itemDelegateForColumn(column))
                    {
                        Emitter columnEmitter = emitter;
                        columnEmitter.column = column;
                        columnEmitter.description = QStringLiteral("delegate of the column %1 of %2").arg(column).arg(info.description);
                        addEmitter(delegate, columnEmitter);
                    }
                }
                for (int row = 0; model && row < qMin(model->rowCount(view->rootIndex()), 32); ++row)
                {
                    if (QAbstractItemDelegate* delegate = view->itemDelegateForRow(row))
                    {
                        Emitter rowEmitter = emitter;
                        rowEmitter.row = row;
                        rowEmitter.description = QStringLiteral("delegate of the row %1 of %2").arg(row).arg(info.description);
                        addEmitter(delegate, rowEmitter);
                    }
                }
            }
        }

        // The popups of the library (a palette, a message box) and their widgets
        // Widgets of a shown popup of the library (not modal: it is kept open
        // without its own event loop), which get a click later
        struct PopupInput
        {
            int owner = -1;
            QList<Step> steps;
            QString popupClass;
            QList<int> path;
            QString description;
        };
        std::vector<PopupInput> popupInputs;
        QSet<QString> popupInputKeys;
        int hookOwner = -1;
        QList<Step> hookSteps;

        EventWatcher::getInstance()->setShownHook([&addEmitter, &popupInputs, &popupInputKeys, &hookOwner, &hookSteps](QWidget* window)
        {
            const QString popupClass = QString::fromLatin1(window->metaObject()->className());
            if (!popupClass.startsWith(QLatin1String("pdf::")))
            {
                return;
            }
            QList<QWidget*> widgetList = window->findChildren<QWidget*>();
            widgetList.prepend(window);
            for (QWidget* widget : widgetList)
            {
                Emitter emitter;
                emitter.kind = Emitter::Kind::Popup;
                emitter.popupClass = popupClass;
                emitter.popupPath = WidgetPath::get(window, widget);
                emitter.description = QStringLiteral("popup %1 / %2 '%3' [%4]").arg(popupClass, QString::fromLatin1(widget->metaObject()->className()), widget->objectName(), WidgetCatalog::pathText(emitter.popupPath));
                addEmitter(widget, emitter);

                const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget);
                const QString key = QStringLiteral("%1|%2|%3|%4").arg(popupClass, QString::fromLatin1(widget->metaObject()->className()),
                                                                     widget->parentWidget() ? QString::fromLatin1(widget->parentWidget()->metaObject()->className()) : QString())
                                                                     .arg(button && button->isChecked() ? 1 : 0);
                if (hookOwner >= 0 && widget != window && !window->isModal() && widget->isVisibleTo(window) && widget->isEnabled() && widget->focusPolicy() != Qt::NoFocus && !popupInputKeys.contains(key))
                {
                    popupInputKeys.insert(key);
                    popupInputs.push_back(PopupInput{ hookOwner, hookSteps, popupClass, emitter.popupPath, emitter.description });
                }
            }
        });

        QSet<QString> found;
        m_signalHistory.clear();

        // Makes the trials of the fired signals
        auto collect = [&](int candidate, const QList<Step>& performed, Step step, bool isQtUnsafeArrow, const PopupInput* popupInput)
        {
                for (const SignalReceiver::Fired& fired : recorder.takeFired())
                {
                    if (fired.emitter < 0)
                    {
                        continue;
                    }
                    const Emitter& emitter = emitters[size_t(fired.emitter)];
                    QString key;
                    switch (emitter.kind)
                    {
                        case Emitter::Kind::Widget:
                            key = catalog[emitter.widget].variant;
                            break;
                        case Emitter::Kind::Delegate:
                            key = QStringLiteral("%1|delegate %2|%3|%4").arg(catalog[emitter.widget].variant, QString::fromLatin1(emitter.metaObject->className())).arg(emitter.column).arg(emitter.row);
                            break;
                        case Emitter::Kind::Popup:
                            key = QStringLiteral("popup %1|%2|%3").arg(emitter.popupClass, WidgetCatalog::pathText(emitter.popupPath), QString::fromLatin1(emitter.metaObject->className()));
                            break;
                    }
                    key += QStringLiteral("|%1").arg(fired.method);
                    if (found.contains(key))
                    {
                        continue;
                    }
                    found.insert(key);

                    const QMetaMethod method = emitter.metaObject->method(fired.method);
                    SignalTrial trial;
                    trial.input = candidate;
                    trial.steps = performed;
                    trial.emitter = emitter;
                    trial.method = fired.method;
                    trial.signature = QStringLiteral("%1::%2").arg(QString::fromLatin1(method.enclosingMetaObject()->className()), QString::fromLatin1(method.methodSignature()));
                    trial.history = int(m_signalHistory.size()) - 1;
                    trial.isViewSignal = emitter.kind != Emitter::Kind::Delegate && method.enclosingMetaObject() == &QAbstractItemView::staticMetaObject;
                    if (popupInput)
                    {
                        trial.popupInputClass = popupInput->popupClass;
                        trial.popupInputPath = popupInput->path;
                    }

                    // Qt uses the buttons after the arrow (see runSignalTrials)
                    trial.isQtUnsafe = isQtUnsafeArrow && method.enclosingMetaObject() == &QAbstractButton::staticMetaObject;
                    trials.push_back(trial);
                    LifetimeContext::trace(QStringLiteral("signal found: %1 of %2 by %3 on %4%5").arg(trial.signature, emitter.description, QString::fromLatin1(StepRunner::getName(step)), catalog[candidate].description,
                                                                                                      popupInput ? QStringLiteral(", click on %1").arg(popupInput->description) : QString()));
                }
        };

        for (int candidate : candidates)
        {
            QPointer<QWidget> widget = widgets[size_t(candidate)];
            if (!widget || !widget->isVisible())
            {
                continue;
            }

            QList<Step> performed;
            m_signalHistory.emplace_back(candidate, QList<Step>());
            recorder.takeFired();
            for (int s = 0; s < int(Step::Count) && widget; ++s)
            {
                const Step step = Step(s);
                // The probe sees, whether an arrow key (also Alt+Down) reaches a button
                // of a group behind the filters of the library
                ArrowProbe probe;
                {
                    probe.attach(widget);
                    QWidget* focusBefore = QApplication::focusWidget();
                    if (focusBefore && widget->isAncestorOf(focusBefore))
                    {
                        probe.attach(focusBefore);
                    }
                }
                hookOwner = candidate;
                hookSteps = performed;
                hookSteps << step;
                const bool isPerformed = StepRunner::run(widget, step);
                hookOwner = -1;
                probe.detach();
                if (!isPerformed)
                {
                    continue;
                }
                performed << step;
                m_signalHistory.back().second = performed;
                bool reachesMoveFocus = false;
                for (const QPointer<QWidget>& reached : probe.getReached())
                {
                    reachesMoveFocus = reachesMoveFocus || (reached && reachesButtonMoveFocus(reached));
                }
                PopupCloser::closeAll();
                collect(candidate, performed, step, reachesMoveFocus, nullptr);
            }
            PopupCloser::closeAll();
        }

        // A click on each kind of a widget of the popups (a swatch, a link)
        m_counters["deletedFromOwnSignal: widgets of popups clicked"] += int(popupInputs.size());
        for (const PopupInput& popupInput : popupInputs)
        {
            QWidget* owner = widgets[size_t(popupInput.owner)];
            if (!owner)
            {
                continue;
            }
            recorder.takeFired();
            runPopupInput(owner, popupInput.steps, popupInput.popupClass, popupInput.path, nullptr);
            collect(popupInput.owner, popupInput.steps, popupInput.steps.back(), false, &popupInput);
        }
        EventWatcher::getInstance()->setShownHook(EventWatcher::ShownHook());
        recorder.disconnectAll();
    }
    const QString problems = check.finish();
    if (!problems.isEmpty())
    {
        QTest::qFail(qPrintable(problems), __FILE__, __LINE__);
    }
    return trials;
}

void LifetimeTest::runSignalTrials(int page, const std::vector<WidgetInfo>& catalog, const std::vector<SignalTrial>& trials, Victim victim)
{
    static const char* victimNames[] = { "the sender", "the parent", "the window", "the delegate", "the model" };
    const ScopedFlag allowSelectionWithoutModel(MessageCollector::s_allowSelectionWithoutModel, victim == Victim::Model);

    std::vector<size_t> queue;
    for (size_t i = 0; i < trials.size(); ++i)
    {
        const SignalTrial& trial = trials[i];

        // Qt itself does not survive a view deleted synchronously from inside of
        // its signals pressed, clicked, doubleClicked, activated and entered: it
        // uses the view after it has emitted them (QAbstractItemView::mousePressEvent,
        // mouseReleaseEvent, mouseDoubleClickEvent, keyPressEvent and
        // QAbstractItemViewPrivate::checkMouseMove of Qt 6.11). The delegate and
        // the model are deleted from these signals instead.
        //
        // Generally: Qt does not survive the deletion of an object, while it emits
        // its own signal of the object (or of a descendant) without a guard - the
        // toggled of a QCheckBox (QCheckBox::nextCheckState calls checkStateSet
        // after it), the signals of QLineEdit, QComboBox, ... The deleter refuses
        // such a deletion by the emissions in progress (ObjectTracker::
        // isDeletionUnsupported), the signals of the library and the signals of
        // QAbstractButton, which Qt guards, are tried.
        //
        // QAbstractButtonPrivate::moveFocus (an arrow key on a button in a group,
        // with auto exclusivity or in the viewport of a view, which no filter of
        // the library takes before) clicks the next button and then gives it the
        // focus without a guard, the button with the key calls hasFocus() after
        // it: a button deleted from the signals of that click is used by Qt (found
        // by valgrind). The test does not delete anything from these signals; the
        // arrows handled by the library are tried.
        const bool isAllowed = trial.isViewSignal ? (victim == Victim::Delegate || victim == Victim::Model)
                                                  : (!trial.isQtUnsafe && (victim == Victim::Sender || victim == Victim::Parent || victim == Victim::Host));
        if (isAllowed)
        {
            queue.push_back(i);
        }
    }

    if (victim == Victim::Host)
    {
        // A window for each signal of a class of the library (the buttons of Qt
        // delete their window from their signals in the other modes)
        std::vector<size_t> unique;
        QSet<QString> seen;
        for (size_t i : queue)
        {
            const QString className = QString::fromLatin1(trials[i].emitter.metaObject->className());
            if (!className.startsWith(QLatin1String("pdf::")))
            {
                continue;
            }
            const QString key = className + trials[i].signature;
            if (!seen.contains(key))
            {
                seen.insert(key);
                unique.push_back(i);
            }
        }
        queue = std::move(unique);
    }

    // A trial, whose signal has not come again on a page used by other trials
    // before, is repeated as the first one on a fresh page, then on a fresh
    // page after the inputs of the discovery before it (the signal depends on
    // the state, which they have left); a signal, which does not come there,
    // is a failure (the test would silently lose the case)
    std::unordered_map<size_t, int> mustBeFirst;    ///< Trial and its level: 1 fresh page, 2 fresh page with the history
    QStringList notRepeated;

    while (!queue.empty())
    {
        std::vector<size_t> postponed;
        std::vector<size_t> retried;
        size_t done = 0;
        InstanceCheck check(QStringLiteral("deletion of %1 from inside of a signal").arg(QString::fromLatin1(victimNames[int(victim)])));
        ++m_counters["deletedFromOwnSignal: page instances"];
        {
            PageHost host(page);

            // The widgets are resolved before the first deletion changes the indices
            std::vector<QPointer<QWidget>> inputs;
            std::vector<QPointer<QWidget>> emitterWidgets;
            std::vector<QPointer<QWidget>> ancestors;
            for (size_t i : queue)
            {
                const SignalTrial& trial = trials[i];
                inputs.emplace_back(resolve(host, catalog[trial.input]));
                emitterWidgets.emplace_back(trial.emitter.kind != Emitter::Kind::Popup ? resolve(host, catalog[trial.emitter.widget]) : nullptr);
                const int ancestorOf = trial.emitter.kind == Emitter::Kind::Popup ? trial.input : trial.emitter.widget;
                const int ancestor = (trial.emitter.kind == Emitter::Kind::Popup && catalog[ancestorOf].isTarget) ? ancestorOf : getTargetAncestor(catalog, ancestorOf);
                ancestors.emplace_back(ancestor >= 0 ? resolve(host, catalog[ancestor]) : host.getPage());
            }

            bool isFirst = true;
            for (size_t q = 0; q < queue.size(); ++q)
            {
                const size_t index = queue[q];
                const SignalTrial& trial = trials[index];
                QPointer<QWidget> input = inputs[q];
                const int level = mustBeFirst.count(index) ? mustBeFirst[index] : 0;
                const bool needsFreshPage = victim == Victim::Host || level > 0;
                const bool hasEmitter = trial.emitter.kind == Emitter::Kind::Popup || emitterWidgets[q];
                if (!input || !hasEmitter || (needsFreshPage && !isFirst))
                {
                    postponed.push_back(index);
                    continue;
                }
                QAbstractItemView* view = qobject_cast<QAbstractItemView*>(emitterWidgets[q].data());
                QObject* emitterObject = nullptr;
                switch (trial.emitter.kind)
                {
                    case Emitter::Kind::Widget:
                        emitterObject = emitterWidgets[q].data();
                        break;
                    case Emitter::Kind::Delegate:
                        emitterObject = getDelegate(view, trial.emitter);
                        break;
                    case Emitter::Kind::Popup:
                        break;
                }
                if (trial.emitter.kind != Emitter::Kind::Popup && (!emitterObject || emitterObject->metaObject() != trial.emitter.metaObject))
                {
                    if (isFirst)
                    {
                        ObjectTracker::reportError(QStringLiteral("the emitter %1 of %2 is not found in another instance of the page").arg(trial.emitter.description, trial.signature));
                    }
                    else
                    {
                        // Deleted by a trial before (a delegate replaced by the default one of the view)
                        postponed.push_back(index);
                    }
                    continue;
                }

                const bool isFirstOnPage = isFirst;
                isFirst = false;
                ++done;

                QObject* victimObject = nullptr;
                switch (victim)
                {
                    case Victim::Sender:
                        victimObject = emitterObject;
                        break;
                    case Victim::Parent:
                        victimObject = trial.emitter.kind == Emitter::Kind::Delegate ? static_cast<QObject*>(view) : static_cast<QObject*>(ancestors[q].data());
                        break;
                    case Victim::Host:
                        victimObject = host.getWindow();
                        break;
                    case Victim::Delegate:
                        // Only a delegate of the library (the default delegate of Qt is a part of the view)
                        victimObject = (view && view->itemDelegate() && QByteArrayView(view->itemDelegate()->metaObject()->className()).startsWith("pdf::")) ? view->itemDelegate() : nullptr;
                        break;
                    case Victim::Model:
                        victimObject = view ? view->model() : nullptr;
                        break;
                }

                if (!victimObject && !(trial.emitter.kind == Emitter::Kind::Popup && victim == Victim::Sender))
                {
                    continue;
                }

                const QString description = QStringLiteral("%1 deleted from %2 of %3, input on %4 (steps:").arg(QString::fromLatin1(victimNames[int(victim)]), trial.signature, trial.emitter.description, catalog[trial.input].description);
                QStringList stepNames;
                for (Step step : trial.steps)
                {
                    stepNames << QString::fromLatin1(StepRunner::getName(step));
                }
                LifetimeContext::setStep(QStringLiteral("%1 %2)").arg(description, stepNames.join(QLatin1Char(' '))));

                QWidget* aroundWidget = qobject_cast<QWidget*>(victimObject);
                const DeletedWidget around = remember(aroundWidget ? aroundWidget : input.data());

                if (level == 2)
                {
                    // The inputs of the discovery before this one (nothing is deleted yet on this page)
                    std::vector<QPointer<QWidget>> historyWidgets;
                    for (int h = 0; h < trial.history && h < int(m_signalHistory.size()); ++h)
                    {
                        historyWidgets.emplace_back(resolve(host, catalog[m_signalHistory[size_t(h)].first]));
                    }
                    for (int h = 0; h < int(historyWidgets.size()); ++h)
                    {
                        for (Step step : m_signalHistory[size_t(h)].second)
                        {
                            StepRunner::run(historyWidgets[size_t(h)], step);
                            PopupCloser::closeAll();
                        }
                    }
                    LifetimeContext::setStep(QStringLiteral("%1 %2) after the history of the discovery").arg(description, stepNames.join(QLatin1Char(' '))));
                    if (!input || (trial.emitter.kind != Emitter::Kind::Popup && !emitterWidgets[q]))
                    {
                        notRepeated << QStringLiteral("%1 (the input or the emitter is gone after the history)").arg(description);
                        continue;
                    }
                }

                SignalReceiver deleter;
                if (trial.emitter.kind == Emitter::Kind::Popup)
                {
                    // The popup is created by the input: the signal is connected, when it is shown
                    const Emitter& emitter = trial.emitter;
                    const int method = trial.method;
                    QPointer<QObject> plannedVictim = victimObject;
                    const bool isSenderVictim = victim == Victim::Sender;
                    EventWatcher::getInstance()->setShownHook([&deleter, emitter, method, plannedVictim, isSenderVictim](QWidget* window)
                    {
                        QWidget* widget = QString::fromLatin1(window->metaObject()->className()) == emitter.popupClass ? WidgetPath::resolve(window, emitter.popupPath) : nullptr;
                        if (!widget || widget->metaObject() != emitter.metaObject)
                        {
                            return;
                        }
                        deleter.disconnectAll();
                        deleter.connectSignals(widget, { widget->metaObject()->method(method) });
                        deleter.setVictim(isSenderVictim ? widget : plannedVictim.data());
                    });
                }
                else
                {
                    deleter.setVictim(victimObject);
                    deleter.connectSignals(emitterObject, { emitterObject->metaObject()->method(trial.method) });
                }

                if (!trial.popupInputClass.isEmpty())
                {
                    // A click into the popup opened by the steps
                    runPopupInput(input, trial.steps, trial.popupInputClass, trial.popupInputPath, &deleter);
                }
                else
                {
                    // The step, which has emitted the signal, is tried alone first; the
                    // signal can depend on the state left by the previous steps, then
                    // the previous steps are repeated (the deleter is disarmed: the
                    // deletion is done only by the step found by the discovery, which
                    // has been checked for the limits of Qt) and the step again
                    StepRunner::run(input, trial.steps.back());
                    if (input && !deleter.hasDeleted() && !deleter.hasSkipped() && trial.steps.size() > 1)
                    {
                        deleter.setArmed(false);
                        PopupCloser::closeAll();
                        for (int i = 0; i < trial.steps.size() - 1 && input; ++i)
                        {
                            StepRunner::run(input, trial.steps[i]);
                        }
                        deleter.setArmed(true);
                        if (input)
                        {
                            StepRunner::run(input, trial.steps.back());
                        }
                    }
                }
                PopupCloser::closeAll();
                EventWatcher::getInstance()->setShownHook(EventWatcher::ShownHook());

                if (deleter.hasDeleted())
                {
                    ++m_counters["deletedFromOwnSignal: deletions from a signal"];
                }
                else if (deleter.hasSkipped())
                {
                    ++m_counters["deletedFromOwnSignal: deletions not supported by Qt (skipped)"];
                    LifetimeContext::trace(QStringLiteral("deletion skipped: %1").arg(deleter.getSkipReason()));
                }
                else if (level == 2)
                {
                    notRepeated << QStringLiteral("%1 %2)").arg(description, stepNames.join(QLatin1Char(' ')));
                }
                else
                {
                    ++m_counters[isFirstOnPage ? "deletedFromOwnSignal: signal repeated after the history of the discovery" : "deletedFromOwnSignal: signal repeated on a fresh page"];
                    mustBeFirst[index] = isFirstOnPage ? 2 : 1;
                    retried.push_back(index);
                }
                deleter.disconnectAll();
                useAround(around);

                if (!host.getWindow())
                {
                    break;
                }
            }

            useRest(host, std::vector<DeletedWidget>(), false);
            LifetimeContext::setStep(QStringLiteral("deleting the page %1").arg(host.getId()));
        }

        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));

        if (done == 0)
        {
            // Cannot happen: the first trial of a fresh page always runs
            QFAIL("no progress of the signal trials");
        }

        // The repeated trials first: each of them is the first one on its page
        retried.insert(retried.end(), postponed.begin(), postponed.end());
        queue = std::move(retried);
    }

    QVERIFY2(notRepeated.isEmpty(), qPrintable(QStringLiteral("signals found by the discovery, but not emitted again on a fresh page:\n  %1").arg(notRepeated.join(QStringLiteral("\n  ")))));
}

void LifetimeTest::deletedFromOwnSignal()
{
    QFETCH(int, page);
    const std::vector<WidgetInfo> catalog = buildCatalog(page);
    if (QTest::currentTestFailed())
    {
        return;
    }

    const std::vector<SignalTrial> trials = discoverSignals(page, catalog);
    if (QTest::currentTestFailed())
    {
        return;
    }
    m_counters["deletedFromOwnSignal: signals found"] += int(trials.size());

    // Every signal of a class of the library, which a visible and enabled object
    // of the page has, is found by the input (so it is tried by the deletions),
    // unless it is emitted only by a function called by the application (a setter)
    // or it needs an input, which the generic steps do not give
    // (signal -> reason; empty now: every signal of the library is reached by the input)
    static const std::map<QString, QString> notByInput;
    QSet<QString> found;
    for (const SignalTrial& trial : trials)
    {
        found.insert(trial.signature);
    }
    QStringList missing;
    for (const auto& [signal, emitter] : m_librarySignals)
    {
        if (!found.contains(signal) && !notByInput.count(signal))
        {
            missing << QStringLiteral("%1 (of %2)").arg(signal, emitter);
        }
    }
    QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("signals of the library, which no input on the page has emitted (add an input step, a lifetime hint of the sample, or a justified entry of notByInput):\n  %1").arg(missing.join(QStringLiteral("\n  ")))));

    for (Victim victim : { Victim::Sender, Victim::Parent, Victim::Delegate, Victim::Model, Victim::Host })
    {
        runSignalTrials(page, catalog, trials, victim);
        if (QTest::currentTestFailed())
        {
            return;
        }
    }
}

std::vector<LifetimeTest::PopupCase> LifetimeTest::discoverPopups(int page, const std::vector<WidgetInfo>& catalog)
{
    std::vector<PopupCase> cases;
    const std::vector<int> candidates = selectVariants(catalog, [](const WidgetInfo& info) { return info.isVisible && info.isEnabled; }, Key::Variant);
    // A double click opens the editors of the views (a popup opened by the first
    // click of a double click is found by the click)
    // (ClickTrailing: the arrow of a date edit opens its calendar)
    static const Step openers[] = { Step::RightClickClose, Step::Click, Step::ToolTip, Step::KeySpace, Step::KeyReturn,
                                    Step::KeyAltDown, Step::KeyF4, Step::KeyF2, Step::KeyMenuClose, Step::DoubleClick, Step::ClickTrailing };

    InstanceCheck check(QStringLiteral("discovery of the popups"));
    {
        PageHost host(page);
        host.applyHints();
        std::vector<QPointer<QWidget>> widgets;
        for (int index : candidates)
        {
            widgets.emplace_back(resolve(host, catalog[index]));
        }

        QSet<QString> seen;
        for (size_t c = 0; c < candidates.size(); ++c)
        {
            QPointer<QWidget> widget = widgets[c];
            QList<Step> performed;
            for (Step opener : openers)
            {
                if (!widget || !widget->isVisible())
                {
                    break;
                }

                QAbstractItemView* view = qobject_cast<QAbstractItemView*>(widget.data());
                if (opener == Step::DoubleClick && !view)
                {
                    continue;
                }
                QList<QPointer<QWidget>> editorsBefore;
                if (view)
                {
                    for (QWidget* child : view->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                    {
                        editorsBefore << child;
                    }
                }

                const int shownBefore = EventWatcher::getInstance()->getShownCount();
                if (!StepRunner::run(widget, opener))
                {
                    continue;
                }
                performed << opener;

                QList<QPair<PopupCase::Kind, QString>> found;
                for (const EventWatcher::ShownWindow& window : EventWatcher::getInstance()->getShownSince(shownBefore))
                {
                    found << qMakePair(window.isToolTip ? PopupCase::Kind::ToolTip : PopupCase::Kind::Window, window.className);
                }
                if (found.isEmpty() && (pdf::PDFToolTip::isVisible() || QToolTip::isVisible()))
                {
                    // A tooltip shown again (its window is not shown again)
                    found << qMakePair(PopupCase::Kind::ToolTip, QStringLiteral("tooltip"));
                }
                if (view && widget)
                {
                    for (QWidget* child : view->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                    {
                        if (child->isVisible() && !editorsBefore.contains(child))
                        {
                            found << qMakePair(PopupCase::Kind::Editor, QString::fromLatin1(child->metaObject()->className()));
                        }
                    }
                }

                for (const auto& item : found)
                {
                    const QString key = QStringLiteral("%1|%2|%3").arg(catalog[candidates[c]].variant, item.second).arg(int(opener));
                    if (seen.contains(key))
                    {
                        continue;
                    }
                    seen.insert(key);

                    PopupCase popupCase;
                    popupCase.widget = candidates[c];
                    popupCase.steps = performed;
                    popupCase.kind = item.first;
                    popupCase.popupClass = item.second;
                    cases.push_back(popupCase);
                    LifetimeContext::trace(QStringLiteral("popup found: %1 %2 by %3").arg(catalog[candidates[c]].description, item.second, QString::fromLatin1(StepRunner::getName(opener))));
                }

                PopupCloser::closeAll();
                if (view && widget)
                {
                    // An open editor is closed by Escape
                    Input::key(widget, Qt::Key_Escape);
                    PopupCloser::closeAll();
                }
            }
        }
    }

    const QString problems = check.finish();
    if (!problems.isEmpty())
    {
        QTest::qFail(qPrintable(problems), __FILE__, __LINE__);
    }
    return cases;
}

QString LifetimeTest::runPopupCase(PageHost& host, QWidget* ownerWidget, const std::vector<WidgetInfo>& catalog, const PopupCase& popupCase, PopupAction action, bool* isPopupOpened)
{
    static const char* actionNames[] = { "owner deleted", "popup deleted", "window deleted", "window hidden", "popup accepted, then owner deleted",
                                         "theme, direction, style and language changed, popup accepted, owner deleted", "click into the margin of the popup, owner deleted" };

    if (isPopupOpened)
    {
        *isPopupOpened = false;
    }
    QPointer<QWidget> owner = ownerWidget;
    if (!owner)
    {
        return QString();
    }

    // A menu deleted by a case before is given again
    host.applyHints();

    // Context of the delayed action: the action refers to the data of this
    // function, it must not run after the return
    QObject caseContext;

    QStringList stepNames;
    for (Step step : popupCase.steps)
    {
        stepNames << QString::fromLatin1(StepRunner::getName(step));
    }
    const QString description = QStringLiteral("%1 while %2 (%3) of %4 is open (steps: %5)")
            .arg(QString::fromLatin1(actionNames[int(action)]), popupCase.popupClass,
                 popupCase.kind == PopupCase::Kind::Window ? QStringLiteral("window") : (popupCase.kind == PopupCase::Kind::ToolTip ? QStringLiteral("tooltip") : QStringLiteral("editor")),
                 catalog[popupCase.widget].description, stepNames.join(QLatin1Char(' ')));
    LifetimeContext::setStep(description);

    const DeletedWidget around = remember(owner);
    QPointer<QWidget> window = host.getWindow();
    bool isActionDone = false;

    auto performAction = [&host, owner, window, action, &isActionDone](QWidget* popup)
    {
        isActionDone = true;
        QPointer<QWidget> popupGuard(popup);
        switch (action)
        {
            case PopupAction::DeleteOwner:
            case PopupAction::Accept:
                if (action == PopupAction::Accept && popup)
                {
                    EventWatcher::applyPolicy(popup, EventWatcher::Policy::Accept);
                }
                delete owner.data();
                break;

            case PopupAction::DeletePopup:
            {
                // A popup in its own event loop may be an object on the stack of
                // its owner (QMenu menu; menu.exec()), it is closed - unless it is
                // a popup of the library or of a widget of the library, which never
                // runs a popup on the stack (CLAUDE.md, the lifetime rule); a part
                // of a widget of Qt (popup of a combo box, calendar) is closed too
                const bool isInternal = popup && (popup->inherits("QComboBoxPrivateContainer") || popup->inherits("QCalendarPopup") || popup->objectName().startsWith(QLatin1String("qt_")));
                bool isLibraryPopup = false;
                for (const QObject* object = popup; object && !isLibraryPopup; object = object->parent())
                {
                    isLibraryPopup = QByteArrayView(object->metaObject()->className()).startsWith("pdf::");
                }
                const bool isNested = QThread::currentThread()->loopLevel() > 0;
                if (popup && !isInternal && (!isNested || isLibraryPopup) && popup->windowType() != Qt::ToolTip)
                {
                    delete popup;
                }
                else if (popup)
                {
                    popup->close();
                }
                break;
            }

            case PopupAction::DeleteHost:
                host.destroy();
                break;

            case PopupAction::HideHost:
                if (window)
                {
                    window->hide();
                }
                break;

            case PopupAction::Environment:
            {
                // The popup (a menu, a palette, an editor) meets the changes of the application
                pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
                QApplication::setLayoutDirection(Qt::RightToLeft);
                QApplication::setStyle(new pdf::PDFWidgetStyle());
                QEvent languageChange(QEvent::LanguageChange);
                QCoreApplication::sendEvent(qApp, &languageChange);
                QCoreApplication::processEvents();
                pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
                QApplication::setLayoutDirection(Qt::LeftToRight);
                if (popupGuard && popupGuard->isVisible() && popupGuard->isWindow())
                {
                    EventWatcher::applyPolicy(popupGuard.data(), EventWatcher::Policy::Accept);
                }
                delete owner.data();
                break;
            }

            case PopupAction::MarginClick:
            {
                // A corner of the window of a popup with a shadow is its margin (PDFWidgetStyleImpl::handlePopupMarginEvent)
                if (popupGuard && popupGuard->isVisible() && popupGuard->isWindow() && popupGuard->windowHandle())
                {
                    const QPoint corner(1, popupGuard->height() - 2);
                    QTest::mouseMove(popupGuard->windowHandle(), corner);
                    QTest::mouseClick(popupGuard->windowHandle(), Qt::LeftButton, Qt::NoModifier, corner);
                }
                delete owner.data();
                break;
            }
        }

        // A popup left open (its owner or its window is gone or hidden) is used
        // further: its first action is activated (Return in a dialog)
        if (popupGuard && popupGuard->isVisible() && popupGuard->isWindow() && popupGuard->windowType() != Qt::ToolTip)
        {
            EventWatcher::applyPolicy(popupGuard.data(), EventWatcher::Policy::Accept);
        }
        if (popupGuard && popupGuard->isVisible() && popupGuard->isWindow() && popupGuard->windowType() != Qt::ToolTip)
        {
            popupGuard->close();
        }
    };

    // Steps before the opening one (as the discovery: an editor opened by them is closed by Escape)
    for (int i = 0; i + 1 < popupCase.steps.size() && owner; ++i)
    {
        StepRunner::run(owner, popupCase.steps[i]);
        PopupCloser::closeAll();
        if (owner && qobject_cast<QAbstractItemView*>(owner.data()))
        {
            Input::key(owner, Qt::Key_Escape);
            PopupCloser::closeAll();
        }
    }
    if (!owner)
    {
        return QString();
    }

    const int shownBefore = EventWatcher::getInstance()->getShownCount();
    const Step opener = popupCase.steps.back();
    QPointer<QWidget> editorBefore;
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(owner.data());
    QList<QPointer<QWidget>> childrenBefore;
    if (view)
    {
        for (QWidget* child : view->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            childrenBefore << child;
        }
    }

    if (popupCase.kind == PopupCase::Kind::Window)
    {
        // The action is performed, when the popup is shown (also inside of its own event loop)
        EventWatcher::getInstance()->setAction([performAction](QWidget* popup) { performAction(popup); }, &caseContext);
    }

    StepRunner::run(owner, opener);
    if (popupCase.kind == PopupCase::Kind::Window && !isActionDone && owner && EventWatcher::getInstance()->getShownCount() == shownBefore)
    {
        // The first press after a closed popup can be taken by Qt (the press,
        // which closes a popup, does not go on), the opening step is repeated
        StepRunner::run(owner, opener);
    }
    EventWatcher::getInstance()->setAction(EventWatcher::Action());

    bool isOpen = isActionDone;
    if (popupCase.kind != PopupCase::Kind::Window && !isActionDone)
    {
        QWidget* popup = nullptr;
        if (popupCase.kind == PopupCase::Kind::ToolTip)
        {
            popup = pdf::PDFToolTip::isVisible() ? pdf::PDFToolTip::getWindow() : nullptr;
            const QList<EventWatcher::ShownWindow> shown = EventWatcher::getInstance()->getShownSince(shownBefore);
            if (!popup && !shown.isEmpty())
            {
                popup = shown.back().window.data();
            }
            isOpen = popup || QToolTip::isVisible();
            if (action == PopupAction::DeletePopup)
            {
                // The windows of the tooltips belong to the library and to Qt
                popup = nullptr;
            }
        }
        else if (view && owner)
        {
            for (QWidget* child : view->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            {
                if (child->isVisible() && !childrenBefore.contains(child))
                {
                    popup = child;
                }
            }
            isOpen = popup != nullptr;
        }

        if (isOpen)
        {
            performAction(popup);
        }
    }

    if (isOpen)
    {
        ++m_counters["popupsOpen: actions with an open popup"];
    }
    else
    {
        ++m_counters["popupsOpen: popup not opened again"];
        LifetimeContext::trace(QStringLiteral("popup not opened again: %1").arg(description));
    }
    if (isPopupOpened)
    {
        *isPopupOpened = isOpen;
    }

    PopupCloser::closeAll();
    if (action == PopupAction::HideHost)
    {
        // The window is shown and used again
        host.activate();
        useRest(host, std::vector<DeletedWidget>(), false);
    }
    useAround(around);
    return description;
}

void LifetimeTest::popupsOpen()
{
    QFETCH(int, page);
    const std::vector<WidgetInfo> catalog = buildCatalog(page);
    if (QTest::currentTestFailed())
    {
        return;
    }
    const std::vector<PopupCase> cases = discoverPopups(page, catalog);
    if (QTest::currentTestFailed())
    {
        return;
    }
    m_counters["popupsOpen: popups found"] += int(cases.size());

    // Owner and popup deleted, popup accepted: the cases share a page
    for (PopupAction action : { PopupAction::DeleteOwner, PopupAction::DeletePopup, PopupAction::Accept, PopupAction::Environment, PopupAction::MarginClick })
    {
        std::vector<size_t> queue;
        for (size_t i = 0; i < cases.size(); ++i)
        {
            // The windows of the tooltips belong to the library and to Qt, they are not deleted;
            // the changes of the environment also with an editor, the margins of the windows only
            const bool isApplicable = action == PopupAction::DeleteOwner || cases[i].kind == PopupCase::Kind::Window ||
                                      ((action == PopupAction::DeletePopup || action == PopupAction::Environment) && cases[i].kind == PopupCase::Kind::Editor);
            if (isApplicable)
            {
                queue.push_back(i);
            }
        }

        // A popup, which has not opened again on a page used by other cases, is
        // tried as the first case of a fresh page; not opened there is a failure
        // (the case would be lost silently)
        std::unordered_set<size_t> mustBeFirst;
        QStringList notOpened;
        while (!queue.empty())
        {
            std::vector<size_t> postponed;
            std::vector<size_t> retried;
            QStringList descriptions;
            InstanceCheck check(QStringLiteral("popups"));
            ++m_counters["popupsOpen: page instances"];
            {
                PageHost host(page);
                host.applyHints();
                std::vector<QPointer<QWidget>> owners;
                for (size_t i : queue)
                {
                    owners.emplace_back(resolve(host, catalog[cases[i].widget]));
                }

                bool isFirst = true;
                for (size_t q = 0; q < queue.size(); ++q)
                {
                    const bool needsFreshPage = mustBeFirst.count(queue[q]) > 0;
                    if (!owners[q] || !host.getWindow() || (needsFreshPage && !isFirst))
                    {
                        if (isFirst && !owners[q])
                        {
                            QFAIL("the owner of a popup is missing in a fresh page");
                        }
                        postponed.push_back(queue[q]);
                        continue;
                    }
                    const bool isFirstOnPage = isFirst;
                    isFirst = false;
                    bool isOpened = false;
                    const QString description = runPopupCase(host, owners[q], catalog, cases[queue[q]], action, &isOpened);
                    descriptions << description;
                    if (!isOpened && isFirstOnPage)
                    {
                        notOpened << description;
                    }
                    else if (!isOpened)
                    {
                        ++m_counters["popupsOpen: popup opened again on a fresh page"];
                        mustBeFirst.insert(queue[q]);
                        retried.push_back(queue[q]);
                    }
                }

                useRest(host, std::vector<DeletedWidget>(), false);
            }

            const QString problems = check.finish();
            QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("%1\n  cases: %2").arg(problems, descriptions.join(QStringLiteral("; ")))));
            retried.insert(retried.end(), postponed.begin(), postponed.end());
            queue = std::move(retried);
        }
        QVERIFY2(notOpened.isEmpty(), qPrintable(QStringLiteral("popups found by the discovery, which do not open again on a fresh page:\n  %1").arg(notOpened.join(QStringLiteral("\n  ")))));
    }

    // The window deleted or hidden: a page for each case of a class
    QSet<QString> seen;
    for (const PopupCase& popupCase : cases)
    {
        const QString key = QStringLiteral("%1|%2|%3").arg(catalog[popupCase.widget].className, popupCase.popupClass).arg(int(popupCase.kind));
        if (seen.contains(key))
        {
            continue;
        }
        seen.insert(key);

        for (PopupAction action : { PopupAction::DeleteHost, PopupAction::HideHost })
        {
            if (action == PopupAction::HideHost && popupCase.kind != PopupCase::Kind::Window)
            {
                continue;
            }

            QString description;
            InstanceCheck check(QStringLiteral("popups"));
            ++m_counters["popupsOpen: page instances"];
            {
                PageHost host(page);
                host.applyHints();
                bool isOpened = false;
                description = runPopupCase(host, resolve(host, catalog[popupCase.widget]), catalog, popupCase, action, &isOpened);
                useRest(host, std::vector<DeletedWidget>(), false);
                QVERIFY2(isOpened, qPrintable(QStringLiteral("the popup does not open again on a fresh page: %1").arg(description)));
            }
            const QString problems = check.finish();
            QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("%1\n  case: %2").arg(problems, description)));
        }
    }
}

void LifetimeTest::itemViews()
{
    QFETCH(int, page);
    const std::vector<WidgetInfo> catalog = buildCatalog(page);
    if (QTest::currentTestFailed())
    {
        return;
    }

    // Views with items; one for each combination of the view, the delegate and the model
    std::vector<int> views;
    InstanceCheck viewCheck(QStringLiteral("item views of the page"));
    {
        PageHost host(page);
        QSet<QString> seen;
        for (int i = 0; i < int(catalog.size()); ++i)
        {
            const WidgetInfo& info = catalog[i];
            if (!info.isTarget || !info.isView || !info.isVisible || !isSelected(info))
            {
                continue;
            }

            QAbstractItemView* view = qobject_cast<QAbstractItemView*>(resolve(host, info));
            if (!view)
            {
                continue;
            }

            // The types of the values of the first row decide the editors
            QStringList types;
            for (int column = 0; column < view->model()->columnCount(view->rootIndex()); ++column)
            {
                types << QString::fromLatin1(view->model()->index(0, column, view->rootIndex()).data(Qt::EditRole).metaType().name());
            }
            const QString key = QStringLiteral("%1|%2|%3|%4").arg(info.className, QString::fromLatin1(view->itemDelegate() ? view->itemDelegate()->metaObject()->className() : ""),
                                                                  QString::fromLatin1(view->model()->metaObject()->className()), types.join(QLatin1Char(',')));
            if (!seen.contains(key))
            {
                seen.insert(key);
                views.push_back(i);
            }
        }
    }
    const QString viewProblems = viewCheck.finish();
    QVERIFY2(viewProblems.isEmpty(), qPrintable(viewProblems));
    m_counters["itemViews: views (variants)"] += int(views.size());
    if (views.empty())
    {
        return;
    }

    // EditingPopup: an editor with its own popup open (the combo box of a boolean,
    // the calendar of a date), opened by Alt+Down in the editor
    enum class State { Hovered, Pressed, Editing, ContextMenu, HoveredAction, EditingPopup };
    enum class Part { Model, Delegate, View, Viewport, Editor };
    enum class Operation { DeleteParts, ResetModel, SetModelNull, ReplaceDelegate, EditorSignal, RemoveRow, Sort, ReplaceHeader };

    struct Trial
    {
        State state;
        Operation operation;
        QList<Part> order;
        int column = 0;     ///< Column of the edited item (each type of PDFItemEditorFactory has its column in the gallery)
    };

    std::vector<Trial> trialList;
    const QList<QList<Part>> permutations =
    {
        { Part::Model, Part::Delegate, Part::View }, { Part::Model, Part::View, Part::Delegate },
        { Part::Delegate, Part::Model, Part::View }, { Part::Delegate, Part::View, Part::Model },
        { Part::View, Part::Model, Part::Delegate }, { Part::View, Part::Delegate, Part::Model }
    };
    for (const QList<Part>& order : permutations)
    {
        trialList.push_back({ State::Hovered, Operation::DeleteParts, order });
    }
    for (State state : { State::Pressed, State::Editing, State::ContextMenu, State::HoveredAction })
    {
        // The first deletion meets the state: the model (pressed only), the delegate, the view
        if (state == State::Pressed)
        {
            trialList.push_back({ state, Operation::DeleteParts, permutations[0] });
        }
        trialList.push_back({ state, Operation::DeleteParts, permutations[2] });
        trialList.push_back({ state, Operation::DeleteParts, permutations[4] });
    }
    trialList.push_back({ State::Hovered, Operation::DeleteParts, { Part::Viewport, Part::Model, Part::Delegate, Part::View } });
    trialList.push_back({ State::Pressed, Operation::DeleteParts, { Part::Model, Part::Viewport, Part::Delegate, Part::View } });
    trialList.push_back({ State::Editing, Operation::DeleteParts, { Part::Viewport, Part::View } });
    for (State state : { State::Pressed, State::Editing, State::ContextMenu })
    {
        trialList.push_back({ state, Operation::ResetModel, { Part::View } });
        trialList.push_back({ state, Operation::SetModelNull, { Part::Model, Part::View } });
        trialList.push_back({ state, Operation::ReplaceDelegate, { Part::Delegate, Part::View } });
    }

    // The editor of the library (PDFShortcutRecorder) deletes a part from inside of its
    // signals (a key recorded, Escape), the order gives the part
    for (Part part : { Part::Editor, Part::Delegate, Part::View, Part::Model })
    {
        trialList.push_back({ State::Editing, Operation::EditorSignal, { part } });
    }

    // Rows removed and sorted, the header replaced, while a row is pressed, edited or has its menu
    for (State state : { State::Pressed, State::Editing, State::ContextMenu })
    {
        trialList.push_back({ state, Operation::RemoveRow, { Part::View } });
    }
    for (State state : { State::Pressed, State::Editing })
    {
        trialList.push_back({ state, Operation::Sort, { Part::View } });
    }
    for (State state : { State::Hovered, State::Pressed })
    {
        trialList.push_back({ state, Operation::ReplaceHeader, { Part::View } });
    }

    // The editor of each column (the types of PDFItemEditorFactory), also with its popup open
    for (int column = 1; column < 8; ++column)
    {
        trialList.push_back({ State::Editing, Operation::DeleteParts, { Part::View }, column });
    }
    for (int column = 0; column < 8; ++column)
    {
        trialList.push_back({ State::EditingPopup, Operation::DeleteParts, { Part::View }, column });
    }
    trialList.push_back({ State::EditingPopup, Operation::DeleteParts, { Part::Model, Part::Delegate, Part::View }, 0 });

    static const char* stateNames[] = { "hovered", "pressed", "editing", "context menu", "hovered action", "editing with the popup of the editor" };
    static const char* operationNames[] = { "deleted", "model reset", "model set to null", "delegate replaced", "deleted from a signal of the editor", "row removed", "sorted", "header replaced" };
    static const char* partNames[] = { "model", "delegate", "view", "viewport", "editor" };

    // A state, which no view of the page has reached (no editor, no context
    // menu), is not tried again; a view, which has reached a state, must reach
    // it in every trial (a lost state would silently lose the trials)
    QSet<int> unreachedStates;
    std::map<std::tuple<size_t, int, int>, bool> stateOfView;
    QStringList lostStates;

    for (const Trial& trial : trialList)
    {
        const int stateKey = int(trial.state) * 100 + trial.column;
        if (unreachedStates.contains(stateKey))
        {
            continue;
        }
        bool isStateReached = false;

        QStringList orderNames;
        for (Part part : trial.order)
        {
            orderNames << QString::fromLatin1(partNames[int(part)]);
        }
        const QString trialName = QStringLiteral("%1 (column %4), %2: %3").arg(QString::fromLatin1(stateNames[int(trial.state)]), QString::fromLatin1(operationNames[int(trial.operation)]), orderNames.join(QStringLiteral(", "))).arg(trial.column);

        InstanceCheck check(QStringLiteral("item views, %1").arg(trialName));
        // The model deleted or taken from the view (Qt warns at a selection without a model)
        const ScopedFlag allowSelectionWithoutModel(MessageCollector::s_allowSelectionWithoutModel,
                                                    trial.order.contains(Part::Model) || trial.operation == Operation::SetModelNull || trial.operation == Operation::ResetModel);
        ++m_counters["itemViews: page instances"];
        {
            PageHost host(page);
            std::vector<QPointer<QAbstractItemView>> viewPointers;
            for (int index : views)
            {
                viewPointers.emplace_back(qobject_cast<QAbstractItemView*>(resolve(host, catalog[index])));
            }

            for (size_t v = 0; v < views.size(); ++v)
            {
                QPointer<QAbstractItemView> view = viewPointers[v];
                if (!view || !host.getWindow())
                {
                    continue;
                }
                if (view->model() && trial.column >= view->model()->columnCount(view->rootIndex()))
                {
                    // The view has no such column
                    continue;
                }

                LifetimeContext::setStep(QStringLiteral("%1: %2").arg(catalog[views[v]].description, trialName));
                const DeletedWidget around = remember(view);
                QPointer<QAbstractItemModel> model = view->model();
                QPointer<QAbstractItemDelegate> delegate = view->itemDelegate();
                const bool isLibraryDelegate = delegate && QByteArrayView(delegate->metaObject()->className()).startsWith("pdf::");
                QPointer<QWidget> viewport = view->viewport();

                // The state
                EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Keep);
                switch (trial.state)
                {
                    case State::Hovered:
                        StepRunner::run(view, Step::Hover);
                        break;
                    case State::Pressed:
                    {
                        QRect rect;
                        if (QWidget* target = StepRunner::getMouseTarget(view, rect, 0))
                        {
                            Input::mouse(target, rect.center(), QTest::MousePress);
                        }
                        break;
                    }
                    case State::Editing:
                    case State::EditingPopup:
                    {
                        if (model && model->rowCount(view->rootIndex()) > 0)
                        {
                            view->setCurrentIndex(model->index(0, trial.column, view->rootIndex()));
                        }
                        if (!StepRunner::run(view, Step::KeyF2))
                        {
                            StepRunner::run(view, Step::DoubleClick);
                        }
                        QPointer<QWidget> editor = view ? QApplication::focusWidget() : nullptr;
                        if (trial.state == State::EditingPopup && editor && view && view->viewport()->isAncestorOf(editor))
                        {
                            // The popup of the editor (a combo box, a calendar), kept open
                            Input::key(editor, Qt::Key_Down, Qt::AltModifier);
                            if (!QApplication::activePopupWidget() && editor)
                            {
                                Input::key(editor, Qt::Key_F4);
                            }
                        }
                        break;
                    }
                    case State::ContextMenu:
                        StepRunner::run(view, Step::RightClickClose);
                        break;
                    case State::HoveredAction:
                    {
                        const QPoint point = StepRunner::getActionPoint(view, 0);
                        if (point.x() >= 0)
                        {
                            Input::mouse(view->viewport(), point, QTest::MouseMove);
                        }
                        else
                        {
                            StepRunner::run(view, Step::Hover);
                        }
                        break;
                    }
                }
                EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Close);

                // Is the state there?
                bool isEstablished = view;
                if (view && (trial.state == State::Editing || trial.state == State::EditingPopup))
                {
                    isEstablished = false;
                    for (QWidget* child : view->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                    {
                        isEstablished = isEstablished || child->isVisible();
                    }
                }
                else if (view && trial.state == State::ContextMenu)
                {
                    isEstablished = QApplication::activePopupWidget() != nullptr;
                }
                else if (view && trial.state == State::Pressed)
                {
                    isEstablished = Input::s_pressed;
                }
                ++m_counters[isEstablished ? "itemViews: trials in the state" : "itemViews: trials without the state (no editor, no menu)"];
                isStateReached = isStateReached || isEstablished;
                if (trial.state == State::EditingPopup && isEstablished && QApplication::activePopupWidget())
                {
                    ++m_counters["itemViews: trials with the popup of an editor"];
                }

                // The first trial of a state decides, whether the view reaches it
                const auto stateKeyOfView = std::make_tuple(v, int(trial.state), trial.column);
                auto stateIt = stateOfView.find(stateKeyOfView);
                if (stateIt == stateOfView.end())
                {
                    stateOfView.emplace(stateKeyOfView, isEstablished);
                }
                else if (stateIt->second && !isEstablished && trial.state != State::Hovered && trial.state != State::HoveredAction)
                {
                    lostStates << QStringLiteral("%1: %2").arg(catalog[views[v]].description, trialName);
                }

                // The operation
                if (view)
                {
                    switch (trial.operation)
                    {
                        case Operation::DeleteParts:
                            break;
                        case Operation::ResetModel:
                            if (QStandardItemModel* standardModel = qobject_cast<QStandardItemModel*>(model.data()))
                            {
                                standardModel->clear();
                            }
                            break;
                        case Operation::SetModelNull:
                            view->setModel(nullptr);
                            break;
                        case Operation::ReplaceDelegate:
                            if (isLibraryDelegate)
                            {
                                view->setItemDelegate(new QStyledItemDelegate(view));
                            }
                            break;
                        case Operation::EditorSignal:
                            break;
                        case Operation::RemoveRow:
                            if (model && model->rowCount(view->rootIndex()) > 0)
                            {
                                model->removeRows(0, 1, view->rootIndex());
                            }
                            break;
                        case Operation::Sort:
                            if (model)
                            {
                                model->sort(0, Qt::DescendingOrder);
                            }
                            break;
                        case Operation::ReplaceHeader:
                            if (QTreeView* treeView = qobject_cast<QTreeView*>(view.data()))
                            {
                                treeView->setHeader(new QHeaderView(Qt::Horizontal, treeView));
                            }
                            else if (QTableView* tableView = qobject_cast<QTableView*>(view.data()))
                            {
                                tableView->setHorizontalHeader(new QHeaderView(Qt::Horizontal, tableView));
                            }
                            break;
                    }
                    Input::flush();
                }

                if (trial.operation == Operation::EditorSignal)
                {
                    // An open editor with signals of the library
                    QPointer<QWidget> editor;
                    if (view)
                    {
                        for (QWidget* child : view->viewport()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                        {
                            if (child->isVisible() && !SignalReceiver::getTestedSignals(child).isEmpty())
                            {
                                editor = child;
                            }
                        }
                    }

                    if (editor)
                    {
                        QObject* victimObject = nullptr;
                        switch (trial.order.front())
                        {
                            case Part::Editor:
                                victimObject = editor.data();
                                break;
                            case Part::Delegate:
                                victimObject = isLibraryDelegate ? delegate.data() : nullptr;
                                break;
                            case Part::View:
                                victimObject = view.data();
                                break;
                            case Part::Model:
                                victimObject = model.data();
                                break;
                            default:
                                break;
                        }

                        SignalReceiver deleter;
                        deleter.setVictim(victimObject);
                        deleter.connectSignals(editor, SignalReceiver::getTestedSignals(editor));
                        if (editor)
                        {
                            Input::key(editor, Qt::Key_K, Qt::ControlModifier);
                        }
                        if (editor && !deleter.hasDeleted())
                        {
                            StepRunner::run(editor, Step::KeyEscape);
                        }
                        ++m_counters[deleter.hasDeleted() ? "itemViews: deletions from a signal of an editor" : "itemViews: editor without a signal"];
                        deleter.disconnectAll();
                        Input::flush();
                    }

                    PopupCloser::closeAll();
                    useAround(around);
                    continue;
                }

                // The parts in the order
                for (Part part : trial.order)
                {
                    switch (part)
                    {
                        case Part::Model:
                            delete model.data();
                            break;
                        case Part::Delegate:
                            if (isLibraryDelegate)
                            {
                                delete delegate.data();
                            }
                            // The delegates of the columns and the rows of the library too;
                            // Qt keeps a deleted delegate of a column or of a row and paints
                            // by it (QAbstractItemViewPrivate::delegateForIndex of Qt 6.11
                            // returns the null pointer), so it is taken from the view first
                            for (int section = 0; view && view->model() && section < 32; ++section)
                            {
                                QPointer<QAbstractItemDelegate> columnDelegate = view->itemDelegateForColumn(section);
                                QPointer<QAbstractItemDelegate> rowDelegate = view->itemDelegateForRow(section);
                                if (columnDelegate && QByteArrayView(columnDelegate->metaObject()->className()).startsWith("pdf::"))
                                {
                                    view->setItemDelegateForColumn(section, nullptr);
                                    delete columnDelegate.data();
                                }
                                if (view && rowDelegate && QByteArrayView(rowDelegate->metaObject()->className()).startsWith("pdf::"))
                                {
                                    view->setItemDelegateForRow(section, nullptr);
                                    delete rowDelegate.data();
                                }
                            }
                            break;
                        case Part::View:
                            delete view.data();
                            break;
                        case Part::Viewport:
                            if (view)
                            {
                                // The viewport is replaced, Qt deletes the old one
                                view->setViewport(new QWidget());
                            }
                            break;
                        case Part::Editor:
                            break;
                    }

                    // The context menu, which is still open after the first deletion,
                    // runs its first action (the action of the item of a deleted part)
                    if (trial.state == State::ContextMenu && part == trial.order.front())
                    {
                        if (QWidget* popup = QApplication::activePopupWidget())
                        {
                            EventWatcher::applyPolicy(popup, EventWatcher::Policy::Accept);
                        }
                    }

                    Input::flush();
                    if (view && view->isVisible())
                    {
                        StepRunner::run(view, Step::Hover);
                        view->grab();
                    }
                    else if (viewport && viewport->isVisible())
                    {
                        viewport->grab();
                    }
                }

                PopupCloser::closeAll();
                useAround(around);
            }

            useRest(host, std::vector<DeletedWidget>(), false);
            LifetimeContext::setStep(QStringLiteral("deleting the page %1").arg(host.getId()));
        }

        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));

        if (!isStateReached)
        {
            unreachedStates.insert(stateKey);
        }
    }
    QVERIFY2(lostStates.isEmpty(), qPrintable(QStringLiteral("views, which have reached a state in an earlier trial, but not in a later one:\n  %1").arg(lostStates.join(QStringLiteral("\n  ")))));
}

void LifetimeTest::regressionPasswordButtonDeleted()
{
    // PDFLineEdit held the button of a password field by a plain pointer: the
    // button deleted by the application crashed the field at the next change
    // of the layout direction (or a resize)
    InstanceCheck check(QStringLiteral("password button deleted"));
    {
        QWidget window;
        pdf::PDFLineEdit* field = new pdf::PDFLineEdit(&window);
        field->setPasswordField();
        field->resize(240, 40);
        window.resize(300, 80);
        EventWatcher::getInstance()->setHostWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(field->getPasswordButton());
        delete field->getPasswordButton();
        QVERIFY(!field->getPasswordButton());
        QVERIFY(field->isPasswordField());

        field->setLayoutDirection(Qt::RightToLeft);
        field->resize(260, 40);
        field->setText(QStringLiteral("secret"));
        field->setPasswordField();
        QVERIFY(!field->grab().isNull());
        QVERIFY(!field->getPasswordButton());
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionMessageBoxParts()
{
    // PDFMessageBox held its parts by plain pointers: a part deleted by the
    // application (the button box) crashed the message box when it was shown
    for (int part = 0; part < 6; ++part)
    {
        InstanceCheck check(QStringLiteral("part %1 of a message box deleted").arg(part));
        {
            QWidget window;
            window.resize(500, 400);
            EventWatcher::getInstance()->setHostWindow(&window);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            pdf::PDFMessageBox* messageBox = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Question, QStringLiteral("Title"), QStringLiteral("Text"),
                                                                    QDialogButtonBox::Yes | QDialogButtonBox::No, &window);
            messageBox->setWindowFlags(Qt::Widget);
            messageBox->setDetailedText(QStringLiteral("Details"));
            messageBox->setCheckBoxText(QStringLiteral("Do not show again"));

            QList<QWidget*> parts;
            parts << messageBox->getButtonBox();
            for (QLabel* label : messageBox->findChildren<QLabel*>())
            {
                parts << label;
            }
            parts << messageBox->findChild<QCheckBox*>();
            parts << messageBox->findChild<QPlainTextEdit*>();
            QWidget* deleted = part < parts.size() ? parts[part] : nullptr;
            QVERIFY(deleted);
            delete deleted;

            messageBox->show();
            Input::flush();
            messageBox->setDetailedText(QString(1200, QLatin1Char('x')));
            messageBox->setCheckBoxText(QStringLiteral("Again"));
            messageBox->setDefaultButton(QDialogButtonBox::Yes);
            messageBox->setEscapeButton(QDialogButtonBox::No);
            Q_UNUSED(messageBox->addButton(QStringLiteral("More"), QDialogButtonBox::ActionRole));
            Q_UNUSED(messageBox->isCheckBoxChecked());
            QTest::keyClick(messageBox, Qt::Key_C, Qt::ControlModifier);
            QVERIFY(!messageBox->grab().isNull());
            messageBox->reject();
            Input::flush();
        }
        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }
}

void LifetimeTest::regressionMessageBoxParentDeleted()
{
    // PDFMessageBox::information and the others ran a message box on the stack
    // with the parent of the caller: the parent deleted while the message box
    // was shown deleted it as its child and the stack deleted it again
    InstanceCheck check(QStringLiteral("parent of a message box deleted while it is shown"));
    {
        QPointer<QWidget> window = new QWidget();
        window->resize(500, 400);
        EventWatcher::getInstance()->setHostWindow(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        EventWatcher::getInstance()->setAction([window](QWidget* popup)
        {
            QVERIFY(qobject_cast<pdf::PDFMessageBox*>(popup));
            delete window.data();
        });
        const QDialogButtonBox::StandardButton result = pdf::PDFMessageBox::warning(window, QStringLiteral("Title"), QStringLiteral("Text"));
        QCOMPARE(result, QDialogButtonBox::NoButton);
        QVERIFY(!window);
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionTabCloseButtonDeleted()
{
    // QTabBar keeps the buttons of the tabs by plain pointers: a close button of
    // PDFDocumentTabBar deleted by the application crashed the tab bar at its
    // next paint; the slot, which takes it out of the tab, must not reach the
    // tab bar, when the tab bar itself is destroyed
    InstanceCheck check(QStringLiteral("close button of a document tab deleted"));
    {
        QWidget window;
        pdf::PDFDocumentTabBar* tabBar = new pdf::PDFDocumentTabBar(&window);
        tabBar->addTab(QStringLiteral("Home"));
        tabBar->addTab(QStringLiteral("Report.pdf"));
        tabBar->addTab(QStringLiteral("Invoice.pdf"));
        tabBar->setTabClosable(1, true);
        tabBar->setTabClosable(2, true);
        window.resize(500, 60);
        EventWatcher::getInstance()->setHostWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(tabBar->isTabClosable(1));
        const QTabBar::ButtonPosition side = tabBar->tabButton(1, QTabBar::RightSide) ? QTabBar::RightSide : QTabBar::LeftSide;
        delete tabBar->tabButton(1, side);
        QVERIFY(!tabBar->isTabClosable(1));
        QVERIFY(tabBar->isTabClosable(2));
        QVERIFY(!tabBar->grab().isNull());
        tabBar->setLayoutDirection(Qt::RightToLeft);
        QVERIFY(!tabBar->grab().isNull());

        // The tab bar deleted with its remaining close button
        delete tabBar;
        Input::flush();
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionKeyValueListMenu()
{
    // The context menu of PDFKeyValueList was an object on the stack with the
    // list as its parent: the list deleted while the menu was shown deleted the
    // menu as its child and the stack deleted it again (heap corruption)
    InstanceCheck check(QStringLiteral("key value list deleted while its menu is shown"));
    {
        QPointer<QWidget> window = new QWidget();
        QPointer<pdf::PDFKeyValueList> list = new pdf::PDFKeyValueList(window);
        list->addRow(QStringLiteral("Pages"), QStringLiteral("12"));
        list->addRow(QStringLiteral("Size"), QStringLiteral("1.2 MB"));
        list->resize(300, list->heightForWidth(300));
        window->resize(320, 200);
        EventWatcher::getInstance()->setHostWindow(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        bool isMenuShown = false;
        EventWatcher::getInstance()->setAction([list, &isMenuShown](QWidget* popup)
        {
            isMenuShown = qobject_cast<QMenu*>(popup) != nullptr;
            delete list.data();
        });
        const QPoint position = list->getRowRect(0).center();
        Input::mouse(list, position, QTest::MouseClick, Qt::RightButton);
        EventWatcher::getInstance()->setAction(EventWatcher::Action());
        QVERIFY(isMenuShown);
        QVERIFY(!list);
        delete window.data();
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionMessageBoxWithoutButtons()
{
    // PDFMessageBox kept its main layout by a plain pointer (a layout deleted by
    // the application crashed the next show), and a message box, whose button
    // box was deleted, could not be closed by Esc or close()
    InstanceCheck check(QStringLiteral("message box without its layout and buttons"));
    {
        QWidget window;
        window.resize(500, 400);
        EventWatcher::getInstance()->setHostWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QPointer<pdf::PDFMessageBox> messageBox = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok, &window);
        messageBox->setWindowFlags(Qt::Widget);
        delete messageBox->layout();
        delete messageBox->getButtonBox();
        messageBox->show();
        Input::flush();
        QVERIFY(messageBox->isVisible());
        QVERIFY(!messageBox->grab().isNull());

        messageBox->reject();
        QVERIFY(!messageBox->isVisible());
        QCOMPARE(messageBox->getClickedStandardButton(), QDialogButtonBox::NoButton);

        messageBox->show();
        QVERIFY(messageBox->close());
        QVERIFY(!messageBox->isVisible());
        delete messageBox.data();

        // The button box kept, all its buttons deleted (by a slot of a button)
        QPointer<pdf::PDFMessageBox> withoutButtons = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Question, QStringLiteral("Title"), QStringLiteral("Text"),
                                                                             QDialogButtonBox::Yes | QDialogButtonBox::No, &window);
        withoutButtons->setWindowFlags(Qt::Widget);
        qDeleteAll(withoutButtons->getButtonBox()->buttons());
        withoutButtons->show();
        Input::flush();
        QTest::keyClick(withoutButtons, Qt::Key_Escape);
        QVERIFY(!withoutButtons->isVisible());
        delete withoutButtons.data();
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionPaletteDeletedBySwatch()
{
    // PDFColorPalette selected its swatch of the current color (signal toggled)
    // and went on with the palette and the button: a slot of the swatch, which
    // deleted the palette (or the color button), crashed PDFColorButton::showPalette,
    // setCurrentColor and setSuggestedColors (heap corruption, found by the test)
    for (int mode = 0; mode < 4; ++mode)
    {
        InstanceCheck check(QStringLiteral("palette deleted by a slot of a swatch, mode %1").arg(mode));
        {
            QWidget window;
            window.resize(400, 300);
            EventWatcher::getInstance()->setHostWindow(&window);
            QPointer<pdf::PDFColorButton> button = new pdf::PDFColorButton(QColor(Qt::black), QString(), &window);
            QPointer<pdf::PDFColorPalette> palette = button->getPalette();
            palette->setSuggestedColors({ pdf::PDFColorPalette::Entry{ QColor(Qt::black), QStringLiteral("Black") }, pdf::PDFColorPalette::Entry{ QColor(Qt::red), QStringLiteral("Red") } });
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            // The swatches of both colors delete the palette (mode 0, 2, 3) or the button (mode 1) when toggled
            QObject context;
            for (int i = 0; i < 2; ++i)
            {
                pdf::PDFColorSwatch* swatch = palette->getSuggestedSwatch(i);
                QVERIFY(swatch);
                QObject::connect(swatch, &QAbstractButton::toggled, &context, [mode, button, palette]()
                {
                    if (mode == 1)
                    {
                        delete button.data();
                    }
                    else
                    {
                        delete palette.data();
                    }
                });
            }

            switch (mode)
            {
                case 0:
                case 1:
                    button->setColor(QColor(Qt::red));
                    QVERIFY(!button || !button->showPalette());
                    break;
                case 2:
                    palette->setCurrentColor(QColor(Qt::red));
                    break;
                case 3:
                    palette->setCurrentColor(QColor(Qt::red));
                    if (palette)
                    {
                        palette->setSuggestedColors({ pdf::PDFColorPalette::Entry{ QColor(Qt::blue), QStringLiteral("Blue") } });
                    }
                    break;
            }
            QVERIFY(!palette || mode == 1);
            Input::flush();
            if (button)
            {
                QVERIFY(!button->grab().isNull());
                QVERIFY(!button->isPaletteVisible());
            }
        }
        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }
}

void LifetimeTest::regressionMessageBoxDeletedInExec()
{
    // The message box of PDFMessageBox::information deleted by the application
    // inside of its own event loop, synchronously and by deleteLater
    for (const bool isDeferred : { false, true })
    {
        InstanceCheck check(QStringLiteral("message box deleted inside of its loop (%1)").arg(isDeferred ? QStringLiteral("deleteLater") : QStringLiteral("delete")));
        {
            QWidget window;
            window.resize(500, 400);
            EventWatcher::getInstance()->setHostWindow(&window);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            bool isShown = false;
            QObject context;
            EventWatcher::getInstance()->setAction([&isShown, isDeferred](QWidget* popup)
            {
                isShown = qobject_cast<pdf::PDFMessageBox*>(popup) != nullptr;
                if (isDeferred)
                {
                    popup->deleteLater();
                }
                else
                {
                    delete popup;
                }
            }, &context);
            const QDialogButtonBox::StandardButton result = pdf::PDFMessageBox::information(&window, QStringLiteral("Title"), QStringLiteral("Text"));
            QVERIFY(isShown);
            QCOMPARE(result, QDialogButtonBox::NoButton);
            QVERIFY(window.findChildren<pdf::PDFMessageBox*>().isEmpty());
        }
        const QString problems = check.finish();
        QVERIFY2(problems.isEmpty(), qPrintable(problems));
    }
}

void LifetimeTest::regressionStyleReplacedInForwardedClick()
{
    // A click into the shadow margin of a submenu is forwarded by the style to
    // the parent menu below it; the action activated by it replaces the style
    // of the application, which deletes the object of the style, that forwards
    // the event (it wrote to its member after the forwarded event)
    InstanceCheck check(QStringLiteral("style replaced by an action of a forwarded click"));
    {
        QWidget window;
        window.resize(400, 300);
        EventWatcher::getInstance()->setHostWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QMenu* menu = new QMenu(&window);
        QMenu* submenu = menu->addMenu(QStringLiteral("Submenu"));
        submenu->addAction(QStringLiteral("Inner"));
        bool isReplaced = false;
        QAction* replaceAction = nullptr;
        for (int i = 0; i < 6; ++i)
        {
            QAction* action = menu->addAction(QStringLiteral("Replace the style %1").arg(i));
            QObject::connect(action, &QAction::triggered, menu, [&isReplaced]()
            {
                isReplaced = true;
                QApplication::setStyle(new pdf::PDFWidgetStyle());
            });
            replaceAction = action;
        }

        EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Keep);
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        menu->setActiveAction(menu->actions().first());
        QTest::keyClick(menu, Qt::Key_Right);
        QVERIFY(QTest::qWaitFor([submenu]() { return submenu->isVisible(); }, 2000));
        EventWatcher::getInstance()->setPolicy(EventWatcher::Policy::Close);

        // A point of the left margin of the submenu, which lies over an action of the parent menu
        std::optional<QPoint> found;
        for (int x = 1; x < 24 && !found; ++x)
        {
            for (int y = 1; y < submenu->height() && !found; y += 2)
            {
                const QPoint global = submenu->mapToGlobal(QPoint(x, y));
                const QPoint inMenu = menu->mapFromGlobal(global);
                if (submenu->childAt(QPoint(x, y)) == nullptr && menu->actionAt(inMenu) && menu->actionAt(inMenu) != menu->actions().first() &&
                    !submenu->actionAt(QPoint(x, y)))
                {
                    found = QPoint(x, y);
                }
            }
        }
        Q_UNUSED(replaceAction);
        if (found)
        {
            QTest::mouseMove(submenu->windowHandle(), *found);
            QTest::mouseClick(submenu->windowHandle(), Qt::LeftButton, Qt::NoModifier, *found);
            Input::flush();
            QVERIFY(isReplaced);
        }
        else
        {
            // The submenu does not overlap its parent (no margins of the shadow on this platform)
            qInfo("no margin of the submenu over the parent menu");
        }
        PopupCloser::closeAll();
        delete menu;
        QVERIFY(pdf::PDFWidgetStyle::isActive());
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionCalendarViewportReplaced()
{
    // PDFCalendarWidget held the viewport of its view by a plain pointer: the
    // viewport replaced (deleted by Qt) was used at the next paint of a cell
    InstanceCheck check(QStringLiteral("viewport of the calendar replaced"));
    {
        QWidget window;
        window.resize(400, 360);
        pdf::PDFCalendarWidget* calendar = new pdf::PDFCalendarWidget(&window);
        calendar->resize(calendar->sizeHint());
        EventWatcher::getInstance()->setHostWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QAbstractItemView* view = calendar->findChild<QAbstractItemView*>();
        QVERIFY(view);
        view->setViewport(new QWidget());
        Input::flush();
        QVERIFY(!calendar->grab().isNull());
        Input::mouse(view, view->rect().center(), QTest::MouseMove);
        QVERIFY(!calendar->grab().isNull());
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

void LifetimeTest::regressionFocusFrameReparented()
{
    // The focus frame of the style kept its parent from the old window, when the
    // button with the keyboard focus (or its container) moved to another window
    // (QWidget::mapTo warned, the frame was drawn into the old window)
    InstanceCheck check(QStringLiteral("focus frame of a button moved to another window"));
    {
        QWidget window;
        window.resize(300, 200);
        QWidget* container = new QWidget(&window);
        container->setGeometry(10, 10, 200, 100);
        QPushButton* button = new QPushButton(QStringLiteral("Button"), container);
        button->setGeometry(10, 10, 120, 32);
        EventWatcher::getInstance()->setHostWindow(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(Input::focus(button));
        Input::key(button, Qt::Key_F24);
        QVERIFY(QTest::qWaitFor([button]() { return findFocusFrame(button) != nullptr; }, 1000));

        QWidget other;
        other.resize(300, 200);
        EventWatcher::getInstance()->setHostWindow(&other);
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        container->setParent(&other);
        container->show();
        container->move(20, 30);
        Input::flush();
        QVERIFY(!other.grab().isNull());
        QVERIFY(!window.grab().isNull());

        // The frame is attached again in the new window, or not at all (the focus left the old window)
        QFocusFrame* frame = findFocusFrame(button);
        QVERIFY(!frame || frame->window() == &other);
        EventWatcher::getInstance()->setHostWindow(&window);
    }
    const QString problems = check.finish();
    QVERIFY2(problems.isEmpty(), qPrintable(problems));
}

int main(int argc, char** argv)
{
    // QSignalDumper of QtTest (-vs) replaces the signal spy callbacks of the tracker
    for (int i = 1; i < argc; ++i)
    {
        if (qstrcmp(argv[i], "-vs") == 0)
        {
            fprintf(stderr, "UnitTestsFluentLifetime: the option -vs replaces the signal checks of the test, it is not supported\n");
            return 1;
        }
    }

    // The objects are tracked from the first one. QtTest calls exit() for the
    // options -functions and -datatags, while the application lives: the hooks
    // are removed before the static data of the tracker are destroyed (the
    // handler registered after their construction runs before their destructors)
    ObjectTracker::install();
    std::atexit(&ObjectTracker::uninstall);

    int result = 0;
    {
        QApplication application(argc, argv);
        application.setAttribute(Qt::AA_Use96Dpi, true);
        LifetimeTest test;
        QTEST_SET_MAIN_SOURCE_PATH
        result = QTest::qExec(&test, argc, argv);
    }
    ObjectTracker::uninstall();
    return result;
}

#include "tst_fluentlifetimetest.moc"
