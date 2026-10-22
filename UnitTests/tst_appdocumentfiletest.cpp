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

// Package E of step 3 (file operations): Save (safe write, incremental update of a
// signed document with the dialog save-signed, read-only file and failed write with
// save-error), Save as, Recent after a save, the automatic refresh (reload keeping
// the view, own saves ignored, unsaved changes - refresh-unsaved, the switch of the
// document and the setting), printing (the own print dialog on every platform with
// the injected printers writing PDF files, the preview, no printer, the job in a
// worker with Stop, the error with Print the Rest…, Printer properties… by a test
// handler, the print permission, the narrow window) and the e-mail (injected
// launcher, Save first). The deletion matrix deletes the tab, the window and the
// dialogs during the asynchronous work. PDF4QT_APP_SCREENSHOTS=<folder> writes
// pictures of the dialogs (both themes; run with QT_SCALE_FACTOR=2 for 2x).

#include "appdocumentfixture.h"

#include "pdfappautorefresh.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappprint.h"
#include "pdfappprintdialog.h"
#include "pdfappsendmail.h"
#include "pdfappundostack.h"
#include "pdfrecentrepository.h"
#include "pdfsettingdefinitions.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfthemecolors.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QCheckBox>
#include <QDir>
#include <QMutex>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalSpy>
#include <QtTest>

#include <atomic>

/// Helpers of the tests of package E
class FileTestHelper
{
public:
    FileTestHelper() = delete;

    /// Opens the file and waits until the document is shown
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab)
        {
            return nullptr;
        }
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        if (!QTest::qWaitFor([&guard]() { return !guard || (guard->getDocument() && guard->getDocument()->isReady()); }, 20000))
        {
            return nullptr;
        }
        return guard.data();
    }

    static pdf::PDFAppDocumentFileCommandHandler* handler(pdf::PDFAppDocumentTab* tab)
    {
        return pdf::PDFAppDocumentFileCommandHandler::find(tab);
    }

    /// Changes the title of the document (one undo step)
    static bool modify(pdf::PDFAppDocument* document, const QString& title)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(title);
        pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(builder.build()));
        return document->commit(pdf::PDFModifiedDocument(pointer, nullptr, pdf::PDFModifiedDocument::None), title, document->getVersion(),
                                pdf::PDFAppChangeSource::User);
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// Replaces the file atomically (as an editor saves it)
    static bool replaceFile(const QString& filePath, const QByteArray& data)
    {
        // A reader of the file (the thumbnail of Recent) blocks the replacement on
        // Windows for a moment: an editor tries it again
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            QSaveFile file(filePath);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
            {
                return true;
            }
            QTest::qWait(100);
        }
        return false;
    }

    /// Page count of a PDF file (-1 for an invalid file)
    static int getPageCount(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? int(document.getCatalog()->getPageCount()) : -1;
    }

    /// Title of the document info of a PDF file
    static QString getTitle(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? document.getInfo()->title : QString();
    }

    /// Visible message box of the object name
    static pdf::PDFMessageBox* findDialog(pdftest::AppDocumentFixture& fixture, const QString& name)
    {
        for (pdf::PDFMessageBox* box : fixture.window->findChildren<pdf::PDFMessageBox*>(name))
        {
            if (box->isVisible())
            {
                return box;
            }
        }
        return nullptr;
    }

    static void click(QWidget* dialog, const QString& buttonName)
    {
        QPushButton* button = dialog ? dialog->findChild<QPushButton*>(buttonName) : nullptr;
        QVERIFY2(button, qPrintable(buttonName));
        button->click();
    }

    /// Printers writing PDF files into the folder
    static void setTestPrinters(const QString& folder, int count = 1)
    {
        pdf::PDFAppPrinters::setProvider([folder, count]()
        {
            std::vector<pdf::PDFAppPrinterInfo> printers;
            for (int i = 0; i < count; ++i)
            {
                pdf::PDFAppPrinterInfo info;
                info.name = QStringLiteral("Test Printer %1").arg(i + 1);
                info.location = QStringLiteral("Office");
                info.isDefault = i == 0;
                info.stateText = QStringLiteral("Ready");
                info.outputFile = QDir(folder).absoluteFilePath(QStringLiteral("printed-%1.pdf").arg(i + 1));
                printers.push_back(info);
            }
            return printers;
        });
    }

    /// Opens the print dialog of the tab
    static pdf::PDFAppPrintDialog* openPrintDialog(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFAppDocumentFileCommandHandler* fileHandler = handler(tab);
        if (!fileHandler || !fileHandler->isEnabled(pdf::PDFAppCommands::print()))
        {
            return nullptr;
        }
        fileHandler->execute(pdf::PDFAppCommands::print());
        return fileHandler->getPrintController()->getDialog();
    }

    /// Number of the pixels of the leak color in the picture
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if ((line[x] & 0xFFFFFF) == leak && qAlpha(line[x]) > 0)
                {
                    ++count;
                }
            }
        }
        return count;
    }
};

class AppDocumentFileTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    // Commands and the API of wave 0
    void commands();
    void commandStates();
    void saveDialogHandler();
    void originalDataAndSavedVersion();
    void ownerPasswordSample();

    // Save
    void saveModified();
    void saveRecordsRecent();
    void saveAs();
    void saveAsCanceled();
    void saveAsOpenInAnotherTab();
    void saveReadOnlyFile();
    void saveWriteFailure();
    void saveSignedIncremental();
    void saveSignedCanceled();
    void saveSignedAsCopy();
    void saveChangedDuringWrite();
    void writerUnitCases();

    // Automatic refresh
    void autoRefreshReload();
    void autoRefreshOwnSaveIgnored();
    void autoRefreshUnsavedKeep();
    void autoRefreshUnsavedReload();
    void autoRefreshSwitch();
    void autoRefreshSettingOff();
    void autoRefreshHostileFiles();
    void autoRefreshReaderUnit();

    // Print
    void printDialogDefaults();
    void printSelectedPagesPreselected();
    void printToFile();
    void printCopiesAndCollate();
    void printPreview();
    void printNoPrinter();
    void printStop();
    void printErrorPrintTheRest();
    void printPrinterProperties();
    void printPermission();
    void printGeometry();
    void printNarrowWindow();
    void printThemesAndDirection();
    void neutralBadge();

    // E-mail
    void mailSendsSavedFile();
    void mailSaveFirst();
    void mailError();
    void mailArguments();

    // Lifetime
    void deleteTabDuringSave();
    void deleteTabDuringPrint();
    void deleteTabWithDialogs_data();
    void deleteTabWithDialogs();
    void deleteWindowWithPrintDialog();
    void deleteTabFromOwnSignals_data();
    void deleteTabFromOwnSignals();

    // Pictures for the comparison with Figma
    void screenshots();
};

void AppDocumentFileTest::initTestCase()
{
    // The style of the application (the dialogs are drawn by the tokens of the theme)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppDocumentFileTest::cleanup()
{
    pdf::PDFAppPrinters::setProvider(pdf::PDFAppPrinters::Provider());
    pdf::PDFAppPrinters::setPropertiesHandler(pdf::PDFAppPrinters::PropertiesHandler());
    pdf::PDFAppSendMail::setLauncher(pdf::PDFAppSendMail::Launcher());
    QThreadPool::globalInstance()->waitForDone(30000);
}

void AppDocumentFileTest::commands()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("File.pdf"), 1));
    QVERIFY(tab);

    for (const QString& id : pdf::PDFAppDocumentCommandTable::getCommands(pdf::PDFAppDocumentPackage::File))
    {
        QVERIFY2(tab->findHandler(id), qPrintable(id));
    }
    QCOMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::save())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_S));
    QCOMPARE(fixture.window->getCommandAction(pdf::PDFAppCommands::print())->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_P));
    QVERIFY(FileTestHelper::handler(tab));
    QVERIFY(!pdf::PDFAppDocumentFileCommandHandler::find(nullptr));
}

void AppDocumentFileTest::commandStates()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("States.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(handler);

    // Save needs a change, the others are available
    QVERIFY(!handler->isEnabled(pdf::PDFAppCommands::save()));
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::saveAs()));
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::print()));
    QCOMPARE(handler->isEnabled(pdf::PDFAppCommands::sendMail()), pdf::PDFAppSendMail::isAvailable());
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::autoRefresh()));
    QVERIFY(handler->isChecked(pdf::PDFAppCommands::autoRefresh()));
    QVERIFY(!handler->isChecked(pdf::PDFAppCommands::save()));

    QSignalSpy stateSpy(handler, &pdf::PDFAppDocumentCommandHandler::stateChanged);
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("Changed")));
    QVERIFY(stateSpy.count() > 0);
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::save()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::save()));

    // The window follows the tab
    fixture.window->updateCommandStates();
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::save())->isEnabled());

    // A locked document is not saved (Save as is)
    tab->getDocument()->setLocked(true);
    QVERIFY(!handler->isEnabled(pdf::PDFAppCommands::save()));
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::saveAs()));
    tab->getDocument()->setLocked(false);

    // The switch of the automatic refresh
    handler->execute(pdf::PDFAppCommands::autoRefresh());
    QVERIFY(!handler->isChecked(pdf::PDFAppCommands::autoRefresh()));
    handler->execute(pdf::PDFAppCommands::autoRefresh());
    QVERIFY(handler->isChecked(pdf::PDFAppCommands::autoRefresh()));

    // An unknown command is not handled
    handler->execute(QStringLiteral("unknown.command"));
    QVERIFY(!handler->isEnabled(QStringLiteral("unknown.command")));
}

