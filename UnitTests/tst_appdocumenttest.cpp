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

#include "appdocumentfixture.h"

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumentpage.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapphomebasepage.h"
#include "pdfappnavigator.h"
#include "pdfapproutetable.h"
#include "pdfappundostack.h"
#include "pdfappworkspace.h"
#include "pdfrecentrepository.h"
#include "pdfsettingdefinitions.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdflockeddocumentpicture.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include "pdfdocumentbuilder.h"

#include <QClipboard>
#include <QMimeData>
#include <QPushButton>
#include <QtTest>

#include <functional>
#include <set>

/// Helpers of the test of package A
class AppDocumentTestHelper
{
public:
    AppDocumentTestHelper() = delete;

    /// Waits until the document has left the state Loading
    static bool waitLoaded(pdf::PDFAppDocumentTab* tab, int timeout = 30000)
    {
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        QElapsedTimer timer;
        timer.start();
        while (guard && guard->getDocument() && guard->getDocument()->getState() == pdf::PDFAppDocumentState::Loading && timer.elapsed() < timeout)
        {
            QTest::qWait(5);
        }
        pdftest::AppDocumentFixture::settle();
        return guard && guard->getDocument() && guard->getDocument()->getState() != pdf::PDFAppDocumentState::Loading;
    }

    /// The recent row of the file (an empty row, if there is none)
    static pdf::PDFRecentFile recent(pdftest::AppDocumentFixture& fixture, const QString& path)
    {
        return fixture.context->getRecent().getFile(path).value_or(pdf::PDFRecentFile());
    }

    /// Changes the title of the document (one undo step)
    static bool change(pdf::PDFAppDocument* document, const QString& title)
    {
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(title);
        pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
        return document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), title, document->getVersion(), pdf::PDFAppChangeSource::User);
    }

    /// Runs \p action on the next modal dialog of the type (from the event loop of its exec)
    template<typename Dialog>
    static void onDialog(std::function<void(Dialog*)> action, int timeout = 10000)
    {
        auto check = std::make_shared<std::function<void(int)>>();
        *check = [action, check, timeout](int elapsed)
        {
            Dialog* dialog = qobject_cast<Dialog*>(QApplication::activeModalWidget());
            if (dialog && dialog->isVisible())
            {
                action(dialog);
                *check = nullptr;
                return;
            }
            if (elapsed < timeout)
            {
                QTimer::singleShot(10, [check, elapsed]() { if (*check) { (*check)(elapsed + 10); } });
            }
            else
            {
                *check = nullptr;
            }
        };
        QTimer::singleShot(0, [check]() { if (*check) { (*check)(0); } });
    }

    /// Clicks the button of the choice of the next choice dialog
    template<typename Dialog>
    static void choose(pdf::PDFAppDocumentChoice choice)
    {
        onDialog<Dialog>([choice](Dialog* dialog)
        {
            if (choice == pdf::PDFAppDocumentChoice::Cancel)
            {
                dialog->reject();
            }
            else if (QPushButton* button = dialog->getChoiceButton(choice))
            {
                button->click();
            }
        });
    }

    /// Texts of the labels of a widget
    static QStringList texts(const QWidget* widget)
    {
        QStringList result;
        for (const QLabel* label : widget->findChildren<QLabel*>())
        {
            result << label->text();
        }
        return result;
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

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

    /// Index of the tab in the tab bar
    static int tabIndex(pdf::PDFAppMainWindow* window, pdf::PDFAppTab* tab)
    {
        return window->getTabs().indexOf(tab);
    }

    /// Writes a damaged PDF (a header, nothing parsable)
    static QString damaged(const QString& folder)
    {
        const QString path = QDir(folder).filePath(QStringLiteral("damaged.pdf"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
        {
            return QString();
        }
        file.write("%PDF-1.7\n1 0 obj << /Type /Catalog >> broken\n%%EOF");
        return path;
    }
};

/// Package A of step 3 (model, lifecycle, window integration)
class AppDocumentTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void documentId();
    void commandTable();
    void openDocument();
    void openAsynchronous();
    void openAlreadyOpen();
    void openFailures();
    void openFailedDialog();
    void passwordFlow();
    void passwordClose();
    void ownerPassword();
    void pagePrecedence();
    void routeWriteBack();
    void readingState();
    void commitAndUndo();
    void undoStack();
    void undoMemoryLimit();
    void operations();
    void lockAndPopover();
    void lockStoredInRecent();
    void editCopy();
    void closeTab();
    void closeUnsaved();
    void closeUnsavedSave();
    void closeRunning();
    void quitUnsaved();
    void quitSaveAll();
    void lastSession();
    void workspacePages();
    void emptyNewTabReplaced();
    void modalDeferral();
    void forwardedRequest();
    void deletionDuringLoad();
    void deletionDuringDialogs();
    void hostileInput();
    void stateScreenGeometry();
    void themesAndDirection();
    void homePageSurvivesOpening();
    void largeDocument();
    void registry();
    void loaderReload();
    void screenshots();
};

void AppDocumentTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppDocumentTest::documentId()
{
    // 128 random bits in base32: 26 characters accepted by the route table, unique
    std::set<QString> ids;
    for (int i = 0; i < 1000; ++i)
    {
        const QString id = pdf::PDFAppDocument::createId();
        QCOMPARE(id.size(), pdf::PDFAppRouteValues::DOCUMENT_ID_LENGTH);
        QVERIFY2(pdf::PDFAppRouteValues::documentId().accepts(id), qPrintable(id));
        ids.insert(id);
    }
    QCOMPARE(ids.size(), size_t(1000));
}

void AppDocumentTest::commandTable()
{
    pdftest::AppDocumentFixture fixture;
    const pdf::PDFAppCommandRegistry* registry = fixture.context->getCommandRegistry();

    // Every command of the documents belongs to exactly one package, every command
    // of the table is registered with the scope Document or Canvas
    const QStringList tableCommands = pdf::PDFAppDocumentCommandTable::getAllCommands();
    QCOMPARE(QSet<QString>(tableCommands.cbegin(), tableCommands.cend()).size(), tableCommands.size());
    for (const QString& id : tableCommands)
    {
        const pdf::PDFAppCommand* command = registry->findCommand(id);
        QVERIFY2(command, qPrintable(id));
        QVERIFY2(command->scope != pdf::PDFAppCommandScope::Window, qPrintable(id));
    }
    for (const pdf::PDFAppCommand* command : registry->getCommands())
    {
        if (command->scope != pdf::PDFAppCommandScope::Window)
        {
            QVERIFY2(tableCommands.contains(command->id), qPrintable(command->id));
        }
        QVERIFY2(fixture.window->getCommandAction(command->id), qPrintable(command->id));
    }

    // No two default shortcuts conflict
    QVERIFY(registry->findConflicts(pdf::PDFAppCommandRegistry::UserShortcuts()).empty());

    // Without a document the commands of the documents are disabled
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::zoomIn())->isEnabled());
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::undo())->isEnabled());
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::editCopy())->isEnabled());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::newTab())->isEnabled());
}

void AppDocumentTest::openDocument()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 5);
    QVERIFY(!file.isEmpty());

    pdf::PDFAppDocumentTab* tab = fixture.open(file, 3);
    QVERIFY(tab);
    QCOMPARE(fixture.currentDocumentTab(), tab);
    QVERIFY(tab->isViewShown());
    QVERIFY(!tab->isStateScreenShown());
    QCOMPARE(tab->getTabTitle(), QStringLiteral("Report.pdf"));
    QCOMPARE(tab->getTabIconName(), QStringLiteral("file-text"));

    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(document->isReady());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(5));
    QVERIFY(!document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 0);
    QVERIFY(!document->isPasswordProtected());
    QVERIFY(!document->isCopy());
    QVERIFY(!document->getOriginalData().isEmpty());
    QCOMPARE(document->getFilePath(), QFileInfo(file).canonicalFilePath());
    QCOMPARE(fixture.context->getDocumentRegistry()->findDocument(document->getId()), document);
    QCOMPARE(fixture.context->getDocumentRegistry()->findByCanonicalPath(document->getFilePath()), document);

    // The page of the request (--page) is shown
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // The route of the tab, the window title and the open documents of the window
    const pdf::PDFAppHistoryEntry* entry = tab->getHistory().getCurrentEntry();
    QVERIFY(entry);
    QCOMPARE(entry->route.getSegments(), QStringList({ QStringLiteral("document"), document->getId() }));
    QCOMPARE(fixture.window->findDocumentTab(document->getId()), tab);
    QVERIFY(fixture.window->windowTitle().startsWith(QStringLiteral("Report.pdf")));
    const std::vector<pdf::PDFAppOpenDocument> openDocuments = fixture.window->getOpenDocuments();
    QCOMPARE(openDocuments.size(), size_t(1));
    QCOMPARE(openDocuments.front().pageCount, 5);
    QCOMPARE(openDocuments.front().page, 3);
    QCOMPARE(openDocuments.front().path, document->getFilePath());

    // The tab bar: no lock, no dot, no password icon, no progress
    pdf::PDFDocumentTabBar* tabBar = fixture.window->getTabBar();
    const int index = AppDocumentTestHelper::tabIndex(fixture.window, tab);
    QVERIFY(!tabBar->isTabLocked(index));
    QVERIFY(!tabBar->isTabUnsaved(index));
    QVERIFY(!tabBar->isTabPasswordProtected(index));
    QVERIFY(tabBar->getTabProgress(index) < 0.0);

    // The commands of the view follow the current tab
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::zoomIn())->isEnabled());
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::lockDocument())->isEnabled());
    fixture.window->activateTab(fixture.window->getHomeTab());
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::zoomIn())->isEnabled());
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppDocumentTest::openAsynchronous()
{
    // The tab is shown at once in the state Loading; the file is read by a worker
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Async.pdf"), 30);
    fixture.window->openDocuments(QStringList({ file }), 0);

    pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QCOMPARE(document->getFilePath(), QFileInfo(file).canonicalFilePath());
    QCOMPARE(tab->getTabTitle(), QStringLiteral("Async.pdf"));
    if (document->getState() == pdf::PDFAppDocumentState::Loading)
    {
        // The state screen (no toolbar) and the progress of the tab
        QVERIFY(tab->isStateScreenShown());
        QVERIFY(!tab->isViewShown());
        QVERIFY(tab->getTabProgress() >= 0.0);
        QVERIFY(tab->getLoader()->isRunning());
        QVERIFY(!tab->open(file));

        // A second request of the same file while it is read activates the tab
        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.window->openDocuments(QStringList({ file }), 0);
        QCOMPARE(fixture.currentDocumentTab(), tab);
        QCOMPARE(fixture.window->getDocumentTabs().size(), qsizetype(1));
    }

    QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    QVERIFY(guard);
    QVERIFY(document->isReady());
    QVERIFY(tab->isViewShown());
    QVERIFY(!tab->getLoader()->isRunning());
    QVERIFY(tab->getTabProgress() < 0.0);
    QCOMPARE(document->getVersion(), quint64(1));
    QCOMPARE(document->getSavedVersion(), quint64(1));

    // The reading of a file, which is not a PDF, by the reading class directly
    pdf::PDFAppDocumentReading::Request request;
    request.filePath = fixture.path(QStringLiteral("none.pdf"));
    pdf::PDFAppDocumentReading::Result result = pdf::PDFAppDocumentReading::read(request);
    QCOMPARE(result.status, pdf::PDFAppDocumentReading::Status::Failed);
    QVERIFY(!result.errorMessage.isEmpty());

    request.filePath = file;
    request.progress = std::make_shared<std::atomic<int>>(-1);
    result = pdf::PDFAppDocumentReading::read(request);
    QCOMPARE(result.status, pdf::PDFAppDocumentReading::Status::Loaded);
    QVERIFY(result.document);
    QCOMPARE(request.progress->load(), 100);
    QVERIFY(!result.isEncrypted);
}

