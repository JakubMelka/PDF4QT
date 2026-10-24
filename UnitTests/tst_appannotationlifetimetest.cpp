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

// Generic lifetime test of step 4 (annotations, Insert, Notes / Outline editing,
// form filling): every scenario of the user is run once with a recorder of the
// signals of every object of the application (classes pdf::PDFApp*), then once
// more for each signal it has seen (the first emission of the signal of an object
// in each step of the scenario), with the tab or the window deleted synchronously
// from that emission. Nothing may crash (run it under valgrind - freed memory stays
// readable in a Release build on Windows), the window survives the deletion of its
// tab and the deferred work of the deleted objects finds nothing.

#include "appdocumentfixture.h"

#include "pdfappannotatecommands.h"
#include "pdfappannotationbar.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreatedialogs.h"
#include "pdfappannotationcreatetools.h"
#include "pdfappannotationcreator.h"
#include "pdfappannotationeditcommands.h"
#include "pdfappannotationgeometrydialog.h"
#include "pdfappannotationmenus.h"
#include "pdfappannotationpropertiesdialog.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappcopytopagesdialog.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_notes.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappmarkresults.h"
#include "pdfappoutlineeditor.h"

#include "pdfannotation.h"
#include "pdfdocument.h"
#include "pdfwidgetformmanager.h"
#include "pdfform.h"

#include "pdffluentwidgets.h"
#include "pdfcomponentpanels.h"

#include <QHash>
#include <QLineEdit>
#include <QMenu>
#include <QMetaMethod>
#include <QPushButton>
#include <QSet>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <functional>
#include <tuple>
#include <vector>

#include <QtCore/private/qobject_p.h>

class AnnotationLifetimeRecorder;

/// One connection of the recorder to one signal of one object: the slot is the
/// first method after the methods of QObject (the pattern of QSignalSpy)
class AnnotationLifetimeHook : public QObject
{
public:
    explicit AnnotationLifetimeHook(AnnotationLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature);

    virtual int qt_metacall(QMetaObject::Call call, int id, void** arguments) override;

private:
    AnnotationLifetimeRecorder* m_recorder;
    QPointer<QObject> m_sender;
    QByteArray m_signature;
};