void AppDocumentFileTest::saveDialogHandler()
{
    pdftest::AppDocumentFixture fixture;
    QString proposed;
    pdf::PDFAppFileDialogs::setSaveFileHandler([&proposed](QWidget*, const QString& filePath)
    {
        proposed = filePath;
        return QStringLiteral("C:/saved.pdf");
    });
    QCOMPARE(pdf::PDFAppFileDialogs::getSaveFileName(fixture.window, QStringLiteral("D:/a.pdf")), QStringLiteral("C:/saved.pdf"));
    QCOMPARE(proposed, QStringLiteral("D:/a.pdf"));
}

void AppDocumentFileTest::originalDataAndSavedVersion()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Original.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    QCOMPARE(document->getOriginalData(), FileTestHelper::readFile(file));

    const pdf::PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    QVERIFY(snapshot.isValid());
    QCOMPARE(snapshot.filePath, QFileInfo(file).canonicalFilePath());
    pdf::PDFDocumentPointer copy(new pdf::PDFDocument(*snapshot.document));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(copy, nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Change"), snapshot.version, pdf::PDFAppChangeSource::User));
    QVERIFY(document->isModified());
    document->setSaved();
    QVERIFY(!document->isModified());
}

void AppDocumentFileTest::ownerPasswordSample()
{
    // A document with an owner password only opens without a password
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::ownerPassword(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isPasswordProtected());
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));
}

void AppDocumentFileTest::saveModified()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Save.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);

    QVERIFY(FileTestHelper::modify(document, QStringLiteral("Saved title")));
    QVERIFY(document->isModified());

    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    fixture.window->updateCommandStates();
    fixture.window->getCommandAction(pdf::PDFAppCommands::save())->trigger();
    QVERIFY(handler->getSaver()->isRunning() || finishedSpy.count() == 1);
    QVERIFY(!handler->isEnabled(pdf::PDFAppCommands::save()));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), true);

    // The file has the change, the document is saved and holds the bytes of the file
    QVERIFY(!document->isModified());
    QCOMPARE(FileTestHelper::getTitle(file), QStringLiteral("Saved title"));
    QCOMPARE(document->getOriginalData(), FileTestHelper::readFile(file));
    QVERIFY(!handler->getSaver()->isRunning());
    QVERIFY(document->getOperations().empty());

    // No temporary file is left
    const QStringList files = QDir(fixture.folder()).entryList(QDir::Files);
    for (const QString& name : files)
    {
        QVERIFY2(!name.contains(QStringLiteral(".tmp")) && !name.endsWith(QStringLiteral("~")), qPrintable(name));
    }

    // A second save without a change is not possible (Save is disabled), save() still works
    QVERIFY(!handler->isEnabled(pdf::PDFAppCommands::save()));
    QVERIFY(handler->save());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 20000);
    QCOMPARE(finishedSpy.at(1).at(0).toBool(), true);
}

void AppDocumentFileTest::saveRecordsRecent()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Recent.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);

    const QString target = fixture.path(QStringLiteral("Recent copy.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());

    const QString canonical = QFileInfo(target).canonicalFilePath();
    QVERIFY(fixture.context->getRecent().getFile(canonical).has_value());
}

void AppDocumentFileTest::saveAs()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Source.pdf"), 3);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("New file")));

    const QString target = fixture.path(QStringLiteral("Target.pdf"));
    QString proposed;
    pdf::PDFAppFileDialogs::setSaveFileHandler([target, &proposed](QWidget*, const QString& filePath)
    {
        proposed = filePath;
        return target;
    });

    QSignalSpy titleSpy(tab, &pdf::PDFAppTab::pageTitleChanged);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    handler->execute(pdf::PDFAppCommands::saveAs());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());

    QCOMPARE(proposed, QFileInfo(file).canonicalFilePath());
    QCOMPARE(document->getFilePath(), QFileInfo(target).canonicalFilePath());
    QCOMPARE(document->getTitle(), QStringLiteral("Target.pdf"));
    QCOMPARE(tab->getTabTitle(), QStringLiteral("Target.pdf"));
    QVERIFY(titleSpy.count() > 0);
    QVERIFY(!document->isModified());
    QCOMPARE(FileTestHelper::getTitle(target), QStringLiteral("New file"));
    QVERIFY(FileTestHelper::getTitle(file) != QStringLiteral("New file"));
    QCOMPARE(fixture.context->getDocumentRegistry()->findByCanonicalPath(QFileInfo(target).canonicalFilePath()), document);

    // The automatic refresh watches the new file
    QCOMPARE(handler->getAutoRefresh()->getWatchedPath(), QFileInfo(target).canonicalFilePath());
}

void AppDocumentFileTest::saveAsCanceled()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Cancel.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));

    pdf::PDFAppFileDialogs::setSaveFileHandler([](QWidget*, const QString&) { return QString(); });
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), false);
    QVERIFY(tab->getDocument()->isModified());
    QVERIFY(!handler->getSaver()->isRunning());
}

void AppDocumentFileTest::saveAsOpenInAnotherTab()
{
    pdftest::AppDocumentFixture fixture;
    const QString first = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("First.pdf"), 1);
    const QString second = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second.pdf"), 1);
    pdf::PDFAppDocumentTab* firstTab = FileTestHelper::openReady(fixture, first);
    pdf::PDFAppDocumentTab* secondTab = FileTestHelper::openReady(fixture, second);
    QVERIFY(firstTab && secondTab);

    pdf::PDFAppFileDialogs::setSaveFileHandler([second](QWidget*, const QString&) { return second; });
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(firstTab);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());

    // The file of the other tab is not overwritten: save-error offers another file
    pdf::PDFMessageBox* dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveErrorDialog"));
    QVERIFY(dialog);
    QCOMPARE(finishedSpy.count(), 0);
    FileTestHelper::click(dialog, QStringLiteral("cancelButton"));
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), false);
    QCOMPARE(FileTestHelper::readFile(second), secondTab->getDocument()->getOriginalData());
}

void AppDocumentFileTest::saveReadOnlyFile()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ReadOnly.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("Changed")));
    const QByteArray original = FileTestHelper::readFile(file);

    QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    QVERIFY(!pdf::PDFAppDocumentFileWriter::isWritable(file));
    QVERIFY(!handler->isEnabled(pdf::PDFAppCommands::save()));

    // save() of the close dialogs: the dialog save-error, Save As… to another file
    const QString target = fixture.path(QStringLiteral("Writable.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    pdf::PDFMessageBox* dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveErrorDialog"));
    QVERIFY(dialog);
    QVERIFY(dialog->windowTitle().size() >= 0);
    QCOMPARE(handler->getSaver()->getDialog(), dialog);
    QVERIFY(!handler->isEnabled(pdf::PDFAppCommands::saveAs()));
    FileTestHelper::click(dialog, QStringLiteral("saveAsButton"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());

    QCOMPARE(FileTestHelper::readFile(file), original);
    QCOMPARE(document->getFilePath(), QFileInfo(target).canonicalFilePath());
    QVERIFY(!document->isModified());
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
}

void AppDocumentFileTest::saveWriteFailure()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Failure.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("Changed")));

    // A folder, which does not exist: the dialog save-error, Escape ends the flow
    const QString target = fixture.path(QStringLiteral("missing/folder/Target.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->saveAs());
    pdf::PDFMessageBox* dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveErrorDialog"));
    QVERIFY(dialog);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), false);
    QVERIFY(tab->getDocument()->isModified());
    QVERIFY(!QFileInfo::exists(target));

    // The writer reports an error of the system
    pdf::PDFAppDocumentFileWriter::Request request;
    request.document = tab->getDocument()->getDocument();
    request.filePath = target;
    const pdf::PDFAppDocumentFileWriter::Result result = pdf::PDFAppDocumentFileWriter::write(request);
    QVERIFY(!result.isSaved);
    QVERIFY(!result.errorMessage.isEmpty());
}

void AppDocumentFileTest::saveSignedIncremental()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    QVERIFY(!file.isEmpty());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QCOMPARE(pdf::PDFAppDocumentFileWriter::getSignatureCount(document->getDocument().data()), 1);
    const QByteArray original = FileTestHelper::readFile(file);

    QVERIFY(FileTestHelper::modify(document, QStringLiteral("After signing")));
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());

    // The dialog save-signed: Save Incrementally
    pdf::PDFMessageBox* dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveSignedDialog"));
    QVERIFY(dialog);
    QCOMPARE(dialog->getActionButtons().size(), 3);
    QCOMPARE(dialog->getActionButtons().at(0)->objectName(), QStringLiteral("saveCopyButton"));
    QCOMPARE(dialog->getActionButtons().at(2)->objectName(), QStringLiteral("saveButton"));
    FileTestHelper::click(dialog, QStringLiteral("saveButton"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());

    // The signed revision is kept byte by byte, the update follows it
    const QByteArray saved = FileTestHelper::readFile(file);
    QVERIFY(saved.size() > original.size());
    QVERIFY(saved.startsWith(original));
    QCOMPARE(FileTestHelper::getTitle(file), QStringLiteral("After signing"));
    QVERIFY(handler->getSaver()->isSignedSaveConfirmed());

    // The next save of the tab does not ask again
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("Second change")));
    QVERIFY(handler->save());
    QVERIFY(!FileTestHelper::findDialog(fixture, QStringLiteral("saveSignedDialog")));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 20000);
    QVERIFY(FileTestHelper::readFile(file).startsWith(saved));
}

