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

// Generic lifetime test of the pages of the new application (step 9): every route
// of the route table of the navigator (newapp_navigation.md 3) is driven without
// a list of pages in the test - the cases are made from the patterns, their
// parameters and query keys (PagesRouteCatalog), so a route added later is driven
// at once (routeInventory fails only for a route, whose parameters the catalog
// cannot fill).
//
//  - deletionOrders: the route opened in its tab (Home tab, New tab, Help tab, the
//    tab of a document), a child focused by the keyboard and another one hovered,
//    optionally with a popup (a menu, a context menu, a panel of the document
//    toolbar), a dialog of the page or pending work (a notification of another
//    connection, an update check, a search, thumbnails and folder information of
//    Recent, the minute ticker), then closed, or the page, the tab and the window
//    deleted in every order;
//  - deleteFromEverySignal: the route opened, a child focused and hovered, its
//    filter field and the field of the global search typed into, the popup of the
//    global search used by the pointer and Enter (a result activated), its popups
//    opened, Back / Forward, a test translator installed (LanguageChange), the
//    theme switched, the window resized to 720 x 480, another connection
//    writing, a tick of the minute ticker, an update check, the tab closed - run
//    once with a recorder of every signal of the application, then once per
//    signal with the tab, the window or the page deleted synchronously from it
//    (the pattern of UnitTestsAppToolsLifetime).
//
// After every deletion: no popup or window of the deleted parts is left visible,
// the surviving window works (Home, a document, the AI page); after the fixture is
// gone no object created by the run is left alive (objects tracked by the hooks of
// QtCore), no single shot timer of the run is still connected to a receiver
// (it would run code of the application after its window and context are gone),
// and no warning of Qt was printed. The detectors are checked by detectorSelfTest.
// Freed memory stays readable in a Release build on Windows - run the test under
// ASan (CLAUDE.md).

#include "apptoolsfixture.h"

#include "pdfappbuiltintoolcollections.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpage.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapphelpcatalog.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappminuteticker.h"
#include "pdfappnavigator.h"
#include "pdfapppage.h"
#include "pdfapproute.h"
#include "pdfapproutetable.h"
#include "pdfappsearchwidgets.h"
#include "pdfapptab.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfrecentrepository.h"
#include "pdfsettingsrepository.h"
#include "pdftoolrepository.h"
#include "pdfupdatechecker.h"
#include "pdfwhatsnew.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QHash>
#include <QLineEdit>
#include <QMenu>
#include <QMetaMethod>
#include <QMutex>
#include <QPushButton>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QClipboard>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTranslator>
#include <QtTest>

#include <QtCore/private/qhooks_p.h>
#include <QtCore/private/qmetaobject_p.h>
#include <QtCore/private/qobject_p.h>

#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_set>
#include <vector>

using PagesObjectSet = std::unordered_set<const QObject*>;

/// Tracks the living objects by the hooks of QtCore (installed before the
/// application object is created)
class PagesObjectTracker
{
public:
    PagesObjectTracker() = delete;

    static void install()
    {
        qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(&PagesObjectTracker::onAdd);
        qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(&PagesObjectTracker::onRemove);
    }

    static void uninstall()
    {
        qtHookData[QHooks::AddQObject] = 0;
        qtHookData[QHooks::RemoveQObject] = 0;
    }

    static PagesObjectSet getLiveObjects()
    {
        std::lock_guard<std::mutex> lock(getMutex());
        return getLive();
    }

    /// Describes the objects of the thread of the user interface, which live now and
    /// did not live in \p before, except of the objects, which are no leak:
    ///  - a thread of the global thread pool of Qt (idle threads expire after 30 s);
    ///  - an SVG renderer without a parent (the global cache of the icons of
    ///    Pdf4QtLibFluent, one renderer per icon and width of its strokes);
    ///  - a pending single shot timer of Qt, whose receiver is gone (Qt has removed
    ///    the connection of its timeout, it deletes itself at its timeout);
    ///  - a gesture of the gesture manager of Qt (the gestures of a destroyed widget,
    ///    which grabbed them - a scroll area with QScroller -, are deleted by
    ///    QGestureManager at the next event it filters, m_gesturesToDelete);
    ///  - the data of the clipboard (the clipboard of the platform owns the last data
    ///    set by the application, Copy build info of About);
    ///  - a descendant of \p ignoredRoot (the silent server of the test).
    /// A pending single shot timer, whose timeout is still connected, is a leak: it
    /// runs code after the window and the context of the run are gone.
    static QStringList describeNewObjects(const PagesObjectSet& before, const QObject* ignoredRoot, int limit)
    {
        // Asked before the lock (the clipboard may create objects)
        const QObject* clipboardData = QApplication::clipboard() ? QApplication::clipboard()->mimeData(QClipboard::Clipboard) : nullptr;

        std::lock_guard<std::mutex> lock(getMutex());
        QStringList result;
        const QThread* mainThread = QCoreApplication::instance()->thread();
        for (const QObject* object : getLive())
        {
            if (before.count(object) || object->thread() != mainThread)
            {
                continue;
            }

            const QByteArray className = object->metaObject()->className();
            if (className == "QThreadPoolThread" || (className == "QSvgRenderer" && !object->parent()) ||
                (object->parent() && QByteArray(object->parent()->metaObject()->className()) == "QGestureManager"))
            {
                continue;
            }

            bool isIgnored = object == clipboardData;
            for (const QObject* current = object; current && !isIgnored; current = current->parent())
            {
                isIgnored = current == ignoredRoot;
            }
            if (isIgnored)
            {
                continue;
            }

            if (className == "QSingleShotTimer")
            {
                const int index = object->metaObject()->indexOfSignal("timeout()");
                const int signalIndex = index >= 0 ? QMetaObjectPrivate::signalIndex(object->metaObject()->method(index)) : -1;
                if (signalIndex < 0 || !QObjectPrivate::get(const_cast<QObject*>(object))->isSignalConnected(uint(signalIndex), false))
                {
                    continue;
                }
                result << QStringLiteral("a pending single shot timer, whose timeout is connected");
            }
            else
            {
                const QObject* parent = object->parent();
                result << QStringLiteral("%1 '%2' (parent %3)").arg(QString::fromLatin1(className), object->objectName(),
                                                                   parent ? QString::fromLatin1(parent->metaObject()->className()) : QStringLiteral("none"));
            }

            if (result.size() >= limit)
            {
                result << QStringLiteral("...");
                break;
            }
        }
        return result;
    }

private:
    static void onAdd(QObject* object)
    {
        std::lock_guard<std::mutex> lock(getMutex());
        getLive().insert(object);
    }

    static void onRemove(QObject* object)
    {
        std::lock_guard<std::mutex> lock(getMutex());
        getLive().erase(object);
    }

    // Never destroyed: QtTest ends the process by exit() for -functions and
    // -datatags, the objects of the libraries are destroyed after the static data
    // of the test, while the hooks are installed
    static std::mutex& getMutex()
    {
        static std::mutex* mutex = new std::mutex();
        return *mutex;
    }

    static PagesObjectSet& getLive()
    {
        static PagesObjectSet* live = new PagesObjectSet();
        return *live;
    }
};

/// Collects the warnings of Qt; a warning is an error of the test, unless it is in
/// the short list of the expected ones
class PagesMessageCollector
{
public:
    PagesMessageCollector() = delete;

    static void install()
    {
        s_previous = qInstallMessageHandler(&PagesMessageCollector::handler);
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
        // The offscreen platform plugin cannot propagate the size hints of a window,
        // raise a window and grab the keyboard (QPlatformWindow); it looks for the
        // fonts in the directory lib/fonts of Qt on Windows (the application has its
        // own fonts)
        return message.startsWith(QLatin1String("QFontDatabase: Cannot find font directory")) ||
               message == QLatin1String("This plugin does not support propagateSizeHints()") ||
               message == QLatin1String("This plugin does not support raise()") ||
               message == QLatin1String("This plugin does not support grabbing the keyboard");
    }

private:
    static void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        const bool isWarning = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
        if (isWarning && isAllowed(message))
        {
            return;
        }

        if (isWarning)
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_messages.size() < 20)
            {
                s_messages << message;
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

std::mutex PagesMessageCollector::s_mutex;
QStringList PagesMessageCollector::s_messages;
QtMessageHandler PagesMessageCollector::s_previous = nullptr;

/// Emissions of the signals of the line edits in the thread of the user interface
/// (the signal spy callbacks of QtCore): Qt does not survive the deletion of a line
/// edit inside of its own signals - QLineEdit and QWidgetLineControl use the deleted
/// line edit after textEdited / textChanged / returnPressed / editingFinished. A
/// key event, which an event filter of the application consumes, is no such
/// emission: QApplication::notify guards its receiver by a QPointer.
class PagesLineEditEmissions
{
public:
    PagesLineEditEmissions() = delete;

    static void install()
    {
        static QSignalSpyCallbackSet callbacks = { &PagesLineEditEmissions::onBegin, nullptr, &PagesLineEditEmissions::onEnd, nullptr };
        qt_register_signal_spy_callbacks(&callbacks);
    }

    static void uninstall() { qt_register_signal_spy_callbacks(nullptr); }

    /// A signal of a line edit is being emitted, which the deletion of the object
    /// deletes (the line edit itself or one of its ancestors)
    static bool isInside(const QObject* object)
    {
        for (const QPointer<QLineEdit>& lineEdit : getEmitters())
        {
            for (const QObject* current = lineEdit.data(); current; current = current->parent())
            {
                if (current == object)
                {
                    return true;
                }
            }
        }
        return false;
    }

private:
    // The end of an emission gets the sender, which may be deleted: only the stack
    // of the emissions is popped (begin and end come in pairs in one thread)
    static void onBegin(QObject* caller, int signalIndex, void** arguments)
    {
        Q_UNUSED(signalIndex);
        Q_UNUSED(arguments);
        if (QThread::isMainThread())
        {
            getEmitters().push_back(QPointer<QLineEdit>(qobject_cast<QLineEdit*>(caller)));
        }
    }

    static void onEnd(QObject* caller, int signalIndex)
    {
        Q_UNUSED(caller);
        Q_UNUSED(signalIndex);
        if (QThread::isMainThread() && !getEmitters().empty())
        {
            getEmitters().pop_back();
        }
    }

    // Never destroyed: QtTest ends the process by exit() for -functions and
    // -datatags, the objects of the libraries emit signals after the static data
    // of the test are gone, while the callbacks are installed
    static std::vector<QPointer<QLineEdit>>& getEmitters()
    {
        static std::vector<QPointer<QLineEdit>>* emitters = new std::vector<QPointer<QLineEdit>>();
        return *emitters;
    }
};

/// Application of the test: it knows the events being delivered (QApplication::notify),
/// so the recorder can tell an emission inside of the delivery of a state event
class PagesLifetimeApplication : public QApplication
{
public:
    PagesLifetimeApplication(int& argc, char** argv) :
        QApplication(argc, argv)
    {
    }

    virtual bool notify(QObject* receiver, QEvent* event) override
    {
        if (isWidgetStateEvent(event->type()) && receiver && receiver->isWidgetType())
        {
            s_stateEventReceivers.push_back(QPointer<QObject>(receiver));
            const bool result = QApplication::notify(receiver, event);
            s_stateEventReceivers.pop_back();
            return result;
        }
        return QApplication::notify(receiver, event);
    }

    /// Events, whose delivery Qt does not survive, when the receiving widget is
    /// deleted inside of it: QWidgetPrivate::show_helper / hide_helper use the widget
    /// after its QShowEvent / QHideEvent, QWidget::event sends LanguageChange to the
    /// children by a copied list of raw pointers after changeEvent,
    /// QWidgetPrivate::setGeometry_sys and the layout of the parent (QLayoutPrivate::
    /// doResize) use the widget after its QResizeEvent / QMoveEvent
    static bool isWidgetStateEvent(QEvent::Type type)
    {
        return type == QEvent::Show || type == QEvent::Hide || type == QEvent::LanguageChange || type == QEvent::Resize || type == QEvent::Move;
    }

    /// An event of isWidgetStateEvent is being delivered to a widget, which the
    /// deletion of the object deletes (the widget itself or one of its ancestors)
    static bool isWidgetStateEventInside(const QObject* object)
    {
        for (const QPointer<QObject>& receiver : s_stateEventReceivers)
        {
            for (const QObject* current = receiver.data(); current; current = current->parent())
            {
                if (current == object)
                {
                    return true;
                }
            }
        }
        return false;
    }

private:
    static std::vector<QPointer<QObject>> s_stateEventReceivers;
};

std::vector<QPointer<QObject>> PagesLifetimeApplication::s_stateEventReceivers;

/// Server of the update checks of the test: it accepts the connections on the
/// loopback and never answers (a check of the run stays pending)
class PagesSilentServer : public QTcpServer
{
public:
    explicit PagesSilentServer(QObject* parent = nullptr) :
        QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, &PagesSilentServer::onNewConnection);
    }

    QUrl getUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/latest").arg(serverPort())); }