/// Recorder of the signals of the objects of the application. In the discovery
/// mode it collects the sites (signal of an object, step of the scenario), in the
/// replay mode it deletes the victim at the first emission of the target site.
class AnnotationLifetimeRecorder : public QObject
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

    explicit AnnotationLifetimeRecorder(Mode mode) :
        m_mode(mode)
    {
        qApp->installEventFilter(this);
        s_current = this;
        s_callbacks.signal_begin_callback = &AnnotationLifetimeRecorder::onSignalBegin;
        s_callbacks.slot_begin_callback = nullptr;
        s_callbacks.signal_end_callback = &AnnotationLifetimeRecorder::onSignalEnd;
        s_callbacks.slot_end_callback = nullptr;
        qt_register_signal_spy_callbacks(&s_callbacks);
    }

    virtual ~AnnotationLifetimeRecorder() override
    {
        qt_register_signal_spy_callbacks(nullptr);
        qApp->removeEventFilter(this);
        s_current = nullptr;
    }

    /// Emissions of the GUI thread on the stack (signal spy callbacks of QtCore):
    /// true for an emission of a foreign object - the engine (pdf:: classes of
    /// Pdf4QtLibWidgets / Pdf4QtLibCore shared with the old applications) or Qt
    /// (item models, selection models, text documents, ...) - which does not
    /// survive the deletion of the objects it works with inside of its own signal.
    /// Buttons, actions, shortcuts and timers survive it (Qt guards them).
    static void onSignalBegin(QObject* caller, int signalIndex, void** arguments)
    {
        Q_UNUSED(signalIndex);
        Q_UNUSED(arguments);
        if (s_current && QThread::currentThread() == qApp->thread())
        {
            s_current->m_emissions.push_back(isForeignEmitter(caller));
        }
    }

    static void onSignalEnd(QObject* caller, int signalIndex)
    {
        Q_UNUSED(caller);
        Q_UNUSED(signalIndex);
        if (s_current && QThread::currentThread() == qApp->thread() && !s_current->m_emissions.empty())
        {
            s_current->m_emissions.pop_back();
        }
    }

    static bool isForeignEmitter(const QObject* caller)
    {
        if (!caller)
        {
            return false;
        }
        const QMetaObject* metaObject = caller->metaObject();
        if (isApplicationClass(metaObject))
        {
            return false;
        }
        for (const char* guarded : { "QAbstractButton", "QAction", "QShortcut", "QTimer", "QButtonGroup", "QActionGroup" })
        {
            if (caller->inherits(guarded))
            {
                return false;
            }
        }
        return true;
    }

    bool hasForeignEmission() const
    {
        return std::find(m_emissions.cbegin(), m_emissions.cend(), true) != m_emissions.cend();
    }

    /// The recorder of the running scenario (nullptr without one)
    static AnnotationLifetimeRecorder* getCurrent() { return s_current; }

    /// The test delivers an input event to the canvas: the engine (the tools, the
    /// annotation and form managers of Pdf4QtLibWidgets, which the old applications
    /// share) is on the stack. The engine does not survive the deletion of its
    /// widget inside of its own input handler or signal, nor does Qt inside of the
    /// signals of its models and text documents, and the application never deletes
    /// a tab synchronously (PDFAppMainWindow::closeTab - deleteLater): a victim of
    /// such a site is deleted by deleteLater (the deferred work after the foreign
    /// code returns is checked); every other site deletes it synchronously, so the
    /// code of the application after each of its own Q_EMIT must survive it.
    void beginEngineInput() { ++m_engineInputDepth; }
    void endEngineInput() { --m_engineInputDepth; }

    void setTarget(const Site& site, std::function<bool(bool)> action)
    {
        m_target = site;
        m_action = std::move(action);
    }

    void setStep(int step) { m_step = step; }
    bool isFired() const { return m_isFired; }
    bool isStopped() const { return m_isStopped; }
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

        // Signals declared by the classes of the application only (the signals
        // of Qt and of the engine are not emitted by the application itself)
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

            AnnotationLifetimeHook* hook = new AnnotationLifetimeHook(this, object, method.methodSignature());
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

        // A signal emitted while a widget paints (a model asked for its data by a
        // delegate) is no site: Qt does not survive a deletion of the painted
        // widget inside of its paint event
        if (m_paintedWidget && m_paintedWidget->paintingActive())
        {
            return;
        }
        if (isPaintSignal(sender, signature))
        {
            return;
        }

        const Site site{ getPath(sender) + QStringLiteral("::") + QString::fromLatin1(signature), m_step };
        if (m_mode == Mode::Discover)
        {
            if (!m_seen.contains(site.key + QLatin1Char('#') + QString::number(site.step)))
            {
                m_seen.insert(site.key + QLatin1Char('#') + QString::number(site.step));
                m_sites.push_back(site);
            }
        }
        else if (!m_isFired && !m_isInAction && site == m_target && m_action)
        {
            // The deletion emits further signals (also of the target site): no
            // second deletion of the victim from inside of its own destruction
            m_isInAction = true;
            m_isFired = m_action(m_engineInputDepth > 0 || hasForeignEmission());
            m_isInAction = false;
        }
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        // An object receiving an event is constructed: hook it (objects created in
        // the middle of a step, e.g. dialogs and popups shown by a step)
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

    /// Signals emitted from QAbstractItemModel::data, which a delegate calls
    /// while it paints (the painted view cannot die inside of its paint event)
    static bool isPaintSignal(const QObject* sender, const QByteArray& signature)
    {
        return qobject_cast<const QAbstractItemModel*>(sender) && signature == QByteArrayLiteral("pictureRequested()");
    }

    static bool isApplicationClass(const QMetaObject* metaObject)
    {
        return metaObject && QByteArray(metaObject->className()).startsWith("pdf::PDFApp");
    }

    /// Path of the object by the class names of its parents (no pointers, no
    /// object names - both differ in each run)
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
    bool m_isInAction = false;
    bool m_isStopped = false;
    Site m_target;
    std::function<bool(bool)> m_action;
    int m_engineInputDepth = 0;
    std::vector<bool> m_emissions;
    static inline AnnotationLifetimeRecorder* s_current = nullptr;
    static inline QSignalSpyCallbackSet s_callbacks = { nullptr, nullptr, nullptr, nullptr };
    std::vector<Site> m_sites;
    QSet<QString> m_seen;
    QHash<QObject*, QPointer<QObject>> m_hooked;
    QPointer<QWidget> m_paintedWidget;
};

AnnotationLifetimeHook::AnnotationLifetimeHook(AnnotationLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature) :
    QObject(recorder),
    m_recorder(recorder),
    m_sender(sender),
    m_signature(signature)
{
}

int AnnotationLifetimeHook::qt_metacall(QMetaObject::Call call, int id, void** arguments)
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

/// Input of the test delivered to the engine (see beginEngineInput)
class AnnotationLifetimeEngineInput
{
public:
    AnnotationLifetimeEngineInput()
    {
        if (AnnotationLifetimeRecorder* recorder = AnnotationLifetimeRecorder::getCurrent())
        {
            recorder->beginEngineInput();
        }
    }

