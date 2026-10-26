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

// Generic lifetime test of the Tools catalog (step 6, review RT6): the scenarios of
// the user (the Tools page - search, chips, pins by the star and by the menu, Add to
// collection, the confirmation of Clear, a collection filter; the collections editor
// - create, rename, icon, drag and drop reorder, add a tool, save, delete with its
// confirmation, another instance changing the edited collection; the Tools panel of
// a document - open, search, run a command, open a tool, the lock while it is open)
// are run once with a recorder of every signal of every object of the application
// (classes pdf::PDFApp* and pdf::PDFDatabase*), then once more for each signal it has
// seen (the first emission of the signal of an object in each step), with the tab, the
// window, the page (the Tools panel of the document scenario) or the home tab deleted
// synchronously from that emission. Nothing may crash (run it under ASan / valgrind -
// freed memory stays readable in a Release build on Windows), the window survives the
// deletion of a tab and of the page. A second test opens every menu, dialog, info bar
// and panel of the step and installs a translator or lets another instance change the
// data while it is open.

#include "apptoolsfixture.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumenttoolspanel.h"
#include "pdfappdocumentview.h"
#include "pdfapphomedialogs.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfappsearchwidgets.h"
#include "pdfapptab.h"
#include "pdfappcollectioniconchooser.h"
#include "pdfappcollectiontoollist.h"
#include "pdfapptoolcategorycard.h"
#include "pdfapptoolcollectioneditor.h"
#include "pdfapptoolcollectionspage.h"
#include "pdfapptoolmenu.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolspage.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdftoolcollectionrepository.h"
#include "pdftoolrepository.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QLineEdit>
#include <QMetaMethod>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QTranslator>
#include <QtTest>

#include <functional>
#include <memory>
#include <vector>

class ToolsLifetimeRecorder;

/// Application of the test: it knows the events being delivered (QApplication::notify),
/// so the recorder can tell an emission inside of the key handling of a QLineEdit
class ToolsLifetimeApplication : public QApplication
{
public:
    ToolsLifetimeApplication(int& argc, char** argv) :
        QApplication(argc, argv)
    {
    }

    virtual bool notify(QObject* receiver, QEvent* event) override
    {
        // A line edit handles a key: its signals textEdited / textChanged / returnPressed
        // are emitted inside of this event
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

std::vector<QPointer<QLineEdit>> ToolsLifetimeApplication::s_keyedLineEdits;

/// One connection of the recorder to one signal of one object: the slot is the
/// first method after the methods of QObject (the pattern of QSignalSpy)
class ToolsLifetimeHook : public QObject
{
public:
    explicit ToolsLifetimeHook(ToolsLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature);

    virtual int qt_metacall(QMetaObject::Call call, int id, void** arguments) override;

private:
    ToolsLifetimeRecorder* m_recorder;
    QPointer<QObject> m_sender;
    QByteArray m_signature;
};

/// Recorder of the signals of the objects of the application. In the discovery
/// mode it collects the sites (signal of an object, step of the scenario), in the
/// replay mode it deletes the victim at the first emission of the target site.
class ToolsLifetimeRecorder : public QObject
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

    explicit ToolsLifetimeRecorder(Mode mode) :
        m_mode(mode)
    {
        qApp->installEventFilter(this);
    }

    virtual ~ToolsLifetimeRecorder() override
    {
        qApp->removeEventFilter(this);
    }

    /// Result of the action at the target site
    enum class Action
    {
        None,       ///< No victim (it is already gone or it does not exist)
        Deleted,    ///< Deleted synchronously
        Deferred    ///< Deleted by deleteLater (a deferred site or state)
    };

    /// The action deletes the victim (synchronously, or later for a deferred site or a
    /// deferred state of the emission, which the action checks)
    void setTarget(const Site& site, std::function<Action(const Site&)> action)
    {
        m_target = site;
        m_action = std::move(action);
    }

    /// Sites, where the victim is deleted by deleteLater instead of synchronously,
    /// each with the reason why no code can survive it:
    ///  - a signal of a destructor (PDFAppPage::aboutToBeDestroyed): the dying object is
    ///    still a child of its parents, a deletion of an ancestor deletes it twice (Qt);
    static bool isDeferredSite(const Site& site)
    {
        return site.key.endsWith(QStringLiteral("::aboutToBeDestroyed()"));
    }

    /// The state of the emission defers the deletion of the victim (counted with the site):
    ///  - an emission inside of the key handling of a QLineEdit, which the deletion of
    ///    the victim deletes (the name or the search of the collections editor, the
    ///    search of the Tools page and of the panel): Qt does not survive the deletion
    ///    of the line edit inside of its own textEdited / textChanged
    ///    (QLineEditPrivate::textEdited reads the completer of the deleted line edit after
    ///    the emission - QWidgetLineControl::finishChange)
    /// (PDFAppDocumentView::viewChanged of a command of the Tools panel - Zoom In - is no
    /// deferred state since the fix wave of step 6: PDFDrawWidgetProxy emits
    /// drawSpaceChanged after its PDFBoolGuard, the view is deleted synchronously.)
    static bool isDeferredState(const Site& site, const QObject* victim)
    {
        Q_UNUSED(site);
        return ToolsLifetimeApplication::isLineEditKeyInside(victim);
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
        if (!object || m_isStopped)
        {
            return;
        }

        auto it = m_hooked.find(object);
        if (it != m_hooked.end() && it.value())
        {
            return;
        }

        // Signals declared by the classes of the application only (the signals of
        // Qt and of the library are not emitted by the application itself)
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

            ToolsLifetimeHook* hook = new ToolsLifetimeHook(this, object, method.methodSignature());
            if (!QMetaObject::connect(object, i, hook, QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr))
            {
                delete hook;
            }
        }
    }