private:
    void onNewConnection()
    {
        while (QTcpSocket* socket = nextPendingConnection())
        {
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }
};

class PagesLifetimeRecorder;

/// One connection of the recorder to one signal of one object: the slot is the
/// first method after the methods of QObject (the pattern of QSignalSpy)
class PagesLifetimeHook : public QObject
{
public:
    explicit PagesLifetimeHook(PagesLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature);

    virtual int qt_metacall(QMetaObject::Call call, int id, void** arguments) override;

private:
    PagesLifetimeRecorder* m_recorder;
    QPointer<QObject> m_sender;
    QByteArray m_signature;
};

/// Recorder of the signals of the objects of the application. In the discovery
/// mode it collects the sites (signal of an object, step of the scenario), in the
/// replay mode it deletes the victim at the first emission of the target site.
class PagesLifetimeRecorder : public QObject
{
public:
    enum class Mode
    {
        Discover,
        Replay
    };

    struct Site
    {
        QString key;
        int step = 0;

        bool operator==(const Site& other) const { return key == other.key && step == other.step; }
    };

    enum class Action
    {
        None,       ///< No victim (it is already gone or it does not exist)
        Deleted,    ///< Deleted synchronously
        Deferred    ///< Deleted by deleteLater (a deferred site or state)
    };

    explicit PagesLifetimeRecorder(Mode mode) :
        m_mode(mode)
    {
        qApp->installEventFilter(this);
    }

    virtual ~PagesLifetimeRecorder() override
    {
        qApp->removeEventFilter(this);
    }

    void setTarget(const Site& site, std::function<Action(const Site&)> action)
    {
        m_target = site;
        m_action = std::move(action);
    }

    /// Sites, where the victim is deleted by deleteLater instead of synchronously,
    /// each with the reason why no code can survive it:
    ///  - a signal of a destructor (PDFAppPage::aboutToBeDestroyed, ...): the dying
    ///    object is still a child of its parents, a deletion of an ancestor deletes
    ///    it twice (Qt)
    static bool isDeferredSite(const Site& site)
    {
        return site.key.endsWith(QStringLiteral("::aboutToBeDestroyed()"));
    }

    /// The state of the emission defers the deletion of the victim:
    ///  - an emission inside of a signal of a QLineEdit, which the deletion of the
    ///    victim deletes (the filter of a page, the global search): Qt does not
    ///    survive the deletion of the line edit inside of its own textEdited /
    ///    textChanged (QWidgetLineControl reads the deleted line edit after the
    ///    emission) - PagesLineEditEmissions;
    ///  - an emission inside of the delivery of a QShowEvent / QHideEvent /
    ///    LanguageChange to a widget, which the deletion of the victim deletes (the
    ///    What's New page starts its update check, when it is shown; the pages and the
    ///    document view emit their titles and pages, when the language changes, and
    ///    its view, when the window is resized - also a QResizeEvent / QMoveEvent): Qt
    ///    uses the widget after the event (PagesLifetimeApplication::isWidgetStateEvent)
    static bool isDeferredState(const Site& site, const QObject* victim)
    {
        Q_UNUSED(site);
        return PagesLineEditEmissions::isInside(victim) || PagesLifetimeApplication::isWidgetStateEventInside(victim);
    }

    void setStep(int step) { m_step = step; }
    bool isFired() const { return m_isFired; }
    bool isDeferredFired() const { return m_isFired && m_isDeferredFired; }
    const std::vector<Site>& getSites() const { return m_sites; }
    void stop() { m_isStopped = true; }

    void hookTree(QObject* root)
    {
        if (!root)
        {
            return;
        }
        hookObject(root);
        for (QObject* child : root->findChildren<QObject*>())
        {
            hookObject(child);
        }
    }

    void hookApplication()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            hookTree(widget);
        }
    }

    void hookObject(QObject* object)
    {
        if (!object || m_isStopped)
        {
            return;
        }

        auto it = m_hooked.find(object);
        if (it != m_hooked.end() && it.value())
        {
            return;
        }

        const QMetaObject* metaObject = object->metaObject();
        if (!isApplicationClass(metaObject))
        {
            return;
        }
        m_hooked.insert(object, QPointer<QObject>(object));

        // Signals declared by the classes of the application only (the signals of
        // Qt and of the library are not emitted by the application itself)
        const QMetaObject* base = metaObject;
        while (base->superClass() && isApplicationClass(base->superClass()))
        {
            base = base->superClass();
        }

        for (int i = base->methodOffset(); i < metaObject->methodCount(); ++i)
        {
            const QMetaMethod method = metaObject->method(i);
            if (method.methodType() != QMetaMethod::Signal)
            {
                continue;
            }

            PagesLifetimeHook* hook = new PagesLifetimeHook(this, object, method.methodSignature());
            if (!QMetaObject::connect(object, i, hook, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr))
            {
                delete hook;
            }
        }
    }

    void onSignal(QObject* sender, const QByteArray& signature)
    {
        if (m_isStopped || !sender || !QThread::isMainThread())
        {
            return;
        }

        // A signal emitted while a widget paints is no site (Qt does not survive a
        // deletion of the painted widget inside of its paint event)
        if (m_paintedWidget && m_paintedWidget->paintingActive())
        {
            return;
        }

        const Site site{ getPath(sender) + QStringLiteral("::") + QString::fromLatin1(signature), m_step };
        if (m_mode == Mode::Discover)
        {
            const QString seenKey = site.key + QLatin1Char('#') + QString::number(site.step);
            if (!m_seen.contains(seenKey))
            {
                m_seen.insert(seenKey);
                m_sites.push_back(site);
            }
        }
        else if (!m_isFired && !m_isInAction && site == m_target && m_action)
        {
            m_isInAction = true;
            const Action action = m_action(m_target);
            m_isFired = action != Action::None;
            m_isDeferredFired = action == Action::Deferred;
            m_isInAction = false;
        }
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        // An object receiving an event is constructed: hook it (menus, dialogs,
        // panels and rows created in the middle of a step)
        if (event->type() == QEvent::Paint && watched->isWidgetType())
        {
            m_paintedWidget = static_cast<QWidget*>(watched);
        }
        if (!m_isStopped && watched && QThread::isMainThread() && !m_hooked.contains(watched))
        {
            if (isConstructionEvent(event->type()))
            {
                // The object may be in its constructor (Qt sends these events to a
                // widget, which creates its children, sets its properties and
                // fonts): it is hooked from the event loop. A signal emitted by a
                // constructor reaches no receiver of the application (nothing can be
                // connected to an object before it exists), so it is no site - the
                // recorder would delete the parent of a half-built object.
                QPointer<QObject> guard(watched);
                QMetaObject::invokeMethod(this, [this, guard]() { if (guard && !m_hooked.contains(guard.data())) { hookObject(guard.data()); } }, Qt::QueuedConnection);
            }
            else
            {
                hookObject(watched);
            }
        }
        return false;
    }

    /// Events, which Qt sends to an object from inside of its constructor
    static bool isConstructionEvent(QEvent::Type type)
    {
        switch (type)
        {
            case QEvent::ChildAdded:
            case QEvent::ChildPolished:
            case QEvent::ChildRemoved:
            case QEvent::DynamicPropertyChange:
            case QEvent::ParentChange:
            case QEvent::ParentAboutToChange:
            case QEvent::FontChange:
            case QEvent::PaletteChange:
            case QEvent::StyleChange:
            case QEvent::LayoutDirectionChange:
            case QEvent::LocaleChange:
            case QEvent::EnabledChange:
            case QEvent::ToolTipChange:
            case QEvent::WindowTitleChange:
            case QEvent::WindowIconChange:
            case QEvent::ModifiedChange:
            case QEvent::MouseTrackingChange:
            case QEvent::ContentsRectChange:
            case QEvent::Polish:
            case QEvent::PolishRequest:
            case QEvent::LayoutRequest:
                return true;
            default:
                return false;
        }
    }

    static bool isApplicationClass(const QMetaObject* metaObject)
    {
        if (!metaObject)
        {
            return false;
        }
        const QByteArray className(metaObject->className());
        return className.startsWith("pdf::PDFApp") || className.startsWith("pdf::PDFDatabase") || className == "pdf::PDFPageNotFoundPage" ||
               className == "pdf::PDFUpdateChecker";
    }

    /// Path of the object by the class names of its parents
    static QString getPath(const QObject* object)
    {
        QStringList parts;
        for (const QObject* current = object; current; current = current->parent())
        {
            parts.prepend(QString::fromLatin1(current->metaObject()->className()));
        }
        return parts.join(QLatin1Char('/'));
    }

private:
    Mode m_mode;
    int m_step = 0;
    bool m_isFired = false;
    bool m_isDeferredFired = false;
    bool m_isInAction = false;
    bool m_isStopped = false;
    Site m_target;
    std::function<Action(const Site&)> m_action;
    std::vector<Site> m_sites;
    QSet<QString> m_seen;
    QHash<QObject*, QPointer<QObject>> m_hooked;
    QPointer<QWidget> m_paintedWidget;
};

PagesLifetimeHook::PagesLifetimeHook(PagesLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature) :
    QObject(recorder),
    m_recorder(recorder),
    m_sender(sender),
    m_signature(signature)
{
}

int PagesLifetimeHook::qt_metacall(QMetaObject::Call call, int id, void** arguments)
{
    id = QObject::qt_metacall(call, id, arguments);
    if (id < 0 || call != QMetaObject::InvokeMetaMethod)
    {
        return id;
    }
    if (id == 0)
    {
        m_recorder->onSignal(m_sender.data(), m_signature);
    }
    return id - 1;
}