    ~AnnotationLifetimeEngineInput()
    {
        if (AnnotationLifetimeRecorder* recorder = AnnotationLifetimeRecorder::getCurrent())
        {
            recorder->endEngineInput();
        }
    }

    AnnotationLifetimeEngineInput(const AnnotationLifetimeEngineInput&) = delete;
    AnnotationLifetimeEngineInput& operator=(const AnnotationLifetimeEngineInput&) = delete;
};

/// State of one run of a scenario
struct AnnotationLifetimeRun
{
    pdftest::AppDocumentFixture* fixture = nullptr;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppDocumentTab> tab;
};

using AnnotationLifetimeStep = std::function<bool(AnnotationLifetimeRun&)>;

/// Helpers of the scenarios
class AnnotationLifetimeTestHelper
{
public:
    AnnotationLifetimeTestHelper() = delete;

    static pdf::PDFAppAnnotationCreator* creator(AnnotationLifetimeRun& run)
    {
        return run.tab ? pdf::PDFAppAnnotateCommandHandler::findCreator(run.tab->getView()) : nullptr;
    }

    static pdf::PDFAppAnnotationController* controller(AnnotationLifetimeRun& run)
    {
        return run.tab ? run.tab->getView()->getAnnotationController() : nullptr;
    }

    static pdf::PDFAppAnnotationEditCommandHandler* editHandler(AnnotationLifetimeRun& run)
    {
        return run.tab ? run.tab->findChild<pdf::PDFAppAnnotationEditCommandHandler*>() : nullptr;
    }

    static pdf::PDFObjectReference find(AnnotationLifetimeRun& run, int pageIndex, const QString& subject)
    {
        const pdf::PDFDocumentPointer document = run.tab && run.tab->getDocument() ? run.tab->getDocument()->getDocument() : pdf::PDFDocumentPointer();
        const pdf::PDFPage* page = document ? document->getCatalog()->getPage(pageIndex) : nullptr;
        if (!page)
        {
            return pdf::PDFObjectReference();
        }

        pdf::PDFDocumentDataLoaderDecorator loader(document.data());
        for (const pdf::PDFObjectReference& reference : page->getAnnotations())
        {
            const pdf::PDFObject& object = document->getObjectByReference(reference);
            if (object.isDictionary() && loader.readTextStringFromDictionary(object.getDictionary(), "Subj", QString()) == subject)
            {
                return reference;
            }
        }
        return pdf::PDFObjectReference();
    }

    static bool select(AnnotationLifetimeRun& run, const QString& subject)
    {
        pdf::PDFAppAnnotationController* annotationController = controller(run);
        const pdf::PDFObjectReference reference = find(run, 0, subject);
        if (!annotationController || !reference.isValid())
        {
            return false;
        }
        annotationController->selectAnnotations({ reference });
        pdftest::AppDocumentFixture::settle();
        return true;
    }

    static bool command(AnnotationLifetimeRun& run, const QString& commandId)
    {
        if (!run.tab)
        {
            return false;
        }
        const bool result = run.tab->executeCommand(commandId);
        pdftest::AppDocumentFixture::settle();
        return result;
    }

    /// Moves the pointer to the point of the page and clicks there
    static bool pick(AnnotationLifetimeRun& run, int pageIndex, const QPointF& pagePoint)
    {
        if (!run.tab)
        {
            return false;
        }
        pdf::PDFAppDocumentView* view = run.tab->getView();
        bool ok = false;
        const QPoint position = pdftest::AppDocumentFixture::pageToWidget(view, pageIndex, pagePoint, &ok);
        QPointer<QWidget> target = pdftest::AppDocumentFixture::canvasWidget(view);
        if (!ok || !target)
        {
            return false;
        }
        {
            AnnotationLifetimeEngineInput input;
            for (const QPoint& point : { position + QPoint(1, 1), position })
            {
                if (!target)
                {
                    return true;
                }
                QMouseEvent move(QEvent::MouseMove, point, target->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &move);
            }
            if (run.tab)
            {
                pdftest::AppDocumentFixture::clickPage(view, pageIndex, pagePoint);
            }
        }
        pdftest::AppDocumentFixture::settle();
        return true;
    }

    static void key(AnnotationLifetimeRun& run, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QPointer<QWidget> target = run.tab ? pdftest::AppDocumentFixture::canvasWidget(run.tab->getView()) : nullptr;
        AnnotationLifetimeEngineInput input;
        if (target)
        {
            QKeyEvent press(QEvent::KeyPress, key, modifiers);
            QCoreApplication::sendEvent(target, &press);
        }
        if (target)
        {
            QKeyEvent release(QEvent::KeyRelease, key, modifiers);
            QCoreApplication::sendEvent(target, &release);
        }
        pdftest::AppDocumentFixture::settle();
    }

