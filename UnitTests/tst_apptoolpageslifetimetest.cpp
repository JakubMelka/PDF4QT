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

// Generic lifetime test of the tools in a document tab (plan of the first batch of phase
// 2, chapter 8, the pattern of UnitTestsAppToolsLifetime): the scenarios of a tool - the
// parameters and the panels, the run with Stop, the result dialog with Keep, Discard and a
// copy, the leaving by a navigation (F14), the lock, a plain navigation - run once with a
// recorder of every signal of every object of the application (classes pdf::PDFApp* and
// pdf::PDFDocumentTool*), then once more for each signal it has seen, with the tab, the
// window, the page of the tool or its shell deleted synchronously from that emission.
// Nothing may crash (run it under ASan: freed memory stays readable in a Release build on
// Windows), the window and the tab work afterwards. A second test keeps each popup of the
// tool open, while a translator is installed, the document or the theme changes. The
// scenarios parameters, run and leave use the test tool, the others the real tools of the
// first batch (Compress, Rotate and Remove Pages, Remove Hidden Data, Remove Links, Optimize
// Structure, Extract Text) through their own panels, menus and result dialogs; the tools of
// the next batches add their scenarios.

#include "apptoolshellfixture.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptooltoolbar.h"

#include "pdfcomponentbuttons.h"
#include "pdfdatabasenotifier.h"
#include "pdfapptoolbeforeafteroverlay.h"
#include "pdfapptoolgui_compress.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolgui_structure.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"
#include "pdfappworkspace.h"
#include "pdfapptoolresultdialog.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenthiddendatascan.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QHash>
#include <QLineEdit>
#include <QMetaMethod>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QtTest>

#include <functional>
#include <memory>
#include <vector>

class ToolPagesLifetimeRecorder;

/// Application of the test: it knows the events being delivered (QApplication::notify),
/// so the recorder can tell an emission inside of the key handling of a QLineEdit
class ToolPagesLifetimeApplication : public QApplication
{
public:
    ToolPagesLifetimeApplication(int& argc, char** argv) :
        QApplication(argc, argv)
    {
    }

    virtual bool notify(QObject* receiver, QEvent* event) override
    {
        // Events, whose delivery Qt does not survive, when the receiving widget is deleted
        // inside of it: QWidgetPrivate::show_helper / hide_helper use the widget after its
        // QShowEvent / QHideEvent, QWidget::event sends LanguageChange to the children by
        // a copied list of raw pointers after changeEvent, QWidgetPrivate::setGeometry_sys and
        // the layout of the parent (QLayoutPrivate::doResize) use the widget after its
        // QResizeEvent / QMoveEvent, QObjectPrivate::setParent_helper the child after the
        // QChildEvent of its old parent (the pattern of UnitTestsAppPagesLifetime)
        const QEvent::Type type = event->type();
        const QScopedValueRollback<int> languageDepth(s_languageChangeDepth, s_languageChangeDepth + (type == QEvent::LanguageChange ? 1 : 0));
        if ((type == QEvent::Show || type == QEvent::Hide || type == QEvent::LanguageChange || type == QEvent::Resize || type == QEvent::Move ||
             type == QEvent::ChildRemoved || type == QEvent::ChildAdded || type == QEvent::ParentChange) && receiver && receiver->isWidgetType())
        {
            s_stateEventReceivers.push_back(QPointer<QObject>(receiver));
            const bool result = QApplication::notify(receiver, event);
            s_stateEventReceivers.pop_back();
            return result;
        }

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

    /// The language changes: QWidget::event of any ancestor of the parts of a tool sends
    /// the event to its children by a copied list of raw pointers
    static bool isLanguageChangeInside() { return s_languageChangeDepth > 0; }

    /// A state event is being delivered to a widget, which the deletion of the object
    /// deletes (the widget itself or one of its ancestors)
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
    static std::vector<QPointer<QObject>> s_stateEventReceivers;
    static int s_languageChangeDepth;
};

std::vector<QPointer<QLineEdit>> ToolPagesLifetimeApplication::s_keyedLineEdits;
std::vector<QPointer<QObject>> ToolPagesLifetimeApplication::s_stateEventReceivers;
int ToolPagesLifetimeApplication::s_languageChangeDepth = 0;

/// One connection of the recorder to one signal of one object: the slot is the
/// first method after the methods of QObject (the pattern of QSignalSpy)
class ToolPagesLifetimeHook : public QObject
{
public:
    explicit ToolPagesLifetimeHook(ToolPagesLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature);

    virtual int qt_metacall(QMetaObject::Call call, int id, void** arguments) override;

private:
    ToolPagesLifetimeRecorder* m_recorder;
    QPointer<QObject> m_sender;
    QByteArray m_signature;
};

/// Recorder of the signals of the objects of the application. In the discovery
/// mode it collects the sites (signal of an object, step of the scenario), in the
/// replay mode it deletes the victim at the first emission of the target site.
class ToolPagesLifetimeRecorder : public QObject
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

    explicit ToolPagesLifetimeRecorder(Mode mode) :
        m_mode(mode)
    {
        qApp->installEventFilter(this);
    }

    virtual ~ToolPagesLifetimeRecorder() override
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
        return ToolPagesLifetimeApplication::isLineEditKeyInside(victim) || ToolPagesLifetimeApplication::isWidgetStateEventInside(victim);
    }

    void setStep(int step) { m_step = step; }
    bool isFired() const { return m_isFired; }
    bool isDeferredFired() const { return m_isFired && m_isDeferredFired; }

    /// The emission of the target site has come (the victim may have not existed at it)
    bool isTargetReached() const { return m_isTargetReached; }
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

            ToolPagesLifetimeHook* hook = new ToolPagesLifetimeHook(this, object, method.methodSignature());
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

        // A signal emitted from QAbstractItemModel::data, which a delegate calls while it
        // paints or measures its rows (the pattern of UnitTestsAppAnnotationLifetime)
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
            m_isTargetReached = true;
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
            if (isConstructionEvent(event->type()))
            {
                // The object may be in its constructor: it is hooked from the event loop. A
                // signal of a constructor reaches no receiver of the application, the
                // recorder would delete the parent of a half-built object (the pattern
                // of UnitTestsAppPagesLifetime).
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
        return className.startsWith("pdf::PDFApp") || className.startsWith("pdf::PDFDocumentTool");
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
    bool m_isTargetReached = false;
    bool m_isInAction = false;
    bool m_isStopped = false;
    Site m_target;
    std::function<Action(const Site&)> m_action;
    std::vector<Site> m_sites;
    QSet<QString> m_seen;
    QHash<QObject*, QPointer<QObject>> m_hooked;
    QPointer<QWidget> m_paintedWidget;
};

ToolPagesLifetimeHook::ToolPagesLifetimeHook(ToolPagesLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature) :
    QObject(recorder),
    m_recorder(recorder),
    m_sender(sender),
    m_signature(signature)
{
}

int ToolPagesLifetimeHook::qt_metacall(QMetaObject::Call call, int id, void** arguments)
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
class ToolPagesLifetimeTranslator : public QTranslator
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
struct ToolPagesLifetimeRun
{
    pdftest::AppToolShellFixture* fixture = nullptr;
    std::shared_ptr<pdftest::ShellTestTool> tool;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppDocumentTab> tab;
    QString toolId;
};

using ToolPagesLifetimeStep = std::function<bool(ToolPagesLifetimeRun&)>;

/// Victim of the deletion
enum class ToolPagesLifetimeVictim
{
    Tab,        ///< The tab of the document
    Window,
    Page,       ///< The page of the tool
    Shell       ///< The shell of the tool
};

/// Helpers of the scenarios: every input is sent through guards, the victim may be
/// deleted inside of any event
class ToolPagesLifetimeHelper
{
public:
    ToolPagesLifetimeHelper() = delete;

    static void settle() { pdftest::AppDocumentFixture::settle(); }

