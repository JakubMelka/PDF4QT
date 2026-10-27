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

// Settings › Shortcuts and Storage (package D of step 5): the table of the
// shortcuts (recording, conflicts with scopes, reserved keys, reset / remove,
// search, Restore defaults, another instance) and the section Storage (the
// database file and its state, export / import of the settings, the other
// database files and their deletion), with the common cases of the packages.

#include "appsettingsfixture.h"

#include "pdfappcommand.h"
#include "pdfappcertificatetable.h"
#include "pdfappcommandline.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfappsettingssection.h"
#include "pdfappsettingssection_shortcuts.h"
#include "pdfappsettingssection_storage.h"
#include "pdfappsearchengine.h"
#include "pdfappshortcutsmodel.h"
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

#include "pdfcomponentpanels.h"
#include "pdfdocumenttoolsearch.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QThread>
#include <QTranslator>
#include <QtTest>

#include <atomic>

/// Helpers of the tests of package D
class ShortcutsTestHelper
{
public:
    ShortcutsTestHelper() = delete;

    static pdf::PDFAppShortcutsSettingsSection* shortcuts(pdf::PDFAppSettingsPage* page)
    {
        return page ? qobject_cast<pdf::PDFAppShortcutsSettingsSection*>(page->getSection()) : nullptr;
    }

    static pdf::PDFAppStorageSettingsSection* storage(pdf::PDFAppSettingsPage* page)
    {
        return page ? qobject_cast<pdf::PDFAppStorageSettingsSection*>(page->getSection()) : nullptr;
    }

    /// Index of the command in the view (the filter model)
    static QModelIndex viewIndex(pdf::PDFAppShortcutsSettingsSection* section, const QString& commandId)
    {
        const int row = section->getModel()->findRow(commandId);
        return row < 0 ? QModelIndex() : section->getFilterModel()->mapFromSource(section->getModel()->index(row));
    }

    /// Records a shortcut as the user does: the row current, F2, the keys
    static bool record(pdf::PDFAppShortcutsSettingsSection* section, const QString& commandId, Qt::Key key, Qt::KeyboardModifiers modifiers)
    {
        const QModelIndex index = viewIndex(section, commandId);
        pdf::PDFAppShortcutsListView* view = section->getView();
        if (!index.isValid() || !view)
        {
            return false;
        }

        view->setFocus(Qt::TabFocusReason);
        view->setCurrentIndex(index);
        view->scrollTo(index);
        QTest::keyClick(view, Qt::Key_F2);
        QApplication::processEvents();
        pdf::PDFShortcutRecorder* recorder = view->findChild<pdf::PDFShortcutRecorder*>();
        if (!recorder || !section->getDelegate()->isRecording(index))
        {
            return false;
        }

        QTest::keyClick(recorder, key, modifiers);
        pdftest::AppDocumentFixture::settle();
        return true;
    }

    /// Stored text of the shortcut of the command (empty optional = not stored)
    static std::optional<QString> stored(pdf::PDFAppContext* context, const QString& commandId)
    {
        const std::map<QString, QString> shortcuts = context->getShortcuts().getShortcuts();
        auto it = shortcuts.find(commandId);
        return it != shortcuts.end() ? std::optional<QString>(it->second) : std::nullopt;
    }

    /// Picture of the widget at the device pixel ratio
    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// Number of the pixels of the leak color (a color of a palette)
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak ? 1 : 0;
            }
        }
        return count;
    }

    static QScrollArea* scrollArea(QWidget* page)
    {
        for (QWidget* widget = page; widget; widget = widget->parentWidget())
        {
            if (QScrollArea* area = qobject_cast<QScrollArea*>(widget))
            {
                return area;
            }
        }
        return nullptr;
    }

    /// Writes a database file of \p version with the setting appearance.colorScheme
    static bool createSchemaFile(const QString& filePath, int version, const QByteArray& value)
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, &error);
        if (!handle)
        {
            return false;
        }
        error = pdf::PDFDatabaseSchema::migrate(handle, getMigrations(qMax(0, version - pdf::PDFDatabaseSchema::getCurrentVersion())), version);
        if (!error.isError())
        {
            const QByteArray sql = "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', '" + value + "', 0)";
            error = pdf::PDFDatabase::executeOnHandle(handle, sql.constData());
        }
        pdf::PDFDatabase::closeHandle(handle);
        return !error.isError();
    }

    /// Migrations of the application plus \p extraVersions newer versions
    static std::vector<pdf::PDFDatabaseMigration> getMigrations(int extraVersions)
    {
        std::vector<pdf::PDFDatabaseMigration> migrations = pdf::PDFDatabaseSchema::getMigrations();
        for (int i = 0; i < extraVersions; ++i)
        {
            pdf::PDFDatabaseMigration migration;
            migration.version = int(migrations.size()) + 1;
            migration.sql = "CREATE TABLE test_extra" + QByteArray::number(migration.version) + "(id INTEGER PRIMARY KEY);";
            migrations.push_back(migration);
        }
        return migrations;
    }

    /// The visible dialog of the section Storage
    static pdf::PDFAppStorageDialog* storageDialog()
    {
        return pdftest::AppDocumentFixture::findDialog<pdf::PDFAppStorageDialog>();
    }

    /// PDF4QT_APP_SCREENSHOTS: a picture of the widget (a dialog) at 1x and 2x
    static void saveShot(const QString& name, QWidget* widget)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        if (folder.isEmpty() || !widget)
        {
            return;
        }
        render(widget, 1.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
        render(widget, 2.0).save(QDir(folder).absoluteFilePath(name + QStringLiteral("@2x.png")));
    }

    /// Texts of the labels of a widget (a dialog)
    static QString labelTexts(const QWidget* widget)
    {
        QStringList texts;
        for (const QLabel* label : widget->findChildren<QLabel*>())
        {
            texts << label->text();
        }
        return texts.join(QLatin1Char(' '));
    }
};

/// Thread writing a shortcut by its own connection (another instance)
class ShortcutWriterThread : public QThread
{
public:
    ShortcutWriterThread(pdf::PDFDatabase* database, QString commandId, QString keys) :
        m_database(database),
        m_commandId(std::move(commandId)),
        m_keys(std::move(keys))
    {
    }

    std::atomic_bool isSuccess = false;

protected:
    virtual void run() override
    {
        pdf::PDFShortcutRepository repository(m_database);
        isSuccess = !repository.setShortcut(m_commandId, m_keys).isError();
    }

private:
    pdf::PDFDatabase* m_database;
    QString m_commandId;
    QString m_keys;
};

/// Fixture with a database on the disk in a folder of the test (the database files
/// of the schema versions, O19): the context of the settings fixture is replaced by
/// a context, which is not portable
class StorageFixture : public pdftest::AppSettingsFixture
{
public:
    explicit StorageFixture(const QString& folder = QString(), int extraVersions = 0, QSize size = QSize(1440, 960)) :
        AppSettingsFixture(size)
    {
        delete window.data();
        settle();
        context.reset();

        dataDirectory = folder.isEmpty() ? path(QStringLiteral("data")) : folder;
        QDir().mkpath(dataDirectory);
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(QStringList({ QStringLiteral("PDF4QT") }), QDir::currentPath()));
        pdf::PDFDatabaseOptions options;
        options.directory = dataDirectory;
        options.memoryName = QStringLiteral("/pdf4qt-storage-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
        if (extraVersions > 0)
        {
            options.migrations = ShortcutsTestHelper::getMigrations(extraVersions);
        }
        context->open(options);
        if (context->getNotifier())
        {
            context->getNotifier()->setPollInterval(20);
        }

        window = new pdf::PDFAppMainWindow(context.get());
        window->getNavigator()->setExternalUrlsEnabled(false);
        window->resize(size);
        window->show();
        if (!QTest::qWaitForWindowExposed(window.data()))
        {
            qWarning("The window was not exposed");
        }
        settle();
        closeMessageBoxes();
    }

    ~StorageFixture()
    {
        pdf::PDFAppFileDialogs::setSaveBackupHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
        pdf::PDFAppFileDialogs::setOpenBackupHandler(pdf::PDFAppFileDialogs::FolderHandler());
    }

    QString dataDirectory;
    static int s_counter;
};

int StorageFixture::s_counter = 0;

/// Translator of the language change test
class ShortcutsTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray source(sourceText ? sourceText : "");
        const QByteArray contextText(context ? context : "");
        if (contextText == "pdf::PDFAppStorageSettingsSection" && source == "Export settings")
        {
            return QStringLiteral("Einstellungen exportieren");
        }
        if (contextText == "pdf::PDFAppShortcutsSettingsSection" && source == "Search actions or shortcuts…")
        {
            return QString::fromUtf8("Aktionen suchen…");
        }
        if (contextText == "pdf::PDFAppShortcutsTableHeader" && source == "Action")
        {
            return QStringLiteral("Aktion");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

class AppSettingsShortcutsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void sectionsSmoke();

    // Shortcuts
    void shortcutTableLayout();
    void shortcutRecordApplies();
    void shortcutConflictScopes();
    void shortcutConflictAssign();
    void shortcutReservedKeys();
    void shortcutResetAndRemove();
    void shortcutSearch();
    void shortcutsRestoreDefaults();
    void shortcutsOtherInstance();
    void shortcutsHostileStoredValues();
    void shortcutControlAltLayouts();
    void shortcutModelDefensive();
    void shortcutResetConflicts();

    // Storage
    void storageFilesList();
    void storageDeleteOlder();
    void storageDeleteInUse();
    void storageDeleteHostile();
    void storageExportImport();
    void storageImportErrors();
    void storagePortableAndMemory();
    void storageReadOnlyNewer();
    void storageExportKeepsExistingFile();
    void storageFilterOfRebuiltRows();
    void storageImportSecurity();
    void importWeakensMcp();
    void reviewImportTrustCheckedAtAccept();
    void storageBackupFolder();

    // Common
    void searchAndAnchors();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
    void screenshots();
};

void AppSettingsShortcutsTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppSettingsShortcutsTest::sectionsSmoke()
{
    // Every section of the package opens with its rows of the definitions
    pdftest::AppSettingsFixture fixture;
    for (const QString& sectionId : { QStringLiteral("shortcuts"), QStringLiteral("storage") })
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY2(page, qPrintable(sectionId));
        QCOMPARE(page->getSectionId(), sectionId);
        QVERIFY(page->getSection());
        const size_t definitions = fixture.context->getSettingRegistry()->getSectionDefinitions(sectionId).size();
        QVERIFY(size_t(page->getSection()->getRows().size()) >= definitions);
    }
}

void AppSettingsShortcutsTest::shortcutTableLayout()
{
    // 147:4184: one group with its hint, the search field, the card of 540 with the
    // header Action / Shortcut and a row per command
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(section);
    QCOMPARE(page->getSectionHeader()->getTitleLabel()->text(), QStringLiteral("Shortcuts"));
    QVERIFY(page->getRestoreButton());

    QCOMPARE(section->getGroups().size(), 1);
    pdf::PDFAppSettingGroup* group = section->getGroups().front();
    QCOMPARE(group->getTitle(), QStringLiteral("Keyboard shortcuts"));
    QVERIFY(group->findChild<pdf::PDFAppParagraph*>(QStringLiteral("hint"))->text().contains(QStringLiteral("Click a shortcut to record a new one")));
    QCOMPARE(section->getSearchField()->placeholderText(), QString::fromUtf8("Search actions or shortcuts…"));
    QCOMPARE(section->getTableCard()->height(), pdf::PDFAppShortcutsSettingsSection::TABLE_HEIGHT);
    QCOMPARE(section->getSearchField()->width(), section->getTableCard()->width());

    const int commandCount = int(fixture.context->getCommandRegistry()->getCommands().size());
    QCOMPARE(section->getModel()->rowCount(), commandCount);
    QCOMPARE(section->getFilterModel()->rowCount(), commandCount);

    // The rows: 40 high, the title, the icon and the effective shortcut
    const QModelIndex open = ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::open());
    QVERIFY(open.isValid());
    QCOMPARE(section->getView()->visualRect(open).height(), 40);
    QCOMPARE(open.data(Qt::DisplayRole).toString(), fixture.context->getCommandRegistry()->findCommand(pdf::PDFAppCommands::open())->getTitle());
    QCOMPARE(open.data(pdf::PDFComponentRoles::ShortcutRole).value<QKeySequence>(), QKeySequence(Qt::CTRL | Qt::Key_O));
    QVERIFY(!open.data(pdf::PDFComponentRoles::IconNameRole).toString().isEmpty());
    QVERIFY(open.flags().testFlag(Qt::ItemIsEditable));
    QCOMPARE(section->getView()->accessibleName(), QStringLiteral("Keyboard shortcuts"));

    // The header and the rows end 19 px before the edge of the card (pr 18 + border)
    pdf::PDFAppShortcutsTableHeader* header = section->getTableCard()->findChild<pdf::PDFAppShortcutsTableHeader*>();
    QVERIFY(header);
    const QRect viewport(section->getView()->viewport()->mapTo(section->getTableCard(), QPoint(0, 0)), section->getView()->viewport()->size());
    QCOMPARE(section->getTableCard()->width() - viewport.right() - 1, 19);
    QCOMPARE(viewport.left(), 9);
}

void AppSettingsShortcutsTest::shortcutRecordApplies()
{
    // A recorded shortcut is stored and applies at once: the action of the window,
    // the canvas action of an open document, the tooltip key of the commands
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(section);

    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::open(), Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier));
    const QKeySequence expected(Qt::CTRL | Qt::SHIFT | Qt::Key_J);
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()), std::optional<QString>(expected.toString(QKeySequence::PortableText)));
    QCOMPARE(ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::open()).data(pdf::PDFComponentRoles::ShortcutRole).value<QKeySequence>(), expected);
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::open())->shortcut(), expected);
    QVERIFY(ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::open()).data(pdf::PDFAppShortcutsModel::IsChangedRole).toBool());

    // A canvas command of the document view
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::copy(), Qt::Key_B, Qt::ControlModifier | Qt::AltModifier));
    const QKeySequence copy = section->getModel()->getShortcut(pdf::PDFAppCommands::copy());
    QVERIFY(!copy.isEmpty());
    QVERIFY(copy != QKeySequence(Qt::CTRL | Qt::Key_C));
    QAction* canvasAction = tab->getView() ? tab->getView()->getCanvasAction(pdf::PDFAppCommands::copy()) : nullptr;
    if (canvasAction)
    {
        QTRY_VERIFY(canvasAction->shortcuts().contains(copy));
    }

    // The default shortcut recorded again removes the own row
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::open(), Qt::Key_O, Qt::ControlModifier));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()));
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::open())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_O));

    // Esc cancels a recording: nothing changes
    const QModelIndex index = ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::open());
    section->getView()->setCurrentIndex(index);
    QTest::keyClick(section->getView(), Qt::Key_F2);
    pdf::PDFShortcutRecorder* recorder = section->getView()->findChild<pdf::PDFShortcutRecorder*>();
    QVERIFY(recorder);
    QTest::keyClick(recorder, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!section->getDelegate()->isRecording(index));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()));
}