    static bool selectAllText(AnnotationLifetimeRun& run)
    {
        QPointer<pdf::PDFAppAnnotationCreator> annotationCreator = creator(run);
        if (!annotationCreator)
        {
            return false;
        }
        run.tab->getView()->selectAllText();
        std::ignore = QTest::qWaitFor([&annotationCreator]() { return !annotationCreator || annotationCreator->hasTextSelection(); }, 10000);
        pdftest::AppDocumentFixture::settle();
        return !annotationCreator || annotationCreator->hasTextSelection();
    }

    static pdf::PDFAppCommentPopup* popup(AnnotationLifetimeRun& run)
    {
        return run.tab ? run.tab->getView()->getCommentPopup() : nullptr;
    }

    template<typename Panel>
    static Panel* panel(AnnotationLifetimeRun& run, const QString& id)
    {
        pdf::PDFAppDocumentSidebar* sidebar = run.tab ? run.tab->getView()->getSidebar() : nullptr;
        if (!sidebar || !sidebar->showPanel(id))
        {
            return nullptr;
        }
        pdftest::AppDocumentFixture::settle();
        return run.tab ? qobject_cast<Panel*>(sidebar->findPanel(id)) : nullptr;
    }

    /// Form field: center of its first widget (page coordinates)
    static QPointF fieldCenter(AnnotationLifetimeRun& run, const QString& fieldName)
    {
        pdf::PDFWidgetFormManager* manager = run.tab ? run.tab->getView()->getFormManager() : nullptr;
        if (!manager || !manager->getForm())
        {
            return QPointF();
        }

        QPointF result;
        manager->getForm()->apply([&result, &fieldName, manager](const pdf::PDFFormField* formField)
        {
            if (!result.isNull() || formField->getName(pdf::PDFFormField::Partial) != fieldName)
            {
                return;
            }
            const pdf::PDFFormField* field = formField;
            while (field && field->getWidgets().empty() && !field->getChildFields().empty())
            {
                field = field->getChildFields().front().get();
            }
            if (field && !field->getWidgets().empty())
            {
                result = manager->getWidgetRectangle(field->getWidgets().front()).center();
            }
        });
        return result;
    }

    static bool clickField(AnnotationLifetimeRun& run, const QString& fieldName)
    {
        const QPointF center = fieldCenter(run, fieldName);
        if (center.isNull())
        {
            return false;
        }
        {
            AnnotationLifetimeEngineInput input;
            pdftest::AppDocumentFixture::clickPage(run.tab->getView(), 0, center);
        }
        pdftest::AppDocumentFixture::settle();
        return true;
    }

    static void typeText(AnnotationLifetimeRun& run, const QString& text)
    {
        for (const QChar character : text)
        {
            QPointer<QWidget> target = run.tab ? pdftest::AppDocumentFixture::canvasWidget(run.tab->getView()) : nullptr;
            if (!target)
            {
                return;
            }
            AnnotationLifetimeEngineInput input;
            QTest::keyClick(target, character.toLatin1());
        }
        pdftest::AppDocumentFixture::settle();
    }

    /// Steps and sample of the scenario; an empty list for an unknown name
    static std::vector<AnnotationLifetimeStep> scenario(const QString& name, const QString& folder, QString* sample);
};

using LH = AnnotationLifetimeTestHelper;
using Kind = pdf::PDFAppAnnotationToolKind;

