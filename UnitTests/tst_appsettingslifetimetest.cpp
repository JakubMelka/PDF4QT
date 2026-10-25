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

// Generic lifetime test of the page Settings (step 5, review RT5): every scenario
// of the user (each section with its rows, dialogs, popups, the certificate table,
// the speech section, the shortcuts model and delegate, the storage dialogs, the
// translator, the color profile scanner, changes of another instance) is run once
// with a recorder of every signal of every object of the application (classes
// pdf::PDFApp* and pdf::PDFDatabase*), then once more for each signal it has seen
// (the first emission of the signal of an object in each step), with the home tab,
// the window or the Settings page deleted synchronously from that emission. Nothing
// may crash (run it under ASan / valgrind - freed memory stays readable in a Release
// build on Windows), the window survives the deletion of its home tab and of the
// page. A second test opens every dialog and popup of the page and changes the
// language (a translator, the language of another instance) and the settings of
// another instance while it is open.

#include "appsettingsfixture.h"

#include "pdfappcertificatetable.h"
#include "pdfappcommand.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentspeech.h"
#include "pdfappdocumenttab.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfapproute.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfappsettingssection.h"
#include "pdfappsettingssection_colormanagement.h"
#include "pdfappsettingssection_colormodes.h"
#include "pdfappsettingssection_general.h"
#include "pdfappsettingssection_security.h"
#include "pdfappsettingssection_shortcuts.h"
#include "pdfappsettingssection_speech.h"
#include "pdfappsettingssection_storage.h"
#include "pdfappshortcutsmodel.h"
#include "pdfapptab.h"
#include "pdfapptranslator.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabaseschema.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfshortcutrepository.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfcertificatestore.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QHash>
#include <QMetaMethod>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QSlider>
#include <QThread>
#include <QTranslator>
#include <QtTest>

#include <functional>
#include <vector>

class SettingsLifetimeRecorder;

/// One connection of the recorder to one signal of one object: the slot is the
/// first method after the methods of QObject (the pattern of QSignalSpy)
class SettingsLifetimeHook : public QObject
{
public:
    explicit SettingsLifetimeHook(SettingsLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature);

    virtual int qt_metacall(QMetaObject::Call call, int id, void** arguments) override;

private:
    SettingsLifetimeRecorder* m_recorder;
    QPointer<QObject> m_sender;
    QByteArray m_signature;
};

/// Recorder of the signals of the objects of the application. In the discovery
/// mode it collects the sites (signal of an object, step of the scenario), in the
/// replay mode it deletes the victim at the first emission of the target site.
class SettingsLifetimeRecorder : public QObject
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

    explicit SettingsLifetimeRecorder(Mode mode) :
        m_mode(mode)
    {
        qApp->installEventFilter(this);
    }

    virtual ~SettingsLifetimeRecorder() override
    {
        qApp->removeEventFilter(this);
    }

    /// The action deletes the victim (synchronously, or later for a deferred site -
    /// the predicate tells the deferred sites of the state of the run)
    void setTarget(const Site& site, std::function<bool(bool)> action, std::function<bool()> isDeferredState)
    {
        m_target = site;
        m_action = std::move(action);
        m_isDeferredState = std::move(isDeferredState);
    }

    /// Sites, where the victim is deleted by deleteLater instead of synchronously,
    /// each with the reason why no code can survive it:
    ///  - a signal of a destructor (PDFAppPage::aboutToBeDestroyed): the dying object is
    ///    still a child of its parents, a deletion of an ancestor deletes it twice (Qt);
    ///  - the state of the run says so (a predicate of the run; none today - the pending
    ///    values written at the hide of the Settings page and the sample stopped by the
    ///    speech section come from the event loop since FX5-2, not from inside of
    ///    QWidgetPrivate::hideChildren)
    bool isDeferredSite(const Site& site) const
    {
        return site.key.endsWith(QStringLiteral("::aboutToBeDestroyed()")) || (m_isDeferredState && m_isDeferredState());
    }

    void setStep(int step) { m_step = step; }
    bool isFired() const { return m_isFired; }
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

            SettingsLifetimeHook* hook = new SettingsLifetimeHook(this, object, method.methodSignature());
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
            m_isFired = m_action(isDeferredSite(m_target));
            m_isInAction = false;
        }
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        // An object receiving an event is constructed: hook it (dialogs, popups and
        // rows created in the middle of a step)
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
    bool m_isInAction = false;
    bool m_isStopped = false;
    Site m_target;
    std::function<bool(bool)> m_action;
    std::function<bool()> m_isDeferredState;
    std::vector<Site> m_sites;
    QSet<QString> m_seen;
    QHash<QObject*, QPointer<QObject>> m_hooked;
    QPointer<QWidget> m_paintedWidget;
};

SettingsLifetimeHook::SettingsLifetimeHook(SettingsLifetimeRecorder* recorder, QObject* sender, const QByteArray& signature) :
    QObject(recorder),
    m_recorder(recorder),
    m_sender(sender),
    m_signature(signature)
{
}

int SettingsLifetimeHook::qt_metacall(QMetaObject::Call call, int id, void** arguments)
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

/// Speech engine of the test: it records nothing, a test step ends a text by finish()
class SettingsLifetimeSpeechEngine : public pdf::PDFAppSpeechEngine
{
public:
    explicit SettingsLifetimeSpeechEngine(QObject* parent) :
        pdf::PDFAppSpeechEngine(parent)
    {
    }

    static pdf::PDFAppSpeechEngine* create(const QString& engineName, QObject* parent)
    {
        Q_UNUSED(engineName);
        return new SettingsLifetimeSpeechEngine(parent);
    }

    virtual State getState() const override { return m_state; }
    virtual QString getErrorString() const override { return QString(); }
    virtual QList<QLocale> getLocales() const override { return { QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::German, QLocale::Germany) }; }
    virtual QLocale getLocale() const override { return m_locale; }
    virtual void setLocale(const QLocale& locale) override { m_locale = locale; }
    virtual QList<pdf::PDFAppSpeechVoice> getVoices() const override
    {
        return { { QStringLiteral("Aria"), QStringLiteral("Aria (Female, Adult)") }, { QStringLiteral("Guy"), QStringLiteral("Guy (Male, Adult)") } };
    }
    virtual QString getVoice() const override { return m_voice; }
    virtual void setVoice(const QString& name) override { m_voice = name; }
    virtual void setRate(double) override { }
    virtual void setPitch(double) override { }
    virtual void setVolume(double) override { }
    virtual void say(const QString&) override { setEngineState(State::Speaking); }
    virtual void pause() override { setEngineState(State::Paused); }
    virtual void resume() override { setEngineState(State::Speaking); }
    virtual void stop() override { setEngineState(State::Ready); }

    void finish() { setEngineState(State::Ready); }