void AppDocumentFileTest::saveSignedCanceled()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    const QByteArray original = FileTestHelper::readFile(file);
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));

    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    pdf::PDFMessageBox* dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveSignedDialog"));
    QVERIFY(dialog);

    // A second request joins the running one
    QVERIFY(handler->save());
    FileTestHelper::click(dialog, QStringLiteral("cancelButton"));
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(0).toBool(), false);
    QCOMPARE(FileTestHelper::readFile(file), original);
    QVERIFY(!handler->getSaver()->isSignedSaveConfirmed());

    // Escape cancels too
    QVERIFY(handler->save());
    dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveSignedDialog"));
    QVERIFY(dialog);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_COMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).toBool(), false);
}

void AppDocumentFileTest::saveSignedAsCopy()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    const QByteArray original = FileTestHelper::readFile(file);
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("Copy")));

    const QString target = fixture.path(QStringLiteral("Signed copy.pdf"));
    pdf::PDFAppFileDialogs::setSaveFileHandler([target](QWidget*, const QString&) { return target; });
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    pdf::PDFMessageBox* dialog = FileTestHelper::findDialog(fixture, QStringLiteral("saveSignedDialog"));
    QVERIFY(dialog);
    FileTestHelper::click(dialog, QStringLiteral("saveCopyButton"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());

    // The original is untouched, the copy keeps the signed revision
    QCOMPARE(FileTestHelper::readFile(file), original);
    QVERIFY(FileTestHelper::readFile(target).startsWith(original));
    QCOMPARE(tab->getDocument()->getFilePath(), QFileInfo(target).canonicalFilePath());
}

void AppDocumentFileTest::saveChangedDuringWrite()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::large(fixture.folder(), 300);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("First")));
    const quint64 writtenVersion = document->getVersion();

    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    if (handler->getSaver()->isWriting())
    {
        // The document stays usable: a change during the writing keeps it modified
        QVERIFY(FileTestHelper::modify(document, QStringLiteral("Second")));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 30000);
        QVERIFY(finishedSpy.at(0).at(0).toBool());
        QVERIFY(document->isModified());
        QCOMPARE(document->getSavedVersion(), writtenVersion);
        QCOMPARE(FileTestHelper::getTitle(file), QStringLiteral("First"));
    }
    else
    {
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 30000);
    }
}

void AppDocumentFileTest::writerUnitCases()
{
    // No document, an incremental update of no original data
    pdf::PDFAppDocumentFileWriter::Request request;
    pdf::PDFAppDocumentFileWriter::Result result = pdf::PDFAppDocumentFileWriter::write(request);
    QVERIFY(!result.isSaved);
    QVERIFY(!result.errorMessage.isEmpty());

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Unit.pdf"), 1));
    QVERIFY(tab);
    request.document = tab->getDocument()->getDocument();
    request.filePath = fixture.path(QStringLiteral("Incremental.pdf"));
    request.isIncremental = true;
    request.originalData = QByteArray("not a pdf");
    result = pdf::PDFAppDocumentFileWriter::write(request);
    QVERIFY(!result.isSaved);
    QVERIFY(!QFileInfo::exists(request.filePath));

    request.originalData = tab->getDocument()->getOriginalData();
    request.version = 7;
    result = pdf::PDFAppDocumentFileWriter::write(request);
    QVERIFY(result.isSaved);
    QCOMPARE(result.version, quint64(7));
    QVERIFY(result.isIncremental);
    QCOMPARE(result.data, FileTestHelper::readFile(request.filePath));

    QCOMPARE(pdf::PDFAppDocumentFileWriter::getSignatureCount(nullptr), 0);
    QVERIFY(!pdf::PDFAppDocumentFileWriter::isWritable(QString()));
    QVERIFY(!pdf::PDFAppDocumentFileWriter::isWritable(fixture.folder()));
    QVERIFY(pdf::PDFAppDocumentFileWriter::isWritable(fixture.path(QStringLiteral("New.pdf"))));
}

void AppDocumentFileTest::autoRefreshReload()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refresh.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = FileTestHelper::handler(tab)->getAutoRefresh();
    QVERIFY(refresh->isEnabled());
    QCOMPARE(refresh->getWatchedPath(), QFileInfo(file).canonicalFilePath());

    // The undo history is cleared by a reload
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("x")));
    document->setSaved();
    QVERIFY(document->getUndoStack()->canUndo());

    // Another program replaces the file by a document with 5 pages
    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 5);
    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    QVERIFY(FileTestHelper::replaceFile(file, FileTestHelper::readFile(other)));
    if (!reloadedSpy.wait(5000))
    {
        // The watcher of the platform may be slow: the check is forced
        refresh->checkNow();
    }
    QTRY_VERIFY_WITH_TIMEOUT(reloadedSpy.count() >= 1, 20000);
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(5));
    QVERIFY(!document->isModified());
    QVERIFY(!document->getUndoStack()->canUndo());
    QCOMPARE(document->getOriginalData(), FileTestHelper::readFile(file));

    // The atomic replace removed the path from the watcher; it is watched again
    QTRY_COMPARE(refresh->getWatchedPath(), QFileInfo(file).canonicalFilePath());
    QVERIFY(refresh->getReloadCount() >= 1);
}

void AppDocumentFileTest::autoRefreshOwnSaveIgnored()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Own.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    pdf::PDFAppAutoRefresh* refresh = handler->getAutoRefresh();

    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("Own")));
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);

    QSignalSpy checkedSpy(refresh, &pdf::PDFAppAutoRefresh::checked);
    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    refresh->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(checkedSpy.count() >= 1, 20000);
    QCOMPARE(checkedSpy.last().at(0).value<pdf::PDFAppAutoRefreshReader::Outcome>(), pdf::PDFAppAutoRefreshReader::Outcome::Unchanged);
    QTest::qWait(pdf::PDFAppAutoRefresh::CHECK_DELAY + 300);
    QCOMPARE(reloadedSpy.count(), 0);
    QCOMPARE(tab->getDocument()->getSavedVersion(), tab->getDocument()->getVersion());
}

void AppDocumentFileTest::autoRefreshUnsavedKeep()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Keep.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = FileTestHelper::handler(tab)->getAutoRefresh();
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("Mine")));

    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Theirs.pdf"), 4);
    QVERIFY(FileTestHelper::replaceFile(file, FileTestHelper::readFile(other)));
    refresh->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(refresh->getDialog(), 20000);
    pdf::PDFMessageBox* dialog = refresh->getDialog();
    QCOMPARE(dialog->objectName(), QStringLiteral("refreshUnsavedDialog"));
    QCOMPARE(dialog->getActionButtons().at(0)->objectName(), QStringLiteral("reloadButton"));

    // Keep My Version: the document is not touched, the kept file is not offered again
    QSignalSpy checkedSpy(refresh, &pdf::PDFAppAutoRefresh::checked);
    FileTestHelper::click(dialog, QStringLiteral("keepButton"));
    QTRY_VERIFY(!refresh->getDialog());
    QVERIFY(document->isModified());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(2));

    refresh->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(checkedSpy.count() >= 1, 20000);
    QTest::qWait(100);
    QVERIFY(!refresh->getDialog());

    // My version replaces the file by Save
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QCOMPARE(FileTestHelper::getPageCount(file), 2);
}

void AppDocumentFileTest::autoRefreshUnsavedReload()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Lose.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = FileTestHelper::handler(tab)->getAutoRefresh();
    QVERIFY(FileTestHelper::modify(document, QStringLiteral("Mine")));

    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Theirs.pdf"), 3);
    QVERIFY(FileTestHelper::replaceFile(file, FileTestHelper::readFile(other)));
    refresh->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(refresh->getDialog(), 20000);

    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    FileTestHelper::click(refresh->getDialog(), QStringLiteral("reloadButton"));
    QTRY_COMPARE(reloadedSpy.count(), 1);
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(3));
    QVERIFY(!document->isModified());
}