std::vector<AnnotationLifetimeStep> AnnotationLifetimeTestHelper::scenario(const QString& name, const QString& folder, QString* sample)
{
    // Each step checks the objects it uses: a step after the deletion of the victim
    // is not run, but the action of a step may delete it (then the step returns)
    std::vector<AnnotationLifetimeStep> steps;
    const QString simple = pdftest::AppDocumentSamples::simple(folder, QStringLiteral("Lifetime.pdf"), 3);
    const auto settled = []() { pdftest::AppDocumentFixture::settle(); return true; };

    if (name == QStringLiteral("markup"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::selectAllText(run); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->markTextSelection(pdf::AnnotationType::Highlight); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::redo()); });
    }
    else if (name == QStringLiteral("highlightComment"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::selectAllText(run); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::creator(run)->commentTextSelection(); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->setComposedText(QStringLiteral("Comment")); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->accept(); return settled(); });
    }
    else if (name == QStringLiteral("stickyNote"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::creator(run)->startTool(Kind::StickyNote) || !run.tab; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(60, 200)); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->setComposedText(QStringLiteral("Note")); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->accept(); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(120, 250)); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->cancel(); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { LH::key(run, Qt::Key_Escape); return true; });
    }
    else if (name == QStringLiteral("textBox"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::creator(run)->startTool(Kind::TextBox) || !run.tab; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(40, 150)); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(140, 190)); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->getTextBoxPopup()->setText(QStringLiteral("Text")); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::creator(run)->getTextBoxPopup()->accept(); return settled() && (result || !run.tab); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(40, 250)); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(140, 290)); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->getTextBoxPopup()->cancel(); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->endTool(); return settled(); });
    }
    else if (name == QStringLiteral("webLink"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::creator(run)->startTool(Kind::WebLink) || !run.tab; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(40, 150)); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(140, 190)); });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            QPointer<pdf::PDFAppWebLinkDialog> dialog = LH::creator(run)->getWebLinkDialog();
            if (!dialog)
            {
                return false;
            }
            dialog->getField()->setText(QStringLiteral("https://example.com/"));
            return settled();
        });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->getWebLinkDialog()->getAddButton()->click(); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->endTool(); return settled(); });
    }
    else if (name == QStringLiteral("pageLink"))
    {
        *sample = simple;
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->setPageLinkTarget(pdf::DestinationType::Fit, false); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::creator(run)->startTool(Kind::PageLink) || !run.tab; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(40, 150)); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(140, 170)); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { run.tab->getView()->goToPage(1); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 1, QPointF(80, 260)); });
        steps.push_back([](AnnotationLifetimeRun& run) { LH::key(run, Qt::Key_Escape); return true; });
    }
    else if (name == QStringLiteral("shape"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::creator(run)->startTool(Kind::Rectangle) || !run.tab; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(40, 100)); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::pick(run, 0, QPointF(120, 160)); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::creator(run)->setPenWidth(Kind::Rectangle, 3.0); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { LH::key(run, Qt::Key_Escape); return true; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
    }
    else if (name == QStringLiteral("markResults"))
    {
        *sample = simple;
        steps.push_back([](AnnotationLifetimeRun& run)
        {
            pdf::PDFAppDocumentSearchCommandHandler* handler = pdf::PDFAppDocumentSearchCommandHandler::findHandler(run.tab->getView());
            QPointer<pdf::PDFAppDocumentSearchController> searchController = handler ? handler->getSearchController() : nullptr;
            if (!searchController)
            {
                return false;
            }
            searchController->search(QStringLiteral("alpha"));
            std::ignore = QTest::qWaitFor([&searchController]() { return !searchController || searchController->getState() == pdf::PDFAppDocumentSearchController::State::Finished; }, 60000);
            pdftest::AppDocumentFixture::settle();
            return true;
        });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            pdf::PDFAppAnnotateCommandHandler* handler = pdf::PDFAppAnnotateCommandHandler::findHandler(run.tab->getView());
            const bool result = handler && handler->getMarkResults()->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::All);
            return settled() && (result || !run.tab);
        });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
    }
    else if (name == QStringLiteral("properties") || name == QStringLiteral("geometry") || name == QStringLiteral("copyToPages"))
    {
        *sample = pdftest::AppDocumentSamples::annotations(folder);
        const QString commandId = name == QStringLiteral("properties") ? pdf::PDFAppCommands::annotationProperties()
                                                                        : (name == QStringLiteral("geometry") ? pdf::PDFAppCommands::annotationGeometry() : pdf::PDFAppCommands::annotationCopyToPages());
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::select(run, QStringLiteral("square")); });
        steps.push_back([commandId](AnnotationLifetimeRun& run) { LH::command(run, commandId); return !run.tab || (LH::editHandler(run) && LH::editHandler(run)->getOpenDialog()); });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            QPointer<QWidget> dialog = LH::editHandler(run)->getOpenDialog();
            if (auto* properties = qobject_cast<pdf::PDFAppAnnotationPropertiesDialog*>(dialog.data()))
            {
                properties->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("Lifetime"));
                if (dialog && run.tab)
                {
                    properties->apply();
                }
            }
            else if (auto* geometry = qobject_cast<pdf::PDFAppAnnotationGeometryDialog*>(dialog.data()))
            {
                geometry->getField(QStringLiteral("width"))->setValue(5.0);
                if (dialog && run.tab)
                {
                    geometry->apply();
                }
            }
            else if (auto* copy = qobject_cast<pdf::PDFAppCopyToPagesDialog*>(dialog.data()))
            {
                copy->copy();
            }
            else
            {
                return false;
            }
            return settled();
        });
        steps.push_back([commandId](AnnotationLifetimeRun& run) { LH::command(run, commandId); return true; });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            if (QDialog* dialog = qobject_cast<QDialog*>(LH::editHandler(run)->getOpenDialog()))
            {
                dialog->reject();
            }
            return settled();
        });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
    }
    else if (name == QStringLiteral("comment"))
    {
        *sample = pdftest::AppDocumentSamples::annotations(folder);
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->showThread(LH::find(run, 0, QStringLiteral("text")), true); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->setReplyText(QStringLiteral("Reply")); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::popup(run)->sendReply(); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::popup(run)->beginEditContents(); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->setEditedContents(QStringLiteral("Edited")); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::popup(run)->commitContents(); return settled() && (result || !run.tab); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::popup(run)->cancel(); return settled(); });
    }
    else if (name == QStringLiteral("bar"))
    {
        *sample = pdftest::AppDocumentSamples::annotations(folder);
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::select(run, QStringLiteral("square")); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::editHandler(run)->getAnnotationBar()->applyColor(QColor(Qt::green)); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::editHandler(run)->getAnnotationBar()->applyColor(QColor(Qt::blue)); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::editHandler(run)->getAnnotationBar()->applyWidth(3.0); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::select(run, QStringLiteral("circle")); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { LH::editHandler(run)->getAnnotationBar()->showMoreMenu(); return settled(); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
    }
    else if (name == QStringLiteral("menu"))
    {
        *sample = pdftest::AppDocumentSamples::annotations(folder);
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::select(run, QStringLiteral("square")); });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            pdf::PDFAppAnnotationController* annotationController = LH::controller(run);
            pdf::PDFAnnotationMenuRequest request;
            request.kind = pdf::PDFAnnotationMenuRequest::Kind::Selection;
            request.selection = annotationController->getSelectedAnnotations();
            request.annotation = request.selection.size() == 1 ? request.selection.front() : pdf::PDFObjectReference();
            request.isModificationAllowed = annotationController->isAnnotationEditingAllowed();
            request.canPaste = annotationController->canPaste();
            request.globalPosition = QPoint(100, 60);
            annotationController->requestMenu(request);
            settled();
            return !run.tab || LH::editHandler(run)->getMenus()->getMenu() != nullptr;
        });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            QAction* action = pdf::PDFAppAnnotationMenus::findAction(LH::editHandler(run)->getMenus()->getMenu(), pdf::PDFAppCommands::annotationHide());
            if (!action)
            {
                return false;
            }
            action->trigger();
            return settled();
        });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::select(run, QStringLiteral("circle")); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::copy()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::paste()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::deleteSelection()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::lockDocument()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::lockDocument()); });
    }
    else if (name == QStringLiteral("outline"))
    {
        *sample = pdftest::AppDocumentSamples::outline(folder);
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::panel<pdf::PDFAppOutlinePanel>(run, pdf::PDFAppDocumentPanelIds::outline()) != nullptr || !run.tab; });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = run.tab->getView()->getOutlineEditor()->createItemAtCurrentView(); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = run.tab->getView()->getOutlineEditor()->finishCreation(QStringLiteral("Lifetime")); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { run.tab->getView()->getOutlineEditor()->setCurrentItem({ 1 }); return settled(); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = run.tab->getView()->getOutlineEditor()->renameItem({ 1 }, QStringLiteral("Renamed")); return settled() && (result || !run.tab); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::outlineMoveDown()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::outlineIndent()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::outlineBold()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::outlineSetTarget()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::outlineDelete()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = run.tab->getView()->getOutlineEditor()->createItemAtCurrentView(); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run) { run.tab->getView()->getOutlineEditor()->cancelCreation(); return settled(); });
    }
    else if (name == QStringLiteral("notes"))
    {
        *sample = pdftest::AppDocumentSamples::annotations(folder);
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::panel<pdf::PDFAppNotesPanel>(run, pdf::PDFAppDocumentPanelIds::notes()) != nullptr || !run.tab; });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            pdf::PDFAppNotesPanel* notes = LH::panel<pdf::PDFAppNotesPanel>(run, pdf::PDFAppDocumentPanelIds::notes());
            if (notes)
            {
                notes->selectNotes({ LH::find(run, 0, QStringLiteral("square")) });
            }
            return settled();
        });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::panel<pdf::PDFAppNotesPanel>(run, pdf::PDFAppDocumentPanelIds::notes())->setSelectedNotesHidden(true); return settled() && (result || !run.tab); });
        steps.push_back([settled](AnnotationLifetimeRun& run)
        {
            pdf::PDFAppNotesPanel* notes = LH::panel<pdf::PDFAppNotesPanel>(run, pdf::PDFAppDocumentPanelIds::notes());
            if (notes)
            {
                notes->selectNotes({ LH::find(run, 0, QStringLiteral("circle")) });
            }
            return settled();
        });
        steps.push_back([settled](AnnotationLifetimeRun& run) { const bool result = LH::panel<pdf::PDFAppNotesPanel>(run, pdf::PDFAppDocumentPanelIds::notes())->deleteSelectedNotes(); return settled() && (result || !run.tab); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::redo()); });
    }
    else if (name == QStringLiteral("forms"))
    {
        *sample = pdftest::AppDocumentSamples::fullForm(folder);
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::clickField(run, QStringLiteral("name")); });
        steps.push_back([](AnnotationLifetimeRun& run) { LH::typeText(run, QStringLiteral("abc")); return true; });
        steps.push_back([](AnnotationLifetimeRun& run) { LH::key(run, Qt::Key_Tab); return true; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::clickField(run, QStringLiteral("agree")); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::clickField(run, QStringLiteral("choice")); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::clickField(run, QStringLiteral("reset")); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::clickField(run, QStringLiteral("name")); });
        steps.push_back([](AnnotationLifetimeRun& run) { LH::typeText(run, QStringLiteral("x")); return true; });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::undo()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::redo()); });
        steps.push_back([](AnnotationLifetimeRun& run) { return LH::command(run, pdf::PDFAppCommands::lockDocument()); });
    }

    return steps;
}

class AppAnnotationLifetimeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void deleteFromEverySignal_data();
    void deleteFromEverySignal();

private:
    static constexpr int PART_COUNT = 4;

    /// Runs the scenario. In the replay mode the victim is deleted at the target
    /// site. \returns the sites seen (discovery) and whether the victim was deleted.
    void runScenario(const QString& name, AnnotationLifetimeRecorder::Mode mode, const AnnotationLifetimeRecorder::Site& target, bool deleteWindow,
                     std::vector<AnnotationLifetimeRecorder::Site>* sites, bool* isFired);
};

void AppAnnotationLifetimeTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AppAnnotationLifetimeTest::deleteFromEverySignal_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<bool>("deleteWindow");
    QTest::addColumn<int>("part");

    for (const char* name : { "markup", "highlightComment", "stickyNote", "textBox", "webLink", "pageLink", "shape", "markResults",
                              "properties", "geometry", "copyToPages", "comment", "bar", "menu", "outline", "notes", "forms" })
    {
        // The sites of a scenario are divided into parts (every PART_COUNT-th site),
        // so a row ends within the time limit of QtTest also under valgrind
        for (int part = 0; part < PART_COUNT; ++part)
        {
            QTest::newRow(QStringLiteral("%1-tab-%2").arg(QLatin1String(name)).arg(part).toLatin1().constData()) << QString::fromLatin1(name) << false << part;
            QTest::newRow(QStringLiteral("%1-window-%2").arg(QLatin1String(name)).arg(part).toLatin1().constData()) << QString::fromLatin1(name) << true << part;
        }
    }
}