void AppSettingsShortcutsTest::shortcutConflictScopes()
{
    // getConflictingCommands: a key of the canvas and a key of a panel do not
    // conflict, a key of the window and of the canvas do, a prefix does
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(section);
    pdf::PDFAppShortcutsModel* model = section->getModel();
    const pdf::PDFAppCommandRegistry* registry = fixture.context->getCommandRegistry();
    QCOMPARE(registry->findCommand(pdf::PDFAppCommands::annotationProperties())->scope, pdf::PDFAppCommandScope::Canvas);
    QCOMPARE(registry->findCommand(pdf::PDFAppCommands::outlineRename())->scope, pdf::PDFAppCommandScope::Panel);

    // F2 of the canvas and of the panel stay together
    QVERIFY(!model->getConflicts(pdf::PDFAppCommands::outlineRename(), QKeySequence(Qt::Key_F2)).contains(pdf::PDFAppCommands::annotationProperties()));
    QVERIFY(!model->getConflicts(pdf::PDFAppCommands::annotationProperties(), QKeySequence(Qt::Key_F2)).contains(pdf::PDFAppCommands::outlineRename()));

    // Ctrl+O (window) on a canvas command conflicts with Open
    QVERIFY(model->getConflicts(pdf::PDFAppCommands::copy(), QKeySequence(Qt::CTRL | Qt::Key_O)).contains(pdf::PDFAppCommands::open()));

    // A sequence starting with Ctrl+O conflicts with it (a prefix)
    QVERIFY(model->getConflicts(pdf::PDFAppCommands::save(), QKeySequence(QKeyCombination(Qt::CTRL | Qt::Key_O), QKeyCombination(Qt::CTRL | Qt::Key_D))).contains(pdf::PDFAppCommands::open()));

    // An alternate key of a command conflicts while its shortcut is not changed
    QVERIFY(model->getConflicts(pdf::PDFAppCommands::save(), QKeySequence(Qt::ALT | Qt::Key_Return)).contains(pdf::PDFAppCommands::annotationProperties()) ||
            model->getConflicts(pdf::PDFAppCommands::copy(), QKeySequence(Qt::ALT | Qt::Key_Return)).contains(pdf::PDFAppCommands::annotationProperties()));

    // The model refuses a conflicting value written into the row (the dialog asks)
    QSignalSpy conflictSpy(model, &pdf::PDFAppShortcutsModel::conflictFound);
    const QModelIndex copy = model->index(model->findRow(pdf::PDFAppCommands::copy()));
    QVERIFY(!model->setData(copy, QVariant::fromValue(QKeySequence(Qt::CTRL | Qt::Key_O)), pdf::PDFComponentRoles::ShortcutRole));
    QCOMPARE(conflictSpy.count(), 0);
    QTRY_COMPARE(conflictSpy.count(), 1);
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::copy()));
    QVERIFY(section->getConflictDialog());
    section->getConflictDialog()->reject();
    fixture.settle();

    // F2 for a panel command: allowed beside the canvas F2, written at once
    const QModelIndex outlineRename = model->index(model->findRow(pdf::PDFAppCommands::outlineRename()));
    QVERIFY(model->setData(outlineRename, QVariant::fromValue(QKeySequence(Qt::Key_F2)), pdf::PDFComponentRoles::ShortcutRole));
    QCOMPARE(conflictSpy.count(), 1);
}

void AppSettingsShortcutsTest::shortcutConflictAssign()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(section);

    // Ctrl+O recorded for Save: the dialog asks, Cancel changes nothing
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::save(), Qt::Key_O, Qt::ControlModifier));
    QTRY_VERIFY(section->getConflictDialog());
    pdf::PDFAppShortcutConflictDialog* dialog = section->getConflictDialog();
    QVERIFY(dialog->isVisible());
    // The first focus on the Primary Cancel, not on Assign Shortcut (FX5-2)
    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->getCancelButton()));
    QVERIFY(dialog->getFacts()->getRowCount() == 3);
    QCOMPARE(dialog->getFacts()->getKey(1), QStringLiteral("Used by"));
    QVERIFY(dialog->getFacts()->getValue(1).contains(fixture.context->getCommandRegistry()->findCommand(pdf::PDFAppCommands::open())->getTitle()));
    // The names of the commands in quotes ("Open…" is not followed by a bare period)
    QVERIFY(ShortcutsTestHelper::labelTexts(dialog).contains(QString::fromUtf8("of “%1”.").arg(fixture.context->getCommandRegistry()->findCommand(pdf::PDFAppCommands::open())->getTitle())));
    QTest::mouseClick(dialog->getCancelButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!section->getConflictDialog());
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::save()));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()));

    // Esc of the dialog is Cancel
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::save(), Qt::Key_O, Qt::ControlModifier));
    QTRY_VERIFY(section->getConflictDialog());
    QTest::keyClick(section->getConflictDialog(), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!section->getConflictDialog());
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::save()));

    // Assign: both rows in one transaction (one notification of the change log for
    // the two keys, never a state with both shortcuts)
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::save(), Qt::Key_O, Qt::ControlModifier));
    QTRY_VERIFY(section->getConflictDialog());
    QSignalSpy committedSpy(fixture.context->getDatabase(), &pdf::PDFDatabase::committed);
    QTest::mouseClick(section->getConflictDialog()->getAssignButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::save()), std::optional<QString>(QStringLiteral("Ctrl+O")));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()), std::optional<QString>(QString()));
    QCOMPARE(committedSpy.count(), 1);
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::save())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_O));
    QTRY_VERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::open())->shortcut().isEmpty());
    QVERIFY(section->getModel()->getConflicts(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O)).isEmpty());

    // An alternate key: the other command keeps its own shortcut without its alternates
    pdf::PDFAppShortcutsModel* model = section->getModel();
    const QStringList conflicts = model->getConflicts(pdf::PDFAppCommands::save(), QKeySequence(Qt::ALT | Qt::Key_Return));
    if (conflicts.contains(pdf::PDFAppCommands::annotationProperties()))
    {
        const QKeySequence own = model->getShortcut(pdf::PDFAppCommands::annotationProperties());
        QVERIFY(!model->assign(pdf::PDFAppCommands::save(), QKeySequence(Qt::ALT | Qt::Key_Return), conflicts).isError());
        QCOMPARE(model->getShortcut(pdf::PDFAppCommands::annotationProperties()), own);
        QCOMPARE(fixture.context->getCommandRegistry()->getShortcuts(pdf::PDFAppCommands::annotationProperties(), model->getUserShortcuts()).size(), qsizetype(1));
    }

    // Invalid assignments are refused
    QVERIFY(model->assign(QStringLiteral("no.command"), QKeySequence(Qt::Key_F7), {}).isError());
    QVERIFY(model->assign(pdf::PDFAppCommands::save(), QKeySequence(), {}).isError());
    QVERIFY(model->assign(pdf::PDFAppCommands::save(), QKeySequence(Qt::Key_Escape), {}).isError());

    // A newer conflict replaces an open dialog
    section->openConflictDialog(pdf::PDFAppCommands::print(), QKeySequence(Qt::CTRL | Qt::Key_Z), { pdf::PDFAppCommands::undo() });
    QPointer<pdf::PDFAppShortcutConflictDialog> first = section->getConflictDialog();
    section->openConflictDialog(pdf::PDFAppCommands::print(), QKeySequence(Qt::CTRL | Qt::Key_Y), { pdf::PDFAppCommands::redo() });
    fixture.settle();
    QVERIFY(!first);
    QVERIFY(section->getConflictDialog());
    section->getConflictDialog()->reject();
    fixture.settle();
}

void AppSettingsShortcutsTest::shortcutReservedKeys()
{
    using Model = pdf::PDFAppShortcutsModel;
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::Key_Escape)).isEmpty());
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::Key_Tab)).isEmpty());
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::Key_Backtab)).isEmpty());
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::Key_Return)).isEmpty());
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::Key_Enter)).isEmpty());
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::ALT | Qt::Key_F4)).isEmpty());
    QVERIFY(!Model::getRefusalReason(QKeySequence(Qt::Key_Control)).isEmpty());
    QVERIFY(Model::getRefusalReason(QKeySequence(Qt::CTRL | Qt::Key_Return)).isEmpty());
    QVERIFY(Model::getRefusalReason(QKeySequence(Qt::Key_F4)).isEmpty());
    QVERIFY(Model::getRefusalReason(QKeySequence()).isEmpty());

    // Recorded Alt+F4 / Enter: refused with the status message of the page
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::print(), Qt::Key_F4, Qt::AltModifier));
    QTRY_VERIFY(!page->getStatusLabel()->isHidden());
    QVERIFY(page->getStatusLabel()->text().contains(QStringLiteral("closes the window")));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::print()));

    const QModelIndex print = section->getModel()->index(section->getModel()->findRow(pdf::PDFAppCommands::print()));
    QVERIFY(!section->getModel()->setData(print, QVariant::fromValue(QKeySequence(Qt::Key_Return)), pdf::PDFComponentRoles::ShortcutRole));
    QVERIFY(!section->getModel()->setData(print, QVariant(42), pdf::PDFComponentRoles::ShortcutRole));
    QVERIFY(!section->getModel()->setData(print, QStringLiteral("Ctrl+Nonsense+%%"), pdf::PDFComponentRoles::ShortcutRole));
    QVERIFY(!section->getModel()->setData(print, QVariant::fromValue(QKeySequence(Qt::Key_F9)), Qt::DisplayRole));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::print()));
}

void AppSettingsShortcutsTest::shortcutResetAndRemove()
{
    // The context menu of a row: Remove shortcut and Reset to Default
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(section);
    pdf::PDFAppShortcutsListView* view = section->getView();
    pdf::PDFAppShortcutDelegate* delegate = section->getDelegate();

    QModelIndex index = ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::print());
    QStyleOptionViewItem option;
    option.rect = view->visualRect(index);
    option.widget = view;
    QList<pdf::PDFRowDelegate::Action> actions = delegate->getActions(option, index);
    QCOMPARE(actions.size(), 3);
    QCOMPARE(actions[2].text, QStringLiteral("Reset to Default"));
    QVERIFY(!actions[2].enabled);

    delegate->triggerAction(view, index, pdf::PDFShortcutDelegate::ClearAction);
    fixture.settle();
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::print()), std::optional<QString>(QString()));
    QTRY_VERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::print())->shortcut().isEmpty());

    index = ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::print());
    actions = delegate->getActions(option, index);
    QVERIFY(actions[2].enabled);
    QVERIFY(!actions[1].enabled);
    delegate->triggerAction(view, index, pdf::PDFAppShortcutDelegate::ResetAction);
    fixture.settle();
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::print()));
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::print())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_P));

    // The context menu of the delegate offers the three items
    QMenu* menu = delegate->createContextMenu(view, index, view);
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 3);
    delete menu;
}

void AppSettingsShortcutsTest::shortcutSearch()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(section);
    const int all = section->getFilterModel()->rowCount();

    // A title, a key text (portable and native), an id, no case
    QTest::keyClicks(section->getSearchField(), QStringLiteral("zoom"));
    fixture.settle();
    QVERIFY(section->getFilterModel()->rowCount() >= 2);
    QVERIFY(section->getFilterModel()->rowCount() < all);
    for (int row = 0; row < section->getFilterModel()->rowCount(); ++row)
    {
        const QModelIndex index = section->getFilterModel()->index(row, 0);
        const QString texts = index.data(Qt::DisplayRole).toString() + index.data(pdf::PDFAppShortcutsModel::SearchTextRole).toStringList().join(QLatin1Char(' '));
        QVERIFY(texts.contains(QStringLiteral("zoom"), Qt::CaseInsensitive));
    }

    section->getSearchField()->setText(QStringLiteral("ctrl+o"));
    fixture.settle();
    QVERIFY(ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::open()).isValid());

    // No match: the empty text instead of the list
    section->getSearchField()->setText(QStringLiteral("qqqqzzzz"));
    fixture.settle();
    QCOMPARE(section->getFilterModel()->rowCount(), 0);
    QVERIFY(!section->getEmptyText()->isHidden());
    QVERIFY(section->getEmptyText()->text().contains(QStringLiteral("qqqqzzzz")));
    QVERIFY(section->getView()->isHidden());

    // Hostile texts: control characters, 10 kB
    section->getSearchField()->setText(QString(10000, QLatin1Char('x')) + QChar(0x202E) + QChar(0));
    fixture.settle();
    QVERIFY(section->getFilterModel()->getFilterText().size() <= pdf::PDFAppShortcutsFilterModel::MAXIMUM_FILTER_LENGTH);

    // Cleared: everything again; a recording through the filter writes the command
    section->getSearchField()->clear();
    fixture.settle();
    QCOMPARE(section->getFilterModel()->rowCount(), all);
    QVERIFY(section->getEmptyText()->isHidden());
    section->getSearchField()->setText(QStringLiteral("print"));
    fixture.settle();
    QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::print(), Qt::Key_F7, Qt::ControlModifier));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::print()), std::optional<QString>(QStringLiteral("Ctrl+F7")));
}