    static pdf::PDFAppToolDocumentPage* page(const ToolPagesLifetimeRun& run)
    {
        return run.tab ? qobject_cast<pdf::PDFAppToolDocumentPage*>(run.tab->getPage()) : nullptr;
    }

    static pdf::PDFDocumentToolShell* shell(const ToolPagesLifetimeRun& run)
    {
        return pdftest::AppToolShellFixture::getShell(run.tab.data());
    }

    static pdftest::ShellTestGUI* gui(const ToolPagesLifetimeRun& run)
    {
        return pdftest::AppToolShellFixture::getGUI(shell(run));
    }

    static pdf::PDFAppToolToolbar* toolbar(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* current = shell(run);
        return current ? current->getToolbar() : nullptr;
    }

    /// An assistant removes the first page below the open tool (pages.md): the marks of
    /// Rotate Pages and Remove Pages follow their pages, the model announces the changes
    static bool removeFirstPage(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
        if (!document || !document->getDocument() || document->getPageCount() < 2)
        {
            return false;
        }
        const pdf::PDFDocumentPointer changed(new pdf::PDFDocument(pdf::PDFDocumentPageOperations::removePages(document->getDocument().data(), { 0 }, nullptr, nullptr)));
        document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp);
        settle();
        return true;
    }

    static bool openTool(ToolPagesLifetimeRun& run)
    {
        if (!run.window || !run.tab || !run.window->getNavigator())
        {
            return false;
        }
        run.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(run.tab->getDocumentId(), run.toolId));
        settle();
        return shell(run) != nullptr;
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
            QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, point);
        }
        settle();
        return true;
    }

    static bool key(QWidget* widget, int keyCode, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        if (!widget)
        {
            return false;
        }
        QTest::keyClick(widget, Qt::Key(keyCode), modifiers);
        settle();
        return true;
    }

    /// Waits for a condition, while the run has its tab
    static bool wait(const ToolPagesLifetimeRun& run, const std::function<bool()>& condition)
    {
        return pdftest::AppToolShellFixture::waitFor([&run, &condition]() { return !run.window || !run.tab || condition(); }) && run.window && run.tab;
    }

    /// Answers the dialog of the leaving by its button from the event loop
    static void answerLater(const ToolPagesLifetimeRun& run, const QString& buttonName)
    {
        if (!run.window)
        {
            return;
        }
        QTimer::singleShot(0, run.window.data(), [buttonName]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                pdf::PDFAppDocumentChoiceDialog* dialog = qobject_cast<pdf::PDFAppDocumentChoiceDialog*>(widget);
                if (dialog && dialog->isVisible())
                {
                    if (QPushButton* button = dialog->findChild<QPushButton*>(buttonName))
                    {
                        button->click();
                    }
                }
            }
        });
    }

    static bool runToHold(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolToolbar* bar = toolbar(run);
        if (!bar)
        {
            return false;
        }
        run.tool->setHold(true);
        click(bar->getConfirmButton());
        return wait(run, [&run]() { return run.tool->getHeldCount() >= 1; });
    }

    static void releaseHold(ToolPagesLifetimeRun& run)
    {
        run.tool->setHold(false);
        pdftest::AppToolShellFixture::waitFor([&run]() { return run.tool->getHeldCount() == 0; });
        settle();
    }

    /// The Primary button, then the dialog
    static pdf::PDFAppToolResultDialog* runToDialog(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolToolbar* bar = toolbar(run);
        if (!bar || !click(bar->getConfirmButton()))
        {
            return nullptr;
        }
        wait(run, [&run]() { pdf::PDFDocumentToolShell* current = shell(run); return !current || current->getResultDialog(); });
        settle();
        pdf::PDFDocumentToolShell* current = shell(run);
        return current ? current->getResultDialog() : nullptr;
    }

    /// The scenarios of the test tool; the scenarios of the tools added by the next waves
    /// are new names here (plan of the first batch, chapter 8)
    static std::vector<ToolPagesLifetimeStep> scenario(const QString& name);

    /// The GUI of the real tool Compress, nullptr = none or gone
    static pdf::PDFAppToolGUICompress* compressGui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* current = shell(run);
        return current ? qobject_cast<pdf::PDFAppToolGUICompress*>(current->getGUI()) : nullptr;
    }

    /// Waits for the end of the measuring; the victim may go meanwhile
    static bool waitCompressMeasured(ToolPagesLifetimeRun& run)
    {
        return pdftest::AppToolShellFixture::waitFor([&run]()
        {
            const pdf::PDFAppToolGUICompress* current = compressGui(run);
            return !current || current->getMeasureState() != pdf::PDFAppToolGUICompress::MeasureState::Measuring;
        }) && compressGui(run) != nullptr;
    }

    /// A PDF with an image, which the presets of Compress reduce
    static QString writePhoto(const QString& filePath)
    {
        QPdfWriter writer(filePath);
        writer.setResolution(72);
        QPainter painter(&writer);
        QImage image(600, 600, QImage::Format_RGB32);
        QRandomGenerator generator(5);
        for (int y = 0; y < image.height(); ++y)
        {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                line[x] = qRgb(100 + generator.bounded(60), 80 + generator.bounded(60), 60 + generator.bounded(60));
            }
        }
        painter.drawImage(QRectF(50, 50, 200, 200), image);
        return filePath;
    }

    /// The GUI of a real tool of the batch, nullptr = none or gone
    template<typename GUI>
    static GUI* realGui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* current = shell(run);
        return current ? qobject_cast<GUI*>(current->getGUI()) : nullptr;
    }

    /// Clicks the tile of the page in the grid of Rotate / Remove Pages by the mouse
    static bool clickTile(const ToolPagesLifetimeRun& run, int page, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        pdf::PDFAppPageMarkingToolGUI* current = realGui<pdf::PDFAppPageMarkingToolGUI>(run);
        pdf::PDFAppToolPageGridView* view = current && current->getGrid() ? current->getGrid()->getView() : nullptr;
        if (!view || page >= view->model()->rowCount())
        {
            return false;
        }
        QPointer<pdf::PDFAppToolPageGridView> guard(view);
        view->scrollTo(view->model()->index(page, 0));
        settle();
        if (!guard)
        {
            return true;
        }
        const QPoint center = guard->visualRect(guard->model()->index(page, 0)).center();
        QTest::mouseClick(guard->viewport(), Qt::LeftButton, modifiers, center);
        settle();
        return true;
    }

    /// Clicks the row of a list view by the mouse
    static bool clickRow(QAbstractItemView* view, int row)
    {
        if (!view || !view->model() || row >= view->model()->rowCount())
        {
            return false;
        }
        QPointer<QAbstractItemView> guard(view);
        view->scrollTo(view->model()->index(row, 0));
        settle();
        if (guard)
        {
            QTest::mouseClick(guard->viewport(), Qt::LeftButton, Qt::NoModifier, guard->visualRect(guard->model()->index(row, 0)).center());
            settle();
        }
        return true;
    }

    /// The button of the toolbar of the tool, whose text starts by \p textStart
    static QToolButton* findToolButton(const ToolPagesLifetimeRun& run, const QString& textStart)
    {
        pdf::PDFDocumentToolShell* current = shell(run);
        if (!current || !current->getToolActions())
        {
            return nullptr;
        }
        for (QToolButton* button : current->getToolActions()->findChildren<QToolButton*>())
        {
            if (button->text().startsWith(textStart))
            {
                return button;
            }
        }
        return nullptr;
    }

    /// Opens the panel of the button of the toolbar by the mouse, then clicks its row \p row
    static bool clickPanelRow(const ToolPagesLifetimeRun& run, QToolButton* button, int row)
    {
        if (!click(button))
        {
            return false;
        }
        pdf::PDFAppToolPanelPopup* panel = toolbar(run) ? toolbar(run)->getOpenPanel() : nullptr;
        return panel && row < int(panel->getRows().size()) && click(panel->getRows()[row]);
    }

    /// Esc on the open panel of the toolbar, nothing without a panel
    static bool closePanel(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolPanelPopup* panel = toolbar(run) ? toolbar(run)->getOpenPanel() : nullptr;
        if (panel && panel->isVisible())
        {
            key(panel, Qt::Key_Escape);
        }
        return true;
    }

    /// Waits for the result dialog of the shell after the Primary button, nullptr = none
    static pdf::PDFAppToolResultDialog* confirmToDialog(ToolPagesLifetimeRun& run)
    {
        if (!click(toolbar(run) ? toolbar(run)->getConfirmButton() : nullptr))
        {
            return nullptr;
        }
        wait(run, [&run]() { pdf::PDFDocumentToolShell* current = shell(run); return !current || (current->getResultDialog() && current->getResultDialog()->isVisible()); });
        settle();
        pdf::PDFDocumentToolShell* current = shell(run);
        return current ? current->getResultDialog() : nullptr;
    }

    /// The confirmation button of the result dialog by the mouse, then the end of the shell
    /// (the write of a file ends within the click or after it, both in this step)
    static bool confirmDialog(ToolPagesLifetimeRun& run, pdf::PDFAppToolResultDialog* dialog)
    {
        if (!dialog || !click(dialog->getConfirmButton()))
        {
            return false;
        }
        wait(run, [&run]() { return !shell(run); });
        settle();
        return true;
    }

    /// Save as a copy into the folder of the test, the copy not opened
    static bool confirmCopy(ToolPagesLifetimeRun& run, pdf::PDFAppToolResultDialog* dialog)
    {
        if (!dialog)
        {
            return false;
        }
        QPointer<pdf::PDFAppToolResultDialog> guard(dialog);
        guard->selectChoice(pdf::PDFAppToolResultDialog::Choice::Copy);
        static int s_counter = 0;
        if (guard && guard->getFileNameField())
        {
            guard->getFileNameField()->setText(QStringLiteral("lifetime-tool-copy-%1.pdf").arg(++s_counter));
        }
        if (guard && guard->getOpenCheckBox())
        {
            guard->getOpenCheckBox()->setChecked(false);
        }
        return !guard || confirmDialog(run, guard.data());
    }

    /// A PDF, which Optimize Structure makes smaller: an unused object and two identical streams
    static QString writeReducible(const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 2; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            pdf::PDFDictionaryBuilder content;
            const QByteArray data = "1 0 0 rg 10 10 80 80 re f";
            content.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
            const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(content), QByteArray(data))));
            pdf::PDFDictionaryBuilder update;
            update.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(update)));
        }
        builder.addObject(pdf::PDFObject::createString(QByteArray("An object, which nothing references")));
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// The tool of a scenario: the real tools of the batch by their names, the test tool
    /// under the id of Rotate Pages otherwise
    static QString getScenarioToolId(const QString& name)
    {
        static const std::pair<const char*, const char*> s_tools[] = { { "compress", "optimize.compress" }, { "compressrun", "optimize.compress" },
                                                                         { "rotate", "pages.rotate" }, { "remove", "pages.remove" },
                                                                         { "sanitize", "protect.sanitize" }, { "removelinks", "protect.remove-links" },
                                                                         { "structure", "optimize.structure" }, { "extracttext", "convert.extract-text" } };
        for (const auto& [scenario, toolId] : s_tools)
        {
            if (name == QLatin1String(scenario))
            {
                return QString::fromLatin1(toolId);
            }
        }
        return QString();
    }

    /// The document of a scenario of a real tool (the test tool: three plain pages)
    static pdf::PDFAppDocumentTab* openScenarioDocument(pdftest::AppToolShellFixture& fixture, const QString& name)
    {
        if (name.startsWith(QLatin1String("compress")))
        {
            return fixture.open(writePhoto(fixture.path(QStringLiteral("photo.pdf"))));
        }
        if (name == QLatin1String("rotate") || name == QLatin1String("remove"))
        {
            return fixture.openDocument(6);
        }
        if (name == QLatin1String("sanitize"))
        {
            return fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
        }
        if (name == QLatin1String("removelinks"))
        {
            return fixture.open(pdftest::AppDocumentSamples::links(fixture.folder()));
        }
        if (name == QLatin1String("structure"))
        {
            return fixture.open(writeReducible(fixture.path(QStringLiteral("reducible.pdf"))));
        }
        return fixture.openDocument(3);
    }

    /// Brings a real tool to a state, where its Primary button runs it (the panels of
    /// openPopupsSurviveChanges)
    static bool prepareRun(ToolPagesLifetimeRun& run)
    {
        if (pdf::PDFAppToolGUIRotatePages* rotate = realGui<pdf::PDFAppToolGUIRotatePages>(run))
        {
            rotate->rotateRight();
            settle();
            return true;
        }
        if (pdf::PDFAppToolGUISanitize* sanitize = realGui<pdf::PDFAppToolGUISanitize>(run))
        {
            const QPointer<pdf::PDFAppToolGUISanitize> guard(sanitize);
            return wait(run, [&guard]() { return !guard || guard->isScanned(); }) && guard;
        }
        if (pdf::PDFAppToolGUIRemoveLinks* links = realGui<pdf::PDFAppToolGUIRemoveLinks>(run))
        {
            const QPointer<pdf::PDFAppToolGUIRemoveLinks> guard(links);
            return wait(run, [&guard]() { return !guard || guard->isListed(); }) && guard;
        }
        return true;
    }

    /// Waits for the previews of Extract Text: the estimate of the result and the text of the
    /// shown page; the victim may go meanwhile
    static bool waitExtractPreviews(ToolPagesLifetimeRun& run)
    {
        return pdftest::AppToolShellFixture::waitFor([&run]()
        {
            const pdf::PDFAppToolGUIExtractText* current = realGui<pdf::PDFAppToolGUIExtractText>(run);
            return !current || (current->getResultRows() && current->getResultRows()->getRowCount() == 3 && current->getResultRows()->getValue(0) != QStringLiteral("–") &&
                                current->getPreviewPage() && !current->getPreviewPage()->getTextLabel()->text().isEmpty());
        }) && realGui<pdf::PDFAppToolGUIExtractText>(run) != nullptr;
    }

    /// The first panel button of the toolbar of a real tool
    static QToolButton* firstPanelButton(const ToolPagesLifetimeRun& run)
    {
        const std::vector<QToolButton*> buttons = toolbar(run) ? toolbar(run)->getPanelButtons() : std::vector<QToolButton*>();
        return buttons.empty() ? nullptr : buttons.front();
    }
};