void AppAnnotationLifetimeTest::runScenario(const QString& name, AnnotationLifetimeRecorder::Mode mode, const AnnotationLifetimeRecorder::Site& target, bool deleteWindow,
                                            std::vector<AnnotationLifetimeRecorder::Site>* sites, bool* isFired)
{
    pdftest::AppDocumentFixture fixture;
    QString sample;
    const std::vector<AnnotationLifetimeStep> steps = LH::scenario(name, fixture.folder(), &sample);
    QVERIFY(!steps.empty());
    QVERIFY(!sample.isEmpty());

    AnnotationLifetimeRun run;
    run.fixture = &fixture;
    run.window = fixture.window;
    run.tab = fixture.open(sample);
    QVERIFY(run.tab);
    std::ignore = QTest::qWaitFor([&run]() { return !run.tab || (run.tab->getDocument() && run.tab->getDocument()->isReady() && run.tab->getView()->getPageCount() > 0); }, 20000);
    QVERIFY(run.tab);
    fixture.window->activateTab(run.tab.data());
    pdftest::AppDocumentFixture::settle();

    // The verification of the signatures (a worker) has ended: its results are given
    // to every new version inside of the commits of the steps (the same sites in
    // each run, independent of the speed of the worker)
    pdf::PDFAppDocumentSearchCommandHandler* searchHandler = pdf::PDFAppDocumentSearchCommandHandler::findHandler(run.tab->getView());
    QPointer<pdf::PDFAppDocumentSignatureVerifier> verifier = searchHandler ? searchHandler->getSignatureVerifier() : nullptr;
    QVERIFY(verifier);
    QVERIFY(QTest::qWaitFor([&verifier]() { return !verifier || !verifier->isVerifying(); }, 30000));
    pdftest::AppDocumentFixture::settle();

    AnnotationLifetimeRecorder recorder(mode);
    if (mode == AnnotationLifetimeRecorder::Mode::Replay)
    {
        recorder.setTarget(target, [&run, deleteWindow](bool isEngineInput)
        {
            // A victim being destroyed already (a signal of its destructor) is not
            // deleted again: qobject_cast fails once the derived destructor has run
            if (deleteWindow)
            {
                if (!qobject_cast<pdf::PDFAppMainWindow*>(run.window.data()))
                {
                    return false;
                }
                if (isEngineInput)
                {
                    run.window->deleteLater();
                    return true;
                }
                delete run.window.data();
            }
            else
            {
                if (!qobject_cast<pdf::PDFAppDocumentTab*>(run.tab.data()))
                {
                    return false;
                }
                if (isEngineInput)
                {
                    run.tab->deleteLater();
                    return true;
                }
                delete run.tab.data();
            }
            return true;
        });
    }

    for (size_t i = 0; i < steps.size(); ++i)
    {
        recorder.setStep(int(i));
        recorder.hookTree(run.window.data());
        recorder.hookApplication();
        if (run.tab)
        {
            recorder.hookTree(run.tab->getDocument());
        }

        const bool isOk = steps[i](run);
        if (recorder.isFired())
        {
            break;
        }
        if (mode == AnnotationLifetimeRecorder::Mode::Discover)
        {
            QVERIFY2(run.tab && run.window, qPrintable(QStringLiteral("%1: the tab was lost in step %2").arg(name).arg(i)));
            QVERIFY2(isOk, qPrintable(QStringLiteral("%1: step %2 failed").arg(name).arg(i)));
        }
        else if (!run.tab || !run.window)
        {
            break;
        }
    }

    recorder.setStep(int(steps.size()));
    pdftest::AppDocumentFixture::settle();
    if (mode == AnnotationLifetimeRecorder::Mode::Replay && recorder.isFired())
    {
        // The deferred work of the deleted objects finds nothing; the window keeps
        // working after its tab was deleted
        QTest::qWait(50);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!run.tab);
        if (!deleteWindow)
        {
            QVERIFY(run.window);
            QVERIFY(fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("After.pdf"), 1)));
            pdftest::AppDocumentFixture::settle();
        }
        else
        {
            QVERIFY(!run.window);
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
}

