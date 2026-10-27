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

// Generic lifetime test of the MCP server of step 7 (plan of step 7, 10.2 item 6,
// review RT7): the scenarios of an assistant and of the user (the read tools over the
// direct path of the host and over HTTP with the read approval; the edit tools with
// Edit = Ask - the dialog, Show on Page, the status item, the preview bar, Allow /
// Deny, the notification bar, the banner of the home tab; save / save_copy / open of
// the files tool; listen streams with the resources of a document and a task of the
// tasks extension; the AI page - Start / Stop, a new token, Disconnect, Disconnect
// All, Clear of the activity, the setup cards) are run once with a recorder of every
// signal of every object of the application (classes pdf::PDFApp*, pdf::PDFDatabase*
// and pdf::PDFMcp*, emitted in the thread of the user interface), then once more for
// each signal it has seen (the first emission of the signal of an object in each
// step), with the tab, the window, the AI page or the MCP controller (with its server,
// its host and the confirmation interface) deleted synchronously from that emission.
// Nothing may crash (run it under ASan / valgrind - freed memory stays readable in a
// Release build on Windows), the window survives the deletion of a tab, of the page
// and of the controller, and the pending calls of the assistants are finished.

#include "appmcpfixture.h"

#include "pdfappaipage.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpclientsetup.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcphost.h"
#include "pdfappmcppreview.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbuttons.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMetaMethod>
#include <QPushButton>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include <functional>
#include <memory>
#include <vector>

class McpLifetimeRecorder;

/// Application of the test: it knows the events being delivered (QApplication::notify),
/// so the recorder can tell an emission inside of the key handling of a QLineEdit
class McpLifetimeApplication : public QApplication
{
public:
    McpLifetimeApplication(int& argc, char** argv) :
        QApplication(argc, argv)
    {
    }

    virtual bool notify(QObject* receiver, QEvent* event) override
    {
        QLineEdit* lineEdit = (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease || event->type() == QEvent::InputMethod) ? qobject_cast<QLineEdit*>(receiver) : nullptr;
        if (!lineEdit)
        {
            return QApplication::notify(receiver, event);
        }

        s_keyedLineEdits.push_back(QPointer<QLineEdit>(lineEdit));
        const bool result = QApplication::notify(receiver, event);
        s_keyedLineEdits.pop_back();
        return result;
    }

    /// A key event of a line edit is being delivered, which the deletion of the
    /// object deletes (the line edit itself or one of its ancestors)
    static bool isLineEditKeyInside(const QObject* object)
    {
        for (const QPointer<QLineEdit>& lineEdit : s_keyedLineEdits)
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
    static std::vector<QPointer<QLineEdit>> s_keyedLineEdits;
};

std::vector<QPointer<QLineEdit>> McpLifetimeApplication::s_keyedLineEdits;

/// One connection of the recorder to one signal of one object: the slot is the
/// first method after the methods of QObject (the pattern of QSignalSpy)
class McpLifetimeHook : public QObject
{
public:
    explicit McpLifetimeHook(McpLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature);

    virtual int qt_metacall(QMetaObject::Call call, int id, void** arguments) override;

private:
    McpLifetimeRecorder* m_recorder;
    QPointer<QObject> m_sender;
    QByteArray m_signature;
};

/// Recorder of the signals of the objects of the application. In the discovery
/// mode it collects the sites (signal of an object, step of the scenario), in the
/// replay mode it deletes the victim at the first emission of the target site.
class McpLifetimeRecorder : public QObject
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

    /// Result of the action at the target site
    enum class Action
    {
        None,       ///< No victim (it is already gone or it does not exist)
        Deleted,    ///< Deleted synchronously
        Deferred    ///< Deleted by deleteLater (a deferred site or state)
    };

    explicit McpLifetimeRecorder(Mode mode) :
        m_mode(mode)
    {
        qApp->installEventFilter(this);
        s_active = this;
    }

    virtual ~McpLifetimeRecorder() override
    {
        qApp->removeEventFilter(this);
        if (s_active == this)
        {
            s_active = nullptr;
        }
    }

    /// The recorder of the running scenario (nullptr = none)
    static McpLifetimeRecorder* getActive() { return s_active; }

    /// The victim of the running replay is gone (the waits of the steps end)
    static bool isActiveFired() { return s_active && s_active->isFired(); }

    void setTarget(const Site& site, std::function<Action(const Site&)> action)
    {
        m_target = site;
        m_action = std::move(action);
    }

    /// Sites, where the victim is deleted by deleteLater instead of synchronously,
    /// each with the reason why no code can survive it:
    ///  - a signal of a destructor (PDFAppPage::aboutToBeDestroyed): the dying object is
    ///    still a child of its parents, a deletion of an ancestor deletes it twice (Qt);
    ///  - a signal of the document registry, which passes the document as a plain
    ///    pointer (documentAdded / documentChanged): the document belongs to the
    ///    victim, the receivers connected after the deletion get the pointer of the
    ///    deleted document as their argument and cannot know it (Qt copies the
    ///    pointer, not a guard)
    static bool isDeferredSite(const Site& site)
    {
        return site.key.endsWith(QStringLiteral("::aboutToBeDestroyed()")) ||
               site.key.endsWith(QStringLiteral("PDFAppDocumentRegistry::documentAdded(pdf::PDFAppDocument*)")) ||
               site.key.endsWith(QStringLiteral("PDFAppDocumentRegistry::documentChanged(pdf::PDFAppDocument*)"));
    }

    /// The state of the emission defers the deletion of the victim (counted with the site):
    ///  - an emission inside of the key handling of a QLineEdit, which the deletion of
    ///    the victim deletes: Qt does not survive the deletion of the line edit inside
    ///    of its own textEdited / textChanged (QWidgetLineControl::finishChange)
    static bool isDeferredState(const Site& site, const QObject* victim)
    {
        Q_UNUSED(site);
        return McpLifetimeApplication::isLineEditKeyInside(victim);
    }

    void setStep(int step) { m_step = step; }
    bool isFired() const { return m_isFired; }
    bool isDeferredFired() const { return m_isFired && m_isDeferredFired; }
    const std::vector<Site>& getSites() const { return m_sites; }

    /// Stops the recording (the end of the scenario, the fixture is destroyed)
    void stop() { m_isStopped = true; }

    /// Hooks the object and all of its descendants
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