void AppSettingsShortcutsTest::shortcutsRestoreDefaults()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(section);
    QVERIFY(!page->getRestoreButton()->isEnabled());

    QVERIFY(!fixture.context->getShortcuts().setShortcuts({ { pdf::PDFAppCommands::print(), QStringLiteral("Ctrl+F8") },
                                                            { pdf::PDFAppCommands::open(), QString() },
                                                            { pdf::PDFAppCommands::save(), QStringLiteral("Ctrl+S") } }).isError());
    QTRY_VERIFY(page->getRestoreButton()->isEnabled());

    // The rows "command: current → default"; a stored default is no change
    QCOMPARE(section->getChangedKeys().size(), 2);
    QVERIFY(page->openRestoreDefaultsDialog());
    pdf::PDFAppRestoreDefaultsDialog* dialog = page->getRestoreDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getChangeCount(), 2);
    QStringList values;
    for (int i = 0; i < dialog->getFacts()->getRowCount(); ++i)
    {
        values << dialog->getFacts()->getValue(i);
    }
    QVERIFY(values.contains(QStringLiteral("Ctrl+F8 → Ctrl+P")));
    QVERIFY(values.contains(QStringLiteral("None → Ctrl+O")));
    QVERIFY(ShortcutsTestHelper::labelTexts(dialog).contains(QStringLiteral("These 2 shortcuts")));

    QTest::mouseClick(dialog->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::print())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_P));
    QTRY_VERIFY(!page->getRestoreButton()->isEnabled());
    QCOMPARE(section->getRestoreDefaultsText(1), QStringLiteral("This shortcut goes back to its default. Your settings and your history stay as they are."));
    // %Ln: the number in the locale of the user interface (O17, FX5-2)
    QVERIFY(section->getRestoreDefaultsText(1234).startsWith(QStringLiteral("These 1,234 shortcuts")));
}

void AppSettingsShortcutsTest::shortcutsOtherInstance()
{
    // A shortcut written by another connection shows in the open table and applies
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(section);

    ShortcutWriterThread thread(fixture.context->getDatabase(), pdf::PDFAppCommands::print(), QStringLiteral("Ctrl+F9"));
    thread.start();
    QVERIFY(thread.wait(30000));
    QVERIFY(thread.isSuccess);
    QTRY_COMPARE(ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::print()).data(pdf::PDFComponentRoles::ShortcutRole).value<QKeySequence>(),
                 QKeySequence(Qt::CTRL | Qt::Key_F9));
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::print())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F9));

    // A change of another instance while a row records: the recording goes on
    const QModelIndex index = ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::save());
    section->getView()->setFocus();
    section->getView()->setCurrentIndex(index);
    QTest::keyClick(section->getView(), Qt::Key_F2);
    QVERIFY(section->getDelegate()->isRecording(index));
    ShortcutWriterThread second(fixture.context->getDatabase(), pdf::PDFAppCommands::undo(), QStringLiteral("Ctrl+F10"));
    second.start();
    QVERIFY(second.wait(30000));
    QTRY_COMPARE(section->getModel()->getShortcut(pdf::PDFAppCommands::undo()), QKeySequence(Qt::CTRL | Qt::Key_F10));
    QVERIFY(section->getDelegate()->isRecording(ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::save())));
    QTest::keyClick(section->getView()->findChild<pdf::PDFShortcutRecorder*>(), Qt::Key_F11, Qt::ControlModifier);
    fixture.settle();
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::save()), std::optional<QString>(QStringLiteral("Ctrl+F11")));
}

void AppSettingsShortcutsTest::shortcutsHostileStoredValues()
{
    // Rows of another version: unknown commands, texts, which are no keys, huge texts
    pdftest::AppSettingsFixture fixture;
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    pdf::PDFDatabaseStatement insert(connection, "INSERT OR REPLACE INTO shortcut(command_id, keys) VALUES (?1, ?2)");
    const std::pair<QString, QString> rows[] = { { QStringLiteral("no.such.command"), QStringLiteral("Ctrl+Q") },
                                                 { pdf::PDFAppCommands::print(), QStringLiteral("Ctrl+Nonsense+%%") },
                                                 { pdf::PDFAppCommands::save(), QString(100000, QLatin1Char('x')) },
                                                 { pdf::PDFAppCommands::open(), QString::fromUtf8("\xE2\x80\xAE") } };
    for (const auto& [command, keys] : rows)
    {
        insert.bindText(1, command);
        insert.bindText(2, keys);
        QVERIFY(insert.execute());
    }

    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(section);
    QCOMPARE(section->getModel()->rowCount(), int(fixture.context->getCommandRegistry()->getCommands().size()));
    QCOMPARE(section->getModel()->getShortcut(pdf::PDFAppCommands::print()), QKeySequence(Qt::CTRL | Qt::Key_P));
    QCOMPARE(section->getModel()->getShortcut(pdf::PDFAppCommands::save()), QKeySequence(Qt::CTRL | Qt::Key_S));
    QVERIFY(!section->getChangedKeys().contains(QStringLiteral("no.such.command")));
    QCOMPARE(ShortcutsTestHelper::countLeakPixels(ShortcutsTestHelper::render(page, 1.0)), 0);

    // Restore defaults removes every row, also the rows of unknown commands
    QVERIFY(!section->restoreDefaults().isError());
    QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());

    // A hostile table (RS5 F2): the rows read are bounded by their count and by the
    // lengths of their texts in SQL (about twice the commands of the registry)
    using Repository = pdf::PDFShortcutRepository;
    QVERIFY(Repository::MAXIMUM_SHORTCUT_COUNT >= 2 * int(fixture.context->getCommandRegistry()->getCommands().size()));
    pdf::PDFDatabaseStatement many(connection, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000) "
                                               "INSERT INTO shortcut(command_id, keys) SELECT 'junk.' || i, 'Ctrl+J' FROM n");
    QVERIFY(many.execute());
    pdf::PDFDatabaseStatement huge(connection, "INSERT INTO shortcut(command_id, keys) VALUES ('a.' || hex(zeroblob(200)), 'Ctrl+K'), ('a.b', hex(zeroblob(200)))");
    QVERIFY(huge.execute());
    const std::map<QString, QString> shortcuts = fixture.context->getShortcuts().getShortcuts();
    QCOMPARE(int(shortcuts.size()), Repository::MAXIMUM_SHORTCUT_COUNT);
    for (const auto& [command, keys] : shortcuts)
    {
        QVERIFY(command.size() <= Repository::MAXIMUM_COMMAND_ID_LENGTH);
        QVERIFY(keys.size() <= Repository::MAXIMUM_KEYS_LENGTH);
    }
    QVERIFY(!section->restoreDefaults().isError());
    QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
}

void AppSettingsShortcutsTest::shortcutControlAltLayouts()
{
    // Ctrl+Alt on Windows is recorded as all the modifiers with the base key of the
    // physical key (PDFShortcutRecorder of the library); the model stores it portably
    using Recorder = pdf::PDFShortcutRecorder;
    const QKeyCombination czech = Recorder::getWindowsKeyCombination(Qt::Key_BracketLeft, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("["), u'f');
    QCOMPARE(czech, QKeyCombination(Qt::ControlModifier | Qt::AltModifier, Qt::Key_F));

    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(section);
    const QModelIndex print = section->getModel()->index(section->getModel()->findRow(pdf::PDFAppCommands::print()));
    QVERIFY(section->getModel()->setData(print, QVariant::fromValue(QKeySequence(czech)), pdf::PDFComponentRoles::ShortcutRole));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::print()), std::optional<QString>(QStringLiteral("Ctrl+Alt+F")));

    // A non-Latin base key is stored as its character
    const QKeyCombination russian = Recorder::getWindowsKeyCombination(Qt::Key_A, Qt::ControlModifier | Qt::AltModifier, QString(), u'\u0444');
    QVERIFY(section->getModel()->setData(print, QVariant::fromValue(QKeySequence(russian)), pdf::PDFComponentRoles::ShortcutRole));
    QCOMPARE(section->getModel()->getShortcut(pdf::PDFAppCommands::print()), QKeySequence(russian));
}

void AppSettingsShortcutsTest::shortcutModelDefensive()
{
    // A model without a context, invalid rows and indices of another model
    pdf::PDFAppShortcutsModel empty(nullptr);
    QCOMPARE(empty.rowCount(), 0);
    QVERIFY(!empty.data(empty.index(0), Qt::DisplayRole).isValid());
    QCOMPARE(empty.flags(empty.index(0)), Qt::ItemFlags(Qt::NoItemFlags));
    QVERIFY(!empty.setData(empty.index(0), QVariant::fromValue(QKeySequence(Qt::Key_F7)), pdf::PDFComponentRoles::ShortcutRole));
    QVERIFY(empty.getShortcut(pdf::PDFAppCommands::open()).isEmpty());
    QVERIFY(empty.getDefaultShortcut(pdf::PDFAppCommands::open()).isEmpty());
    QCOMPARE(empty.getCommandTitle(QStringLiteral("x.y")), QStringLiteral("x.y"));
    QVERIFY(empty.getConflicts(pdf::PDFAppCommands::open(), QKeySequence(Qt::Key_F7)).isEmpty());
    QVERIFY(empty.assign(pdf::PDFAppCommands::open(), QKeySequence(Qt::Key_F7), {}).isError());
    QVERIFY(empty.writeShortcut(pdf::PDFAppCommands::open(), QKeySequence(Qt::Key_F7)).isError());
    QVERIFY(empty.resetShortcut(pdf::PDFAppCommands::open()).isError());
    QVERIFY(empty.getCommandId(-1).isEmpty());
    QCOMPARE(empty.findRow(QStringLiteral("x.y")), -1);
    QCOMPARE(empty.rowCount(empty.index(0)), 0);

    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsModel model(fixture.context.get());
    QVERIFY(model.rowCount() > 0);
    QCOMPARE(model.rowCount(model.index(0)), 0);
    QVERIFY(model.getCommandId(model.rowCount()).isEmpty());
    QCOMPARE(model.getCommandId(0), fixture.context->getCommandRegistry()->getCommands().front()->id);
    const QModelIndex open = model.index(model.findRow(pdf::PDFAppCommands::open()));
    QCOMPARE(open.data(pdf::PDFAppShortcutsModel::DefaultShortcutRole).value<QKeySequence>(), QKeySequence(Qt::CTRL | Qt::Key_O));
    QVERIFY(!open.data(Qt::ToolTipRole).isValid());

    // An index of another model is not a row of this one
    pdf::PDFAppShortcutsModel other(fixture.context.get());
    QVERIFY(!model.data(other.index(0), Qt::DisplayRole).isValid());
    QCOMPARE(model.flags(other.index(0)), Qt::ItemFlags(Qt::NoItemFlags));
    QVERIFY(!model.setData(other.index(0), QVariant(), pdf::PDFAppShortcutsModel::ResetRole));

    // The same shortcut again writes nothing; a string; an empty value removes it
    QVERIFY(model.setData(open, QVariant::fromValue(QKeySequence(Qt::CTRL | Qt::Key_O)), pdf::PDFComponentRoles::ShortcutRole));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()));
    QVERIFY(model.setData(open, QStringLiteral("Ctrl+F12"), Qt::EditRole));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()), std::optional<QString>(QStringLiteral("Ctrl+F12")));
    QVERIFY(model.setData(open, QVariant(), pdf::PDFComponentRoles::ShortcutRole));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::open()), std::optional<QString>(QString()));
    QVERIFY(open.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("no shortcut")));
    QCOMPARE(pdf::PDFAppShortcutsModel::getShortcutText(QKeySequence()), QStringLiteral("None"));

    // A database, which refuses the writes: the errors are reported from the event loop
    QSignalSpy failedSpy(&model, &pdf::PDFAppShortcutsModel::writeFailed);
    pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_shortcut BEFORE INSERT ON shortcut BEGIN SELECT RAISE(ABORT, 'blocked'); END");
    QVERIFY(block.execute());
    pdf::PDFDatabaseStatement blockDelete(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_shortcut_delete BEFORE DELETE ON shortcut BEGIN SELECT RAISE(ABORT, 'blocked'); END");
    QVERIFY(blockDelete.execute());
    QVERIFY(model.writeShortcut(pdf::PDFAppCommands::print(), QKeySequence(Qt::Key_F7)).isError());
    QVERIFY(model.resetShortcut(pdf::PDFAppCommands::open()).isError());
    QVERIFY(model.assign(pdf::PDFAppCommands::print(), QKeySequence(Qt::Key_F7), { pdf::PDFAppCommands::open(), pdf::PDFAppCommands::print(), QStringLiteral("no.command") }).isError());
    QTRY_COMPARE(failedSpy.count(), 3);
}