/// Pseudo translation of every text of PDF4QT: "[text]"
class PagesLifetimeTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray contextText(context ? context : "");
        if (!contextText.startsWith("pdf") || !sourceText || !*sourceText)
        {
            return QString();
        }
        return QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText));
    }

    virtual bool isEmpty() const override { return false; }
};

/// One route driven by the test, made from a pattern of the route table
struct PagesRouteCase
{
    QString pattern;            ///< Pattern of the entry of the route table
    QString route;              ///< Route; the parameter documentId is "{documentId}"
    QString name;               ///< Name of the data rows
    bool isNewTab = false;      ///< Opened by the navigation mode NewTab (the tab New tab)
    bool isSweep = false;       ///< The case of its pattern driven by deleteFromEverySignal
};

/// Patterns, whose deletion from every signal is driven by another lifetime test
/// (the open / close / delete orders of every route run here too)
struct PagesSweepExclusion
{
    const char* pattern;
    const char* reason;
};

/// Cases of the routes made from the route table
class PagesRouteCatalog
{
public:
    PagesRouteCatalog() = delete;

    static constexpr const char* DOCUMENT_ID = "{documentId}";

    static const std::vector<PagesSweepExclusion>& getSweepExclusions()
    {
        static const std::vector<PagesSweepExclusion> exclusions = {
            { "/settings", "UnitTestsAppSettingsLifetime deleteFromEverySignal: the 9 scenarios of the sections of Settings, the filter, the dialogs and popups" },
            { "/settings/{section}", "UnitTestsAppSettingsLifetime deleteFromEverySignal / openDialogsSurviveChanges: every section, its dialogs and popups, LanguageChange" },
            { "/tools", "UnitTestsAppToolsLifetime deleteFromEverySignal (scenario catalog) / openPopupsSurviveChanges" },
            { "/tools/collections", "UnitTestsAppToolsLifetime deleteFromEverySignal (scenario collections), a redirect to /tools/collections/{collectionId}" },
            { "/tools/collections/new", "UnitTestsAppToolsLifetime deleteFromEverySignal (scenario collections: New collection, Save)" },
            { "/tools/collections/{collectionId}", "UnitTestsAppToolsLifetime deleteFromEverySignal (scenario collections) / openPopupsSurviveChanges" },
            { "/ai", "UnitTestsAppMcpLifetime deleteFromEverySignal (scenario ai: the AI page, its dialogs, assistants and activity)" }
        };
        return exclusions;
    }

    static const PagesSweepExclusion* findSweepExclusion(const QString& pattern)
    {
        for (const PagesSweepExclusion& exclusion : getSweepExclusions())
        {
            if (pattern == QLatin1String(exclusion.pattern))
            {
                return &exclusion;
            }
        }
        return nullptr;
    }

    /// Patterns, whose cases show Page Not Found by design (any other case showing
    /// it drives no page - a value of the catalog is not valid for the page); the
    /// query variant only, when \p isQueryOnly
    static const std::vector<std::tuple<const char*, bool, const char*>>& getNotFoundCases()
    {
        static const std::vector<std::tuple<const char*, bool, const char*>> cases = {
            { "/tools/{toolId}", false, "a tool has a page only in phase 2 (PDFAppToolPageRegistry)" },
            { "/document/{documentId}/tool/{toolId}", false, "a tool has a page only in phase 2 (PDFAppToolPageRegistry)" },
            { "/tool/{toolId}/{instanceId}", false, "a tool has a page only in phase 2 (PDFAppToolPageRegistry)" },
            { "/tools", true, "a collection together with a category or a text is refused (owner decision Q1); the filters are driven by UnitTestsAppToolsLifetime" }
        };
        return cases;
    }

    static bool isNotFoundCase(const PagesRouteCase& routeCase)
    {
        for (const auto& [pattern, isQueryOnly, reason] : getNotFoundCases())
        {
            Q_UNUSED(reason);
            if (routeCase.pattern == QLatin1String(pattern) && (!isQueryOnly || routeCase.route.contains(QLatin1Char('?'))))
            {
                return true;
            }
        }
        return false;
    }

    /// Ids of the panels of the sidebar of a document
    static QStringList getPanelIds()
    {
        QStringList ids;
        const pdf::PDFAppDocumentPanelRegistry registry = pdf::PDFAppDocumentPanelRegistry::createDefault();
        for (const pdf::PDFAppDocumentPanelFactory* factory : registry.getFactories())
        {
            ids << factory->getId();
        }
        return ids;
    }

    /// Values of a parameter of a segment of a route (\p isPath) or of a query key:
    /// every option of a segment (each one is another page - a section), the first
    /// option of a query key, a valid value of the catalogs for the known names, a
    /// value of the kind otherwise (Page Not Found is a page too)
    static QStringList getValues(const QString& name, const pdf::PDFAppRouteValues& values, bool isPath)
    {
        using Kind = pdf::PDFAppRouteValues::Kind;
        switch (values.getKind())
        {
            case Kind::Options:
                return isPath ? values.getOptions() : QStringList(values.getOptions().value(0));
            case Kind::DocumentId:
                return { QString::fromLatin1(DOCUMENT_ID) };
            case Kind::Number:
                return { QStringLiteral("1") };
            case Kind::Version:
            {
                const std::shared_ptr<const pdf::PDFWhatsNewCatalog> catalog = pdf::PDFWhatsNewCatalog::getBuiltin();
                if (catalog && !catalog->getReleases().empty())
                {
                    return { catalog->getReleases().front().getDisplayVersion() };
                }
                return { QStringLiteral("1.6") };
            }
            case Kind::Slug:
            {
                // A segment {toolId} and a query key tool are both the id of a tool
                const QString object = name.endsWith(QLatin1String("Id")) ? name.chopped(2) : name;
                if (object == QLatin1String("tool") && !pdf::PDFDocumentToolCatalog::getTools().empty())
                {
                    return { pdf::PDFDocumentToolCatalog::getTools().front().getId() };
                }
                if (object == QLatin1String("collection") && !pdf::PDFAppBuiltinToolCollections::getCollections().empty())
                {
                    return { pdf::PDFAppBuiltinToolCollections::getCollections().front().getId() };
                }
                if (name == QLatin1String("topic"))
                {
                    const std::shared_ptr<const pdf::PDFAppHelpCatalog> catalog = pdf::PDFAppHelpCatalog::getBuiltin();
                    if (catalog && !catalog->getTopics().empty())
                    {
                        return { catalog->getTopics().front().id };
                    }
                }
                if (name == QLatin1String("panel"))
                {
                    return getPanelIds();
                }
                return { QStringLiteral("sample") };
            }
            case Kind::Text:
                return { QStringLiteral("alpha") };
        }
        return { QStringLiteral("sample") };
    }

    static QString createName(const QString& route)
    {
        QString name;
        for (const QChar character : route)
        {
            name += character.isLetterOrNumber() || character == QLatin1Char('.') ? character : QLatin1Char('-');
        }
        name.replace(QStringLiteral("-documentId-"), QStringLiteral("-doc-"));
        while (name.contains(QStringLiteral("--")))
        {
            name.replace(QStringLiteral("--"), QStringLiteral("-"));
        }
        while (name.startsWith(QLatin1Char('-')))
        {
            name.remove(0, 1);
        }
        while (name.endsWith(QLatin1Char('-')))
        {
            name.chop(1);
        }
        return name.isEmpty() ? QStringLiteral("root") : name;
    }

    /// Cases of all patterns of the table: the segments filled by every value of
    /// getValues; a pattern with query keys has one more case per value of its query
    /// (all keys set); /open, a route of the tab New tab, has its New tab case
    static std::vector<PagesRouteCase> create(const pdf::PDFAppRouteTable& table)
    {
        std::vector<PagesRouteCase> cases;
        for (const pdf::PDFAppRouteEntry& entry : table.getEntries())
        {
            // The segments: the product of the values of the parameters
            std::vector<QStringList> segmentPaths = { QStringList() };
            for (const QString& segment : entry.segments)
            {
                QStringList values;
                if (segment.size() > 2 && segment.startsWith(QLatin1Char('{')) && segment.endsWith(QLatin1Char('}')))
                {
                    const QString parameter = segment.mid(1, segment.size() - 2);
                    auto it = entry.parameters.find(parameter);
                    values = it != entry.parameters.end() ? getValues(parameter, it->second, true) : QStringList();
                }
                else
                {
                    values << segment;
                }

                std::vector<QStringList> extended;
                for (const QStringList& path : segmentPaths)
                {
                    for (const QString& value : std::as_const(values))
                    {
                        extended.push_back(path + QStringList(value));
                    }
                }
                segmentPaths = std::move(extended);
            }

            // The query: one variant per value of a key with more values (the panels
            // of a document), the other keys with their first value
            std::vector<QStringList> queries;
            if (!entry.query.empty())
            {
                size_t variantCount = 1;
                std::vector<std::pair<QString, QStringList>> keyValues;
                for (const auto& [key, values] : entry.query)
                {
                    keyValues.emplace_back(key, getValues(key, values, false));
                    variantCount = std::max<size_t>(variantCount, size_t(keyValues.back().second.size()));
                }
                for (size_t variant = 0; variant < variantCount; ++variant)
                {
                    QStringList items;
                    for (const auto& [key, values] : keyValues)
                    {
                        if (!values.isEmpty())
                        {
                            items << key + QLatin1Char('=') + values.value(qMin(qsizetype(variant), values.size() - 1));
                        }
                    }
                    queries.push_back(items);
                }
            }

            bool isFirstOfPattern = true;
            for (const QStringList& path : segmentPaths)
            {
                const QString base = QLatin1Char('/') + path.join(QLatin1Char('/'));
                std::vector<QString> routes = { base };
                for (const QStringList& query : queries)
                {
                    routes.push_back(base + QLatin1Char('?') + query.join(QLatin1Char('&')));
                }

                for (const QString& route : routes)
                {
                    PagesRouteCase routeCase;
                    routeCase.pattern = entry.pattern;
                    routeCase.route = route;
                    routeCase.name = createName(route);
                    // The case of the pattern with the most state (its query) is swept
                    routeCase.isSweep = isFirstOfPattern && route == routes.back() && !findSweepExclusion(entry.pattern);
                    cases.push_back(routeCase);

                    if (entry.isNewTabRoute && route == base)
                    {
                        PagesRouteCase newTabCase = routeCase;
                        newTabCase.isNewTab = true;
                        newTabCase.isSweep = !findSweepExclusion(entry.pattern);
                        newTabCase.name += QStringLiteral("-newtab");
                        cases.push_back(newTabCase);
                    }
                }
                isFirstOfPattern = false;
            }
        }
        return cases;
    }
};

/// Victim of a deletion
enum class PagesVictim
{
    Tab,        ///< The tab of the route
    Window,
    Page        ///< The shown page of the tab of the route
};

/// State of one run of a route
struct PagesRun
{
    pdftest::AppToolsFixture* fixture = nullptr;
    PagesSilentServer* server = nullptr;
    PagesRouteCase routeCase;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppTab> tab;               ///< Tab of the route
    QPointer<pdf::PDFAppDocumentTab> documentTab;
    QString documentId;
    QStringList recentFiles;
};

using PagesStep = std::function<bool(PagesRun&)>;

/// Helpers of the runs: every input is sent through guards, the victim may be
/// deleted inside of any event
class PagesLifetimeHelper
{
public:
    PagesLifetimeHelper() = delete;