void AppDocumentTest::openAlreadyOpen()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Twice.pdf"), 3);
    pdf::PDFAppDocumentTab* first = fixture.open(file);
    QVERIFY(first);
    const int tabCount = fixture.window->getTabs().size();

    // The tab is activated and goes to the page of the request
    fixture.window->activateTab(fixture.window->getHomeTab());
    pdf::PDFAppDocumentTab* second = fixture.open(file, 2);
    QCOMPARE(second, first);
    QCOMPARE(fixture.window->getTabs().size(), tabCount);
    QCOMPARE(fixture.currentDocumentTab(), first);
    QTRY_COMPARE(first->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));

    // Without a page the tab keeps its page
    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.open(file, 0);
    QCOMPARE(fixture.currentDocumentTab(), first);
    QCOMPARE(first->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
}

void AppDocumentTest::openFailures()
{
    // Missing, a folder, a file, which is not a PDF (by its extension and content),
    // a .pdf file with text, a damaged PDF: one dialog open-failed, no tab is left
    pdftest::AppDocumentFixture fixture;
    const QString notPdf = fixture.path(QStringLiteral("text.pdf"));
    QFile file(notPdf);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("This is not a PDF file.");
    file.close();
    const QString textFile = fixture.path(QStringLiteral("notes.txt"));
    QFile text(textFile);
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("Some notes.");
    text.close();
    const QString damaged = AppDocumentTestHelper::damaged(fixture.folder());
    QVERIFY(!damaged.isEmpty());

    const int tabCount = fixture.window->getTabs().size();
    fixture.window->openDocuments({ fixture.path(QStringLiteral("missing.pdf")), fixture.folder(), textFile, notPdf, damaged }, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.window->getTabs().size(), tabCount, 20000);
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());

    // The failures of the workers join the dialog of the failures found at once
    // (or follow it in a second dialog, when they come later)
    std::vector<pdf::PDFAppOpenFailure> failures;
    for (int i = 0; i < 2; ++i)
    {
        QTest::qWait(pdf::PDFAppMainWindow::OPEN_FAILURE_DELAY * 3);
        if (pdf::PDFAppOpenFailedDialog* dialog = fixture.window->getOpenFailedDialog())
        {
            failures.insert(failures.end(), dialog->getFailures().begin(), dialog->getFailures().end());
            fixture.closeMessageBoxes();
        }
    }
    QCOMPARE(failures.size(), size_t(5));
    QStringList reasons;
    for (const pdf::PDFAppOpenFailure& failure : failures)
    {
        QVERIFY(!failure.isOption);
        reasons << failure.reason;
    }
    QVERIFY(reasons.contains(QStringLiteral("File not found")));
    QVERIFY(reasons.contains(QStringLiteral("A folder, not a file")));
    QVERIFY(reasons.contains(QStringLiteral("Not a PDF document")));
    QVERIFY(fixture.context->getDocumentRegistry()->getDocuments().empty());

    // A password protected file is not a failure (the tab asks for the password)
    const QString password = pdftest::AppDocumentSamples::userPassword(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(password);
    QVERIFY(tab);
    QCOMPARE(tab->getDocument()->getState(), pdf::PDFAppDocumentState::PasswordRequired);
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppDocumentTest::openFailedDialog()
{
    pdftest::AppDocumentFixture fixture;

    // Titles of the combinations of files and options
    pdf::PDFAppOpenFailure file;
    file.name = QStringLiteral("C:/Users/Jakub/Documents/Reprot.pdf");
    file.reason = QStringLiteral("File not found");
    pdf::PDFAppOpenFailure option;
    option.isOption = true;
    option.name = QStringLiteral("--pgae 5");
    option.reason = QStringLiteral("Unknown option");
    QCOMPARE(pdf::PDFAppOpenFailedDialog::getTitle({ file }), QStringLiteral("1 file could not be opened"));
    QCOMPARE(pdf::PDFAppOpenFailedDialog::getTitle({ file, file }), QStringLiteral("2 files could not be opened"));
    QCOMPARE(pdf::PDFAppOpenFailedDialog::getTitle({ option }), QStringLiteral("1 option of the command line could not be used"));
    QCOMPARE(pdf::PDFAppOpenFailedDialog::getTitle({ file, file, option }), QStringLiteral("2 files and 1 option could not be used"));

    // The dialog: the list (at most 8 rows and "… and N more"), Copy Details, Close
    std::vector<pdf::PDFAppOpenFailure> failures(12, file);
    failures.push_back(option);
    fixture.window->reportOpenFailures(failures);
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    pdf::PDFAppOpenFailedDialog* dialog = fixture.window->getOpenFailedDialog();
    QCOMPARE(dialog->getFailures().size(), size_t(13));
    QCOMPARE(dialog->getList()->getItemCount(), pdf::PDFAppDocumentItemList::MAXIMUM_ROWS);
    QVERIFY(dialog->getList()->findChild<QWidget*>(QStringLiteral("moreRow")));
    for (QLabel* status : dialog->getList()->findChildren<QLabel*>(QStringLiteral("itemStatus")))
    {
        // The status lies inside of its row at the trailing side
        QVERIFY2(status->parentWidget()->rect().contains(status->geometry()), qPrintable(QStringLiteral("%1 %2 %3").arg(status->parentWidget()->width()).arg(status->x()).arg(status->parentWidget()->minimumSizeHint().width())));
    }
    QCOMPARE(dialog->width() > 0, true);

    dialog->getCopyButton()->click();
    const QString copied = QApplication::clipboard()->text();
    QVERIFY(copied.contains(QStringLiteral("--pgae 5")));
    QVERIFY(copied.contains(QStringLiteral("Unknown option")));
    QCOMPARE(copied.count(QLatin1Char('\n')), 12);

    // A failure reported while the dialog is open follows in the next dialog
    fixture.window->reportOpenFailures({ option });
    QTest::qWait(pdf::PDFAppMainWindow::OPEN_FAILURE_DELAY * 2);
    QCOMPARE(fixture.window->getOpenFailedDialog(), dialog);
    QPointer<pdf::PDFAppOpenFailedDialog> guard(dialog);
    dialog->getCloseButton()->click();
    QTRY_VERIFY(!guard);
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    QCOMPARE(fixture.window->getOpenFailedDialog()->getFailures().size(), size_t(1));
    QCOMPARE(fixture.closeMessageBoxes(), 1);

    // Hostile names: bounded texts
    pdf::PDFAppOpenFailure hostile;
    hostile.isOption = true;
    hostile.name = QString(100000, QLatin1Char('x'));
    hostile.reason = QString(100000, QLatin1Char('y'));
    fixture.window->reportOpenFailures({ hostile });
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    QVERIFY(fixture.window->getOpenFailedDialog()->width() < 2000);
    QCOMPARE(fixture.closeMessageBoxes(), 1);
}

void AppDocumentTest::passwordFlow()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::userPassword(fixture.folder());
    pdf::PDFAppDocumentTab* tab = fixture.open(file, 2);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QCOMPARE(document->getState(), pdf::PDFAppDocumentState::PasswordRequired);
    QVERIFY(!document->isPasswordWrong());
    QVERIFY(document->isPasswordProtected());

    // The password screen 228:8709 instead of the view; the tab has the password icon
    QVERIFY(tab->isStateScreenShown());
    pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
    QVERIFY(screen->getPasswordCard()->isVisible());
    QVERIFY(!screen->getLoadingPanel()->isVisible());
    QVERIFY(!screen->getErrorRow()->isVisible());
    QCOMPARE(screen->getPicture()->getState(), pdf::PDFLockedDocumentPicture::State::Locked);
    QCOMPARE(screen->getTitleLabel()->text(), QStringLiteral("This document is protected"));
    QCOMPARE(screen->getStatusBar()->getMessage(), QStringLiteral("Password required"));
    QVERIFY(screen->getPasswordField()->isPasswordField());
    QVERIFY(!screen->getOpenButton()->isEnabled());
    QVERIFY(screen->getCloseButton()->isEnabled());
    const int index = AppDocumentTestHelper::tabIndex(fixture.window, tab);
    QVERIFY(fixture.window->getTabBar()->isTabPasswordProtected(index));
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::zoomIn())->isEnabled());
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::lockDocument())->isEnabled());

    // A wrong password (Enter in the field): 228:8965
    QTest::keyClicks(screen->getPasswordField(), QStringLiteral("wrong"));
    QVERIFY(screen->getOpenButton()->isEnabled());
    QTest::keyClick(screen->getPasswordField(), Qt::Key_Return);
    QVERIFY(screen->getPasswordField()->text().isEmpty());
    QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    QCOMPARE(document->getState(), pdf::PDFAppDocumentState::PasswordRequired);
    QVERIFY(document->isPasswordWrong());
    QVERIFY(screen->getErrorRow()->isVisible());
    QVERIFY(screen->getPasswordField()->isInvalid());
    QCOMPARE(screen->getPicture()->getState(), pdf::PDFLockedDocumentPicture::State::Wrong);
    QCOMPARE(screen->getStatusBar()->getMessage(), QStringLiteral("Incorrect password"));
    QVERIFY(!screen->getOpenButton()->isEnabled());

    // A new password hides the error
    QTest::keyClicks(screen->getPasswordField(), pdftest::AppDocumentSamples::getUserPassword());
    QVERIFY(!screen->getErrorRow()->isVisible());
    QVERIFY(!screen->getPasswordField()->isInvalid());

    // The right password: the document is shown at the page of the request
    screen->getOpenButton()->click();
    QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    QVERIFY(document->isReady());
    QVERIFY(tab->isViewShown());
    QVERIFY(document->isPasswordProtected());
    QVERIFY(!document->isPasswordWrong());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(2));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(fixture.window->getTabBar()->isTabPasswordProtected(index));
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::zoomIn())->isEnabled());

    // A password is accepted only in the password state
    QVERIFY(!tab->getLoader()->setPassword(QStringLiteral("x")));
}