void AppDocumentFileTest::autoRefreshSwitch()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Switch.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    pdf::PDFAppAutoRefresh* refresh = handler->getAutoRefresh();

    QSignalSpy stateSpy(handler, &pdf::PDFAppDocumentCommandHandler::stateChanged);
    fixture.window->updateCommandStates();
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::autoRefresh())->isChecked());
    handler->execute(pdf::PDFAppCommands::autoRefresh());
    QVERIFY(stateSpy.count() > 0);
    QVERIFY(!refresh->isEnabled());
    QVERIFY(refresh->getWatchedPath().isEmpty());

    // A changed file is not read while the switch is off
    QSignalSpy checkedSpy(refresh, &pdf::PDFAppAutoRefresh::checked);
    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Off.pdf"), 6);
    QVERIFY(FileTestHelper::replaceFile(file, FileTestHelper::readFile(other)));
    refresh->checkNow();
    QTest::qWait(pdf::PDFAppAutoRefresh::CHECK_DELAY + 300);
    QCOMPARE(checkedSpy.count(), 0);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));

    // The switch on again checks the next change
    refresh->setEnabled(true);
    QCOMPARE(refresh->getWatchedPath(), QFileInfo(file).canonicalFilePath());
    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    refresh->checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(reloadedSpy.count(), 1, 20000);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(6));
}

void AppDocumentFileTest::autoRefreshSettingOff()
{
    pdftest::AppDocumentFixture fixture;
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::documentAutoRefresh(), false).isError());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Setting.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QVERIFY(!handler->getAutoRefresh()->isEnabled());
    QVERIFY(!handler->isChecked(pdf::PDFAppCommands::autoRefresh()));
    QVERIFY(handler->getAutoRefresh()->getWatchedPath().isEmpty());
}

void AppDocumentFileTest::autoRefreshHostileFiles()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Hostile.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppAutoRefresh* refresh = FileTestHelper::handler(tab)->getAutoRefresh();
    QSignalSpy checkedSpy(refresh, &pdf::PDFAppAutoRefresh::checked);
    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    const QByteArray original = FileTestHelper::readFile(file);

    // Garbage, a truncated file and an empty file are ignored (a file being written)
    for (const QByteArray& data : { QByteArray("garbage %PDF"), original.left(original.size() / 2), QByteArray() })
    {
        QVERIFY(FileTestHelper::replaceFile(file, data));
        const int count = checkedSpy.count();
        refresh->checkNow();
        QTRY_VERIFY_WITH_TIMEOUT(checkedSpy.count() > count, 20000);
        QCOMPARE(reloadedSpy.count(), 0);
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2));
    }

    // A missing file is watched again, when it appears
    QVERIFY(QFile::remove(file));
    const int count = checkedSpy.count();
    refresh->checkNow();
    // A check of a notification of the previous write may still end before it
    QTRY_VERIFY_WITH_TIMEOUT(checkedSpy.count() > count &&
                             checkedSpy.last().at(0).value<pdf::PDFAppAutoRefreshReader::Outcome>() == pdf::PDFAppAutoRefreshReader::Outcome::Missing, 20000);
    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Back.pdf"), 4);
    QVERIFY(QFile::copy(other, file));
    refresh->checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(reloadedSpy.count(), 1, 20000);
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(4));
}

void AppDocumentFileTest::autoRefreshReaderUnit()
{
    using Reader = pdf::PDFAppAutoRefreshReader;
    QTemporaryDir directory;
    const QString missing = QDir(directory.path()).absoluteFilePath(QStringLiteral("missing.pdf"));
    QCOMPARE(Reader::read(missing, QByteArray()).outcome, Reader::Outcome::Missing);
    QCOMPARE(Reader::read(directory.path(), QByteArray()).outcome, Reader::Outcome::Failed);

    const QString file = pdftest::AppDocumentSamples::simple(directory.path(), QStringLiteral("Unit.pdf"), 1);
    const QByteArray data = FileTestHelper::readFile(file);
    QCOMPARE(Reader::read(file, data).outcome, Reader::Outcome::Unchanged);
    const Reader::Result changed = Reader::read(file, QByteArray("other"));
    QCOMPARE(changed.outcome, Reader::Outcome::Changed);
    QVERIFY(!changed.document.isNull());
    QCOMPARE(changed.data, data);
    QVERIFY(!changed.isPasswordProtected);

    const QString encrypted = pdftest::AppDocumentSamples::userPassword(directory.path());
    QCOMPARE(Reader::read(encrypted, QByteArray()).outcome, Reader::Outcome::PasswordRequired);
    const QString owner = pdftest::AppDocumentSamples::ownerPassword(directory.path());
    const Reader::Result ownerResult = Reader::read(owner, QByteArray());
    QCOMPARE(ownerResult.outcome, Reader::Outcome::Changed);
    QVERIFY(ownerResult.isPasswordProtected);
}

void AppDocumentFileTest::printDialogDefaults()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder(), 2);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Print.pdf"), 4));
    QVERIFY(tab);

    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    QVERIFY(!FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()));

    // Two printers, the default first; the state of the printer
    QCOMPARE(dialog->getPrinters().size(), size_t(2));
    QCOMPARE(dialog->getPrinterIndex(), 0);
    QCOMPARE(dialog->getPrinterCombo()->currentText(), QStringLiteral("Test Printer 1"));
    QCOMPARE(dialog->getPrinterStatusText(), QStringLiteral("Ready · Office"));
    QVERIFY(dialog->getPrinter());
    QVERIFY(!dialog->getNoPrinterNote()->isVisibleTo(dialog));
    QVERIFY(!dialog->getPropertiesButton()->isVisibleTo(dialog));

    // All pages, one copy, Auto, fit to page, color
    QCOMPARE(dialog->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::All);
    const pdf::PDFAppPrintSettings settings = dialog->getSettings();
    QCOMPARE(settings.pages.size(), size_t(4));
    QCOMPARE(settings.copies, 1);
    QCOMPARE(settings.orientation, pdf::PDFAppPrintOrientation::Auto);
    QCOMPARE(settings.scaling, pdf::PDFAppPrintScaling::FitToPage);
    QVERIFY(!settings.isGrayscale);
    QVERIFY(settings.pageSize.isValid());
    QVERIFY(!dialog->getScaleSpinBox()->isEnabled());
    QVERIFY(!dialog->getCollateCheckBox()->isEnabled());
    QVERIFY(dialog->getPrintButton()->isEnabled());
    QVERIFY(dialog->getPrintButton()->isDefault());
    QVERIFY(dialog->getSummaryText().contains(QStringLiteral("4")));
    QVERIFY(dialog->getPreviewText().startsWith(QStringLiteral("Page 1")));
    QVERIFY(dialog->getPaperText().contains(QStringLiteral("fit to page")));

    // Custom scaling enables the field; grayscale; another printer
    dialog->getScalingControl()->setCurrentIndex(2);
    QVERIFY(dialog->getScaleSpinBox()->isEnabled());
    dialog->getScaleSpinBox()->setValue(150);
    dialog->getColorControl()->setCurrentIndex(1);
    dialog->getOrientationControl()->setCurrentIndex(2);
    const pdf::PDFAppPrintSettings changed = dialog->getSettings();
    QCOMPARE(changed.scaling, pdf::PDFAppPrintScaling::Custom);
    QCOMPARE(changed.customScale, 150);
    QVERIFY(changed.isGrayscale);
    QCOMPARE(changed.orientation, pdf::PDFAppPrintOrientation::Landscape);
    QVERIFY(dialog->getPaperText().contains(QStringLiteral("150")));
    dialog->setPrinterIndex(1);
    QCOMPARE(dialog->getPrinterIndex(), 1);
    QVERIFY(dialog->getPrinter()->outputFileName().endsWith(QStringLiteral("printed-2.pdf")));

    // An invalid range disables Print
    dialog->setRange(QStringLiteral("9-12"));
    QVERIFY(!dialog->getPageSelection()->isValid());
    QVERIFY(!dialog->getPrintButton()->isEnabled());
    QVERIFY(dialog->getSettings().pages.empty());

    // Cancel closes the dialog, no job
    dialog->getCancelButton()->click();
    QTRY_VERIFY(!FileTestHelper::handler(tab)->getPrintController()->getDialog());
    QVERIFY(!FileTestHelper::handler(tab)->getPrintController()->isPrinting());
    QVERIFY(FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()));
}

void AppDocumentFileTest::printSelectedPagesPreselected()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Selected.pdf"), 6));
    QVERIFY(tab);

    // Thumbnails 2, 4, 5 selected (O9): the mode Selected
    tab->getView()->setSelectedPages({ 1, 3, 4 });
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    QCOMPARE(dialog->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Selected);
    const std::vector<pdf::PDFInteger> expected = { 1, 3, 4 };
    QCOMPARE(dialog->getSettings().pages, expected);
    dialog->reject();
    QTRY_VERIFY(!FileTestHelper::handler(tab)->getPrintController()->getDialog());

    // Only the current page selected: All
    tab->getView()->setSelectedPages({});
    dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    QCOMPARE(dialog->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::All);
    dialog->reject();
}