    static void settle() { pdftest::AppDocumentFixture::settle(); }

    static int timeout(int milliseconds) { return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor(); }

    /// Waits, processing the events (the victim may go meanwhile)
    static void wait(int milliseconds)
    {
        QTest::qWait(milliseconds);
        settle();
    }

    static pdf::PDFAppPage* page(const PagesRun& run)
    {
        return run.tab ? run.tab->getPage() : nullptr;
    }

    /// Tab of the route; before the route is shown (its first step) the current tab
    static pdf::PDFAppTab* victimTab(const PagesRun& run)
    {
        if (run.tab)
        {
            return run.tab.data();
        }
        // A window in its destructor (a signal of the destruction) is not asked
        pdf::PDFAppMainWindow* window = qobject_cast<pdf::PDFAppMainWindow*>(run.window.data());
        return window ? window->getCurrentTab() : nullptr;
    }

    static bool isDocumentRoute(const PagesRouteCase& routeCase)
    {
        return routeCase.route.contains(QLatin1String(PagesRouteCatalog::DOCUMENT_ID));
    }

    /// Route of the case with the id of the open document
    static QString resolveRoute(const PagesRun& run)
    {
        QString route = run.routeCase.route;
        route.replace(QLatin1String(PagesRouteCatalog::DOCUMENT_ID), run.documentId);
        return route;
    }

    /// Sample of the document of the route: a sample, which the panel of the route
    /// shows, by the id of the panel (an unknown panel gets the simple sample)
    static QString createDocument(PagesRun& run)
    {
        const QString folder = run.fixture->folder();
        const QString& route = run.routeCase.route;
        using Samples = pdftest::AppDocumentSamples;
        if (route.contains(QStringLiteral("panel=outline")))
        {
            return Samples::outline(folder);
        }
        if (route.contains(QStringLiteral("panel=attachments")))
        {
            return Samples::attachments(folder);
        }
        if (route.contains(QStringLiteral("panel=layers")))
        {
            return Samples::layers(folder);
        }
        if (route.contains(QStringLiteral("panel=notes")))
        {
            return Samples::annotations(folder);
        }
        return Samples::simple(folder, QStringLiteral("pages-lifetime.pdf"), 3);
    }

    /// Prepares the run: no native file dialog, update checks to the silent server,
    /// Recent with files, a folder, a pinned location, a workspace, used and pinned
    /// tools (the pages of the Home tab request thumbnails and folder information),
    /// the document of a document route
    static bool prepare(PagesRun& run)
    {
        pdftest::AppToolsFixture& fixture = *run.fixture;
        pdf::PDFAppFileDialogs::setOpenFilesHandler([](QWidget*, const QString&) { return QStringList(); });
        pdf::PDFAppFileDialogs::setFolderHandler([](QWidget*, const QString&) { return QString(); });
        pdf::PDFAppFileDialogs::setSaveFileHandler([](QWidget*, const QString&) { return QString(); });
        pdf::PDFAppFileDialogs::setSaveBackupHandler([](QWidget*, const QString&) { return QString(); });
        pdf::PDFAppFileDialogs::setOpenBackupHandler([](QWidget*, const QString&) { return QString(); });
        if (pdf::PDFUpdateChecker* checker = pdf::PDFUpdateChecker::getInstance(fixture.context.get()))
        {
            checker->setUrl(run.server->getUrl());
        }

        using Samples = pdftest::AppDocumentSamples;
        run.recentFiles = { Samples::simple(fixture.folder(), QStringLiteral("recent-a.pdf"), 2), Samples::outline(fixture.folder()) };
        pdf::PDFRecentRepository recent = fixture.context->getRecent();
        for (const QString& file : std::as_const(run.recentFiles))
        {
            if (recent.recordOpened(file).isError())
            {
                return false;
            }
        }
        pdf::PDFWorkspaceDocument workspaceDocument;
        workspaceDocument.path = run.recentFiles.front();
        pdf::PDFToolRepository tools = fixture.tools();
        if (recent.setPinned(run.recentFiles.front(), true).isError() || recent.setReadingState(run.recentFiles.back(), 2, 5, false).isError() ||
            recent.addPinnedLocation(fixture.folder(), QStringLiteral("Samples")).isError() ||
            fixture.context->getWorkspaces().saveWorkspace(QStringLiteral("Reading"), { workspaceDocument }, true).isError() ||
            tools.recordUsage(QStringLiteral("pages.merge")).isError() || tools.setPinned(QStringLiteral("optimize.compress"), true).isError())
        {
            return false;
        }
        settle();

        if (isDocumentRoute(run.routeCase))
        {
            QPointer<pdf::PDFAppDocumentTab> documentTab = fixture.open(createDocument(run));
            if (!documentTab || !QTest::qWaitFor([&documentTab]() { return !documentTab || (documentTab->getDocument() && documentTab->getDocument()->isReady()); }, timeout(20000)) || !documentTab)
            {
                return false;
            }
            run.documentTab = documentTab.data();
            run.documentId = documentTab->getDocumentId();
            waitForDocumentWork(run);
        }
        return true;
    }

    /// Opens the route of the case (in the Home tab, a New tab, the Help tab, the
    /// tab of the document) and remembers the tab, which shows it
    static bool open(PagesRun& run)
    {
        if (!run.window || !run.window->getNavigator())
        {
            return false;
        }
        const pdf::PDFAppRoute route = pdf::PDFAppRoute::parse(resolveRoute(run));
        run.window->getNavigator()->navigate(route, run.routeCase.isNewTab ? pdf::PDFAppNavigationMode::NewTab : pdf::PDFAppNavigationMode::Push);
        settle();
        if (!run.window)
        {
            return false;
        }
        run.tab = run.window->getCurrentTab();

        // A case drives its page: Page Not Found only, where it is the page of the
        // route by design (a value of the catalog refused by the page drives nothing)
        pdf::PDFAppPage* shownPage = page(run);
        if (shownPage && QByteArray(shownPage->metaObject()->className()) == "pdf::PDFPageNotFoundPage" && !PagesRouteCatalog::isNotFoundCase(run.routeCase))
        {
            qWarning().noquote() << QStringLiteral("%1 shows Page Not Found").arg(resolveRoute(run));
            return false;
        }
        return shownPage != nullptr;
    }

    /// Root of the content of the route: the shown page, or the view of the
    /// document, when the page is the route adapter of the document tab (the view
    /// with its toolbar, sidebar and canvas is a child of the tab, not of the page)
    static QWidget* contentRoot(const PagesRun& run)
    {
        pdf::PDFAppPage* shownPage = page(run);
        if (shownPage && run.documentTab && run.tab == run.documentTab && qobject_cast<pdf::PDFAppDocumentPage*>(shownPage))
        {
            return run.documentTab->getView();
        }
        return shownPage;
    }

    /// Visible, enabled widgets of the type below the root
    template<typename T>
    static QList<T*> visibleWidgets(QWidget* root)
    {
        QList<T*> result;
        if (!root)
        {
            return result;
        }
        for (T* widget : root->findChildren<T*>())
        {
            if (widget->isVisible() && widget->isEnabled())
            {
                result << widget;
            }
        }
        return result;
    }

    /// The first child focused by the keyboard, another child hovered
    static bool focusAndHover(PagesRun& run)
    {
        QWidget* root = contentRoot(run);
        if (!root)
        {
            return false;
        }

        QPointer<QWidget> focused;
        QPointer<QWidget> hovered;
        for (QWidget* widget : visibleWidgets<QWidget>(root))
        {
            if (!focused && (widget->focusPolicy() & Qt::TabFocus))
            {
                focused = widget;
            }
            else if (!hovered && qobject_cast<QAbstractButton*>(widget))
            {
                hovered = widget;
            }
        }
        if (focused)
        {
            focused->setFocus(Qt::TabFocusReason);
            settle();
        }
        if (hovered && run.window)
        {
            QTest::mouseMove(run.window.data(), hovered->mapTo(run.window.data(), hovered->rect().center()));
            settle();
        }
        waitForDocumentWork(run);
        return true;
    }

    /// The operations of the document of the route (the text index of the search,
    /// started by a focus or a filter) end in the step, which started them: a site of
    /// a worker moved to the next step by a slow run is not reproduced
    static void waitForDocumentWork(const PagesRun& run)
    {
        const QPointer<pdf::PDFAppDocumentTab> documentTab = run.documentTab;
        if (documentTab)
        {
            const bool isIdle = QTest::qWaitFor([&documentTab]() { return !documentTab || !documentTab->getDocument() || documentTab->getDocument()->getOperations().empty(); }, timeout(20000));
            Q_UNUSED(isIdle);
            settle();
        }
    }

    /// Types the text into the line edit (each character guarded)
    static bool type(QWidget* widget, const QString& text)
    {
        QPointer<QWidget> guard(widget);
        if (!widget)
        {
            return false;
        }
        widget->setFocus(Qt::OtherFocusReason);
        for (const QChar character : text)
        {
            if (!guard)
            {
                return false;
            }
            QTest::keyClick(guard.data(), character.toLatin1());
        }
        return bool(guard);
    }

    static bool key(QWidget* widget, int keyCode)
    {
        if (!widget)
        {
            return false;
        }
        QPointer<QWidget> guard(widget);
        QTest::keyPress(widget, Qt::Key(keyCode));
        if (guard)
        {
            QTest::keyRelease(guard.data(), Qt::Key(keyCode));
        }
        settle();
        return true;
    }