void AppDocumentTest::passwordClose()
{
    // Close tab of the password screen closes the tab (nothing is asked)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);
    tab->getStateScreen()->getCloseButton()->click();
    fixture.settle();
    QVERIFY(!guard);
    QVERIFY(fixture.context->getDocumentRegistry()->getDocuments().empty());

    // The tab closed while the password is being checked: the result is dropped
    tab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(tab);
    guard = tab;
    tab->getStateScreen()->getPasswordField()->setText(pdftest::AppDocumentSamples::getUserPassword());
    tab->getStateScreen()->getOpenButton()->click();
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();
    QVERIFY(!guard);
    QTest::qWait(200);
    fixture.settle();
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppDocumentTest::ownerPassword()
{
    // A file encrypted by the owner password only opens without a question; the
    // tab shows the password icon (the file is encrypted)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::ownerPassword(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isReady());
    QVERIFY(tab->getDocument()->isPasswordProtected());
    QVERIFY(tab->isTabPasswordProtected());
}

void AppDocumentTest::pagePrecedence()
{
    // --page > ?page= > the last page of Recent; a page out of range is ignored
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Pages.pdf"), 6);
    pdf::PDFAppDocumentTab* tab = fixture.open(file, 4);
    QVERIFY(tab);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();

    // The page was written into Recent at the closing: the next opening goes there
    QCOMPARE(AppDocumentTestHelper::recent(fixture, QFileInfo(file).canonicalFilePath()).lastPage, 4);
    tab = fixture.open(file, 0);
    QVERIFY(tab);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();

    // --page wins over Recent
    tab = fixture.open(file, 2);
    QVERIFY(tab);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));

    // ?page= of a route navigates the open document; out of range is ignored
    const QString id = tab->getDocumentId();
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(id, 5), pdf::PDFAppNavigationMode::Push, tab);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(4));
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(id, 600), pdf::PDFAppNavigationMode::Push, tab);
    fixture.settle();
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(4));
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();

    // A page of the request out of range: the document opens at its first page
    tab = fixture.open(file, 1000);
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isReady());
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppDocumentTest::routeWriteBack()
{
    // The page of the view is written back into the route (debounced), so Back /
    // Forward and a copied link return to it
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Route.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppDocumentPage* page = qobject_cast<pdf::PDFAppDocumentPage*>(tab->getPage());
    QVERIFY(page);

    tab->getView()->goToPage(3);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));
    QTRY_COMPARE_WITH_TIMEOUT(tab->getHistory().getCurrentEntry()->route.getQueryValue(QStringLiteral("page")), QStringLiteral("4"), 3000);
    QCOMPARE(page->getRoute().getQueryValue(QStringLiteral("page")), QStringLiteral("4"));
    QCOMPARE(page->getCurrentRoute().getQueryValue(QStringLiteral("page")), QStringLiteral("4"));

    // A route of another document is refused by the page
    QVERIFY(!page->setRoute(pdf::PDFAppRoutes::document(pdf::PDFAppDocument::createId(), 1)));
    page->updateRoute();
    QCOMPARE(page->getTitle(), QStringLiteral("Route.pdf"));
    QVERIFY(!page->hasSearchHeader());
    QCOMPARE(page->getDocumentTab(), tab);
}

void AppDocumentTest::readingState()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Reading.pdf"), 4);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    const QString path = tab->getDocument()->getFilePath();

    // The last page is Finished
    tab->getView()->goToPage(3);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));
    QTRY_VERIFY_WITH_TIMEOUT(AppDocumentTestHelper::recent(fixture, path).lastPage == 4, pdf::PDFAppDocumentTab::READING_STATE_DELAY * 4);
    const pdf::PDFRecentFile recent = AppDocumentTestHelper::recent(fixture, path);
    QVERIFY(recent.reachedEnd);
    QCOMPARE(recent.pageCount, 4);
    QVERIFY(!recent.viewState.isEmpty());

    // At the closing at once
    tab->getView()->goToPage(1);
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(fixture.window->closeTab(tab));
    QCOMPARE(AppDocumentTestHelper::recent(fixture, path).lastPage, 2);
}

void AppDocumentTest::commitAndUndo()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Edit.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const quint64 version = document->getVersion();
    const pdf::PDFDocumentPointer original = document->getDocument();
    QSignalSpy modifiedSpy(document, &pdf::PDFAppDocument::modifiedChanged);

    // A change of the current version is accepted, a change of an old one refused
    pdf::PDFDocumentBuilder builder(original.data());
    builder.setDocumentTitle(QStringLiteral("Changed"));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Title"), version, pdf::PDFAppChangeSource::User));
    QCOMPARE(document->getVersion(), version + 1);
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 1);
    QVERIFY(modifiedSpy.count() >= 1);
    QVERIFY(!document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Stale"), version, pdf::PDFAppChangeSource::User));
    QVERIFY(!document->commit(pdf::PDFModifiedDocument(pdf::PDFDocumentPointer(), nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Null"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Title"));
    QVERIFY(fixture.window->getTabBar()->isTabUnsaved(AppDocumentTestHelper::tabIndex(fixture.window, tab)));

    // Undo / redo by the commands of the window; back at the saved version the
    // document is not modified
    QAction* undo = fixture.window->getCommandAction(pdf::PDFAppCommands::undo());
    QAction* redo = fixture.window->getCommandAction(pdf::PDFAppCommands::redo());
    QVERIFY(undo->isEnabled());
    QVERIFY(!redo->isEnabled());
    undo->trigger();
    QCOMPARE(document->getDocument(), original);
    QCOMPARE(document->getVersion(), version + 2);
    QVERIFY(!document->isModified());
    QCOMPARE(document->getUndoStack()->getRedoDescription(), QStringLiteral("Title"));
    QVERIFY(!fixture.window->getTabBar()->isTabUnsaved(AppDocumentTestHelper::tabIndex(fixture.window, tab)));
    QVERIFY(redo->isEnabled());
    redo->trigger();
    QCOMPARE(document->getDocument(), changed);
    QVERIFY(document->isModified());

    // Saved (package E calls setSavedAs / setSaved): the changes are 0, one undo makes one
    document->setSaved();
    QVERIFY(!document->isModified());
    undo->trigger();
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), 1);
    redo->trigger();
    QVERIFY(!document->isModified());

    // An older version saved (a snapshot written by a worker): the stack cannot tell
    QVERIFY(AppDocumentTestHelper::change(document, QStringLiteral("Second")));
    document->setSavedAs(document->getVersion() - 1, document->getFilePath(), document->getOriginalData());
    QVERIFY(document->isModified());
    QCOMPARE(document->getUnsavedChangeCount(), -1);
    document->setSavedAs(document->getVersion() + 1, document->getFilePath(), document->getOriginalData());
    QVERIFY(document->isModified());

    // A locked document refuses changes, undo and redo
    document->setLocked(true);
    QVERIFY(!document->commit(pdf::PDFModifiedDocument(original, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Locked"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::undo())->isEnabled());
    QVERIFY(!document->getUndoStack()->undo());
    document->setLocked(false);

    // The limit of the setting editing.undoLimit is read at each commit
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::editingUndoLimit(), 2).isError());
    for (int i = 0; i < 5; ++i)
    {
        QVERIFY(AppDocumentTestHelper::change(document, QStringLiteral("Step %1").arg(i)));
    }
    QCOMPARE(document->getUndoStack()->getUndoCount(), size_t(2));
    QCOMPARE(document->getUndoStack()->getLimit(), 2);

    // Undo limit 0: no undo at all
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::editingUndoLimit(), 0).isError());
    QVERIFY(AppDocumentTestHelper::change(document, QStringLiteral("No undo")));
    QCOMPARE(document->getUndoStack()->getUndoCount(), size_t(0));
    QVERIFY(!undo->isEnabled());
}

void AppDocumentTest::undoStack()
{
    // The undo stack without a window: limits, saved position, refusals
    pdf::PDFAppDocument document(nullptr, nullptr);
    pdf::PDFAppUndoStack* stack = document.getUndoStack();
    QVERIFY(!stack->undo());
    QVERIFY(!stack->redo());
    QVERIFY(stack->getUndoDescription().isEmpty());
    QVERIFY(stack->getRedoDescription().isEmpty());

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    pdf::PDFDocumentPointer first(new pdf::PDFDocument(builder.build()));
    QVERIFY(!document.commit(pdf::PDFModifiedDocument(first, nullptr), QString(), 0, pdf::PDFAppChangeSource::User));
    document.setLoaded(first, QByteArray("%PDF"), QStringLiteral("C:/missing/first.pdf"), false);
    QVERIFY(document.isReady());
    QCOMPARE(document.getTitle(), QStringLiteral("first.pdf"));

    // A not ready document (setLoaded with null) fails
    pdf::PDFAppDocument failed(nullptr, nullptr);
    failed.setLoaded(pdf::PDFDocumentPointer(), QByteArray(), QStringLiteral("x.pdf"), false);
    QCOMPARE(failed.getState(), pdf::PDFAppDocumentState::Failed);
    QVERIFY(!failed.getErrorMessage().isEmpty());
    QCOMPARE(failed.getUnsavedChangeCount(), 0);

    for (int i = 0; i < 4; ++i)
    {
        QVERIFY(AppDocumentTestHelper::change(&document, QStringLiteral("Change %1").arg(i)));
    }
    QCOMPARE(stack->getUndoCount(), size_t(4));
    QCOMPARE(document.getUnsavedChangeCount(), 4);
    QVERIFY(stack->getMemoryUsage() > 0);

    // Saved in the middle, a new change after an undo makes the saved version unreachable
    stack->undo();
    stack->undo();
    document.setSaved();
    QCOMPARE(document.getUnsavedChangeCount(), 0);
    stack->undo();
    QCOMPARE(document.getUnsavedChangeCount(), 1);
    stack->redo();
    stack->redo();
    QCOMPARE(document.getUnsavedChangeCount(), 1);
    stack->undo();
    stack->undo();
    QVERIFY(AppDocumentTestHelper::change(&document, QStringLiteral("Branch")));
    QCOMPARE(document.getUnsavedChangeCount(), -1);
    QVERIFY(!stack->canRedo());

    // The limit drops the oldest steps; the saved position moves with them
    document.setSaved();
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(AppDocumentTestHelper::change(&document, QStringLiteral("More %1").arg(i)));
    }
    stack->setLimit(2);
    QCOMPARE(stack->getUndoCount(), size_t(2));
    QCOMPARE(document.getUnsavedChangeCount(), -1);
    stack->setLimit(-5);
    QCOMPARE(stack->getLimit(), 0);
    QCOMPARE(stack->getUndoCount(), size_t(0));

    // Clear (a reload): the current version is the saved one
    stack->setLimit(pdf::PDFAppUndoStack::DEFAULT_LIMIT);
    stack->clear();
    QVERIFY(stack->isAtSavedPosition());
    QCOMPARE(stack->getMemoryUsage(), qint64(0));

    // A blocking operation refuses undo, commit and redo
    QVERIFY(AppDocumentTestHelper::change(&document, QStringLiteral("Before")));
    pdf::PDFAppDocumentOperation* operation = document.beginOperation(pdf::PDFAppDocumentOperationKind::Save, QStringLiteral("Save"), true);
    QVERIFY(!stack->undo());
    QVERIFY(!AppDocumentTestHelper::change(&document, QStringLiteral("Blocked")));
    operation->finish();
    QVERIFY(stack->undo());
    QVERIFY(stack->redo());

    // Memory estimate: a document shares nothing with null, everything with itself
    QVERIFY(pdf::PDFAppUndoStack::estimateMemory(document.getDocument().data(), nullptr) > 0);
    QCOMPARE(pdf::PDFAppUndoStack::estimateMemory(document.getDocument().data(), document.getDocument().data()), qint64(0));
    QCOMPARE(pdf::PDFAppUndoStack::estimateMemory(nullptr, document.getDocument().data()), qint64(0));
}