void AppDocumentFileTest::printToFile()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("ToFile.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppPrintController* controller = FileTestHelper::handler(tab)->getPrintController();

    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->setRange(QStringLiteral("2-4"));
    QCOMPARE(dialog->getSettings().pages.size(), size_t(3));

    QSignalSpy finishedSpy(controller, &pdf::PDFAppPrintController::printFinished);
    dialog->getPrintButton()->click();
    QVERIFY(controller->isPrinting() || finishedSpy.count() == 1);
    if (controller->isPrinting())
    {
        // The document stays usable; the operation Print shows the progress on the tab
        QVERIFY(!tab->getDocument()->isBlockedByOperation());
        QVERIFY(FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::saveAs()));
        QVERIFY(!FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()));
    }
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);

    QVERIFY(controller->getLastResult().isCompleted);
    QCOMPARE(controller->getLastResult().printedCount, 3);
    QCOMPARE(FileTestHelper::getPageCount(fixture.path(QStringLiteral("printed-1.pdf"))), 3);
    QVERIFY(!tab->getView()->findInfoBar(pdf::PDFAppPrintController::getInfoBarId()));
    QVERIFY(tab->getDocument()->getOperations().empty());
    QVERIFY(tab->getView()->getStatusBar()->getMessage().contains(QStringLiteral("Test Printer 1")));
}

void AppDocumentFileTest::printCopiesAndCollate()
{
    pdf::PDFAppPrintSettings settings;
    settings.pages = { 0, 1, 2 };
    settings.copies = 2;
    settings.collate = true;
    std::vector<pdf::PDFAppPrintJob::Item> items = pdf::PDFAppPrintJob::getItems(settings);
    QCOMPARE(items.size(), size_t(6));
    QCOMPARE(items[2].pageIndex, pdf::PDFInteger(2));
    QCOMPARE(items[3].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(items[3].copy, 1);

    settings.collate = false;
    items = pdf::PDFAppPrintJob::getItems(settings);
    QCOMPARE(items[0].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(items[1].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(items[1].copy, 1);
    QCOMPARE(items[2].pageIndex, pdf::PDFInteger(1));

    // Hostile values are bounded
    settings.copies = 100000;
    settings.pages = { 0 };
    QCOMPARE(pdf::PDFAppPrintJob::getItems(settings).size(), size_t(pdf::PDFAppPrintSettings::MAXIMUM_COPIES));
    settings.copies = -5;
    QCOMPARE(pdf::PDFAppPrintJob::getItems(settings).size(), size_t(1));

    // The job prints the copies (two copies of two pages = four pages)
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Copies.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->setCopies(2);
    QVERIFY(dialog->getCollateCheckBox()->isEnabled());
    dialog->getCollateCheckBox()->setChecked(false);
    QVERIFY(dialog->getSummaryText().contains(QStringLiteral("2 copies")));
    pdf::PDFAppPrintController* controller = FileTestHelper::handler(tab)->getPrintController();
    QSignalSpy finishedSpy(controller, &pdf::PDFAppPrintController::printFinished);
    dialog->getPrintButton()->click();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);
    QCOMPARE(FileTestHelper::getPageCount(fixture.path(QStringLiteral("printed-1.pdf"))), 4);
}

void AppDocumentFileTest::printPreview()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Preview.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);

    QTRY_VERIFY_WITH_TIMEOUT(!dialog->getPreview()->getImage().isNull(), 20000);
    QVERIFY(!dialog->getPreviousButton()->isEnabled());
    QVERIFY(dialog->getNextButton()->isEnabled());
    QCOMPARE(dialog->getPreviewText(), QStringLiteral("Page 1 · 1 of 3"));

    // The page has dark text on the white paper
    const QImage image = dialog->getPreview()->getImage().convertToFormat(QImage::Format_ARGB32);
    int darkPixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            darkPixels += qGray(image.pixel(x, y)) < 128 ? 1 : 0;
        }
    }
    QVERIFY(darkPixels > 0);

    QSignalSpy renderedSpy(dialog, &pdf::PDFAppPrintDialog::previewRendered);
    dialog->getNextButton()->click();
    QCOMPARE(dialog->getPreviewIndex(), 1);
    QCOMPARE(dialog->getPreviewText(), QStringLiteral("Page 2 · 2 of 3"));
    QTRY_VERIFY_WITH_TIMEOUT(renderedSpy.count() >= 1, 20000);
    dialog->getNextButton()->click();
    QVERIFY(!dialog->getNextButton()->isEnabled());
    dialog->getPreviousButton()->click();
    QCOMPARE(dialog->getPreviewIndex(), 1);

    // Landscape paper in the preview; grayscale preview
    dialog->getOrientationControl()->setCurrentIndex(2);
    QVERIFY(dialog->getPreview()->getPaperSize().width() > dialog->getPreview()->getPaperSize().height());
    dialog->getColorControl()->setCurrentIndex(1);
    renderedSpy.clear();
    QTRY_VERIFY_WITH_TIMEOUT(renderedSpy.count() >= 1 && !dialog->isPreviewRendering(), 20000);
    QVERIFY(dialog->getPreview()->getImage().isGrayscale());

    // A smaller range moves the preview back into it
    dialog->setRange(QStringLiteral("1"));
    QCOMPARE(dialog->getPreviewIndex(), 0);
    dialog->reject();
}

void AppDocumentFileTest::printNoPrinter()
{
    pdftest::AppDocumentFixture fixture;
    bool hasPrinter = false;
    const QString folder = fixture.folder();
    pdf::PDFAppPrinters::setProvider([&hasPrinter, folder]()
    {
        std::vector<pdf::PDFAppPrinterInfo> printers;
        if (hasPrinter)
        {
            pdf::PDFAppPrinterInfo info;
            info.name = QStringLiteral("Added Printer");
            info.isReady = false;
            info.stateText = QStringLiteral("Not responding");
            info.outputFile = QDir(folder).absoluteFilePath(QStringLiteral("added.pdf"));
            info.pageSizes = { QPageSize(QPageSize::A5) };
            printers.push_back(info);
        }
        return printers;
    });

    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("NoPrinter.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);

    // 1029:24881: the printer-bound controls are disabled, the note with Refresh
    QCOMPARE(dialog->getPrinterIndex(), -1);
    QVERIFY(!dialog->getPrinter());
    QVERIFY(dialog->getNoPrinterNote()->isVisibleTo(dialog));
    QVERIFY(!dialog->getPrintButton()->isEnabled());
    QVERIFY(!dialog->getPrinterCombo()->isEnabled());
    QCOMPARE(dialog->getPrinterCombo()->currentText(), QStringLiteral("No printer found"));
    QVERIFY(!dialog->getCopiesSpinBox()->isEnabled());
    QVERIFY(!dialog->getPaperCombo()->isEnabled());
    QVERIFY(dialog->getOrientationControl()->isEnabled());
    QVERIFY(dialog->getPaperCombo()->count() > 0);

    // A printer added in the system: Refresh keeps the settings
    dialog->getColorControl()->setCurrentIndex(1);
    hasPrinter = true;
    dialog->getRefreshButton()->click();
    QCOMPARE(dialog->getPrinterIndex(), 0);
    QVERIFY(!dialog->getNoPrinterNote()->isVisibleTo(dialog));
    QVERIFY(dialog->getPrintButton()->isEnabled());
    QVERIFY(dialog->getSettings().isGrayscale);
    QCOMPARE(dialog->getPaperCombo()->count(), 1);
    QVERIFY(dialog->getPrinterStatusText().startsWith(QStringLiteral("Not responding")));
    dialog->reject();
}

void AppDocumentFileTest::printStop()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 400));
    QVERIFY(tab);
    pdf::PDFAppPrintController* controller = FileTestHelper::handler(tab)->getPrintController();
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->setCopies(3);

    QSignalSpy finishedSpy(controller, &pdf::PDFAppPrintController::printFinished);
    dialog->getPrintButton()->click();
    QVERIFY(controller->isPrinting());

    // The notification bar 1030:25657 with Stop
    const pdf::PDFAppInfoBarRequest* bar = tab->getView()->findInfoBar(pdf::PDFAppPrintController::getInfoBarId());
    QVERIFY(bar);
    QCOMPARE(bar->tone, pdf::PDFAppInfoBarTone::Progress);
    QVERIFY(bar->title.contains(QStringLiteral("Test Printer 1")));
    QCOMPARE(bar->actions.size(), size_t(1));
    QVERIFY(!bar->isClosable);
    QCOMPARE(tab->getDocument()->getOperations().size(), size_t(1));
    QCOMPARE(tab->getDocument()->getOperations().front()->getKind(), pdf::PDFAppDocumentOperationKind::Print);

    // Stop from the bar
    Q_EMIT tab->getView()->infoBarActionTriggered(pdf::PDFAppPrintController::getInfoBarId(), QStringLiteral("stop"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);
    QVERIFY(controller->getLastResult().isCanceled || controller->getLastResult().isCompleted);
    QVERIFY(!controller->getErrorDialog());
    QVERIFY(!tab->getView()->findInfoBar(pdf::PDFAppPrintController::getInfoBarId()));

    // The close-running dialog of package A stops the job by the operation
    dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->getPrintButton()->click();
    QVERIFY(controller->isPrinting());
    tab->getDocument()->getOperations().front()->requestCancel();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 60000);
    QVERIFY(controller->getLastResult().isCanceled || controller->getLastResult().isCompleted);
}