void AppSettingsShortcutsTest::storageFilesList()
{
    // The other database files of the folder: an older one with Delete…, a newer
    // one with its tag; damaged names are ignored; the version, not the schema
    QTemporaryDir directory;
    const QString folder = directory.path();
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(folder, 1), 1, "dark"));
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(folder, 3), 3, "light"));
    for (const char* name : { "pdf4qt-01.db", "pdf4qt.db", "pdf4qt-1.db.damaged-2026-10-01", "pdf4qt-x.db" })
    {
        QFile file(QDir(folder).filePath(QLatin1String(name)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("garbage");
    }

    StorageFixture fixture(folder, 1);
    QCOMPARE(fixture.context->getDatabase()->getState().filePath, pdf::PDFDatabaseFiles::getFilePath(folder, 2));
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("storage"));
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(page);
    QVERIFY(section);
    QVERIFY(!page->getRestoreButton());
    QCOMPARE(page->getSectionHeader()->getSubtitleLabel()->text(), QStringLiteral("Where PDF4QT keeps your settings and history."));
    QCOMPARE(page->getNavigationItem(QStringLiteral("storage"))->getSubtitle(), QStringLiteral("Database, backup, other files"));

    // Location and state
    QCOMPARE(section->getLocationPath(), pdf::PDFDatabaseFiles::getFilePath(folder, 2));
    QVERIFY(section->getShowInFolderButton()->isEnabled());
    QCOMPARE(section->getStateValue()->getText(), QStringLiteral("Saved on this computer"));
    QCOMPARE(section->getStateValue()->getIconName(), QStringLiteral("check-circle"));
    QCOMPARE(section->getStateRow()->getDescription(), QStringLiteral("Every PDF4QT window uses this file and sees each change at once."));
    QCOMPARE(section->getLocationRow()->getDescriptionLabel()->toolTip(), QDir::toNativeSeparators(pdf::PDFDatabaseFiles::getFilePath(folder, 2)));

    // The files of 1269:14388: the older one first (schema 1), then the newer one (schema 3)
    const QList<pdf::PDFAppSettingRow*> rows = section->getFileRows();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1]->getTitle(), QStringLiteral("pdf4qt-3.db"));
    QCOMPARE(rows[1]->getTagText(), QStringLiteral("Newer version"));
    QVERIFY(rows[1]->getDescription().startsWith(QStringLiteral("Newer PDF4QT · ")));
    // The label and the tag say it is newer: no third "Used by a newer PDF4QT" (coordinator 2026-10-25)
    QVERIFY(!rows[1]->getDescription().contains(QStringLiteral("Used by")));
    QCOMPARE(rows[1]->getDescription().count(QStringLiteral(" · ")), 2);
    QVERIFY(!rows[1]->getControlWidget());
    QCOMPARE(rows[0]->getTitle(), QStringLiteral("pdf4qt-1.db"));
    QVERIFY(rows[0]->getTagText().isEmpty());
    QVERIFY(rows[0]->getDescription().startsWith(QStringLiteral("PDF4QT 2.0 · ")));
    QVERIFY(!rows[0]->getDescription().contains(QStringLiteral("Schema")));
    QVERIFY(qobject_cast<QPushButton*>(rows[0]->getControlWidget()));
    QCOMPARE(qobject_cast<QPushButton*>(rows[0]->getControlWidget())->text(), QString::fromUtf8("Delete…"));
    QVERIFY(rows[0]->mapTo(page, QPoint(0, 0)).y() < rows[1]->mapTo(page, QPoint(0, 0)).y());
    QVERIFY(section->getEmptyFilesCard()->isHidden());
    QVERIFY(!section->getFilesGroup()->isHidden());

    // A new file of another version appears, when the section is shown again
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(folder, 4), 4, "light"));
    fixture.navigate(QStringLiteral("/settings/general"));
    page = fixture.openSettings(QStringLiteral("storage"));
    QCOMPARE(ShortcutsTestHelper::storage(page)->getFileRows().size(), 3);

    // The description of a file
    pdf::PDFDatabaseFileInfo info;
    info.filePath = QStringLiteral("/x/pdf4qt-1.db");
    info.schemaVersion = 1;
    info.size = 2400000;
    info.modified = QDateTime(QDate(2026, 10, 12), QTime(9, 41));
    QCOMPARE(pdf::PDFAppStorageSettingsSection::getFileDescription(info), QString::fromUtf8("PDF4QT 2.0 · 2.4 MB · Oct 12, 2026"));
    info.schemaVersion = 3;
    QCOMPARE(pdf::PDFAppStorageSettingsSection::getFileDescription(info), QString::fromUtf8("Newer PDF4QT · 2.4 MB · Oct 12, 2026"));
}

void AppSettingsShortcutsTest::storageDeleteOlder()
{
    QTemporaryDir directory;
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1);
    QVERIFY(ShortcutsTestHelper::createSchemaFile(olderPath, 1, "dark"));

    StorageFixture fixture(directory.path(), 1);
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    QCOMPARE(section->getFileRows().size(), 1);
    QFile::copy(olderPath, olderPath + QStringLiteral("-shm"));

    // Delete…: the confirmation of 1271:15230 with the version of PDF4QT; Cancel
    QPushButton* deleteButton = qobject_cast<QPushButton*>(section->getFileRows().front()->getControlWidget());
    QTest::mouseClick(deleteButton, Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Delete the older database file?"));
    QVERIFY(dialog->getMessageText().contains(QStringLiteral("PDF4QT 2.0")));
    QVERIFY(dialog->getMessageText().contains(QStringLiteral("This cannot be undone.")));
    QCOMPARE(dialog->getFacts()->getRowCount(), 3);
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("File"));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("pdf4qt-1.db"));
    QCOMPARE(dialog->getFacts()->getKey(2), QStringLiteral("Modified"));
    QCOMPARE(dialog->getActionButton()->text(), QStringLiteral("Delete File"));
    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->getCancelButton()));   // not Delete File (FX5-2)
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Cancel"));
    QTest::mouseClick(dialog->getCancelButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(QFileInfo::exists(olderPath));

    // Delete File: the file and its -shm go, the row goes, the empty state comes
    QTest::mouseClick(qobject_cast<QPushButton*>(section->getFileRows().front()->getControlWidget()), Qt::LeftButton);
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!QFileInfo::exists(olderPath));
    QVERIFY(!QFileInfo::exists(olderPath + QStringLiteral("-shm")));
    QVERIFY(section->getFileRows().isEmpty());
    QVERIFY(!section->getEmptyFilesCard()->isHidden());
    QCOMPARE(section->getEmptyFilesCard()->sizeHint().height(), 58);   // 1270:14723: the height of a row
    QVERIFY(!ShortcutsTestHelper::storageDialog());
    fixture.screenshot(QStringLiteral("settings-storage-empty"));

    // A request for a file, which is not listed, does nothing
    section->requestDelete(fixture.context->getDatabase()->getState().filePath);
    section->requestDelete(QStringLiteral("C:/Windows/system32/x.db"));
    fixture.settle();
    QVERIFY(!ShortcutsTestHelper::storageDialog());
}

void AppSettingsShortcutsTest::storageDeleteInUse()
{
    // Another PDF4QT (a database of this process here) uses the older file: refused
    // with the error dialog; after it closes, the file is deleted
    QTemporaryDir directory;
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1);
    QVERIFY(ShortcutsTestHelper::createSchemaFile(olderPath, 1, "dark"));

    StorageFixture fixture(directory.path(), 1);
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);

    auto holder = std::make_unique<pdf::PDFDatabase>();
    pdf::PDFDatabaseOptions options;
    options.filePath = olderPath;
    options.memoryName = QStringLiteral("/pdf4qt-holder-%1").arg(QCoreApplication::applicationPid());
    holder->open(options);
    QCOMPARE(holder->getState().mode, pdf::PDFDatabaseStorageMode::Disk);

    // A running PDF4QT has its connection open (the database opens it at the first use)
    QCOMPARE(holder->getConnection()->queryInteger("SELECT COUNT(*) FROM setting", -1), qint64(1));

    section->requestDelete(olderPath);
    fixture.settle();
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppStorageDialog* error = ShortcutsTestHelper::storageDialog();
    QVERIFY(error);
    QCOMPARE(error->getTitleText(), QStringLiteral("The file could not be deleted"));
    QCOMPARE(error->getMessageText(), QStringLiteral("The file is used by another PDF4QT. Close it and try again."));
    QVERIFY(!error->getActionButton());
    QCOMPARE(error->getCancelButton()->text(), QStringLiteral("OK"));
    QTest::mouseClick(error->getCancelButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(QFileInfo::exists(olderPath));
    QCOMPARE(section->getFileRows().size(), 1);

    holder.reset();
    section->requestDelete(olderPath);
    fixture.settle();
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!QFileInfo::exists(olderPath));
    QVERIFY(!ShortcutsTestHelper::storageDialog());
}

void AppSettingsShortcutsTest::storageDeleteHostile()
{
    // The file changed under the open confirmation (replaced by a link / deleted /
    // the current file): the database decides again and refuses
    QTemporaryDir directory;
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1);
    QVERIFY(ShortcutsTestHelper::createSchemaFile(olderPath, 1, "dark"));

    StorageFixture fixture(directory.path(), 1);
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    section->requestDelete(olderPath);
    fixture.settle();
    QVERIFY(ShortcutsTestHelper::storageDialog());
    QVERIFY(QFile::remove(olderPath));
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppStorageDialog* error = ShortcutsTestHelper::storageDialog();
    QVERIFY(error);
    QCOMPARE(error->getTitleText(), QStringLiteral("The file could not be deleted"));
    error->reject();
    fixture.settle();
    QVERIFY(section->getFileRows().isEmpty());

    // The current file is never offered and never deleted
    const QString currentPath = fixture.context->getDatabase()->getState().filePath;
    QVERIFY(fixture.context->getDatabase()->deleteOlderDatabaseFile(currentPath).isError());
    QVERIFY(QFileInfo::exists(currentPath));
}

void AppSettingsShortcutsTest::storageExportImport()
{
    // Export settings…: the file dialog, the info bar 1271:15212 with Show in Folder;
    // Import settings…: the confirmation 1271:15395, the values of the backup apply
    QTemporaryDir backups;
    const QString backupPath = QDir(backups.path()).filePath(QStringLiteral("PDF4QT settings test"));
    StorageFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::print(), QStringLiteral("Ctrl+F8")).isError());

    QString proposed;
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString& filePath) { proposed = filePath; return backupPath; });
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("storage"));
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(page);
    QVERIFY(section);
    QTest::mouseClick(section->getExportButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(proposed.endsWith(pdf::PDFDatabaseBackup::getDefaultFileName(QDate::currentDate())));
    const QString exportedPath = backupPath + QStringLiteral(".pdf4qt-backup");
    QVERIFY(QFileInfo::exists(exportedPath));
    QVERIFY(pdf::PDFDatabaseBackup::read(exportedPath).isValid());

    // The info bar below the Backup card: green check, the name, Show in Folder, close
    pdf::PDFAppInfoBar* bar = section->getExportInfoBar();
    QVERIFY(bar);
    QTRY_VERIFY(bar->isVisible());
    QCOMPARE(bar->getRequest().tone, pdf::PDFAppInfoBarTone::Success);
    QCOMPARE(bar->getRequest().title, QString::fromUtf8("Settings exported to “PDF4QT settings test.pdf4qt-backup”"));
    const QRect card = bar->getCardRect().translated(bar->pos());
    const QWidget* backupCard = section->getExportRow()->parentWidget();
    const QRect backupRect(backupCard->mapTo(section, QPoint(0, 0)), backupCard->size());
    QCOMPARE(card.top(), backupRect.bottom() + 1 + 12);
    QCOMPARE(card.left(), backupRect.left());
    QCOMPARE(card.height(), pdf::PDFAppInfoBar::CARD_HEIGHT);
    QSignalSpy urlSpy(fixture.window->getNavigator(), &pdf::PDFAppNavigator::externalUrlOpened);
    QVERIFY(!bar->getActionButtons().empty());
    QTest::mouseClick(bar->getActionButtons().front(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(urlSpy.count(), 1);
    QCOMPARE(QDir::cleanPath(urlSpy.front().front().toUrl().toLocalFile()), QDir::cleanPath(backups.path()));
    fixture.screenshot(QStringLiteral("settings-storage-exported"));
    QTest::mouseClick(bar->getCloseButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!section->getExportInfoBar());

    // Show in Folder of the database
    QTest::mouseClick(section->getShowInFolderButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(urlSpy.count(), 2);

    // Changes after the export, then the import
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 5.0).isError());
    QVERIFY(!fixture.context->getShortcuts().resetAll().isError());
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return exportedPath; });
    QTest::mouseClick(section->getImportButton(), Qt::LeftButton);
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Import settings from the backup?"));
    QVERIFY(dialog->getMessageText().endsWith(QStringLiteral("This cannot be undone.")));
    QCOMPARE(dialog->getFacts()->getRowCount(), 3);
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("PDF4QT settings test.pdf4qt-backup"));
    QCOMPARE(dialog->getFacts()->getKey(2), QStringLiteral("Version"));
    QCOMPARE(dialog->getFacts()->getValue(2), QStringLiteral("PDF4QT 2.0"));
    QCOMPARE(dialog->getActionButton()->text(), QStringLiteral("Import Settings"));
    // The first focus on the Primary Cancel (FX5-2); the check box of the
    // certificates comes before the buttons, it is not the first focus either
    QCOMPARE(dialog->focusWidget(), static_cast<QWidget*>(dialog->getCancelButton()));

    // Cancel changes nothing
    QTest::mouseClick(dialog->getCancelButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getValue(pdf::PDFSettingKeys::magnifierZoom()).toDouble(), 5.0);

    QTest::mouseClick(section->getImportButton(), Qt::LeftButton);
    fixture.settle();
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getValue(pdf::PDFSettingKeys::magnifierZoom()).toDouble(), 3.0);
    QTRY_COMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::print())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F8));
    QVERIFY(!ShortcutsTestHelper::storageDialog());

    // The page is still there and shows the imported values
    page = fixture.openSettings(QStringLiteral("viewing"));
    QCOMPARE(page->findRow(QStringLiteral("magnifier-zoom"))->getControl()->getShownValue().toDouble(), 3.0);

    // Canceled file dialogs do nothing
    pdf::PDFAppFileDialogs::setOpenBackupHandler([](QWidget*, const QString&) { return QString(); });
    pdf::PDFAppFileDialogs::setSaveBackupHandler([](QWidget*, const QString&) { return QString(); });
    section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    section->importSettings();
    section->exportSettings();
    fixture.settle();
    QVERIFY(!ShortcutsTestHelper::storageDialog());
    QVERIFY(!section->getExportInfoBar());
}