void AppDocumentTest::undoMemoryLimit()
{
    // Large changes are dropped by the memory limit (the last undo step stays)
    pdf::PDFAppDocument document(nullptr, nullptr);
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    document.setLoaded(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())), QByteArray(), QStringLiteral("memory.pdf"), false);

    pdf::PDFAppUndoStack* stack = document.getUndoStack();
    stack->setMemoryLimit(64 * 1024);
    for (int i = 0; i < 6; ++i)
    {
        // Each change adds a stream of 40 kB (a replaced object of the next change)
        pdf::PDFDocumentBuilder changeBuilder(document.getDocument().data());
        changeBuilder.setDocumentTitle(QString(20000, QChar(QLatin1Char('a' + i))));
        pdf::PDFDocumentPointer changed(new pdf::PDFDocument(changeBuilder.build()));
        QVERIFY(document.commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("Big %1").arg(i), document.getVersion(), pdf::PDFAppChangeSource::User));
    }
    QVERIFY(stack->getUndoCount() >= 1);
    QVERIFY(stack->getUndoCount() < 6);
    QVERIFY(stack->getMemoryUsage() <= stack->getMemoryLimit() || stack->getUndoCount() == 1);

    // Redo steps are dropped first
    stack->setMemoryLimit(pdf::PDFAppUndoStack::DEFAULT_MEMORY_LIMIT);
    while (stack->canUndo())
    {
        QVERIFY(stack->undo());
    }
    QVERIFY(stack->canRedo());
    stack->setMemoryLimit(0);
    QVERIFY(!stack->canRedo());
    stack->setMemoryLimit(-1);
    QCOMPARE(stack->getMemoryLimit(), qint64(0));
}

void AppDocumentTest::operations()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Operations.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QSignalSpy spy(document, &pdf::PDFAppDocument::operationsChanged);

    pdf::PDFAppDocumentOperation* print = document->beginOperation(pdf::PDFAppDocumentOperationKind::Print, QStringLiteral("Print"), false);
    pdf::PDFAppDocumentOperation* index = document->beginOperation(pdf::PDFAppDocumentOperationKind::Index, QStringLiteral("Index"), false);
    QCOMPARE(document->getOperations().size(), size_t(2));
    QVERIFY(print->isAskedBeforeClose());
    QVERIFY(!index->isAskedBeforeClose());
    QVERIFY(!document->isBlockedByOperation());

    // The progress of an operation, which is asked about, is the progress of the tab
    print->setProgress(40);
    QCOMPARE(print->getProgress(), 40);
    QCOMPARE(tab->getTabProgress(), 0.4);
    QTRY_COMPARE(fixture.window->getTabBar()->getTabProgress(AppDocumentTestHelper::tabIndex(fixture.window, tab)), 0.4);
    print->setProgress(1000);
    QCOMPARE(print->getProgress(), 100);

    QSignalSpy cancelSpy(index, &pdf::PDFAppDocumentOperation::cancelRequested);
    index->requestCancel();
    index->requestCancel();
    QCOMPARE(cancelSpy.count(), 1);
    QVERIFY(index->isCancelRequested());
    index->finish();
    index->finish();
    print->finish();
    QVERIFY(document->getOperations().empty());
    QVERIFY(spy.count() >= 4);
    fixture.settle();
    QVERIFY(tab->getTabProgress() < 0.0);
}

void AppDocumentTest::lockAndPopover()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Lock.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentTabBar* tabBar = fixture.window->getTabBar();
    const int index = AppDocumentTestHelper::tabIndex(fixture.window, tab);

    // Lock document (File panel): the amber lock in the tab, the editing commands off
    QAction* lock = fixture.window->getCommandAction(pdf::PDFAppCommands::lockDocument());
    QVERIFY(lock->isEnabled());
    QVERIFY(!lock->isChecked());
    QVERIFY(AppDocumentTestHelper::change(document, QStringLiteral("Before lock")));
    lock->trigger();
    QVERIFY(document->isLocked());
    QVERIFY(lock->isChecked());
    QVERIFY(tabBar->isTabLocked(index));
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::undo())->isEnabled());

    // A click on the lock of the tab opens the popover 266:12435
    const QRect lockRect = tabBar->getTabLockRect(index);
    QVERIFY(!lockRect.isEmpty());
    QCOMPARE(tabBar->getLockAt(lockRect.center()), index);
    QSignalSpy lockSpy(tabBar, &pdf::PDFDocumentTabBar::lockClicked);
    QTest::mouseClick(tabBar, Qt::LeftButton, Qt::NoModifier, lockRect.center());
    QCOMPARE(lockSpy.count(), 1);
    QTRY_VERIFY(tab->getLockPopover() && tab->getLockPopover()->isVisible());
    pdf::PDFAppLockPopover* popover = tab->getLockPopover();
    QVERIFY(AppDocumentTestHelper::texts(popover).contains(QStringLiteral("Document is locked")));

    // The panel lies below the tab at the lock
    const QPoint lockGlobal = tabBar->mapToGlobal(lockRect.center());
    QVERIFY(popover->geometry().contains(QPoint(lockGlobal.x(), popover->geometry().center().y())));
    QVERIFY(popover->mapToGlobal(QPoint(0, popover->height())).y() > lockGlobal.y());

    // Unlock
    QPointer<pdf::PDFAppLockPopover> popoverGuard(popover);
    popover->getUnlockButton()->click();
    fixture.settle();
    QVERIFY(!popoverGuard);
    QVERIFY(!document->isLocked());
    QVERIFY(!tabBar->isTabLocked(index));
    QVERIFY(fixture.window->getCommandAction(pdf::PDFAppCommands::undo())->isEnabled());

    // An unlocked tab has no popover, a press beside the lock is no click on it
    tab->showLockPopover(QRect(0, 0, 10, 10));
    QVERIFY(!tab->getLockPopover());
    document->setLocked(true);
    QTest::mousePress(tabBar, Qt::LeftButton, Qt::NoModifier, lockRect.center());
    QTest::mouseRelease(tabBar, Qt::LeftButton, Qt::NoModifier, lockRect.center() + QPoint(60, 0));
    QCOMPARE(lockSpy.count(), 1);
    QCOMPARE(tabBar->getLockAt(QPoint(-10, -10)), -1);
    QVERIFY(tabBar->getTabLockRect(-1).isEmpty());

    // Esc closes the popover (it is deleted)
    QTest::mouseClick(tabBar, Qt::LeftButton, Qt::NoModifier, tabBar->getTabLockRect(index).center());
    QTRY_VERIFY(tab->getLockPopover() && tab->getLockPopover()->isVisible());
    popoverGuard = tab->getLockPopover();
    QTest::keyClick(popoverGuard.data(), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!popoverGuard);

    // The popover is closed, when the document is unlocked by the command
    QTest::mouseClick(tabBar, Qt::LeftButton, Qt::NoModifier, tabBar->getTabLockRect(index).center());
    QTRY_VERIFY(tab->getLockPopover());
    popoverGuard = tab->getLockPopover();
    lock->trigger();
    fixture.settle();
    QVERIFY(!popoverGuard);
    QVERIFY(!document->isLocked());
}

void AppDocumentTest::lockStoredInRecent()
{
    // The lock applies also at the next opening (recent_file.locked)
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Stored.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    tab->getDocument()->setLocked(true);
    QVERIFY(AppDocumentTestHelper::recent(fixture, tab->getDocument()->getFilePath()).locked);
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();

    tab = fixture.open(file);
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isLocked());
    QVERIFY(tab->isTabLocked());
    tab->getDocument()->setLocked(false);
    QVERIFY(!AppDocumentTestHelper::recent(fixture, tab->getDocument()->getFilePath()).locked);
}