    /// Hooks every top-level widget and its descendants
    void hookApplication()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            hookTree(widget);
        }
    }

    void hookObject(QObject* object)
    {
        if (!object || m_isStopped || object->thread() != qApp->thread())
        {
            return;
        }

        auto it = m_hooked.find(object);
        if (it != m_hooked.end() && it.value())
        {
            return;
        }

        // Signals declared by the classes of the application and of the MCP library
        // only (the signals of Qt and of the widget library are not emitted by them)
        const QMetaObject* metaObject = object->metaObject();
        if (!isApplicationClass(metaObject))
        {
            return;
        }
        m_hooked.insert(object, QPointer<QObject>(object));

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

            McpLifetimeHook* hook = new McpLifetimeHook(this, object, method.methodSignature());
            if (!QMetaObject::connect(object, i, hook, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr))
            {
                delete hook;
            }
        }
    }

    void onSignal(QObject* sender, const QByteArray& signature)
    {
        // A signal of a worker or of an I/O thread of the server is no site: the
        // objects of the user interface are deleted in their own thread only
        if (m_isStopped || !sender || QThread::currentThread() != qApp->thread())
        {
            return;
        }

        // A signal emitted while a widget paints is no site (Qt does not survive a
        // deletion of the painted widget inside of its paint event)
        if (m_paintedWidget && m_paintedWidget->paintingActive())
        {
            return;
        }

        // A signal emitted from QAbstractItemModel::data, which a delegate calls while
        // it paints or measures its rows (the pattern of UnitTestsAppAnnotationLifetime)
        if (qobject_cast<const QAbstractItemModel*>(sender) && signature == QByteArrayLiteral("pictureRequested()"))
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
            // The deletion emits further signals (also of the target site): no
            // second deletion of the victim from inside of its own destruction
            m_isInAction = true;
            const Action action = m_action(m_target);
            m_isFired = action != Action::None;
            m_isDeferredFired = action == Action::Deferred;
            m_isInAction = false;
        }
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        // An object receiving an event is constructed: hook it (dialogs, bars and
        // rows created in the middle of a step)
        if (event->type() == QEvent::Paint && watched->isWidgetType())
        {
            m_paintedWidget = static_cast<QWidget*>(watched);
        }
        // The object is hooked from the event loop, never inside of its constructor (a
        // widget gets events while it is constructed - polish, child events): nothing
        // of the application can be connected to an object, which is not constructed
        // yet, so a signal of its constructor is no site (a deletion of its parent
        // would delete the half constructed object)
        if (!m_isStopped && watched && !m_hooked.contains(watched))
        {
            m_pendingHooks.push_back(QPointer<QObject>(watched));
            if (!m_isFlushScheduled)
            {
                m_isFlushScheduled = true;
                QTimer::singleShot(0, this, [this]() { flushPendingHooks(); });
            }
        }
        return false;
    }

    /// Hooks the objects, which have received an event since the last flush
    void flushPendingHooks()
    {
        m_isFlushScheduled = false;
        std::vector<QPointer<QObject>> pending = std::move(m_pendingHooks);
        m_pendingHooks.clear();
        for (const QPointer<QObject>& object : pending)
        {
            if (object)
            {
                hookObject(object.data());
            }
        }
    }

    static bool isApplicationClass(const QMetaObject* metaObject)
    {
        if (!metaObject)
        {
            return false;
        }
        const QByteArray className(metaObject->className());
        return className.startsWith("pdf::PDFApp") || className.startsWith("pdf::PDFDatabase") || className.startsWith("pdf::PDFMcp");
    }

    /// Path of the object by the class names of its parents (no pointers, no object
    /// names - both differ in each run)
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
    static McpLifetimeRecorder* s_active;

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
    std::vector<QPointer<QObject>> m_pendingHooks;
    bool m_isFlushScheduled = false;
};

McpLifetimeRecorder* McpLifetimeRecorder::s_active = nullptr;

McpLifetimeHook::McpLifetimeHook(McpLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature) :
    QObject(recorder),
    m_recorder(recorder),
    m_sender(sender),
    m_signature(signature)
{
}

int McpLifetimeHook::qt_metacall(QMetaObject::Call call, int id, void** arguments)
{
    id = QObject::qt_metacall(call, id, arguments);
    if (id < 0 || call != QMetaObject::InvokeMetaMethod)
    {
        return id;
    }
    if (id == 0)
    {
        // A signal of another thread: the sender is not read there (the recorder
        // ignores it, QPointer is not thread safe against the deletion)
        if (QThread::currentThread() == qApp->thread())
        {
            m_recorder->onSignal(m_sender.data(), m_signature);
        }
    }
    return id - 1;
}

/// The fixture of the scenarios: the real confirmation interface without the input
/// delay (the pattern of UnitTestsAppMcpEdit)
class McpLifetimeFixture : public pdftest::AppMcpFixture
{
public:
    McpLifetimeFixture()
    {
        if (host() && ui())
        {
            host()->setConfirmationProvider(ui());
            ui()->setInputDelay(0);
        }
    }

    pdf::PDFAppMcpConfirmationUi* ui() const { return host() ? pdf::PDFAppMcpConfirmationUi::find(host()) : nullptr; }
};

/// State of one run of a scenario
struct McpLifetimeRun
{
    McpLifetimeFixture* fixture = nullptr;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppTab> tab;               ///< Tab of the scenario (the document tab, the home tab of the AI page)
    QPointer<pdf::PDFAppTab> homeTab;
    std::shared_ptr<pdf::PDFMcpCall> call;      ///< The direct call of the step
    std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
    std::unique_ptr<pdftest::McpTestClient> client;
    std::vector<std::unique_ptr<pdftest::McpTestStream>> streams;
    QString taskId;
    QString documentId;
    QString secondFile;
};

using McpLifetimeStep = std::function<bool(McpLifetimeRun&)>;

/// Victim of the deletion
enum class McpLifetimeVictim
{
    Tab,            ///< The tab of the scenario
    Window,
    Page,           ///< The AI page
    Controller      ///< The MCP controller (its server, host and confirmation interface)
};

/// Helpers of the scenarios: every input is sent through guards, the victim may be
/// deleted inside of any event, a wait ends as soon as the victim is gone
class McpLifetimeHelper
{
public:
    McpLifetimeHelper() = delete;