    static bool click(QWidget* widget)
    {
        if (!widget || !widget->isEnabled() || !widget->isVisible())
        {
            return false;
        }
        QPointer<QWidget> guard(widget);
        const QPoint point = widget->rect().center();
        QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, point);
        if (guard)
        {
            QTest::mouseRelease(guard.data(), Qt::LeftButton, Qt::NoModifier, point);
        }
        settle();
        return true;
    }

    /// The filter of the page: the first editable line edit of the page, which is
    /// not the global search of the header
    static bool typeFilter(PagesRun& run)
    {
        QWidget* root = contentRoot(run);
        if (!root)
        {
            return false;
        }
        for (QLineEdit* field : visibleWidgets<QLineEdit>(root))
        {
            if (!field->isReadOnly() && (!run.tab || field != run.tab->getSearchField()))
            {
                // The work of the filter (a search of the document in a worker)
                // ends in this step also in a slow run (a site moved to the next
                // step is not reproduced)
                if (type(field, QStringLiteral("al")))
                {
                    wait(timeout(400));
                    waitForDocumentWork(run);
                }
                break;
            }
        }
        return true;
    }

    /// The field of the global search of the tab (the popup of the header)
    static bool typeGlobalSearch(PagesRun& run, bool isWaiting)
    {
        QPointer<pdf::PDFLineEdit> field = run.tab ? run.tab->getSearchField() : nullptr;
        if (field && field->isVisible() && type(field.data(), QStringLiteral("me")) && isWaiting)
        {
            wait(timeout(400));
            if (field)
            {
                key(field.data(), Qt::Key_Escape);
            }
        }
        return true;
    }

    /// The route shown again after an action, which may have left it: the tab of
    /// the route current, its page shown again by Back
    static void returnToRoute(PagesRun& run, const QPointer<pdf::PDFAppPage>& shownPage)
    {
        if (run.window && run.tab && run.window->getCurrentTab() != run.tab)
        {
            run.window->activateTab(run.tab.data());
            settle();
        }
        if (!shownPage && run.window && run.tab)
        {
            run.window->getNavigator()->goBack(run.tab.data());
            settle();
        }
    }

    /// The open popup of the global search with its results (nullptr = none)
    static pdf::PDFAppSearchPopup* findSearchPopup(const PagesRun& run)
    {
        for (pdf::PDFAppSearchPopup* popup : run.window ? run.window->findChildren<pdf::PDFAppSearchPopup*>() : QList<pdf::PDFAppSearchPopup*>())
        {
            if (popup->isOpen() && popup->getRowCount() > 0)
            {
                return popup;
            }
        }
        return nullptr;
    }

    /// The global search of the tab used, each part a step of its own (the deletion
    /// from the first emission of a signal in a step reaches each path of the popup):
    ///  - a typed text opens the popup;
    ///  - the pointer hovers its second row;
    ///  - the first row is pressed and released (its result activated);
    ///  - a text typed and Enter at once (the pending search is done first, its
    ///    first result activated).
    /// The route is shown again after an activation.
    static bool openSearchPopup(PagesRun& run)
    {
        QPointer<pdf::PDFLineEdit> field = run.tab ? run.tab->getSearchField() : nullptr;
        if (!field || !field->isVisible())
        {
            return true;
        }

        // The text of the previous step is replaced ("me" finds tools and commands)
        field->clear();
        if (type(field.data(), QStringLiteral("me")))
        {
            const bool isOpen = QTest::qWaitFor([&run]() { return !run.window || findSearchPopup(run); }, timeout(3000));
            Q_UNUSED(isOpen);
        }
        return true;
    }

    static bool hoverSearchPopup(PagesRun& run)
    {
        QPointer<pdf::PDFAppSearchPopup> popup = findSearchPopup(run);
        if (popup && popup->getRowCount() > 1)
        {
            QTest::mouseMove(popup.data(), popup->getRowRect(1).center());
            settle();
        }
        return true;
    }

    static bool clickSearchPopup(PagesRun& run)
    {
        QPointer<pdf::PDFAppPage> shownPage = page(run);
        QPointer<pdf::PDFAppSearchPopup> popup = findSearchPopup(run);
        if (popup)
        {
            const QPoint point = popup->getRowRect(0).center();
            QTest::mousePress(popup.data(), Qt::LeftButton, Qt::NoModifier, point);
            if (popup)
            {
                QTest::mouseRelease(popup.data(), Qt::LeftButton, Qt::NoModifier, point);
            }
            settle();
        }
        returnToRoute(run, shownPage);
        return true;
    }

    static bool enterGlobalSearch(PagesRun& run)
    {
        QPointer<pdf::PDFAppPage> shownPage = page(run);
        QPointer<pdf::PDFLineEdit> field = run.tab ? run.tab->getSearchField() : nullptr;
        if (!field || !field->isVisible())
        {
            return true;
        }
        field->clear();
        if (type(field.data(), QStringLiteral("me")) && field)
        {
            key(field.data(), Qt::Key_Return);
        }
        returnToRoute(run, shownPage);
        return true;
    }

    /// Visible popup or dialog (a menu, a panel of the document toolbar, a message box)
    static QWidget* findOpenPopup(const QWidget* window)
    {
        if (QWidget* popup = QApplication::activePopupWidget())
        {
            return popup;
        }
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            if (topLevel != window && topLevel->isVisible())
            {
                return topLevel;
            }
        }
        if (window)
        {
            for (QDialog* dialog : window->findChildren<QDialog*>())
            {
                if (dialog->isVisible())
                {
                    return dialog;
                }
            }
        }
        return nullptr;
    }

    /// Closes the popups and dialogs by Esc
    static void closePopups(PagesRun& run)
    {
        for (int i = 0; i < 6; ++i)
        {
            QWidget* popup = findOpenPopup(run.window.data());
            if (!popup)
            {
                break;
            }
            key(popup, Qt::Key_Escape);
            if (popup == findOpenPopup(run.window.data()))
            {
                popup->hide();
                settle();
            }
        }
    }

    /// Opens the \p index-th popup of the page: a panel of the toolbar of a document,
    /// the menu of a "more" button, the context menu of a widget with a custom
    /// context menu (an item view by its viewport). \returns false, if the page has
    /// no such popup.
    static bool openPopup(PagesRun& run, int index)
    {
        int current = 0;
        if (run.documentTab && run.tab == run.documentTab && run.documentTab->getView() && run.documentTab->getView()->getToolbar())
        {
            // Every panel of the toolbar, which opens (the values of the enumeration
            // are tried, a new panel is found without a change of the test)
            QPointer<pdf::PDFAppDocumentToolbar> toolbar = run.documentTab->getView()->getToolbar();
            for (int panel = 0; panel < 32 && toolbar; ++panel)
            {
                const bool isOpen = toolbar->openPanel(pdf::PDFAppDocumentToolbar::Panel(panel)) != nullptr;
                settle();
                if (!isOpen)
                {
                    continue;
                }
                if (current++ == index)
                {
                    return true;
                }
                closePopups(run);
            }
            return false;
        }

        pdf::PDFAppPage* shownPage = page(run);
        if (!shownPage)
        {
            return false;
        }
        for (pdf::PDFMoreButton* button : visibleWidgets<pdf::PDFMoreButton>(shownPage))
        {
            if (current++ == index)
            {
                return click(button) && findOpenPopup(run.window.data());
            }
        }
        for (QWidget* widget : visibleWidgets<QWidget>(shownPage))
        {
            if (widget->contextMenuPolicy() != Qt::CustomContextMenu || current++ != index)
            {
                continue;
            }
            QAbstractScrollArea* area = qobject_cast<QAbstractScrollArea*>(widget);
            QWidget* target = area ? area->viewport() : widget;
            const QPoint point(qMin(20, target->width() / 2), qMin(12, target->height() / 2));
            QContextMenuEvent event(QContextMenuEvent::Keyboard, point, target->mapToGlobal(point));
            QApplication::sendEvent(target, &event);
            settle();
            return findOpenPopup(run.window.data());
        }
        return false;
    }

    /// A dialog of the window is visible
    static bool isDialogVisible(const PagesRun& run)
    {
        if (!run.window)
        {
            return false;
        }
        for (QDialog* dialog : run.window->findChildren<QDialog*>())
        {
            if (dialog->isVisible())
            {
                return true;
            }
        }
        return false;
    }

    /// After an attempt, which has not shown a dialog: the popups closed, the tab of
    /// the route current again, its page shown again by Back, when a click left it
    static void restoreAfterAttempt(PagesRun& run, const QPointer<pdf::PDFAppPage>& shownPage)
    {
        closePopups(run);
        returnToRoute(run, shownPage);
    }

    /// Opens a dialog of the page by an item of the menu of a "more" button, whose
    /// text ends by an ellipsis (the convention of a command, which asks before it
    /// acts: Rename..., Delete...); the item is chosen by the keyboard in the open menu.
    /// \returns false, if no such item shows a dialog.
    static bool openMenuDialog(PagesRun& run)
    {
        for (int buttonIndex = 0; buttonIndex < 16; ++buttonIndex)
        {
            for (int actionIndex = 0; actionIndex < 8; ++actionIndex)
            {
                pdf::PDFAppPage* shownPage = page(run);
                if (!shownPage || !run.window)
                {
                    return false;
                }
                const QList<pdf::PDFMoreButton*> buttons = visibleWidgets<pdf::PDFMoreButton>(contentRoot(run));
                if (buttonIndex >= buttons.size())
                {
                    return false;
                }

                QPointer<pdf::PDFAppPage> guard(shownPage);
                QMenu* menu = click(buttons[buttonIndex]) ? qobject_cast<QMenu*>(QApplication::activePopupWidget()) : nullptr;
                QList<QAction*> actions;
                for (QAction* action : menu ? menu->actions() : QList<QAction*>())
                {
                    const QString text = action->text();
                    if (action->isVisible() && action->isEnabled() && !action->isSeparator() && !action->menu() &&
                        (text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1String("..."))))
                    {
                        actions << action;
                    }
                }
                if (actionIndex >= actions.size())
                {
                    restoreAfterAttempt(run, guard);
                    break;
                }

                menu->setActiveAction(actions[actionIndex]);
                key(menu, Qt::Key_Return);
                if (isDialogVisible(run))
                {
                    return true;
                }
                restoreAfterAttempt(run, guard);
            }
        }
        return false;
    }

    /// Opens a dialog of the page: the push buttons and the text links of the page
    /// are clicked in their order, until a dialog is shown (a page left by a click
    /// is shown again by Back), then the items of the menus of the "more" buttons,
    /// which ask before they act (openMenuDialog). \returns false, if nothing shows
    /// a dialog.
    static bool openDialog(PagesRun& run)
    {
        return openButtonDialog(run) || openMenuDialog(run);
    }

    static bool openButtonDialog(PagesRun& run)
    {
        for (int index = 0; index < 16; ++index)
        {
            pdf::PDFAppPage* shownPage = page(run);
            if (!shownPage || !run.window)
            {
                return false;
            }
            QList<QAbstractButton*> buttons;
            for (QAbstractButton* button : visibleWidgets<QAbstractButton>(contentRoot(run)))
            {
                if (qobject_cast<QPushButton*>(button) || qobject_cast<pdf::PDFTextLink*>(button))
                {
                    buttons << button;
                }
            }
            if (index >= buttons.size())
            {
                return false;
            }

            QPointer<pdf::PDFAppPage> guard(shownPage);
            click(buttons[index]);
            if (!run.window)
            {
                return false;
            }
            if (isDialogVisible(run))
            {
                return true;
            }
            restoreAfterAttempt(run, guard);
        }
        return false;
    }

    /// Another connection writes a setting, a recent file and a pin; \p isWaiting =
    /// the notification of this connection is delivered (otherwise it is pending)
    static bool writeOther(PagesRun& run, bool isWaiting)
    {
        pdf::PDFDatabaseNotifier* notifier = run.fixture->context ? run.fixture->context->getNotifier() : nullptr;
        if (!notifier)
        {
            return false;
        }
        QSignalSpy spy(notifier, &pdf::PDFDatabaseNotifier::changed);
        const QString file = run.recentFiles.value(0);
        PagesWriterThread thread(run.fixture->context->getDatabase(), run.fixture->context->getSettingRegistry(), file);
        thread.start();
        if (!thread.wait(timeout(10000)) || !thread.isSuccess())
        {
            return false;
        }
        if (isWaiting)
        {
            const bool isDelivered = QTest::qWaitFor([&spy]() { return !spy.isEmpty(); }, timeout(5000));
            settle();
            return isDelivered;
        }
        return true;
    }

    /// A tick of the minute ticker two minutes later (the clock of the recent times)
    static bool tick(PagesRun& run)
    {
        pdf::PDFAppMinuteTicker* ticker = run.fixture->context ? run.fixture->context->getMinuteTicker() : nullptr;
        if (!ticker)
        {
            return false;
        }
        pdf::PDFAppLocale::setCurrentTimeOverride(QDateTime::currentMSecsSinceEpoch() + 2 * pdf::PDFAppMinuteTicker::MINUTE);
        ticker->tick();
        settle();
        pdf::PDFAppLocale::setCurrentTimeOverride(0);
        return true;
    }

    /// Check for updates now (the request stays pending at the silent server)
    static bool checkForUpdates(PagesRun& run)
    {
        pdf::PDFUpdateChecker* checker = run.fixture->context ? pdf::PDFUpdateChecker::getInstance(run.fixture->context.get()) : nullptr;
        if (checker)
        {
            checker->checkNow();
            settle();
        }
        return true;
    }

    /// The tab of the route closed (the Home tab, which cannot be closed, shows
    /// another page: the old page is deleted by the tab)
    static bool closeTab(PagesRun& run)
    {
        if (!run.window || !run.tab)
        {
            return false;
        }
        if (run.tab->isClosable())
        {
            run.window->closeTab(run.tab.data());
        }
        else
        {
            const bool isAbout = run.routeCase.pattern == QLatin1String("/about");
            run.window->getNavigator()->navigate(pdf::PDFAppRoute::parse(isAbout ? QStringLiteral("/sponsor") : QStringLiteral("/about")), pdf::PDFAppNavigationMode::Push,
                                                 run.tab.data());
        }
        settle();
        return true;
    }

    /// Visible top-level widgets other than \p window (popups and dialogs of deleted
    /// parts, windows left by the run)
    static QStringList describeVisibleTopLevels(const QWidget* window)
    {
        QStringList result;
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            if (topLevel != window && topLevel->isVisible())
            {
                result << QStringLiteral("%1 '%2'").arg(QString::fromLatin1(topLevel->metaObject()->className()), topLevel->objectName());
            }
        }
        return result;
    }

    /// Popups and dialogs, which no surviving tab owns, after a deletion: visible
    /// top-level widgets other than \p window and visible dialogs of the window,
    /// whose parents do not lead to a tab with a page or to a document tab (a popup
    /// of a deleted page or tab, which is a child of the window, of its body, of a
    /// tab without its page or of nothing)
    static QStringList describeStrayPopups(const QWidget* window)
    {
        QList<QWidget*> candidates;
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            if (topLevel != window && topLevel->isVisible())
            {
                candidates << topLevel;
            }
        }
        if (window)
        {
            for (QDialog* dialog : window->findChildren<QDialog*>())
            {
                // A dialog is a top-level widget too: each one once
                if (dialog->isVisible() && !candidates.contains(dialog))
                {
                    candidates << dialog;
                }
            }
        }

        QStringList result;
        for (QWidget* candidate : std::as_const(candidates))
        {
            // A tab, whose page is deleted, owns no popup any more (the document tab
            // keeps its view - the page is its route adapter)
            bool isOwnedByTab = false;
            for (QObject* current = candidate->parent(); current && !isOwnedByTab; current = current->parent())
            {
                const pdf::PDFAppTab* tab = qobject_cast<pdf::PDFAppTab*>(current);
                isOwnedByTab = tab && (tab->getPage() || qobject_cast<const pdf::PDFAppDocumentTab*>(tab));
            }
            if (!isOwnedByTab)
            {
                result << QStringLiteral("%1 '%2' (parent %3)").arg(QString::fromLatin1(candidate->metaObject()->className()), candidate->objectName(),
                                                                   candidate->parent() ? QString::fromLatin1(candidate->parent()->metaObject()->className()) : QStringLiteral("none"));
            }
        }
        return result;
    }

    /// The window works: Home, a new document, the AI page. \returns the failure.
    static QString checkWindowWorks(PagesRun& run)
    {
        if (!run.window)
        {
            return QStringLiteral("the window is gone");
        }
        pdftest::AppToolsFixture& fixture = *run.fixture;
        pdf::PDFAppTab* homeTab = run.window->getHomeTab();
        if (!homeTab)
        {
            return QStringLiteral("no Home tab");
        }
        run.window->activateTab(homeTab);
        settle();
        fixture.navigate(QStringLiteral("/home"));
        if (!fixture.currentPage() || QByteArray(fixture.currentPage()->metaObject()->className()) != "pdf::PDFAppHomePage")
        {
            return QStringLiteral("Home is not shown");
        }

        QPointer<pdf::PDFAppDocumentTab> documentTab = fixture.openDocument(2);
        if (!documentTab || !QTest::qWaitFor([&documentTab]() { return !documentTab || (documentTab->getDocument() && documentTab->getDocument()->isReady()); }, timeout(20000)) ||
            !documentTab || documentTab->getView()->getPageCount() != 2)
        {
            return QStringLiteral("a document does not open");
        }
        settle();

        if (!run.window || !run.window->getHomeTab())
        {
            return QStringLiteral("the window is gone after the document");
        }
        run.window->activateTab(run.window->getHomeTab());
        settle();
        fixture.navigate(QStringLiteral("/ai"));
        if (!fixture.currentPage() || QByteArray(fixture.currentPage()->metaObject()->className()) != "pdf::PDFAppAiPage")
        {
            return QStringLiteral("the AI page is not shown");
        }
        return QString();
    }

    /// The steps of the scenario of deleteFromEverySignal
    static std::vector<PagesStep> createSweepSteps();