void AppDocumentTest::editCopy()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Original.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocument* original = tab->getDocument();
    original->setLocked(true);

    // Edit a Copy of the popover: a new tab after the original, no file, modified
    QTest::mouseClick(fixture.window->getTabBar(), Qt::LeftButton, Qt::NoModifier, fixture.window->getTabBar()->getTabLockRect(AppDocumentTestHelper::tabIndex(fixture.window, tab)).center());
    QTRY_VERIFY(tab->getLockPopover());
    const int tabCount = fixture.window->getTabs().size();
    tab->getLockPopover()->getEditCopyButton()->click();
    fixture.settle();
    QCOMPARE(fixture.window->getTabs().size(), tabCount + 1);
    pdf::PDFAppDocumentTab* copyTab = fixture.currentDocumentTab();
    QVERIFY(copyTab);
    QVERIFY(copyTab != tab);
    QCOMPARE(AppDocumentTestHelper::tabIndex(fixture.window, copyTab), AppDocumentTestHelper::tabIndex(fixture.window, tab) + 1);
    pdf::PDFAppDocument* copy = copyTab->getDocument();
    QVERIFY(copy->isReady());
    QVERIFY(copy->isCopy());
    QVERIFY(copy->getFilePath().isEmpty());
    QCOMPARE(copy->getTitle(), QStringLiteral("Copy of Original.pdf"));
    QCOMPARE(copyTab->getTabTitle(), QStringLiteral("Copy of Original.pdf"));
    QVERIFY(copy->isModified());
    QCOMPARE(copy->getUnsavedChangeCount(), -1);
    QVERIFY(!copy->isLocked());
    QCOMPARE(copy->getDocument(), original->getDocument());
    QCOMPARE(copy->getOriginalData(), original->getOriginalData());
    QVERIFY(copyTab->isViewShown());
    QVERIFY(original->isLocked());

    // The copy can be changed; undo back to its start keeps it modified (no file)
    QVERIFY(AppDocumentTestHelper::change(copy, QStringLiteral("Copy change")));
    QVERIFY(copy->getUndoStack()->undo());
    QVERIFY(copy->isModified());

    // The command Edit a Copy of the window, a copy of a copy
    fixture.window->getCommandAction(pdf::PDFAppCommands::editCopy())->trigger();
    fixture.settle();
    QCOMPARE(fixture.currentDocumentTab()->getDocument()->getTitle(), QStringLiteral("Copy of Copy of Original.pdf"));
    QCOMPARE(fixture.window->getOpenDocuments().back().path, QString());

    // A saved copy (package E: setSavedAs) gets its file and title
    pdf::PDFAppDocument* second = fixture.currentDocumentTab()->getDocument();
    const QString target = fixture.path(QStringLiteral("Saved copy.pdf"));
    QFile targetFile(target);
    QVERIFY(targetFile.open(QIODevice::WriteOnly));
    targetFile.write(second->getOriginalData());
    targetFile.close();
    QSignalSpy fileSpy(second, &pdf::PDFAppDocument::fileChanged);
    second->setSavedAs(second->getVersion(), target, second->getOriginalData());
    QCOMPARE(fileSpy.count(), 1);
    QVERIFY(!second->isCopy());
    QVERIFY(!second->isModified());
    QCOMPARE(second->getTitle(), QStringLiteral("Saved copy.pdf"));
    QTRY_COMPARE(fixture.window->getTabBar()->tabText(fixture.window->getTabBar()->currentIndex()), QStringLiteral("Saved copy.pdf"));

    // Refusals: a copy of a document, which is not shown, or into a shown document
    QVERIFY(!fixture.window->openDocumentCopy(nullptr));
    QVERIFY(!copyTab->openCopyOf(original));
    pdf::PDFAppDocument loading(nullptr, nullptr);
    QVERIFY(!fixture.window->openDocumentCopy(&loading));
    QVERIFY(!loading.setCopyOf(&loading));
}

void AppDocumentTest::closeTab()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Close.pdf"), 1));
    QVERIFY(tab);
    const QString id = tab->getDocumentId();
    QPointer<pdf::PDFAppDocumentTab> guard(tab);

    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();
    QVERIFY(!guard);
    QVERIFY(!fixture.context->getDocumentRegistry()->findDocument(id));
    QVERIFY(!fixture.window->findDocumentTab(id));

    // The route of a closed document is Page Not Found
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::document(id));
    fixture.settle();
    QVERIFY(!qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab()));
}

void AppDocumentTest::closeUnsaved()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Unsaved.pdf"), 1));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);
    QVERIFY(AppDocumentTestHelper::change(tab->getDocument(), QStringLiteral("A")));
    QVERIFY(AppDocumentTestHelper::change(tab->getDocument(), QStringLiteral("B")));

    // Cancel (Esc) keeps the tab; the texts of 955:144943
    QStringList texts;
    AppDocumentTestHelper::onDialog<pdf::PDFAppCloseUnsavedDialog>([&texts](pdf::PDFAppCloseUnsavedDialog* dialog)
    {
        texts = AppDocumentTestHelper::texts(dialog);
        QCOMPARE(dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Discard)->text(), QString::fromUtf8("Don\u2019t Save"));
        QVERIFY(dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Save));
        QVERIFY(!dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Stop));
        QTest::keyClick(dialog, Qt::Key_Escape);
    });
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(guard);
    QVERIFY(texts.contains(QStringLiteral("Save the changes to Unsaved.pdf?")));
    QVERIFY(texts.contains(QStringLiteral("The document has 2 unsaved changes. If you close the tab without saving, the changes are lost.")));

    // Cancel by its button
    AppDocumentTestHelper::choose<pdf::PDFAppCloseUnsavedDialog>(pdf::PDFAppDocumentChoice::Cancel);
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(guard);

    // Don't Save closes the tab
    AppDocumentTestHelper::choose<pdf::PDFAppCloseUnsavedDialog>(pdf::PDFAppDocumentChoice::Discard);
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();
    QVERIFY(!guard);

    // The texts of a copy and of a document with an unknown count
    pdf::PDFAppDocument document(nullptr, nullptr);
    QCOMPARE(pdf::PDFAppCloseUnsavedDialog::getChangesText(nullptr), QString());
    QCOMPARE(pdf::PDFAppCloseUnsavedDialog::getChangesText(&document), QStringLiteral("Unsaved changes"));
    QVERIFY(!pdf::PDFAppCloseUnsavedDialog::getText(nullptr).isEmpty());
}

void AppDocumentTest::closeUnsavedSave()
{
    // Save of the dialog: package E writes the file, the tab closes afterwards
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SaveAndClose.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);
    QVERIFY(AppDocumentTestHelper::change(tab->getDocument(), QStringLiteral("Saved title")));
    QSignalSpy saveSpy(tab, &pdf::PDFAppDocumentTab::saveFinished);

    AppDocumentTestHelper::choose<pdf::PDFAppCloseUnsavedDialog>(pdf::PDFAppDocumentChoice::Save);
    QVERIFY(!fixture.window->closeTab(tab));
    QTRY_VERIFY_WITH_TIMEOUT(!guard, 20000);
    QCOMPARE(saveSpy.count(), 1);
    QCOMPARE(saveSpy.front().front().toBool(), true);

    // The file has the change
    pdf::PDFAppDocumentTab* reopened = fixture.open(file);
    QVERIFY(reopened);
    QCOMPARE(reopened->getDocument()->getDocument()->getInfo()->title, QStringLiteral("Saved title"));
    QCOMPARE(fixture.closeMessageBoxes(), 0);

    // A copy without a file: Save opens Save As; a canceled file dialog keeps the tab
    fixture.window->openDocumentCopy(reopened->getDocument());
    pdf::PDFAppDocumentTab* copyTab = fixture.currentDocumentTab();
    QVERIFY(copyTab && copyTab->getDocument()->isCopy());
    QPointer<pdf::PDFAppDocumentTab> copyGuard(copyTab);
    pdf::PDFAppFileDialogs::setSaveFileHandler([](QWidget*, const QString&) { return QString(); });
    AppDocumentTestHelper::choose<pdf::PDFAppCloseUnsavedDialog>(pdf::PDFAppDocumentChoice::Save);
    QVERIFY(!fixture.window->closeTab(copyTab));
    fixture.settle();
    QTest::qWait(100);
    QVERIFY(copyGuard);
    QVERIFY(!copyTab->isClosingAfterSave());
}

void AppDocumentTest::closeRunning()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Running.pdf"), 1));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QPointer<pdf::PDFAppDocumentOperation> print = document->beginOperation(pdf::PDFAppDocumentOperationKind::Print, QStringLiteral("Printing"), false);
    QPointer<pdf::PDFAppDocumentOperation> index = document->beginOperation(pdf::PDFAppDocumentOperationKind::Index, QStringLiteral("Index"), false);
    print->setProgress(25);

    // The title and the text name the operation (955:145813)
    QCOMPARE(pdf::PDFAppCloseRunningDialog::getTitle(document), QStringLiteral("Stop Printing and close the tab?"));
    QVERIFY(pdf::PDFAppCloseRunningDialog::getText(document).contains(QStringLiteral("25 %")));
    QCOMPARE(pdf::PDFAppCloseRunningDialog::getTitle(nullptr), QStringLiteral("Stop the operation and close the tab?"));

    // Keep Running keeps the tab and the operations
    AppDocumentTestHelper::choose<pdf::PDFAppCloseRunningDialog>(pdf::PDFAppDocumentChoice::Keep);
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(guard);
    QVERIFY(!print->isCancelRequested());

    // Stop and Close requests the stop of every operation and closes the tab
    AppDocumentTestHelper::choose<pdf::PDFAppCloseRunningDialog>(pdf::PDFAppDocumentChoice::Stop);
    QVERIFY(print->isCancelRequested() == false);
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();
    QVERIFY(!guard);

    // A tab with background operations only closes without a question
    tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Background.pdf"), 1));
    QVERIFY(tab);
    guard = tab;
    QPointer<pdf::PDFAppDocumentOperation> background = tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Signatures, QStringLiteral("Signatures"), false);
    QPointer<pdf::PDFAppDocumentOperation> textIndex = tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Index, QStringLiteral("Index"), false);
    textIndex->setProgress(50);
    QCOMPARE(tab->getTabProgress(), 0.5);
    QSignalSpy cancelSpy(background.data(), &pdf::PDFAppDocumentOperation::cancelRequested);
    QSignalSpy indexCancelSpy(textIndex.data(), &pdf::PDFAppDocumentOperation::cancelRequested);
    QVERIFY(fixture.window->closeTab(tab));
    QCOMPARE(cancelSpy.count(), 1);
    QCOMPARE(indexCancelSpy.count(), 1);
    fixture.settle();
    QVERIFY(!guard);
    Q_UNUSED(index);
}

void AppDocumentTest::quitUnsaved()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* first = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    pdf::PDFAppDocumentTab* second = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 2));
    pdf::PDFAppDocumentTab* clean = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Clean.pdf"), 2));
    QVERIFY(first && second && clean);
    QVERIFY(AppDocumentTestHelper::change(first->getDocument(), QStringLiteral("X")));
    QVERIFY(AppDocumentTestHelper::change(second->getDocument(), QStringLiteral("Y")));

    // Cancel keeps the window; the list names the two documents
    int rows = 0;
    QStringList texts;
    AppDocumentTestHelper::onDialog<pdf::PDFAppQuitUnsavedDialog>([&rows, &texts](pdf::PDFAppQuitUnsavedDialog* dialog)
    {
        rows = dialog->getList()->getItemCount();
        texts = AppDocumentTestHelper::texts(dialog);
        dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Cancel)->click();
    });
    QVERIFY(!fixture.window->close());
    QVERIFY(fixture.window->isVisible());
    QCOMPARE(rows, 2);
    QVERIFY(texts.contains(QStringLiteral("Save the changes before quitting?")));
    QVERIFY(texts.contains(QStringLiteral("2 documents have unsaved changes. If you quit without saving, the changes are lost.")));
    QVERIFY(texts.contains(QStringLiteral("1 unsaved change")));

    // Quit Without Saving closes the window
    AppDocumentTestHelper::choose<pdf::PDFAppQuitUnsavedDialog>(pdf::PDFAppDocumentChoice::Discard);
    QVERIFY(fixture.window->close());
    QVERIFY(!fixture.window->isVisible());
}