void AppSettingsShortcutsTest::storageImportErrors()
{
    QTemporaryDir backups;
    StorageFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 4.0).isError());
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);

    // Not a backup
    const QString textPath = QDir(backups.path()).filePath(QStringLiteral("text.pdf4qt-backup"));
    {
        QFile file(textPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(8192, 'a'));
    }
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return textPath; });
    section->importSettings();
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("The file could not be imported"));
    QCOMPARE(dialog->getMessageText(), QStringLiteral("It is not a PDF4QT settings backup. Nothing was changed."));
    QVERIFY(!dialog->getActionButton());
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("text.pdf4qt-backup"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!ShortcutsTestHelper::storageDialog());

    // A backup of a newer PDF4QT (1271:15697): the version row
    const QString newerPath = QDir(backups.path()).filePath(QStringLiteral("newer.pdf4qt-backup"));
    {
        pdf::PDFDatabase newer;
        pdf::PDFDatabaseOptions options;
        options.portable = true;
        options.memoryName = QStringLiteral("/pdf4qt-newer-%1").arg(QCoreApplication::applicationPid());
        options.migrations = ShortcutsTestHelper::getMigrations(1);
        newer.open(options);
        QVERIFY(!newer.exportBackup(newerPath).isError());
    }
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return newerPath; });
    section->importSettings();
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getMessageText(), QStringLiteral("The backup was made by a newer version of PDF4QT. Update PDF4QT, then import it again. Nothing was changed."));
    QCOMPARE(dialog->getFacts()->getRowCount(), 2);
    QCOMPARE(dialog->getFacts()->getKey(1), QStringLiteral("Version"));
    QCOMPARE(dialog->getFacts()->getValue(1), QString::fromUtf8("Newer PDF4QT – this PDF4QT reads backups up to PDF4QT 2.0"));
    dialog->reject();
    fixture.settle();

    // A missing file, a folder
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return backups.path(); });
    section->importSettings();
    fixture.settle();
    QVERIFY(ShortcutsTestHelper::storageDialog());
    QCOMPARE(ShortcutsTestHelper::storageDialog()->getMessageText(), QStringLiteral("The file cannot be read. Nothing was changed."));
    ShortcutsTestHelper::storageDialog()->reject();
    fixture.settle();
    QCOMPARE(fixture.settings().getValue(pdf::PDFSettingKeys::magnifierZoom()).toDouble(), 4.0);

    // An export into a folder, which cannot be written
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return QDir(textPath).filePath(QStringLiteral("x/y")); });
    section->exportSettings();
    fixture.settle();
    QVERIFY(ShortcutsTestHelper::storageDialog());
    QCOMPARE(ShortcutsTestHelper::storageDialog()->getTitleText(), QStringLiteral("The settings could not be exported"));
    ShortcutsTestHelper::storageDialog()->reject();
    fixture.settle();
    QVERIFY(!section->getExportInfoBar());

    // Another instance holds the write lock: the import is refused, nothing changes
    const QString validPath = QDir(backups.path()).filePath(QStringLiteral("valid.pdf4qt-backup"));
    QVERIFY(!fixture.context->getDatabase()->exportBackup(validPath).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 6.0).isError());
    pdf::PDFDatabaseError lockError;
    sqlite3* lock = pdf::PDFDatabase::openFileConnection(fixture.context->getDatabase()->getState().filePath, false, 100, &lockError);
    QVERIFY(lock);
    QVERIFY(!pdf::PDFDatabase::executeOnHandle(lock, "BEGIN IMMEDIATE").isError());
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return validPath; });
    section->importSettings();
    fixture.settle();
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getMessageText(), QStringLiteral("Another PDF4QT is changing the settings right now. Try again in a moment. Nothing was changed."));
    dialog->reject();
    pdf::PDFDatabase::executeOnHandle(lock, "ROLLBACK");
    pdf::PDFDatabase::closeHandle(lock);
    fixture.settle();
    QCOMPARE(fixture.settings().getValue(pdf::PDFSettingKeys::magnifierZoom()).toDouble(), 6.0);
}

void AppSettingsShortcutsTest::storagePortableAndMemory()
{
    // Portable (1270:14909): no file, Show in Folder disabled in its place, the state
    // Portable, the texts of the session, no other database files; export and import work
    QTemporaryDir backups;
    const QString backupPath = QDir(backups.path()).filePath(QStringLiteral("portable.pdf4qt-backup"));
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("storage"));
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(page);
    QVERIFY(section);
    QVERIFY(section->getLocationPath().isEmpty());
    QCOMPARE(section->getLocationRow()->getDescription(), QString::fromUtf8("In memory only – no file is written to this computer."));
    QVERIFY(!section->getShowInFolderButton()->isEnabled());
    QVERIFY(section->getShowInFolderButton()->isVisible());
    QCOMPARE(section->getStateValue()->getText(), QString::fromUtf8("Portable – nothing is written"));
    QCOMPARE(section->getStateValue()->getIconName(), QStringLiteral("portable"));
    QVERIFY(section->getExportRow()->getDescription().contains(QStringLiteral("of this session")));
    QVERIFY(section->getImportRow()->getDescription().contains(QStringLiteral("Nothing is written to this computer.")));
    QVERIFY(section->getFilesGroup()->isHidden());
    QCOMPARE(page->getFooterNote()->text(), QStringLiteral("Changes apply at once and are kept until PDF4QT closes."));

    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    section->exportSettings();
    fixture.settle();
    QVERIFY(section->getExportInfoBar());
    QVERIFY(pdf::PDFDatabaseBackup::read(backupPath).isValid());

    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 5.0).isError());
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    section->importSettings();
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getMessageText().contains(QStringLiteral("of this session")));
    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getValue(pdf::PDFSettingKeys::magnifierZoom()).toDouble(), 3.0);
    pdf::PDFAppFileDialogs::setSaveBackupHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppFileDialogs::setOpenBackupHandler(pdf::PDFAppFileDialogs::FolderHandler());

    // The memory fallback (the file cannot be opened): the state of the banner, the
    // file in Location, no second Try Again
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(pdf::PDFDatabaseFiles::getFilePath(directory.path(), pdf::PDFDatabaseSchema::getCurrentVersion())));
    StorageFixture fallback(directory.path());
    QCOMPARE(fallback.context->getDatabase()->getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    section = ShortcutsTestHelper::storage(fallback.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    QCOMPARE(section->getStateValue()->getText(), QString::fromUtf8("In memory – kept until PDF4QT closes"));
    QCOMPARE(section->getStateValue()->getIconName(), QStringLiteral("alert-triangle"));
    QVERIFY(section->getFilesGroup()->isVisible());
    QVERIFY(!section->findChild<QPushButton*>(QStringLiteral("bannerTryAgain"), Qt::FindChildrenRecursively));
}

void AppSettingsShortcutsTest::storageReadOnlyNewer()
{
    // Only a newer file (copied into memory): Read only, Import disabled, the newer
    // open file is not an "other" file
    QTemporaryDir directory;
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 2), 2, "dark"));
    StorageFixture fixture(directory.path());
    QCOMPARE(fixture.context->getDatabase()->getState().mode, pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema);
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    QCOMPARE(section->getStateValue()->getText(), QString::fromUtf8("Read only – newer version"));
    QCOMPARE(section->getStateValue()->getIconColor(), pdf::PDFThemeColors::AccentBlueIcon);
    QVERIFY(!section->getImportButton()->isEnabled());
    QVERIFY(section->getExportButton()->isEnabled());
    QVERIFY(section->getFileRows().isEmpty());
    QVERIFY(!section->getEmptyFilesCard()->isHidden());
}

void AppSettingsShortcutsTest::shortcutResetConflicts()
{
    // Review RC5-D: the default keys of a command (with its alternate keys) are
    // checked against the keys of the other commands before they apply again -
    // Reset to Default and the default recorded again never give one key to two
    // commands (Qt runs neither of them then)
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(section);
    pdf::PDFAppShortcutsModel* model = section->getModel();
    const pdf::PDFAppCommandRegistry* registry = fixture.context->getCommandRegistry();
    const QString properties = pdf::PDFAppCommands::annotationProperties();
    const QString save = pdf::PDFAppCommands::save();
    const QKeySequence f2(Qt::Key_F2);
    const QKeySequence altReturn(Qt::ALT | Qt::Key_Return);
    const QList<QKeySequence> defaultKeys = { f2, altReturn };
    QCOMPARE(model->getDefaultKeys(properties), defaultKeys);
    QVERIFY(registry->findConflicts(model->getUserShortcuts()).empty());

    // Alt+Return given to Save: Properties keeps F2 without its alternate key, which
    // is a change (Reset to Default is offered, Restore defaults lists it)
    const QStringList conflicts = model->getConflicts(save, altReturn);
    QVERIFY(conflicts.contains(properties));
    QVERIFY(!model->assign(save, altReturn, conflicts).isError());
    QCOMPARE(model->getKeys(properties), QList<QKeySequence>({ f2 }));
    QVERIFY(model->isChanged(properties));
    QVERIFY(model->getChangedCommands().contains(properties));
    const QModelIndex propertiesIndex = model->index(model->findRow(properties));
    QVERIFY(propertiesIndex.data(pdf::PDFAppShortcutsModel::IsChangedRole).toBool());
    QCOMPARE(pdf::PDFAppShortcutsModel::getShortcutsText(model->getDefaultKeys(properties)), f2.toString(QKeySequence::NativeText) + QStringLiteral(", ") + altReturn.toString(QKeySequence::NativeText));

    // Reset to Default of Properties: Alt+Return is the key of Save now - the
    // dialog asks first, nothing is written
    QSignalSpy conflictSpy(model, &pdf::PDFAppShortcutsModel::conflictFound);
    QVERIFY(!model->setData(propertiesIndex, QVariant(), pdf::PDFAppShortcutsModel::ResetRole));
    QTRY_COMPARE(conflictSpy.count(), 1);
    QCOMPARE(conflictSpy.front().at(1).value<QKeySequence>(), f2);
    QVERIFY(conflictSpy.front().at(2).toStringList().contains(save));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), properties), std::optional<QString>(QStringLiteral("F2")));
    QVERIFY(registry->findConflicts(model->getUserShortcuts()).empty());

    // Assign: the own row of Properties is removed (both keys again), Save loses Alt+Return
    QTRY_VERIFY(section->getConflictDialog());
    QTest::mouseClick(section->getConflictDialog()->getAssignButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), properties));
    QCOMPARE(model->getKeys(properties), defaultKeys);
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), save), std::optional<QString>(QString()));
    QVERIFY(registry->findConflicts(model->getUserShortcuts()).empty());

    // The default recorded over a stored default shortcut (the alternate keys off)
    // brings the alternate keys back; Alt+Return of another command is asked for
    QVERIFY(!fixture.context->getShortcuts().setShortcut(properties, QStringLiteral("F2")).isError());
    model->refresh();
    QCOMPARE(model->getKeys(properties), QList<QKeySequence>({ f2 }));
    QVERIFY(model->setData(propertiesIndex, QVariant::fromValue(f2), pdf::PDFComponentRoles::ShortcutRole));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), properties));
    QCOMPARE(model->getKeys(properties), defaultKeys);

    QVERIFY(!fixture.context->getShortcuts().setShortcuts({ { properties, QStringLiteral("F2") }, { save, QStringLiteral("Alt+Return") } }).isError());
    model->refresh();
    QVERIFY(!model->setData(propertiesIndex, QVariant::fromValue(f2), pdf::PDFComponentRoles::ShortcutRole));
    QTRY_COMPARE(conflictSpy.count(), 2);
    QVERIFY(conflictSpy.back().at(2).toStringList().contains(save));
    QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), properties), std::optional<QString>(QStringLiteral("F2")));
    QTRY_VERIFY(section->getConflictDialog());
    section->getConflictDialog()->reject();
    fixture.settle();

    // A reset without a conflict writes at once
    QVERIFY(!fixture.context->getShortcuts().resetShortcut(save).isError());
    model->refresh();
    QVERIFY(model->setData(propertiesIndex, QVariant(), pdf::PDFAppShortcutsModel::ResetRole));
    QVERIFY(!ShortcutsTestHelper::stored(fixture.context.get(), properties));
    QCOMPARE(conflictSpy.count(), 2);

    // The rows of a reset and of new keys in one transaction; an id in both is refused
    QVERIFY(!fixture.context->getShortcuts().setShortcuts({ { save, QStringLiteral("Ctrl+F9") } }, { properties }).isError());
    QVERIFY(fixture.context->getShortcuts().setShortcuts({ { save, QStringLiteral("Ctrl+F9") } }, { save }).isError());
    QVERIFY(fixture.context->getShortcuts().setShortcuts({}, { QString() }).isError());

    // An error of assign is reported once (writeFailed, not again by the section)
    QSignalSpy failedSpy(model, &pdf::PDFAppShortcutsModel::writeFailed);
    QSignalSpy statusSpy(section, &pdf::PDFAppSettingsSection::statusMessageRequested);
    QVERIFY(model->assign(QStringLiteral("no.command"), QKeySequence(Qt::Key_F7), {}).isError());
    fixture.settle();
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(statusSpy.count(), 1);
}

void AppSettingsShortcutsTest::storageExportKeepsExistingFile()
{
    // Review RC5-D: the suffix added after the file dialog never replaces a file,
    // for which the dialog has not asked: the backup gets a free name
    QTemporaryDir backups;
    const QString base = QDir(backups.path()).filePath(QStringLiteral("settings"));
    const QString existingPath = base + QStringLiteral(".pdf4qt-backup");
    {
        QFile file(existingPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("an older backup of the user");
    }

    StorageFixture fixture;
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return base; });
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    section->exportSettings();
    fixture.settle();

    QFile existing(existingPath);
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("an older backup of the user"));
    existing.close();
    const QString exportedPath = base + QStringLiteral(" (2).pdf4qt-backup");
    QVERIFY(pdf::PDFDatabaseBackup::read(exportedPath).isValid());
    QVERIFY(section->getExportInfoBar());
    QCOMPARE(section->getExportInfoBar()->getRequest().title, QString::fromUtf8("Settings exported to “settings (2).pdf4qt-backup”"));

    // The name with the suffix chosen in the dialog (which has asked) is replaced
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return existingPath; });
    section->exportSettings();
    fixture.settle();
    QVERIFY(pdf::PDFDatabaseBackup::read(existingPath).isValid());
    QVERIFY(!QFileInfo::exists(base + QStringLiteral(" (3).pdf4qt-backup")));
}

void AppSettingsShortcutsTest::storageFilterOfRebuiltRows()
{
    // Review RC5-D: the rows of the files built again (the section shown, a change of
    // the state) keep the search of the page
    QTemporaryDir directory;
    const QString folder = directory.path();
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(folder, 1), 1, "dark"));
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(folder, 3), 3, "light"));
    StorageFixture fixture(folder, 1);
    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    QCOMPARE(section->getFileRows().size(), 2);

    QVERIFY(section->applyFilter(pdf::PDFSearchQuery(QStringLiteral("location"))) >= 1);
    QVERIFY(!section->getLocationRow()->isHidden());
    for (pdf::PDFAppSettingRow* row : section->getFileRows())
    {
        QVERIFY(row->isHidden());
    }

    section->updateFiles();
    QCOMPARE(section->getFileRows().size(), 2);
    for (pdf::PDFAppSettingRow* row : section->getFileRows())
    {
        QVERIFY(row->isHidden());
    }
    QVERIFY(section->getFilesGroup()->isHidden());

    // A query of the files shows them, no query shows everything
    QVERIFY(section->applyFilter(pdf::PDFSearchQuery(QStringLiteral("older"))) >= 1);
    section->updateFiles();
    QVERIFY(!section->getFilesGroup()->isHidden());
    section->applyFilter(pdf::PDFSearchQuery());
    section->updateFiles();
    for (pdf::PDFAppSettingRow* row : section->getFileRows())
    {
        QVERIFY(!row->isHidden());
    }
}