private:
    /// Writer of another connection (a thread of its own, as another instance)
    class PagesWriterThread : public QThread
    {
    public:
        PagesWriterThread(pdf::PDFDatabase* database, const pdf::PDFSettingRegistry* registry, const QString& file) :
            m_database(database),
            m_registry(registry),
            m_file(file)
        {
        }

        bool isSuccess() const { return m_isSuccess; }

    protected:
        virtual void run() override
        {
            pdf::PDFSettingsRepository settings(m_database, m_registry);
            pdf::PDFRecentRepository recent(m_database, m_registry);
            pdf::PDFToolRepository tools(m_database);
            m_isSuccess = !settings.setValue(QStringLiteral("viewing.sidebarIconSize"), QStringLiteral("large")).isError() &&
                          (m_file.isEmpty() || !recent.setPinned(m_file, false).isError()) &&
                          !tools.setPinned(QStringLiteral("pages.split"), true).isError();
        }

    private:
        pdf::PDFDatabase* m_database;
        const pdf::PDFSettingRegistry* m_registry;
        QString m_file;
        bool m_isSuccess = false;
    };
};

std::vector<PagesStep> PagesLifetimeHelper::createSweepSteps()
{
    using H = PagesLifetimeHelper;
    std::vector<PagesStep> steps;
    steps.push_back([](PagesRun& run) { return H::open(run); });
    steps.push_back([](PagesRun& run) { return H::focusAndHover(run); });
    steps.push_back([](PagesRun& run) { return H::typeFilter(run); });
    steps.push_back([](PagesRun& run) { return H::typeGlobalSearch(run, true); });
    steps.push_back([](PagesRun& run) { return H::openSearchPopup(run); });
    steps.push_back([](PagesRun& run) { return H::hoverSearchPopup(run); });
    steps.push_back([](PagesRun& run) { return H::clickSearchPopup(run); });
    steps.push_back([](PagesRun& run) { return H::enterGlobalSearch(run); });
    steps.push_back([](PagesRun& run)
    {
        // A popup open while the language and the theme change
        if (H::openPopup(run, 0))
        {
            PagesLifetimeTranslator translator;
            QCoreApplication::installTranslator(&translator);
            H::settle();
            pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
            H::settle();
            pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
            QCoreApplication::removeTranslator(&translator);
            H::settle();
            H::closePopups(run);
        }
        return true;
    });
    steps.push_back([](PagesRun& run)
    {
        if (H::openPopup(run, 1))
        {
            H::closePopups(run);
        }
        return true;
    });
    steps.push_back([](PagesRun& run)
    {
        // A dialog open while the language changes
        if (H::openDialog(run))
        {
            PagesLifetimeTranslator translator;
            QCoreApplication::installTranslator(&translator);
            H::settle();
            QCoreApplication::removeTranslator(&translator);
            H::settle();
            H::closePopups(run);
        }
        return true;
    });
    steps.push_back([](PagesRun& run)
    {
        if (run.window && run.tab && run.window->getNavigator()->goBack(run.tab.data()))
        {
            H::settle();
            if (run.window && run.tab)
            {
                run.window->getNavigator()->goForward(run.tab.data());
                H::settle();
            }
        }
        return true;
    });
    steps.push_back([](PagesRun& run)
    {
        PagesLifetimeTranslator translator;
        QCoreApplication::installTranslator(&translator);
        H::settle();
        QCoreApplication::removeTranslator(&translator);
        H::settle();
        return bool(run.window);
    });
    steps.push_back([](PagesRun&)
    {
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
        H::settle();
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        H::settle();
        return true;
    });
    steps.push_back([](PagesRun& run)
    {
        if (run.window)
        {
            run.window->resize(720, 480);
            H::settle();
        }
        if (run.window)
        {
            run.window->resize(1440, 960);
            H::settle();
        }
        return true;
    });
    steps.push_back([](PagesRun& run) { return H::writeOther(run, true); });
    steps.push_back([](PagesRun& run) { return H::tick(run); });
    steps.push_back([](PagesRun& run) { return H::checkForUpdates(run); });
    steps.push_back([](PagesRun& run) { return H::closeTab(run); });
    return steps;
}

class AppPagesLifetimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();
    void detectorSelfTest();
    void routeInventory();
    void deletionOrders_data();
    void deletionOrders();
    void deleteFromEverySignal_data();
    void deleteFromEverySignal();

private:
    static constexpr int PART_COUNT = 1;

    /// Cases of the route table of the application
    static std::vector<PagesRouteCase> getCases();

    /// Runs one order of deletion of the route in the state (deletionOrders)
    void runOrder(const PagesRouteCase& routeCase, const QString& state, const QString& order, bool* hasState);

    /// Runs the sweep scenario of the route. In the replay mode the victim is deleted
    /// at the target site.
    void runSweep(const PagesRouteCase& routeCase, PagesLifetimeRecorder::Mode mode, const PagesLifetimeRecorder::Site& target, PagesVictim victim,
                  std::vector<PagesLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred);

    /// Checks, that the run left nothing: no object created by it lives (except the
    /// harmless ones of PagesObjectTracker), no warning was printed
    void checkLeftovers(const PagesObjectSet& before, const QString& description);

    PagesSilentServer* m_server = nullptr;
};

std::vector<PagesRouteCase> AppPagesLifetimeTest::getCases()
{
    return PagesRouteCatalog::create(pdf::PDFAppRouteTable::createDefault());
}

void AppPagesLifetimeTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    m_server = new PagesSilentServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));
    PagesMessageCollector::install();

    // The singletons of Qt, which are created at their first use, live before the
    // objects of the runs are counted (a replay may use one first)
    QVERIFY(QApplication::clipboard());
    QVERIFY(QThreadPool::globalInstance());
}

void AppPagesLifetimeTest::cleanupTestCase()
{
    delete m_server;
    m_server = nullptr;
}

void AppPagesLifetimeTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppLocale::setCurrentTimeOverride(0);
    PagesMessageCollector::takeMessages();
}

void AppPagesLifetimeTest::checkLeftovers(const PagesObjectSet& before, const QString& description)
{
    // Deferred deletions and the queued work of the deleted objects
    PagesLifetimeHelper::settle();
    QTest::qWait(50);
    PagesLifetimeHelper::settle();

    const QStringList visible = PagesLifetimeHelper::describeVisibleTopLevels(nullptr);
    QVERIFY2(visible.isEmpty(), qPrintable(QStringLiteral("%1: windows left visible: %2").arg(description, visible.join(QStringLiteral(", ")))));

    const QStringList left = PagesObjectTracker::describeNewObjects(before, m_server, 10);
    QVERIFY2(left.isEmpty(), qPrintable(QStringLiteral("%1: objects left: %2").arg(description, left.join(QStringLiteral("; ")))));

    const QStringList messages = PagesMessageCollector::takeMessages();
    QVERIFY2(messages.isEmpty(), qPrintable(QStringLiteral("%1: warnings: %2").arg(description, messages.join(QStringLiteral("; ")))));
}