void AppDocumentFileTest::printErrorPrintTheRest()
{
    pdftest::AppDocumentFixture fixture;
    const QString badOutput = fixture.path(QStringLiteral("missing/folder/out.pdf"));
    pdf::PDFAppPrinters::setProvider([badOutput]()
    {
        pdf::PDFAppPrinterInfo info;
        info.name = QStringLiteral("Broken Printer");
        info.outputFile = badOutput;
        return std::vector<pdf::PDFAppPrinterInfo>{ info };
    });

    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Error.pdf"), 4));
    QVERIFY(tab);
    pdf::PDFAppPrintController* controller = FileTestHelper::handler(tab)->getPrintController();
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->setRange(QStringLiteral("2-3"));
    dialog->getPrintButton()->click();

    // 1030:25672: Printing stopped - Close / Print the Rest…
    QTRY_VERIFY_WITH_TIMEOUT(controller->getErrorDialog(), 60000);
    pdf::PDFMessageBox* errorDialog = controller->getErrorDialog();
    QCOMPARE(errorDialog->objectName(), QStringLiteral("printErrorDialog"));
    QVERIFY(!controller->getLastResult().isCompleted);
    QCOMPARE(controller->getLastResult().remainingItems.size(), size_t(2));
    QVERIFY(!FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()));

    FileTestHelper::click(errorDialog, QStringLiteral("printRestButton"));
    QTRY_VERIFY(controller->getDialog());
    dialog = controller->getDialog();
    QCOMPARE(dialog->getPageSelection()->getMode(), pdf::PDFPageSelection::Mode::Range);
    QCOMPARE(dialog->getPageSelection()->getRange(), QStringLiteral("2-3"));
    dialog->reject();
    QTRY_VERIFY(!controller->getDialog());

    // Close ends the error
    dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->getPrintButton()->click();
    QTRY_VERIFY_WITH_TIMEOUT(controller->getErrorDialog(), 60000);
    FileTestHelper::click(controller->getErrorDialog(), QStringLiteral("closeButton"));
    QTRY_VERIFY(!controller->getErrorDialog());
    QVERIFY(!controller->getDialog());
}

void AppDocumentFileTest::printPrinterProperties()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    int calls = 0;
    pdf::PDFAppPrinters::setPropertiesHandler([&calls](QWidget* parent, QPrinter* printer, const pdf::PDFAppPrinterInfo& info)
    {
        ++calls;
        if (!parent || !printer || info.name.isEmpty())
        {
            return false;
        }

        // The driver changes the paper, the orientation and the copies
        printer->setPageSize(QPageSize(QPageSize::Letter));
        printer->setPageOrientation(QPageLayout::Landscape);
        printer->setColorMode(QPrinter::GrayScale);
        printer->setCopyCount(3);
        return true;
    });

    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Properties.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    QVERIFY(dialog->getPropertiesButton()->isVisibleTo(dialog));
    QCOMPARE(dialog->getPropertiesButton()->text(), QStringLiteral("Printer properties…"));

    dialog->getOrientationControl()->setCurrentIndex(1);
    dialog->getPropertiesButton()->click();
    QCOMPARE(calls, 1);
    QVERIFY(dialog->getSettings().pageSize.isEquivalentTo(QPageSize(QPageSize::Letter)));
    QCOMPARE(dialog->getSettings().orientation, pdf::PDFAppPrintOrientation::Landscape);
    QVERIFY(dialog->getSettings().isGrayscale);
    QCOMPARE(dialog->getSettings().copies, 3);
    QCOMPARE(dialog->getPrinter()->copyCount(), 1);

    // A refused dialog of the driver changes nothing
    pdf::PDFAppPrinters::setPropertiesHandler([&calls](QWidget*, QPrinter* printer, const pdf::PDFAppPrinterInfo&)
    {
        ++calls;
        printer->setPageSize(QPageSize(QPageSize::A3));
        return false;
    });
    dialog->getPropertiesButton()->click();
    QCOMPARE(calls, 2);
    QVERIFY(dialog->getSettings().pageSize.isEquivalentTo(QPageSize(QPageSize::Letter)));
    dialog->reject();

    // Without a handler, a printer writing a file has no properties of a driver
    pdf::PDFAppPrinters::setPropertiesHandler(pdf::PDFAppPrinters::PropertiesHandler());
    pdf::PDFAppPrinterInfo fileInfo;
    fileInfo.name = QStringLiteral("File");
    fileInfo.outputFile = fixture.path(QStringLiteral("x.pdf"));
    QVERIFY(!pdf::PDFAppPrinters::hasPrinterProperties(fileInfo));
    QVERIFY(!pdf::PDFAppPrinters::showPrinterProperties(nullptr, nullptr, fileInfo));
}

void AppDocumentFileTest::printPermission()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::ownerPassword(fixture.folder()));
    QVERIFY(tab);

    // The owner password sample does not allow printing: Print is disabled
    QVERIFY(!pdf::PDFAppPrintController::isPrintAllowed(tab->getDocument()->getDocument().data()));
    QVERIFY(!FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()));
    QVERIFY(!FileTestHelper::openPrintDialog(tab));
    QVERIFY(!pdf::PDFAppPrintController::isPrintAllowed(nullptr));
    QVERIFY(!pdf::PDFAppPrintController::isHighResolutionAllowed(nullptr));

    pdf::PDFAppDocumentTab* plain = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Plain.pdf"), 1));
    QVERIFY(plain);
    QVERIFY(pdf::PDFAppPrintController::isHighResolutionAllowed(plain->getDocument()->getDocument().data()));
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(plain);
    QVERIFY(dialog);
    QVERIFY(!dialog->isLowResolution());
    dialog->reject();
}

void AppDocumentFileTest::printGeometry()
{
    using Renderer = pdf::PDFAppPrintRenderer;
    const QRectF paper(0, 0, 600, 800);
    const QRectF printable(50, 50, 500, 700);

    // Fit to page: the printable area, aspect kept, centered
    QRectF rect = Renderer::getTargetRect(QSizeF(200, 100), paper, printable, 1.0, pdf::PDFAppPrintScaling::FitToPage, 100);
    QCOMPARE(rect.width(), 500.0);
    QCOMPARE(rect.height(), 250.0);
    QCOMPARE(rect.center(), printable.center());

    // Actual size: points to device units, centered on the paper
    rect = Renderer::getTargetRect(QSizeF(200, 100), paper, printable, 2.0, pdf::PDFAppPrintScaling::ActualSize, 100);
    QCOMPARE(rect.size(), QSizeF(400, 200));
    QCOMPARE(rect.center(), paper.center());

    // Custom: bounded percentage
    rect = Renderer::getTargetRect(QSizeF(200, 100), paper, printable, 1.0, pdf::PDFAppPrintScaling::Custom, 50);
    QCOMPARE(rect.size(), QSizeF(100, 50));
    rect = Renderer::getTargetRect(QSizeF(200, 100), paper, printable, 1.0, pdf::PDFAppPrintScaling::Custom, 100000);
    QCOMPARE(rect.size(), QSizeF(800, 400));

    // Hostile input
    QVERIFY(Renderer::getTargetRect(QSizeF(), paper, printable, 1.0, pdf::PDFAppPrintScaling::FitToPage, 100).isNull());
    QVERIFY(Renderer::getTargetRect(QSizeF(10, 10), QRectF(), printable, 1.0, pdf::PDFAppPrintScaling::FitToPage, 100).isNull());
    QVERIFY(Renderer::getTargetRect(QSizeF(10, 10), paper, printable, 0.0, pdf::PDFAppPrintScaling::ActualSize, 100).isNull());
    QVERIFY(!Renderer::getTargetRect(QSizeF(10, 10), paper, QRectF(), 1.0, pdf::PDFAppPrintScaling::FitToPage, 100).isNull());
    QCOMPARE(Renderer::getPageSize(nullptr), QSizeF());
    QCOMPARE(Renderer::getPaperOrientation(nullptr, pdf::PDFAppPrintOrientation::Auto), QPageLayout::Portrait);
    QCOMPARE(Renderer::getPaperOrientation(nullptr, pdf::PDFAppPrintOrientation::Landscape), QPageLayout::Landscape);
    QVERIFY(Renderer::renderPreview(pdf::PDFDocumentPointer(), pdf::PDFCMSSettings(), 0, pdf::PDFAppPrintSettings(), paper, printable, QSize(10, 10), false).isNull());

    QVERIFY(pdf::PDFAppPrinters::getStandardPageSizes().size() >= 4);
    QVERIFY(pdf::PDFAppPrinters::getLocaleDefaultPageSize().isValid());
}

void AppDocumentFileTest::printNarrowWindow()
{
    // The minimal window 720 x 480: the preview is hidden, the card fits the window
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Narrow.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    fixture.settle();

    QVERIFY(!dialog->getPreview()->isVisibleTo(dialog));
    const QMargins shadow = dialog->getShadowMargins();
    QVERIFY2(dialog->width() - shadow.left() - shadow.right() <= 720, qPrintable(QString::number(dialog->width())));
    QVERIFY2(dialog->height() - shadow.top() - shadow.bottom() <= 480, qPrintable(QString::number(dialog->height())));
    QVERIFY(dialog->getPrintButton()->isVisible());

    // A larger window shows the preview again
    fixture.window->resize(1440, 960);
    fixture.settle();
    QTRY_VERIFY(dialog->getPreview()->isVisibleTo(dialog));
    dialog->reject();
}