    static void settle() { pdftest::AppDocumentFixture::settle(); }

    static int timeout(int milliseconds) { return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor(); }

    static QJsonObject json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

    /// Waits for the condition; ends at once, when the victim of the replay is gone
    static bool waitFor(const std::function<bool()>& condition, int milliseconds = 10000)
    {
        const bool result = QTest::qWaitFor([&condition]() { return McpLifetimeRecorder::isActiveFired() || condition(); }, timeout(milliseconds));
        settle();
        return (result && !McpLifetimeRecorder::isActiveFired()) || condition();
    }

    static pdf::PDFAppDocumentTab* documentTab(const McpLifetimeRun& run)
    {
        return qobject_cast<pdf::PDFAppDocumentTab*>(run.tab.data());
    }

    static pdf::PDFAppMcpConfirmationUi* ui(const McpLifetimeRun& run)
    {
        return run.fixture->ui();
    }

    static pdf::PDFAppMcpConfirmDialog* dialog(const McpLifetimeRun& run)
    {
        pdf::PDFAppMcpConfirmationUi* confirmationUi = ui(run);
        pdf::PDFAppMcpConfirmDialog* confirmDialog = confirmationUi ? confirmationUi->getDialog() : nullptr;
        return confirmDialog && confirmDialog->isVisible() ? confirmDialog : nullptr;
    }

    static pdf::PDFAppMcpConfirmDialog* waitDialog(const McpLifetimeRun& run)
    {
        waitFor([&run]() { return dialog(run) != nullptr; });
        return dialog(run);
    }

    static pdf::PDFAppAiPage* aiPage(const McpLifetimeRun& run)
    {
        return run.tab ? qobject_cast<pdf::PDFAppAiPage*>(run.tab->getPage()) : nullptr;
    }

    /// The page of the scenario for the victim Page
    static QWidget* victimPage(const McpLifetimeRun& run)
    {
        return aiPage(run);
    }

    static bool navigate(McpLifetimeRun& run, const QString& route)
    {
        if (!run.window || !run.window->getNavigator())
        {
            return false;
        }
        run.window->getNavigator()->navigate(pdf::PDFAppRoute::parse(route), pdf::PDFAppNavigationMode::Push);
        settle();
        return true;
    }

    /// A mouse click (press and release, the release only to a living widget)
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

    /// Starts a direct call of the host (the call of the run)
    static bool startCall(McpLifetimeRun& run, const QString& tool, const QJsonObject& arguments)
    {
        run.call.reset();
        if (!run.fixture->host())
        {
            return false;
        }
        run.call = run.fixture->startCall(tool, arguments);
        run.calls.push_back(run.call);
        settle();
        return run.call != nullptr;
    }

    /// Waits for the direct call of the run; \returns its success
    static bool finishCall(McpLifetimeRun& run, bool isErrorExpected = false)
    {
        std::shared_ptr<pdf::PDFMcpCall> call = run.call;
        if (!call)
        {
            return false;
        }
        waitFor([call]() { return call->isFinished(); }, 30000);
        const std::optional<pdf::PDFMcpCallResult> result = call->getResult();
        if (!result)
        {
            return false;
        }
        const bool isError = result->kind != pdf::PDFMcpCallResult::Kind::Complete || result->isError;
        return isError == isErrorExpected;
    }

    /// A direct call of a tool, which needs no confirmation
    static bool call(McpLifetimeRun& run, const QString& tool, const QJsonObject& arguments)
    {
        return startCall(run, tool, arguments) && finishCall(run);
    }

    /// Writes a permission (the host gets it by the notifier)
    static bool permission(McpLifetimeRun& run, pdf::PDFAppMcpPermissionKind kind, pdf::PDFAppMcpPermission value)
    {
        return run.fixture->host() && run.fixture->setPermission(kind, value);
    }

    /// The scripted client of the run (the token of the settings)
    static pdftest::McpTestClient* client(McpLifetimeRun& run, bool withTasks = false)
    {
        if (!run.fixture->controller() || !run.fixture->controller()->isRunning())
        {
            return nullptr;
        }
        run.client = run.fixture->client(pdftest::McpTestEra::Modern);
        if (withTasks)
        {
            run.client->setCapabilities(json(R"({"elicitation":{},"extensions":{"io.modelcontextprotocol/tasks":{}}})"));
            run.client->setInputRetryRounds(0);
        }
        return run.client.get();
    }

    static bool isHttpSuccess(const QJsonObject& response)
    {
        return response.contains(QStringLiteral("result")) && !response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool();
    }

    /// A call of a tool over HTTP (the client of the run)
    static bool httpCall(McpLifetimeRun& run, const QString& tool, const char* arguments)
    {
        return run.client && isHttpSuccess(run.client->callTool(tool, json(arguments)));
    }

    /// Opens a listen stream of the client of the run
    static pdftest::McpTestStream* listen(McpLifetimeRun& run, const QJsonObject& notifications)
    {
        if (!run.client)
        {
            return nullptr;
        }
        std::unique_ptr<pdftest::McpTestStream> stream = run.client->openStream(run.client->createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), notifications } })));
        if (!stream)
        {
            return nullptr;
        }
        pdftest::McpTestStream* result = stream.get();
        run.streams.push_back(std::move(stream));
        waitFor([result]() { return !result->messages().empty() || result->isClosed(); });
        return result;
    }

    /// Waits until a stream has a message of the method
    static bool waitStream(pdftest::McpTestStream* stream, const QString& method)
    {
        return stream && waitFor([stream, method]()
        {
            for (const QJsonObject& message : stream->messages())
            {
                if (message.value(QStringLiteral("method")).toString() == method)
                {
                    return true;
                }
            }
            return stream->isClosed();
        });
    }

    static QJsonObject addNote()
    {
        return json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");
    }

    static QJsonObject addHighlightAndNote()
    {
        return json(R"({"action":"add","add":{"items":[{"type":"highlight","page":1,"quads":[[30,365,120,365,30,350,120,350]]},{"type":"note","page":1,"point":{"x":200,"y":380}}]}})");
    }

    static QJsonObject files(const QString& action, const QJsonObject& parameters)
    {
        QJsonObject arguments;
        arguments.insert(QStringLiteral("action"), action);
        arguments.insert(action, parameters);
        return arguments;
    }

    /// Opens the sample in a document tab and waits until it is shown
    static pdf::PDFAppDocumentTab* openReady(McpLifetimeFixture& fixture, const QString& filePath)
    {
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.open(filePath);
        if (!tab || !QTest::qWaitFor([&tab]() { return !tab || (tab->getDocument() && tab->getDocument()->isReady() && tab->getView() && tab->getView()->getPageCount() > 0); }, timeout(20000)))
        {
            return nullptr;
        }
        settle();
        return tab.data();
    }

    /// Writes a free port of the loopback into mcp.port (the AI page starts the server by its button)
    static bool freePort(McpLifetimeFixture& fixture)
    {
        const quint16 port = pdftest::AppMcpFixture::findFreePort();
        return port >= 1024 && !fixture.context->getSettings().setValue(pdf::PDFSettingKeys::mcpPort(), int(port)).isError();
    }

    static bool isRunning(const McpLifetimeRun& run)
    {
        return run.fixture->controller() && run.fixture->controller()->isRunning();
    }

    /// The steps of a scenario; the setup (permissions, documents) is done here
    /// before the recording
    static std::vector<McpLifetimeStep> scenario(const QString& name, McpLifetimeRun& run);
};