void AppPagesLifetimeTest::detectorSelfTest()
{
    // The detectors find what they are made for: a window left visible, an object
    // and a widget left alive, a single shot timer still connected after its run, a
    // warning of Qt; a single shot timer of a deleted receiver and an allowed warning
    // of the offscreen platform are no findings
    PagesMessageCollector::takeMessages();
    const PagesObjectSet before = PagesObjectTracker::getLiveObjects();

    QWidget* window = new QWidget();
    window->setObjectName(QStringLiteral("leakedWindow"));
    window->show();
    QObject* object = new QObject();
    object->setObjectName(QStringLiteral("leakedObject"));
    QObject* receiver = new QObject();
    QTimer::singleShot(60000, receiver, []() {});
    delete receiver;
    qWarning("pages lifetime self test");
    qWarning("This plugin does not support raise()");

    const QStringList visible = PagesLifetimeHelper::describeVisibleTopLevels(nullptr);
    QCOMPARE(visible, QStringList(QStringLiteral("QWidget 'leakedWindow'")));

    const QStringList left = PagesObjectTracker::describeNewObjects(before, m_server, 10);
    QVERIFY2(left.contains(QStringLiteral("QObject 'leakedObject' (parent none)")), qPrintable(left.join(QStringLiteral("; "))));
    QVERIFY2(left.contains(QStringLiteral("QWidget 'leakedWindow' (parent none)")), qPrintable(left.join(QStringLiteral("; "))));
    QVERIFY2(!left.contains(QStringLiteral("a pending single shot timer, whose timeout is connected")), qPrintable(left.join(QStringLiteral("; "))));

    QCOMPARE(PagesMessageCollector::takeMessages(), QStringList(QStringLiteral("pages lifetime self test")));

    // A single shot timer, which runs code after its run (no context object)
    QTimer::singleShot(60000, qApp, []() {});
    QVERIFY(PagesObjectTracker::describeNewObjects(before, m_server, 10).contains(QStringLiteral("a pending single shot timer, whose timeout is connected")));

    delete window;
    delete object;
    PagesLifetimeHelper::settle();
    QVERIFY(PagesLifetimeHelper::describeVisibleTopLevels(nullptr).isEmpty());

    // A dialog of a tab is no stray popup while the tab shows its page; once the page
    // is deleted, the tab owns no popup any more; a dialog of the window is stray
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(fixture.window);
        pdf::PDFAppTab* homeTab = fixture.window->getHomeTab();
        QVERIFY(homeTab && homeTab->getPage());
        QDialog* tabDialog = new QDialog(homeTab);
        tabDialog->setObjectName(QStringLiteral("tabDialog"));
        tabDialog->show();
        PagesLifetimeHelper::settle();
        QCOMPARE(PagesLifetimeHelper::describeStrayPopups(fixture.window).join(QStringLiteral("; ")), QString());

        delete homeTab->getPage();
        PagesLifetimeHelper::settle();
        QVERIFY(!homeTab->getPage());
        QCOMPARE(PagesLifetimeHelper::describeStrayPopups(fixture.window).join(QStringLiteral("; ")), QStringLiteral("QDialog 'tabDialog' (parent pdf::PDFAppWorkspaceTab)"));
        delete tabDialog;

        QDialog* windowDialog = new QDialog(fixture.window);
        windowDialog->setObjectName(QStringLiteral("windowDialog"));
        windowDialog->show();
        PagesLifetimeHelper::settle();
        QCOMPARE(PagesLifetimeHelper::describeStrayPopups(fixture.window).join(QStringLiteral("; ")), QStringLiteral("QDialog 'windowDialog' (parent pdf::PDFAppMainWindow)"));
        delete windowDialog;
    }
    PagesLifetimeHelper::settle();
    PagesMessageCollector::takeMessages();
}

void AppPagesLifetimeTest::routeInventory()
{
    // Every pattern of the route table of the window is driven by its cases (made
    // from the table, nothing listed by hand), every case is a valid route of its
    // pattern, and every pattern excluded from the sweep exists, names the lifetime
    // test, which drives it, and still has its order cases here
    pdftest::AppToolsFixture fixture;
    QVERIFY(fixture.window);
    const pdf::PDFAppRouteTable& table = fixture.window->getNavigator()->getRouteTable();
    const std::vector<PagesRouteCase> cases = PagesRouteCatalog::create(table);

    QStringList tablePatterns;
    for (const pdf::PDFAppRouteEntry& entry : table.getEntries())
    {
        tablePatterns << entry.pattern;
    }
    QStringList defaultPatterns;
    const pdf::PDFAppRouteTable defaultTable = pdf::PDFAppRouteTable::createDefault();
    for (const pdf::PDFAppRouteEntry& entry : defaultTable.getEntries())
    {
        defaultPatterns << entry.pattern;
    }
    // The data rows are made from createDefault: the window has no other routes
    QCOMPARE(tablePatterns, defaultPatterns);

    QSet<QString> names;
    for (const PagesRouteCase& routeCase : getCases())
    {
        QVERIFY2(!names.contains(routeCase.name), qPrintable(routeCase.name));
        names.insert(routeCase.name);
    }

    for (const pdf::PDFAppRouteEntry& entry : table.getEntries())
    {
        int orderCases = 0;
        int sweepCases = 0;
        for (const PagesRouteCase& routeCase : cases)
        {
            if (routeCase.pattern != entry.pattern)
            {
                continue;
            }
            ++orderCases;
            sweepCases += routeCase.isSweep ? 1 : 0;

            QString route = routeCase.route;
            route.replace(QLatin1String(PagesRouteCatalog::DOCUMENT_ID), QString(pdf::PDFAppRouteValues::DOCUMENT_ID_LENGTH, QLatin1Char('a')));
            const pdf::PDFAppRouteMatch match = table.match(pdf::PDFAppRoute::parse(route));
            QVERIFY2(match.entry && match.entry->pattern == entry.pattern, qPrintable(route));
            QVERIFY2(match.result == pdf::PDFAppRouteMatch::Result::Found || match.result == pdf::PDFAppRouteMatch::Result::Redirect ||
                     match.result == pdf::PDFAppRouteMatch::Result::NotImplemented, qPrintable(route));
        }

        QVERIFY2(orderCases > 0, qPrintable(QStringLiteral("the route %1 is not driven").arg(entry.pattern)));
        const PagesSweepExclusion* exclusion = PagesRouteCatalog::findSweepExclusion(entry.pattern);
        if (exclusion)
        {
            QCOMPARE(sweepCases, 0);
            QVERIFY2(QByteArray(exclusion->reason).startsWith("UnitTestsApp") && QByteArray(exclusion->reason).contains("Lifetime"), exclusion->pattern);
        }
        else
        {
            QVERIFY2(sweepCases >= 1, qPrintable(QStringLiteral("the route %1 is not swept").arg(entry.pattern)));
        }
    }

    for (const PagesSweepExclusion& exclusion : PagesRouteCatalog::getSweepExclusions())
    {
        QVERIFY2(tablePatterns.contains(QString::fromLatin1(exclusion.pattern)), exclusion.pattern);
    }
    for (const auto& [pattern, isQueryOnly, reason] : PagesRouteCatalog::getNotFoundCases())
    {
        Q_UNUSED(isQueryOnly);
        QVERIFY2(tablePatterns.contains(QString::fromLatin1(pattern)) && *reason, pattern);
    }

    // The values of the segments with options are all driven (every section of
    // Settings and of the properties of a document is another page)
    for (const pdf::PDFAppRouteEntry& entry : table.getEntries())
    {
        for (const auto& [name, values] : entry.parameters)
        {
            if (values.getKind() != pdf::PDFAppRouteValues::Kind::Options)
            {
                continue;
            }
            for (const QString& option : values.getOptions())
            {
                bool isFound = false;
                for (const PagesRouteCase& routeCase : cases)
                {
                    isFound = isFound || (routeCase.pattern == entry.pattern && routeCase.route.split(QLatin1Char('?')).front().split(QLatin1Char('/')).contains(option));
                }
                QVERIFY2(isFound, qPrintable(QStringLiteral("%1 = %2 of %3").arg(name, option, entry.pattern)));
            }
        }
    }
}

void AppPagesLifetimeTest::deletionOrders_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("state");

    for (const PagesRouteCase& routeCase : getCases())
    {
        for (const char* state : { "idle", "popup", "dialog", "pending" })
        {
            QTest::newRow(QStringLiteral("%1-%2").arg(routeCase.name, QLatin1String(state)).toLatin1().constData()) << routeCase.name << QString::fromLatin1(state);
        }
    }
}

void AppPagesLifetimeTest::runOrder(const PagesRouteCase& routeCase, const QString& state, const QString& order, bool* hasState)
{
    using H = PagesLifetimeHelper;
    const QString description = QStringLiteral("%1 %2 %3").arg(routeCase.route, state, order);

    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(fixture.window);
        PagesRun run;
        run.fixture = &fixture;
        run.server = m_server;
        run.routeCase = routeCase;
        run.window = fixture.window;
        QVERIFY2(H::prepare(run), qPrintable(description));
        QVERIFY2(H::open(run), qPrintable(description));
        H::focusAndHover(run);

        // The state of the page at the deletion
        bool isStateReached = true;
        if (state == QStringLiteral("popup"))
        {
            isStateReached = H::openPopup(run, 0);
        }
        else if (state == QStringLiteral("dialog"))
        {
            isStateReached = H::openDialog(run);
        }
        else if (state == QStringLiteral("pending"))
        {
            // Work started and not finished: a notification of another connection, an
            // update check, the global search, the thumbnails and the folder
            // information of Recent (requested by the pages of the Home tab when they
            // are shown), the minute ticker of the recent times
            H::writeOther(run, false);
            H::checkForUpdates(run);
            H::typeGlobalSearch(run, false);
        }
        if (hasState)
        {
            *hasState = isStateReached;
        }
        if (qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE"))
        {
            const QWidget* popup = H::findOpenPopup(run.window.data());
            qInfo().noquote() << QStringLiteral("%1: page %2, state reached %3, popup %4").arg(description, H::page(run) ? QString::fromLatin1(H::page(run)->metaObject()->className()) : QStringLiteral("none"))
                                                                                       .arg(isStateReached).arg(popup ? QString::fromLatin1(popup->metaObject()->className()) : QStringLiteral("none"));
        }
        if (!isStateReached || !run.window || !run.tab)
        {
            return;
        }

        // The victims in the order (an empty step = the tab closed by the user)
        const QStringList victims = order.split(QLatin1Char('-'));
        for (const QString& victim : victims)
        {
            if (victim == QStringLiteral("close"))
            {
                H::closeTab(run);
            }
            else if (victim == QStringLiteral("page"))
            {
                QVERIFY2(H::page(run), qPrintable(description));
                delete H::page(run);
            }
            else if (victim == QStringLiteral("tab"))
            {
                QVERIFY2(run.tab, qPrintable(description));
                delete run.tab.data();
            }
            else
            {
                delete run.window.data();
            }
            H::settle();

            // No popup or dialog of the deleted part is left (a popup of a surviving
            // tab - the document view, when its page adapter is deleted - may stay)
            const QStringList stray = H::describeStrayPopups(run.window.data());
            QVERIFY2(stray.isEmpty(), qPrintable(QStringLiteral("%1 after %2: %3").arg(description, victim, stray.join(QStringLiteral(", ")))));

            if (run.window && victim != victims.back())
            {
                // The next victim is the window: the window works before it goes
                if (victims.at(victims.indexOf(victim) + 1) == QStringLiteral("window"))
                {
                    const QString failure = H::checkWindowWorks(run);
                    QVERIFY2(failure.isEmpty(), qPrintable(QStringLiteral("%1 after %2: %3").arg(description, victim, failure)));
                }
            }
        }
        QVERIFY2(!run.window, qPrintable(description));
    }
}