void AppDocumentFileTest::printThemesAndDirection()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Themes.pdf"), 2));
    QVERIFY(tab);

    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        QApplication::setLayoutDirection(direction);
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->isVisible());
            QTRY_VERIFY_WITH_TIMEOUT(!dialog->getPreview()->getImage().isNull(), 20000);
            fixture.settle();
            QCOMPARE(dialog->layoutDirection(), direction);
            const QImage image = dialog->grab().toImage();
            QCOMPARE(FileTestHelper::countLeakPixels(image), 0);
            dialog->reject();
            QTRY_VERIFY(!FileTestHelper::handler(tab)->getPrintController()->getDialog());
        }
    }
}

void AppDocumentFileTest::neutralBadge()
{
    // The badge of the print dialog (1029:24635): accent/neutral/bg with the printer
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        QImage image(36, 36, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            pdf::PDFThemeIcons::paintBadge(&painter, QRectF(0, 0, 36, 36), pdf::PDFThemeIcons::Badge::Neutral, QStringLiteral("printer"));
        }
        const QColor expected = pdf::PDFThemeManager::getInstance()->getTheme().getColor(pdf::PDFThemeColors::AccentNeutralBg);
        QCOMPARE(QColor(image.pixel(5, 18)).rgb() & 0xFFFFFF, expected.rgb() & 0xFFFFFF);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    pdf::PDFMessageBox box(pdf::PDFThemeIcons::Badge::Neutral, QStringLiteral("Title"), QStringLiteral("Text"));
    box.setBadgeIconName(QStringLiteral("printer"));
    QCOMPARE(box.getBadgeIconName(), QStringLiteral("printer"));
    QVERIFY(box.isBadgeVisible());
}

void AppDocumentFileTest::mailSendsSavedFile()
{
    pdftest::AppDocumentFixture fixture;
    QMutex mutex;
    QStringList calls;
    pdf::PDFAppSendMail::setLauncher([&mutex, &calls](const QString& filePath, const QString& subject)
    {
        QMutexLocker locker(&mutex);
        calls << filePath << subject;
        return pdf::PDFAppMailResult::Created;
    });

    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Mail.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppSendMail* mail = FileTestHelper::handler(tab)->getSendMail();
    QVERIFY(pdf::PDFAppSendMail::isAvailable());
    QVERIFY(FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::sendMail()));

    QSignalSpy finishedSpy(mail, &pdf::PDFAppSendMail::finished);
    FileTestHelper::handler(tab)->execute(pdf::PDFAppCommands::sendMail());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QCOMPARE(finishedSpy.at(0).at(0).value<pdf::PDFAppMailResult>(), pdf::PDFAppMailResult::Created);
    QMutexLocker locker(&mutex);
    QCOMPARE(calls.size(), 2);
    QCOMPARE(calls.at(0), QFileInfo(file).canonicalFilePath());
    QCOMPARE(calls.at(1), QStringLiteral("Mail.pdf"));
    QVERIFY(!mail->getDialog());
}

void AppDocumentFileTest::mailSaveFirst()
{
    pdftest::AppDocumentFixture fixture;
    std::atomic<int> calls = 0;
    pdf::PDFAppSendMail::setLauncher([&calls](const QString&, const QString&)
    {
        ++calls;
        return pdf::PDFAppMailResult::Created;
    });

    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SaveFirst.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppSendMail* mail = FileTestHelper::handler(tab)->getSendMail();
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("Mailed")));

    // Cancel: nothing is sent
    mail->send();
    QVERIFY(mail->getDialog());
    QCOMPARE(mail->getDialog()->objectName(), QStringLiteral("saveFirstDialog"));
    FileTestHelper::click(mail->getDialog(), QStringLiteral("cancelButton"));
    QTRY_VERIFY(!mail->getDialog());
    QTest::qWait(50);
    QCOMPARE(calls.load(), 0);

    // Save and Send: the saved file goes to the e-mail
    QSignalSpy finishedSpy(mail, &pdf::PDFAppSendMail::finished);
    mail->send();
    FileTestHelper::click(mail->getDialog(), QStringLiteral("saveButton"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QCOMPARE(calls.load(), 1);
    QVERIFY(!tab->getDocument()->isModified());
    QCOMPARE(FileTestHelper::getTitle(file), QStringLiteral("Mailed"));
}

void AppDocumentFileTest::mailError()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppSendMail::setLauncher([](const QString&, const QString&) { return pdf::PDFAppMailResult::NoMailProgram; });
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("NoMail.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppSendMail* mail = FileTestHelper::handler(tab)->getSendMail();
    QSignalSpy finishedSpy(mail, &pdf::PDFAppSendMail::finished);
    mail->send();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(mail->getDialog());
    QCOMPARE(mail->getDialog()->objectName(), QStringLiteral("mailErrorDialog"));
    fixture.closeMessageBoxes();
    QTRY_VERIFY(!mail->getDialog());

    pdf::PDFAppSendMail::setLauncher([](const QString&, const QString&) { return pdf::PDFAppMailResult::Failed; });
    mail->send();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 20000);
    QVERIFY(mail->getDialog());
}

void AppDocumentFileTest::mailArguments()
{
    // xdg-email: one argument per value, no shell; hostile names stay one argument
    const QString hostile = QStringLiteral("a\"; rm -rf ~; $(x) `y`.pdf");
    const QStringList arguments = pdf::PDFAppSendMail::getXdgEmailArguments(QStringLiteral("/tmp/") + hostile, hostile);
    QCOMPARE(arguments.size(), 4);
    QCOMPARE(arguments.at(0), QStringLiteral("--subject"));
    QCOMPARE(arguments.at(1), hostile);
    QCOMPARE(arguments.at(2), QStringLiteral("--attach"));
    QVERIFY(arguments.at(3).endsWith(hostile));
}

void AppDocumentFileTest::deleteTabDuringSave()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::large(fixture.folder(), 300);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("Deleted")));
    QVERIFY(FileTestHelper::handler(tab)->save());

    // The worker writes its snapshot; nothing is delivered to the deleted tab
    delete tab;
    fixture.settle();
    QThreadPool::globalInstance()->waitForDone(30000);
    fixture.settle();
    QCOMPARE(FileTestHelper::getTitle(file), QStringLiteral("Deleted"));
}

void AppDocumentFileTest::deleteTabDuringPrint()
{
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 300));
    QVERIFY(tab);
    pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    dialog->getPrintButton()->click();
    QVERIFY(FileTestHelper::handler(tab)->getPrintController()->isPrinting());

    // The job is stopped after its page and keeps its printer and its snapshot
    delete tab;
    fixture.settle();
    QThreadPool::globalInstance()->waitForDone(60000);
    fixture.settle();
}

void AppDocumentFileTest::deleteTabWithDialogs_data()
{
    QTest::addColumn<QString>("dialog");
    QTest::addColumn<bool>("deleteWindow");
    for (const char* name : { "saveSigned", "saveError", "refreshUnsaved", "print", "printPreviewRunning", "saveFirst", "mailRunning" })
    {
        QTest::newRow(QStringLiteral("%1-tab").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << false;
        QTest::newRow(QStringLiteral("%1-window").arg(QLatin1String(name)).toLatin1().constData()) << QString::fromLatin1(name) << true;
    }
}

void AppDocumentFileTest::deleteTabWithDialogs()
{
    QFETCH(QString, dialog);
    QFETCH(bool, deleteWindow);

    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    const bool isSigned = dialog == QStringLiteral("saveSigned");
    const QString file = isSigned ? pdftest::AppDocumentSamples::signedDocument(fixture.folder())
                                  : pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Dialogs.pdf"), 3);
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    QPointer<QWidget> shown;

    if (isSigned)
    {
        QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));
        QVERIFY(handler->save());
        shown = handler->getSaver()->getDialog();
    }
    else if (dialog == QStringLiteral("saveError"))
    {
        pdf::PDFAppFileDialogs::setSaveFileHandler([&fixture](QWidget*, const QString&) { return fixture.path(QStringLiteral("no/such/x.pdf")); });
        QVERIFY(handler->saveAs());
        shown = handler->getSaver()->getDialog();
    }
    else if (dialog == QStringLiteral("refreshUnsaved"))
    {
        QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));
        QVERIFY(FileTestHelper::replaceFile(file, FileTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1))));
        handler->getAutoRefresh()->checkNow();
        QTRY_VERIFY_WITH_TIMEOUT(handler->getAutoRefresh()->getDialog(), 20000);
        shown = handler->getAutoRefresh()->getDialog();
    }
    else if (dialog == QStringLiteral("print") || dialog == QStringLiteral("printPreviewRunning"))
    {
        pdf::PDFAppPrintDialog* printDialog = FileTestHelper::openPrintDialog(tab);
        QVERIFY(printDialog);
        if (dialog == QStringLiteral("print"))
        {
            QTRY_VERIFY_WITH_TIMEOUT(!printDialog->isPreviewRendering(), 20000);
        }
        else
        {
            printDialog->getNextButton()->click();
        }
        shown = printDialog;
    }
    else if (dialog == QStringLiteral("saveFirst"))
    {
        QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));
        handler->getSendMail()->send();
        shown = handler->getSendMail()->getDialog();
    }
    else if (dialog == QStringLiteral("mailRunning"))
    {
        pdf::PDFAppSendMail::setLauncher([](const QString&, const QString&)
        {
            QThread::msleep(300);
            return pdf::PDFAppMailResult::Created;
        });
        handler->getSendMail()->send();
        QVERIFY(handler->getSendMail()->isSending());
    }

    if (dialog != QStringLiteral("mailRunning"))
    {
        QVERIFY(shown);
        QVERIFY(shown->isVisible());
    }

    if (deleteWindow)
    {
        delete fixture.window.data();
    }
    else
    {
        delete tab;
    }
    fixture.settle();
    QThreadPool::globalInstance()->waitForDone(30000);
    QTest::qWait(400);
    fixture.settle();
    QVERIFY(!shown || !shown->isVisible());
}