void AppDocumentTest::quitSaveAll()
{
    pdftest::AppDocumentFixture fixture;
    const QString firstFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("First.pdf"), 1);
    const QString secondFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second.pdf"), 1);
    pdf::PDFAppDocumentTab* first = fixture.open(firstFile);
    pdf::PDFAppDocumentTab* second = fixture.open(secondFile);
    QVERIFY(first && second);
    QVERIFY(AppDocumentTestHelper::change(first->getDocument(), QStringLiteral("First saved")));
    QVERIFY(AppDocumentTestHelper::change(second->getDocument(), QStringLiteral("Second saved")));

    // Save All saves both and closes the window
    AppDocumentTestHelper::choose<pdf::PDFAppQuitUnsavedDialog>(pdf::PDFAppDocumentChoice::Save);
    QVERIFY(!fixture.window->close());
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.window->isVisible(), 20000);
    QVERIFY(!first->getDocument()->isModified());
    QVERIFY(!second->getDocument()->isModified());

    // A failed save keeps the window: a copy, whose Save As is canceled
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window.data()));
    pdf::PDFAppDocumentTab* copy = fixture.window->openDocumentCopy(first->getDocument());
    QVERIFY(copy);
    pdf::PDFAppFileDialogs::setSaveFileHandler([](QWidget*, const QString&) { return QString(); });
    AppDocumentTestHelper::choose<pdf::PDFAppQuitUnsavedDialog>(pdf::PDFAppDocumentChoice::Save);
    QVERIFY(!fixture.window->close());
    QTest::qWait(200);
    fixture.settle();
    QVERIFY(fixture.window->isVisible());
    QVERIFY(copy->getDocument()->isModified());
}

void AppDocumentTest::lastSession()
{
    // The documents of a closed window are the last session (with their pages)
    pdftest::AppDocumentFixture fixture;
    const QString firstFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SessionA.pdf"), 4);
    const QString secondFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("SessionB.pdf"), 4);
    QVERIFY(fixture.open(firstFile, 3));
    QVERIFY(fixture.open(secondFile, 2));
    QTRY_COMPARE(fixture.currentDocumentTab()->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(fixture.window->close());

    std::optional<pdf::PDFWorkspace> session = fixture.context->getWorkspaces().getLastSession();
    QVERIFY(session);
    QCOMPARE(session->documents.size(), size_t(2));
    QCOMPARE(session->documents[0].page, 3);
    QCOMPARE(session->documents[1].page, 2);

    // Restore last session of the Home page opens them at their pages
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window.data()));
    for (pdf::PDFAppDocumentTab* tab : fixture.window->getDocumentTabs())
    {
        QVERIFY(fixture.window->closeTab(tab));
    }
    fixture.settle();
    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::open());
    fixture.settle();
    pdf::PDFAppHomeBasePage* page = qobject_cast<pdf::PDFAppHomeBasePage*>(fixture.window->getHomeTab()->getPage());
    QVERIFY(page);
    page->restoreLastSession();
    QTRY_COMPARE(fixture.window->getDocumentTabs().size(), qsizetype(2));
    for (pdf::PDFAppDocumentTab* tab : fixture.window->getDocumentTabs())
    {
        QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    }
    QTRY_COMPARE(fixture.window->getDocumentTabs()[0]->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));
    QTRY_COMPARE(fixture.window->getDocumentTabs()[1]->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
}

void AppDocumentTest::workspacePages()
{
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Workspace.pdf"), 5);
    const QString canonical = QFileInfo(file).canonicalFilePath();
    const std::vector<pdf::PDFAppOpenRequest> requests = pdf::PDFAppDocumentActions::getRequests({ { canonical, 4 }, { canonical, 2 }, { QStringLiteral("relative.pdf"), 1 } });
    QCOMPARE(requests.size(), size_t(1));
    QCOMPARE(requests.front().page, 4);
    QVERIFY(!pdf::PDFAppDocumentActions::openRequests(nullptr, requests));
    QVERIFY(!pdf::PDFAppDocumentActions::openRequests(fixture.window->getHomeTab(), {}));
    QVERIFY(pdf::PDFAppDocumentActions::openRequests(fixture.window->getHomeTab(), requests));
    QTRY_VERIFY(fixture.currentDocumentTab());
    QVERIFY(AppDocumentTestHelper::waitLoaded(fixture.currentDocumentTab()));
    QTRY_COMPARE(fixture.currentDocumentTab()->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));
}

void AppDocumentTest::emptyNewTabReplaced()
{
    // A document opened from an empty New tab takes its place
    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppTab> newTab = fixture.window->openNewTab();
    QVERIFY(newTab);
    const int index = AppDocumentTestHelper::tabIndex(fixture.window, newTab);
    const int tabCount = fixture.window->getTabs().size();

    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Replace.pdf"), 1));
    QVERIFY(tab);
    fixture.settle();
    QVERIFY(!newTab);
    QCOMPARE(fixture.window->getTabs().size(), tabCount);
    QCOMPARE(AppDocumentTestHelper::tabIndex(fixture.window, tab), index);

    // From any other tab the document gets a new tab after the current one
    fixture.window->activateTab(fixture.window->getHomeTab());
    QVERIFY(fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second.pdf"), 1)));
    QCOMPARE(fixture.window->getTabs().size(), tabCount + 1);
}

void AppDocumentTest::modalDeferral()
{
    // While a modal dialog is open, the files wait and open, when it closes
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deferred.pdf"), 1);
    QPointer<pdf::PDFMessageBox> box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Modal"), QString(), QDialogButtonBox::Ok, fixture.window);
    box->setWindowModality(Qt::ApplicationModal);
    box->show();
    QTRY_VERIFY(QApplication::activeModalWidget());

    fixture.window->openDocuments(QStringList({ file }), 0);
    QCOMPARE(fixture.window->getPendingRequestCount(), size_t(1));
    QVERIFY(fixture.window->getDocumentTabs().isEmpty());

    box->close();
    QTRY_COMPARE(fixture.window->getDocumentTabs().size(), qsizetype(1));
    QCOMPARE(fixture.window->getPendingRequestCount(), size_t(0));
    delete box.data();

    // A modal dialog deleted while the requests wait
    box = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Modal"), QString(), QDialogButtonBox::Ok, fixture.window);
    box->setWindowModality(Qt::ApplicationModal);
    box->show();
    QTRY_VERIFY(QApplication::activeModalWidget());
    fixture.window->openDocuments(QStringList({ pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Deferred2.pdf"), 1) }), 0);
    QCOMPARE(fixture.window->getPendingRequestCount(), size_t(1));
    delete box.data();
    QTRY_COMPARE(fixture.window->getDocumentTabs().size(), qsizetype(2));
}

void AppDocumentTest::forwardedRequest()
{
    // A request of another process: the errors join the dialog open-failed, the
    // files open at the page of the request
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Forwarded.pdf"), 6);
    pdf::PDFAppInstanceRequest request;
    request.files = QStringList({ file, fixture.path(QStringLiteral("gone.pdf")) });
    request.page = 5;
    request.errors = QStringList({ QStringLiteral("Unknown argument --pgae.") });
    fixture.window->handleOpenRequest(request);

    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    const std::vector<pdf::PDFAppOpenFailure> failures = fixture.window->getOpenFailedDialog()->getFailures();
    QCOMPARE(failures.size(), size_t(2));
    QCOMPARE(failures[0].isOption, true);
    QCOMPARE(failures[0].name, QStringLiteral("Unknown argument --pgae."));
    QCOMPARE(failures[1].isOption, false);
    QCOMPARE(fixture.window->getForwardedErrors(), request.errors);
    QVERIFY(AppDocumentTestHelper::texts(fixture.window->getOpenFailedDialog()).contains(QStringLiteral("Forwarded.pdf is open; these items were skipped:")));
    QCOMPARE(fixture.closeMessageBoxes(), 1);

    pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
    QVERIFY(tab);
    QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    QTRY_COMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(4));
}

void AppDocumentTest::deletionDuringLoad()
{
    const QString sampleFolder = QDir::temp().filePath(QStringLiteral("pdf4qt-appdocument-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(sampleFolder);
    const QString file = pdftest::AppDocumentSamples::simple(sampleFolder, QStringLiteral("Deleted.pdf"), 200);

    // The tab closed while the file is read: the worker finishes alone
    {
        pdftest::AppDocumentFixture fixture;
        fixture.window->openDocuments(QStringList({ file }), 0);
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.currentDocumentTab();
        QVERIFY(tab);
        QVERIFY(fixture.window->closeTab(tab));
        fixture.settle();
        QVERIFY(!tab);
        QTest::qWait(300);
        fixture.settle();
        QCOMPARE(fixture.closeMessageBoxes(), 0);
    }

    // The window deleted while the file is read
    {
        pdftest::AppDocumentFixture fixture;
        fixture.window->openDocuments(QStringList({ file }), 0);
        delete fixture.window.data();
        fixture.settle();
        QTest::qWait(300);
        fixture.settle();
    }

    // The document deleted by the application while it is read (its tab stays empty)
    {
        pdftest::AppDocumentFixture fixture;
        fixture.window->openDocuments(QStringList({ file }), 0);
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.currentDocumentTab();
        QVERIFY(tab);
        delete tab->getDocument();
        QTest::qWait(300);
        fixture.settle();
        QVERIFY(tab);
        QVERIFY(!tab->getDocument());
        QVERIFY(fixture.window->closeTab(tab));
        fixture.settle();
    }

    // The tab deleted from its own signal loadFailed (the window closes it)
    {
        pdftest::AppDocumentFixture fixture;
        const QString damaged = AppDocumentTestHelper::damaged(fixture.folder());
        fixture.window->openDocuments(QStringList({ damaged }), 0);
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.currentDocumentTab();
        QVERIFY(tab);
        QTRY_VERIFY(!tab);
        QTRY_VERIFY(fixture.window->getOpenFailedDialog());
        fixture.closeMessageBoxes();
    }
    QDir(sampleFolder).removeRecursively();
}

void AppDocumentTest::deletionDuringDialogs()
{
    // The tab deleted while its question is open: the question returns Cancel
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Dialog.pdf"), 1));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        QVERIFY(AppDocumentTestHelper::change(tab->getDocument(), QStringLiteral("Z")));
        AppDocumentTestHelper::onDialog<pdf::PDFAppCloseUnsavedDialog>([guard](pdf::PDFAppCloseUnsavedDialog*) { delete guard.data(); });
        QVERIFY(!fixture.window->closeTab(tab));
        fixture.settle();
        QVERIFY(!guard);
    }

    // The window deleted while the quit question is open
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Quit.pdf"), 1));
        QVERIFY(tab);
        QVERIFY(AppDocumentTestHelper::change(tab->getDocument(), QStringLiteral("Q")));
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        AppDocumentTestHelper::onDialog<pdf::PDFAppQuitUnsavedDialog>([window](pdf::PDFAppQuitUnsavedDialog*) { delete window.data(); });
        fixture.window->close();
        fixture.settle();
        QVERIFY(!window);
    }

    // The tab deleted while the close-running question is open
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Running.pdf"), 1));
        QVERIFY(tab);
        tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Mcp, QStringLiteral("Change"), true);
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        AppDocumentTestHelper::onDialog<pdf::PDFAppCloseRunningDialog>([guard](pdf::PDFAppCloseRunningDialog*) { delete guard.data(); });
        QVERIFY(!fixture.window->closeTab(tab));
        fixture.settle();
        QVERIFY(!guard);
    }

    // The window deleted while the dialog open-failed and the lock popover are shown
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Popover.pdf"), 1));
        QVERIFY(tab);
        tab->getDocument()->setLocked(true);
        fixture.window->reportOpenFailures({ pdf::PDFAppOpenFailure() });
        QTRY_VERIFY(fixture.window->getOpenFailedDialog());
        tab->showLockPopover(QRect(fixture.window->mapToGlobal(QPoint(100, 10)), QSize(12, 28)));
        QVERIFY(tab->getLockPopover());
        delete fixture.window.data();
        fixture.settle();
    }

    // The tab deleted from the slot of a button of its lock popover
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Unlock.pdf"), 1));
        QVERIFY(tab);
        tab->getDocument()->setLocked(true);
        tab->showLockPopover(QRect(fixture.window->mapToGlobal(QPoint(100, 10)), QSize(12, 28)));
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        QObject::connect(tab->getLockPopover(), &pdf::PDFAppLockPopover::unlockRequested, tab, [guard]() { guard->deleteLater(); });
        tab->getLockPopover()->getUnlockButton()->click();
        fixture.settle();
        QVERIFY(!guard);
    }
}