void AppSettingsShortcutsTest::storageImportSecurity()
{
    // Owner 2026-10-25 (Q7): the import confirmation lists the security settings the
    // backup weakens ("old → new") and offers "Also trust the N certificates of the
    // backup" (off by default: the current trusted certificates stay)
    const pdf::PDFCertificateEntries aatl = pdf::PDFCertificateStore::getAATLCertificates();
    QVERIFY(aatl.size() >= 5);
    std::vector<QByteArray> certificates;
    for (size_t i = 0; i < 5; ++i)
    {
        certificates.push_back(aatl[i].info.getCertificateData());
    }

    QTemporaryDir backups;
    const QString weakPath = QDir(backups.path()).filePath(QStringLiteral("weak.pdf4qt-backup"));
    const QString equalPath = QDir(backups.path()).filePath(QStringLiteral("equal.pdf4qt-backup"));
    StorageFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    pdf::PDFTrustedCertificateRepository trusted(database);
    pdf::PDFDatabaseConnection* connection = database->getConnection();
    using Keys = pdf::PDFSettingKeys;

    // The backup: weak settings, five certificates and hostile rows (a text, garbage,
    // a huge blob), which are no certificates
    QVERIFY(!fixture.settings().setValues({ { Keys::securityLaunchFiles(), true }, { Keys::signaturesVerify(), false },
                                            { Keys::signaturesIgnoreExpiration(), true } }).isError());
    for (const QByteArray& data : certificates)
    {
        QVERIFY(!trusted.addCertificate(data).isError());
    }
    pdf::PDFDatabaseStatement hostile(connection, "INSERT INTO trusted_certificate(data, added) VALUES ('text', 0), (CAST('not a certificate' AS BLOB), 0), (zeroblob(70000), 0)");
    QVERIFY(hostile.execute());
    QVERIFY(!database->exportBackup(weakPath).isError());

    // The current state: stronger settings (links and the system store off), one of
    // the certificates; a backup of this state weakens nothing and adds no trust
    QVERIFY(!fixture.settings().setValues({ { Keys::securityLaunchFiles(), false }, { Keys::signaturesVerify(), true },
                                            { Keys::signaturesIgnoreExpiration(), false }, { Keys::securityOpenLinks(), false },
                                            { Keys::signaturesUseSystemStore(), false } }).isError());
    pdf::PDFDatabaseStatement clear(connection, "DELETE FROM trusted_certificate");
    QVERIFY(clear.execute());
    QVERIFY(!trusted.addCertificate(certificates[0]).isError());
    QVERIFY(!database->exportBackup(equalPath).isError());

    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    QString chosen = equalPath;
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return chosen; });

    // The backup of the current state: nothing listed, no check box
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->getCheckBox());
    QVERIFY(!dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("weakenedSettings")));
    QVERIFY(!dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("newCertificates")));
    // The same certificates: the text says nothing about them
    QCOMPARE(dialog->getMessageText(), QStringLiteral("Your settings, shortcuts, Recent files and folders, workspaces, bookmarks, pinned tools, tool history and annotation styles are replaced by the backup. "
                                                      "Other open PDF4QT windows use the imported settings at once. This cannot be undone."));
    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(int(trusted.getCertificates().size()), 1);

    // The weak backup
    chosen = weakPath;
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    const pdf::PDFKeyValueList* weakened = dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("weakenedSettings"));
    QVERIFY(weakened);
    const QStringList expectedKeys = { QStringLiteral("Allow URI link launch"), QStringLiteral("Allow application launch"), QStringLiteral("Signature verification"),
                                       QStringLiteral("Ignore expired certificates"), QStringLiteral("Use system certificate store") };
    const QStringList expectedValues = { QString::fromUtf8("Off → On"), QString::fromUtf8("Off → On"), QString::fromUtf8("On → Off"),
                                         QString::fromUtf8("Off → On"), QString::fromUtf8("Off → On") };
    QCOMPARE(weakened->getRowCount(), expectedKeys.size());
    for (int i = 0; i < weakened->getRowCount(); ++i)
    {
        QCOMPARE(weakened->getKey(i), expectedKeys[i]);
        QCOMPARE(weakened->getValue(i), expectedValues[i]);
    }

    // Four new certificates (the hostile rows are none): three listed with their
    // names and SHA-256, the rest counted; the check box is off
    QCheckBox* checkBox = dialog->getCheckBox();
    QVERIFY(checkBox);
    QCOMPARE(checkBox->text(), QStringLiteral("Also trust the 4 certificates of the backup"));
    QVERIFY(!checkBox->isChecked());
    // The text does not say the certificates are replaced, the note below the check
    // box says they are kept (coordinator 2026-10-25)
    QVERIFY(!dialog->getMessageText().contains(QStringLiteral("certificates")));
    QVERIFY(dialog->getCheckBoxNote());
    QCOMPARE(dialog->getCheckBoxNote()->text(), QStringLiteral("Your current trusted certificates are kept unless you check this."));
    QVERIFY(dialog->getCheckBoxNote()->mapTo(dialog, QPoint(0, 0)).y() > checkBox->mapTo(dialog, QPoint(0, 0)).y());
    const pdf::PDFKeyValueList* newCertificates = dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("newCertificates"));
    QVERIFY(newCertificates);
    QCOMPARE(newCertificates->getRowCount(), 2 * pdf::PDFAppStorageSettingsSection::MAXIMUM_LISTED_CERTIFICATES + 1);
    QCOMPARE(newCertificates->getKey(0), QStringLiteral("Certificate"));
    QCOMPARE(newCertificates->getValue(0), pdf::PDFAppCertificateEntry::create(pdf::PDFAppCertificateEntry::Type::User, 0, certificates[1]).name);
    QCOMPARE(newCertificates->getKey(1), QStringLiteral("SHA-256"));
    QCOMPARE(newCertificates->getValue(1), pdf::PDFAppCertificateEntry::getFingerprint(certificates[1]));
    QCOMPARE(newCertificates->getValue(5), pdf::PDFAppCertificateEntry::getFingerprint(certificates[3]));
    QCOMPARE(newCertificates->getValue(6), QStringLiteral("1 more certificate"));
    ShortcutsTestHelper::saveShot(QStringLiteral("settings-storage-import-security"), dialog);

    // Unchecked: the settings of the backup, the current certificates
    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.settings().getBool(Keys::securityLaunchFiles()));
    QVERIFY(!fixture.settings().getBool(Keys::signaturesVerify()));
    QCOMPARE(int(trusted.getCertificates().size()), 1);
    QCOMPARE(trusted.getCertificates().front().data, certificates[0]);

    // Checked: the certificates of the backup (back to the current state first)
    chosen = equalPath;
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!fixture.settings().getBool(Keys::securityLaunchFiles()));
    chosen = weakPath;
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog && dialog->getCheckBox());
    QTest::mouseClick(dialog->getCheckBox(), Qt::LeftButton);
    QVERIFY(dialog->getCheckBox()->isChecked());
    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(int(trusted.getCertificates().size()), 5);
    QCOMPARE(int(trusted.getRows().size()), 8);

    // Nothing weakened, nothing new: the backup imported over itself
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->getCheckBox());
    QVERIFY(!dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("weakenedSettings")));
    QVERIFY(!dialog->getMessageText().contains(QStringLiteral("certificates")));
    dialog->reject();
    fixture.settle();

    // A backup without new certificates, which lacks trusted ones: no check box, the
    // text says they are replaced (only trust is removed)
    chosen = equalPath;
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->getCheckBox());
    QVERIFY(!dialog->getCheckBoxNote());
    QVERIFY(dialog->getMessageText().endsWith(QStringLiteral("Your trusted certificates are replaced by those of the backup, which adds no new ones. This cannot be undone.")));
    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(int(trusted.getCertificates().size()), 1);
}

void AppSettingsShortcutsTest::importWeakensMcp()
{
    // Owner 2026-10-26 (Q4 of step 7): the import confirmation lists also the AI
    // settings the backup weakens - a permission Deny → Ask → Allow, remote
    // connections off → on, the access token on → off - as "old → new" rows with the
    // titles of the definitions; a stronger or equal backup lists nothing, hostile
    // values of the backup are the defaults of the definitions
    using Keys = pdf::PDFSettingKeys;
    using Values = pdf::PDFSettingValues;
    using Section = pdf::PDFAppStorageSettingsSection;

    QTemporaryDir backups;
    StorageFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    pdf::PDFDatabaseConnection* connection = database->getConnection();

    const auto setMcp = [&](const QString& read, const QString& edit, const QString& files, bool allowRemote, bool requireToken)
    {
        return !fixture.settings().setValues({ { Keys::mcpPermissionRead(), read }, { Keys::mcpPermissionEdit(), edit },
                                               { Keys::mcpPermissionFiles(), files }, { Keys::mcpAllowRemote(), allowRemote },
                                               { Keys::mcpRequireToken(), requireToken } }).isError();
    };
    const auto exportBackup = [&](const QString& name)
    {
        const QString path = QDir(backups.path()).filePath(name + QStringLiteral(".pdf4qt-backup"));
        return database->exportBackup(path).isError() ? QString() : path;
    };
    const auto weakened = [&](const QString& path)
    {
        QStringList rows;
        const pdf::PDFDatabaseBackup backup = pdf::PDFDatabaseBackup::read(path);
        for (const pdf::PDFAppStorageDialog::Fact& fact : Section::getWeakenedSettings(fixture.context.get(), backup))
        {
            rows << fact.key + QStringLiteral(": ") + fact.value;
        }
        return rows;
    };
    const QString deny = Values::permissionDeny();
    const QString ask = Values::permissionAsk();
    const QString allow = Values::permissionAllow();

    QVERIFY(setMcp(deny, deny, deny, false, true));
    const QString strongPath = exportBackup(QStringLiteral("strong"));
    QVERIFY(setMcp(allow, ask, allow, true, false));
    const QString weakPath = exportBackup(QStringLiteral("weak"));
    QVERIFY(setMcp(ask, ask, ask, false, true));
    const QString askPath = exportBackup(QStringLiteral("ask"));

    // Hostile values of the backup: an option of another case, a number, a long
    // text, a blob, a text of a bool, a float - each one the default of its definition
    QVERIFY(setMcp(deny, deny, deny, false, true));
    pdf::PDFDatabaseStatement hostile(connection,
        "INSERT OR REPLACE INTO setting(key, value, modified) VALUES "
        "('mcp.permissionRead', 'ALLOW', 0), ('mcp.permissionEdit', 42, 0), ('mcp.permissionFiles', zeroblob(5000), 0), "
        "('mcp.allowRemote', 'garbage', 0), ('mcp.requireToken', 0.5, 0)");
    QVERIFY(hostile.execute());
    const QString hostilePath = exportBackup(QStringLiteral("hostile"));
    pdf::PDFDatabaseStatement hostileLong(connection, "UPDATE setting SET value = printf('%.3000c', 'a') WHERE key = 'mcp.permissionRead'");
    QVERIFY(hostileLong.execute());
    const QString longPath = exportBackup(QStringLiteral("long"));
    for (const QString& path : { strongPath, weakPath, askPath, hostilePath, longPath })
    {
        QVERIFY(!path.isEmpty());
    }

    // The current state: everything strong
    QVERIFY(setMcp(deny, deny, deny, false, true));
    const QString arrow = QString::fromUtf8(" → ");
    QCOMPARE(weakened(weakPath), QStringList({ QStringLiteral("Read documents: Deny") + arrow + QStringLiteral("Allow"),
                                               QStringLiteral("Edit documents: Deny") + arrow + QStringLiteral("Ask"),
                                               QStringLiteral("Open and save files: Deny") + arrow + QStringLiteral("Allow"),
                                               QStringLiteral("Allow remote connections: Off") + arrow + QStringLiteral("On"),
                                               QStringLiteral("Require access token: On") + arrow + QStringLiteral("Off") }));
    QCOMPARE(weakened(askPath), QStringList({ QStringLiteral("Read documents: Deny") + arrow + QStringLiteral("Ask"),
                                              QStringLiteral("Edit documents: Deny") + arrow + QStringLiteral("Ask"),
                                              QStringLiteral("Open and save files: Deny") + arrow + QStringLiteral("Ask") }));
    QVERIFY(weakened(strongPath).isEmpty());

    // Hostile values: the defaults (Read = Allow, Edit = Files = Ask, remote off,
    // token on), bounded texts of the options
    const QStringList hostileRows = { QStringLiteral("Read documents: Deny") + arrow + QStringLiteral("Allow"),
                                      QStringLiteral("Edit documents: Deny") + arrow + QStringLiteral("Ask"),
                                      QStringLiteral("Open and save files: Deny") + arrow + QStringLiteral("Ask") };
    QCOMPARE(weakened(hostilePath), hostileRows);
    QCOMPARE(weakened(longPath), hostileRows);

    // Ask everywhere: only the steps to Allow and the switches are weaker
    QVERIFY(setMcp(ask, ask, ask, false, true));
    QCOMPARE(weakened(weakPath), QStringList({ QStringLiteral("Read documents: Ask") + arrow + QStringLiteral("Allow"),
                                               QStringLiteral("Open and save files: Ask") + arrow + QStringLiteral("Allow"),
                                               QStringLiteral("Allow remote connections: Off") + arrow + QStringLiteral("On"),
                                               QStringLiteral("Require access token: On") + arrow + QStringLiteral("Off") }));
    QVERIFY(weakened(askPath).isEmpty());
    QVERIFY(weakened(strongPath).isEmpty());

    // The weakest state: no backup weakens it
    QVERIFY(setMcp(allow, allow, allow, true, false));
    for (const QString& path : { strongPath, weakPath, askPath, hostilePath, longPath })
    {
        QVERIFY2(weakened(path).isEmpty(), qPrintable(path));
    }

    // Hostile current values are the defaults too (Read = Allow)
    pdf::PDFDatabaseStatement hostileCurrent(connection,
        "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('mcp.permissionRead', 'nonsense', 0), ('mcp.permissionEdit', x'00', 0), "
        "('mcp.permissionFiles', 'deny', 0), ('mcp.allowRemote', 0, 0), ('mcp.requireToken', 1, 0)");
    QVERIFY(hostileCurrent.execute());
    QCOMPARE(weakened(weakPath), QStringList({ QStringLiteral("Open and save files: Deny") + arrow + QStringLiteral("Allow"),
                                               QStringLiteral("Allow remote connections: Off") + arrow + QStringLiteral("On"),
                                               QStringLiteral("Require access token: On") + arrow + QStringLiteral("Off") }));

    // The confirmation of the import lists the rows
    QVERIFY(setMcp(deny, deny, deny, false, true));
    QString chosen = weakPath;
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return chosen; });
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    const pdf::PDFKeyValueList* list = dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("weakenedSettings"));
    QVERIFY(list);
    QCOMPARE(list->getRowCount(), 5);
    QCOMPARE(list->getKey(0), QStringLiteral("Read documents"));
    QCOMPARE(list->getValue(0), QStringLiteral("Deny") + arrow + QStringLiteral("Allow"));
    QCOMPARE(list->getKey(4), QStringLiteral("Require access token"));
    QCOMPARE(list->getValue(4), QStringLiteral("On") + arrow + QStringLiteral("Off"));
    ShortcutsTestHelper::saveShot(QStringLiteral("settings-storage-import-ai"), dialog);
    dialog->reject();
    fixture.settle();

    // A backup equal to the current state: no rows
    chosen = strongPath;
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->findChild<pdf::PDFKeyValueList*>(QStringLiteral("weakenedSettings")));
    dialog->reject();
    fixture.settle();
    pdf::PDFAppFileDialogs::setOpenBackupHandler(pdf::PDFAppFileDialogs::FolderHandler());
}