private:
    void setEngineState(State state)
    {
        if (m_state != state)
        {
            m_state = state;
            Q_EMIT stateChanged();
        }
    }

    State m_state = State::Ready;
    QLocale m_locale = QLocale(QLocale::English, QLocale::UnitedStates);
    QString m_voice = QStringLiteral("Aria");
};

/// Pseudo translation of every text of PDF4QT: "[text]"
class SettingsLifetimeTranslator : public QTranslator
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

/// Fixture with a database on the disk (a context, which is not portable) of the
/// schema version of the application + 1 and an older file pdf4qt-1.db, which the
/// section Storage lists with Delete
class SettingsLifetimeStorageFixture : public pdftest::AppSettingsFixture
{
public:
    explicit SettingsLifetimeStorageFixture(const QString& dataDirectory) :
        AppSettingsFixture(QSize(1440, 960))
    {
        delete window.data();
        settle();
        context.reset();

        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(QStringList({ QStringLiteral("PDF4QT") }), QDir::currentPath()));
        pdf::PDFDatabaseOptions options;
        options.directory = dataDirectory;
        options.memoryName = QStringLiteral("/pdf4qt-settings-lifetime-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
        options.migrations = getMigrations();
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        window = new pdf::PDFAppMainWindow(context.get());
        window->getNavigator()->setExternalUrlsEnabled(false);
        window->resize(QSize(1440, 960));
        window->show();
        if (!QTest::qWaitForWindowExposed(window.data()))
        {
            qWarning("The window was not exposed");
        }
        settle();
        closeMessageBoxes();
    }

    /// Migrations of the application plus one newer test version
    static std::vector<pdf::PDFDatabaseMigration> getMigrations()
    {
        std::vector<pdf::PDFDatabaseMigration> migrations = pdf::PDFDatabaseSchema::getMigrations();
        pdf::PDFDatabaseMigration migration;
        migration.version = int(migrations.size()) + 1;
        migration.sql = "CREATE TABLE test_lifetime" + QByteArray::number(migration.version) + "(id INTEGER PRIMARY KEY);";
        migrations.push_back(migration);
        return migrations;
    }

    /// Writes a database file of the current schema version (older than the test version)
    static bool createOlderFile(const QString& filePath)
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, &error);
        if (!handle)
        {
            return false;
        }
        error = pdf::PDFDatabaseSchema::migrate(handle, pdf::PDFDatabaseSchema::getMigrations(), pdf::PDFDatabaseSchema::getCurrentVersion());
        if (!error.isError())
        {
            error = pdf::PDFDatabase::executeOnHandle(handle, "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'dark', 0)");
        }
        pdf::PDFDatabase::closeHandle(handle);
        return !error.isError();
    }

private:
    static int s_counter;
};

int SettingsLifetimeStorageFixture::s_counter = 0;

/// State of one run of a scenario
struct SettingsLifetimeRun
{
    pdftest::AppSettingsFixture* fixture = nullptr;
    QPointer<pdf::PDFAppMainWindow> window;
    QPointer<pdf::PDFAppWorkspaceTab> tab;
    QString backupPath;
    QString garbagePath;
};

using SettingsLifetimeStep = std::function<bool(SettingsLifetimeRun&)>;

/// Victim of the deletion
enum class SettingsLifetimeVictim
{
    Tab,
    Window,
    Page
};

/// Helpers of the scenarios: every input is sent through guards, the victim may be
/// deleted inside of any event
class SettingsLifetimeHelper
{
public:
    SettingsLifetimeHelper() = delete;

    static void settle() { pdftest::AppDocumentFixture::settle(); }

    static pdf::PDFAppSettingsPage* page(const SettingsLifetimeRun& run)
    {
        return run.tab ? qobject_cast<pdf::PDFAppSettingsPage*>(run.tab->getPage()) : nullptr;
    }

    template<typename T>
    static T* section(const SettingsLifetimeRun& run)
    {
        pdf::PDFAppSettingsPage* settingsPage = page(run);
        return settingsPage ? qobject_cast<T*>(settingsPage->getSection()) : nullptr;
    }

    static pdf::PDFAppSettingControl* control(const SettingsLifetimeRun& run, const QString& anchor)
    {
        pdf::PDFAppSettingsPage* settingsPage = page(run);
        pdf::PDFAppSettingRow* row = settingsPage ? settingsPage->findRow(anchor) : nullptr;
        return row ? row->getControl() : nullptr;
    }

    static bool navigate(SettingsLifetimeRun& run, const QString& route)
    {
        if (!run.window || !run.window->getNavigator())
        {
            return false;
        }
        run.window->getNavigator()->navigate(pdf::PDFAppRoute::parse(route), pdf::PDFAppNavigationMode::Push);
        settle();
        return page(run) || !route.startsWith(QStringLiteral("/settings"));
    }