void AppPagesLifetimeTest::deletionOrders()
{
    // The route opened in its tab, a child focused and another one hovered, in the
    // state (idle, a popup open, a dialog open, pending work), then closed, or the
    // page, the tab and the window deleted in every order; after each deletion no
    // popup or dialog of the deleted parts is left, the window works, and nothing
    // of the run is left after it
    QFETCH(QString, route);
    QFETCH(QString, state);

    PagesRouteCase routeCase;
    for (const PagesRouteCase& item : getCases())
    {
        if (item.name == route)
        {
            routeCase = item;
        }
    }
    QVERIFY(!routeCase.route.isEmpty());

    // Warm up: the lazily created objects of Qt and of the library live, before
    // the objects of the runs are counted
    bool hasState = false;
    runOrder(routeCase, state, QStringLiteral("close-window"), &hasState);
    if (QTest::currentTestFailed())
    {
        return;
    }
    PagesLifetimeHelper::settle();
    PagesMessageCollector::takeMessages();
    if (!hasState)
    {
        QSKIP(qPrintable(QStringLiteral("The page has no %1").arg(state)));
    }

    for (const QString& order : { QStringLiteral("close-window"), QStringLiteral("page-tab-window"), QStringLiteral("page-window"), QStringLiteral("tab-window"), QStringLiteral("window") })
    {
        const PagesObjectSet before = PagesObjectTracker::getLiveObjects();
        runOrder(routeCase, state, order, nullptr);
        if (QTest::currentTestFailed())
        {
            qWarning().noquote() << QStringLiteral("failed in the order %1").arg(order);
            return;
        }
        checkLeftovers(before, QStringLiteral("%1 %2 %3").arg(routeCase.route, state, order));
        if (QTest::currentTestFailed())
        {
            return;
        }
    }
}

void AppPagesLifetimeTest::deleteFromEverySignal_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<int>("victim");
    QTest::addColumn<int>("part");

    const std::pair<PagesVictim, const char*> victims[] = { { PagesVictim::Tab, "tab" }, { PagesVictim::Window, "window" }, { PagesVictim::Page, "page" } };
    for (const PagesRouteCase& routeCase : getCases())
    {
        if (!routeCase.isSweep)
        {
            continue;
        }
        for (const auto& [victim, victimName] : victims)
        {
            for (int part = 0; part < PART_COUNT; ++part)
            {
                QTest::newRow(QStringLiteral("%1-%2-%3").arg(routeCase.name, QLatin1String(victimName)).arg(part).toLatin1().constData()) << routeCase.name << int(victim) << part;
            }
        }
    }
}

void AppPagesLifetimeTest::runSweep(const PagesRouteCase& routeCase, PagesLifetimeRecorder::Mode mode, const PagesLifetimeRecorder::Site& target, PagesVictim victim,
                                    std::vector<PagesLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred)
{
    using H = PagesLifetimeHelper;
    const std::vector<PagesStep> steps = H::createSweepSteps();
    {
        pdftest::AppToolsFixture fixture;
        QVERIFY(fixture.window);
        PagesRun run;
        run.fixture = &fixture;
        run.server = m_server;
        run.routeCase = routeCase;
        run.window = fixture.window;
        QVERIFY2(H::prepare(run), qPrintable(routeCase.route));

        PagesLifetimeRecorder recorder(mode);
        if (mode == PagesLifetimeRecorder::Mode::Replay)
        {
            recorder.setTarget(target, [&run, victim](const PagesLifetimeRecorder::Site& site)
            {
                // A victim being destroyed already (a signal of its destructor) is not
                // deleted again: qobject_cast fails once the derived destructor has run
                QObject* object = nullptr;
                switch (victim)
                {
                    case PagesVictim::Window:
                        object = qobject_cast<pdf::PDFAppMainWindow*>(run.window.data());
                        break;
                    case PagesVictim::Tab:
                        object = qobject_cast<pdf::PDFAppTab*>(PagesLifetimeHelper::victimTab(run));
                        break;
                    case PagesVictim::Page:
                    {
                        // A tab in its destructor (a signal of the destruction) has no page
                        pdf::PDFAppTab* tab = qobject_cast<pdf::PDFAppTab*>(PagesLifetimeHelper::victimTab(run));
                        object = qobject_cast<pdf::PDFAppPage*>(tab ? tab->getPage() : nullptr);
                        break;
                    }
                }
                if (!object)
                {
                    return PagesLifetimeRecorder::Action::None;
                }
                if (PagesLifetimeRecorder::isDeferredSite(site) || PagesLifetimeRecorder::isDeferredState(site, object))
                {
                    object->deleteLater();
                    return PagesLifetimeRecorder::Action::Deferred;
                }
                delete object;
                return PagesLifetimeRecorder::Action::Deleted;
            });
        }

        for (size_t i = 0; i < steps.size(); ++i)
        {
            recorder.setStep(int(i));
            recorder.hookApplication();
            recorder.hookTree(fixture.context.get());
            recorder.hookObject(fixture.context->getNotifier());
            recorder.hookObject(fixture.context->getDatabase());

            const bool isOk = steps[i](run);
            if (recorder.isFired())
            {
                break;
            }
            if (mode == PagesLifetimeRecorder::Mode::Discover)
            {
                QVERIFY2(run.window, qPrintable(QStringLiteral("%1: the window was lost in step %2").arg(routeCase.route).arg(i)));
                QVERIFY2(isOk, qPrintable(QStringLiteral("%1: step %2 failed").arg(routeCase.route).arg(i)));
            }
            else if (!run.window || (!run.tab && i + 1 < steps.size()))
            {
                break;
            }
        }

        recorder.setStep(int(steps.size()));
        H::settle();
        if (mode == PagesLifetimeRecorder::Mode::Replay && recorder.isFired())
        {
            // The deferred work of the deleted objects finds nothing; no popup of the
            // deleted parts is left; the window keeps working after a tab or the
            // page was deleted
            H::wait(100);
            const QStringList stray = H::describeStrayPopups(run.window.data());
            QVERIFY2(stray.isEmpty(), qPrintable(QStringLiteral("popups left: %1").arg(stray.join(QStringLiteral(", ")))));
            if (victim == PagesVictim::Window)
            {
                QVERIFY(!run.window);
            }
            else
            {
                H::closePopups(run);
                const QString failure = H::checkWindowWorks(run);
                QVERIFY2(failure.isEmpty(), qPrintable(failure));
            }
        }

        recorder.stop();
        if (sites)
        {
            *sites = recorder.getSites();
        }
        if (isFired)
        {
            *isFired = recorder.isFired();
        }
        if (isDeferred)
        {
            *isDeferred = recorder.isDeferredFired();
        }
    }
}

void AppPagesLifetimeTest::deleteFromEverySignal()
{
    // The scenario of the route is run once with the recorder of every signal of
    // the application, then again for each site (the first emission of a signal of
    // an object in a step) with the victim deleted synchronously from it; nothing of
    // a replay is left after it
    QFETCH(QString, route);
    QFETCH(int, victim);
    QFETCH(int, part);

    PagesRouteCase routeCase;
    for (const PagesRouteCase& item : getCases())
    {
        if (item.name == route)
        {
            routeCase = item;
        }
    }
    QVERIFY(!routeCase.route.isEmpty());

    std::vector<PagesLifetimeRecorder::Site> sites;
    runSweep(routeCase, PagesLifetimeRecorder::Mode::Discover, PagesLifetimeRecorder::Site(), PagesVictim(victim), &sites, nullptr, nullptr);
    if (QTest::currentTestFailed())
    {
        return;
    }
    QVERIFY(!sites.empty());
    PagesLifetimeHelper::settle();
    PagesMessageCollector::takeMessages();

    // PDF4QT_LIFETIME_SITE=<n> replays one site only (debugging)
    const int onlySite = qEnvironmentVariableIsSet("PDF4QT_LIFETIME_SITE") ? qEnvironmentVariableIntValue("PDF4QT_LIFETIME_SITE") : -1;
    const bool isTrace = qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE");
    QStringList notReproduced;
    int replayed = 0;
    int deferred = 0;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        if (onlySite >= 0 ? int(i) != onlySite : int(i) % PART_COUNT != part)
        {
            continue;
        }
        if (isTrace)
        {
            qInfo().noquote() << QStringLiteral("site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
        }

        const PagesObjectSet before = PagesObjectTracker::getLiveObjects();
        bool isFired = false;
        bool isDeferred = false;
        runSweep(routeCase, PagesLifetimeRecorder::Mode::Replay, sites[i], PagesVictim(victim), nullptr, &isFired, &isDeferred);
        if (!QTest::currentTestFailed())
        {
            checkLeftovers(before, QStringLiteral("site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key));
        }
        if (QTest::currentTestFailed())
        {
            qWarning().noquote() << QStringLiteral("failed at site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
            return;
        }
        if (isFired)
        {
            ++replayed;
            deferred += isDeferred ? 1 : 0;
        }
        else
        {
            notReproduced << QStringLiteral("step %1: %2").arg(sites[i].step).arg(sites[i].key);
        }
    }

    qInfo().noquote() << QStringLiteral("%1: %2 sites, %3 deletions (%4 deferred) in part %5").arg(route).arg(sites.size()).arg(replayed).arg(deferred).arg(part);
    for (const QString& site : notReproduced)
    {
        qInfo().noquote() << QStringLiteral("not reproduced (the emission differs in the run, or the victim does not exist): ") + site;
    }

    // At least 80 % of the sites are reproduced (a site of a timer or of the order of
    // the events may move to another step; the page victim does not exist after the
    // tab is closed)
    QVERIFY2(onlySite >= 0 || replayed * 10 >= (replayed + int(notReproduced.size())) * 8, qPrintable(notReproduced.join(QLatin1Char('\n'))));
}

int main(int argc, char** argv)
{
    // The objects are tracked from the start (the hooks of QtCore)
    PagesObjectTracker::install();
    PagesLineEditEmissions::install();
    int result = 0;
    {
        PagesLifetimeApplication application(argc, argv);
        application.setAttribute(Qt::AA_Use96Dpi, true);
        AppPagesLifetimeTest test;
        QTEST_SET_MAIN_SOURCE_PATH
        result = QTest::qExec(&test, argc, argv);
    }
    PagesLineEditEmissions::uninstall();
    PagesObjectTracker::uninstall();
    return result;
}

#include "tst_apppageslifetimetest.moc"