void AppSettingsShortcutsTest::reviewImportTrustCheckedAtAccept()
{
    // RFX5: the backup has a certificate, which is trusted, when the dialog opens
    // (no check box); another instance removes it meanwhile. The import adds no
    // trust without the check box: the certificate stays untrusted, the settings of
    // the backup are imported.
    const pdf::PDFCertificateEntries aatl = pdf::PDFCertificateStore::getAATLCertificates();
    QVERIFY(aatl.size() >= 1);
    const QByteArray certificate = aatl[0].info.getCertificateData();

    QTemporaryDir backups;
    const QString backupPath = QDir(backups.path()).filePath(QStringLiteral("trusted.pdf4qt-backup"));
    StorageFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    pdf::PDFTrustedCertificateRepository trusted(database);
    QVERIFY(!trusted.addCertificate(certificate).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierSize(), 222).isError());
    QVERIFY(!database->exportBackup(backupPath).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierSize(), 111).isError());

    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
    QVERIFY(dialog);
    QVERIFY(!dialog->getCheckBox());

    // Removed by another instance while the dialog is open
    pdf::PDFDatabaseStatement remove(database->getConnection(), "DELETE FROM trusted_certificate");
    QVERIFY(remove.execute());
    QVERIFY(trusted.getCertificates().empty());

    QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(222));
    QVERIFY(trusted.getCertificates().empty());
}

void AppSettingsShortcutsTest::storageBackupFolder()
{
    // Owner 2026-10-25: the file dialogs of Export / Import start in the folder of the
    // last backup (a state setting, no row); a missing, a device or a network folder
    // falls back to Documents
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    documents = documents.isEmpty() ? QDir::homePath() : documents;
    QTemporaryDir first;
    QTemporaryDir second;
    StorageFixture fixture;
    const pdf::PDFSettingDefinition* definition = fixture.context->getSettingRegistry()->findDefinition(pdf::PDFSettingKeys::storageBackupFolder());
    QVERIFY(definition);
    QVERIFY(!definition->isVisible());
    QCOMPARE(pdf::PDFAppStorageSettingsSection::getBackupFolder(nullptr), documents);
    QCOMPARE(pdf::PDFAppStorageSettingsSection::getBackupFolder(fixture.context.get()), documents);

    pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(section);
    QString offered;
    QString chosen = QDir(first.path()).filePath(QStringLiteral("a.pdf4qt-backup"));
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString& filePath) { offered = filePath; return chosen; });
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString& folder) { offered = folder; return chosen; });

    // The first export starts in Documents and remembers its folder
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->exportSettings();
    fixture.settle();
    QCOMPARE(QDir::cleanPath(QFileInfo(offered).absolutePath()), QDir::cleanPath(documents));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::storageBackupFolder()), QDir::cleanPath(first.path()));

    // The next dialogs start there; a canceled one changes nothing
    chosen.clear();
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    QCOMPARE(offered, QDir::cleanPath(first.path()));
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->exportSettings();
    QCOMPARE(QDir::cleanPath(QFileInfo(offered).absolutePath()), QDir::cleanPath(first.path()));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::storageBackupFolder()), QDir::cleanPath(first.path()));

    // An import of a backup made elsewhere (its own stored folder is the first one)
    // remembers the folder of the imported file
    const QString copied = QDir(second.path()).filePath(QStringLiteral("copied.pdf4qt-backup"));
    QVERIFY(QFile::copy(QDir(first.path()).filePath(QStringLiteral("a.pdf4qt-backup")), copied));
    chosen = copied;
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    fixture.settle();
    QVERIFY(ShortcutsTestHelper::storageDialog());
    QTest::mouseClick(ShortcutsTestHelper::storageDialog()->getActionButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::storageBackupFolder()), QDir::cleanPath(second.path()));
    QCOMPARE(pdf::PDFAppStorageSettingsSection::getBackupFolder(fixture.context.get()), QDir::cleanPath(second.path()));

    // Stored values, which are never used (and never touched): network and device
    // paths, a relative path, a missing folder, a file
    const QStringList refused = { QStringLiteral("//server/share/backups"), QStringLiteral("\\\\server\\share\\backups"), QStringLiteral("smb://server/share"),
                                  QStringLiteral("\\\\.\\C:"), QStringLiteral("\\\\?\\C:\\backups"), QStringLiteral("relative/folder"),
                                  QDir(second.path()).filePath(QStringLiteral("missing")), copied };
    for (const QString& value : refused)
    {
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::storageBackupFolder(), value).isError());
        QCOMPARE(pdf::PDFAppStorageSettingsSection::getBackupFolder(fixture.context.get()), documents);
    }
    chosen.clear();
    ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")))->importSettings();
    QCOMPARE(offered, documents);
}

void AppSettingsShortcutsTest::searchAndAnchors()
{
    pdftest::AppSettingsFixture fixture;

    // The filter of the page finds the rows of Storage by their texts and keywords
    pdf::PDFAppSettingsPage* page = fixture.openFilter(QStringLiteral("backup"));
    QVERIFY(page);
    QVERIFY(page->findRow(QStringLiteral("export")));
    QVERIFY(!page->findRow(QStringLiteral("export"))->isHidden());
    QVERIFY(!page->findRow(QStringLiteral("import"))->isHidden());

    // The section Shortcuts is found by its keywords, its table shown
    page = fixture.openFilter(QStringLiteral("keyboard"));
    pdf::PDFAppShortcutsSettingsSection* shortcuts = nullptr;
    for (pdf::PDFAppSettingsSection* section : page->getSections())
    {
        if (pdf::PDFAppShortcutsSettingsSection* candidate = qobject_cast<pdf::PDFAppShortcutsSettingsSection*>(section))
        {
            shortcuts = candidate;
        }
    }
    QVERIFY(shortcuts);
    QVERIFY(shortcuts->isVisible());
    QVERIFY(shortcuts->getTableCard()->isVisible());

    // A query of nothing of the package hides both
    page = fixture.openFilter(QStringLiteral("antialiasing"));
    for (pdf::PDFAppSettingsSection* section : page->getSections())
    {
        if (qobject_cast<pdf::PDFAppShortcutsSettingsSection*>(section) || qobject_cast<pdf::PDFAppStorageSettingsSection*>(section))
        {
            QVERIFY(!section->isVisible());
        }
    }

    // Anchors: a row of Storage is highlighted, the row of a command is current
    page = fixture.openSettings(QStringLiteral("storage"), QStringLiteral("import"));
    QVERIFY(page->findRow(QStringLiteral("import")));
    QTRY_VERIFY(page->findRow(QStringLiteral("import"))->isHighlighted());
    page = fixture.openSettings(QStringLiteral("shortcuts"), QStringLiteral("shortcut-") + pdf::PDFAppCommands::print());
    shortcuts = ShortcutsTestHelper::shortcuts(page);
    QVERIFY(shortcuts);
    QTRY_COMPARE(shortcuts->getView()->currentIndex().data(pdf::PDFAppShortcutsModel::CommandIdRole).toString(), pdf::PDFAppCommands::print());
    QVERIFY(!shortcuts->findAnchorWidget(QStringLiteral("shortcut-no.command")));
    QVERIFY(!shortcuts->findAnchorWidget(QStringLiteral("shortcut-")));
    page = fixture.openSettings(QStringLiteral("storage"), QStringLiteral("unknown-anchor"));
    QVERIFY(page);

    // The global search finds the rows of Storage (they are not definitions)
    const pdf::PDFAppSearchData data = pdf::PDFAppSearchData::load(fixture.context.get());
    pdf::PDFAppSettingSearchProvider provider;
    for (const auto& [text, anchor] : { std::make_pair(QStringLiteral("export settings"), QStringLiteral("export")),
                                       std::make_pair(QStringLiteral("import backup"), QStringLiteral("import")),
                                       std::make_pair(QStringLiteral("database location"), QStringLiteral("location")) })
    {
        std::vector<pdf::PDFAppSearchResult> results;
        provider.search(pdf::PDFSearchQuery(text), data, results);
        bool isFound = false;
        for (const pdf::PDFAppSearchResult& result : results)
        {
            isFound = isFound || (result.route.getFragment() == anchor && result.route.getPath() == QStringLiteral("/settings/storage"));
        }
        QVERIFY2(isFound, qPrintable(text));
    }
}

void AppSettingsShortcutsTest::deletionMatrix()
{
    // The window deleted with the conflict dialog open
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        section->openConflictDialog(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O), { pdf::PDFAppCommands::open() });
        QPointer<pdf::PDFAppShortcutConflictDialog> dialog = section->getConflictDialog();
        QVERIFY(dialog);
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!dialog);
        QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
    }

    // The page left with the conflict dialog open: the dialog goes, nothing is assigned
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        section->openConflictDialog(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O), { pdf::PDFAppCommands::open() });
        QPointer<pdf::PDFAppShortcutConflictDialog> dialog = section->getConflictDialog();
        fixture.navigate(QStringLiteral("/home"));
        QVERIFY(!dialog);
        QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
    }

    // A recording running when the section, the page or the window goes
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        const QModelIndex index = ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::save());
        section->getView()->setFocus();
        section->getView()->setCurrentIndex(index);
        QTest::keyClick(section->getView(), Qt::Key_F2);
        QVERIFY(section->getDelegate()->isRecording(index));
        delete section;
        fixture.settle();
        fixture.navigate(QStringLiteral("/settings/general"));
        QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
    }
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        section->getView()->setFocus();
        section->getView()->setCurrentIndex(ShortcutsTestHelper::viewIndex(section, pdf::PDFAppCommands::save()));
        QTest::keyClick(section->getView(), Qt::Key_F2);
        delete fixture.window.data();
        fixture.settle();
    }

    // The model, the view, the delegate and the search field deleted by the application
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        delete section->getDelegate();
        delete section->getSearchField();
        ShortcutWriterThread thread(fixture.context->getDatabase(), pdf::PDFAppCommands::print(), QStringLiteral("Ctrl+F9"));
        thread.start();
        QVERIFY(thread.wait(30000));
        fixture.settle();
        delete section->getView();
        delete section->getModel();
        section->refresh(QString(), QString());
        QVERIFY(section->getChangedKeys().isEmpty());
        QVERIFY(section->findAnchorWidget(QStringLiteral("shortcut-file.print")) == nullptr);
        fixture.navigate(QStringLiteral("/home"));
    }

    // Each dialog of Storage open while the window, the page or the section goes
    QTemporaryDir directory;
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1), 1, "dark"));
    for (int kind = 0; kind < 3; ++kind)
    {
        for (int deletion = 0; deletion < 3; ++deletion)
        {
            StorageFixture fixture(directory.path(), 1);
            const QString validPath = fixture.path(QStringLiteral("valid.pdf4qt-backup"));
            QVERIFY(!fixture.context->getDatabase()->exportBackup(validPath).isError());
            pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
            QVERIFY(section);
            pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return kind == 1 ? validPath : fixture.path(QStringLiteral("missing")); });
            if (kind == 0)
            {
                section->requestDelete(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1));
            }
            else
            {
                section->importSettings();
            }
            fixture.settle();
            QPointer<pdf::PDFAppStorageDialog> dialog = ShortcutsTestHelper::storageDialog();
            QVERIFY(dialog);

            switch (deletion)
            {
                case 0:
                    delete fixture.window.data();
                    break;
                case 1:
                    fixture.navigate(QStringLiteral("/home"));
                    break;
                default:
                    delete section;
                    break;
            }
            fixture.settle();
            QVERIFY2(!dialog, qPrintable(QStringLiteral("%1 %2").arg(kind).arg(deletion)));
            QVERIFY(QFileInfo::exists(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1)));
        }
    }

    // The window deleted with the export info bar shown; the context closed first
    {
        StorageFixture fixture;
        const QString backupPath = fixture.path(QStringLiteral("x.pdf4qt-backup"));
        pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        section->exportSettings();
        fixture.settle();
        QPointer<pdf::PDFAppInfoBar> bar = section->getExportInfoBar();
        QVERIFY(bar);
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!bar);
    }
    {
        StorageFixture fixture;
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        QVERIFY(section);
        fixture.context->close();
        fixture.settle();
        delete fixture.window.data();
        fixture.settle();
    }
}