    void onSignal(QObject* sender, const QByteArray& signature)
    {
        if (m_isStopped || !sender)
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
        // An object receiving an event is constructed: hook it (menus, dialogs,
        // panels and rows created in the middle of a step)
        if (event->type() == QEvent::Paint && watched->isWidgetType())
        {
            m_paintedWidget = static_cast<QWidget*>(watched);
        }
        if (!m_isStopped && watched && !m_hooked.contains(watched))
        {
            hookObject(watched);
        }
        return false;
    }

    static bool isApplicationClass(const QMetaObject* metaObject)
    {
        if (!metaObject)
        {
            return false;
        }
        const QByteArray className(metaObject->className());
        return className.startsWith("pdf::PDFApp") || className.startsWith("pdf::PDFDatabase");
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

ToolsLifetimeHook::ToolsLifetimeHook(ToolsLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature) :
    QObject(recorder),
    m_recorder(recorder),
    m_sender(sender),
    m_signature(signature)
{
}

int ToolsLifetimeHook::qt_metacall(QMetaObject::Call call, int id, void** arguments)
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
class ToolsLifetimeTranslator : public QTranslator
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

/// State of one run of a scenario
struct ToolsLifetimeRun
{
    pdftest::AppToolsFixture* fixture = nullptr;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppTab> tab;              ///< Tab of the scenario (home tab, the document tab)
    QPointer<pdf::PDFAppTab> homeTab;
    qint64 customId = 0;                       ///< Custom collection of the scenario
};

using ToolsLifetimeStep = std::function<bool(ToolsLifetimeRun&)>;

/// Victim of the deletion
enum class ToolsLifetimeVictim
{
    Tab,        ///< The tab of the scenario
    Window,
    Page,       ///< Tools page, collections page, Tools panel of the document
    HomeTab     ///< The home tab of the document scenario (the tab of All tools)
};

/// Helpers of the scenarios: every input is sent through guards, the victim may be
/// deleted inside of any event
class ToolsLifetimeHelper
{
public:
    ToolsLifetimeHelper() = delete;

    using Panel = pdf::PDFAppDocumentToolbar::Panel;

    static void settle() { pdftest::AppDocumentFixture::settle(); }

    static pdf::PDFAppPage* currentPage(const ToolsLifetimeRun& run)
    {
        return run.tab ? run.tab->getPage() : nullptr;
    }

    static pdf::PDFAppToolsPage* toolsPage(const ToolsLifetimeRun& run)
    {
        return qobject_cast<pdf::PDFAppToolsPage*>(currentPage(run));
    }

    static pdf::PDFAppToolCollectionsPage* collectionsPage(const ToolsLifetimeRun& run)
    {
        return qobject_cast<pdf::PDFAppToolCollectionsPage*>(currentPage(run));
    }

    static pdf::PDFAppToolCollectionEditor* editor(const ToolsLifetimeRun& run)
    {
        pdf::PDFAppToolCollectionsPage* page = collectionsPage(run);
        return page ? page->getEditor() : nullptr;
    }

    static pdf::PDFAppDocumentTab* documentTab(const ToolsLifetimeRun& run)
    {
        return qobject_cast<pdf::PDFAppDocumentTab*>(run.tab.data());
    }

    static pdf::PDFAppDocumentToolbar* toolbar(const ToolsLifetimeRun& run)
    {
        pdf::PDFAppDocumentTab* tab = documentTab(run);
        return tab && tab->getView() ? tab->getView()->getToolbar() : nullptr;
    }

    /// The open Tools panel of the document (nullptr = closed or none)
    static pdf::PDFAppDocumentToolsPanel* panel(const ToolsLifetimeRun& run)
    {
        pdf::PDFAppDocumentToolbar* bar = toolbar(run);
        pdf::PDFAppDocumentToolsPanel* toolsPanel = bar ? qobject_cast<pdf::PDFAppDocumentToolsPanel*>(bar->findPanel(Panel::Tools)) : nullptr;
        return toolsPanel && toolsPanel->isVisible() ? toolsPanel : nullptr;
    }

    /// The page of the scenario for the victim Page
    static QWidget* victimPage(const ToolsLifetimeRun& run)
    {
        if (documentTab(run))
        {
            pdf::PDFAppDocumentToolbar* bar = toolbar(run);
            return bar ? bar->findPanel(Panel::Tools) : nullptr;
        }
        pdf::PDFAppPage* page = currentPage(run);
        if (qobject_cast<pdf::PDFAppToolsPage*>(page) || qobject_cast<pdf::PDFAppToolCollectionsPage*>(page))
        {
            return page;
        }
        return nullptr;
    }

    static bool navigate(ToolsLifetimeRun& run, const QString& route)
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
    static bool click(QWidget* widget, QPoint position = QPoint())
    {
        if (!widget || !widget->isEnabled() || !widget->isVisible())
        {
            return false;
        }
        QPointer<QWidget> guard(widget);
        const QPoint point = position.isNull() ? widget->rect().center() : position;
        QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, point);
        if (guard)
        {
            QTest::mouseRelease(guard.data(), Qt::LeftButton, Qt::NoModifier, point);
        }
        settle();
        return true;
    }

    /// A key (press and release, the release only to a living widget)
    static bool key(QWidget* widget, int keyCode, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        if (!widget)
        {
            return false;
        }
        QPointer<QWidget> guard(widget);
        QTest::keyPress(widget, Qt::Key(keyCode), modifiers);
        if (guard)
        {
            QTest::keyRelease(guard.data(), Qt::Key(keyCode), modifiers);
        }
        return true;
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

    /// Replaces the text of the line edit as the user does it (select all, type)
    static bool replaceText(QLineEdit* field, const QString& text)
    {
        QPointer<QLineEdit> guard(field);
        if (!field)
        {
            return false;
        }
        field->setFocus(Qt::OtherFocusReason);
        field->selectAll();
        key(field, Qt::Key_Backspace);
        return guard && type(guard.data(), text);
    }

    /// Triggers the action of a menu (as a click of its item)
    static bool trigger(QAction* action)
    {
        if (!action || !action->isEnabled())
        {
            return false;
        }
        action->trigger();
        settle();
        return true;
    }

    /// Waits for the condition (the victim may go meanwhile)
    static bool waitFor(const std::function<bool()>& condition)
    {
        const bool result = QTest::qWaitFor(condition, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        settle();
        return result;
    }

    /// Waits for the delayed search of the Tools page
    static bool waitForSearch(ToolsLifetimeRun& run, const QString& text)
    {
        return waitFor([&run, text]() { pdf::PDFAppToolsPage* page = toolsPage(run); return !page || page->getSearchText() == text; });
    }

    /// Opens the context menu of the tool on the Tools page
    static pdf::PDFAppToolMenu* toolMenu(ToolsLifetimeRun& run, const QString& toolId)
    {
        pdf::PDFAppToolsPage* page = toolsPage(run);
        pdf::PDFToolRow* row = page ? page->findToolRow(toolId) : nullptr;
        if (!row)
        {
            return nullptr;
        }
        QPointer<pdf::PDFAppToolMenu> menu = page->showToolMenu(toolId, row->mapToGlobal(row->rect().center()));
        settle();
        return menu.data();
    }

    /// Opens the Tools panel of the document
    static pdf::PDFAppDocumentToolsPanel* openPanel(ToolsLifetimeRun& run)
    {
        pdf::PDFAppDocumentToolbar* bar = toolbar(run);
        if (!bar)
        {
            return nullptr;
        }
        QPointer<pdf::PDFAppDocumentToolsPanel> toolsPanel = qobject_cast<pdf::PDFAppDocumentToolsPanel*>(bar->openPanel(Panel::Tools));
        settle();
        return toolsPanel.data();
    }

    /// Drags the tool of the edited collection onto the row of another tool
    /// (the events of a drag of the list; QDrag::exec returns at once offscreen)
    static bool dropTool(ToolsLifetimeRun& run, const QString& toolId, const QString& targetToolId)
    {
        pdf::PDFAppToolCollectionEditor* collectionEditor = editor(run);
        QPointer<pdf::PDFAppCollectionToolList> list = collectionEditor ? collectionEditor->getToolList() : nullptr;
        QWidget* target = list ? list->findRow(targetToolId) : nullptr;
        if (!target)
        {
            return false;
        }
        std::unique_ptr<QMimeData> mimeData(list->createMimeData(toolId));
        const QPoint point = target->geometry().topLeft() + QPoint(20, 4);
        QDragEnterEvent enter(point, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list.data(), &enter);
        if (list)
        {
            QDragMoveEvent move(point, Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list.data(), &move);
        }
        if (list)
        {
            QDropEvent drop(QPointF(point), Qt::MoveAction, mimeData.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list.data(), &drop);
        }
        settle();
        return true;
    }

    /// Back to the document page of the document tab (a tool opened by the panel
    /// is a page of the document tab or of the home tab)
    static bool backToDocument(ToolsLifetimeRun& run)
    {
        pdf::PDFAppTab* tab = run.tab.data();
        if (!tab || !run.window)
        {
            return false;
        }
        run.window->activateTab(tab);
        settle();
        const pdf::PDFAppHistoryEntry* entry = run.tab ? run.tab->getHistory().getCurrentEntry() : nullptr;
        if (run.window && run.tab && entry && entry->route.toString().contains(QStringLiteral("/tool/")))
        {
            run.window->getNavigator()->goBack(run.tab.data());
            settle();
        }
        return run.window && run.tab && toolbar(run) && toolbar(run)->isVisible();
    }

    /// Writes a pin by another connection (another instance)
    static bool pinOther(ToolsLifetimeRun& run, const QString& toolId, bool isPinned)
    {
        return run.fixture->pinFromOtherConnection(toolId, isPinned);
    }

    /// Saves the custom collection of the scenario by another connection
    static bool saveOther(ToolsLifetimeRun& run, const QString& name, const QStringList& toolIds)
    {
        pdf::PDFToolCollectionDraft draft;
        draft.id = run.customId;
        draft.name = name;
        draft.toolIds = toolIds;
        return run.fixture->writeCollectionFromOtherConnection(draft);
    }

    /// Deletes the custom collection of the scenario by another connection
    static bool deleteOther(ToolsLifetimeRun& run)
    {
        const qint64 id = run.customId;
        return run.fixture->writeFromOtherConnection([id](pdf::PDFDatabase* database) { return pdf::PDFToolCollectionRepository(database).deleteCollection(id); },
                                                     pdf::PDFToolCollectionRepository::getCollectionTopic());
    }

    /// The steps of a scenario; the setup (rows of the database, documents, the test
    /// pages of the tools) is done here before the recording
    static std::vector<ToolsLifetimeStep> scenario(const QString& name, ToolsLifetimeRun& run);
};

std::vector<ToolsLifetimeStep> ToolsLifetimeHelper::scenario(const QString& name, ToolsLifetimeRun& run)
{
    using H = ToolsLifetimeHelper;
    pdftest::AppToolsFixture& fixture = *run.fixture;
    std::vector<ToolsLifetimeStep> steps;

    if (name == QStringLiteral("catalog"))
    {
        // Recent tools (Clear), a custom collection (side column, the filter)
        pdf::PDFToolRepository tools = fixture.tools();
        tools.recordUsage(QStringLiteral("pages.merge"));
        tools.recordUsage(QStringLiteral("optimize.compress"));
        fixture.collections().createCollection(QStringLiteral("Invoices"), QStringLiteral("star"), { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }, &run.customId);
        fixture.registerTestToolPage(QStringLiteral("pages.split"));

        steps.push_back([](ToolsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/tools")) && H::toolsPage(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // The search typed (the page created by the Push of the filter)
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && H::type(page->getSearchField(), QStringLiteral("image")) && H::waitForSearch(run, QStringLiteral("image"));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && H::click(page->getChip(QStringLiteral("convert")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && page->getShowAllLink() && H::click(page->getShowAllLink());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && H::replaceText(page->getSearchField(), QStringLiteral("xyzzy")) && H::waitForSearch(run, QStringLiteral("xyzzy"));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && H::click(page->getClearSearchButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Pin by the star of a row
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            pdf::PDFToolRow* row = page ? page->findToolRow(QStringLiteral("pages.split")) : nullptr;
            return row && H::click(row->getStarButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Unpin by the star of the pinned tile
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            pdf::PDFQuickActionTile* tile = page ? page->findTile(QStringLiteral("pages.split")) : nullptr;
            return tile && H::click(tile->getStarButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Pin by the context menu
            pdf::PDFAppToolMenu* menu = H::toolMenu(run, QStringLiteral("pages.rotate"));
            return menu && H::trigger(menu->getPinAction());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Add to collection ▸ a built-in collection (its override is created)
            pdf::PDFAppToolMenu* menu = H::toolMenu(run, QStringLiteral("pages.merge"));
            return menu && H::trigger(menu->findCollectionAction(QStringLiteral("fix-a-scan")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // … and removed from the custom collection
            pdf::PDFAppToolMenu* menu = H::toolMenu(run, QStringLiteral("pages.merge"));
            return menu && H::trigger(menu->findCollectionAction(QStringLiteral("custom-%1").arg(run.customId)));
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::pinOther(run, QStringLiteral("pages.merge"), true); });
        steps.push_back([](ToolsLifetimeRun& run) { return run.fixture->recordUsageFromOtherConnection(QStringLiteral("pages.rotate"), QDateTime::currentMSecsSinceEpoch()); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && H::click(page->getClearRecentLink()) && (!H::toolsPage(run) || H::toolsPage(run)->getClearRecentDialog());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            pdf::PDFAppClearRecentToolsDialog* dialog = page ? page->getClearRecentDialog() : nullptr;
            return dialog && H::click(dialog->getClearButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Everything changed (an import of a backup by another instance)
            Q_EMIT run.fixture->context->getNotifier()->everythingChanged();
            H::settle();
            return true;
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A collection of the side column: its filter (owner decision Q1)
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            for (pdf::PDFCollectionRow* row : page ? page->getCollectionRows() : QList<pdf::PDFCollectionRow*>())
            {
                if (row->objectName() == QStringLiteral("collection-custom-%1").arg(run.customId))
                {
                    return H::click(row) && H::toolsPage(run) && !H::toolsPage(run)->getCollection().isEmpty();
                }
            }
            return false;
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::saveOther(run, QStringLiteral("Invoices 2"), { QStringLiteral("pages.split"), QStringLiteral("pages.crop") }); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A tool row of the collection opens the page of the tool (a test page)
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            pdf::PDFToolRow* row = page ? page->findToolRow(QStringLiteral("pages.split")) : nullptr;
            return row && H::click(row);
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/tools?collection=custom-%1").arg(run.customId)); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Edit of the collection: the collections page
            pdf::PDFAppToolsPage* page = H::toolsPage(run);
            return page && page->getEditCollectionLink() && H::click(page->getEditCollectionLink());
        });
    }
    else if (name == QStringLiteral("collections"))
    {
        fixture.collections().createCollection(QStringLiteral("Invoices"), QString(), { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress"), QStringLiteral("pages.split") }, &run.customId);

        steps.push_back([](ToolsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/tools/collections")) && H::editor(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionsPage* page = H::collectionsPage(run);
            return page && H::click(page->findListRow(QStringLiteral("custom-%1").arg(run.customId)));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::replaceText(collectionEditor->getNameField(), QStringLiteral("Invoices 2"));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getIconChooser()->getChoice(QStringLiteral("zap")));
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::dropTool(run, QStringLiteral("pages.split"), QStringLiteral("pages.merge")); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Reorder by the keyboard (Alt+Down on the handle)
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            pdf::PDFIconButton* handle = collectionEditor ? collectionEditor->getToolList()->findHandle(QStringLiteral("pages.split")) : nullptr;
            if (!handle)
            {
                return false;
            }
            handle->setFocus(Qt::TabFocusReason);
            H::key(handle, Qt::Key_Down, Qt::AltModifier);
            H::settle();
            return true;
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Add a tool found by the search
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            if (!collectionEditor || !H::type(collectionEditor->getSearchField(), QStringLiteral("crop")))
            {
                return false;
            }
            H::settle();
            collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->findAddButton(QStringLiteral("pages.crop")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getToolList()->findRemoveButton(QStringLiteral("optimize.compress")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getSaveButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Another instance changes the collection without a draft: the editor follows
            return H::saveOther(run, QStringLiteral("Invoices 3"), { QStringLiteral("pages.merge"), QStringLiteral("pages.crop") });
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::replaceText(collectionEditor->getNameField(), QStringLiteral("Mine"));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // … and with a draft: the info bar (Reload / Keep my changes)
            return H::saveOther(run, QStringLiteral("Invoices 4"), { QStringLiteral("pages.merge") });
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            pdf::PDFAppInfoBar* bar = collectionEditor ? collectionEditor->getInfoBar() : nullptr;
            return bar && !bar->getActionButtons().empty() && H::click(bar->getActionButtons().front());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Reset of a built-in collection, Cancel
            return H::navigate(run, QStringLiteral("/tools/collections/fix-a-scan")) && H::editor(run);
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getToolList()->findRemoveButton(QStringLiteral("pages.crop")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getCancelButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getToolList()->findRemoveButton(QStringLiteral("pages.crop"))) &&
                   H::editor(run) && H::click(H::editor(run)->getSaveButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getResetLink()) && H::editor(run) && H::click(H::editor(run)->getSaveButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A new collection: the name, a suggested tool, Save
            pdf::PDFAppToolCollectionsPage* page = H::collectionsPage(run);
            return page && H::click(page->getNewButton()) && H::editor(run) && H::editor(run)->isNewCollection();
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            if (!collectionEditor || !H::replaceText(collectionEditor->getNameField(), QStringLiteral("Weekly")) ||
                !H::editor(run) || !H::type(H::editor(run)->getSearchField(), QStringLiteral("merge")))
            {
                return false;
            }
            H::settle();
            collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->findAddButton(QStringLiteral("pages.merge")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getSaveButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Delete of the custom collection of the scenario: the confirmation
            return H::navigate(run, QStringLiteral("/tools/collections/custom-%1").arg(run.customId)) && H::editor(run) &&
                   H::click(H::editor(run)->getDeleteButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionsPage* page = H::collectionsPage(run);
            pdf::PDFAppCardDialog* dialog = page ? page->getDeleteDialog() : nullptr;
            return dialog && H::key(dialog, Qt::Key_Escape);
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            if (!collectionEditor || !H::click(collectionEditor->getDeleteButton()))
            {
                return false;
            }
            pdf::PDFAppToolCollectionsPage* page = H::collectionsPage(run);
            pdf::PDFAppCardDialog* dialog = page ? page->getDeleteDialog() : nullptr;
            return dialog && H::click(dialog->findChild<QPushButton*>(QStringLiteral("deleteButton")));
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // The other custom collection with a draft, deleted by another instance
            pdf::PDFAppToolCollectionsPage* page = H::collectionsPage(run);
            if (!page)
            {
                return false;
            }
            const std::vector<pdf::PDFToolCollectionRow> rows = run.fixture->collections().getCollections();
            for (const pdf::PDFToolCollectionRow& row : rows)
            {
                if (!row.isBuiltin())
                {
                    run.customId = row.id;
                }
            }
            return H::navigate(run, QStringLiteral("/tools/collections/custom-%1").arg(run.customId)) && H::editor(run) &&
                   H::replaceText(H::editor(run)->getNameField(), QStringLiteral("Weekly 2"));
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::deleteOther(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Saved again: a new collection of the draft
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            return collectionEditor && H::click(collectionEditor->getSaveButton());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            Q_EMIT run.fixture->context->getNotifier()->everythingChanged();
            H::settle();
            return true;
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A tool row of the editor opens the tool (Page Not Found)
            pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
            pdf::PDFToolRow* row = collectionEditor && !collectionEditor->getToolList()->getTools().isEmpty()
                                 ? collectionEditor->getToolList()->findToolRow(collectionEditor->getToolList()->getTools().front()) : nullptr;
            return row && H::click(row);
        });
    }
    else if (name == QStringLiteral("panel"))
    {
        pdf::PDFToolRepository tools = fixture.tools();
        for (const QString& toolId : { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress"), QStringLiteral("protect.sign") })
        {
            tools.setPinned(toolId, true);
        }
        tools.recordUsage(QStringLiteral("pages.rotate"));
        tools.recordUsage(QStringLiteral("convert.images-to-pdf"));
        fixture.registerTestToolPage(QStringLiteral("optimize.compress"), pdf::PDFAppToolPageKind::Document);

        QPointer<pdf::PDFAppDocumentTab> tab = fixture.openDocument();
        const bool isReady = tab && QTest::qWaitFor([&tab]()
        {
            return tab && tab->getDocument() && tab->getDocument()->isReady() && tab->getView() && tab->getView()->getPageCount() > 0;
        }, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        if (!isReady)
        {
            return steps;
        }
        run.tab = tab.data();

        steps.push_back([](ToolsLifetimeRun& run) { return H::openPanel(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A command found by the search runs on the view
            pdf::PDFAppDocumentToolsPanel* toolsPanel = H::panel(run);
            if (!toolsPanel || !H::type(toolsPanel->getSearchField(), QStringLiteral("zoom in")))
            {
                return false;
            }
            H::settle();
            toolsPanel = H::panel(run);
            pdf::PDFAppSearchResultRow* row = toolsPanel ? toolsPanel->findChild<pdf::PDFAppSearchResultRow*>(QStringLiteral("result:") + pdf::PDFAppCommands::zoomIn()) : nullptr;
            return row && H::click(row);
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A tool found by the search, Enter
            pdf::PDFAppDocumentToolsPanel* toolsPanel = H::openPanel(run);
            if (!toolsPanel || !H::type(toolsPanel->getSearchField(), QStringLiteral("merge")))
            {
                return false;
            }
            toolsPanel = H::panel(run);
            return toolsPanel && H::key(toolsPanel->getSearchField(), Qt::Key_Return);
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::backToDocument(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A pinned shortcut opens the page of the tool (a test page)
            pdf::PDFAppDocumentToolsPanel* toolsPanel = H::openPanel(run);
            for (pdf::PDFToolShortcut* shortcut : toolsPanel ? toolsPanel->getPinnedShortcuts() : std::vector<pdf::PDFToolShortcut*>())
            {
                if (shortcut->objectName() == pdf::PDFAppToolPresentation::getObjectName(QStringLiteral("optimize.compress")))
                {
                    return H::click(shortcut);
                }
            }
            return false;
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::backToDocument(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Changes of another instance while the panel is open
            return H::openPanel(run) && H::pinOther(run, QStringLiteral("review.compare"), true) &&
                   run.fixture->recordUsageFromOtherConnection(QStringLiteral("pages.split"), QDateTime::currentMSecsSinceEpoch());
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            Q_EMIT run.fixture->context->getNotifier()->everythingChanged();
            H::settle();
            return true;
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // The document locked while the panel is open, then unlocked
            pdf::PDFAppDocumentTab* tab = H::documentTab(run);
            if (!tab || !tab->getDocument())
            {
                return false;
            }
            QPointer<pdf::PDFAppDocument> document = tab->getDocument();
            document->setLocked(true);
            H::settle();
            if (document)
            {
                document->setLocked(false);
            }
            H::settle();
            return true;
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // A recent tool of the panel
            pdf::PDFAppDocumentToolsPanel* toolsPanel = H::panel(run);
            if (!toolsPanel)
            {
                toolsPanel = H::openPanel(run);
            }
            const std::vector<pdf::PDFRecentToolRow*> rows = toolsPanel ? toolsPanel->getRecentRows() : std::vector<pdf::PDFRecentToolRow*>();
            return !rows.empty() && H::click(rows.front());
        });
        steps.push_back([](ToolsLifetimeRun& run) { return H::backToDocument(run); });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // Esc clears the search, then closes the panel
            pdf::PDFAppDocumentToolsPanel* toolsPanel = H::openPanel(run);
            if (!toolsPanel || !H::type(toolsPanel->getSearchField(), QStringLiteral("print")))
            {
                return false;
            }
            for (int i = 0; i < 2 && H::panel(run); ++i)
            {
                H::key(H::panel(run)->getSearchField(), Qt::Key_Escape);
                H::settle();
            }
            return true;
        });
        steps.push_back([](ToolsLifetimeRun& run)
        {
            // All tools: the catalog in the home tab
            pdf::PDFAppDocumentToolsPanel* toolsPanel = H::openPanel(run);
            if (!toolsPanel || !H::type(toolsPanel->getSearchField(), QStringLiteral("sign")))
            {
                return false;
            }
            toolsPanel = H::panel(run);
            return toolsPanel && H::click(toolsPanel->getAllToolsRow());
        });
    }

    return steps;
}

class AppToolsLifetimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void deleteFromEverySignal_data();
    void deleteFromEverySignal();
    void openPopupsSurviveChanges_data();
    void openPopupsSurviveChanges();

private:
    static constexpr int PART_COUNT = 3;

    static QStringList getScenarios() { return { QStringLiteral("catalog"), QStringLiteral("collections"), QStringLiteral("panel") }; }

    /// Runs the scenario. In the replay mode the victim is deleted at the target
    /// site. \returns the sites seen (discovery) and whether the victim was deleted.
    void runScenario(const QString& name, ToolsLifetimeRecorder::Mode mode, const ToolsLifetimeRecorder::Site& target, ToolsLifetimeVictim victim,
                     std::vector<ToolsLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred = nullptr);
};

void AppToolsLifetimeTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolsLifetimeTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolsLifetimeTest::deleteFromEverySignal_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("victim");
    QTest::addColumn<int>("part");

    const std::pair<ToolsLifetimeVictim, const char*> victims[] = { { ToolsLifetimeVictim::Tab, "tab" }, { ToolsLifetimeVictim::Window, "window" },
                                                                   { ToolsLifetimeVictim::Page, "page" }, { ToolsLifetimeVictim::HomeTab, "home" } };
    for (const QString& name : getScenarios())
    {
        // The sites of a scenario are divided into parts (every PART_COUNT-th site),
        // so a row ends within the time limit of QtTest also under the sanitizers
        for (const auto& [victim, victimName] : victims)
        {
            if (victim == ToolsLifetimeVictim::HomeTab && name != QStringLiteral("panel"))
            {
                // The tab of the page scenarios is the home tab
                continue;
            }
            for (int part = 0; part < PART_COUNT; ++part)
            {
                QTest::newRow(QStringLiteral("%1-%2-%3").arg(name, QLatin1String(victimName)).arg(part).toLatin1().constData()) << name << int(victim) << part;
            }
        }
    }
}

void AppToolsLifetimeTest::runScenario(const QString& name, ToolsLifetimeRecorder::Mode mode, const ToolsLifetimeRecorder::Site& target, ToolsLifetimeVictim victim,
                                       std::vector<ToolsLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred)
{
    pdftest::AppToolsFixture fixture;
    QVERIFY(fixture.window);

    ToolsLifetimeRun run;
    run.fixture = &fixture;
    run.window = fixture.window;
    run.tab = fixture.window->getHomeTab();
    run.homeTab = fixture.window->getHomeTab();
    const std::vector<ToolsLifetimeStep> steps = ToolsLifetimeHelper::scenario(name, run);
    QVERIFY(!steps.empty());
    QVERIFY(run.tab);
    fixture.window->activateTab(run.tab.data());
    pdftest::AppDocumentFixture::settle();

    ToolsLifetimeRecorder recorder(mode);
    if (mode == ToolsLifetimeRecorder::Mode::Replay)
    {
        recorder.setTarget(target, [&run, victim](const ToolsLifetimeRecorder::Site& site)
        {
            QObject* object = nullptr;

            // A victim being destroyed already (a signal of its destructor) is not
            // deleted again: qobject_cast fails once the derived destructor has run
            switch (victim)
            {
                case ToolsLifetimeVictim::Window:
                    object = qobject_cast<pdf::PDFAppMainWindow*>(run.window.data());
                    break;

                case ToolsLifetimeVictim::Tab:
                    object = qobject_cast<pdf::PDFAppTab*>(run.tab.data());
                    break;

                case ToolsLifetimeVictim::HomeTab:
                    object = qobject_cast<pdf::PDFAppTab*>(run.homeTab.data());
                    break;

                case ToolsLifetimeVictim::Page:
                    object = ToolsLifetimeHelper::victimPage(run);
                    break;
            }

            if (!object)
            {
                return ToolsLifetimeRecorder::Action::None;
            }
            if (ToolsLifetimeRecorder::isDeferredSite(site) || ToolsLifetimeRecorder::isDeferredState(site, object))
            {
                object->deleteLater();
                return ToolsLifetimeRecorder::Action::Deferred;
            }
            delete object;
            return ToolsLifetimeRecorder::Action::Deleted;
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
        if (mode == ToolsLifetimeRecorder::Mode::Discover)
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
    if (mode == ToolsLifetimeRecorder::Mode::Replay && recorder.isFired())
    {
        // The deferred work of the deleted objects finds nothing; the window keeps
        // working after a tab or the page was deleted
        QTest::qWait(pdf::PDFAppToolsPage::SEARCH_DELAY + 50);
        pdftest::AppDocumentFixture::settle();
        switch (victim)
        {
            case ToolsLifetimeVictim::Window:
                QVERIFY(!run.window);
                break;

            case ToolsLifetimeVictim::Tab:
            case ToolsLifetimeVictim::HomeTab:
            {
                QVERIFY(run.window);
                QVERIFY(victim == ToolsLifetimeVictim::Tab ? !run.tab : !run.homeTab);

                // A new document tab and the catalog in the home tab (or a new one)
                QVERIFY(fixture.openDocument());
                fixture.navigate(QStringLiteral("/tools"));
                QVERIFY(fixture.page<pdf::PDFAppToolsPage>());
                break;
            }

            case ToolsLifetimeVictim::Page:
            {
                QVERIFY(run.window && run.tab);
                if (ToolsLifetimeHelper::documentTab(run))
                {
                    QVERIFY(ToolsLifetimeHelper::backToDocument(run));
                    QVERIFY(ToolsLifetimeHelper::openPanel(run));
                }
                else
                {
                    QVERIFY(ToolsLifetimeHelper::navigate(run, QStringLiteral("/tools/collections")));
                    QVERIFY(ToolsLifetimeHelper::collectionsPage(run));
                    QVERIFY(ToolsLifetimeHelper::navigate(run, QStringLiteral("/tools?q=merge")));
                    QVERIFY(ToolsLifetimeHelper::toolsPage(run));
                }
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

void AppToolsLifetimeTest::deleteFromEverySignal()
{
    QFETCH(QString, scenario);
    QFETCH(int, victim);
    QFETCH(int, part);

    std::vector<ToolsLifetimeRecorder::Site> sites;
    runScenario(scenario, ToolsLifetimeRecorder::Mode::Discover, ToolsLifetimeRecorder::Site(), ToolsLifetimeVictim(victim), &sites, nullptr);
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
        runScenario(scenario, ToolsLifetimeRecorder::Mode::Replay, sites[i], ToolsLifetimeVictim(victim), nullptr, &isFired, &isDeferred);
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

    // At least 80 % of the sites are reproduced (a site of a timer or of the order of
    // the events may move to another step; the page victim exists only on its routes)
    QVERIFY2(onlySite >= 0 || replayed * 10 >= (replayed + int(notReproduced.size())) * 8, qPrintable(notReproduced.join(QLatin1Char('\n'))));
}

void AppToolsLifetimeTest::openPopupsSurviveChanges_data()
{
    QTest::addColumn<QString>("popup");
    QTest::addColumn<QString>("change");

    for (const char* popup : { "toolMenu", "collectionsSubmenu", "clearRecent", "collectionFilter", "deleteCollection", "editorDraft", "editorInfoBar",
                               "dragOver", "toolsPanel", "toolsPanelSearch", "toolsPanelLocked" })
    {
        for (const char* change : { "translator", "otherPins", "otherUsage", "otherCollection", "otherDelete", "everything" })
        {
            QTest::newRow(QStringLiteral("%1-%2").arg(QLatin1String(popup), QLatin1String(change)).toLatin1().constData()) << QString::fromLatin1(popup) << QString::fromLatin1(change);
        }
    }
}

void AppToolsLifetimeTest::openPopupsSurviveChanges()
{
    // Every menu, dialog, info bar and panel of step 6 is open while the language
    // changes (a translator of the application - the texts are built again) or
    // another instance writes the data, which it shows (pins, recent tools, the
    // collection, its deletion, everything). Nothing crashes, the change itself
    // writes nothing, the popup is either closed or still works, and the window stays
    // usable.
    QFETCH(QString, popup);
    QFETCH(QString, change);
    using H = ToolsLifetimeHelper;

    pdftest::AppToolsFixture fixture;
    QVERIFY(fixture.window);
    ToolsLifetimeRun run;
    run.fixture = &fixture;
    run.window = fixture.window;
    run.tab = fixture.window->getHomeTab();
    run.homeTab = fixture.window->getHomeTab();

    pdf::PDFToolRepository tools = fixture.tools();
    QVERIFY(!tools.recordUsage(QStringLiteral("pages.merge")).isError());
    QVERIFY(!tools.setPinned(QStringLiteral("pages.split"), true).isError());
    QVERIFY(!fixture.collections().createCollection(QStringLiteral("Invoices"), QString(), { QStringLiteral("pages.merge"), QStringLiteral("pages.crop") }, &run.customId).isError());

    QPointer<QWidget> shown;
    if (popup == QStringLiteral("toolMenu") || popup == QStringLiteral("collectionsSubmenu"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/tools")));
        pdf::PDFAppToolMenu* menu = H::toolMenu(run, QStringLiteral("pages.merge"));
        QVERIFY(menu);
        shown = menu;
        if (popup == QStringLiteral("collectionsSubmenu"))
        {
            menu->setActiveAction(menu->getCollectionsMenu()->menuAction());
            menu->getCollectionsMenu()->popup(menu->mapToGlobal(QPoint(menu->width(), 10)));
            shown = menu->getCollectionsMenu();
        }
        QTRY_VERIFY(shown && shown->isVisible());
    }
    else if (popup == QStringLiteral("clearRecent"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/tools")));
        shown = H::toolsPage(run)->openClearRecentDialog();
    }
    else if (popup == QStringLiteral("collectionFilter"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/tools?collection=custom-%1").arg(run.customId)));
        QVERIFY(H::toolsPage(run));
        shown = H::toolsPage(run)->getCollectionCard();
    }
    else if (popup == QStringLiteral("deleteCollection"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/tools/collections/custom-%1").arg(run.customId)));
        QVERIFY(H::collectionsPage(run) && H::collectionsPage(run)->requestDelete());
        shown = H::collectionsPage(run)->getDeleteDialog();
    }
    else if (popup == QStringLiteral("editorDraft") || popup == QStringLiteral("editorInfoBar") || popup == QStringLiteral("dragOver"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/tools/collections/custom-%1").arg(run.customId)));
        pdf::PDFAppToolCollectionEditor* collectionEditor = H::editor(run);
        QVERIFY(collectionEditor);
        QVERIFY(H::replaceText(collectionEditor->getNameField(), QStringLiteral("Mine")));
        QVERIFY(H::type(collectionEditor->getSearchField(), QStringLiteral("split")));
        shown = collectionEditor;
        if (popup == QStringLiteral("editorInfoBar"))
        {
            QVERIFY(H::saveOther(run, QStringLiteral("Invoices 2"), { QStringLiteral("pages.merge") }));
            shown = collectionEditor->getInfoBar();
        }
        else if (popup == QStringLiteral("dragOver"))
        {
            // A drag over the list (enter, no drop): the drop indicator is shown
            pdf::PDFAppCollectionToolList* list = collectionEditor->getToolList();
            QMimeData* mimeData = list->createMimeData(QStringLiteral("pages.crop"));
            mimeData->setParent(list);
            QDragEnterEvent enter(QPoint(20, 6), Qt::MoveAction, mimeData, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(list, &enter);
            QVERIFY(enter.isAccepted());
            QVERIFY(list->getDropIndicatorIndex() >= 0);
            shown = list;
        }
    }
    else
    {
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.openDocument();
        QVERIFY(tab);
        QVERIFY(QTest::qWaitFor([&tab]() { return tab && tab->getDocument() && tab->getDocument()->isReady() && tab->getView() && tab->getView()->getPageCount() > 0; },
                                20000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
        H::settle();
        run.tab = tab.data();
        pdf::PDFAppDocumentToolsPanel* toolsPanel = H::openPanel(run);
        QVERIFY(toolsPanel);
        if (popup == QStringLiteral("toolsPanelSearch"))
        {
            QVERIFY(H::type(toolsPanel->getSearchField(), QStringLiteral("merge")));
            QVERIFY(toolsPanel->isSearching());
        }
        else if (popup == QStringLiteral("toolsPanelLocked"))
        {
            tab->getDocument()->setLocked(true);
            H::settle();
        }
        shown = toolsPanel;
    }
    QVERIFY2(shown && shown->isVisible(), qPrintable(popup));

    // What the change must not write (a translator changes no data)
    const QStringList pinnedBefore = fixture.tools().getPinnedTools();
    const std::vector<pdf::PDFToolCollectionRow> collectionsBefore = fixture.collections().getCollections();

    ToolsLifetimeTranslator translator;
    if (change == QStringLiteral("translator"))
    {
        QVERIFY(QCoreApplication::installTranslator(&translator));
        H::settle();
    }
    else if (change == QStringLiteral("otherPins"))
    {
        QVERIFY(H::pinOther(run, QStringLiteral("pages.merge"), true));
        QVERIFY(H::pinOther(run, QStringLiteral("pages.split"), false));
    }
    else if (change == QStringLiteral("otherUsage"))
    {
        QVERIFY(fixture.recordUsageFromOtherConnection(QStringLiteral("pages.rotate"), QDateTime::currentMSecsSinceEpoch()));
        QVERIFY(fixture.writeFromOtherConnection([](pdf::PDFDatabase* database) { return pdf::PDFToolRepository(database).clearUsage(); }, pdf::PDFToolRepository::getUsageTopic()));
    }
    else if (change == QStringLiteral("otherCollection"))
    {
        QVERIFY(H::saveOther(run, QStringLiteral("Invoices elsewhere"), { QStringLiteral("pages.split") }));
    }
    else if (change == QStringLiteral("otherDelete"))
    {
        QVERIFY(H::deleteOther(run));
    }
    else
    {
        Q_EMIT fixture.context->getNotifier()->everythingChanged();
        H::settle();
    }
    QVERIFY(run.window);

    if (change == QStringLiteral("translator") || change == QStringLiteral("everything"))
    {
        // Nothing is written by the change itself
        QCOMPARE(fixture.tools().getPinnedTools(), pinnedBefore);
        const std::vector<pdf::PDFToolCollectionRow> collectionsAfter = fixture.collections().getCollections();
        QCOMPARE(collectionsAfter.size(), collectionsBefore.size());
        for (size_t i = 0; i < collectionsAfter.size(); ++i)
        {
            QCOMPARE(collectionsAfter[i].name, collectionsBefore[i].name);
            QCOMPARE(collectionsAfter[i].toolIds, collectionsBefore[i].toolIds);
        }
    }

    // A dialog or popup still open is closed by Esc
    if (shown && shown->isVisible() && shown->isWindow())
    {
        H::key(shown->window(), Qt::Key_Escape);
        H::settle();
    }
    if (change == QStringLiteral("translator"))
    {
        QCoreApplication::removeTranslator(&translator);
    }
    H::settle();
    QTest::qWait(50);
    H::settle();

    // The window works: the catalog, the collections and the panel open again
    QVERIFY(run.window);
    run.window->activateTab(run.homeTab.data());
    run.tab = run.homeTab;
    H::settle();
    QVERIFY(H::navigate(run, QStringLiteral("/tools?q=merge")));
    QVERIFY(H::toolsPage(run));
    QVERIFY(H::navigate(run, QStringLiteral("/tools/collections")));
    QVERIFY(H::editor(run));
    QVERIFY(fixture.openDocument());
    run.tab = fixture.currentTab();
    QVERIFY(QTest::qWaitFor([&run]() { pdf::PDFAppDocumentTab* tab = H::documentTab(run); return tab && tab->getDocument() && tab->getDocument()->isReady(); },
                            20000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
    H::settle();
    QVERIFY(H::openPanel(run));
}

int main(int argc, char** argv)
{
    // QTEST_MAIN with the application of the test (it knows the delivered events)
    ToolsLifetimeApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppToolsLifetimeTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_apptoolslifetimetest.moc"