std::vector<ToolPagesLifetimeStep> ToolPagesLifetimeHelper::scenario(const QString& name)
{
    using H = ToolPagesLifetimeHelper;
    std::vector<ToolPagesLifetimeStep> steps;

    if (name == QLatin1String("parameters"))
    {
        // The tool open: every part changes the model, Undo, the panels, a language
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdftest::ShellTestGUI* current = gui(run);
            if (!current)
            {
                return false;
            }
            QPointer<pdf::PDFSegmentedControl> segments = current->segments->getSegmentedControl();
            segments->setCurrentIndex(0);
            settle();
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getUndoButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getRedoButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdftest::ShellTestGUI* current = gui(run);
            if (!current || !click(current->flagsMenu->getButton()))
            {
                return false;
            }
            current = gui(run);
            pdf::PDFAppToolPanelPopup* panel = current ? current->flagsMenu->getPanel() : nullptr;
            return panel && panel->isVisible() && click(panel->getRows().front());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdftest::ShellTestGUI* current = gui(run);
            pdf::PDFAppToolPanelPopup* panel = current ? current->flagsMenu->getPanel() : nullptr;
            pdf::PDFTextLink* link = panel ? panel->findChild<pdf::PDFTextLink*>() : nullptr;
            return link && click(link);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The neighbor by the arrow, Esc
            pdftest::ShellTestGUI* current = gui(run);
            pdf::PDFAppToolPanelPopup* panel = current ? current->flagsMenu->getPanel() : nullptr;
            if (!panel || !key(panel, Qt::Key_Right))
            {
                return false;
            }
            current = gui(run);
            panel = current ? current->enumMenu->getPanel() : nullptr;
            return panel && key(panel, Qt::Key_Escape);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // A choice of the Enum menu
            pdftest::ShellTestGUI* current = gui(run);
            if (!current || !click(current->enumMenu->getButton()))
            {
                return false;
            }
            current = gui(run);
            return current && click(current->enumMenu->getRow(QStringLiteral("title")));
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdftest::ShellTestGUI* current = gui(run);
            return current && click(current->selectAllAction);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The page selection: a range, an error, a range
            pdftest::ShellTestGUI* current = gui(run);
            if (!current)
            {
                return false;
            }
            QPointer<pdf::PDFPageSelection> selection = current->pageSelection;
            selection->setMode(pdf::PDFPageSelection::Mode::Range);
            settle();

            // Typed by the keys: Qt does not survive the deletion of a line edit inside of its
            // own setText, the emissions inside of the keys are deferred (isDeferredState)
            for (const char* text : { "1-x", "1-2" })
            {
                QPointer<QLineEdit> field = selection ? selection->getRangeField() : nullptr;
                if (field)
                {
                    field->selectAll();
                }
                for (const char* character = text; *character && field; ++character)
                {
                    QTest::keyClick(field.data(), *character);
                }
                settle();
            }
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // A change of the language: the parts come again
            ToolPagesLifetimeTranslator translator;
            QCoreApplication::installTranslator(&translator);
            settle();
            QCoreApplication::removeTranslator(&translator);
            settle();
            return run.window != nullptr;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The sidebar command of the toolbar and the document changing below the tool
            if (!click(toolbar(run) ? toolbar(run)->getSidebarButton() : nullptr))
            {
                return false;
            }
            if (run.tab && run.tab->getDocument())
            {
                pdf::PDFAppDocument* document = run.tab->getDocument();
                document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp);
                settle();
            }
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getCancelButton() : nullptr); });
    }
    else if (name == QLatin1String("run"))
    {
        // The run: Stop, Esc, Keep, Discard, a copy
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            if (!runToHold(run))
            {
                return false;
            }
            const bool isClicked = click(toolbar(run) ? toolbar(run)->getStopButton() : nullptr);
            releaseHold(run);
            return isClicked;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            if (!runToHold(run))
            {
                return false;
            }
            pdf::PDFDocumentToolShell* current = shell(run);
            if (current)
            {
                current->stop();
            }
            releaseHold(run);
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* current = shell(run);
            return current && current->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title")).isEmpty();
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // Keep in this tab ends the tool
            pdf::PDFAppToolResultDialog* dialog = runToDialog(run);
            if (!dialog)
            {
                return false;
            }
            // The choice may delete the dialog with its window (a deletion from its signal)
            QPointer<pdf::PDFAppToolResultDialog> guard(dialog);
            guard->selectChoice(pdf::PDFAppToolResultDialog::Choice::Keep);
            if (guard)
            {
                guard->confirm();
            }
            settle();
            return true;
        });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* current = shell(run);
            return current && current->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title")).isEmpty();
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // Discard
            pdf::PDFAppToolResultDialog* dialog = runToDialog(run);
            return dialog && click(dialog->getDiscardButton());
        });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // Save as a copy, with the file in the folder of the test
            pdf::PDFDocumentToolShell* current = shell(run);
            if (!current || !current->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title")).isEmpty())
            {
                return false;
            }
            pdf::PDFAppToolResultDialog* dialog = runToDialog(run);
            if (!dialog)
            {
                return false;
            }
            // Each input may delete the dialog with its window
            QPointer<pdf::PDFAppToolResultDialog> guard(dialog);
            guard->selectChoice(pdf::PDFAppToolResultDialog::Choice::Copy);
            static int s_counter = 0;
            if (guard && guard->getFileNameField())
            {
                guard->getFileNameField()->setText(QStringLiteral("lifetime-copy-%1.pdf").arg(++s_counter));
            }
            if (guard && guard->getOpenCheckBox())
            {
                guard->getOpenCheckBox()->setChecked(false);
            }
            if (guard)
            {
                guard->confirm();
            }
            wait(run, [&guard]() { return !guard; });
            settle();
            return true;
        });
    }
    else if (name == QLatin1String("compress"))
    {
        // The real tool Compress: the measuring, a preset, the panel Custom…, the handle of the
        // overlay, the result dialog
        steps.push_back(&H::openTool);
        steps.push_back(&H::waitCompressMeasured);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUICompress* current = compressGui(run);
            const QList<QToolButton*> buttons = current ? current->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>() : QList<QToolButton*>();
            return !buttons.isEmpty() && click(buttons.front());
        });
        steps.push_back(&H::waitCompressMeasured);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUICompress* current = compressGui(run);
            const QList<QToolButton*> buttons = current ? current->getPresetControl()->getSegmentedControl()->findChildren<QToolButton*>() : QList<QToolButton*>();
            if (buttons.isEmpty() || !click(buttons.back()))
            {
                return false;
            }
            settle();
            current = compressGui(run);
            return current && current->getCustomPanel() && current->getCustomPanel()->isVisible();
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUICompress* current = compressGui(run);
            pdf::PDFAppSettingControl* control = current && current->getCustomPanel() ? current->getCustomPanel()->getControl(QStringLiteral("cleanStructure")) : nullptr;
            return control && click(control->getCheckBox());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUICompress* current = compressGui(run);
            return current && current->getCustomPanel() && key(current->getCustomPanel(), Qt::Key_Escape);
        });
        steps.push_back(&H::waitCompressMeasured);
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getUndoButton() : nullptr); });
        steps.push_back(&H::waitCompressMeasured);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUICompress* current = compressGui(run);
            return current && current->getBeforeAfterOverlay() && key(current->getBeforeAfterOverlay(), Qt::Key_Right);
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getConfirmButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            settle();
            pdf::PDFDocumentToolShell* current = shell(run);
            pdf::PDFAppToolResultDialog* dialog = current ? current->getResultDialog() : nullptr;
            return dialog && click(dialog->getDiscardButton());
        });
    }
    else if (name == QLatin1String("compressrun"))
    {
        // Compress with the measuring running: a run started during the delay and stopped, a run
        // joining the computation of the preview, its dialog, the write of a copy
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getConfirmButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The run may have ended already: then there is nothing to stop
            if (toolbar(run) && toolbar(run)->getStopButton()->isVisible())
            {
                click(toolbar(run)->getStopButton());
            }
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            settle();
            pdf::PDFDocumentToolShell* current = shell(run);
            pdf::PDFAppToolResultDialog* dialog = current ? current->getResultDialog() : nullptr;
            if (dialog)
            {
                return click(dialog->getDiscardButton());
            }
            return current != nullptr;
        });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // Past the delay of the preview: its computation runs, the run joins it
            QTest::qWait(pdf::PDFDocumentToolRunner::PREVIEW_DELAY + 10);
            return click(toolbar(run) ? toolbar(run)->getConfirmButton() : nullptr);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            wait(run, [&run]() { return !shell(run) || shell(run)->getResultDialog() != nullptr; });
            settle();
            pdf::PDFDocumentToolShell* current = shell(run);
            pdf::PDFAppToolResultDialog* dialog = current ? current->getResultDialog() : nullptr;
            if (!dialog)
            {
                return false;
            }
            dialog->getOpenCheckBox()->setChecked(false);
            if (!click(dialog->getConfirmButton()))
            {
                return false;
            }

            // One step with its click: the write ends within the click or after it by the load of the
            // computer, a site of another step would not be found again by the replay
            wait(run, [&run]() { return !shell(run); });
            settle();
            return true;
        });
    }
    else if (name == QLatin1String("leave"))
    {
        // F14 and the end of the tool without a click on it
        steps.push_back(&H::openTool);
        steps.push_back(&H::runToHold);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            answerLater(run, QStringLiteral("keepRunningButton"));
            if (run.window && run.tab)
            {
                run.window->getNavigator()->goBack(run.tab);
            }
            settle();
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            answerLater(run, QStringLiteral("stopAndLeaveButton"));
            if (run.window && run.tab)
            {
                run.window->getNavigator()->goBack(run.tab);
            }
            settle();
            releaseHold(run);
            return true;
        });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // A lock: Unavailable ends the tool
            if (run.tab && run.tab->getDocument())
            {
                run.tab->getDocument()->setLocked(true);
            }
            settle();
            if (run.tab && run.tab->getDocument())
            {
                run.tab->getDocument()->setLocked(false);
            }
            settle();
            return true;
        });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // A plain navigation is Cancel
            if (run.window && run.tab)
            {
                run.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(run.tab->getDocumentId()));
            }
            settle();
            return true;
        });
    }
    else if (name == QLatin1String("rotate"))
    {
        // Rotate Pages: the tiles by the mouse, the buttons of the turns, Undo / Redo, a pending
        // row, the result dialog Keep, then Discard of a second run
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run) { return clickTile(run, 3) && clickTile(run, 5, Qt::ShiftModifier); });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The size of the tiles (owner 2026-11-07): the panel Thumbnails, Large - the grid is laid out again
            pdf::PDFAppToolGUIRotatePages* current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            if (!current || !click(current->getTileSizeButton()))
            {
                return false;
            }
            current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            pdf::PDFTileRow* row = current ? current->getTileSizeRow(pdf::PDFAppToolPageGrid::TileSize::Large) : nullptr;
            return row && click(row);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRotatePages* current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            return current && click(current->getRotateRightButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRotatePages* current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            return current && click(current->getRotateHalfButton());
        });
        steps.push_back(&H::removeFirstPage);
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getUndoButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getRedoButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // Undo of a pending row (the link of the row)
            pdf::PDFAppToolGUIRotatePages* current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            if (!current || current->getPendingModel()->rowCount() == 0)
            {
                return false;
            }
            Q_EMIT current->getPendingList()->linkActivated(current->getPendingModel()->index(0));
            settle();
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRotatePages* current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            return current && click(current->getRotateLeftButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return confirmDialog(run, confirmToDialog(run)); });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRotatePages* current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            if (!current || !click(current->getRotateRightButton()))
            {
                return false;
            }
            current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            if (!current || !click(current->getResetButton()))
            {
                return false;
            }
            current = realGui<pdf::PDFAppToolGUIRotatePages>(run);
            return current && click(current->getRotateRightButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolResultDialog* dialog = confirmToDialog(run);
            return dialog && click(dialog->getDiscardButton());
        });
    }
    else if (name == QLatin1String("remove"))
    {
        // Remove Pages: the tiles by the mouse and Delete, Keep of a pending row, Keep All Pages,
        // Undo, a copy of the result
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run) { return clickTile(run, 2) && clickTile(run, 4, Qt::ShiftModifier); });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The size of the tiles: the panel Thumbnails, Small
            pdf::PDFAppToolGUIRemovePages* current = realGui<pdf::PDFAppToolGUIRemovePages>(run);
            if (!current || !click(current->getTileSizeButton()))
            {
                return false;
            }
            current = realGui<pdf::PDFAppToolGUIRemovePages>(run);
            pdf::PDFTileRow* row = current ? current->getTileSizeRow(pdf::PDFAppToolPageGrid::TileSize::Small) : nullptr;
            return row && click(row);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemovePages* current = realGui<pdf::PDFAppToolGUIRemovePages>(run);
            if (!current || current->getPendingModel()->rowCount() < 2)
            {
                return false;
            }
            Q_EMIT current->getPendingList()->linkActivated(current->getPendingModel()->index(1));
            settle();
            return true;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // Delete marks the current tile
            pdf::PDFAppToolGUIRemovePages* current = realGui<pdf::PDFAppToolGUIRemovePages>(run);
            pdf::PDFAppToolPageGridView* view = current ? current->getGrid()->getView() : nullptr;
            if (!view)
            {
                return false;
            }
            view->setFocus();
            view->setCurrentIndex(view->model()->index(5, 0));
            return key(view, Qt::Key_Delete);
        });
        steps.push_back(&H::removeFirstPage);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemovePages* current = realGui<pdf::PDFAppToolGUIRemovePages>(run);
            return current && click(current->getKeepAllButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getUndoButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run) { return confirmCopy(run, confirmToDialog(run)); });
    }
    else if (name == QLatin1String("sanitize"))
    {
        // Remove Hidden Data: the scan, the panel Preset, a kind by its check box, Undo, the
        // result dialog with the steps beside it, a copy (the only choice of LostCopyOnly besides Keep)
        steps.push_back(&H::openTool);
        steps.push_back(&H::prepareRun);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUISanitize* current = realGui<pdf::PDFAppToolGUISanitize>(run);
            if (!current || !click(current->getPresetButton()))
            {
                return false;
            }
            current = realGui<pdf::PDFAppToolGUISanitize>(run);
            pdf::PDFTileRow* row = current ? current->getPresetRow(pdf::PDFAppToolGUISanitize::Preset::Everything) : nullptr;
            return row && click(row);
        });
        steps.push_back(&H::closePanel);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUISanitize* current = realGui<pdf::PDFAppToolGUISanitize>(run);
            pdf::PDFTileRow* row = current ? current->getKindRow(pdf::PDFDocumentHiddenDataScan::Kind::Comments) : nullptr;
            return row && click(row->getCheckBox());
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return click(toolbar(run) ? toolbar(run)->getUndoButton() : nullptr); });
        steps.push_back([](ToolPagesLifetimeRun& run) { return confirmCopy(run, confirmToDialog(run)); });
    }
    else if (name == QLatin1String("removelinks"))
    {
        // Remove Links: the list, a check box of a row, the current row (the overlay shows its
        // page), the panel Show, Select None / All, the result dialog Keep
        steps.push_back(&H::openTool);
        steps.push_back(&H::prepareRun);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemoveLinks* current = realGui<pdf::PDFAppToolGUIRemoveLinks>(run);
            pdf::PDFAppToolLinkListModel* model = current ? current->getListModel() : nullptr;
            if (!model)
            {
                return false;
            }
            for (int row = 0; row < model->rowCount(); ++row)
            {
                if (model->getLink(row))
                {
                    model->setData(model->index(row), Qt::Unchecked, Qt::CheckStateRole);
                    settle();
                    return true;
                }
            }
            return false;
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemoveLinks* current = realGui<pdf::PDFAppToolGUIRemoveLinks>(run);
            return current && clickRow(current->getListView(), current->getListModel()->rowCount() - 1);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemoveLinks* current = realGui<pdf::PDFAppToolGUIRemoveLinks>(run);
            if (!current || !click(current->getShowButton()))
            {
                return false;
            }
            current = realGui<pdf::PDFAppToolGUIRemoveLinks>(run);
            pdf::PDFTileRow* row = current ? current->getFilterRow(pdf::PDFAppToolGUIRemoveLinks::Filter::Web) : nullptr;
            return row && click(row);
        });
        steps.push_back(&H::closePanel);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemoveLinks* current = realGui<pdf::PDFAppToolGUIRemoveLinks>(run);
            return current && click(current->getSelectNoneButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIRemoveLinks* current = realGui<pdf::PDFAppToolGUIRemoveLinks>(run);
            return current && click(current->getSelectAllButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return confirmDialog(run, confirmToDialog(run)); });
    }
    else if (name == QLatin1String("structure"))
    {
        // Optimize Structure: the panel of the steps, Restore defaults, Clear / Select all, the
        // result dialog with Copy report, Keep; a second run, which does not reduce the file
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIStructure* current = realGui<pdf::PDFAppToolGUIStructure>(run);
            return current && clickPanelRow(run, current->getStepsMenu()->getButton(), 4);
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIStructure* current = realGui<pdf::PDFAppToolGUIStructure>(run);
            return current && click(current->getRestoreDefaultsLink());
        });
        steps.push_back(&H::closePanel);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIStructure* current = realGui<pdf::PDFAppToolGUIStructure>(run);
            return current && click(current->getClearButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIStructure* current = realGui<pdf::PDFAppToolGUIStructure>(run);
            return current && click(current->getSelectAllButton());
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return confirmToDialog(run) != nullptr; });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolGUIStructure* current = realGui<pdf::PDFAppToolGUIStructure>(run);
            return current && click(current->getCopyReportLink());
        });
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* current = shell(run);
            return current && confirmDialog(run, current->getResultDialog());
        });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            // The kept result does not get smaller again: the card Result says it, no dialog
            if (!click(toolbar(run) ? toolbar(run)->getConfirmButton() : nullptr))
            {
                return false;
            }
            return wait(run, [&run]()
            {
                pdf::PDFAppToolGUIStructure* current = realGui<pdf::PDFAppToolGUIStructure>(run);
                return !current || (shell(run)->getState() == pdf::PDFDocumentToolShell::State::Editing && shell(run)->getRunner()->getState() == pdf::PDFDocumentToolRunner::State::Idle);
            });
        });
    }
    else if (name == QLatin1String("extracttext"))
    {
        // Extract Text: the menus Format / Order / Include - Format back to Plain text finds its
        // previews in the caches of the runners, they come within the change of the value -, the
        // preview of another page, the zoom of the page, Save Text of the result dialog; a second
        // run to the clipboard
        steps.push_back(&H::openTool);
        steps.push_back(&H::waitExtractPreviews);
        steps.push_back([](ToolPagesLifetimeRun& run) { return clickPanelRow(run, findToolButton(run, QStringLiteral("Format:")), 1); });
        steps.push_back(&H::closePanel);
        steps.push_back(&H::waitExtractPreviews);
        steps.push_back([](ToolPagesLifetimeRun& run) { return clickPanelRow(run, findToolButton(run, QStringLiteral("Format:")), 0); });
        steps.push_back(&H::closePanel);
        steps.push_back([](ToolPagesLifetimeRun& run) { return clickPanelRow(run, findToolButton(run, QStringLiteral("Order:")), 1); });
        steps.push_back(&H::closePanel);
        steps.push_back([](ToolPagesLifetimeRun& run) { return clickPanelRow(run, findToolButton(run, QStringLiteral("Include:")), 0); });
        steps.push_back(&H::closePanel);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            if (run.tab)
            {
                run.tab->getView()->goToPage(1);
            }
            settle();
            pdf::PDFAppToolGUIExtractText* current = realGui<pdf::PDFAppToolGUIExtractText>(run);
            const QList<QToolButton*> buttons = current ? current->getPreviewPage()->getZoomControl()->findChildren<QToolButton*>() : QList<QToolButton*>();
            return !buttons.isEmpty() && click(buttons.back());
        });
        steps.push_back([](ToolPagesLifetimeRun& run) { return confirmDialog(run, confirmToDialog(run)); });
        steps.push_back(&H::openTool);
        steps.push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppToolResultDialog* dialog = confirmToDialog(run);
            if (!dialog)
            {
                return false;
            }
            // The choice may delete the dialog with its window (a deletion from its signal)
            QPointer<pdf::PDFAppToolResultDialog> guard(dialog);
            guard->selectChoice(pdf::PDFAppToolResultDialog::Choice::Clipboard);
            return !guard || confirmDialog(run, guard.data());
        });
    }
    return steps;
}