void AppSettingsShortcutsTest::deleteFromOwnSignals()
{
    // The window deleted from the file dialog of an export / an import (a nested
    // event loop of the native dialog)
    for (int kind = 0; kind < 2; ++kind)
    {
        StorageFixture fixture;
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        QVERIFY(section);
        const QString backupPath = fixture.path(QStringLiteral("own.pdf4qt-backup"));
        pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { delete fixture.window.data(); return backupPath; });
        pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { delete fixture.window.data(); return backupPath; });
        if (kind == 0)
        {
            section->exportSettings();
        }
        else
        {
            section->importSettings();
        }
        fixture.settle();
        QVERIFY(!fixture.window);
        QVERIFY(!QFileInfo::exists(backupPath));
    }

    // The window deleted (later) from the finished dialog of a deletion and of an
    // import, from the action of the info bar, from the conflict of the model
    {
        QTemporaryDir directory;
        QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1), 1, "dark"));
        StorageFixture fixture(directory.path(), 1);
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        section->requestDelete(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1));
        fixture.settle();
        pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
        QObject::connect(dialog, &QDialog::finished, fixture.window.data(), [&fixture]() { fixture.window->deleteLater(); });
        QTest::mouseClick(dialog->getActionButton(), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
        QVERIFY(!QFileInfo::exists(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1)));
    }
    {
        StorageFixture fixture;
        const QString backupPath = fixture.path(QStringLiteral("bar.pdf4qt-backup"));
        pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        section->exportSettings();
        fixture.settle();
        pdf::PDFAppInfoBar* bar = section->getExportInfoBar();
        QObject::connect(bar, &pdf::PDFAppInfoBar::actionTriggered, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QTest::mouseClick(bar->getActionButtons().front(), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
    }
    {
        StorageFixture fixture;
        const QString backupPath = fixture.path(QStringLiteral("close.pdf4qt-backup"));
        pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        section->exportSettings();
        fixture.settle();
        pdf::PDFAppInfoBar* bar = section->getExportInfoBar();
        QObject::connect(bar, &pdf::PDFAppInfoBar::closeRequested, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QTest::mouseClick(bar->getCloseButton(), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
    }
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        QObject::connect(section->getModel(), &pdf::PDFAppShortcutsModel::conflictFound, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QVERIFY(ShortcutsTestHelper::record(section, pdf::PDFAppCommands::save(), Qt::Key_O, Qt::ControlModifier));
        fixture.settle();
        QVERIFY(!fixture.window);
        QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
    }
    {
        // The window deleted from the dialog of a conflict (later), Assign done first
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppShortcutsSettingsSection* section = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        section->openConflictDialog(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O), { pdf::PDFAppCommands::open() });
        pdf::PDFAppShortcutConflictDialog* dialog = section->getConflictDialog();
        QObject::connect(dialog, &QDialog::finished, fixture.window.data(), [&fixture]() { fixture.window->deleteLater(); });
        QTest::mouseClick(dialog->getAssignButton(), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(ShortcutsTestHelper::stored(fixture.context.get(), pdf::PDFAppCommands::save()), std::optional<QString>(QStringLiteral("Ctrl+O")));
    }
}

void AppSettingsShortcutsTest::themesAndScales()
{
    QTemporaryDir directory;
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1), 1, "dark"));
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 3), 3, "dark"));
    StorageFixture fixture(directory.path(), 1);
    const QString backupPath = fixture.path(QStringLiteral("theme.pdf4qt-backup"));
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (const QString& sectionId : { QStringLiteral("shortcuts"), QStringLiteral("storage") })
        {
            pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
            QVERIFY(page);
            if (pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(page))
            {
                storage->exportSettings();
                fixture.settle();
                QVERIFY(storage->getExportInfoBar());
            }
            for (qreal ratio : { 1.0, 1.5, 2.0 })
            {
                QVERIFY2(ShortcutsTestHelper::countLeakPixels(ShortcutsTestHelper::render(page, ratio)) == 0, qPrintable(QStringLiteral("%1 at %2").arg(sectionId).arg(ratio)));
            }
        }

        // The dialogs
        pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(fixture.settingsPage());
        storage->requestDelete(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1));
        fixture.settle();
        pdf::PDFAppStorageDialog* dialog = ShortcutsTestHelper::storageDialog();
        QVERIFY(dialog);
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QCOMPARE(ShortcutsTestHelper::countLeakPixels(ShortcutsTestHelper::render(dialog, ratio)), 0);
        }
        dialog->reject();
        fixture.settle();

        pdf::PDFAppShortcutsSettingsSection* shortcuts = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        shortcuts->openConflictDialog(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O), { pdf::PDFAppCommands::open() });
        fixture.settle();
        QCOMPARE(ShortcutsTestHelper::countLeakPixels(ShortcutsTestHelper::render(shortcuts->getConflictDialog(), 2.0)), 0);
        shortcuts->getConflictDialog()->reject();
        fixture.settle();
    }

    // The colors of the state: check-circle accent/green/icon
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QCOMPARE(storage->getStateValue()->getIconColor(), pdf::PDFThemeColors::AccentGreenIcon);
    const QImage image = ShortcutsTestHelper::render(storage->getStateValue(), 1.0);
    const QColor green = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::AccentGreenIcon);
    bool hasGreen = false;
    for (int y = 0; y < image.height() && !hasGreen; ++y)
    {
        for (int x = 0; x < pdf::PDFAppStorageStateValue::ICON_SIZE && !hasGreen; ++x)
        {
            const QColor color = image.pixelColor(x, y);
            hasGreen = qAbs(color.red() - green.red()) < 24 && qAbs(color.green() - green.green()) < 24 && qAbs(color.blue() - green.blue()) < 24;
        }
    }
    QVERIFY(hasGreen);
}

void AppSettingsShortcutsTest::rightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        StorageFixture fixture;
        const QString backupPath = fixture.path(QStringLiteral("rtl.pdf4qt-backup"));
        pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("storage"));
        pdf::PDFAppStorageSettingsSection* section = ShortcutsTestHelper::storage(page);
        QVERIFY(section);

        // The controls at the left of the texts, the info bar at the leading (right) edge
        pdf::PDFAppSettingRow* row = section->getExportRow();
        QVERIFY(row->getControlWrap()->x() < row->getTitleLabel()->mapTo(row, QPoint(0, 0)).x());
        section->exportSettings();
        fixture.settle();
        pdf::PDFAppInfoBar* bar = section->getExportInfoBar();
        QVERIFY(bar);
        const QRect card = bar->getCardRect().translated(bar->pos());
        const QWidget* backupCard = row->parentWidget();
        const QRect backupRect(backupCard->mapTo(section, QPoint(0, 0)), backupCard->size());
        QCOMPARE(card.right(), backupRect.right());
        QCOMPARE(ShortcutsTestHelper::countLeakPixels(ShortcutsTestHelper::render(page, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-storage-rtl"));

        page = fixture.openSettings(QStringLiteral("shortcuts"));
        pdf::PDFAppShortcutsSettingsSection* shortcuts = ShortcutsTestHelper::shortcuts(page);
        QVERIFY(shortcuts);
        QCOMPARE(ShortcutsTestHelper::countLeakPixels(ShortcutsTestHelper::render(page, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-shortcuts-rtl"));
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsShortcutsTest::minimalWindow()
{
    QTemporaryDir directory;
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1), 1, "dark"));
    StorageFixture fixture(directory.path(), 1, QSize(720, 480));
    const QString backupPath = fixture.path(QStringLiteral("min.pdf4qt-backup"));
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    for (const QString& sectionId : { QStringLiteral("shortcuts"), QStringLiteral("storage") })
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY(page);
        if (pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(page))
        {
            storage->exportSettings();
        }
        fixture.settle();
        QScrollArea* area = ShortcutsTestHelper::scrollArea(page);
        QVERIFY(area);
        QVERIFY2(area->horizontalScrollBar()->maximum() == 0 || !area->horizontalScrollBar()->isVisible(), qPrintable(sectionId));
        for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
        {
            const QRect rect(row->mapTo(page, QPoint(0, 0)), row->size());
            QVERIFY2(rect.right() <= page->width(), qPrintable(row->getAnchor()));
        }
        if (pdf::PDFAppShortcutsSettingsSection* shortcuts = ShortcutsTestHelper::shortcuts(page))
        {
            QVERIFY(QRect(shortcuts->getTableCard()->mapTo(page, QPoint(0, 0)), shortcuts->getTableCard()->size()).right() <= page->width());
        }
        if (pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(page))
        {
            pdf::PDFAppInfoBar* bar = storage->getExportInfoBar();
            QVERIFY(bar);
            QVERIFY(bar->getCardRect().translated(bar->pos()).right() < storage->width());

            // The path is elided in the middle to its line, the whole one in the tooltip
            {
                pdf::PDFAppParagraph* label = storage->getLocationRow()->getDescriptionLabel();
                QVERIFY(label->fontMetrics().horizontalAdvance(label->text()) <= label->contentsRect().width() + 1);
                QCOMPARE(label->toolTip(), QDir::toNativeSeparators(storage->getLocationPath()));
            }
        }
        fixture.screenshot(QStringLiteral("settings-%1-720").arg(sectionId));
    }
}

void AppSettingsShortcutsTest::keyboardAndAccessibility()
{
    StorageFixture fixture;
    pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
    QVERIFY(storage);
    for (QPushButton* button : { storage->getShowInFolderButton(), storage->getExportButton(), storage->getImportButton() })
    {
        QVERIFY(button->focusPolicy() & Qt::TabFocus);
        QVERIFY(!button->text().isEmpty());
    }
    QCOMPARE(storage->getStateValue()->accessibleName(), QStringLiteral("Saved on this computer"));

    // Space on Export… runs the export
    const QString backupPath = fixture.path(QStringLiteral("key.pdf4qt-backup"));
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    storage->getExportButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(storage->getExportButton(), Qt::Key_Space);
    fixture.settle();
    QVERIFY(QFileInfo::exists(backupPath));

    // Shortcuts: the search field and the list by Tab, F2 / Enter / Space start a recording
    pdf::PDFAppShortcutsSettingsSection* shortcuts = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
    QVERIFY(shortcuts);
    QCOMPARE(shortcuts->getSearchField()->accessibleName(), QStringLiteral("Search actions or shortcuts"));
    QVERIFY(shortcuts->getView()->focusPolicy() & Qt::TabFocus);
    const QModelIndex index = ShortcutsTestHelper::viewIndex(shortcuts, pdf::PDFAppCommands::print());
    QVERIFY(index.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("Ctrl+P")));
    shortcuts->getView()->setFocus(Qt::TabFocusReason);
    shortcuts->getView()->setCurrentIndex(index);
    QTest::keyClick(shortcuts->getView(), Qt::Key_Return);
    QVERIFY(shortcuts->getDelegate()->isRecording(index));
    QTest::keyClick(shortcuts->getView()->findChild<pdf::PDFShortcutRecorder*>(), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!shortcuts->getDelegate()->isRecording(index));
}

void AppSettingsShortcutsTest::languageChange()
{
    StorageFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("storage"));
    QVERIFY(page);
    QCOMPARE(page->findRow(QStringLiteral("export"))->getTitle(), QStringLiteral("Export settings"));

    // The sections are built again in the new language
    ShortcutsTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    QCOMPARE(page->findRow(QStringLiteral("export"))->getTitle(), QStringLiteral("Einstellungen exportieren"));
    QVERIFY(ShortcutsTestHelper::storage(page)->getStateValue());

    page = fixture.openSettings(QStringLiteral("shortcuts"));
    pdf::PDFAppShortcutsSettingsSection* shortcuts = ShortcutsTestHelper::shortcuts(page);
    QCOMPARE(shortcuts->getSearchField()->placeholderText(), QString::fromUtf8("Aktionen suchen…"));
    QVERIFY(shortcuts->getTableCard()->findChild<pdf::PDFLabel*>(QStringLiteral("actionColumn"))->text() == QStringLiteral("Aktion"));

    // A conflict dialog open across a language change goes with its section
    shortcuts->openConflictDialog(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O), { pdf::PDFAppCommands::open() });
    QPointer<pdf::PDFAppShortcutConflictDialog> dialog = shortcuts->getConflictDialog();
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    QVERIFY(!dialog);
    page = fixture.settingsPage();
    QCOMPARE(ShortcutsTestHelper::shortcuts(page)->getSearchField()->placeholderText(), QString::fromUtf8("Search actions or shortcuts…"));
    QVERIFY(fixture.context->getShortcuts().getShortcuts().empty());
}

void AppSettingsShortcutsTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the screens of the package in both themes
    if (qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    QTemporaryDir directory;
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1), 1, "dark"));
    QVERIFY(ShortcutsTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 3), 3, "dark"));
    StorageFixture fixture(directory.path(), 1);
    const QString backupPath = fixture.path(QStringLiteral("PDF4QT settings 2026-10-25"));
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString&) { return backupPath; });
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        fixture.openSettings(QStringLiteral("shortcuts"));
        fixture.screenshot(QStringLiteral("settings-shortcuts-") + suffix);
        pdf::PDFAppStorageSettingsSection* storage = ShortcutsTestHelper::storage(fixture.openSettings(QStringLiteral("storage")));
        fixture.screenshot(QStringLiteral("settings-storage-") + suffix);
        storage->exportSettings();
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-storage-exported-") + suffix);
        storage->requestDelete(pdf::PDFDatabaseFiles::getFilePath(directory.path(), 1));
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-storage-delete-") + suffix);
        ShortcutsTestHelper::saveShot(QStringLiteral("dialog-storage-delete-") + suffix, ShortcutsTestHelper::storageDialog());
        ShortcutsTestHelper::storageDialog()->reject();
        fixture.settle();
        pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return backupPath + QStringLiteral(".pdf4qt-backup"); });
        storage->importSettings();
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-storage-import-") + suffix);
        ShortcutsTestHelper::saveShot(QStringLiteral("dialog-storage-import-") + suffix, ShortcutsTestHelper::storageDialog());
        ShortcutsTestHelper::storageDialog()->reject();
        fixture.settle();
        pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString&) { return fixture.path(QStringLiteral("missing.pdf4qt-backup")); });
        storage->importSettings();
        fixture.settle();
        ShortcutsTestHelper::saveShot(QStringLiteral("dialog-storage-import-error-") + suffix, ShortcutsTestHelper::storageDialog());
        ShortcutsTestHelper::storageDialog()->reject();
        fixture.settle();
        pdf::PDFAppShortcutsSettingsSection* shortcuts = ShortcutsTestHelper::shortcuts(fixture.openSettings(QStringLiteral("shortcuts")));
        shortcuts->openConflictDialog(pdf::PDFAppCommands::save(), QKeySequence(Qt::CTRL | Qt::Key_O), { pdf::PDFAppCommands::open() });
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-shortcuts-conflict-") + suffix);
        ShortcutsTestHelper::saveShot(QStringLiteral("dialog-shortcuts-conflict-") + suffix, shortcuts->getConflictDialog());
        shortcuts->getConflictDialog()->reject();
        fixture.settle();
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    pdftest::AppSettingsFixture portable;
    portable.openSettings(QStringLiteral("storage"));
    portable.screenshot(QStringLiteral("settings-storage-portable"));
}

QTEST_MAIN(AppSettingsShortcutsTest)
#include "tst_appsettingsshortcutstest.moc"