void AppAnnotationLifetimeTest::deleteFromEverySignal()
{
    QFETCH(QString, scenario);
    QFETCH(bool, deleteWindow);
    QFETCH(int, part);

    std::vector<AnnotationLifetimeRecorder::Site> sites;
    runScenario(scenario, AnnotationLifetimeRecorder::Mode::Discover, AnnotationLifetimeRecorder::Site(), deleteWindow, &sites, nullptr);
    if (QTest::currentTestFailed())
    {
        return;
    }
    QVERIFY(!sites.empty());

    // PDF4QT_LIFETIME_SITE=<n> replays one site only (debugging)
    const int onlySite = qEnvironmentVariableIsSet("PDF4QT_LIFETIME_SITE") ? qEnvironmentVariableIntValue("PDF4QT_LIFETIME_SITE") : -1;
    QStringList notReproduced;
    int replayed = 0;
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
        runScenario(scenario, AnnotationLifetimeRecorder::Mode::Replay, sites[i], deleteWindow, nullptr, &isFired);
        if (QTest::currentTestFailed())
        {
            qWarning().noquote() << QStringLiteral("failed at site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
            return;
        }
        if (isFired)
        {
            ++replayed;
        }
        else
        {
            notReproduced << QStringLiteral("step %1: %2").arg(sites[i].step).arg(sites[i].key);
        }
    }

    qInfo().noquote() << QStringLiteral("%1: %2 sites, %3 deletions in part %4").arg(scenario).arg(sites.size()).arg(replayed).arg(part);
    for (const QString& site : notReproduced)
    {
        qInfo().noquote() << QStringLiteral("not reproduced (the emission differs in the run or the victim was being destroyed): ") + site;
    }

    // Almost every site is reproduced (a site of a timer or of the order of the
    // events may move to another step)
    QVERIFY2(onlySite >= 0 || replayed * 10 >= (replayed + int(notReproduced.size())) * 8, qPrintable(notReproduced.join(QLatin1Char('\n'))));
}

QTEST_MAIN(AppAnnotationLifetimeTest)

#include "tst_appannotationlifetimetest.moc"