    /// A mouse click (press and release, the release only to a living widget)
    static bool click(QWidget* widget, QPoint position = QPoint())
    {
        if (!widget || !widget->isEnabled())
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

    static bool clickCheckBox(SettingsLifetimeRun& run, const QString& anchor)
    {
        pdf::PDFAppSettingControl* settingControl = control(run, anchor);
        QCheckBox* checkBox = settingControl ? settingControl->getCheckBox() : nullptr;
        return checkBox && click(checkBox, QPoint(9, checkBox->height() / 2));
    }

    /// Chooses the item of the data by the arrows of the closed combo box (activated)
    static bool chooseItem(QComboBox* comboBox, const QVariant& data)
    {
        QPointer<QComboBox> guard(comboBox);
        const int target = comboBox ? comboBox->findData(data) : -1;
        if (target < 0 || !comboBox->isEnabled())
        {
            return false;
        }

        comboBox->setFocus();
        for (int i = 0; guard && guard->currentIndex() != target && i < 1000; ++i)
        {
            const int current = guard->currentIndex();
            key(guard.data(), current < target ? Qt::Key_Down : Qt::Key_Up);
            if (guard && guard->currentIndex() == current)
            {
                break;
            }
        }
        settle();
        return true;
    }

    static bool chooseRowItem(SettingsLifetimeRun& run, const QString& anchor, const QVariant& data)
    {
        pdf::PDFAppSettingControl* settingControl = control(run, anchor);
        return settingControl && chooseItem(settingControl->getComboBox(), data);
    }

    /// Selects the text of the editor of the row, types the text and Enter
    static bool typeRowText(SettingsLifetimeRun& run, const QString& anchor, const QString& text, bool isConfirmed = true)
    {
        pdf::PDFAppSettingControl* settingControl = control(run, anchor);
        QWidget* editor = settingControl ? settingControl->getEditor() : nullptr;
        if (!editor || !editor->isEnabled())
        {
            return false;
        }

        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
        if (!lineEdit)
        {
            lineEdit = editor->findChild<QLineEdit*>();
        }
        QPointer<QLineEdit> guard(lineEdit);
        if (!lineEdit)
        {
            return false;
        }

        editor->setFocus();
        lineEdit->selectAll();
        if (!type(lineEdit, text))
        {
            return true;
        }
        if (isConfirmed && guard)
        {
            key(guard.data(), Qt::Key_Return);
        }
        settle();
        return true;
    }

    /// Moves the slider of the row by one step to the right and writes it
    static bool moveSlider(SettingsLifetimeRun& run, const QString& anchor)
    {
        QPointer<pdf::PDFAppSettingControl> settingControl = control(run, anchor);
        QSlider* slider = settingControl ? settingControl->getSlider() : nullptr;
        if (!slider)
        {
            return false;
        }
        slider->setFocus();
        key(slider, Qt::Key_Right);
        if (settingControl)
        {
            settingControl->commitPendingValue();
        }
        settle();
        return true;
    }

    /// Another instance writes a setting
    static bool writeOther(SettingsLifetimeRun& run, const QString& key, const QVariant& value)
    {
        return run.fixture->writeFromOtherConnection(key, value);
    }

    /// Waits for the condition (the victim may go meanwhile)
    static bool waitFor(const std::function<bool()>& condition)
    {
        const bool result = QTest::qWaitFor(condition, 20000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        settle();
        return result;
    }

    /// DER data of a certificate of the Adobe Approved Trust List
    static QByteArray certificate(int index)
    {
        static const pdf::PDFCertificateEntries entries = pdf::PDFCertificateStore::getAATLCertificates();
        return index >= 0 && size_t(index) < entries.size() ? entries[size_t(index)].info.getCertificateData() : QByteArray();
    }

    /// Two system certificates
    static pdf::PDFCertificateEntries systemCertificates()
    {
        pdf::PDFCertificateEntries entries;
        for (int index : { 5, 6 })
        {
            if (std::optional<pdf::PDFCertificateInfo> info = pdf::PDFCertificateInfo::getCertificateInfo(certificate(index)))
            {
                pdf::PDFCertificateEntry entry;
                entry.type = pdf::PDFCertificateEntry::EntryType::System;
                entry.info = *info;
                entries.push_back(entry);
            }
        }
        return entries;
    }

    /// Another instance adds a trusted certificate (a thread with its own connection)
    static bool addCertificateFromOtherInstance(SettingsLifetimeRun& run, int index)
    {
        pdf::PDFAppContext* context = run.fixture->context.get();
        QSignalSpy spy(context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
        pdf::PDFDatabase* database = context->getDatabase();
        const QByteArray data = certificate(index);
        bool isAdded = false;
        std::unique_ptr<QThread> thread(QThread::create([database, data, &isAdded]() { isAdded = !pdf::PDFTrustedCertificateRepository(database).addCertificate(data).isError(); }));
        thread->start();
        thread->wait();
        return isAdded && waitFor([&spy]() { return !spy.isEmpty(); });
    }

    /// Another instance writes a shortcut
    static bool writeShortcutFromOtherInstance(SettingsLifetimeRun& run, const QString& commandId, const QString& keys)
    {
        pdf::PDFAppContext* context = run.fixture->context.get();
        QSignalSpy spy(context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
        pdf::PDFDatabase* database = context->getDatabase();
        bool isWritten = false;
        std::unique_ptr<QThread> thread(QThread::create([database, commandId, keys, &isWritten]() { isWritten = !pdf::PDFShortcutRepository(database).setShortcut(commandId, keys).isError(); }));
        thread->start();
        thread->wait();
        return isWritten && waitFor([&spy]() { return !spy.isEmpty(); });
    }

    /// Opens the editor of the shortcut of the command (F2)
    static bool startRecording(SettingsLifetimeRun& run, const QString& commandId)
    {
        pdf::PDFAppShortcutsSettingsSection* shortcuts = section<pdf::PDFAppShortcutsSettingsSection>(run);
        if (!shortcuts || !shortcuts->getView())
        {
            return false;
        }
        const int row = shortcuts->getModel()->findRow(commandId);
        const QModelIndex index = row < 0 ? QModelIndex() : shortcuts->getFilterModel()->mapFromSource(shortcuts->getModel()->index(row));
        pdf::PDFAppShortcutsListView* view = shortcuts->getView();
        if (!index.isValid())
        {
            return false;
        }
        view->setFocus(Qt::TabFocusReason);
        view->setCurrentIndex(index);
        view->scrollTo(index);
        key(view, Qt::Key_F2);
        settle();
        return view->findChild<pdf::PDFShortcutRecorder*>() != nullptr;
    }

    /// Records the keys into the open editor of a shortcut
    static bool record(SettingsLifetimeRun& run, int keyCode, Qt::KeyboardModifiers modifiers)
    {
        pdf::PDFAppShortcutsSettingsSection* shortcuts = section<pdf::PDFAppShortcutsSettingsSection>(run);
        pdf::PDFShortcutRecorder* recorder = shortcuts && shortcuts->getView() ? shortcuts->getView()->findChild<pdf::PDFShortcutRecorder*>() : nullptr;
        if (!recorder)
        {
            return false;
        }
        key(recorder, keyCode, modifiers);
        settle();
        return true;
    }

    static bool openRestoreDialog(SettingsLifetimeRun& run)
    {
        pdf::PDFAppSettingsPage* settingsPage = page(run);
        return settingsPage && settingsPage->getRestoreButton() && click(settingsPage->getRestoreButton()) && (!page(run) || page(run)->getRestoreDialog());
    }

    static bool finishRestoreDialog(SettingsLifetimeRun& run, bool isRestored)
    {
        pdf::PDFAppSettingsPage* settingsPage = page(run);
        pdf::PDFAppRestoreDefaultsDialog* dialog = settingsPage ? settingsPage->getRestoreDialog() : nullptr;
        return dialog && click(isRestored ? dialog->getRestoreButton() : dialog->getCancelButton());
    }

    /// The visible dialog of the section Storage
    static pdf::PDFAppStorageDialog* storageDialog(const SettingsLifetimeRun& run)
    {
        pdf::PDFAppStorageSettingsSection* storage = section<pdf::PDFAppStorageSettingsSection>(run);
        return storage ? storage->getDialog() : nullptr;
    }

    /// The steps of a scenario; the setup (documents, rows of the database, the
    /// handlers of the file dialogs) is done here before the recording
    static std::vector<SettingsLifetimeStep> scenario(const QString& name, SettingsLifetimeRun& run);
};

std::vector<SettingsLifetimeStep> SettingsLifetimeHelper::scenario(const QString& name, SettingsLifetimeRun& run)
{
    using H = SettingsLifetimeHelper;
    using K = pdf::PDFSettingKeys;
    pdftest::AppSettingsFixture& fixture = *run.fixture;
    std::vector<SettingsLifetimeStep> steps;

    if (name == QStringLiteral("general"))
    {
        // The tool history with runs (Clear history…)
        pdf::PDFDatabaseStatement statement(fixture.context->getDatabase()->getConnection(), "INSERT OR REPLACE INTO tool_pair(from_tool, to_tool, count, last_used) VALUES ('merge', 'compress', 4, 1)");
        statement.execute();
        fixture.settings().setValue(K::toolsHistoryRuns(), 4);

        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/general")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::clickCheckBox(run, QStringLiteral("suggest-next-tools")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::chooseRowItem(run, QStringLiteral("color-scheme"), pdf::PDFSettingValues::colorSchemeDark()); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::typeRowText(run, QStringLiteral("recent-files"), QStringLiteral("25")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::chooseRowItem(run, QStringLiteral("language"), QStringLiteral("de")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::chooseRowItem(run, QStringLiteral("language"), pdf::PDFSettingValues::languageEnglish()); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppGeneralSettingsSection* general = H::section<pdf::PDFAppGeneralSettingsSection>(run);
            return general && H::click(general->getClearHistoryButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppGeneralSettingsSection* general = H::section<pdf::PDFAppGeneralSettingsSection>(run);
            return general && general->getClearHistoryDialog() && H::click(general->getClearHistoryDialog()->getClearButton());
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeOther(run, K::toolsHistoryRuns(), 5); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, true); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSettingsPage* settingsPage = H::page(run);
            if (!settingsPage)
            {
                return false;
            }
            settingsPage->getSearchField()->setFocus();
            H::type(settingsPage->getSearchField(), QStringLiteral("cache"));
            return H::waitFor([&run]() { return !H::page(run) || H::page(run)->isFiltering(); });
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSettingsPage* settingsPage = H::page(run);
            if (!settingsPage)
            {
                return false;
            }
            H::key(settingsPage->getSearchField(), Qt::Key_Escape);
            H::settle();
            return true;
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSettingsPage* settingsPage = H::page(run);
            return settingsPage && H::click(settingsPage->getNavigationItem(QStringLiteral("viewing")));
        });
    }
    else if (name == QStringLiteral("viewing"))
    {
        // An open document is a consumer of the settings
        fixture.openDocument();
        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.settle();

        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/viewing")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::clickCheckBox(run, QStringLiteral("antialiasing")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::typeRowText(run, QStringLiteral("magnifier-size"), QStringLiteral("150")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::typeRowText(run, QStringLiteral("magnifier-zoom"), QStringLiteral("3.5")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::page(run) && H::page(run)->openLink(QStringLiteral("#prefetch-pages")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeOther(run, K::magnifierZoom(), 4.0); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::typeRowText(run, QStringLiteral("magnifier-size"), QStringLiteral("222"), false); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/home")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/viewing")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, false); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, true); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            // Everything changed (an import of the settings by another instance)
            Q_EMIT run.fixture->context->getNotifier()->everythingChanged();
            H::settle();
            return true;
        });
    }
    else if (name == QStringLiteral("colorModes"))
    {
        fixture.openDocument();
        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.settle();

        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/color-modes")); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppColorModesSettingsSection* section = H::section<pdf::PDFAppColorModesSettingsSection>(run);
            return section && H::click(section->getModeCard(QStringLiteral("customColors")));
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppColorModesSettingsSection* section = H::section<pdf::PDFAppColorModesSettingsSection>(run);
            return section && H::click(section->getPresetCard(QStringLiteral("sepia")));
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            // A color picked in the palette (the palette emits colorSelected of the button)
            pdf::PDFAppSettingControl* settingControl = H::control(run, QStringLiteral("text-color"));
            pdf::PDFColorButton* button = settingControl ? settingControl->getColorButton() : nullptr;
            if (!button)
            {
                return false;
            }
            button->setColor(QColor(QStringLiteral("#123456")));
            Q_EMIT button->colorSelected(QColor(QStringLiteral("#123456")));
            H::settle();
            return true;
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::moveSlider(run, QStringLiteral("high-contrast-strength")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeOther(run, K::colorModesPageColor(), QStringLiteral("#FFFFFF")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, true); });
    }
    else if (name == QStringLiteral("colorManagement"))
    {
        fixture.openDocument();
        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.settle();
        const QString profileFolder = fixture.path(QStringLiteral("profiles"));
        QDir().mkpath(profileFolder);

        const auto waitForScan = [](SettingsLifetimeRun& run)
        {
            return H::waitFor([&run]()
            {
                pdf::PDFAppColorManagementSettingsSection* section = H::section<pdf::PDFAppColorManagementSettingsSection>(run);
                return !section || !section->isScanning();
            });
        };
        steps.push_back([waitForScan](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/color-management")) && waitForScan(run); });
        steps.push_back([profileFolder, waitForScan](SettingsLifetimeRun& run) { return H::typeRowText(run, QStringLiteral("additional-profile-directory"), profileFolder) && waitForScan(run); });
        steps.push_back([waitForScan](SettingsLifetimeRun& run) { return H::writeOther(run, K::cmsProfileDirectory(), QVariant()) && waitForScan(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::chooseRowItem(run, QStringLiteral("color-management-system"), QStringLiteral("generic")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([waitForScan](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, true) && waitForScan(run); });
    }
    else if (name == QStringLiteral("security"))
    {
        pdf::PDFTrustedCertificateRepository repository(fixture.context->getDatabase());
        repository.addCertificate(certificate(0));
        repository.addCertificate(certificate(1));

        const auto waitForTable = [](SettingsLifetimeRun& run)
        {
            return H::waitFor([&run]()
            {
                pdf::PDFAppSecuritySettingsSection* section = H::section<pdf::PDFAppSecuritySettingsSection>(run);
                return !section || !section->getCertificateTable() || !section->getCertificateTable()->isLoadingSystemCertificates();
            });
        };
        steps.push_back([waitForTable](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/security")) && waitForTable(run); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSecuritySettingsSection* section = H::section<pdf::PDFAppSecuritySettingsSection>(run);
            pdf::PDFAppCertificateTable* table = section ? section->getCertificateTable() : nullptr;
            if (!table)
            {
                return false;
            }
            const QModelIndex index = table->getModel()->index(0, pdf::PDFAppCertificateTableModel::NameColumn);
            table->getView()->scrollTo(index);
            return H::click(table->getView()->viewport(), table->getView()->visualRect(index).center());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSecuritySettingsSection* section = H::section<pdf::PDFAppSecuritySettingsSection>(run);
            return section && section->getCertificateTable() && H::click(section->getCertificateTable()->getRemoveButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSecuritySettingsSection* section = H::section<pdf::PDFAppSecuritySettingsSection>(run);
            pdf::PDFAppCardDialog* dialog = section && section->getCertificateTable() ? section->getCertificateTable()->getRemoveDialog() : nullptr;
            return dialog && H::click(dialog->findChild<QPushButton*>(QStringLiteral("removeButton")));
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::addCertificateFromOtherInstance(run, 2); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            // The first check box of the section (the links)
            pdf::PDFAppSettingsPage* settingsPage = H::page(run);
            for (pdf::PDFAppSettingRow* row : settingsPage ? settingsPage->getSection()->getRows() : QList<pdf::PDFAppSettingRow*>())
            {
                if (row->getControl() && row->getControl()->getCheckBox())
                {
                    return H::clickCheckBox(run, row->getAnchor());
                }
            }
            return false;
        });
    }
    else if (name == QStringLiteral("speech"))
    {
        steps.push_back([](SettingsLifetimeRun& run)
        {
            return H::navigate(run, QStringLiteral("/settings/speech")) &&
                   H::waitFor([&run]() { pdf::PDFAppSpeechSettingsSection* section = H::section<pdf::PDFAppSpeechSettingsSection>(run); return !section || section->getEngine(); });
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSpeechSettingsSection* section = H::section<pdf::PDFAppSpeechSettingsSection>(run);
            return section && H::click(section->getPlayButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSpeechSettingsSection* section = H::section<pdf::PDFAppSpeechSettingsSection>(run);
            SettingsLifetimeSpeechEngine* engine = section ? static_cast<SettingsLifetimeSpeechEngine*>(section->getEngine()) : nullptr;
            if (!engine)
            {
                return false;
            }
            engine->finish();
            H::settle();
            return true;
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSpeechSettingsSection* section = H::section<pdf::PDFAppSpeechSettingsSection>(run);
            QComboBox* comboBox = section ? section->getVoiceComboBox() : nullptr;
            if (!comboBox || comboBox->count() < 2)
            {
                return false;
            }
            return H::chooseItem(comboBox, comboBox->itemData(comboBox->currentIndex() == 0 ? 1 : 0));
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::moveSlider(run, QStringLiteral("volume")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeOther(run, K::speechVolume(), 0.5); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppSpeechSettingsSection* section = H::section<pdf::PDFAppSpeechSettingsSection>(run);
            return section && H::click(section->getPlayButton());
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/home")); });
    }
    else if (name == QStringLiteral("shortcuts"))
    {
        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/shortcuts")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::startRecording(run, pdf::PDFAppCommands::print()); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::record(run, Qt::Key_F8, Qt::ControlModifier); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::startRecording(run, pdf::PDFAppCommands::save()); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            // Ctrl+O belongs to Open: the conflict dialog (from the event loop)
            return H::record(run, Qt::Key_O, Qt::ControlModifier) &&
                   H::waitFor([&run]() { pdf::PDFAppShortcutsSettingsSection* section = H::section<pdf::PDFAppShortcutsSettingsSection>(run); return !section || section->getConflictDialog(); });
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppShortcutsSettingsSection* section = H::section<pdf::PDFAppShortcutsSettingsSection>(run);
            return section && section->getConflictDialog() && H::click(section->getConflictDialog()->getAssignButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppShortcutsSettingsSection* section = H::section<pdf::PDFAppShortcutsSettingsSection>(run);
            if (!section)
            {
                return false;
            }
            section->getSearchField()->setFocus();
            H::type(section->getSearchField(), QStringLiteral("save"));
            H::settle();
            return true;
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeShortcutFromOtherInstance(run, pdf::PDFAppCommands::print(), QStringLiteral("Ctrl+F9")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, true); });
    }
    else if (name == QStringLiteral("storage"))
    {
        run.backupPath = fixture.path(QStringLiteral("backup.pdf4qt-backup"));
        run.garbagePath = fixture.path(QStringLiteral("garbage.pdf4qt-backup"));
        QFile garbage(run.garbagePath);
        if (garbage.open(QFile::WriteOnly))
        {
            garbage.write(QByteArray(8192, 'x'));
            garbage.close();
        }
        const QString backupPath = run.backupPath;
        pdf::PDFAppFileDialogs::setSaveBackupHandler([backupPath](QWidget*, const QString&) { return backupPath; });
        QFileInfo::exists(backupPath);

        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/storage")); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppStorageSettingsSection* section = H::section<pdf::PDFAppStorageSettingsSection>(run);
            return section && H::click(section->getExportButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppStorageSettingsSection* section = H::section<pdf::PDFAppStorageSettingsSection>(run);
            return section && section->getExportInfoBar() && H::click(section->getExportInfoBar()->getCloseButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            const QString path = run.backupPath;
            pdf::PDFAppFileDialogs::setOpenBackupHandler([path](QWidget*, const QString&) { return path; });
            pdf::PDFAppStorageSettingsSection* section = H::section<pdf::PDFAppStorageSettingsSection>(run);
            return section && H::click(section->getImportButton()) && (!H::section<pdf::PDFAppStorageSettingsSection>(run) || H::storageDialog(run));
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppStorageDialog* dialog = H::storageDialog(run);
            return dialog && H::click(dialog->getActionButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppStorageSettingsSection* section = H::section<pdf::PDFAppStorageSettingsSection>(run);
            if (!section || section->getFileRows().isEmpty())
            {
                return false;
            }
            return H::click(section->getFileRows().front()->getControlWidget());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppStorageDialog* dialog = H::storageDialog(run);
            return dialog && H::click(dialog->getActionButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            const QString path = run.garbagePath;
            pdf::PDFAppFileDialogs::setOpenBackupHandler([path](QWidget*, const QString&) { return path; });
            pdf::PDFAppStorageSettingsSection* section = H::section<pdf::PDFAppStorageSettingsSection>(run);
            return section && H::click(section->getImportButton());
        });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            pdf::PDFAppStorageDialog* dialog = H::storageDialog(run);
            if (!dialog)
            {
                return false;
            }
            H::key(dialog, Qt::Key_Escape);
            H::settle();
            return true;
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeOther(run, K::magnifierZoom(), 3.0); });
    }
    else if (name == QStringLiteral("editing"))
    {
        fixture.openDocument();
        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.settle();

        steps.push_back([](SettingsLifetimeRun& run) { return H::navigate(run, QStringLiteral("/settings/editing")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::typeRowText(run, QStringLiteral("maximum-undo-steps"), QStringLiteral("3")); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::chooseRowItem(run, QStringLiteral("author-of-new-annotations"), pdf::PDFSettingValues::authorModeCustomName()); });
        steps.push_back([](SettingsLifetimeRun& run)
        {
            // Typed, written after WRITE_DELAY (the timer of the binder)
            if (!H::typeRowText(run, QStringLiteral("custom-author-name"), QStringLiteral("Jakub"), false))
            {
                return false;
            }
            QTest::qWait(pdf::PDFAppSettingControl::WRITE_DELAY * 2);
            H::settle();
            return true;
        });
        steps.push_back([](SettingsLifetimeRun& run) { return H::writeOther(run, K::editingUndoLimit(), 7); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::openRestoreDialog(run); });
        steps.push_back([](SettingsLifetimeRun& run) { return H::finishRestoreDialog(run, true); });
    }

    return steps;
}

class AppSettingsLifetimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void deleteFromEverySignal_data();
    void deleteFromEverySignal();
    void openDialogsSurviveChanges_data();
    void openDialogsSurviveChanges();

private:
    static constexpr int PART_COUNT = 3;

    static QStringList getScenarios();

    /// Creates the fixture of the scenario (Storage on the disk, the others portable)
    static std::unique_ptr<pdftest::AppSettingsFixture> createFixture(const QString& name, QTemporaryDir& dataDirectory);

    /// Runs the scenario. In the replay mode the victim is deleted at the target
    /// site. \returns the sites seen (discovery) and whether the victim was deleted.
    void runScenario(const QString& name, SettingsLifetimeRecorder::Mode mode, const SettingsLifetimeRecorder::Site& target, SettingsLifetimeVictim victim,
                     std::vector<SettingsLifetimeRecorder::Site>* sites, bool* isFired);
};

void AppSettingsLifetimeTest::initTestCase()
{
    pdf::PDFAppTranslator::setSystemLanguages({ QStringLiteral("en-US") });
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppSettingsLifetimeTest::init()
{
    pdf::PDFAppSpeechEngine::setFactory(&SettingsLifetimeSpeechEngine::create);
    pdf::PDFAppCertificateTable::setSystemCertificatesProvider(&SettingsLifetimeHelper::systemCertificates);
}

void AppSettingsLifetimeTest::cleanup()
{
    pdf::PDFAppSpeechEngine::setFactory(nullptr);
    pdf::PDFAppCertificateTable::setSystemCertificatesProvider(nullptr);
    pdf::PDFAppFileDialogs::setSaveBackupHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppFileDialogs::setOpenBackupHandler(pdf::PDFAppFileDialogs::FolderHandler());
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

QStringList AppSettingsLifetimeTest::getScenarios()
{
    return { QStringLiteral("general"), QStringLiteral("viewing"), QStringLiteral("colorModes"), QStringLiteral("colorManagement"), QStringLiteral("security"),
             QStringLiteral("speech"), QStringLiteral("shortcuts"), QStringLiteral("storage"), QStringLiteral("editing") };
}

std::unique_ptr<pdftest::AppSettingsFixture> AppSettingsLifetimeTest::createFixture(const QString& name, QTemporaryDir& dataDirectory)
{
    if (name == QStringLiteral("storage"))
    {
        const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(dataDirectory.path(), pdf::PDFDatabaseSchema::getCurrentVersion());
        if (!SettingsLifetimeStorageFixture::createOlderFile(olderPath))
        {
            return nullptr;
        }
        return std::make_unique<SettingsLifetimeStorageFixture>(dataDirectory.path());
    }
    return std::make_unique<pdftest::AppSettingsFixture>();
}

void AppSettingsLifetimeTest::deleteFromEverySignal_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::addColumn<int>("victim");
    QTest::addColumn<int>("part");

    const std::pair<SettingsLifetimeVictim, const char*> victims[] = { { SettingsLifetimeVictim::Tab, "tab" }, { SettingsLifetimeVictim::Window, "window" },
                                                                      { SettingsLifetimeVictim::Page, "page" } };
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

void AppSettingsLifetimeTest::runScenario(const QString& name, SettingsLifetimeRecorder::Mode mode, const SettingsLifetimeRecorder::Site& target, SettingsLifetimeVictim victim,
                                          std::vector<SettingsLifetimeRecorder::Site>* sites, bool* isFired)
{
    QTemporaryDir dataDirectory;
    std::unique_ptr<pdftest::AppSettingsFixture> fixture = createFixture(name, dataDirectory);
    QVERIFY(fixture);
    QVERIFY(fixture->window);

    SettingsLifetimeRun run;
    run.fixture = fixture.get();
    run.window = fixture->window;
    run.tab = fixture->window->getHomeTab();
    const std::vector<SettingsLifetimeStep> steps = SettingsLifetimeHelper::scenario(name, run);
    QVERIFY(!steps.empty());
    fixture->window->activateTab(fixture->window->getHomeTab());
    pdftest::AppDocumentFixture::settle();

    SettingsLifetimeRecorder recorder(mode);
    if (mode == SettingsLifetimeRecorder::Mode::Replay)
    {
        const auto deleteVictim = [](QObject* object, bool isDeferred)
        {
            if (isDeferred)
            {
                object->deleteLater();
            }
            else
            {
                delete object;
            }
            return true;
        };
        recorder.setTarget(target, [&run, victim, deleteVictim](bool isDeferred)
        {
            // A victim being destroyed already (a signal of its destructor) is not
            // deleted again: qobject_cast fails once the derived destructor has run
            switch (victim)
            {
                case SettingsLifetimeVictim::Window:
                    return qobject_cast<pdf::PDFAppMainWindow*>(run.window.data()) && deleteVictim(run.window.data(), isDeferred);

                case SettingsLifetimeVictim::Tab:
                    return qobject_cast<pdf::PDFAppWorkspaceTab*>(run.tab.data()) && deleteVictim(run.tab.data(), isDeferred);

                case SettingsLifetimeVictim::Page:
                {
                    pdf::PDFAppSettingsPage* page = qobject_cast<pdf::PDFAppSettingsPage*>(SettingsLifetimeHelper::page(run));
                    return page && deleteVictim(page, isDeferred);
                }
            }
            return false;
        }, std::function<bool()>());
    }

    for (size_t i = 0; i < steps.size(); ++i)
    {
        recorder.setStep(int(i));
        recorder.hookApplication();
        recorder.hookTree(fixture->context.get());
        recorder.hookObject(fixture->context->getNotifier());
        recorder.hookObject(fixture->context->getDatabase());
        recorder.hookObject(fixture->context->getTranslator());

        const bool isOk = steps[i](run);
        if (recorder.isFired())
        {
            break;
        }
        if (mode == SettingsLifetimeRecorder::Mode::Discover)
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
    if (mode == SettingsLifetimeRecorder::Mode::Replay && recorder.isFired())
    {
        // The deferred work of the deleted objects finds nothing; the window keeps
        // working after its home tab or its page was deleted
        QTest::qWait(50);
        pdftest::AppDocumentFixture::settle();
        switch (victim)
        {
            case SettingsLifetimeVictim::Window:
                QVERIFY(!run.window);
                break;

            case SettingsLifetimeVictim::Tab:
                QVERIFY(!run.tab);
                QVERIFY(run.window);
                QVERIFY(fixture->openDocument(1));
                break;

            case SettingsLifetimeVictim::Page:
                QVERIFY(run.window && run.tab);
                QVERIFY(SettingsLifetimeHelper::navigate(run, QStringLiteral("/settings/viewing")));
                QVERIFY(SettingsLifetimeHelper::page(run));
                break;
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

void AppSettingsLifetimeTest::deleteFromEverySignal()
{
    QFETCH(QString, scenario);
    QFETCH(int, victim);
    QFETCH(int, part);

    std::vector<SettingsLifetimeRecorder::Site> sites;
    runScenario(scenario, SettingsLifetimeRecorder::Mode::Discover, SettingsLifetimeRecorder::Site(), SettingsLifetimeVictim(victim), &sites, nullptr);
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
        runScenario(scenario, SettingsLifetimeRecorder::Mode::Replay, sites[i], SettingsLifetimeVictim(victim), nullptr, &isFired);
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
        qInfo().noquote() << QStringLiteral("not reproduced (the emission differs in the run, or the page is not shown): ") + site;
    }

    // Almost every site is reproduced (a site of a timer or of the order of the events
    // may move to another step; the page victim exists only on the Settings routes)
    QVERIFY2(onlySite >= 0 || replayed * 10 >= (replayed + int(notReproduced.size())) * 7, qPrintable(notReproduced.join(QLatin1Char('\n'))));
}

void AppSettingsLifetimeTest::openDialogsSurviveChanges_data()
{
    QTest::addColumn<QString>("dialog");
    QTest::addColumn<QString>("change");

    for (const char* dialog : { "restore", "clearHistory", "removeCertificates", "shortcutConflict", "storageImport", "storageDelete", "storageError",
                                "languagePopup", "colorSchemePopup", "voicePopup", "colorPalette", "exportInfoBar" })
    {
        for (const char* change : { "translator", "otherInstanceLanguage", "otherInstanceValue" })
        {
            QTest::newRow(QStringLiteral("%1-%2").arg(QLatin1String(dialog), QLatin1String(change)).toLatin1().constData()) << QString::fromLatin1(dialog) << QString::fromLatin1(change);
        }
    }
}

void AppSettingsLifetimeTest::openDialogsSurviveChanges()
{
    // Every dialog and popup of the page is open while the language changes (a
    // translator of the application, the language written by another instance - the
    // page is built again) or another instance writes the settings, which it shows.
    // Nothing crashes, nothing is written by the change itself, the dialog is either
    // closed with its section or still works, and the window stays usable.
    QFETCH(QString, dialog);
    QFETCH(QString, change);
    using H = SettingsLifetimeHelper;
    using K = pdf::PDFSettingKeys;

    const bool isStorage = dialog.startsWith(QStringLiteral("storage")) || dialog == QStringLiteral("exportInfoBar");
    QTemporaryDir dataDirectory;
    std::unique_ptr<pdftest::AppSettingsFixture> fixture = createFixture(isStorage ? QStringLiteral("storage") : QString(), dataDirectory);
    QVERIFY(fixture && fixture->window);
    SettingsLifetimeRun run;
    run.fixture = fixture.get();
    run.window = fixture->window;
    run.tab = fixture->window->getHomeTab();

    // Open the dialog / popup; the key, which another instance writes meanwhile
    QPointer<QWidget> shown;
    QString otherKey = K::magnifierZoom();
    QVariant otherValue = 3.0;
    if (dialog == QStringLiteral("restore"))
    {
        QVERIFY(!fixture->settings().setValue(K::magnifierZoom(), 4.0).isError());
        QVERIFY(H::navigate(run, QStringLiteral("/settings/viewing")));
        QVERIFY(H::openRestoreDialog(run));
        shown = H::page(run)->getRestoreDialog();
        otherValue = QVariant();
    }
    else if (dialog == QStringLiteral("clearHistory"))
    {
        pdf::PDFDatabaseStatement statement(fixture->context->getDatabase()->getConnection(), "INSERT OR REPLACE INTO tool_pair(from_tool, to_tool, count, last_used) VALUES ('merge', 'compress', 4, 1)");
        QVERIFY(statement.execute());
        QVERIFY(!fixture->settings().setValue(K::toolsHistoryRuns(), 4).isError());
        QVERIFY(H::navigate(run, QStringLiteral("/settings/general")));
        pdf::PDFAppGeneralSettingsSection* general = H::section<pdf::PDFAppGeneralSettingsSection>(run);
        QVERIFY(general && general->openClearHistoryDialog());
        shown = general->getClearHistoryDialog();
        otherKey = K::toolsHistoryRuns();
        otherValue = 0;
    }
    else if (dialog == QStringLiteral("removeCertificates"))
    {
        pdf::PDFTrustedCertificateRepository repository(fixture->context->getDatabase());
        QVERIFY(!repository.addCertificate(H::certificate(0)).isError());
        QVERIFY(H::navigate(run, QStringLiteral("/settings/security")));
        pdf::PDFAppCertificateTable* table = H::section<pdf::PDFAppSecuritySettingsSection>(run)->getCertificateTable();
        QVERIFY(H::waitFor([table]() { return !table->isLoadingSystemCertificates(); }));
        const QModelIndex index = table->getModel()->index(0, pdf::PDFAppCertificateTableModel::NameColumn);
        QVERIFY(H::click(table->getView()->viewport(), table->getView()->visualRect(index).center()));
        QVERIFY(table->openRemoveDialog());
        shown = table->getRemoveDialog();
    }
    else if (dialog == QStringLiteral("shortcutConflict"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/settings/shortcuts")));
        QVERIFY(H::startRecording(run, pdf::PDFAppCommands::save()));
        QVERIFY(H::record(run, Qt::Key_O, Qt::ControlModifier));
        QVERIFY(H::waitFor([&run]() { return H::section<pdf::PDFAppShortcutsSettingsSection>(run)->getConflictDialog(); }));
        shown = H::section<pdf::PDFAppShortcutsSettingsSection>(run)->getConflictDialog();
    }
    else if (dialog == QStringLiteral("storageImport") || dialog == QStringLiteral("storageError") || dialog == QStringLiteral("exportInfoBar"))
    {
        const QString backupPath = fixture->path(QStringLiteral("backup.pdf4qt-backup"));
        const QString garbagePath = fixture->path(QStringLiteral("garbage.pdf4qt-backup"));
        QFile garbage(garbagePath);
        QVERIFY(garbage.open(QFile::WriteOnly));
        garbage.write(QByteArray(8192, 'x'));
        garbage.close();
        QVERIFY(!fixture->context->getDatabase()->exportBackup(backupPath).isError());
        pdf::PDFAppFileDialogs::setSaveBackupHandler([backupPath](QWidget*, const QString&) { return backupPath; });
        const QString importPath = dialog == QStringLiteral("storageError") ? garbagePath : backupPath;
        pdf::PDFAppFileDialogs::setOpenBackupHandler([importPath](QWidget*, const QString&) { return importPath; });
        QVERIFY(H::navigate(run, QStringLiteral("/settings/storage")));
        pdf::PDFAppStorageSettingsSection* storage = H::section<pdf::PDFAppStorageSettingsSection>(run);
        if (dialog == QStringLiteral("exportInfoBar"))
        {
            QVERIFY(H::click(storage->getExportButton()));
            shown = storage->getExportInfoBar();
        }
        else
        {
            QVERIFY(H::click(storage->getImportButton()));
            shown = H::storageDialog(run);
        }
    }
    else if (dialog == QStringLiteral("storageDelete"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/settings/storage")));
        pdf::PDFAppStorageSettingsSection* storage = H::section<pdf::PDFAppStorageSettingsSection>(run);
        QVERIFY(!storage->getFileRows().isEmpty());
        QVERIFY(H::click(storage->getFileRows().front()->getControlWidget()));
        shown = H::storageDialog(run);
    }
    else if (dialog == QStringLiteral("languagePopup") || dialog == QStringLiteral("colorSchemePopup"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/settings/general")));
        pdf::PDFAppSettingControl* settingControl = H::control(run, dialog == QStringLiteral("languagePopup") ? QStringLiteral("language") : QStringLiteral("color-scheme"));
        settingControl->getComboBox()->showPopup();
        H::settle();
        shown = settingControl->getComboBox()->view();
        otherKey = dialog == QStringLiteral("languagePopup") ? K::appearanceLanguage() : K::colorScheme();
        otherValue = dialog == QStringLiteral("languagePopup") ? QStringLiteral("fr") : QStringLiteral("dark");
    }
    else if (dialog == QStringLiteral("voicePopup"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/settings/speech")));
        pdf::PDFAppSpeechSettingsSection* speech = H::section<pdf::PDFAppSpeechSettingsSection>(run);
        QVERIFY(H::waitFor([speech]() { return speech->getEngine(); }));
        speech->getVoiceComboBox()->showPopup();
        H::settle();
        shown = speech->getVoiceComboBox()->view();
        otherKey = K::speechVolume();
        otherValue = 0.25;
    }
    else if (dialog == QStringLiteral("colorPalette"))
    {
        QVERIFY(H::navigate(run, QStringLiteral("/settings/color-modes")));
        pdf::PDFColorButton* button = H::control(run, QStringLiteral("text-color"))->getColorButton();
        QVERIFY(button->showPalette());
        H::settle();
        shown = button->getPalette();
        otherKey = K::colorModesTextColor();
        otherValue = QStringLiteral("#00FF00");
    }
    QVERIFY2(shown && shown->isVisible(), qPrintable(dialog));
    const pdf::PDFSettingsRepository settings = fixture->settings();
    const QStringList changedBefore = settings.getChangedKeys(pdf::PDFSettingDefinitions::sectionViewing()) + settings.getChangedKeys(pdf::PDFSettingDefinitions::sectionGeneral());

    // The change while it is open
    SettingsLifetimeTranslator translator;
    if (change == QStringLiteral("translator"))
    {
        QVERIFY(QCoreApplication::installTranslator(&translator));
        H::settle();
    }
    else if (change == QStringLiteral("otherInstanceLanguage"))
    {
        // The language of another instance (a real translation, or the error of a
        // missing one) - the whole user interface is translated again
        QVERIFY(H::writeOther(run, K::appearanceLanguage(), QStringLiteral("de")));
    }
    else
    {
        QVERIFY(H::writeOther(run, otherKey, otherValue));
    }
    QVERIFY(run.window);
    if (change != QStringLiteral("otherInstanceValue"))
    {
        // Nothing is written by a language change itself
        const QStringList changedAfter = settings.getChangedKeys(pdf::PDFSettingDefinitions::sectionViewing()) + settings.getChangedKeys(pdf::PDFSettingDefinitions::sectionGeneral());
        QStringList expected = changedBefore;
        if (change == QStringLiteral("otherInstanceLanguage") && !expected.contains(K::appearanceLanguage()))
        {
            expected << K::appearanceLanguage();
        }
        QStringList sortedAfter = changedAfter;
        sortedAfter.sort();
        expected.sort();
        QCOMPARE(sortedAfter, expected);
    }

    // The dialog is closed by Esc when it is still open, the language goes back
    if (shown && shown->isVisible())
    {
        H::key(shown->window(), Qt::Key_Escape);
        H::settle();
    }
    if (change == QStringLiteral("translator"))
    {
        QCoreApplication::removeTranslator(&translator);
    }
    else if (change == QStringLiteral("otherInstanceLanguage"))
    {
        QVERIFY(H::writeOther(run, K::appearanceLanguage(), QVariant()));
    }
    H::settle();
    QTest::qWait(50);
    H::settle();

    // The window works: every section opens again
    QVERIFY(run.window && run.tab);
    for (const QString& sectionId : pdf::PDFAppSettingsSectionRegistry::getSectionIds())
    {
        QVERIFY2(H::navigate(run, QStringLiteral("/settings/") + sectionId), qPrintable(sectionId));
    }
}

QTEST_MAIN(AppSettingsLifetimeTest)
#include "tst_appsettingslifetimetest.moc"