std::vector<McpLifetimeStep> McpLifetimeHelper::scenario(const QString& name, McpLifetimeRun& run)
{
    using H = McpLifetimeHelper;
    using Kind = pdf::PDFAppMcpPermissionKind;
    using Permission = pdf::PDFAppMcpPermission;
    McpLifetimeFixture& fixture = *run.fixture;
    std::vector<McpLifetimeStep> steps;

    if (name == QStringLiteral("ai"))
    {
        // The AI page of the home tab; the server is started by its button
        if (!fixture.setPermissions(Permission::Allow) || !H::freePort(fixture))
        {
            return steps;
        }

        steps.push_back([](McpLifetimeRun& run) { return H::navigate(run, QStringLiteral("/ai")) && H::aiPage(run); });
        steps.push_back([](McpLifetimeRun& run)
        {
            pdf::PDFAppAiPage* page = H::aiPage(run);
            return page && H::click(page->getStartStopButton()) && H::waitFor([&run]() { return H::isRunning(run); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // An assistant connects: its row of the page, an activity row
            return H::client(run) && run.client->listTools().contains(QStringLiteral("result")) && H::httpCall(run, QStringLiteral("application"), R"({"action":"get_state"})") &&
                   H::waitFor([&run]() { return H::aiPage(run) && !H::aiPage(run)->getAssistantRows().isEmpty(); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A new token: Cancel
            pdf::PDFAppAiPage* page = H::aiPage(run);
            if (!page || !H::click(page->getNewTokenButton()) || !H::aiPage(run))
            {
                return false;
            }
            pdf::PDFAppAiNewTokenDialog* tokenDialog = H::aiPage(run)->getNewTokenDialog();
            return H::waitFor([&run]() { return H::aiPage(run) && H::aiPage(run)->getNewTokenDialog() && H::aiPage(run)->getNewTokenDialog()->isVisible(); }) &&
                   tokenDialog && H::aiPage(run) && H::click(H::aiPage(run)->getNewTokenDialog()->getCancelButton());
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A new token: Create New Token (everybody disconnected, the server restarts)
            pdf::PDFAppAiPage* page = H::aiPage(run);
            if (!page || !page->openNewTokenDialog())
            {
                return false;
            }
            return H::waitFor([&run]() { return H::aiPage(run) && H::aiPage(run)->getNewTokenDialog() && H::aiPage(run)->getNewTokenDialog()->isVisible(); }) &&
                   H::aiPage(run) && H::click(H::aiPage(run)->getNewTokenDialog()->getCreateButton()) && H::waitFor([&run]() { return H::isRunning(run); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            return H::client(run) && run.client->listTools().contains(QStringLiteral("result")) &&
                   H::waitFor([&run]() { return H::aiPage(run) && !H::aiPage(run)->getAssistantRows().isEmpty(); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Disconnect of the row
            pdf::PDFAppAiPage* page = H::aiPage(run);
            const QList<pdf::PDFAssistantRow*> rows = page ? page->getAssistantRows() : QList<pdf::PDFAssistantRow*>();
            return !rows.isEmpty() && H::click(rows.front()->getDisconnectButton()) && H::waitFor([&run]() { return H::aiPage(run) && H::aiPage(run)->getAssistantRows().isEmpty(); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Disconnect All
            if (!run.client || !run.client->listTools().contains(QStringLiteral("result")) ||
                !H::waitFor([&run]() { return H::aiPage(run) && !H::aiPage(run)->getAssistantRows().isEmpty(); }))
            {
                return false;
            }
            pdf::PDFAppAiPage* page = H::aiPage(run);
            return page && H::click(page->getDisconnectAllLink()) && H::waitFor([&run]() { return H::aiPage(run) && H::aiPage(run)->getAssistantRows().isEmpty(); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Clear of the activity (a row written by the log of another window)
            pdf::PDFMcpActivityRow row;
            row.time = QDateTime::currentMSecsSinceEpoch();
            row.client = QStringLiteral("Claude Code");
            row.action = QStringLiteral("annotations.add");
            row.document = QStringLiteral("Project_Report.pdf");
            row.status = pdf::PDFMcpActivityStatus::Allowed;
            if (pdf::PDFMcpRepository(run.fixture->context->getDatabase()).addActivity({ row }).isError() ||
                !H::waitFor([&run]() { return H::aiPage(run) && !H::aiPage(run)->getActivityRows().isEmpty(); }))
            {
                return false;
            }
            pdf::PDFAppAiPage* page = H::aiPage(run);
            return page && H::click(page->getClearLink());
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // The setup cards: a tile, a variant chip, the copy buttons
            pdf::PDFAppAiPage* page = H::aiPage(run);
            if (!page || !H::click(page->getTile(pdf::PDFAppMcpSetupClient::Codex)) || !H::aiPage(run) || !H::click(H::aiPage(run)->getTile(pdf::PDFAppMcpSetupClient::ClaudeCode)))
            {
                return false;
            }
            page = H::aiPage(run);
            const QList<QPushButton*> chips = page ? page->getVariantChips() : QList<QPushButton*>();
            return chips.size() >= 2 && H::click(chips.at(1)) && H::aiPage(run) && H::click(H::aiPage(run)->getCopySetupButton()) &&
                   H::aiPage(run) && H::click(H::aiPage(run)->getCopyAddressButton());
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Stop, Start again
            pdf::PDFAppAiPage* page = H::aiPage(run);
            if (!page || !H::click(page->getStartStopButton()) || !H::waitFor([&run]() { return !H::isRunning(run); }))
            {
                return false;
            }
            page = H::aiPage(run);
            return page && H::click(page->getStartStopButton()) && H::waitFor([&run]() { return H::isRunning(run); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // The permissions changed by the settings (a new catalog), Stop by the controller
            if (!H::permission(run, Kind::Edit, Permission::Deny) || !H::permission(run, Kind::Read, Permission::Ask))
            {
                return false;
            }
            run.fixture->controller()->stop();
            H::settle();
            return true;
        });
        return steps;
    }

    // The document scenarios: one document in its tab
    if (!fixture.setPermissions(Permission::Allow))
    {
        return steps;
    }
    run.tab = H::openReady(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
    if (!run.tab)
    {
        return steps;
    }
    run.documentId = H::documentTab(run)->getDocument()->getId();

    if (name == QStringLiteral("read"))
    {
        if (!fixture.setPermission(Kind::Read, Permission::Ask))
        {
            return steps;
        }

        steps.push_back([](McpLifetimeRun& run)
        {
            // The read approval of the assistant (966:150610)
            return H::startCall(run, QStringLiteral("document"), H::json(R"({"action":"get_info"})")) && H::waitDialog(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            pdf::PDFAppMcpConfirmDialog* confirmDialog = H::dialog(run);
            return confirmDialog && H::click(confirmDialog->getAllowButton()) && H::finishCall(run);
        });
        steps.push_back([](McpLifetimeRun& run) { return H::call(run, QStringLiteral("text"), H::json(R"({"action":"get_text","get_text":{"pages":"1","geometry":"words"}})")); });
        steps.push_back([](McpLifetimeRun& run) { return H::call(run, QStringLiteral("text"), H::json(R"({"action":"search","search":{"query":"alpha"}})")); });
        steps.push_back([](McpLifetimeRun& run) { return H::call(run, QStringLiteral("render"), H::json(R"({"action":"page","page":{"page":1,"dpi":36}})")); });
        steps.push_back([](McpLifetimeRun& run) { return H::call(run, QStringLiteral("view"), H::json(R"({"action":"show_area","show_area":{"page":1,"rect":{"left":0,"bottom":0,"right":10,"top":10}}})")); });
        steps.push_back([](McpLifetimeRun& run) { return H::call(run, QStringLiteral("view"), H::json(R"({"action":"go_to","go_to":{"outlineItem":"2.1"}})")); });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A read denied by the user (another assistant asks again)
            run.fixture->setClient(QStringLiteral("gemini-cli-mcp-client"));
            const bool isOk = H::startCall(run, QStringLiteral("document"), H::json(R"({"action":"get_outline"})")) && H::waitDialog(run);
            pdf::PDFAppMcpConfirmDialog* confirmDialog = H::dialog(run);
            return isOk && confirmDialog && H::click(confirmDialog->getDenyButton()) && H::finishCall(run, true);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            run.fixture->setClient(QStringLiteral("claude-code"));
            return H::permission(run, Kind::Read, Permission::Allow) && run.fixture->startServer();
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            return H::client(run) && run.client->listTools().contains(QStringLiteral("result")) &&
                   H::httpCall(run, QStringLiteral("document"), R"({"action":"get_pages","get_pages":{"pages":"1-2"}})");
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            return H::httpCall(run, QStringLiteral("text"), R"({"action":"get_text","get_text":{"pages":"1"}})") &&
                   H::httpCall(run, QStringLiteral("render"), R"({"action":"overview","overview":{"pages":"1-3","maxSize":64}})");
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            if (!run.client)
            {
                return false;
            }
            const QJsonObject response = run.client->request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), pdf::PDFAppMcpDocumentResources::getDocumentPrefix(run.documentId) + QStringLiteral("info") } }));
            QJsonObject activate = H::json(R"({"action":"activate","activate":{}})");
            activate.insert(QStringLiteral("documentId"), run.documentId);
            const QJsonObject activated = run.client ? run.client->callTool(QStringLiteral("view"), activate) : QJsonObject();
            if (!response.contains(QStringLiteral("result")) || !H::isHttpSuccess(activated))
            {
                qWarning().noquote() << QJsonDocument(response).toJson(QJsonDocument::Compact).left(300) << QJsonDocument(activated).toJson(QJsonDocument::Compact).left(300);
                return false;
            }
            return true;
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            if (!run.fixture->controller())
            {
                return false;
            }
            run.fixture->controller()->stop();
            H::settle();
            return true;
        });
    }
    else if (name == QStringLiteral("edit"))
    {
        if (!fixture.setPermission(Kind::Edit, Permission::Ask))
        {
            return steps;
        }

        steps.push_back([](McpLifetimeRun& run) { return H::startCall(run, QStringLiteral("annotations"), H::addHighlightAndNote()) && H::waitDialog(run); });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Show on Page: the ghosts and the preview bar
            pdf::PDFAppMcpConfirmDialog* confirmDialog = H::dialog(run);
            return confirmDialog && H::click(confirmDialog->getShowOnPageLink()) &&
                   H::waitFor([&run]() { return H::ui(run) && H::ui(run)->getPreviewBar() && H::ui(run)->getPreviewBar()->isVisible(); });
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // The status item gives the dialog back
            pdf::PDFAppDocumentTab* tab = H::documentTab(run);
            QToolButton* item = tab && H::ui(run) ? H::ui(run)->findStatusItem(tab->getView()) : nullptr;
            return item && H::click(item) && H::waitDialog(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Allow of the preview bar: one undo step, the notification bar
            pdf::PDFAppMcpConfirmDialog* confirmDialog = H::dialog(run);
            if (!confirmDialog || !H::click(confirmDialog->getShowOnPageLink()) ||
                !H::waitFor([&run]() { return H::ui(run) && H::ui(run)->getPreviewBar() && H::ui(run)->getPreviewBar()->isVisible(); }))
            {
                return false;
            }
            return H::click(H::ui(run)->getPreviewBar()->getAllowButton()) && H::finishCall(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            pdf::PDFAppDocumentTab* tab = H::documentTab(run);
            pdf::PDFAppMcpNotificationBar* bar = tab && H::ui(run) ? H::ui(run)->findNotificationBar(tab->getDocument()) : nullptr;
            return bar && H::click(bar->getShowChangesButton());
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            pdf::PDFAppDocumentTab* tab = H::documentTab(run);
            pdf::PDFAppMcpNotificationBar* bar = tab && H::ui(run) ? H::ui(run)->findNotificationBar(tab->getDocument()) : nullptr;
            return bar && H::click(bar->getUndoButton());
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Deny of the dialog
            return H::startCall(run, QStringLiteral("annotations"), H::addNote()) && H::waitDialog(run) &&
                   H::click(H::dialog(run)->getDenyButton()) && H::finishCall(run, true);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Allow of the dialog, the notification bar closed
            if (!H::startCall(run, QStringLiteral("annotations"), H::addNote()) || !H::waitDialog(run) || !H::click(H::dialog(run)->getAllowButton()) || !H::finishCall(run))
            {
                return false;
            }
            pdf::PDFAppDocumentTab* tab = H::documentTab(run);
            pdf::PDFAppMcpNotificationBar* bar = tab && H::ui(run) ? H::ui(run)->findNotificationBar(tab->getDocument()) : nullptr;
            return bar && H::click(bar->getCloseButton());
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A request of the document while the home tab is shown: the banner, its Show
            QJsonObject arguments = H::addNote();
            arguments.insert(QStringLiteral("documentId"), run.documentId);
            if (!run.window || !run.homeTab)
            {
                return false;
            }
            run.window->activateTab(run.homeTab.data());
            H::settle();
            if (!H::startCall(run, QStringLiteral("annotations"), arguments) ||
                !H::waitFor([&run]() { return H::ui(run) && run.homeTab && H::ui(run)->findBanner(run.homeTab.data()) && H::ui(run)->findBanner(run.homeTab.data())->isVisible(); }))
            {
                return false;
            }
            return H::click(H::ui(run)->findBanner(run.homeTab.data())->getShowButton()) && H::waitDialog(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            pdf::PDFAppMcpConfirmDialog* confirmDialog = H::dialog(run);
            return confirmDialog && H::click(confirmDialog->getAllowButton()) && H::finishCall(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Edit = Allow: an outline edit without a question
            return H::permission(run, Kind::Edit, Permission::Allow) &&
                   H::call(run, QStringLiteral("outline"), H::json(R"({"action":"add","add":{"parent":"2","title":"Section 1.2","page":2,"zoom":"fit_page"}})"));
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Escape of the dialog denies
            if (!H::permission(run, Kind::Edit, Permission::Ask) || !H::startCall(run, QStringLiteral("annotations"), H::addNote()) || !H::waitDialog(run))
            {
                return false;
            }
            QTest::keyClick(H::dialog(run), Qt::Key_Escape);
            return H::finishCall(run, true);
        });
    }
    else if (name == QStringLiteral("files"))
    {
        if (!fixture.setPermission(Kind::Files, Permission::Ask))
        {
            return steps;
        }
        run.secondFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Q3_Forecast.pdf"), 2);

        steps.push_back([](McpLifetimeRun& run) { return H::call(run, QStringLiteral("annotations"), H::addNote()); });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Save, Deny
            return H::startCall(run, QStringLiteral("files"), H::json(R"({"action":"save"})")) && H::waitDialog(run) &&
                   H::click(H::dialog(run)->getDenyButton()) && H::finishCall(run, true);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Save, Allow
            return H::startCall(run, QStringLiteral("files"), H::json(R"({"action":"save"})")) && H::waitDialog(run) &&
                   H::click(H::dialog(run)->getAllowButton()) && H::finishCall(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Save a copy
            const QJsonObject copy = H::files(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), run.fixture->path(QStringLiteral("Report_reviewed.pdf")) } }));
            return H::startCall(run, QStringLiteral("files"), copy) && H::waitDialog(run) && H::click(H::dialog(run)->getAllowButton()) && H::finishCall(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Open a new file: Open
            const QJsonObject open = H::files(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), run.secondFile } }));
            return H::startCall(run, QStringLiteral("files"), open) && H::waitDialog(run) && H::click(H::dialog(run)->getAllowButton()) && H::finishCall(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Open another file: Deny (in the tab of the scenario)
            if (!run.window || !run.tab)
            {
                return false;
            }
            run.window->activateTab(run.tab.data());
            H::settle();
            const QJsonObject open = H::files(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("other.pdf"), 1) } }));
            return H::startCall(run, QStringLiteral("files"), open) && H::waitDialog(run) && H::click(H::dialog(run)->getDenyButton()) && H::finishCall(run, true);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Files = Allow: a file opened in the background, a copy over its file
            if (!H::permission(run, Kind::Files, Permission::Allow))
            {
                return false;
            }
            const QJsonObject open = H::files(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("background.pdf"), 1) },
                                                                                   { QStringLiteral("activate"), false } }));
            const QJsonObject copy = H::files(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), run.fixture->path(QStringLiteral("Report_reviewed.pdf")) }, { QStringLiteral("overwrite"), true } }));
            return H::call(run, QStringLiteral("files"), open) && H::startCall(run, QStringLiteral("files"), copy) && H::waitDialog(run) &&
                   H::click(H::dialog(run)->getAllowButton()) && H::finishCall(run);
        });
    }
    else if (name == QStringLiteral("stream"))
    {
        steps.push_back([](McpLifetimeRun& run) { return run.fixture->startServer(); });
        steps.push_back([](McpLifetimeRun& run)
        {
            return H::client(run, true) && run.client->listTools().contains(QStringLiteral("result")) && run.client->request(QStringLiteral("resources/list")).contains(QStringLiteral("result"));
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A listen stream: the list, the resources of the document and the state
            const QJsonObject notifications({ { QStringLiteral("resourcesListChanged"), true },
                                              { QStringLiteral("resourceSubscriptions"), QJsonArray({ pdf::PDFAppMcpDocumentResources::getDocumentPrefix(run.documentId) + QStringLiteral("info"),
                                                                                                       QStringLiteral("pdf4qt://application/state") }) } });
            return H::waitStream(H::listen(run, notifications), QStringLiteral("notifications/subscriptions/acknowledged"));
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A change of the document: the updated resources
            return H::httpCall(run, QStringLiteral("annotations"), R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})") &&
                   !run.streams.empty() && H::waitStream(run.streams.front().get(), QStringLiteral("notifications/resources/updated"));
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // Edit = Ask: the call of a client with the tasks extension becomes a task
            if (!H::permission(run, Kind::Edit, Permission::Ask) || !run.client)
            {
                return false;
            }
            const QJsonObject created = run.client->callTool(QStringLiteral("annotations"), H::addNote()).value(QStringLiteral("result")).toObject();
            run.taskId = created.value(QStringLiteral("taskId")).toString();
            return !run.taskId.isEmpty() && H::waitDialog(run);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // notifications/tasks of the task on a second stream, Allow
            pdftest::McpTestStream* stream = H::listen(run, QJsonObject({ { QStringLiteral("taskIds"), QJsonArray({ run.taskId }) } }));
            return stream && H::dialog(run) && H::click(H::dialog(run)->getAllowButton()) && H::waitStream(stream, QStringLiteral("notifications/tasks"));
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            return run.client && run.client->pollTask(run.taskId).value(QStringLiteral("result")).toObject().value(QStringLiteral("status")).toString() == QStringLiteral("completed");
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A second task denied
            if (!run.client)
            {
                return false;
            }
            const QJsonObject created = run.client->callTool(QStringLiteral("annotations"), H::addNote()).value(QStringLiteral("result")).toObject();
            run.taskId = created.value(QStringLiteral("taskId")).toString();
            return !run.taskId.isEmpty() && H::waitDialog(run) && H::click(H::dialog(run)->getDenyButton()) && run.client &&
                   !run.client->pollTask(run.taskId).value(QStringLiteral("result")).toObject().isEmpty();
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // A second document opened and closed: the list changes
            pdf::PDFAppDocumentTab* second = H::openReady(*run.fixture, pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("second.pdf"), 1));
            if (!second || !run.window)
            {
                return false;
            }
            const bool isListed = !run.streams.empty() && H::waitStream(run.streams.front().get(), QStringLiteral("notifications/resources/list_changed"));
            return isListed && run.window && run.window->closeTab(second);
        });
        steps.push_back([](McpLifetimeRun& run)
        {
            // The client closes its streams, the server stops
            for (const std::unique_ptr<pdftest::McpTestStream>& stream : run.streams)
            {
                stream->close();
            }
            H::settle();
            if (!run.fixture->controller())
            {
                return false;
            }
            run.fixture->controller()->stop();
            H::settle();
            return true;
        });
    }

    return steps;
}

class AppMcpLifetimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void deleteFromEverySignal_data();
    void deleteFromEverySignal();

private:
    static constexpr int PART_COUNT = 4;

    static QStringList getScenarios() { return { QStringLiteral("read"), QStringLiteral("edit"), QStringLiteral("files"), QStringLiteral("stream"), QStringLiteral("ai") }; }

    /// Runs the scenario. In the replay mode the victim is deleted at the target
    /// site. \returns the sites seen (discovery) and whether the victim was deleted.
    void runScenario(const QString& name, McpLifetimeRecorder::Mode mode, const McpLifetimeRecorder::Site& target, McpLifetimeVictim victim,
                     std::vector<McpLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred = nullptr);
};

void AppMcpLifetimeTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppMcpLifetimeTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppMcpLifetimeTest::deleteFromEverySignal_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("victim");
    QTest::addColumn<int>("part");

    const std::pair<McpLifetimeVictim, const char*> victims[] = { { McpLifetimeVictim::Tab, "tab" }, { McpLifetimeVictim::Window, "window" },
                                                                 { McpLifetimeVictim::Page, "page" }, { McpLifetimeVictim::Controller, "controller" } };
    for (const QString& name : getScenarios())
    {
        // The sites of a scenario are divided into parts (every PART_COUNT-th site),
        // so a row ends within the time limit of QtTest also under the sanitizers
        for (const auto& [victim, victimName] : victims)
        {
            if (victim == McpLifetimeVictim::Page && name != QStringLiteral("ai"))
            {
                // The AI page exists only in its scenario
                continue;
            }
            for (int part = 0; part < PART_COUNT; ++part)
            {
                QTest::newRow(QStringLiteral("%1-%2-%3").arg(name, QLatin1String(victimName)).arg(part).toLatin1().constData()) << name << int(victim) << part;
            }
        }
    }
}