void AppDocumentTest::hostileInput()
{
    pdftest::AppDocumentFixture fixture;

    // A password longer than the limit is wrong (never a crash)
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(tab->getLoader()->setPassword(QString(100000, QLatin1Char('p'))));
    QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    QVERIFY(tab->getDocument()->isPasswordWrong());

    // An empty file, a file of garbage with the PDF signature
    const QString empty = fixture.path(QStringLiteral("empty.pdf"));
    QFile emptyFile(empty);
    QVERIFY(emptyFile.open(QIODevice::WriteOnly));
    emptyFile.close();
    const QString garbage = fixture.path(QStringLiteral("garbage.pdf"));
    QFile garbageFile(garbage);
    QVERIFY(garbageFile.open(QIODevice::WriteOnly));
    QByteArray bytes("%PDF-1.7\n");
    QRandomGenerator generator(42);
    for (int i = 0; i < 100000; ++i)
    {
        bytes.append(char(generator.bounded(256)));
    }
    garbageFile.write(bytes);
    garbageFile.close();
    const int tabCount = fixture.window->getTabs().size();
    fixture.window->openDocuments(QStringList({ empty, garbage }), -5);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.window->getOpenFailedDialog(), 20000);
    QTRY_COMPARE(fixture.window->getTabs().size(), tabCount);
    fixture.closeMessageBoxes();

    // A view state of a hostile database row is parsed safely
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Hostile.pdf"), 2);
    QVERIFY(!fixture.context->getRecent().recordOpened(file).isError());
    QVERIFY(!fixture.context->getRecent().setViewState(QFileInfo(file).canonicalFilePath(), QStringLiteral("{\"zoom\": 1e308, \"rotation\": \"x\", \"panelId\": \"../../\"}")).isError());
    fixture.context->getRecent().setReadingState(QFileInfo(file).canonicalFilePath(), 99999, 2, false);
    pdf::PDFAppDocumentTab* hostile = fixture.open(file);
    QVERIFY(hostile);
    QVERIFY(hostile->getDocument()->isReady());

    // Too many requests are bounded
    std::vector<pdf::PDFAppOpenRequest> requests(500);
    for (pdf::PDFAppOpenRequest& request : requests)
    {
        request.filePath = fixture.path(QStringLiteral("x.pdf"));
    }
    fixture.window->openDocuments(requests);
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    QVERIFY(fixture.window->getOpenFailedDialog()->getFailures().size() <= size_t(pdf::PDFAppInstanceProtocol::MAXIMUM_FILES));
    fixture.closeMessageBoxes();
}

void AppDocumentTest::stateScreenGeometry()
{
    // locked-body of 228:8709: the card 440 x 426 centered, the field 358 x 40, the
    // buttons of the same width 36 high; 228:8965 (wrong) is 449 high
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
    fixture.settle();

    QWidget* card = screen->getPasswordCard();
    QVERIFY(screen->getSubtitleLabel()->text().contains(QStringLiteral(">user-password.pdf</span>.")));
    QCOMPARE(screen->getSubtitleLabel()->height(), 16);
    QCOMPARE(card->width(), 440);
    QCOMPARE(card->height(), 426);
    QCOMPARE(screen->getPasswordField()->width(), 358);
    QCOMPARE(screen->getPasswordField()->height(), 40);
    QCOMPARE(screen->getPicture()->size(), QSize(160, 160));
    QCOMPARE(screen->getOpenButton()->width(), screen->getCloseButton()->width());
    QCOMPARE(screen->getOpenButton()->height(), 36);
    QCOMPARE(screen->getStatusBar()->height(), 28);
    const QRect cardRect(card->mapTo(screen, QPoint(0, 0)), card->size());
    QVERIFY(qAbs(cardRect.center().x() - screen->width() / 2) <= 1);

    tab->getLoader()->setPassword(QStringLiteral("wrong"));
    QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    QCOMPARE(card->height(), 449);

    // The minimal window: the card scrolls, nothing overlaps the status bar
    fixture.window->resize(720, 480);
    fixture.settle();
    QVERIFY(screen->getStatusBar()->isVisible());
    QCOMPARE(screen->getStatusBar()->mapTo(screen, QPoint(0, 0)).y(), screen->height() - 28);
}

void AppDocumentTest::themesAndDirection()
{
    // Both themes and right-to-left: no leak color on the password screen
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(tab);
    for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
    {
        QApplication::setLayoutDirection(direction);
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            fixture.settle();
            const QImage image = AppDocumentTestHelper::render(fixture.window);
            QCOMPARE(AppDocumentTestHelper::countLeakPixels(image), 0);
        }
    }

    // The card is centered in both directions, the texts are mirrored
    pdf::PDFAppDocumentStateScreen* screen = tab->getStateScreen();
    QCOMPARE(screen->layoutDirection(), Qt::RightToLeft);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(screen, &languageChange);
    QCOMPARE(screen->getTitleLabel()->text(), QStringLiteral("This document is protected"));
}

void AppDocumentTest::homePageSurvivesOpening()
{
    // A document opens in its own tab: the page of the Home tab, from which it was
    // opened (Home, Open, Recent, a drop, a workspace), stays the same instance
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();
    int fileIndex = 0;
    auto nextFile = [&fixture, &fileIndex]()
    {
        return pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Survive%1.pdf").arg(++fileIndex), 2);
    };

    for (const pdf::PDFAppRoute& route : { pdf::PDFAppRoutes::home(), pdf::PDFAppRoutes::open(), pdf::PDFAppRoutes::recent() })
    {
        fixture.window->activateTab(homeTab);
        fixture.window->getNavigator()->navigate(route);
        fixture.settle();
        QPointer<pdf::PDFAppPage> page = homeTab->getPage();
        QVERIFY(page);

        QVERIFY(pdf::PDFAppDocumentActions::openFiles(page, { nextFile() }));
        fixture.settle();
        QVERIFY2(page, qPrintable(route.toString()));
        QCOMPARE(homeTab->getPage(), page.data());
        QVERIFY(fixture.currentDocumentTab());
    }

    // A drop onto the window
    fixture.window->activateTab(homeTab);
    QPointer<pdf::PDFAppPage> page = homeTab->getPage();
    QMimeData data;
    data.setUrls({ QUrl::fromLocalFile(nextFile()) });
    const QPoint position(fixture.window->width() / 2, fixture.window->height() / 2);
    QDragEnterEvent enter(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(position), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window, &drop);
    fixture.settle();
    QVERIFY(page);
    QCOMPARE(homeTab->getPage(), page.data());

    // A workspace opened by the action of the page
    qint64 workspaceId = 0;
    QVERIFY(!fixture.context->getWorkspaces().saveWorkspace(QStringLiteral("Survive"), { { nextFile(), 2 }, { nextFile(), 1 } }, false, &workspaceId).isError());
    fixture.window->activateTab(homeTab);
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::recent());
    fixture.settle();
    QPointer<pdf::PDFAppHomeBasePage> homePage = qobject_cast<pdf::PDFAppHomeBasePage*>(homeTab->getPage());
    QVERIFY(homePage);
    pdf::PDFAppHomeItem item;
    item.kind = pdf::PDFAppHomeItemKind::Workspace;
    item.id = workspaceId;
    QVERIFY(homePage->performAction(item, pdf::PDFAppHomeAction::Open));
    fixture.settle();
    QVERIFY(homePage);
    QCOMPARE(homeTab->getPage(), homePage.data());
    QCOMPARE(fixture.window->getDocumentTabs().size(), qsizetype(fileIndex));
    for (pdf::PDFAppDocumentTab* tab : fixture.window->getDocumentTabs())
    {
        QVERIFY(AppDocumentTestHelper::waitLoaded(tab));
    }
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppDocumentTest::largeDocument()
{
    // 2000 pages (D11): the opening is fast enough and reports its progress
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::large(fixture.folder(), 2000);
    QVERIFY(!file.isEmpty());
    QElapsedTimer timer;
    timer.start();
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isReady());
    QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(2000));
    qInfo("Opening of 2000 pages: %lld ms", qlonglong(timer.elapsed()));
    QVERIFY(timer.elapsed() < 60000);
}