void AppDocumentFileTest::deleteWindowWithPrintDialog()
{
    // The dialog deleted by the application from its own signal (a click on Print)
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Own.pdf"), 2));
    QVERIFY(tab);
    QPointer<pdf::PDFAppPrintDialog> dialog = FileTestHelper::openPrintDialog(tab);
    QVERIFY(dialog);
    connect(dialog.data(), &QDialog::finished, dialog.data(), &QObject::deleteLater);
    dialog->getPrintButton()->click();
    fixture.settle();
    QVERIFY(!dialog);
    QThreadPool::globalInstance()->waitForDone(60000);
    fixture.settle();

    // The dialog deleted directly while the controller watches it
    dialog = FileTestHelper::openPrintDialog(tab);
    QTRY_VERIFY_WITH_TIMEOUT(FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()) || dialog, 60000);
    if (!dialog)
    {
        dialog = FileTestHelper::openPrintDialog(tab);
    }
    QVERIFY(dialog);
    delete dialog.data();
    fixture.settle();
    QVERIFY(!FileTestHelper::handler(tab)->getPrintController()->getDialog());
    QVERIFY(FileTestHelper::handler(tab)->isEnabled(pdf::PDFAppCommands::print()));
}

void AppDocumentFileTest::deleteTabFromOwnSignals_data()
{
    QTest::addColumn<QString>("signal");
    for (const char* name : { "saveFinished", "saveStateChanged", "fileWritten", "reloaded", "checked", "printFinished", "printStateChanged", "mailFinished" })
    {
        QTest::newRow(name) << QString::fromLatin1(name);
    }
}

void AppDocumentFileTest::deleteTabFromOwnSignals()
{
    // The application deletes the tab from a signal of package E (the code of the
    // package must not use anything of the tab after the signal)
    QFETCH(QString, signal);
    pdftest::AppDocumentFixture fixture;
    FileTestHelper::setTestPrinters(fixture.folder());
    pdf::PDFAppSendMail::setLauncher([](const QString&, const QString&) { return pdf::PDFAppMailResult::Created; });
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Own signal.pdf"), 2);
    QPointer<pdf::PDFAppDocumentTab> tab = FileTestHelper::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);
    auto deleteTab = [&tab]()
    {
        delete tab.data();
    };

    if (signal == QStringLiteral("saveFinished") || signal == QStringLiteral("saveStateChanged") || signal == QStringLiteral("fileWritten"))
    {
        QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));
        if (signal == QStringLiteral("saveFinished"))
        {
            connect(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished, fixture.window.data(), deleteTab);
        }
        else if (signal == QStringLiteral("fileWritten"))
        {
            connect(handler->getSaver(), &pdf::PDFAppDocumentSaver::fileWritten, fixture.window.data(), deleteTab);
        }
        QVERIFY(handler->save());
        if (signal == QStringLiteral("saveStateChanged"))
        {
            connect(handler->getSaver(), &pdf::PDFAppDocumentSaver::stateChanged, fixture.window.data(), deleteTab);
        }
    }
    else if (signal == QStringLiteral("reloaded") || signal == QStringLiteral("checked"))
    {
        pdf::PDFAppAutoRefresh* refresh = handler->getAutoRefresh();
        if (signal == QStringLiteral("reloaded"))
        {
            connect(refresh, &pdf::PDFAppAutoRefresh::reloaded, fixture.window.data(), deleteTab);
        }
        else
        {
            connect(refresh, &pdf::PDFAppAutoRefresh::checked, fixture.window.data(), deleteTab);
        }
        QVERIFY(FileTestHelper::replaceFile(file, FileTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("New.pdf"), 3))));
        refresh->checkNow();
    }
    else if (signal == QStringLiteral("printFinished") || signal == QStringLiteral("printStateChanged"))
    {
        pdf::PDFAppPrintDialog* dialog = FileTestHelper::openPrintDialog(tab);
        QVERIFY(dialog);
        dialog->getPrintButton()->click();
        pdf::PDFAppPrintController* controller = handler->getPrintController();
        if (signal == QStringLiteral("printFinished"))
        {
            connect(controller, &pdf::PDFAppPrintController::printFinished, fixture.window.data(), deleteTab);
        }
        else
        {
            connect(controller, &pdf::PDFAppPrintController::stateChanged, fixture.window.data(), deleteTab);
        }
    }
    else if (signal == QStringLiteral("mailFinished"))
    {
        connect(handler->getSendMail(), &pdf::PDFAppSendMail::finished, fixture.window.data(), deleteTab);
        handler->getSendMail()->send();
    }

    QTRY_VERIFY_WITH_TIMEOUT(!tab, 30000);
    QThreadPool::globalInstance()->waitForDone(30000);
    fixture.settle();
}

void AppDocumentFileTest::screenshots()
{
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);
    const QString scale = QString::number(qApp->devicePixelRatio());

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString theme = scheme == pdf::PDFThemeColors::Scheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        auto save = [&folder, &theme, &scale](QWidget* widget, const QString& name)
        {
            QVERIFY(widget);
            widget->grab().save(QDir(folder).absoluteFilePath(QStringLiteral("file-%1-%2-%3x.png").arg(name, theme, scale)));
        };

        pdftest::AppDocumentFixture fixture;
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        FileTestHelper::setTestPrinters(fixture.folder());
        const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
        pdf::PDFAppDocumentTab* tab = FileTestHelper::openReady(fixture, signedFile);
        QVERIFY(tab);
        pdf::PDFAppDocumentFileCommandHandler* handler = FileTestHelper::handler(tab);

        // Print dialog 1029:24633
        pdf::PDFAppPrintDialog* printDialog = FileTestHelper::openPrintDialog(tab);
        QVERIFY(printDialog);
        printDialog->setCopies(2);
        QTRY_VERIFY_WITH_TIMEOUT(!printDialog->getPreview()->getImage().isNull() && !printDialog->isPreviewRendering(), 20000);
        fixture.settle();
        save(printDialog, QStringLiteral("print"));
        printDialog->reject();
        fixture.settle();

        // Save signed 955:146677, save error 955:147112
        QVERIFY(FileTestHelper::modify(tab->getDocument(), QStringLiteral("x")));
        QVERIFY(handler->save());
        fixture.settle();
        save(handler->getSaver()->getDialog(), QStringLiteral("save-signed"));
        FileTestHelper::click(handler->getSaver()->getDialog(), QStringLiteral("cancelButton"));
        fixture.settle();
        pdf::PDFAppFileDialogs::setSaveFileHandler([&fixture](QWidget*, const QString&) { return fixture.path(QStringLiteral("no/such/x.pdf")); });
        QVERIFY(handler->saveAs());
        fixture.settle();
        save(handler->getSaver()->getDialog(), QStringLiteral("save-error"));
        FileTestHelper::click(handler->getSaver()->getDialog(), QStringLiteral("cancelButton"));
        fixture.settle();

        // Refresh unsaved 955:146242
        QVERIFY(FileTestHelper::replaceFile(signedFile, FileTestHelper::readFile(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("O.pdf"), 1))));
        handler->getAutoRefresh()->checkNow();
        QTRY_VERIFY_WITH_TIMEOUT(handler->getAutoRefresh()->getDialog(), 20000);
        fixture.settle();
        save(handler->getAutoRefresh()->getDialog(), QStringLiteral("refresh-unsaved"));
        FileTestHelper::click(handler->getAutoRefresh()->getDialog(), QStringLiteral("keepButton"));
        fixture.settle();

        // No printer 1029:24881, the narrow window
        pdf::PDFAppPrinters::setProvider([]() { return std::vector<pdf::PDFAppPrinterInfo>(); });
        printDialog = FileTestHelper::openPrintDialog(tab);
        QVERIFY(printDialog);
        QTRY_VERIFY_WITH_TIMEOUT(!printDialog->isPreviewRendering(), 20000);
        fixture.settle();
        save(printDialog, QStringLiteral("print-no-printer"));
        fixture.window->resize(720, 480);
        fixture.settle();
        save(printDialog, QStringLiteral("print-720x480"));
        printDialog->reject();
        fixture.window->resize(1440, 960);
        fixture.settle();
    }
}

QTEST_MAIN(AppDocumentFileTest)
#include "tst_appdocumentfiletest.moc"