void AppMcpLifetimeTest::runScenario(const QString& name, McpLifetimeRecorder::Mode mode, const McpLifetimeRecorder::Site& target, McpLifetimeVictim victim,
                                     std::vector<McpLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred)
{
    McpLifetimeFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(fixture.host() && fixture.ui());

    McpLifetimeRun run;
    run.fixture = &fixture;
    run.window = fixture.window;
    run.tab = fixture.window->getHomeTab();
    run.homeTab = fixture.window->getHomeTab();
    const std::vector<McpLifetimeStep> steps = McpLifetimeHelper::scenario(name, run);
    QVERIFY2(!steps.empty(), qPrintable(name));
    QVERIFY(run.tab);
    fixture.window->activateTab(run.tab.data());
    pdftest::AppDocumentFixture::settle();

    McpLifetimeRecorder recorder(mode);
    if (mode == McpLifetimeRecorder::Mode::Replay)
    {
        recorder.setTarget(target, [&run, &fixture, victim](const McpLifetimeRecorder::Site& site)
        {
            QObject* object = nullptr;

            // A victim being destroyed already (a signal of its destructor) is not
            // deleted again: qobject_cast fails once the derived destructor has run
            switch (victim)
            {
                case McpLifetimeVictim::Window:
                    object = qobject_cast<pdf::PDFAppMainWindow*>(run.window.data());
                    break;

                case McpLifetimeVictim::Tab:
                    object = qobject_cast<pdf::PDFAppTab*>(run.tab.data());
                    break;

                case McpLifetimeVictim::Page:
                    object = McpLifetimeHelper::victimPage(run);
                    break;

                case McpLifetimeVictim::Controller:
                    object = qobject_cast<pdf::PDFAppMcpController*>(fixture.controller());
                    break;
            }

            if (!object)
            {
                return McpLifetimeRecorder::Action::None;
            }
            if (McpLifetimeRecorder::isDeferredSite(site) || McpLifetimeRecorder::isDeferredState(site, object))
            {
                object->deleteLater();
                return McpLifetimeRecorder::Action::Deferred;
            }
            delete object;
            return McpLifetimeRecorder::Action::Deleted;
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
        if (!recorder.isFired() && fixture.host() && fixture.host()->getActivityLog())
        {
            // The activity of the step is written in the step (the log writes it by a
            // timer of 250 ms otherwise, at a moment, which differs between the runs)
            fixture.host()->getActivityLog()->flush();
            pdftest::AppDocumentFixture::settle();
        }
        if (recorder.isFired())
        {
            break;
        }
        if (mode == McpLifetimeRecorder::Mode::Discover)
        {
            QVERIFY2(run.window && run.tab, qPrintable(QStringLiteral("%1: the window was lost in step %2").arg(name).arg(i)));
            QVERIFY2(isOk, qPrintable(QStringLiteral("%1: step %2 failed").arg(name).arg(i)));
        }
        else if (!run.window || !run.tab)
        {
            break;
        }
    }

    recorder.setStep(int(steps.size()));
    pdftest::AppDocumentFixture::settle();
    if (mode == McpLifetimeRecorder::Mode::Replay && recorder.isFired())
    {
        // Every call of the assistant ends (denied, canceled or done) - nothing waits
        // for a deleted dialog or tab. A direct call of the fixture belongs to no
        // server: when the controller (the server and the host) is deleted, the stop
        // of the server cancels only its own calls (the HTTP calls of the scenarios
        // end by the closed connection), so the direct calls are not waited for then
        for (const std::shared_ptr<pdf::PDFMcpCall>& call : victim == McpLifetimeVictim::Controller ? std::vector<std::shared_ptr<pdf::PDFMcpCall>>() : run.calls)
        {
            const QString callText = call->getRequest().name + QLatin1Char(' ') + QString::fromUtf8(QJsonDocument(call->getRequest().arguments).toJson(QJsonDocument::Compact));
            QVERIFY2(QTest::qWaitFor([&call]() { return call->isFinished(); }, McpLifetimeHelper::timeout(30000)), qPrintable(QStringLiteral("a call of the assistant does not end: ") + callText));
        }
        run.client.reset();
        run.streams.clear();
        pdftest::AppDocumentFixture::settle();

        switch (victim)
        {
            case McpLifetimeVictim::Window:
                QVERIFY(!run.window);
                break;

            case McpLifetimeVictim::Tab:
            case McpLifetimeVictim::Page:
            case McpLifetimeVictim::Controller:
            {
                // The window works: a document opens, the AI page shows the state
                QVERIFY(run.window);
                QVERIFY(victim != McpLifetimeVictim::Tab || !run.tab);
                QVERIFY(victim != McpLifetimeVictim::Controller || !fixture.controller());
                pdf::PDFAppDocumentTab* tab = McpLifetimeHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("after.pdf"), 1));
                QVERIFY(tab);
                if (fixture.host())
                {
                    run.tab = tab;
                    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
                    QVERIFY(McpLifetimeHelper::call(run, QStringLiteral("document"), McpLifetimeHelper::json(R"({"action":"get_info"})")));
                }
                run.window->activateTab(run.window->getHomeTab());
                run.tab = run.window->getHomeTab();
                QVERIFY(McpLifetimeHelper::navigate(run, QStringLiteral("/ai")));
                QVERIFY(McpLifetimeHelper::aiPage(run));
                break;
            }
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

void AppMcpLifetimeTest::deleteFromEverySignal()
{
    QFETCH(QString, scenario);
    QFETCH(int, victim);
    QFETCH(int, part);

    std::vector<McpLifetimeRecorder::Site> sites;
    runScenario(scenario, McpLifetimeRecorder::Mode::Discover, McpLifetimeRecorder::Site(), McpLifetimeVictim(victim), &sites, nullptr);
    if (QTest::currentTestFailed())
    {
        return;
    }
    QVERIFY(!sites.empty());

    // PDF4QT_LIFETIME_SITE=<n> replays one site only (debugging)
    const int onlySite = qEnvironmentVariableIsSet("PDF4QT_LIFETIME_SITE") ? qEnvironmentVariableIntValue("PDF4QT_LIFETIME_SITE") : -1;
    QStringList notReproduced;
    int replayed = 0;
    int deferred = 0;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        if (onlySite >= 0 ? int(i) != onlySite : int(i) % PART_COUNT != part)
        {
            continue;
        }
        if (qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE"))
        {
            qInfo().noquote() << QStringLiteral("site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
        }

        bool isFired = false;
        bool isDeferred = false;
        runScenario(scenario, McpLifetimeRecorder::Mode::Replay, sites[i], McpLifetimeVictim(victim), nullptr, &isFired, &isDeferred);
        if (QTest::currentTestFailed())
        {
            qWarning().noquote() << QStringLiteral("failed at site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
            return;
        }
        if (isFired)
        {
            ++replayed;
            if (isDeferred)
            {
                ++deferred;
                if (qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE"))
                {
                    qInfo().noquote() << QStringLiteral("deferred: site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
                }
            }
        }
        else
        {
            notReproduced << QStringLiteral("step %1: %2").arg(sites[i].step).arg(sites[i].key);
        }
    }

    qInfo().noquote() << QStringLiteral("%1: %2 sites, %3 deletions (%4 deferred) in part %5").arg(scenario).arg(sites.size()).arg(replayed).arg(deferred).arg(part);
    for (const QString& site : notReproduced)
    {
        qInfo().noquote() << QStringLiteral("not reproduced (the emission differs in the run, or the victim does not exist): ") + site;
    }

    // At least 80 % of the sites are reproduced (a site of a timer, of a thread or of
    // the order of the events may move to another step)
    QVERIFY2(onlySite >= 0 || replayed * 10 >= (replayed + int(notReproduced.size())) * 8, qPrintable(notReproduced.join(QLatin1Char('\n'))));
}

int main(int argc, char** argv)
{
    // A child process of the fixture (another instance)
    const int childResult = pdftest::AppMcpFixture::runChild(argc, argv);
    if (childResult >= 0)
    {
        return childResult;
    }

    // QTEST_MAIN with the application of the test (it knows the delivered events)
    McpLifetimeApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppMcpLifetimeTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_appmcplifetimetest.moc"