void AppDocumentTest::registry()
{
    // The registry of the context: ids, canonical paths, signals, destroyed documents
    pdf::PDFAppDocumentRegistry registry;
    QSignalSpy addedSpy(&registry, &pdf::PDFAppDocumentRegistry::documentAdded);
    QSignalSpy removedSpy(&registry, &pdf::PDFAppDocumentRegistry::documentRemoved);
    QSignalSpy changedSpy(&registry, &pdf::PDFAppDocumentRegistry::documentChanged);
    QVERIFY(!registry.findDocument(QString()));
    QVERIFY(!registry.findByCanonicalPath(QString()));
    registry.addDocument(nullptr);
    registry.removeDocument(nullptr);
    QCOMPARE(addedSpy.count(), 0);

    pdf::PDFAppDocument* first = new pdf::PDFAppDocument(nullptr, nullptr);
    pdf::PDFAppDocument* second = new pdf::PDFAppDocument(nullptr, nullptr);
    registry.addDocument(first);
    registry.addDocument(first);
    registry.addDocument(second);
    QCOMPARE(addedSpy.count(), 2);
    QCOMPARE(registry.getDocuments().size(), size_t(2));
    QCOMPARE(registry.findDocument(second->getId()), second);
    QVERIFY(!registry.findDocument(QStringLiteral("unknown")));

    first->setLoading(QStringLiteral("C:/Files/Report.pdf"));
    QCOMPARE(registry.findByCanonicalPath(QStringLiteral("C:/Files/Report.pdf")), first);
    QVERIFY(!registry.findByCanonicalPath(QStringLiteral("C:/Files/Other.pdf")));
    first->setLocked(true);
    QCOMPARE(changedSpy.count(), 1);

    // A removed document is not announced any more, a destroyed one leaves the registry
    registry.removeDocument(first);
    QCOMPARE(removedSpy.count(), 1);
    first->setLocked(false);
    QCOMPARE(changedSpy.count(), 1);
    delete first;
    const QString secondId = second->getId();
    delete second;
    QCOMPARE(removedSpy.count(), 2);
    QCOMPARE(removedSpy.back().front().toString(), secondId);
    QVERIFY(registry.getDocuments().empty());

    // The registry destroyed before its documents
    pdf::PDFAppDocument third(nullptr, nullptr);
    {
        pdf::PDFAppDocumentRegistry shortLived;
        shortLived.addDocument(&third);
    }
    third.setLocked(true);
}

void AppDocumentTest::loaderReload()
{
    // A reload (automatic refresh) replaces the document, keeps the view, clears the undo
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Reload.pdf"), 2);
    pdf::PDFAppDocumentTab* tab = fixture.open(file);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(AppDocumentTestHelper::change(document, QStringLiteral("Changed")));
    int changes = 0;
    bool isPreserveView = false;
    QObject receiver;
    QObject::connect(document, &pdf::PDFAppDocument::documentChanged, &receiver, [&changes, &isPreserveView](const pdf::PDFModifiedDocument& modified)
    {
        ++changes;
        isPreserveView = modified.hasPreserveView();
    });

    QVERIFY(QFile::remove(file));
    QVERIFY(!pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Reload.pdf"), 4).isEmpty());
    QVERIFY(tab->getLoader()->reload());
    QVERIFY(!tab->getLoader()->reload());
    QTRY_VERIFY(!tab->getLoader()->isRunning());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(4));
    QVERIFY(!document->isModified());
    QVERIFY(!document->getUndoStack()->canUndo());
    QVERIFY(document->isReady());
    QVERIFY(changes >= 1);
    QVERIFY(isPreserveView);

    // A damaged file keeps the shown document
    QFile damaged(file);
    QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("%PDF-1.7 damaged");
    damaged.close();
    QVERIFY(tab->getLoader()->reload());
    QTRY_VERIFY(!tab->getLoader()->isRunning());
    QVERIFY(document->isReady());
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(4));

    // A password protected file is read again with the accepted password
    pdf::PDFAppDocumentTab* protectedTab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(protectedTab);
    QVERIFY(protectedTab->getLoader()->setPassword(pdftest::AppDocumentSamples::getUserPassword()));
    QVERIFY(AppDocumentTestHelper::waitLoaded(protectedTab));
    QVERIFY(protectedTab->getDocument()->isReady());
    QVERIFY(protectedTab->getLoader()->reload());
    QTRY_VERIFY(!protectedTab->getLoader()->isRunning());
    QVERIFY(protectedTab->getDocument()->isReady());

    // A cancel drops a running reading
    QVERIFY(protectedTab->getLoader()->reload());
    protectedTab->getLoader()->cancel();
    QVERIFY(!protectedTab->getLoader()->isRunning());
    QTest::qWait(100);
    QVERIFY(protectedTab->getDocument()->isReady());

    // A document without a file cannot be reloaded
    pdf::PDFAppDocumentTab* copy = fixture.window->openDocumentCopy(document);
    QVERIFY(copy);
    QVERIFY(!copy->getLoader()->reload());
    QVERIFY(!copy->getLoader()->setPassword(QStringLiteral("x")));
}

void AppDocumentTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the password screens (228:8709, 228:8965),
    // the lock popover (266:12010), the dialogs close-unsaved (955:144943),
    // quit-unsaved (955:145378), close-running (955:145813), open-failed (1028:24297)
    // in both themes at 1x and 2x, also 720 x 480 and right-to-left
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* passwordTab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(passwordTab);

    auto shoot = [&fixture, &folder](const QString& name)
    {
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            pdftest::AppDocumentFixture::settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QImage image = AppDocumentTestHelper::render(fixture.window, ratio);
                QPainter painter(&image);
                for (QWidget* widget : QApplication::topLevelWidgets())
                {
                    if (widget != fixture.window && widget->isVisible() && (qobject_cast<QMenu*>(widget) || qobject_cast<QDialog*>(widget)))
                    {
                        const QPoint position = fixture.window->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0)));
                        widget->render(&painter, position, QRegion(), QWidget::DrawChildren);
                    }
                }
                painter.end();
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString file = QStringLiteral("%1-%2-%3x.png").arg(name, scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(ratio));
                    QVERIFY(image.save(QDir(folder).filePath(file)));
                }
                QCOMPARE(AppDocumentTestHelper::countLeakPixels(image), 0);
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    };

    // The offscreen screen is smaller than the window: the card is centered over the body as on a real screen
    auto center = [&fixture](pdf::PDFMessageBox* box)
    {
        QWidget* host = fixture.window->getDialogHost();
        const QRect card = box->rect().marginsRemoved(box->getShadowMargins());
        QRect target(QPoint(0, 0), card.size());
        target.moveCenter(QRect(host->mapToGlobal(QPoint(0, 0)), host->size()).center());
        box->move(target.topLeft() - card.topLeft());
    };

    fixture.window->resize(1440, 960);
    shoot(QStringLiteral("document-password"));
    passwordTab->getStateScreen()->getPasswordField()->setText(QStringLiteral("Report-2025!"));
    shoot(QStringLiteral("document-password-entered"));
    passwordTab->getLoader()->setPassword(QStringLiteral("wrong"));
    QVERIFY(AppDocumentTestHelper::waitLoaded(passwordTab));
    shoot(QStringLiteral("document-password-wrong"));
    fixture.window->resize(720, 480);
    shoot(QStringLiteral("document-password-minimal"));
    QApplication::setLayoutDirection(Qt::RightToLeft);
    fixture.window->resize(1440, 960);
    shoot(QStringLiteral("document-password-rtl"));
    QApplication::setLayoutDirection(Qt::LeftToRight);

    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
    QVERIFY(tab);
    tab->getDocument()->setLocked(true);
    fixture.settle();
    pdf::PDFDocumentTabBar* tabBar = fixture.window->getTabBar();
    QTest::mouseClick(tabBar, Qt::LeftButton, Qt::NoModifier, tabBar->getTabLockRect(AppDocumentTestHelper::tabIndex(fixture.window, tab)).center());
    QTRY_VERIFY(tab->getLockPopover());
    shoot(QStringLiteral("document-locked"));
    tab->getLockPopover()->close();
    fixture.settle();
    tab->getDocument()->setLocked(false);

    QVERIFY(AppDocumentTestHelper::change(tab->getDocument(), QStringLiteral("Changed")));
    QPointer<pdf::PDFAppCloseUnsavedDialog> unsaved = new pdf::PDFAppCloseUnsavedDialog(tab->getDocument(), tab);
    unsaved->open();
    fixture.settle();
    center(unsaved);
    shoot(QStringLiteral("document-close-unsaved"));
    delete unsaved.data();

    QPointer<pdf::PDFAppDocumentOperation> operation = tab->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Print, QStringLiteral("Printing"), false);
    operation->setProgress(26);
    QPointer<pdf::PDFAppCloseRunningDialog> running = new pdf::PDFAppCloseRunningDialog(tab->getDocument(), tab);
    running->open();
    fixture.settle();
    center(running);
    shoot(QStringLiteral("document-close-running"));
    delete running.data();
    operation->finish();

    pdf::PDFAppDocumentTab* invoice = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 2));
    QVERIFY(AppDocumentTestHelper::change(invoice->getDocument(), QStringLiteral("Changed")));
    QPointer<pdf::PDFAppQuitUnsavedDialog> quit = new pdf::PDFAppQuitUnsavedDialog({ tab->getDocument(), invoice->getDocument() }, fixture.window);
    quit->open();
    fixture.settle();
    center(quit);
    shoot(QStringLiteral("quit-unsaved"));
    delete quit.data();

    pdf::PDFAppOpenFailure missing;
    missing.name = QStringLiteral("C:/Users/Jakub/Documents/Reprot.pdf");
    missing.reason = QStringLiteral("File not found");
    pdf::PDFAppOpenFailure text;
    text.name = QStringLiteral("C:/Users/Jakub/Documents/notes.txt");
    text.reason = QStringLiteral("Not a PDF document");
    pdf::PDFAppOpenFailure option;
    option.isOption = true;
    option.name = QStringLiteral("--pgae 5");
    option.reason = QString::fromUtf8("Unknown option \u2013 did you mean --page?");
    QPointer<pdf::PDFAppOpenFailedDialog> failed = new pdf::PDFAppOpenFailedDialog({ missing, text, option }, QStringLiteral("Project_Report.pdf"), fixture.window);
    failed->open();
    fixture.settle();
    center(failed);
    shoot(QStringLiteral("document-open-failed"));
    delete failed.data();
}

QTEST_MAIN(AppDocumentTest)
#include "tst_appdocumenttest.moc"