class AppToolPagesLifetimeTest : public QObject
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

    /// The scenarios of the test tool (parameters, run, leave) and of the real tools of the
    /// first batch (the next batches add theirs)
    static QStringList getScenarios()
    {
        return { QStringLiteral("parameters"), QStringLiteral("run"), QStringLiteral("leave"), QStringLiteral("compress"), QStringLiteral("compressrun"),
                 QStringLiteral("rotate"), QStringLiteral("remove"), QStringLiteral("sanitize"), QStringLiteral("removelinks"), QStringLiteral("structure"),
                 QStringLiteral("extracttext") };
    }

    /// Runs the scenario. In the replay mode the victim is deleted at the target
    /// site. \returns the sites seen (discovery), whether the victim was deleted and
    /// whether the site came without the victim (the page and the shell exist only while
    /// the tool is open).
    void runScenario(const QString& name, ToolPagesLifetimeRecorder::Mode mode, const ToolPagesLifetimeRecorder::Site& target, ToolPagesLifetimeVictim victim,
                     std::vector<ToolPagesLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred = nullptr, bool* isReached = nullptr);
};

void AppToolPagesLifetimeTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolPagesLifetimeTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolPagesLifetimeTest::deleteFromEverySignal_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("victim");
    QTest::addColumn<int>("part");

    const std::pair<ToolPagesLifetimeVictim, const char*> victims[] = { { ToolPagesLifetimeVictim::Tab, "tab" }, { ToolPagesLifetimeVictim::Window, "window" },
                                                                       { ToolPagesLifetimeVictim::Page, "page" }, { ToolPagesLifetimeVictim::Shell, "shell" } };
    for (const QString& name : getScenarios())
    {
        // The sites of a scenario are divided into parts (every PART_COUNT-th site),
        // so a row ends within the time limit of QtTest also under the sanitizers
        for (const auto& [victim, victimName] : victims)
        {
            for (int part = 0; part < PART_COUNT; ++part)
            {
                QTest::newRow(QStringLiteral("%1-%2-%3").arg(name, QLatin1String(victimName)).arg(part).toLatin1().constData()) << name << int(victim) << part;
            }
        }
    }
}

void AppToolPagesLifetimeTest::runScenario(const QString& name, ToolPagesLifetimeRecorder::Mode mode, const ToolPagesLifetimeRecorder::Site& target, ToolPagesLifetimeVictim victim,
                                           std::vector<ToolPagesLifetimeRecorder::Site>* sites, bool* isFired, bool* isDeferred, bool* isReached)
{
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);

    ToolPagesLifetimeRun run;
    run.fixture = &fixture;
    const QString realToolId = ToolPagesLifetimeHelper::getScenarioToolId(name);
    const bool isRealTool = !realToolId.isEmpty();
    run.toolId = isRealTool ? realToolId : QStringLiteral("pages.rotate");
    if (!isRealTool)
    {
        run.tool = fixture.registerShellTool(run.toolId, true);
        QVERIFY(run.tool);
        run.tool->setPreview(true, false);
    }
    run.window = fixture.window;
    run.tab = ToolPagesLifetimeHelper::openScenarioDocument(fixture, name);
    QVERIFY(run.tab);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&run]() { return run.tab->getDocument() && run.tab->getDocument()->isReady(); }));
    const std::vector<ToolPagesLifetimeStep> steps = ToolPagesLifetimeHelper::scenario(name);
    QVERIFY(!steps.empty());
    pdftest::AppDocumentFixture::settle();

    ToolPagesLifetimeRecorder recorder(mode);
    if (mode == ToolPagesLifetimeRecorder::Mode::Replay)
    {
        recorder.setTarget(target, [&run, victim](const ToolPagesLifetimeRecorder::Site& site)
        {
            QObject* object = nullptr;

            // A victim being destroyed already (a signal of its destructor) is not
            // deleted again: qobject_cast fails once the derived destructor has run
            switch (victim)
            {
                case ToolPagesLifetimeVictim::Window:
                    object = qobject_cast<pdf::PDFAppMainWindow*>(run.window.data());
                    break;

                case ToolPagesLifetimeVictim::Tab:
                    object = qobject_cast<pdf::PDFAppDocumentTab*>(run.tab.data());
                    break;

                case ToolPagesLifetimeVictim::Page:
                    object = ToolPagesLifetimeHelper::page(run);
                    break;

                case ToolPagesLifetimeVictim::Shell:
                    object = ToolPagesLifetimeHelper::shell(run);
                    break;
            }

            if (!object)
            {
                return ToolPagesLifetimeRecorder::Action::None;
            }
            // The parts of the tool go with the page and the shell, they are no children of them
            bool isDeferred = ToolPagesLifetimeRecorder::isDeferredSite(site) || ToolPagesLifetimeRecorder::isDeferredState(site, object);
            if (victim != ToolPagesLifetimeVictim::Window)
            {
                isDeferred = isDeferred || ToolPagesLifetimeApplication::isLanguageChangeInside();
            }
            if (victim == ToolPagesLifetimeVictim::Page || victim == ToolPagesLifetimeVictim::Shell)
            {
                if (pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeHelper::shell(run))
                {
                    const QObject* parts[] = { shell->getToolbar(), shell->getLeftPanel(), shell->getRightPanel(), shell->getCanvasPage() };
                    for (const QObject* part : parts)
                    {
                        isDeferred = isDeferred || (part && ToolPagesLifetimeRecorder::isDeferredState(site, part));
                    }
                }
            }
            if (isDeferred)
            {
                object->deleteLater();
                return ToolPagesLifetimeRecorder::Action::Deferred;
            }
            delete object;
            return ToolPagesLifetimeRecorder::Action::Deleted;
        });
    }

    for (size_t i = 0; i < steps.size(); ++i)
    {
        recorder.setStep(int(i));
        recorder.hookApplication();
        recorder.hookTree(fixture.context.get());

        const bool isOk = steps[i](run);
        if (qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE"))
        {
            qInfo().noquote() << QStringLiteral("step %1: %2").arg(i).arg(isOk ? QStringLiteral("ok") : QStringLiteral("failed"));
        }
        if (recorder.isFired())
        {
            break;
        }
        if (mode == ToolPagesLifetimeRecorder::Mode::Discover)
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
    if (run.tool)
    {
        run.tool->setHold(false);
        pdftest::AppToolShellFixture::waitFor([&run]() { return run.tool->getHeldCount() == 0; });
    }
    pdftest::AppDocumentFixture::settle();

    if (mode == ToolPagesLifetimeRecorder::Mode::Replay && recorder.isFired())
    {
        // Close a dialog of a question, which nobody answers any more
        QTest::qWait(pdf::PDFDocumentToolRunner::PREVIEW_DELAY + 50);
        pdftest::AppDocumentFixture::settle();

        switch (victim)
        {
            case ToolPagesLifetimeVictim::Window:
                QVERIFY(!run.window);
                break;

            case ToolPagesLifetimeVictim::Tab:
            {
                QVERIFY(run.window);
                QVERIFY(!run.tab);

                // A new document tab with the tool in it
                run.tab = fixture.openDocument(2);
                QVERIFY(run.tab);
                QVERIFY(ToolPagesLifetimeHelper::openTool(run));
                QVERIFY(run.tab->getView()->isToolModeActive());
                break;
            }

            case ToolPagesLifetimeVictim::Page:
            case ToolPagesLifetimeVictim::Shell:
            {
                // The view is free, the document too; the tool opens again
                QVERIFY(run.window && run.tab);
                QVERIFY(!run.tab->getDocument()->isBlockedByOperation());
                if (!ToolPagesLifetimeHelper::shell(run))
                {
                    QVERIFY(!run.tab->getView()->isToolModeActive());
                    run.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(run.tab->getDocumentId()));
                    pdftest::AppDocumentFixture::settle();
                    QVERIFY(ToolPagesLifetimeHelper::openTool(run));
                    QVERIFY(run.tab->getView()->isToolModeActive());
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
    if (isReached)
    {
        *isReached = recorder.isTargetReached();
    }
}

void AppToolPagesLifetimeTest::deleteFromEverySignal()
{
    QFETCH(QString, scenario);
    QFETCH(int, victim);
    QFETCH(int, part);

    std::vector<ToolPagesLifetimeRecorder::Site> sites;
    runScenario(scenario, ToolPagesLifetimeRecorder::Mode::Discover, ToolPagesLifetimeRecorder::Site(), ToolPagesLifetimeVictim(victim), &sites, nullptr);
    if (QTest::currentTestFailed())
    {
        return;
    }
    QVERIFY(!sites.empty());

    // PDF4QT_LIFETIME_SITE=<n> replays one site only (debugging)
    // PDF4QT_LIFETIME_SITE=<first>-<last> replays the sites of the range (of the part), one
    // process for all of them (a damage left by a replay shows in a later one)
    const QString siteText = qEnvironmentVariable("PDF4QT_LIFETIME_SITE");
    const bool isSiteRange = siteText.contains(QLatin1Char('-'));
    const int firstSite = isSiteRange ? siteText.section(QLatin1Char('-'), 0, 0).toInt() : 0;
    const int lastSite = isSiteRange ? siteText.section(QLatin1Char('-'), 1, 1).toInt() : int(sites.size());
    const int onlySite = !siteText.isEmpty() && !isSiteRange ? siteText.toInt() : -1;
    QStringList notReproduced;
    int replayed = 0;
    int deferred = 0;
    int withoutVictim = 0;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        if (onlySite >= 0 ? int(i) != onlySite : (int(i) % PART_COUNT != part || int(i) < firstSite || int(i) > lastSite))
        {
            continue;
        }
        if (qEnvironmentVariableIsSet("PDF4QT_LIFETIME_TRACE"))
        {
            qInfo().noquote() << QStringLiteral("site %1 step %2: %3").arg(i).arg(sites[i].step).arg(sites[i].key);
        }

        bool isFired = false;
        bool isDeferred = false;
        bool isReached = false;
        runScenario(scenario, ToolPagesLifetimeRecorder::Mode::Replay, sites[i], ToolPagesLifetimeVictim(victim), nullptr, &isFired, &isDeferred, &isReached);
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
        else if (isReached)
        {
            // The page and the shell exist only while the tool is open
            ++withoutVictim;
        }
        else
        {
            notReproduced << QStringLiteral("step %1: %2").arg(sites[i].step).arg(sites[i].key);
        }
    }

    qInfo().noquote() << QStringLiteral("%1: %2 sites, %3 deletions (%4 deferred), %5 without the victim in part %6").arg(scenario).arg(sites.size()).arg(replayed).arg(deferred).arg(withoutVictim).arg(part);
    for (const QString& site : notReproduced)
    {
        qInfo().noquote() << QStringLiteral("not reproduced (the emission differs in the run): ") + site;
    }

    // At least 80 % of the sites are reproduced for every victim (a site of a timer or of
    // the order of the events may move to another step); a site, which came while the victim
    // does not exist, has nothing to delete. Only the tab and the window exist at each site.
    QVERIFY2(onlySite >= 0 || isSiteRange || replayed * 10 >= (replayed + int(notReproduced.size())) * 8, qPrintable(notReproduced.join(QLatin1Char('\n'))));
    const bool isVictimAlways = victim == int(ToolPagesLifetimeVictim::Tab) || victim == int(ToolPagesLifetimeVictim::Window);
    QVERIFY2(onlySite >= 0 || isSiteRange || !isVictimAlways || withoutVictim == 0, qPrintable(QString::number(withoutVictim)));
    QVERIFY(replayed > 0 || onlySite >= 0 || isSiteRange);
}

void AppToolPagesLifetimeTest::openPopupsSurviveChanges_data()
{
    QTest::addColumn<QString>("popup");
    QTest::addColumn<QString>("change");
    for (const char* popup : { "panel", "dialog", "leave", "permission", "custom" })
    {
        for (const char* change : { "language", "document", "theme" })
        {
            QTest::addRow("%s-%s", popup, change) << QString::fromLatin1(popup) << QString::fromLatin1(change);
        }
    }

    // The real tools of the batch: the result dialog of each, the first panel of the toolbar of
    // those, which have one (Rotate and Remove Pages have none)
    for (const char* popup : { "dialog-rotate", "dialog-remove", "dialog-sanitize", "dialog-removelinks", "dialog-structure", "dialog-extracttext",
                               "panel-sanitize", "panel-removelinks", "panel-structure", "panel-extracttext" })
    {
        for (const char* change : { "language", "document", "theme" })
        {
            QTest::addRow("%s-%s", popup, change) << QString::fromLatin1(popup) << QString::fromLatin1(change);
        }
    }
}

void AppToolPagesLifetimeTest::openPopupsSurviveChanges()
{
    // The popups of the tool open while a translator is installed, the document changes
    // or the theme changes: nothing is written by the change, the tool ends afterwards
    using H = ToolPagesLifetimeHelper;
    QFETCH(QString, popup);
    QFETCH(QString, change);

    pdftest::AppToolShellFixture fixture;
    ToolPagesLifetimeRun run;
    run.fixture = &fixture;
    const bool isCustomPanel = popup == QLatin1String("custom");
    const QString realScenario = popup.section(QLatin1Char('-'), 1);
    const bool isRealTool = !realScenario.isEmpty();
    run.toolId = popup == QLatin1String("permission") ? QStringLiteral("protect.remove-links") : (isCustomPanel ? QStringLiteral("optimize.compress") : QStringLiteral("pages.rotate"));
    if (isRealTool)
    {
        run.toolId = H::getScenarioToolId(realScenario);
        QVERIFY(!run.toolId.isEmpty());
    }
    else if (!isCustomPanel)
    {
        run.tool = fixture.registerShellTool(run.toolId, true);
        QVERIFY(run.tool);
    }
    run.window = fixture.window;
    if (isRealTool)
    {
        run.tab = H::openScenarioDocument(fixture, realScenario);
    }
    else
    {
        run.tab = popup == QLatin1String("permission") ? fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()))
                                                       : (isCustomPanel ? fixture.open(H::writePhoto(fixture.path(QStringLiteral("photo.pdf")))) : fixture.openDocument(3));
    }
    QVERIFY(run.tab);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&run]() { return run.tab->getDocument()->isReady(); }));
    QVERIFY(H::openTool(run));
    pdf::PDFAppDocument* document = run.tab->getDocument();
    const quint64 version = document->getVersion();

    const auto applyChange = [&]()
    {
        if (change == QLatin1String("language"))
        {
            ToolPagesLifetimeTranslator translator;
            QCoreApplication::installTranslator(&translator);
            H::settle();
            QCoreApplication::removeTranslator(&translator);
            H::settle();
        }
        else if (change == QLatin1String("theme"))
        {
            pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
            H::settle();
            pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
            H::settle();
        }
        else
        {
            // Behind a blocking operation only a restore changes the version
            document->restore(document->getDocument(), pdf::PDFModifiedDocument::Annotation);
            H::settle();
        }
    };

    if (isRealTool && popup.startsWith(QLatin1String("dialog-")))
    {
        // The result dialog of a real tool: it stays open over the change, Cancel keeps the document
        QVERIFY(H::prepareRun(run));
        pdf::PDFAppToolResultDialog* dialog = H::confirmToDialog(run);
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        const QPointer<pdf::PDFAppToolResultDialog> dialogGuard(dialog);
        applyChange();
        QVERIFY(dialogGuard);
        QVERIFY(H::shell(run));
        QCOMPARE(H::shell(run)->getResultDialog(), dialog);
        QVERIFY(dialog->isVisible());
        // Discard ends the tool
        dialog->reject();
        QVERIFY(pdftest::AppToolShellFixture::waitFor([&run]() { return !H::shell(run); }));
        H::settle();
    }
    else if (isRealTool)
    {
        // The first panel of the toolbar of a real tool open over the change; afterwards the
        // tool works - a panel opens again and the toolbar of the tool is there
        QVERIFY(H::prepareRun(run));
        QToolButton* button = H::firstPanelButton(run);
        QVERIFY(button);
        QVERIFY(H::toolbar(run)->openPanel(button));
        H::settle();
        QVERIFY(H::toolbar(run)->getOpenPanel());
        applyChange();
        QVERIFY(H::shell(run));
        if (H::toolbar(run)->getOpenPanel())
        {
            H::toolbar(run)->getOpenPanel()->close();
            H::settle();
        }

        // A new version of the document lists the links again: Show waits for the list
        QVERIFY(H::prepareRun(run));
        button = H::firstPanelButton(run);
        QVERIFY(button);
        QVERIFY(H::toolbar(run)->openPanel(button));
        H::settle();
        QVERIFY(H::toolbar(run)->getOpenPanel() && H::toolbar(run)->getOpenPanel()->isVisible());
        H::toolbar(run)->getOpenPanel()->close();
        H::settle();
    }
    else if (isCustomPanel)
    {
        pdf::PDFAppToolGUICompress* gui = H::compressGui(run);
        QVERIFY(gui);
        QVERIFY(H::waitCompressMeasured(run));
        QVERIFY(H::shell(run)->getModel()->setValue(QStringLiteral("preset"), QStringLiteral("custom")).isEmpty());
        gui->openCustomPanel();
        QVERIFY(gui->getCustomPanel()->isVisible());
        QPointer<pdf::PDFAppToolCompressCustomPanel> panel = gui->getCustomPanel();
        applyChange();
        QVERIFY(H::waitCompressMeasured(run));

        // A theme and a new version keep the open panel, a language builds the parts again -
        // the new panel is closed. Its controls still write the values of the model.
        gui = H::compressGui(run);
        QVERIFY(gui);
        if (change == QLatin1String("language"))
        {
            QVERIFY(!panel);
            gui->openCustomPanel();
        }
        else
        {
            QCOMPARE(gui->getCustomPanel(), panel.data());
        }
        QVERIFY(gui->getCustomPanel()->isVisible());
        gui->getCustomPanel()->getControl(QStringLiteral("cleanStructure"))->getCheckBox()->click();
        QVERIFY(!H::shell(run)->getModel()->getValue(QStringLiteral("cleanStructure")).toBool());
        QVERIFY(H::waitCompressMeasured(run));
        gui->getCustomPanel()->hide();
        H::settle();
    }
    else if (popup == QLatin1String("panel"))
    {
        pdftest::ShellTestGUI* gui = H::gui(run);
        QVERIFY(gui);
        QVERIFY(H::toolbar(run)->openPanel(gui->flagsMenu->getButton()));
        H::settle();
        applyChange();
    }
    else if (popup == QLatin1String("dialog"))
    {
        QVERIFY(H::shell(run)->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title")).isEmpty());
        pdf::PDFAppToolResultDialog* dialog = H::runToDialog(run);
        QVERIFY(dialog);
        applyChange();
        QVERIFY(H::shell(run));
        QCOMPARE(H::shell(run)->getResultDialog(), dialog);
        dialog->reject();
        H::settle();
    }
    else if (popup == QLatin1String("leave"))
    {
        QVERIFY(H::runToHold(run));
        H::settle();
        QTimer::singleShot(0, fixture.window.data(), [&]()
        {
            applyChange();
            H::answerLater(run, QStringLiteral("stopAndLeaveButton"));
        });
        fixture.window->getNavigator()->goBack(run.tab);
        H::settle();
        H::releaseHold(run);
        QVERIFY(!H::shell(run));
    }
    else
    {
        QVERIFY(pdftest::AppToolShellFixture::waitFor([&run]() { return H::shell(run) && H::shell(run)->getPermissionDialog(); }));
        applyChange();
        QVERIFY(H::shell(run)->getPermissionDialog());
        H::shell(run)->getPermissionDialog()->reject();
        H::settle();
    }

    // Nothing has been written by the change, the tool ends, the view works
    QVERIFY(document->getVersion() == version || change == QLatin1String("document"));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    if (H::shell(run))
    {
        H::shell(run)->cancel();
        H::settle();
    }
    QVERIFY(!run.tab->getView()->isToolModeActive());
    QVERIFY(!document->isBlockedByOperation());
}

int main(int argc, char** argv)
{
    // QTEST_MAIN with the application of the test (it knows the delivered events)
    ToolPagesLifetimeApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppToolPagesLifetimeTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_apptoolpageslifetimetest.moc"
