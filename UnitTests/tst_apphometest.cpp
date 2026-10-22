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

// Pages Home, Open and Recent of the new application (step 2 of phase 1):
// routes and their validation (the filter, the sort and the search of Recent in
// the route), data of a database filled by the test (files, folders, pinned
// files and locations, workspaces, the last session, tools), changes of another
// connection arriving by the notifier, thumbnails arriving after the page is
// shown and after it is deleted, the existence of the files, the menus of the
// items by the mouse and the keyboard, the dialogs (save / rename / delete a
// workspace, rename a location, clear the history with and without the pinned
// items), the drop of files onto the window, the native file dialogs (replaced
// by handlers), the New tab variant of Open, the empty state, geometry and
// pixels against the Figma frames in both themes, right-to-left, the minimal
// window, deletion of the page, the tab and the window with an open menu, an
// open dialog or a pending thumbnail, and hostile rows of the database.
// PDF4QT_APP_SCREENSHOTS=<folder> writes pictures of the pages.

#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappfiledrop.h"
#include "pdfappfolderinfo.h"
#include "pdfapphomebasepage.h"
#include "pdfapphomedialogs.h"
#include "pdfapphomepage.h"
#include "pdfapphomewidgets.h"
#include "pdfappinfowidgets.h"
#include "pdfappinstance.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfappopenpage.h"
#include "pdfapppagenotfound.h"
#include "pdfapprecentpage.h"
#include "pdfapproute.h"
#include "pdfapptab.h"
#include "pdfappthumbnailer.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfrecentrepository.h"
#include "pdftoolrepository.h"
#include "pdfworkspacerepository.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessible>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSemaphore>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QFileInfo>
#include <QTimeZone>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <memory>

using PDFMoreButtonPointer = QPointer<pdf::PDFMoreButton>;

/// Fixture: a window of a portable memory database (unique name per process and
/// instance), files and folders in a temporary directory, a fixed current time
class HomeFixture
{
public:
    explicit HomeFixture(const QStringList& arguments = QStringList({ QStringLiteral("--portable") }), QSize size = QSize(1440, 1303))
    {
        pdf::PDFAppHomeFormat::setCurrentTimeOverride(getNow());

        QStringList commandLineArguments = { QStringLiteral("PDF4QT") };
        commandLineArguments << arguments;
        context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(commandLineArguments, QDir::currentPath()));

        pdf::PDFDatabaseOptions options;
        options.memoryName = QStringLiteral("/pdf4qt-home-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
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

    ~HomeFixture()
    {
        delete window.data();
        settle();
        context.reset();
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        QApplication::setLayoutDirection(Qt::LeftToRight);
        pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
        pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
        pdf::PDFAppDocumentActions::setOpenDocumentsProvider(pdf::PDFAppDocumentActions::OpenDocumentsProvider());
        pdf::PDFAppHomeFormat::setCurrentTimeOverride(0);
    }

    /// The fixed current time of the tests (Sep 20, 2026 11:30, local time)
    static qint64 getNow()
    {
        return QDateTime(QDate(2026, 9, 20), QTime(11, 30), QTimeZone::LocalTime).toMSecsSinceEpoch();
    }

    /// Time of today / of the day before (local)
    static qint64 at(int daysBefore, int hour, int minute)
    {
        return QDateTime(QDate(2026, 9, 20).addDays(-daysBefore), QTime(hour, minute), QTimeZone::LocalTime).toMSecsSinceEpoch();
    }

    pdf::PDFAppNavigator* navigator() const { return window ? window->getNavigator() : nullptr; }
    pdf::PDFAppTab* currentTab() const { return window ? window->getCurrentTab() : nullptr; }
    pdf::PDFAppPage* currentPage() const { return currentTab() ? currentTab()->getPage() : nullptr; }
    pdf::PDFAppWorkspaceTab* homeTab() const { return window ? window->getHomeTab() : nullptr; }

    template<typename Page>
    Page* page() const
    {
        return qobject_cast<Page*>(currentPage());
    }

    void navigate(const QString& text)
    {
        navigator()->navigate(pdf::PDFAppRoute::parse(text), pdf::PDFAppNavigationMode::Push);
        settle();
    }

    void hideBanner()
    {
        homeTab()->getBanner()->hide();
        settle();
    }

    /// Lets the deleted widgets go, the notifications and the layouts settle
    static void settle(int rounds = 4)
    {
        for (int i = 0; i < rounds; ++i)
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
    }

    /// Waits for the folder information and the thumbnailer, then reads the
    /// changes of the other connections (the thumbnailer, a thread of the test)
    /// by an explicit poll of the notifier - not by its timer, which may come
    /// late on a loaded machine - and lets the pages refresh
    void waitIdle(int milliseconds = 150)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 30000)
        {
            QTest::qWait(qMin(milliseconds, 50));
            const bool thumbnailsDone = !context->getThumbnailer() || context->getThumbnailer()->isIdle();
            const bool infoDone = !context->getFolderInfo() || context->getFolderInfo()->getPendingCount() == 0;
            if (thumbnailsDone && infoDone && timer.elapsed() >= milliseconds)
            {
                break;
            }
        }

        // The thumbnailer has written its result before it is idle (one lock)
        for (int i = 0; i < 2; ++i)
        {
            if (context && context->getNotifier())
            {
                context->getNotifier()->poll();
            }
            settle();
        }
    }

    /// Closes the message boxes of the window (files, which could not be opened)
    /// and the document tabs, which the opening of files has added (step 3: an
    /// opened file is shown in its own tab, the tests of the pages go on with the
    /// page of the tab, from which the file was opened). \returns the number of the
    /// closed message boxes.
    int closeMessageBoxes()
    {
        int count = 0;
        if (!window)
        {
            return 0;
        }
        for (pdf::PDFMessageBox* box : window->findChildren<pdf::PDFMessageBox*>())
        {
            if (box->isVisible() && !qobject_cast<pdf::PDFAppCardDialog*>(box))
            {
                box->close();
                ++count;
            }
        }
        settle();
        closeDocumentTabs();
        return count;
    }

    /// Closes the document tabs of the window. \returns their number.
    int closeDocumentTabs()
    {
        int count = 0;
        if (!window)
        {
            return 0;
        }

        // The tab, from which the files were opened: the nearest tab, which is not a
        // document, left of the current document tab (a new document is inserted
        // after the current tab)
        QPointer<pdf::PDFAppTab> sourceTab;
        const QList<pdf::PDFAppTab*> tabs = window->getTabs();
        for (qsizetype i = tabs.indexOf(window->getCurrentTab()); i >= 0 && qobject_cast<pdf::PDFAppDocumentTab*>(window->getCurrentTab()); --i)
        {
            if (!qobject_cast<pdf::PDFAppDocumentTab*>(tabs[i]))
            {
                sourceTab = tabs[i];
                break;
            }
        }

        for (pdf::PDFAppDocumentTab* tab : window->getDocumentTabs())
        {
            if (window->closeTab(tab))
            {
                ++count;
            }
        }
        if (sourceTab)
        {
            window->activateTab(sourceTab);
        }
        settle();
        return count;
    }

    pdf::PDFRecentRepository recent() const { return context->getRecent(); }
    pdf::PDFWorkspaceRepository workspaces() const { return context->getWorkspaces(); }

    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;
    QTemporaryDir directory;

    static int s_counter;
};

int HomeFixture::s_counter = 0;

/// Sample data of the frames (Figma 28:4, 61:1201, 80:910)
class HomeSample
{
public:
    QString projectReport;
    QString invoice;
    QString technical;
    QString buildingPlan;
    QString presentation;
    QString workProjects;
    qint64 thesisId = 0;
    qint64 invoicesId = 0;
    qint64 contractsId = 0;
    qint64 locationId = 0;
};

/// Helpers of the test
class HomeTestHelper
{
public:
    HomeTestHelper() = delete;

    /// Creates a PDF of the pages (page 1 has a red rectangle)
    static bool createPdf(const QString& filePath, int pageCount)
    {
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
            if (i == 0)
            {
                pdf::PDFPageContentStreamBuilder content(&builder);
                QPainter* painter = content.begin(page);
                if (!painter)
                {
                    return false;
                }
                painter->fillRect(QRectF(0, 0, 150, 200), Qt::red);
                content.end(painter);
            }
        }
        pdf::PDFDocument document = builder.build();
        return bool(pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false));
    }

    /// Records a file opened at the time
    static QString record(HomeFixture& fixture, const QString& filePath, qint64 time, int lastPage = 0, int pageCount = 0, bool reachedEnd = false, bool pinned = false)
    {
        pdf::PDFRecentRepository repository = fixture.recent();
        repository.setCurrentTime(time);
        QString stored;
        if (repository.recordOpened(filePath, &stored).isError())
        {
            return QString();
        }
        if (lastPage > 0 || reachedEnd)
        {
            repository.setReadingState(stored, qMax(1, lastPage), pageCount, reachedEnd);
        }
        if (pinned)
        {
            repository.setPinned(stored, true);
        }
        return stored;
    }

    /// Files, folders, workspaces, a location and tools of the frames
    static HomeSample fill(HomeFixture& fixture, bool withPdfs = true)
    {
        HomeSample sample;
        const QDir root(fixture.directory.path());
        auto makeFile = [&](const QString& relative, int pages)
        {
            const QString path = root.filePath(relative);
            if (withPdfs)
            {
                createPdf(path, pages);
            }
            else
            {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write("%PDF-1.7\n");
                }
            }
            return path;
        };

        sample.presentation = record(fixture, makeFile(QStringLiteral("Documents/Presentation.pdf"), 36), HomeFixture::at(5, 14, 55), 36, 36, true);
        sample.buildingPlan = record(fixture, makeFile(QStringLiteral("Desktop/Building_Plan.pdf"), 12), HomeFixture::at(2, 16, 3), 3, 12, false, true);
        sample.technical = record(fixture, makeFile(QStringLiteral("Projects/PDF4QT/Docs/Technical_Documentation.pdf"), 128), HomeFixture::at(1, 16, 3), 41, 128, false, true);
        sample.invoice = record(fixture, makeFile(QStringLiteral("Downloads/Invoice_2026.pdf"), 3), HomeFixture::at(0, 8, 17), 3, 3, true);
        sample.projectReport = record(fixture, makeFile(QStringLiteral("Documents/Project_Report.pdf"), 24), HomeFixture::at(0, 10, 24), 12, 24, false, true);

        sample.workProjects = root.filePath(QStringLiteral("Projects"));
        pdf::PDFRecentRepository repository = fixture.recent();
        repository.setCurrentTime(HomeFixture::at(8, 9, 0));
        repository.recordFolder(sample.workProjects);
        repository.addPinnedLocation(sample.workProjects, QStringLiteral("Work Projects"), &sample.locationId);

        pdf::PDFWorkspaceRepository workspaces = fixture.workspaces();
        const std::vector<pdf::PDFWorkspaceDocument> documents = { { sample.projectReport, 12 }, { sample.invoice, 1 }, { sample.technical, 41 }, { sample.presentation, 2 } };
        workspaces.setCurrentTime(HomeFixture::at(10, 9, 0));
        workspaces.saveWorkspace(QStringLiteral("Client contracts"), { documents[0], documents[1] }, false, &sample.contractsId);
        workspaces.saveWorkspace(QStringLiteral("Invoices 2026"), { documents[1], documents[2], documents[3] }, false, &sample.invoicesId);
        workspaces.saveWorkspace(QStringLiteral("Thesis review"), documents, false, &sample.thesisId);
        workspaces.setCurrentTime(HomeFixture::at(10, 9, 30));
        workspaces.markOpened(sample.contractsId);
        workspaces.setCurrentTime(HomeFixture::at(4, 9, 30));
        workspaces.markOpened(sample.invoicesId);
        workspaces.setCurrentTime(HomeFixture::at(1, 16, 15));
        workspaces.markOpened(sample.thesisId);
        workspaces.setCurrentTime(HomeFixture::at(0, 10, 24));
        workspaces.saveLastSession({ documents[0], documents[1], documents[2] });

        pdf::PDFToolRepository tools(fixture.context->getDatabase());
        const std::pair<const char*, qint64> usage[] = { { "convert.pdf-to-images", HomeFixture::at(5, 10, 0) }, { "convert.ocr", HomeFixture::at(2, 11, 0) },
                                                         { "pages.manage", HomeFixture::at(1, 16, 15) }, { "protect.sign", HomeFixture::at(0, 9, 40) },
                                                         { "optimize.compress", HomeFixture::at(0, 11, 2) } };
        for (const auto& tool : usage)
        {
            tools.setCurrentTime(tool.second);
            tools.recordUsage(QString::fromLatin1(tool.first));
        }

        fixture.waitIdle();
        return sample;
    }

    static QColor token(pdf::PDFThemeColors::Color color)
    {
        return pdf::PDFStylePainter::color(color);
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground) : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    static QColor pixel(const QImage& image, const QPoint& point)
    {
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(qFloor(point.x() * ratio), qFloor(point.y() * ratio));
    }

    static bool isClose(const QColor& first, const QColor& second, int tolerance = 3)
    {
        return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance && qAbs(first.blue() - second.blue()) <= tolerance;
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

    static QRect rectIn(const QWidget* widget, const QWidget* ancestor)
    {
        return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
    }

    static QStringList menuTexts(const QMenu* menu)
    {
        QStringList texts;
        for (const QAction* action : menu->actions())
        {
            texts << (action->isSeparator() ? QStringLiteral("|") : action->text());
        }
        return texts;
    }

    static QAction* findAction(const QMenu* menu, const QString& text)
    {
        for (QAction* action : menu->actions())
        {
            if (action->text() == text)
            {
                return action;
            }
        }
        return nullptr;
    }

    /// Visible popup menu of the application (nullptr = none)
    static QMenu* visibleMenu()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (QMenu* menu = qobject_cast<QMenu*>(widget); menu && menu->isVisible())
            {
                return menu;
            }
        }
        return nullptr;
    }

    /// Runs a write of another connection (a thread with its own connection)
    static void runInThread(const std::function<void()>& function)
    {
        QThread* thread = QThread::create(function);
        thread->start();
        thread->wait();
        delete thread;
    }

    /// PNG of a thumbnail of the color
    static QByteArray createPng(const QColor& color, QSize size = QSize(60, 80))
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return data;
    }

    static QMimeData* createMimeData(const QList<QUrl>& urls)
    {
        QMimeData* data = new QMimeData();
        data->setUrls(urls);
        return data;
    }

    /// Sends a drag enter, move and drop of the data onto the widget
    static bool drop(QWidget* target, QMimeData* data)
    {
        const QPoint position(target->width() / 2, target->height() / 2);
        QDragEnterEvent enter(position, Qt::CopyAction | Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &enter);
        if (!enter.isAccepted())
        {
            return false;
        }
        QDragMoveEvent move(position, Qt::CopyAction | Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &move);
        QDropEvent dropEvent(QPointF(position), Qt::CopyAction | Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &dropEvent);
        return dropEvent.isAccepted();
    }
};

class AppHomeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void routes_data();
    void routes();
    void recentRouteRoundTrip();
    void recentRouteRandom();
    void formats();
    void homeContent();
    void homeLinks();
    void homeMissingFile();
    void openContent();
    void openNewTab();
    void openFolderAndLocationActions();
    void recentContent();
    void recentFilters_data();
    void recentFilters();
    void recentSortAndGroups();
    void recentEmptyState();
    void recentContinueReading();
    void recentListActions();
    void recentPortableBanner();
    void menus_data();
    void menus();
    void menuByKeyboard();
    void menuActions();
    void saveWorkspaceDialog();
    void renameWorkspaceDialog();
    void deleteWorkspaceDialog();
    void renameLocationDialog();
    void clearHistoryDialog_data();
    void clearHistoryDialog();
    void restoreAndOpenWorkspace();
    void addPinnedLocation();
    void browseFiles();
    void dropFiles();
    void dropFilesRefused();
    void dropZoneFeedback();
    void dialogScrim();
    void messageDetailsPlainText();
    void openDocumentsRecords();
    void notificationsFromOtherConnection();
    void notificationsCoalesced();
    void thumbnailArrives();
    void thumbnailAfterDeletion();
    void realThumbnailer();
    void hostileRows();
    void geometryHome();
    void geometryOpen();
    void geometryRecent();
    void geometryDialogs();
    void pixels_data();
    void pixels();
    void rightToLeft();
    void minimalWindow();
    void keyboard();
    void accessibleNames();
    void viewState();
    void deleteWithOpenMenu();
    void deleteWithOpenDialog();
    void deleteWithPendingThumbnail();
    void deleteWithNativeDialog();
    void deleteFromOwnSignal();
    void deleteWindow();
    void pageRoutesRefused();
    void databaseErrors();
    void smallParts();
    void recentModel_data();
    void recentModel();
    void recentLockArrives();
    void recentListKeys();
    void withoutDatabase();
    void dialogEdgeCases();
    void databaseFaults();
    void manyThumbnails();
    void networkRows();
    void refusedPathRequests();
    void thumbnailCacheLru();
    void focusAfterPin();
    void homeEmptyStates();
    void screenshots();
    void screenshotsStates();
};

void AppHomeTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    qRegisterMetaType<pdf::PDFAppRoute>();
    qRegisterMetaType<pdf::PDFAppNavigationMode>();
}

void AppHomeTest::cleanup()
{
    HomeFixture::settle();
}

void AppHomeTest::routes_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<QString>("pageClass");

    QTest::newRow("home") << "/home" << "pdf::PDFAppHomePage";
    QTest::newRow("root") << "/" << "pdf::PDFAppHomePage";
    QTest::newRow("open") << "/open" << "pdf::PDFAppOpenPage";
    QTest::newRow("recent") << "/recent" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent files") << "/recent?filter=files" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent folders") << "/recent?filter=folders" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent workspaces") << "/recent?filter=workspaces" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent pinned") << "/recent?filter=pinned" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent sort name") << "/recent?sort=name" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent sort folder") << "/recent?sort=folder" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent sort last") << "/recent?sort=last-opened" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent search") << "/recent?q=report%202026" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent all") << "/recent?filter=pinned&sort=name&q=x" << "pdf::PDFAppRecentPage";
    QTest::newRow("recent unicode") << "/recent?q=%C4%8Dern%C3%BD" << "pdf::PDFAppRecentPage";

    QTest::newRow("home query") << "/home?x=1" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("home anchor") << "/home#top" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("open query") << "/open?filter=files" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("open anchor") << "/open#drop" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("open segment") << "/open/files" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent filter bogus") << "/recent?filter=bogus" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent filter case") << "/recent?filter=Files" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent filter all") << "/recent?filter=all" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent filter empty") << "/recent?filter=" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent sort bogus") << "/recent?sort=size" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent unknown key") << "/recent?topic=x" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent duplicate") << "/recent?filter=files&filter=pinned" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent long search") << QStringLiteral("/recent?q=%1").arg(QString(300, QLatin1Char('a'))) << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent anchor") << "/recent#pinned" << "pdf::PDFPageNotFoundPage";
    QTest::newRow("recent nul") << "/recent?q=%00" << "pdf::PDFPageNotFoundPage";
}

void AppHomeTest::routes()
{
    QFETCH(QString, route);
    QFETCH(QString, pageClass);

    HomeFixture fixture;
    fixture.navigate(route);
    QVERIFY(fixture.currentPage());
    QCOMPARE(QString::fromLatin1(fixture.currentPage()->metaObject()->className()), pageClass);

    if (pageClass != QLatin1String("pdf::PDFPageNotFoundPage"))
    {
        QVERIFY(!fixture.currentPage()->getHeaderTitle().isEmpty());
        QVERIFY(!fixture.currentPage()->getHeaderSubtitle().isEmpty());
        QCOMPARE(fixture.homeTab()->getHeader()->getTitleLabel()->text(), fixture.currentPage()->getHeaderTitle());
        const pdf::PDFAppNavItem item = fixture.homeTab()->getNavItem();
        QVERIFY(item == pdf::PDFAppNavItem::Home || item == pdf::PDFAppNavItem::Open || item == pdf::PDFAppNavItem::Recent);
    }

    // The page itself accepts the routes of the table (it is created also by other means)
    const pdf::PDFAppRoute parsed = pdf::PDFAppRoute::parse(route);
    if (pageClass == QLatin1String("pdf::PDFAppRecentPage"))
    {
        pdf::PDFAppRecentPage recent(fixture.context.get());
        QVERIFY(recent.setRoute(parsed));
    }
    if (pageClass == QLatin1String("pdf::PDFAppOpenPage"))
    {
        pdf::PDFAppOpenPage open(fixture.context.get());
        QVERIFY(open.setRoute(parsed));
    }
}

void AppHomeTest::recentRouteRoundTrip()
{
    using Filter = pdf::PDFAppRecentPage::Filter;
    const Filter filters[] = { Filter::All, Filter::Files, Filter::Folders, Filter::Workspaces, Filter::Pinned };
    const pdf::PDFRecentSort sorts[] = { pdf::PDFRecentSort::LastOpened, pdf::PDFRecentSort::Name, pdf::PDFRecentSort::Folder };
    const QString texts[] = { QString(), QStringLiteral("report"), QStringLiteral("a&b c?d=e#f/g%h"), QStringLiteral("černý 日本"), QString(256, QLatin1Char('x')) };

    HomeFixture fixture;
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);

    for (Filter filter : filters)
    {
        for (pdf::PDFRecentSort sort : sorts)
        {
            for (const QString& text : texts)
            {
                const pdf::PDFAppRoute route = pdf::PDFAppRecentPage::createRoute(filter, sort, text);
                QVERIFY2(route.isValid(), qPrintable(route.toString()));
                const pdf::PDFAppRoute parsed = pdf::PDFAppRoute::parse(route.toString());
                QCOMPARE(parsed, route);

                // Defaults are left out of the route
                QCOMPARE(route.hasQueryItem(QStringLiteral("filter")), filter != Filter::All);
                QCOMPARE(route.hasQueryItem(QStringLiteral("sort")), sort != pdf::PDFRecentSort::LastOpened);
                QCOMPARE(route.hasQueryItem(QStringLiteral("q")), !text.isEmpty());

                // The page takes the route and its controls show it
                QVERIFY(page->setRoute(parsed));
                QCOMPARE(page->getFilter(), filter);
                QCOMPARE(page->getSort(), sort);
                QCOMPARE(page->getSearchText(), text);
                QCOMPARE(page->getSearchField()->text(), text);
                QVERIFY(page->getChip(filter)->isChecked());
                QCOMPARE(page->getSortBox()->currentData().toString(), pdf::PDFRecentRepository::getSortId(sort));

                // The navigator shows the same page for the route
                fixture.navigate(route.toString());
                page = fixture.page<pdf::PDFAppRecentPage>();
                QVERIFY2(page, qPrintable(route.toString()));
                QCOMPARE(page->getFilter(), filter);
            }
        }
    }

    // A change of the filter by the controls replaces the route of the history of the tab, the page stays
    fixture.navigate(QStringLiteral("/home"));
    fixture.navigate(QStringLiteral("/recent"));
    page = fixture.page<pdf::PDFAppRecentPage>();
    QPointer<pdf::PDFAppRecentPage> guard(page);
    const int historySize = fixture.currentTab()->getHistory().getCount();
    page->getChip(Filter::Folders)->click();
    HomeFixture::settle();
    QVERIFY(guard);
    QCOMPARE(fixture.currentPage(), page);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/recent?filter=folders"));
    QCOMPARE(fixture.currentTab()->getHistory().getCount(), historySize);

    page->getSortBox()->setCurrentIndex(1);
    Q_EMIT page->getSortBox()->activated(1);
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/recent?filter=folders&sort=name"));

    page->getSearchField()->setFocus();
    QTest::keyClicks(page->getSearchField(), QStringLiteral("ab"));
    QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), QStringLiteral("/recent?filter=folders&sort=name&q=ab"));
    QCOMPARE(page->getRoute().toString(), QStringLiteral("/recent?filter=folders&sort=name&q=ab"));
    QVERIFY(page->getSearchField()->hasFocus());

    // Back returns to the previous entry (Home), Forward shows the replaced route again
    QVERIFY(fixture.navigator()->goBack());
    HomeFixture::settle();
    QVERIFY(fixture.page<pdf::PDFAppHomePage>());
    QVERIFY(fixture.navigator()->goForward());
    HomeFixture::settle();
    page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);
    QCOMPARE(page->getFilter(), Filter::Folders);
    QCOMPARE(page->getSort(), pdf::PDFRecentSort::Name);
    QCOMPARE(page->getSearchText(), QStringLiteral("ab"));
}

void AppHomeTest::recentRouteRandom()
{
    // Random search texts never make an invalid route (control characters, lone
    // surrogates, overlong encodings are cut)
    HomeFixture fixture;
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);

    QRandomGenerator generator(20261021);
    for (int i = 0; i < 300; ++i)
    {
        QString text;
        const int length = generator.bounded(0, 400);
        for (int j = 0; j < length; ++j)
        {
            text += QChar(char16_t(generator.bounded(0, 0x10000)));
        }

        page->setFilter(pdf::PDFAppRecentPage::Filter::All, pdf::PDFRecentSort::LastOpened, text);
        QVERIFY(page->getSearchText().size() <= pdf::PDFAppRecentPage::MAXIMUM_SEARCH_LENGTH);
        const pdf::PDFAppRoute route = fixture.currentTab()->getHistory().getCurrentEntry()->route;
        QVERIFY2(route.isValid(), qPrintable(route.getRequestedText().left(100)));
        QCOMPARE(pdf::PDFAppRoute::parse(route.toString()), route);
        for (const QChar character : page->getSearchText())
        {
            QVERIFY(character.unicode() >= 0x20);
            QVERIFY(!(character.unicode() >= 0x7F && character.unicode() <= 0x9F));
        }
    }
}

void AppHomeTest::formats()
{
    const qint64 now = HomeFixture::getNow();
    QCOMPARE(pdf::PDFAppHomeFormat::formatTime(HomeFixture::at(0, 10, 24), now), QStringLiteral("Today 10:24"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatTime(HomeFixture::at(1, 16, 15), now), QStringLiteral("Yesterday 16:15"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatTime(HomeFixture::at(2, 9, 0), now), QStringLiteral("Sep 18, 2026"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatTime(0, now), QString());
    QCOMPARE(pdf::PDFAppHomeFormat::formatTime(-5, now), QString());
    QCOMPARE(pdf::PDFAppHomeFormat::formatLastUsed(HomeFixture::at(0, 8, 17), now), QStringLiteral("Last used Today 08:17"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatLastUsed(0, now), QStringLiteral("Not used yet"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatLastOpened(HomeFixture::at(0, 10, 24), now), QStringLiteral("Last opened Today 10:24"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatLastOpened(0, now), QString());
    QCOMPARE(pdf::PDFAppHomeFormat::formatPageCount(24), QStringLiteral("24 pages"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPageCount(-1), QString());
    QCOMPARE(pdf::PDFAppHomeFormat::formatDocumentCount(4), QStringLiteral("4 documents"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPdfCount(6, false), QStringLiteral("6 PDF files"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPdfCount(10000, true), QStringLiteral("10000+ PDF files"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPdfCount(-1, false), QString());
    QCOMPARE(pdf::PDFAppHomeFormat::formatPercent(0.5), QStringLiteral("50 %"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPercent(qQNaN()), QStringLiteral("0 %"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPercent(7.0), QStringLiteral("100 %"));
    QVERIFY(!pdf::PDFAppHomeFormat::formatSize(2516582).isEmpty());
    QCOMPARE(pdf::PDFAppHomeFormat::formatSize(-1), QString());

    // Groups of the Recent list
    using Group = pdf::PDFAppHomeFormat::DateGroup;
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroup(HomeFixture::at(0, 1, 0), now), Group::Today);
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroup(now + 86400000LL * 3, now), Group::Today);
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroup(HomeFixture::at(1, 23, 59), now), Group::Yesterday);
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroup(HomeFixture::at(6, 12, 0), now), Group::ThisWeek);
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroup(HomeFixture::at(7, 12, 0), now), Group::Earlier);
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroup(0, now), Group::Earlier);
    for (Group group : { Group::Today, Group::Yesterday, Group::ThisWeek, Group::Earlier })
    {
        QVERIFY(!pdf::PDFAppHomeFormat::getDateGroupText(group).isEmpty());
    }
    QCOMPARE(pdf::PDFAppHomeFormat::getDateGroupText(static_cast<Group>(77)), QStringLiteral("Earlier"));

    // Reading states
    pdf::PDFRecentFile file;
    QCOMPARE(pdf::PDFAppHomeFormat::formatReading(file), QStringLiteral("Not started"));
    QCOMPARE(pdf::PDFAppHomeFormat::getBarProgress(file), -1.0);
    file.lastPage = 12;
    file.pageCount = 24;
    QCOMPARE(pdf::PDFAppHomeFormat::formatReading(file), QStringLiteral("Page 12 of 24"));
    QCOMPARE(pdf::PDFAppHomeFormat::getBarProgress(file), 0.5);
    file.pageCount = -1;
    QCOMPARE(pdf::PDFAppHomeFormat::formatReading(file), QStringLiteral("Page 12"));
    QCOMPARE(pdf::PDFAppHomeFormat::getBarProgress(file), -1.0);
    file.pageCount = 36;
    file.reachedEnd = true;
    QCOMPARE(pdf::PDFAppHomeFormat::formatReading(file), QStringLiteral("Finished"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatPinnedProgress(file), QStringLiteral("Finished · 36 pages"));
    QCOMPARE(pdf::PDFAppHomeFormat::getBarProgress(file), 1.0);
    file.pageCount = -1;
    QCOMPARE(pdf::PDFAppHomeFormat::formatPinnedProgress(file), QStringLiteral("Finished"));

    file.folder = QStringLiteral("C:/Users/Jakub/Documents");
    file.pageCount = 24;
    file.fileSize = -1;
    QCOMPARE(pdf::PDFAppHomeFormat::formatFileDetails(file), QStringLiteral("%1 · 24 pages").arg(QDir::toNativeSeparators(file.folder)));

    // Workspaces
    pdf::PDFWorkspace workspace;
    workspace.documentCount = 4;
    QCOMPARE(pdf::PDFAppHomeFormat::formatWorkspaceMeta(workspace, now), QStringLiteral("4 documents"));
    workspace.opened = HomeFixture::at(1, 16, 15);
    QCOMPARE(pdf::PDFAppHomeFormat::formatWorkspaceMeta(workspace, now), QStringLiteral("4 documents · Opened Yesterday 16:15"));
    QCOMPARE(pdf::PDFAppHomeFormat::formatWorkspaceDetails(workspace, now), QStringLiteral("4 documents"));
    workspace.saved = HomeFixture::at(10, 9, 0);
    QCOMPARE(pdf::PDFAppHomeFormat::formatWorkspaceDetails(workspace, now), QStringLiteral("4 documents · Saved Sep 10, 2026"));
    workspace.documentCount = 3;
    QCOMPARE(pdf::PDFAppHomeFormat::formatSessionMeta(workspace, now), QStringLiteral("3 documents · closed Sep 10, 2026"));
    workspace.saved = 0;
    QCOMPARE(pdf::PDFAppHomeFormat::formatSessionMeta(workspace, now), QStringLiteral("3 documents"));

    // Hues and icons
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderHue(pdf::PDFRecentFolderKind::Documents), pdf::PDFAccentHue::Green);
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderHue(pdf::PDFRecentFolderKind::Downloads), pdf::PDFAccentHue::Blue);
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderHue(pdf::PDFRecentFolderKind::Desktop), pdf::PDFAccentHue::Violet);
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderHue(pdf::PDFRecentFolderKind::Other), pdf::PDFAccentHue::Neutral);
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderIconName(pdf::PDFRecentFolderKind::Downloads), QStringLiteral("download"));
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderIconName(pdf::PDFRecentFolderKind::Desktop), QStringLiteral("monitor"));
    QCOMPARE(pdf::PDFAppHomeFormat::getFolderIconName(pdf::PDFRecentFolderKind::Documents), QStringLiteral("folder"));
    QCOMPARE(pdf::PDFAppHomeFormat::getLocationIconName(pdf::PDFPinnedLocationKind::Local), QStringLiteral("briefcase"));
    QCOMPARE(pdf::PDFAppHomeFormat::getLocationIconName(pdf::PDFPinnedLocationKind::Cloud), QStringLiteral("cloud"));
    QCOMPARE(pdf::PDFAppHomeFormat::getLocationIconName(pdf::PDFPinnedLocationKind::Network), QStringLiteral("server"));
    for (const QString& icon : { QStringLiteral("download"), QStringLiteral("monitor"), QStringLiteral("folder"), QStringLiteral("briefcase"), QStringLiteral("cloud"), QStringLiteral("server") })
    {
        QVERIFY(pdf::PDFThemeIcons::hasIcon(icon));
    }

    // Untrusted texts: control characters, lone surrogates, long texts
    QCOMPARE(pdf::PDFAppHomeFormat::toDisplayText(QStringLiteral("a\nb\tc\u2028d")), QStringLiteral("a b c d"));
    QString surrogates = QStringLiteral("x");
    surrogates += QChar(0xD800);
    surrogates += QStringLiteral("y");
    QCOMPARE(pdf::PDFAppHomeFormat::toDisplayText(surrogates), QStringLiteral("xy"));
    const QString shortened = pdf::PDFAppHomeFormat::toDisplayText(QString(5000, QLatin1Char('a')), 10);
    QCOMPARE(shortened.size(), 10);
    QVERIFY(shortened.endsWith(QChar(0x2026)));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(QString(300, QLatin1Char('b')), 256).size(), 256);
    QString emoji;
    for (int i = 0; i < 200; ++i)
    {
        emoji += QString::fromUcs4(U"\U0001F600");
    }
    const QString cut = pdf::PDFAppHomeFormat::toSearchText(emoji, 255);
    QVERIFY(cut.size() <= 255);
    QVERIFY(!cut.back().isHighSurrogate());

    // A search text is cut without an ellipsis (the ellipsis would be searched for)
    // and never in the middle of a pair of surrogates
    QCOMPARE(cut, emoji.left(254));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(emoji.left(256), 255), emoji.left(254));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(emoji.left(256), 256), emoji.left(256));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(QStringLiteral("a") + emoji, 256), QStringLiteral("a") + emoji.left(254));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(QString(300, QLatin1Char('b')), 256), QString(256, QLatin1Char('b')));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(QStringLiteral("a\nb"), 256), QStringLiteral("a b"));
    QCOMPARE(pdf::PDFAppHomeFormat::toSearchText(QString(), 256), QString());

    // The shortest limit: only the ellipsis
    QCOMPARE(pdf::PDFAppHomeFormat::toDisplayText(QStringLiteral("ab"), 1), QString(QChar(0x2026)));
    QCOMPARE(pdf::PDFAppHomeFormat::toDisplayText(QStringLiteral("ab"), 0), QString(QChar(0x2026)));
    QCOMPARE(pdf::PDFAppHomeFormat::toDisplayText(QStringLiteral("a"), 1), QStringLiteral("a"));
}

void AppHomeTest::homeContent()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(page);
    fixture.waitIdle();

    QCOMPARE(page->getTitle(), QStringLiteral("Home"));
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("Welcome to PDF4QT"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("A fast, modern and open-source PDF editor for Windows, Linux and macOS."));

    // Quick Actions of 28:4: Open PDF, eleven tools of the catalog, All Tools
    const QList<pdf::PDFQuickActionTile*> tiles = page->getQuickActionTiles();
    QCOMPARE(tiles.size(), 13);
    const QStringList titles = { QStringLiteral("Open PDF"), QStringLiteral("Images to PDF"), QStringLiteral("PDF to Images"), QStringLiteral("Recognize Text (OCR)"),
                                 QStringLiteral("Edit Content"), QStringLiteral("Merge"), QStringLiteral("Manage Pages"), QStringLiteral("Compress"),
                                 QStringLiteral("Protect with Password"), QStringLiteral("Sign with Certificate"), QStringLiteral("Remove Hidden Data"),
                                 QStringLiteral("Measure"), QStringLiteral("All Tools") };
    const pdf::PDFAccentHue hues[] = { pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Green,
                                       pdf::PDFAccentHue::Amber, pdf::PDFAccentHue::Amber, pdf::PDFAccentHue::Violet, pdf::PDFAccentHue::Red, pdf::PDFAccentHue::Red,
                                       pdf::PDFAccentHue::Red, pdf::PDFAccentHue::Teal, pdf::PDFAccentHue::Neutral };
    const QStringList icons = { QStringLiteral("folder-open"), QStringLiteral("image"), QStringLiteral("download"), QStringLiteral("ocr"), QStringLiteral("edit"),
                                QStringLiteral("layers"), QStringLiteral("grid"), QStringLiteral("minimize-2"), QStringLiteral("lock"), QStringLiteral("sign-certificate"),
                                QStringLiteral("hidden-data"), QStringLiteral("ruler"), QStringLiteral("tools") };
    for (int i = 0; i < tiles.size(); ++i)
    {
        QCOMPARE(tiles[i]->getTitle(), titles[i]);
        QCOMPARE(tiles[i]->getHue(), hues[i]);
        QCOMPARE(tiles[i]->getIconName(), icons[i]);
        QVERIFY(!tiles[i]->getDescription().isEmpty());
        QVERIFY(!tiles[i]->isStarVisible());
    }
    QCOMPARE(tiles[5]->getDescription(), QStringLiteral("Combine several files into one PDF"));
    QCOMPARE(tiles[0]->getDescription(), QStringLiteral("Browse and open a document"));
    QCOMPARE(tiles[12]->getDescription(), QStringLiteral("View complete catalog"));
    for (const QString& toolId : pdf::PDFAppHomePage::getQuickActionToolIds())
    {
        QVERIFY2(pdf::PDFDocumentToolCatalog::findTool(toolId), qPrintable(toolId));
    }

    // Recent Files: the five last files, newest first, without the star
    const QList<pdf::PDFRecentFileRow*> rows = page->getRecentFileRows();
    QCOMPARE(rows.size(), 5);
    const QStringList names = { QStringLiteral("Project_Report.pdf"), QStringLiteral("Invoice_2026.pdf"), QStringLiteral("Technical_Documentation.pdf"),
                                QStringLiteral("Building_Plan.pdf"), QStringLiteral("Presentation.pdf") };
    const QStringList times = { QStringLiteral("Today 10:24"), QStringLiteral("Today 08:17"), QStringLiteral("Yesterday 16:03"), QStringLiteral("Sep 18, 2026"),
                                QStringLiteral("Sep 15, 2026") };
    for (int i = 0; i < rows.size(); ++i)
    {
        QCOMPARE(rows[i]->getFileName(), names[i]);
        QCOMPARE(rows[i]->getTime(), times[i]);
        QVERIFY(!rows[i]->isStarVisible());
        QCOMPARE(rows[i]->getStatus(), pdf::PDFRecentFileRow::Status::Available);
        QVERIFY(rows[i]->getMenu());
        QVERIFY(!rows[i]->getPath().contains(QLatin1Char(QDir::separator() == QLatin1Char('/') ? '\\' : '/')));
    }
    QVERIFY(page->getRecentFilesEmptyState()->isHidden());
    QVERIFY(page->getDropZone()->isVisible());
    QCOMPARE(page->getDropZone()->getVariant(), pdf::PDFAppDropZone::Variant::Compact);
    QVERIFY(!page->getDropZone()->getBrowseButton());

    // Learn & Resources and Support
    const QList<pdf::PDFResourceCard*> cards = page->getResourceCards();
    QCOMPARE(cards.size(), 3);
    QCOMPARE(cards[0]->getTitle(), QStringLiteral("Video Tutorials"));
    QCOMPARE(cards[0]->getHue(), pdf::PDFAccentHue::Violet);
    QCOMPARE(cards[1]->getTitle(), QStringLiteral("User Manual"));
    QCOMPARE(cards[1]->getHue(), pdf::PDFAccentHue::Green);
    QCOMPARE(cards[2]->getTitle(), QStringLiteral("Getting Started"));
    QCOMPARE(cards[2]->getHue(), pdf::PDFAccentHue::Amber);
    const QList<pdf::PDFLinkRow*> supportRows = page->getSupportRows();
    QCOMPARE(supportRows.size(), 3);
    QCOMPARE(supportRows[0]->getTitle(), QStringLiteral("Report Issue"));
    QCOMPARE(supportRows[0]->getIconColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::IconDanger));
    QCOMPARE(supportRows[1]->getTitle(), QStringLiteral("Discuss on GitHub"));
    QCOMPARE(supportRows[2]->getTitle(), QStringLiteral("Become a Sponsor"));
    QCOMPARE(supportRows[2]->getIconColor(), std::optional<pdf::PDFThemeColors::Color>(pdf::PDFThemeColors::AccentRedIcon));

    // Recent Tools: the last used tools of tool_usage
    const QList<pdf::PDFRecentToolRow*> toolRows = page->getRecentToolRows();
    QCOMPARE(toolRows.size(), 5);
    QCOMPARE(toolRows[0]->getName(), pdf::PDFDocumentToolCatalog::findTool(u"optimize.compress")->getTitle());
    QCOMPARE(toolRows[0]->getLastUsed(), QStringLiteral("Today 11:02"));
    QCOMPARE(toolRows[0]->getHue(), pdf::PDFAccentHue::Violet);
    QCOMPARE(toolRows[1]->getName(), pdf::PDFDocumentToolCatalog::findTool(u"protect.sign")->getTitle());
    QCOMPARE(toolRows[1]->getLastUsed(), QStringLiteral("Today 09:40"));
    QCOMPARE(toolRows[2]->getLastUsed(), QStringLiteral("Yesterday 16:15"));
    QCOMPARE(toolRows[4]->getName(), pdf::PDFDocumentToolCatalog::findTool(u"convert.pdf-to-images")->getTitle());
    QCOMPARE(toolRows[4]->getLastUsed(), QStringLiteral("Sep 15, 2026"));
    QVERIFY(page->getRecentToolsEmptyState()->isHidden());

    // An empty database: the hints of the empty lists
    HomeFixture empty;
    pdf::PDFAppHomePage* emptyPage = empty.page<pdf::PDFAppHomePage>();
    QVERIFY(emptyPage);
    QVERIFY(emptyPage->getRecentFileRows().isEmpty());
    QVERIFY(emptyPage->getRecentToolRows().isEmpty());
    QVERIFY(!emptyPage->getRecentFilesEmptyState()->isHidden());
    QVERIFY(!emptyPage->getRecentToolsEmptyState()->isHidden());
}

void AppHomeTest::homeLinks()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    QSignalSpy externalSpy(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    struct Link
    {
        std::function<QAbstractButton*(pdf::PDFAppHomePage*)> button;
        QString route;
        QString pageClass;
    };

    const Link links[] = {
        { [](pdf::PDFAppHomePage* page) { return page->getQuickActionTiles().front(); }, QStringLiteral("/open"), QStringLiteral("pdf::PDFAppOpenPage") },
        { [](pdf::PDFAppHomePage* page) { return page->getQuickActionTiles()[5]; }, QStringLiteral("/tools/pages.merge"), QStringLiteral("pdf::PDFPageNotFoundPage") },
        { [](pdf::PDFAppHomePage* page) { return page->getQuickActionTiles().back(); }, QStringLiteral("/tools"), QString() },
        { [](pdf::PDFAppHomePage* page) { return page->getResourceCards()[0]; }, QStringLiteral("/support"), QStringLiteral("pdf::PDFAppSupportPage") },
        { [](pdf::PDFAppHomePage* page) { return page->getSupportRows()[2]; }, QStringLiteral("/sponsor"), QStringLiteral("pdf::PDFAppSponsorPage") },
        { [](pdf::PDFAppHomePage* page) { return page->getRecentToolRows()[0]; }, QStringLiteral("/tools/optimize.compress"), QStringLiteral("pdf::PDFPageNotFoundPage") },
        { [](pdf::PDFAppHomePage* page) { return page->findChild<pdf::PDFTextLink*>(); }, QStringLiteral("/recent"), QStringLiteral("pdf::PDFAppRecentPage") }
    };

    for (const Link& link : links)
    {
        fixture.navigate(QStringLiteral("/home"));
        pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
        QVERIFY(page);
        QAbstractButton* button = link.button(page);
        QVERIFY(button);
        button->click();
        HomeFixture::settle();
        QCOMPARE(fixture.currentTab()->getHistory().getCurrentEntry()->route.toString(), link.route);
        if (!link.pageClass.isEmpty())
        {
            QCOMPARE(QString::fromLatin1(fixture.currentPage()->metaObject()->className()), link.pageClass);
        }
    }

    // The manual opens in the Help tab
    fixture.navigate(QStringLiteral("/home"));
    fixture.page<pdf::PDFAppHomePage>()->getResourceCards()[1]->click();
    HomeFixture::settle();
    QVERIFY(fixture.window->findHelpTab());
    fixture.window->activateTab(fixture.homeTab());
    HomeFixture::settle();

    // Report Issue and Discuss on GitHub are external https addresses
    fixture.navigate(QStringLiteral("/home"));
    fixture.page<pdf::PDFAppHomePage>()->getSupportRows()[0]->click();
    fixture.page<pdf::PDFAppHomePage>()->getSupportRows()[1]->click();
    QCOMPARE(externalSpy.count(), 2);
    QCOMPARE(externalSpy[0][0].toUrl(), pdf::PDFAppProjectLinks::getNewIssue());
    QCOMPARE(externalSpy[1][0].toUrl(), pdf::PDFAppProjectLinks::getDiscussions());

    // A recent file opens in its own document tab (step 3), which becomes current
    pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
    page->getRecentFileRows()[1]->click();
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.invoice));
    QVERIFY(qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab()));
    QCOMPARE(fixture.window->getOpenDocuments().size(), size_t(1));
    QCOMPARE(fixture.window->getOpenDocuments().front().path, QFileInfo(sample.invoice).canonicalFilePath());
    QCOMPARE(fixture.closeDocumentTabs(), 1);
    QCOMPARE(fixture.currentTab(), fixture.homeTab());
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppHomeTest::homeMissingFile()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    QVERIFY(QFile::remove(sample.buildingPlan));

    fixture.navigate(QStringLiteral("/open"));
    fixture.navigate(QStringLiteral("/home"));
    fixture.waitIdle();
    pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(page);

    // "No longer exists" for the missing file only (asked by the folder information service)
    for (pdf::PDFRecentFileRow* row : page->getRecentFileRows())
    {
        const bool isMissing = row->getFileName() == QLatin1String("Building_Plan.pdf");
        QCOMPARE(row->getStatus(), isMissing ? pdf::PDFRecentFileRow::Status::Missing : pdf::PDFRecentFileRow::Status::Available);
    }

    // Open shows it too, in the pinned and in the recent files
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    int missing = 0;
    for (pdf::PDFRecentFileRow* row : open->getPinnedFileRows() + open->getRecentFileRows())
    {
        missing += row->getStatus() == pdf::PDFRecentFileRow::Status::Missing ? 1 : 0;
    }
    QCOMPARE(missing, 2);

    // Recent says it in the column of the progress
    fixture.navigate(QStringLiteral("/recent?filter=files"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    bool isFound = false;
    for (int row = 0; row < recent->getModel()->rowCount(); ++row)
    {
        const QModelIndex index = recent->getModel()->index(row, 0);
        if (index.data(Qt::DisplayRole).toString() == QLatin1String("Building_Plan.pdf"))
        {
            QCOMPARE(index.data(pdf::PDFComponentRoles::ProgressTextRole).toString(), QStringLiteral("No longer exists"));
            QCOMPARE(index.data(pdf::PDFComponentRoles::ProgressRole).toDouble(), -1.0);
            QVERIFY(index.data(pdf::PDFComponentRoles::MissingRole).toBool());
            isFound = true;
        }
        else if (index.data(Qt::DisplayRole).toString() == QLatin1String("Invoice_2026.pdf"))
        {
            QVERIFY(!index.data(pdf::PDFComponentRoles::MissingRole).toBool());
            QVERIFY(!index.data(pdf::PDFComponentRoles::LockedRole).toBool());
        }
    }
    QVERIFY(isFound);

    // The pinned card of the missing file has the badge of the library (PDFPinnedCard::Status)
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    recent = fixture.page<pdf::PDFAppRecentPage>();
    for (pdf::PDFPinnedCard* card : recent->getPinnedCards())
    {
        QCOMPARE(card->getStatus(), card->getName() == QLatin1String("Building_Plan.pdf") ? pdf::PDFPinnedCard::Status::Missing : pdf::PDFPinnedCard::Status::Available);
        QVERIFY(!card->isLocked());
    }

    // A document protected by a password (the thumbnailer records the state): the lock of the library
    const QDateTime modified = QFileInfo(sample.invoice).lastModified(QTimeZone::UTC);
    QVERIFY(!fixture.recent().setThumbnail(sample.invoice, QByteArray(), modified.toMSecsSinceEpoch(), pdf::PDFRecentThumbnailState::PasswordProtected, 0).isError());
    fixture.waitIdle();
    bool isLockedFound = false;
    for (int row = 0; row < recent->getModel()->rowCount(); ++row)
    {
        const QModelIndex index = recent->getModel()->index(row, 0);
        if (index.data(Qt::DisplayRole).toString() == QLatin1String("Invoice_2026.pdf"))
        {
            QVERIFY(index.data(pdf::PDFComponentRoles::LockedRole).toBool());
            isLockedFound = true;
        }
    }
    QVERIFY(isLockedFound);
    for (pdf::PDFPinnedCard* card : recent->getPinnedCards())
    {
        QCOMPARE(card->isLocked(), card->getName() == QLatin1String("Invoice_2026.pdf"));
    }
}

void AppHomeTest::openContent()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QVERIFY(page);

    QCOMPARE(page->getHeaderTitle(), QStringLiteral("Open"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Open a PDF document from your computer, a recent folder or a saved workspace."));
    QCOMPARE(fixture.homeTab()->getNavItem(), pdf::PDFAppNavItem::Open);

    // Drop zone with Browse Files… and the hint of the shortcut
    QCOMPARE(page->getDropZone()->getVariant(), pdf::PDFAppDropZone::Variant::Large);
    QVERIFY(page->getDropZone()->getBrowseButton());
    QCOMPARE(page->getDropZone()->getBrowseButton()->text(), QStringLiteral("Browse Files…"));
    QCOMPARE(page->getDropZone()->findChild<pdf::PDFLabel*>(QStringLiteral("dropHint"))->text(), QStringLiteral("%1  ·  PDF files (.pdf)").arg(QKeySequence(Qt::CTRL | Qt::Key_O).toString(QKeySequence::NativeText)));

    // Recent Folders: the standard folders first, then the used ones; at most six
    const std::vector<pdf::PDFRecentFolder> standard = pdf::PDFRecentRepository::getStandardFolders();
    const QList<pdf::PDFFolderCard*> folders = page->getFolderCards();
    QVERIFY(folders.size() <= pdf::PDFAppOpenPage::MAXIMUM_FOLDERS);
    QVERIFY(folders.size() >= qMin<int>(pdf::PDFAppOpenPage::MAXIMUM_FOLDERS, int(standard.size()) + 1));
    for (int i = 0; i < folders.size(); ++i)
    {
        if (i < int(standard.size()))
        {
            QCOMPARE(folders[i]->getHue(), pdf::PDFAppHomeFormat::getFolderHue(standard[size_t(i)].kind));
            QCOMPARE(folders[i]->getIconName(), pdf::PDFAppHomeFormat::getFolderIconName(standard[size_t(i)].kind));
        }
        else
        {
            QCOMPARE(folders[i]->getHue(), pdf::PDFAccentHue::Neutral);
            QVERIFY(folders[i]->getMeta().startsWith(QLatin1String("Last used ")));
        }
        QVERIFY(folders[i]->getMenu());
    }

    // Pinned Files in the order of pinning, View all (3)
    const QList<pdf::PDFRecentFileRow*> pinned = page->getPinnedFileRows();
    QCOMPARE(pinned.size(), 3);
    QCOMPARE(pinned[0]->getFileName(), QStringLiteral("Building_Plan.pdf"));
    QCOMPARE(pinned[1]->getFileName(), QStringLiteral("Technical_Documentation.pdf"));
    QCOMPARE(pinned[2]->getFileName(), QStringLiteral("Project_Report.pdf"));
    for (pdf::PDFRecentFileRow* row : pinned)
    {
        QVERIFY(row->isStarVisible());
        QVERIFY(row->isPinned());
    }
    QCOMPARE(page->getPinnedViewAllLink()->text(), QStringLiteral("View all (3)"));
    QVERIFY(page->getPinnedFilesSection()->isVisible());

    // Recent Files with the stars
    const QList<pdf::PDFRecentFileRow*> recent = page->getRecentFileRows();
    QCOMPARE(recent.size(), 5);
    QCOMPARE(recent[0]->getFileName(), QStringLiteral("Project_Report.pdf"));
    QVERIFY(recent[0]->isPinned());
    QVERIFY(!recent[1]->isPinned());
    QCOMPARE(recent[1]->getTime(), QStringLiteral("Today 08:17"));

    // Workspaces: Restore last session, the saved workspaces, Save open documents… (no documents in step 2)
    QVERIFY(page->getSessionRow()->isVisible());
    QVERIFY(page->getSessionRow()->isAccent());
    QCOMPARE(page->getSessionRow()->getMeta(), QStringLiteral("3 documents · closed Today 10:24"));
    const QList<pdf::PDFWorkspaceRow*> workspaces = page->getWorkspaceRows();
    QCOMPARE(workspaces.size(), 3);
    QCOMPARE(workspaces[0]->getName(), QStringLiteral("Thesis review"));
    QCOMPARE(workspaces[0]->getMeta(), QStringLiteral("4 documents · Opened Yesterday 16:15"));
    QCOMPARE(workspaces[1]->getName(), QStringLiteral("Invoices 2026"));
    QCOMPARE(workspaces[1]->getMeta(), QStringLiteral("3 documents · Opened Sep 16, 2026"));
    QCOMPARE(workspaces[2]->getName(), QStringLiteral("Client contracts"));
    QVERIFY(!page->getSaveWorkspaceButton()->isEnabled());
    QVERIFY(!page->getSaveWorkspaceButton()->toolTip().isEmpty());

    // Pinned Locations
    const QList<pdf::PDFFolderCard*> locations = page->getLocationCards();
    QCOMPARE(locations.size(), 1);
    QCOMPARE(locations[0]->getName(), QStringLiteral("Work Projects"));
    QCOMPARE(locations[0]->getMeta(), QStringLiteral("This PC"));
    QCOMPARE(locations[0]->getIconName(), QStringLiteral("briefcase"));
    QCOMPARE(locations[0]->getPath(), QDir::toNativeSeparators(sample.workProjects));

    // With open documents (step 3) the button saves them
    pdf::PDFAppDocumentActions::setOpenDocumentsProvider([](const QWidget*)
    {
        std::vector<pdf::PDFAppOpenDocument> documents(1);
        documents[0].path = QStringLiteral("C:/a.pdf");
        return documents;
    });
    page->refreshNow();
    QVERIFY(page->getSaveWorkspaceButton()->isEnabled());

    // Without data: no pinned section, the hints of the empty lists, no session
    HomeFixture empty;
    empty.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* emptyPage = empty.page<pdf::PDFAppOpenPage>();
    QVERIFY(emptyPage);
    QVERIFY(!emptyPage->getPinnedFilesSection()->isVisible());
    QVERIFY(emptyPage->getRecentFileRows().isEmpty());
    QVERIFY(!emptyPage->getSessionRow()->isVisible());
    QVERIFY(emptyPage->getWorkspaceRows().isEmpty());
    QVERIFY(emptyPage->getLocationCards().isEmpty());
    QCOMPARE(int(emptyPage->getFolderCards().size()), qMin<int>(pdf::PDFAppOpenPage::MAXIMUM_FOLDERS, int(standard.size())));
}

void AppHomeTest::openNewTab()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);

    // The New tab shows /open without the sidebar (pdf4qt-new-tab 955:152339)
    pdf::PDFAppTab* tab = fixture.window->openNewTab();
    HomeFixture::settle();
    QVERIFY(tab);
    QCOMPARE(tab->getKind(), pdf::PDFAppTabKind::NewTab);
    pdf::PDFAppOpenPage* page = qobject_cast<pdf::PDFAppOpenPage*>(tab->getPage());
    QVERIFY(page);
    pdf::PDFAppWorkspaceTab* workspaceTab = qobject_cast<pdf::PDFAppWorkspaceTab*>(tab);
    QVERIFY(workspaceTab && !workspaceTab->getSidebar());
    QCOMPARE(page->getPinnedFileRows().size(), 3);
    QCOMPARE(page->getWorkspaceRows().size(), 3);

    // Another link switches to the Home tab (View all of the recent files)
    QPointer<pdf::PDFAppTab> newTab(tab);
    page->getRecentFilesSection()->findChild<pdf::PDFTextLink*>(QStringLiteral("recentViewAll"))->click();
    HomeFixture::settle();
    QCOMPARE(fixture.currentTab(), fixture.homeTab());
    QVERIFY(fixture.page<pdf::PDFAppRecentPage>());
    QVERIFY(newTab);
    QVERIFY(qobject_cast<pdf::PDFAppOpenPage*>(newTab->getPage()));

    // Manage of the workspaces from the New tab: Recent with the filter workspaces
    fixture.window->activateTab(newTab);
    HomeFixture::settle();
    qobject_cast<pdf::PDFAppOpenPage*>(newTab->getPage())->findChild<pdf::PDFTextLink*>(QStringLiteral("manageWorkspaces"))->click();
    HomeFixture::settle();
    QCOMPARE(fixture.currentTab(), fixture.homeTab());
    QCOMPARE(fixture.page<pdf::PDFAppRecentPage>()->getFilter(), pdf::PDFAppRecentPage::Filter::Workspaces);

    // A file opened from the New tab is recorded by the window
    fixture.window->activateTab(newTab);
    HomeFixture::settle();
    qobject_cast<pdf::PDFAppOpenPage*>(newTab->getPage())->getRecentFileRows()[2]->click();
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), 1);
    fixture.closeMessageBoxes();
}

void AppHomeTest::openFolderAndLocationActions()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    QStringList requestedFolders;
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget* parent, const QString& folder)
    {
        requestedFolders << folder;
        return parent ? QStringList({ sample.presentation }) : QStringList();
    });

    // A folder card browses the files in the folder
    pdf::PDFFolderCard* folder = page->getFolderCards().back();
    const QString folderPath = folder->getPath();
    folder->click();
    HomeFixture::settle();
    QCOMPARE(requestedFolders.size(), 1);
    QCOMPARE(QDir::fromNativeSeparators(requestedFolders[0]), QDir::fromNativeSeparators(folderPath));
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.presentation));
    fixture.closeMessageBoxes();

    // A pinned location too
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->getLocationCards().front()->click();
    HomeFixture::settle();
    QCOMPARE(requestedFolders.size(), 2);
    QCOMPARE(requestedFolders[1], sample.workProjects);
    fixture.closeMessageBoxes();

    // Browse Files… of the drop zone and Ctrl+O: the default folder
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->getDropZone()->getBrowseButton()->click();
    HomeFixture::settle();
    QCOMPARE(requestedFolders.size(), 3);
    QVERIFY(requestedFolders[2].isEmpty());
    fixture.closeMessageBoxes();
    fixture.window->getCommandAction(pdf::PDFAppCommands::open())->trigger();
    HomeFixture::settle();
    QCOMPARE(requestedFolders.size(), 4);
    fixture.closeMessageBoxes();

    // Canceled dialog: nothing opens
    pdf::PDFAppFileDialogs::setOpenFilesHandler([](QWidget*, const QString&) { return QStringList(); });
    const int requested = fixture.window->getRequestedFiles().size();
    fixture.page<pdf::PDFAppOpenPage>()->getDropZone()->getBrowseButton()->click();
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), requested);
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppHomeTest::recentContent()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);
    QCOMPARE(page->getHeaderTitle(), QStringLiteral("Recent"));
    QCOMPARE(page->getHeaderSubtitle(), QStringLiteral("Documents, folders and workspaces you opened recently, with the page where you stopped reading."));

    // Chips: All = files + folders + workspaces
    const int folders = int(fixture.recent().getUsedFolders().size());
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Files)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 5);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Folders)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), folders);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Workspaces)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 3);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Pinned)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 3);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::All)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 5 + folders + 3);
    QVERIFY(page->getChip(pdf::PDFAppRecentPage::Filter::All)->isChecked());
    QCOMPARE(page->getSortBox()->currentText(), QStringLiteral("Last opened"));
    QCOMPARE(page->getSortBox()->property(pdf::PDFWidgetStyleProperties::SELECT_PREFIX).toString(), QStringLiteral("Sort:"));
    QCOMPARE(page->getSearchField()->placeholderText(), QStringLiteral("Search by name or path…"));

    // Pinned cards (three), Show all pinned (3)
    QVERIFY(page->getPinnedSection()->isVisible());
    const QList<pdf::PDFPinnedCard*> cards = page->getPinnedCards();
    QCOMPARE(cards.size(), 3);
    QCOMPARE(cards[0]->getName(), QStringLiteral("Building_Plan.pdf"));
    QCOMPARE(cards[0]->getProgressText(), QStringLiteral("Page 3 of 12"));
    QCOMPARE(cards[2]->getName(), QStringLiteral("Project_Report.pdf"));
    QCOMPARE(cards[2]->getProgressText(), QStringLiteral("Page 12 of 24"));
    QCOMPARE(cards[2]->getProgress(), 0.5);
    QVERIFY(cards[2]->isPinned());
    QCOMPARE(page->getShowAllPinnedLink()->text(), QStringLiteral("Show all pinned (3)"));

    // The history: groups by the day, the items of all kinds
    const pdf::PDFAppRecentModel* model = page->getModel();
    QVERIFY(model->rowCount() > 0);
    QCOMPARE(model->index(0, 0).data(pdf::PDFAppRecentModel::HeaderRole).toBool(), true);
    QCOMPARE(model->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("Today"));
    QCOMPARE(model->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("Project_Report.pdf"));
    QCOMPARE(model->index(1, 0).data(pdf::PDFComponentRoles::ProgressTextRole).toString(), QStringLiteral("Page 12 of 24"));
    QCOMPARE(model->index(1, 0).data(pdf::PDFComponentRoles::DateRole).toString(), QStringLiteral("Today 10:24"));
    QVERIFY(model->index(1, 0).data(pdf::PDFComponentRoles::SubtitleRole).toString().contains(QStringLiteral(" · ")));
    QVERIFY(model->index(1, 0).data(pdf::PDFComponentRoles::PinnedRole).toBool());

    int items = 0;
    int headers = 0;
    int headerCounts = 0;
    QStringList groups;
    bool hasWorkspace = false;
    bool hasThesis = false;
    bool hasFolderCount = false;
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QModelIndex index = model->index(row, 0);
        if (index.data(pdf::PDFAppRecentModel::HeaderRole).toBool())
        {
            ++headers;
            headerCounts += index.data(pdf::PDFAppRecentModel::CountRole).toInt();
            groups << index.data(Qt::DisplayRole).toString();
            QCOMPARE(model->flags(index), Qt::ItemIsEnabled);
            continue;
        }

        ++items;
        const int type = index.data(pdf::PDFComponentRoles::ItemTypeRole).toInt();
        if (type == int(pdf::PDFComponentPainter::RecentItemType::Workspace))
        {
            hasWorkspace = true;
            QCOMPARE(index.data(pdf::PDFComponentRoles::ProgressTextRole).toString(), QStringLiteral("—"));
            if (index.data(Qt::DisplayRole).toString() == QLatin1String("Thesis review"))
            {
                hasThesis = true;
                QCOMPARE(index.data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("4 documents · Saved Sep 10, 2026"));
                QCOMPARE(index.data(pdf::PDFComponentRoles::DateRole).toString(), QStringLiteral("Yesterday 16:15"));
            }
        }
        if (type == int(pdf::PDFComponentPainter::RecentItemType::Folder) && index.data(pdf::PDFComponentRoles::SubtitleRole).toString().endsWith(QLatin1String("PDF files")))
        {
            hasFolderCount = true;
        }
    }
    QCOMPARE(items, 5 + folders + 3);
    QCOMPARE(headerCounts, items);
    QCOMPARE(groups, QStringList({ QStringLiteral("Today"), QStringLiteral("Yesterday"), QStringLiteral("This week"), QStringLiteral("Earlier") }));
    QVERIFY(hasWorkspace);
    QVERIFY(hasThesis);
    QVERIFY(hasFolderCount);
    QVERIFY(!page->isEmptyStateShown());

    // Continue where you left off: the newest file in progress, which exists
    QVERIFY(page->getContinueSection()->isVisible());
    QCOMPARE(page->getContinueCard()->getName(), QStringLiteral("Project_Report.pdf"));
    QCOMPARE(page->getContinueCard()->getProgressText(), QStringLiteral("Page 12 of 24"));
    QCOMPARE(page->getContinueCard()->getProgress(), 0.5);
    QCOMPARE(page->getContinueCard()->getPath(), sample.projectReport);

    // Workspaces with Restore last session (bg/subtle on Recent)
    QVERIFY(page->getSessionRow()->isVisible());
    QVERIFY(!page->getSessionRow()->isAccent());
    QCOMPARE(page->getWorkspaceRows().size(), 3);
}

void AppHomeTest::recentFilters_data()
{
    QTest::addColumn<QString>("route");
    QTest::addColumn<int>("files");
    QTest::addColumn<bool>("hasFolders");
    QTest::addColumn<int>("workspaces");
    QTest::addColumn<bool>("pinnedSection");
    QTest::addColumn<bool>("headers");

    QTest::newRow("all") << "/recent" << 5 << true << 3 << true << true;
    QTest::newRow("files") << "/recent?filter=files" << 5 << false << 0 << false << true;
    QTest::newRow("folders") << "/recent?filter=folders" << 0 << true << 0 << false << true;
    QTest::newRow("workspaces") << "/recent?filter=workspaces" << 0 << false << 3 << false << true;
    QTest::newRow("pinned") << "/recent?filter=pinned" << 3 << false << 0 << false << true;
    QTest::newRow("search report") << "/recent?q=REPORT" << 1 << false << 0 << false << true;
    QTest::newRow("search path") << "/recent?q=PDF4QT%2FDocs" << 1 << true << 0 << false << true;
    QTest::newRow("search workspace") << "/recent?q=thesis" << 0 << false << 1 << false << true;
    QTest::newRow("search none") << "/recent?q=zzzz" << 0 << false << 0 << false << false;
    QTest::newRow("files by name") << "/recent?filter=files&sort=name" << 5 << false << 0 << false << false;
    QTest::newRow("all by folder") << "/recent?sort=folder" << 5 << true << 3 << true << false;
}

void AppHomeTest::recentFilters()
{
    QFETCH(QString, route);
    QFETCH(int, files);
    QFETCH(bool, hasFolders);
    QFETCH(int, workspaces);
    QFETCH(bool, pinnedSection);
    QFETCH(bool, headers);

    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(route);
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);

    int fileCount = 0;
    int folderCount = 0;
    int workspaceCount = 0;
    int headerCount = 0;
    for (const pdf::PDFAppRecentItem& item : page->getModel()->getItems())
    {
        switch (item.type)
        {
            case pdf::PDFAppRecentItem::Type::File:
                ++fileCount;
                break;
            case pdf::PDFAppRecentItem::Type::Folder:
                ++folderCount;
                break;
            case pdf::PDFAppRecentItem::Type::Workspace:
                ++workspaceCount;
                break;
            case pdf::PDFAppRecentItem::Type::Header:
                ++headerCount;
                break;
        }
    }

    QCOMPARE(fileCount, files);
    QCOMPARE(folderCount > 0, hasFolders);
    QCOMPARE(workspaceCount, workspaces);
    QCOMPARE(page->getPinnedSection()->isVisible(), pinnedSection);
    QCOMPARE(headerCount > 0, headers);
    QCOMPARE(page->isEmptyStateShown(), fileCount + folderCount + workspaceCount == 0);
    if (page->isEmptyStateShown())
    {
        QCOMPARE(page->getEmptyState()->getTitle(), QStringLiteral("No results"));
        QVERIFY(page->getEmptyState()->getText().contains(QStringLiteral("zzzz")));
    }
}

void AppHomeTest::recentSortAndGroups()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent?filter=files&sort=name"));
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QStringList names;
    for (const pdf::PDFAppRecentItem& item : page->getModel()->getItems())
    {
        names << item.name;
    }
    QCOMPARE(names, QStringList({ QStringLiteral("Building_Plan.pdf"), QStringLiteral("Invoice_2026.pdf"), QStringLiteral("Presentation.pdf"),
                                  QStringLiteral("Project_Report.pdf"), QStringLiteral("Technical_Documentation.pdf") }));

    // Sort by the folder: the folders first, then the names
    page->setFilter(pdf::PDFAppRecentPage::Filter::Files, pdf::PDFRecentSort::Folder, QString());
    QStringList folders;
    for (const pdf::PDFAppRecentItem& item : page->getModel()->getItems())
    {
        folders << item.sortFolder;
    }
    QStringList sorted = folders;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) { return QString::compare(a, b, Qt::CaseInsensitive) < 0; });
    QCOMPARE(folders, sorted);

    // The menu of the sort (976:12914): three items, the current one is checked by the combo box
    QCOMPARE(page->getSortBox()->count(), 3);
    QCOMPARE(page->getSortBox()->itemText(0), QStringLiteral("Last opened"));
    QCOMPARE(page->getSortBox()->itemText(1), QStringLiteral("Name"));
    QCOMPARE(page->getSortBox()->itemText(2), QStringLiteral("Folder"));
    QCOMPARE(page->getSortBox()->currentIndex(), 2);

    // Sort by keyboard: Last opened groups again
    page->getSortBox()->setFocus();
    page->getSortBox()->setCurrentIndex(0);
    Q_EMIT page->getSortBox()->activated(0);
    QCOMPARE(page->getSort(), pdf::PDFRecentSort::LastOpened);
    QVERIFY(page->getModel()->getItems().front().type == pdf::PDFAppRecentItem::Type::Header);
}

void AppHomeTest::recentEmptyState()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    QVERIFY(!fixture.recent().clearHistory(false).isError());
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();

    // 955:142865: the pinned items stay above the empty history, the workspaces stay
    QVERIFY(page->isEmptyStateShown());
    QCOMPARE(page->getEmptyState()->getTitle(), QStringLiteral("The history is empty"));
    QCOMPARE(page->getEmptyState()->getText(), QStringLiteral("Documents, folders and workspaces you open from now on appear here. Your pinned items and saved workspaces were kept."));
    QVERIFY(page->getPinnedSection()->isVisible());
    QCOMPARE(page->getPinnedCards().size(), 3);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::All)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 3);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Files)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 3);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Folders)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 0);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Workspaces)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 0);
    QCOMPARE(page->getChip(pdf::PDFAppRecentPage::Filter::Pinned)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), 3);
    QVERIFY(!page->getContinueSection()->isVisible());
    QVERIFY(!page->getSessionRow()->isVisible());
    QCOMPARE(page->getWorkspaceRows().size(), 3);
    QCOMPARE(page->getWorkspaceRows()[0]->getMeta(), QStringLiteral("4 documents"));

    // The filter Files shows the pinned files as the history
    page->setFilter(pdf::PDFAppRecentPage::Filter::Files, pdf::PDFRecentSort::LastOpened, QString());
    QVERIFY(!page->isEmptyStateShown());

    // Texts of the empty filters
    page->setFilter(pdf::PDFAppRecentPage::Filter::Folders, pdf::PDFRecentSort::LastOpened, QString());
    QVERIFY(page->isEmptyStateShown());
    QCOMPARE(page->getEmptyState()->getTitle(), QStringLiteral("No recent folders"));
    page->setFilter(pdf::PDFAppRecentPage::Filter::Workspaces, pdf::PDFRecentSort::LastOpened, QString());
    QCOMPARE(page->getEmptyState()->getTitle(), QStringLiteral("No recent workspaces"));
    QVERIFY(!fixture.recent().clearHistory(true).isError());
    fixture.waitIdle();
    page->setFilter(pdf::PDFAppRecentPage::Filter::Pinned, pdf::PDFRecentSort::LastOpened, QString());
    QCOMPARE(page->getEmptyState()->getTitle(), QStringLiteral("No pinned files"));
    page->setFilter(pdf::PDFAppRecentPage::Filter::Files, pdf::PDFRecentSort::LastOpened, QString());
    QCOMPARE(page->getEmptyState()->getTitle(), QStringLiteral("No recent files"));
    page->setFilter(pdf::PDFAppRecentPage::Filter::All, pdf::PDFRecentSort::LastOpened, QString());
    QVERIFY(!page->getPinnedSection()->isVisible());

    // Browse Files… of the empty state
    int calls = 0;
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&) { ++calls; return QStringList({ sample.invoice }); });
    page->getEmptyState()->getBrowseButton()->click();
    HomeFixture::settle();
    QCOMPARE(calls, 1);
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.invoice));
    fixture.closeMessageBoxes();
    fixture.waitIdle();

    // The opened file appears in the history (recorded by the window, notified)
    page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(!page->isEmptyStateShown());
}

void AppHomeTest::recentContinueReading()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // The newest file in progress does not exist: the next one is shown
    QVERIFY(QFile::remove(sample.projectReport));
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page->getContinueSection()->isVisible());
    QCOMPARE(page->getContinueCard()->getName(), QStringLiteral("Technical_Documentation.pdf"));
    QCOMPARE(page->getContinueCard()->getProgressText(), QStringLiteral("Page 41 of 128"));

    // Continue reading opens the file at its page
    page->getContinueCard()->getContinueButton()->click();
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.technical));
    fixture.closeMessageBoxes();

    // No file in progress exists: the section is hidden
    QVERIFY(QFile::remove(sample.technical));
    QVERIFY(QFile::remove(sample.buildingPlan));
    fixture.navigate(QStringLiteral("/home"));
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    QVERIFY(!fixture.page<pdf::PDFAppRecentPage>()->getContinueSection()->isVisible());
}

void AppHomeTest::recentListActions()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    pdf::PDFAppRecentListView* view = page->getListView();
    pdf::PDFAppRecentModel* model = page->getModel();
    pdf::PDFAppRecentDelegate* delegate = page->getDelegate();

    auto findRow = [&](const QString& name)
    {
        for (int row = 0; row < model->rowCount(); ++row)
        {
            if (model->index(row, 0).data(Qt::DisplayRole).toString() == name)
            {
                return row;
            }
        }
        return -1;
    };

    // The star of a file pins it (the database, then the notification)
    int row = findRow(QStringLiteral("Invoice_2026.pdf"));
    QVERIFY(row >= 0);
    QRect star = delegate->getActionRect(view, model->index(row, 0), pdf::PDFRecentItemDelegate::PinAction);
    QVERIFY(star.isValid());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, star.center());
    fixture.waitIdle();
    QVERIFY(fixture.recent().getFile(sample.invoice)->pinned);
    QCOMPARE(fixture.window->getRequestedFiles().size(), 0);
    row = findRow(QStringLiteral("Invoice_2026.pdf"));
    QVERIFY(model->index(row, 0).data(pdf::PDFComponentRoles::PinnedRole).toBool());

    // The star of a folder pins the location
    int folderRow = -1;
    for (int i = 0; i < model->rowCount(); ++i)
    {
        if (model->getItem(i)->type == pdf::PDFAppRecentItem::Type::Folder && !model->getItem(i)->isPinned)
        {
            folderRow = i;
            break;
        }
    }
    QVERIFY(folderRow >= 0);
    const QString folderPath = model->getItem(folderRow)->item.path;
    star = delegate->getActionRect(view, model->index(folderRow, 0), pdf::PDFRecentItemDelegate::PinAction);
    view->scrollTo(model->index(folderRow, 0));
    HomeFixture::settle();
    star = delegate->getActionRect(view, model->index(folderRow, 0), pdf::PDFRecentItemDelegate::PinAction);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, star.center());
    fixture.waitIdle();
    QVERIFY(fixture.recent().hasPinnedLocation(folderPath));
    QVERIFY(model->setData(model->index(0, 0), true, pdf::PDFComponentRoles::PinnedRole) == false);

    // A workspace has no star (StarVisibleRole), its only action is the menu
    int workspaceRow = -1;
    for (int i = 0; i < model->rowCount(); ++i)
    {
        if (model->getItem(i)->type == pdf::PDFAppRecentItem::Type::Workspace)
        {
            workspaceRow = i;
            break;
        }
    }
    QVERIFY(workspaceRow >= 0);
    QStyleOptionViewItem option;
    option.rect = view->visualRect(model->index(workspaceRow, 0));
    const QList<pdf::PDFRowDelegate::Action> actions = delegate->getActions(option, model->index(workspaceRow, 0));
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions[0].id, int(pdf::PDFRecentItemDelegate::MenuAction));
    QVERIFY(!model->index(workspaceRow, 0).data(pdf::PDFComponentRoles::StarVisibleRole).toBool());
    QVERIFY(model->index(row, 0).data(pdf::PDFComponentRoles::StarVisibleRole).toBool());
    QVERIFY(!model->setData(model->index(workspaceRow, 0), true, pdf::PDFComponentRoles::PinnedRole));
    QVERIFY(delegate->getActions(option, model->index(0, 0)).isEmpty());

    // A click on a file opens it; Enter opens the current item
    row = findRow(QStringLiteral("Presentation.pdf"));
    view->scrollTo(model->index(row, 0));
    HomeFixture::settle();
    const QRect rect = view->visualRect(model->index(row, 0));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.left() + 120, rect.center().y()));
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.presentation));
    fixture.closeMessageBoxes();

    page = fixture.page<pdf::PDFAppRecentPage>();
    view = page->getListView();
    model = page->getModel();
    view->setFocus();
    view->setCurrentIndex(model->index(findRow(QStringLiteral("Technical_Documentation.pdf")), 0));
    QTest::keyClick(view, Qt::Key_Return);
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.technical));
    fixture.closeMessageBoxes();

    // A click on a header does nothing
    const int requested = fixture.window->getRequestedFiles().size();
    page = fixture.page<pdf::PDFAppRecentPage>();
    page->getListView()->scrollToTop();
    HomeFixture::settle();
    const QRect headerRect = page->getListView()->visualRect(page->getModel()->index(0, 0));
    QTest::mouseClick(page->getListView()->viewport(), Qt::LeftButton, Qt::NoModifier, headerRect.center());
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), requested);
}

void AppHomeTest::recentPortableBanner()
{
    // 955:150880 (1440 x 1048): the portable banner at the top of the main column
    // (958:28036), the side column starts at the top of the page, the tag in the footer
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 1048));
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppStorageBanner* banner = fixture.homeTab()->getBanner();
    QVERIFY(banner->isVisible());
    QCOMPARE(banner->getKind(), pdf::PDFAppStorageBanner::Kind::Portable);
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);
    QVERIFY(page->getColumns()->getMainColumn()->isAncestorOf(banner));
    const QRect bannerRect = HomeTestHelper::rectIn(banner, fixture.window);
    const QRect searchRect = HomeTestHelper::rectIn(page->getSearchField(), fixture.window);
    const QRect continueRect = HomeTestHelper::rectIn(page->getContinueSection(), fixture.window);
    QCOMPARE(bannerRect.topLeft(), QPoint(242, 149));
    QCOMPARE(bannerRect.right(), HomeTestHelper::rectIn(page->getColumns()->getMainColumn(), fixture.window).right());
    QCOMPARE(searchRect.top() - bannerRect.bottom() - 1, 24);
    QCOMPARE(continueRect.top(), 149);
    QCOMPARE(fixture.homeTab()->getFooter()->getTagText(), QStringLiteral("Portable"));

    // Open has it at the top of its main column too, Home above the whole page
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    QVERIFY(open && open->getColumns()->getMainColumn()->isAncestorOf(banner));
    QVERIFY(banner->isVisible());
    QCOMPARE(HomeTestHelper::rectIn(open->getWorkspacesCard(), fixture.window).top(), 179);
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(!fixture.currentPage()->isAncestorOf(banner));
    QVERIFY(banner->isVisible());
    QVERIFY(HomeTestHelper::rectIn(banner, fixture.window).bottom() < HomeTestHelper::rectIn(fixture.currentPage(), fixture.window).top());

    // A hidden (dismissed) banner stays hidden in another page
    banner->hide();
    fixture.navigate(QStringLiteral("/recent"));
    QVERIFY(fixture.page<pdf::PDFAppRecentPage>()->getColumns()->getMainColumn()->isAncestorOf(banner));
    QVERIFY(banner->isHidden());
    banner->show();
    HomeFixture::settle();

    // The page holding the banner is destroyed: the banner goes back above the page
    QPointer<pdf::PDFAppStorageBanner> bannerGuard(banner);
    delete fixture.currentPage();
    HomeFixture::settle();
    QVERIFY(bannerGuard);
    QCOMPARE(bannerGuard->parentWidget(), fixture.homeTab()->findChild<QWidget*>(QStringLiteral("pageArea")));
    QVERIFY(!bannerGuard->isHidden());

    // A new page takes it again; the window is deleted with the banner in a page
    fixture.navigate(QStringLiteral("/open"));
    QVERIFY(fixture.page<pdf::PDFAppOpenPage>()->isAncestorOf(bannerGuard));
    delete fixture.window.data();
    HomeFixture::settle();
    QVERIFY(!bannerGuard);
}

void AppHomeTest::menus_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QStringList>("texts");

    const QStringList file = { QStringLiteral("Open"), QStringLiteral("Pin"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("|"), QStringLiteral("Remove from List") };
    const QStringList pinnedFile = { QStringLiteral("Open"), QStringLiteral("Unpin"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("|"), QStringLiteral("Remove from List") };
    const QStringList folder = { QStringLiteral("Browse files here…"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("Pin to Pinned Locations"), QStringLiteral("|"), QStringLiteral("Remove from List") };
    const QStringList pinnedFolder = { QStringLiteral("Browse files here…"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("Unpin from Pinned Locations"), QStringLiteral("|"), QStringLiteral("Remove from List") };
    const QStringList standardFolder = { QStringLiteral("Browse files here…"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("Pin to Pinned Locations") };
    const QStringList workspace = { QStringLiteral("Open"), QStringLiteral("Rename…"), QStringLiteral("|"), QStringLiteral("Delete…") };
    const QStringList location = { QStringLiteral("Browse files here…"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("Rename…"), QStringLiteral("|"), QStringLiteral("Remove from Pinned Locations") };

    QTest::newRow("home file") << "home-file" << file;
    QTest::newRow("home pinned file") << "home-pinned" << pinnedFile;
    QTest::newRow("open pinned file") << "open-pinned" << pinnedFile;
    QTest::newRow("open recent file") << "open-recent" << file;
    QTest::newRow("open used folder") << "open-folder" << folder;
    QTest::newRow("open pinned folder") << "open-pinned-folder" << pinnedFolder;
    QTest::newRow("open standard folder") << "open-standard-folder" << standardFolder;
    QTest::newRow("open workspace") << "open-workspace" << workspace;
    QTest::newRow("open location") << "open-location" << location;
    QTest::newRow("recent pinned card") << "recent-card" << pinnedFile;
    QTest::newRow("recent workspace row") << "recent-workspace" << workspace;
    QTest::newRow("recent list file") << "recent-list-file" << file;
    QTest::newRow("recent list folder") << "recent-list-folder" << folder;
    QTest::newRow("recent list workspace") << "recent-list-workspace" << workspace;
}

void AppHomeTest::menus()
{
    QFETCH(QString, kind);
    QFETCH(QStringList, texts);

    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    if (kind == QLatin1String("open-pinned-folder"))
    {
        fixture.recent().addPinnedLocation(QFileInfo(sample.invoice).absolutePath(), QString());
        fixture.waitIdle();
    }

    PDFMoreButtonPointer moreButton;
    if (kind.startsWith(QLatin1String("home")))
    {
        fixture.navigate(QStringLiteral("/home"));
        pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
        moreButton = page->getRecentFileRows()[kind == QLatin1String("home-file") ? 1 : 0]->getMenuButton();
    }
    else if (kind.startsWith(QLatin1String("open")))
    {
        fixture.navigate(QStringLiteral("/open"));
        fixture.waitIdle();
        pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
        if (kind == QLatin1String("open-pinned"))
        {
            moreButton = page->getPinnedFileRows()[0]->getMenuButton();
        }
        else if (kind == QLatin1String("open-recent"))
        {
            moreButton = page->getRecentFileRows()[1]->getMenuButton();
        }
        else if (kind == QLatin1String("open-workspace"))
        {
            moreButton = page->getWorkspaceRows()[0]->getMenuButton();
        }
        else if (kind == QLatin1String("open-location"))
        {
            moreButton = page->getLocationCards()[0]->getMenuButton();
        }
        else
        {
            const QString downloads = QDir::toNativeSeparators(QFileInfo(sample.invoice).absolutePath());
            for (pdf::PDFFolderCard* card : page->getFolderCards())
            {
                const bool isStandard = card->getMeta() == QLatin1String("Not used yet");
                if ((kind == QLatin1String("open-standard-folder") && isStandard) ||
                    (kind == QLatin1String("open-folder") && !isStandard && card->getPath() != downloads) ||
                    (kind == QLatin1String("open-pinned-folder") && card->getPath() == downloads))
                {
                    moreButton = card->getMenuButton();
                    break;
                }
            }
            if (!moreButton && kind == QLatin1String("open-standard-folder"))
            {
                QSKIP("Every standard folder of this computer has been used");
            }
        }
    }
    else if (kind == QLatin1String("recent-card") || kind == QLatin1String("recent-workspace"))
    {
        fixture.navigate(QStringLiteral("/recent"));
        pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
        moreButton = kind == QLatin1String("recent-card") ? page->getPinnedCards()[0]->getMenuButton() : page->getWorkspaceRows()[0]->getMenuButton();
    }
    else
    {
        fixture.navigate(QStringLiteral("/recent"));
        fixture.waitIdle();
        pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
        const pdf::PDFAppRecentItem::Type type = kind.endsWith(QLatin1String("file")) ? pdf::PDFAppRecentItem::Type::File
                                               : (kind.endsWith(QLatin1String("folder")) ? pdf::PDFAppRecentItem::Type::Folder : pdf::PDFAppRecentItem::Type::Workspace);
        int row = -1;
        for (int i = 0; i < page->getModel()->rowCount(); ++i)
        {
            const pdf::PDFAppRecentItem* item = page->getModel()->getItem(i);
            if (item->type == type && !item->isPinned)
            {
                row = i;
                break;
            }
        }
        QVERIFY(row >= 0);

        // The menu button of the delegate (a click on its action)
        pdf::PDFAppRecentListView* view = page->getListView();
        view->scrollTo(page->getModel()->index(row, 0));
        HomeFixture::settle();
        const QRect rect = page->getDelegate()->getActionRect(view, page->getModel()->index(row, 0), pdf::PDFRecentItemDelegate::MenuAction);
        QVERIFY(rect.isValid());
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        HomeFixture::settle();
        QMenu* menu = page->getListMenu();
        QVERIFY(menu);
        QVERIFY(menu->isVisible());
        QCOMPARE(HomeTestHelper::menuTexts(menu), texts);
        for (QAction* action : menu->actions())
        {
            QVERIFY(action->isSeparator() || !action->icon().isNull());
        }
        menu->close();
        HomeFixture::settle();
        QVERIFY(!page->getListMenu());
        return;
    }

    QVERIFY(moreButton);
    QMenu* menu = moreButton->getMenu();
    QVERIFY(menu);
    QCOMPARE(HomeTestHelper::menuTexts(menu), texts);
    for (QAction* action : menu->actions())
    {
        QVERIFY(action->isSeparator() || !action->icon().isNull());
        if (action->text() == QLatin1String("Delete…"))
        {
            QVERIFY(action->property(pdf::PDFWidgetStyleProperties::DESTRUCTIVE).toBool());
        }
    }

    // The mouse shows the menu below the button
    QTest::mouseClick(moreButton, Qt::LeftButton);
    HomeFixture::settle();
    QVERIFY(menu->isVisible());
    menu->close();
    HomeFixture::settle();
}

void AppHomeTest::menuByKeyboard()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // Space on the focused menu button, Escape closes the menu, the focus stays
    pdf::PDFMoreButton* button = page->getRecentFileRows()[0]->getMenuButton();
    button->setFocus(Qt::TabFocusReason);
    QTest::keyClick(button, Qt::Key_Space);
    HomeFixture::settle();
    QVERIFY(button->getMenu()->isVisible());
    QTest::keyClick(button->getMenu(), Qt::Key_Escape);
    HomeFixture::settle();
    QVERIFY(!button->getMenu()->isVisible());

    // The context menu key of a focused row shows its menu
    pdf::PDFRecentFileRow* row = page->getRecentFileRows()[1];
    const QString rowPath = row->property("pdf4qtPath").toString();
    QVERIFY(!rowPath.isEmpty());
    QVERIFY(!fixture.recent().getFile(rowPath)->pinned);
    row->setFocus(Qt::TabFocusReason);
    QContextMenuEvent keyEvent(QContextMenuEvent::Keyboard, QPoint(10, 10), row->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(row, &keyEvent);
    HomeFixture::settle();
    QVERIFY(row->getMenu()->isVisible());

    // The keyboard picks an action: Down to Pin, Enter pins the file
    QTest::keyClick(row->getMenu(), Qt::Key_Down);
    QTest::keyClick(row->getMenu(), Qt::Key_Down);
    QAction* active = row->getMenu()->activeAction();
    QVERIFY(active);
    QCOMPARE(active->text(), QStringLiteral("Pin"));
    QTest::keyClick(row->getMenu(), Qt::Key_Return);
    fixture.waitIdle();
    QVERIFY(fixture.recent().getFile(rowPath)->pinned);

    // Recent list: the context menu key of the current item and its action More actions
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    pdf::PDFAppRecentListView* view = recent->getListView();
    view->setFocus();
    view->setCurrentIndex(recent->getModel()->index(1, 0));
    recent->getDelegate()->triggerAction(view, recent->getModel()->index(1, 0), pdf::PDFRecentItemDelegate::MenuAction);
    HomeFixture::settle();
    QVERIFY(recent->getListMenu());
    QVERIFY(recent->getListMenu()->isVisible());
    QTest::keyClick(recent->getListMenu(), Qt::Key_Escape);
    HomeFixture::settle();

    // showListMenu of a header or an invalid row does nothing
    recent->showListMenu(0);
    recent->showListMenu(-1);
    recent->showListMenu(100000);
    HomeFixture::settle();
    QVERIFY(!recent->getListMenu() || !recent->getListMenu()->isVisible());
}

void AppHomeTest::menuActions()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    QSignalSpy externalSpy(fixture.navigator(), &pdf::PDFAppNavigator::externalUrlOpened);

    auto page = [&fixture]() { return fixture.page<pdf::PDFAppOpenPage>(); };
    auto item = [](pdf::PDFAppHomeItemKind kind, const QString& path, qint64 id = 0, bool pinned = false)
    {
        pdf::PDFAppHomeItem result;
        result.kind = kind;
        result.path = path;
        result.id = id;
        result.isPinned = pinned;
        result.name = QFileInfo(path).fileName();
        return result;
    };

    // Through the menu of a row: Pin of the invoice
    pdf::PDFRecentFileRow* invoiceRow = page()->getRecentFileRows()[1];
    QAction* pin = HomeTestHelper::findAction(invoiceRow->getMenu(), QStringLiteral("Pin"));
    QVERIFY(pin);
    pin->trigger();
    fixture.waitIdle();
    QVERIFY(fixture.recent().getFile(sample.invoice)->pinned);
    QCOMPARE(page()->getPinnedFileRows().size(), 4);

    // Unpin, Copy Path, Show in Folder (of the file: its folder), Remove from List
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::File, sample.invoice, 0, true), pdf::PDFAppHomeAction::Unpin));
    fixture.waitIdle();
    QVERIFY(!fixture.recent().getFile(sample.invoice)->pinned);
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::File, sample.invoice), pdf::PDFAppHomeAction::CopyPath));
    QCOMPARE(QGuiApplication::clipboard()->text(), QDir::toNativeSeparators(sample.invoice));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::File, sample.invoice), pdf::PDFAppHomeAction::ShowInFolder));
    QCOMPARE(externalSpy.count(), 1);
    QCOMPARE(externalSpy[0][0].toUrl(), QUrl::fromLocalFile(QFileInfo(sample.invoice).absolutePath()));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::File, sample.invoice), pdf::PDFAppHomeAction::RemoveFromList));
    fixture.waitIdle();
    QVERIFY(!fixture.recent().getFile(sample.invoice));
    QCOMPARE(page()->getRecentFileRows().size(), 4);

    // Open of a file
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::File, sample.presentation), pdf::PDFAppHomeAction::Open));
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.presentation));
    fixture.closeMessageBoxes();

    // Folders: Show in Folder (the folder itself), Pin / Unpin the location, Browse files here…, Remove from List
    const QString folder = QFileInfo(sample.technical).absolutePath();
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder), pdf::PDFAppHomeAction::ShowInFolder));
    QCOMPARE(externalSpy.last()[0].toUrl(), QUrl::fromLocalFile(folder));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder), pdf::PDFAppHomeAction::PinLocation));
    fixture.waitIdle();
    QVERIFY(fixture.recent().hasPinnedLocation(folder));
    QCOMPARE(page()->getLocationCards().size(), 2);
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder), pdf::PDFAppHomeAction::PinLocation));
    fixture.waitIdle();
    QCOMPARE(page()->getLocationCards().size(), 2);
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder, 0, true), pdf::PDFAppHomeAction::UnpinLocation));
    fixture.waitIdle();
    QVERIFY(!fixture.recent().hasPinnedLocation(folder));

    QStringList browsed;
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString& path) { browsed << path; return QStringList(); });
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder), pdf::PDFAppHomeAction::BrowseHere));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder), pdf::PDFAppHomeAction::Open));
    QCOMPARE(browsed, QStringList({ folder, folder }));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Folder, folder), pdf::PDFAppHomeAction::RemoveFromList));
    fixture.waitIdle();
    bool isUsed = false;
    for (const pdf::PDFRecentFolder& used : fixture.recent().getUsedFolders())
    {
        isUsed = isUsed || used.path == folder;
    }
    QVERIFY(!isUsed);

    // Locations: Browse files here…, Rename…, Remove from Pinned Locations
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Location, sample.workProjects, sample.locationId), pdf::PDFAppHomeAction::BrowseHere));
    QCOMPARE(browsed.last(), sample.workProjects);
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Location, sample.workProjects, sample.locationId), pdf::PDFAppHomeAction::Rename));
    QVERIFY(qobject_cast<pdf::PDFAppRenameLocationDialog*>(page()->getDialog()));
    page()->getDialog()->reject();
    HomeFixture::settle();
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Location, sample.workProjects, sample.locationId), pdf::PDFAppHomeAction::RemoveLocation));
    fixture.waitIdle();
    QVERIFY(fixture.recent().getPinnedLocations().empty());
    QCOMPARE(page()->getLocationCards().size(), 0);

    // Workspaces: Open, Rename…, Delete… (dialogs)
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Workspace, QString(), sample.thesisId), pdf::PDFAppHomeAction::Rename));
    QVERIFY(qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(page()->getDialog()));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Workspace, QString(), sample.thesisId), pdf::PDFAppHomeAction::Delete));
    QVERIFY(qobject_cast<pdf::PDFAppDeleteWorkspaceDialog*>(page()->getDialog()));
    page()->getDialog()->reject();
    HomeFixture::settle();
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Workspace, QString(), sample.contractsId), pdf::PDFAppHomeAction::Open));
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.projectReport));
    fixture.closeMessageBoxes();

    // A workspace, which another instance has deleted meanwhile: nothing happens
    QVERIFY(!fixture.workspaces().deleteWorkspace(sample.invoicesId).isError());
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Workspace, QString(), sample.invoicesId), pdf::PDFAppHomeAction::Rename));
    QVERIFY(!page()->getDialog());
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Workspace, QString(), sample.invoicesId), pdf::PDFAppHomeAction::Delete));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Workspace, QString(), sample.invoicesId), pdf::PDFAppHomeAction::Open));
    QVERIFY(page()->performAction(item(pdf::PDFAppHomeItemKind::Location, QString(), 123456), pdf::PDFAppHomeAction::Rename));
    QVERIFY(!page()->getDialog());

    // Texts and icons of all actions exist
    for (int action = int(pdf::PDFAppHomeAction::Open); action <= int(pdf::PDFAppHomeAction::RemoveLocation); ++action)
    {
        QVERIFY(!pdf::PDFAppHomeBasePage::getActionText(pdf::PDFAppHomeAction(action)).isEmpty());
        QVERIFY(pdf::PDFThemeIcons::hasIcon(pdf::PDFAppHomeBasePage::getActionIconName(pdf::PDFAppHomeAction(action))));
    }
    QVERIFY(pdf::PDFAppHomeBasePage::getActionText(pdf::PDFAppHomeAction(99)).isEmpty());
    QVERIFY(pdf::PDFAppHomeBasePage::getActionIconName(pdf::PDFAppHomeAction(99)).isEmpty());

    // A hostile action data of a menu does nothing
    QMenu* menu = page()->createItemMenu(item(pdf::PDFAppHomeItemKind::File, sample.presentation), page());
    for (QAction* action : menu->actions())
    {
        action->setData(QVariantList({ 99, 99 }));
    }
    menu->actions().front()->trigger();
    menu->actions().front()->setData(QVariantList({ 0, 99, QString(), 0, QString(), false, false, 0 }));
    menu->actions().front()->trigger();
    delete menu;
}

void AppHomeTest::saveWorkspaceDialog()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    pdf::PDFAppDocumentActions::setOpenDocumentsProvider([&sample](const QWidget*)
    {
        std::vector<pdf::PDFAppOpenDocument> documents(3);
        documents[0].path = sample.projectReport;
        documents[0].page = 12;
        documents[0].pageCount = 24;
        documents[1].path = sample.invoice;
        documents[1].page = 1;
        documents[1].pageCount = 3;
        documents[1].isModified = true;
        documents[2].title = QStringLiteral("Untitled 1");
        return documents;
    });
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QVERIFY(page->getSaveWorkspaceButton()->isEnabled());
    page->getSaveWorkspaceButton()->click();
    HomeFixture::settle();

    QPointer<pdf::PDFAppSaveWorkspaceDialog> dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog());
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->windowTitle().isEmpty(), false);
    const QList<QCheckBox*> checkBoxes = dialog->getDocumentCheckBoxes();
    QCOMPARE(checkBoxes.size(), 3);
    QVERIFY(checkBoxes[0]->isChecked() && checkBoxes[0]->isEnabled());
    QVERIFY(checkBoxes[1]->isChecked() && checkBoxes[1]->isEnabled());
    QVERIFY(!checkBoxes[2]->isChecked() && !checkBoxes[2]->isEnabled());
    QCOMPARE(dialog->getSelectionText(), QStringLiteral("2 of 3 selected"));

    // The texts of the row of a document without a file are text/disabled (Untitled 1
    // of 941:136915), the texts of the other rows primary and muted
    for (int i = 0; i < checkBoxes.size(); ++i)
    {
        const QList<pdf::PDFAppElidedLabel*> labels = checkBoxes[i]->parentWidget()->findChildren<pdf::PDFAppElidedLabel*>();
        QCOMPARE(labels.size(), 2);
        const bool isDisabled = i == 2;
        QCOMPARE(labels[0]->getTextColor(), isDisabled ? pdf::PDFThemeColors::TextDisabled : pdf::PDFThemeColors::TextPrimary);
        QCOMPARE(labels[1]->getTextColor(), isDisabled ? pdf::PDFThemeColors::TextDisabled : pdf::PDFThemeColors::TextMuted);
    }
    QVERIFY(dialog->findChild<pdf::PDFTag*>(QStringLiteral("unsavedTag")));
    QVERIFY(dialog->getSaveButton()->isVisible());
    QVERIFY(!dialog->getSaveButton()->isEnabled());
    QVERIFY(!dialog->getReplaceButton()->isVisible());

    // A name enables Save; no selected document disables it
    QTest::keyClicks(dialog->getNameField(), QStringLiteral("Project review"));
    QVERIFY(dialog->getSaveButton()->isEnabled());
    checkBoxes[0]->click();
    checkBoxes[1]->click();
    QCOMPARE(dialog->getSelectionText(), QStringLiteral("0 of 3 selected"));
    QVERIFY(!dialog->getSaveButton()->isEnabled());
    QTest::keyClick(dialog->getNameField(), Qt::Key_Return);
    QVERIFY(dialog);
    checkBoxes[0]->click();
    QCOMPARE(dialog->getSelectionText(), QStringLiteral("1 of 3 selected"));
    QVERIFY(dialog->getSaveButton()->isEnabled());

    // A whitespace name is not a name
    dialog->getNameField()->setText(QStringLiteral("   "));
    QVERIFY(!dialog->getSaveButton()->isEnabled());

    // A name of an existing workspace (955:147918): the error and Cancel / Replace
    dialog->getNameField()->setText(QStringLiteral("invoices 2026"));
    dialog->getSaveButton()->click();
    HomeFixture::settle();
    QVERIFY(dialog);
    QVERIFY(dialog->isDuplicateShown());
    QCOMPARE(dialog->getErrorText(), QStringLiteral("A workspace named “invoices 2026” already exists (3 documents). Choose another name, or replace it."));
    QVERIFY(dialog->getNameField()->isInvalid());
    QVERIFY(!dialog->getSaveButton()->isVisible());
    QVERIFY(dialog->getReplaceButton()->isVisible());
    QVERIFY(dialog->getCancelButton()->isVisible());
    QVERIFY(dialog->getCancelButton()->isDefault());

    // A change of the name returns to Save
    QTest::keyClick(dialog->getNameField(), Qt::Key_X);
    QVERIFY(!dialog->isDuplicateShown());
    QVERIFY(dialog->getErrorText().isEmpty());
    QVERIFY(dialog->getSaveButton()->isVisible());
    QTest::keyClick(dialog->getNameField(), Qt::Key_Backspace);
    dialog->getSaveButton()->click();
    QVERIFY(dialog->isDuplicateShown());

    // Replace keeps the id of the workspace and replaces its documents
    dialog->getReplaceButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    const std::optional<pdf::PDFWorkspace> replaced = fixture.workspaces().getWorkspace(sample.invoicesId);
    QVERIFY(replaced);
    QCOMPARE(replaced->documents.size(), size_t(1));
    QCOMPARE(replaced->documents[0].path, sample.projectReport);
    QCOMPARE(replaced->documents[0].page, 12);

    // A new name saves a new workspace (Enter in the field)
    fixture.waitIdle();
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showSaveWorkspaceDialog();
    dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog());
    QVERIFY(dialog);
    QTest::keyClicks(dialog->getNameField(), QStringLiteral("Project review"));
    QTest::keyClick(dialog->getNameField(), Qt::Key_Return);
    HomeFixture::settle();
    QVERIFY(!dialog);
    const std::optional<pdf::PDFWorkspace> saved = fixture.workspaces().findWorkspace(QStringLiteral("Project review"));
    QVERIFY(saved);
    QCOMPARE(saved->documentCount, 2);
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getWorkspaceRows().size(), 4);

    // Enter with a name of an existing workspace shows the choice, the same key does
    // not click the new default Cancel (the dialog stays open, the name is kept)
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showSaveWorkspaceDialog();
    dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog());
    QVERIFY(dialog);
    QTest::keyClicks(dialog->getNameField(), QStringLiteral("Project review"));
    QTest::keyClick(dialog->getNameField(), Qt::Key_Return);
    HomeFixture::settle();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(dialog->isDuplicateShown());
    QCOMPARE(dialog->getNameField()->text(), QStringLiteral("Project review"));
    QVERIFY(dialog->getReplaceButton()->isVisible());

    // Enter again: the primary Cancel of the choice
    QTest::keyClick(dialog->getNameField(), Qt::Key_Return);
    HomeFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.workspaces().findWorkspace(QStringLiteral("Project review"))->id, saved->id);
    fixture.waitIdle();

    // Escape cancels
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showSaveWorkspaceDialog();
    dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog());
    QTest::keyClick(dialog, Qt::Key_Escape);
    HomeFixture::settle();
    QVERIFY(!dialog);

    // Without documents the dialog has no list and cannot save
    pdf::PDFAppDocumentActions::setOpenDocumentsProvider(pdf::PDFAppDocumentActions::OpenDocumentsProvider());
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showSaveWorkspaceDialog();
    dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog());
    QVERIFY(dialog->getDocumentCheckBoxes().isEmpty());
    dialog->getNameField()->setText(QStringLiteral("Empty"));
    QVERIFY(!dialog->getSaveButton()->isEnabled());
    dialog->getSaveButton()->click();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSavedId(), qint64(0));
    dialog->reject();
}

void AppHomeTest::renameWorkspaceDialog()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // Through the menu of the workspace (955:150171)
    QAction* rename = HomeTestHelper::findAction(page->getWorkspaceRows()[0]->getMenu(), QStringLiteral("Rename…"));
    rename->trigger();
    HomeFixture::settle();
    QPointer<pdf::PDFAppRenameWorkspaceDialog> dialog = qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(page->getDialog());
    QVERIFY(dialog);
    QCOMPARE(dialog->getWorkspaceId(), sample.thesisId);
    QCOMPARE(dialog->getNameField()->text(), QStringLiteral("Thesis review"));
    QCOMPARE(dialog->getActionButton()->text(), QStringLiteral("Rename"));
    QVERIFY(dialog->getActionButton()->isDefault());
    QCOMPARE(dialog->getCardWidth(), 440);

    // An empty name cannot be submitted, a name of another workspace is refused
    dialog->getNameField()->clear();
    QVERIFY(!dialog->getActionButton()->isEnabled());
    dialog->getNameField()->setText(QStringLiteral("Client contracts"));
    dialog->getActionButton()->click();
    QVERIFY(dialog);
    QCOMPARE(dialog->getErrorText(), QStringLiteral("A workspace named “Client contracts” already exists. Choose another name."));
    QTest::keyClick(dialog->getNameField(), Qt::Key_X);
    QVERIFY(dialog->getErrorText().isEmpty());

    // Rename
    dialog->getNameField()->setText(QStringLiteral("Thesis review – final"));
    QTest::keyClick(dialog->getNameField(), Qt::Key_Return);
    HomeFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.workspaces().getWorkspace(sample.thesisId)->name, QStringLiteral("Thesis review – final"));
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getWorkspaceRows()[0]->getName(), QStringLiteral("Thesis review – final"));

    // Cancel changes nothing
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showRenameWorkspaceDialog(sample.thesisId);
    dialog = qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(page->getDialog());
    dialog->getNameField()->setText(QStringLiteral("Something else"));
    dialog->getCancelButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.workspaces().getWorkspace(sample.thesisId)->name, QStringLiteral("Thesis review – final"));

    // The workspace is deleted by another instance while the dialog is open
    page->showRenameWorkspaceDialog(sample.thesisId);
    dialog = qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(page->getDialog());
    QVERIFY(!fixture.workspaces().deleteWorkspace(sample.thesisId).isError());
    dialog->getNameField()->setText(QStringLiteral("Gone"));
    dialog->getActionButton()->click();
    HomeFixture::settle();

    // The rename of a workspace, which no longer exists, changes nothing and says
    // it (the dialog stays with the error); the list follows the deletion
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->getErrorText(), QStringLiteral("The workspace no longer exists. It may have been deleted in another PDF4QT window."));
    QVERIFY(!fixture.workspaces().findWorkspace(QStringLiteral("Gone")));
    QVERIFY(!fixture.workspaces().getWorkspace(sample.thesisId));
    dialog->getCancelButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getWorkspaceRows().size(), 2);

    // A dialog without a database refuses the name
    pdf::PDFAppRenameWorkspaceDialog detached(nullptr, 1, QStringLiteral("x"), nullptr);
    detached.getNameField()->setText(QStringLiteral("y"));
    detached.getActionButton()->click();
    QCOMPARE(detached.getErrorText(), QStringLiteral("The settings of PDF4QT are not available."));
    pdf::PDFAppRenameLocationDialog detachedLocation(nullptr, 1, QStringLiteral("x"), nullptr);
    detachedLocation.getActionButton()->click();
    QVERIFY(!detachedLocation.getErrorText().isEmpty());
}

void AppHomeTest::deleteWorkspaceDialog()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // 955:149658: the texts, Cancel is the default (Enter)
    page->showDeleteWorkspaceDialog(sample.thesisId);
    QPointer<pdf::PDFAppDeleteWorkspaceDialog> dialog = qobject_cast<pdf::PDFAppDeleteWorkspaceDialog*>(page->getDialog());
    QVERIFY(dialog);
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Delete the workspace “Thesis review”?"));
    QCOMPARE(dialog->accessibleDescription(), QStringLiteral("The workspace is removed from PDF4QT settings and from Recent. Its 4 documents are not touched – they stay where they are on the disk."));
    QCOMPARE(dialog->getDeleteButton()->text(), QStringLiteral("Delete Workspace"));
    QVERIFY(dialog->getCancelButton()->isDefault());
    QVERIFY(!dialog->getDeleteButton()->isDefault());
    QCOMPARE(dialog->getCardWidth(), 440);
    dialog->getCancelButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(fixture.workspaces().getWorkspace(sample.thesisId));

    // Delete Workspace removes it; the documents stay
    page->showDeleteWorkspaceDialog(sample.thesisId);
    dialog = qobject_cast<pdf::PDFAppDeleteWorkspaceDialog*>(page->getDialog());
    dialog->getDeleteButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(!fixture.workspaces().getWorkspace(sample.thesisId));
    QVERIFY(QFile::exists(sample.projectReport));
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getWorkspaceRows().size(), 2);

    // A second dialog replaces the first one
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showDeleteWorkspaceDialog(sample.invoicesId);
    QPointer<QDialog> first = page->getDialog();
    page->showDeleteWorkspaceDialog(sample.contractsId);
    HomeFixture::settle();
    QVERIFY(!first);
    QVERIFY(page->getDialog());
    page->getDialog()->reject();
}

void AppHomeTest::renameLocationDialog()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QAction* rename = HomeTestHelper::findAction(page->getLocationCards()[0]->getMenu(), QStringLiteral("Rename…"));
    QVERIFY(rename);
    rename->trigger();
    QPointer<pdf::PDFAppRenameLocationDialog> dialog = qobject_cast<pdf::PDFAppRenameLocationDialog*>(page->getDialog());
    QVERIFY(dialog);
    QCOMPARE(dialog->getLocationId(), sample.locationId);
    QCOMPARE(dialog->getNameField()->text(), QStringLiteral("Work Projects"));
    dialog->getNameField()->setText(QStringLiteral("Projects at work"));
    dialog->getActionButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.recent().getPinnedLocations().front().name, QStringLiteral("Projects at work"));
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getLocationCards()[0]->getName(), QStringLiteral("Projects at work"));

    // The location is removed by another instance while the dialog is open: the
    // dialog says it (no silent success)
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showRenameLocationDialog(sample.locationId);
    dialog = qobject_cast<pdf::PDFAppRenameLocationDialog*>(page->getDialog());
    QVERIFY(dialog);
    QVERIFY(!fixture.recent().removePinnedLocation(sample.locationId).isError());
    dialog->getNameField()->setText(QStringLiteral("Gone"));
    dialog->getActionButton()->click();
    HomeFixture::settle();
    QVERIFY(dialog);
    QCOMPARE(dialog->getErrorText(), QStringLiteral("The pinned location no longer exists. It may have been removed in another PDF4QT window."));
    dialog->reject();
    HomeFixture::settle();
    QVERIFY(!dialog);
}

void AppHomeTest::clearHistoryDialog_data()
{
    QTest::addColumn<bool>("includePinned");
    QTest::newRow("keep pinned") << false;
    QTest::newRow("remove pinned") << true;
}

void AppHomeTest::clearHistoryDialog()
{
    QFETCH(bool, includePinned);

    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    page->getClearHistoryButton()->click();
    HomeFixture::settle();

    // 941:135742: the facts of the repository, the option of the pinned items
    QPointer<pdf::PDFAppClearHistoryDialog> dialog = qobject_cast<pdf::PDFAppClearHistoryDialog*>(page->getDialog());
    QVERIFY(dialog);
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Clear the Recent history?"));
    QCOMPARE(dialog->getCardWidth(), 520);
    pdf::PDFKeyValueList* facts = dialog->getFacts();
    QCOMPARE(facts->getRowCount(), 3);
    const pdf::PDFRecentClearSummary summary = fixture.recent().getClearHistorySummary(false);
    QCOMPARE(facts->getKey(0), QStringLiteral("Removed"));
    QCOMPARE(facts->getValue(0), QStringLiteral("%1 entries").arg(summary.getRemovedEntries()));
    QCOMPARE(facts->getKey(1), QStringLiteral("Also cleared"));
    QCOMPARE(facts->getValue(1), QStringLiteral("Continue where you left off, Restore last session"));
    QCOMPARE(facts->getKey(2), QStringLiteral("Kept"));
    QCOMPARE(facts->getValue(2), QStringLiteral("3 pinned items, saved workspaces"));
    QCOMPARE(dialog->getPinnedCheckBox()->text(), QStringLiteral("Also remove the pinned items (3)"));
    QVERIFY(!dialog->getPinnedCheckBox()->isChecked());
    QVERIFY(dialog->getCancelButton()->isDefault());
    QCOMPARE(dialog->getClearButton()->text(), QStringLiteral("Clear Recent History"));

    // The option changes the facts
    if (includePinned)
    {
        dialog->getPinnedCheckBox()->click();
        QCOMPARE(facts->getValue(0), QStringLiteral("%1 entries").arg(fixture.recent().getClearHistorySummary(true).getRemovedEntries()));
        QCOMPARE(facts->getValue(2), QStringLiteral("saved workspaces"));
    }

    dialog->getClearButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    fixture.waitIdle();

    QCOMPARE(fixture.recent().getFileCount(), includePinned ? 0 : 3);
    QCOMPARE(fixture.recent().getPinnedCount(), includePinned ? 0 : 3);
    QVERIFY(fixture.recent().getUsedFolders().empty());
    QVERIFY(!fixture.workspaces().getLastSession());
    QCOMPARE(fixture.workspaces().getWorkspaces().size(), size_t(3));
    QVERIFY(fixture.recent().getContinueReading().empty());
    QVERIFY(fixture.recent().hasPinnedLocation(sample.workProjects));

    page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page->isEmptyStateShown());
    QCOMPARE(page->getPinnedSection()->isVisible(), !includePinned);

    // The dialog of an empty history: nothing else to clear, no pinned items
    page->showClearHistoryDialog();
    dialog = qobject_cast<pdf::PDFAppClearHistoryDialog*>(page->getDialog());
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("Nothing else"));
    QCOMPARE(dialog->getPinnedCheckBox()->isVisibleTo(dialog), !includePinned);
    QTest::keyClick(dialog, Qt::Key_Escape);
    HomeFixture::settle();
    QVERIFY(!dialog);

    // Without a database the dialog shows zeros
    pdf::PDFAppClearHistoryDialog detached(nullptr, nullptr);
    QCOMPARE(detached.getFacts()->getValue(0), QStringLiteral("0 entries"));
    QVERIFY(!detached.isPinnedIncluded());
}

void AppHomeTest::restoreAndOpenWorkspace()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // A click on a workspace opens its documents and marks it opened
    const qint64 before = fixture.workspaces().getWorkspace(sample.contractsId)->opened;
    page->getWorkspaceRows()[2]->click();
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.projectReport));
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.invoice));
    QVERIFY(fixture.workspaces().getWorkspace(sample.contractsId)->opened > before);
    fixture.closeMessageBoxes();
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getWorkspaceRows()[0]->getName(), QStringLiteral("Client contracts"));

    // Restore of the last session opens its documents and forgets it
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->getSessionRow()->getRestoreButton()->click();
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.technical));
    QVERIFY(!fixture.workspaces().getLastSession());
    fixture.closeMessageBoxes();
    fixture.waitIdle();
    QVERIFY(!fixture.page<pdf::PDFAppOpenPage>()->getSessionRow()->isVisible());

    // Restore without a session (another instance restored it) does nothing
    fixture.page<pdf::PDFAppOpenPage>()->restoreLastSession();
    QCOMPARE(fixture.closeMessageBoxes(), 0);

    // Recent: the workspace row and Restore of the side column
    fixture.workspaces().saveLastSession({ { sample.presentation, 2 } });
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(recent->getSessionRow()->isVisible());
    QCOMPARE(recent->getSessionRow()->getMeta(), QStringLiteral("1 document · closed %1").arg(pdf::PDFAppHomeFormat::formatTime(fixture.workspaces().getLastSession()->saved, HomeFixture::getNow())));
    recent->getSessionRow()->getRestoreButton()->click();
    HomeFixture::settle();
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.presentation));
    fixture.closeMessageBoxes();
    fixture.waitIdle();
    recent = fixture.page<pdf::PDFAppRecentPage>();
    recent->getWorkspaceRows()[0]->click();
    HomeFixture::settle();
    QVERIFY(qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab()));
    QVERIFY(fixture.closeDocumentTabs() >= 1);
    QCOMPARE(fixture.closeMessageBoxes(), 0);
}

void AppHomeTest::addPinnedLocation()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // Add… is the folder dialog of the system (a handler in the test)
    const QString folder = QDir(fixture.directory.path()).filePath(QStringLiteral("Shared"));
    QVERIFY(QDir().mkpath(folder));
    QString askedFolder = QStringLiteral("not asked");
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget* parent, const QString& start)
    {
        askedFolder = start;
        return parent ? folder : QString();
    });
    page->getAddLocationLink()->click();
    fixture.waitIdle();
    QVERIFY(askedFolder.isEmpty());
    QVERIFY(fixture.recent().hasPinnedLocation(folder));
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getLocationCards().size(), 2);

    // The same folder again: a message, nothing added
    fixture.page<pdf::PDFAppOpenPage>()->addPinnedLocation();
    fixture.waitIdle();
    pdf::PDFAppOpenPage* current = fixture.page<pdf::PDFAppOpenPage>();
    QVERIFY(current->getErrorBox());
    QCOMPARE(current->getErrorBox()->objectName(), QStringLiteral("duplicateLocationBox"));
    QCOMPARE(fixture.recent().getPinnedLocations().size(), size_t(2));
    current->getErrorBox()->close();
    HomeFixture::settle();

    // Canceled dialog
    pdf::PDFAppFileDialogs::setFolderHandler([](QWidget*, const QString&) { return QString(); });
    current->addPinnedLocation();
    QCOMPARE(fixture.recent().getPinnedLocations().size(), size_t(2));
    Q_UNUSED(sample);
}

void AppHomeTest::browseFiles()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // Ctrl+O is the command Open… of the window (file.open): on the pages Home,
    // Open, Recent and on a page without files (About)
    QAction* openAction = fixture.window->getCommandAction(pdf::PDFAppCommands::open());
    QVERIFY(openAction);
    QCOMPARE(openAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_O));
    for (const QString& route : { QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("/recent"), QStringLiteral("/about") })
    {
        fixture.navigate(route);
        int calls = 0;
        pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget* parent, const QString& folder)
        {
            ++calls;
            Q_UNUSED(folder);
            return parent == fixture.window.data() ? QStringList({ sample.presentation, sample.invoice }) : QStringList();
        });
        if (pdf::PDFAppHomeBasePage* page = qobject_cast<pdf::PDFAppHomeBasePage*>(fixture.currentPage()))
        {
            QCOMPARE(page->getBrowseShortcutText(), QKeySequence(Qt::CTRL | Qt::Key_O).toString(QKeySequence::NativeText));
        }
        fixture.window->activateWindow();
        fixture.currentPage()->setFocus();
        QTest::keyClick(fixture.window, Qt::Key_O, Qt::ControlModifier);
        HomeFixture::settle();
        if (calls == 0)
        {
            openAction->trigger();
            HomeFixture::settle();
        }
        QCOMPARE(calls, 1);
        QVERIFY(fixture.window->getRequestedFiles().contains(sample.invoice));
        QCOMPARE(fixture.window->getDocumentTabs().size(), 2);
        QVERIFY(qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab()));
        QCOMPARE(fixture.closeDocumentTabs(), 2);
        QCOMPARE(fixture.closeMessageBoxes(), 0);
    }

    // The shortcut of the user: the action of the window and the hint of the drop
    // zone of Open follow the notification of the database
    fixture.navigate(QStringLiteral("/open"));
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::open(), QStringLiteral("Ctrl+Shift+O")).isError());
    const QString userShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O).toString(QKeySequence::NativeText);
    QTRY_COMPARE(openAction->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    QTRY_VERIFY(fixture.page<pdf::PDFAppOpenPage>()->getDropZone()->findChild<pdf::PDFLabel*>(QStringLiteral("dropHint"))->text().startsWith(userShortcut));
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getBrowseShortcutText(), userShortcut);
    QVERIFY(!fixture.context->getShortcuts().resetShortcut(pdf::PDFAppCommands::open()).isError());
    QTRY_COMPARE(openAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_O));

    // A second browse while the first dialog runs is ignored (re-entrancy)
    fixture.navigate(QStringLiteral("/open"));
    int nested = 0;
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&)
    {
        ++nested;
        if (nested == 1)
        {
            fixture.page<pdf::PDFAppOpenPage>()->browseFiles();
        }
        return QStringList();
    });
    fixture.page<pdf::PDFAppOpenPage>()->browseFiles();
    QCOMPARE(nested, 1);
    QVERIFY(!fixture.window->isBrowsingDocuments());

    // The page deleted while the dialog runs: the window opens the files
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&)
    {
        fixture.navigate(QStringLiteral("/about"));
        return QStringList({ sample.presentation });
    });
    QPointer<pdf::PDFAppOpenPage> openPage = fixture.page<pdf::PDFAppOpenPage>();
    openPage->browseFiles();
    QTRY_VERIFY(openPage.isNull());
    QVERIFY(qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab()));
    QCOMPARE(fixture.closeDocumentTabs(), 1);
    QCOMPARE(fixture.closeMessageBoxes(), 0);


    // openFile of an invalid path does nothing
    fixture.page<pdf::PDFAppOpenPage>()->openFile(QStringLiteral("relative.pdf"));
    QCOMPARE(fixture.closeMessageBoxes(), 0);

    // Opening outside of a window does nothing
    QVERIFY(!pdf::PDFAppDocumentActions::openFiles(nullptr, { sample.invoice }));
    QWidget orphan;
    QVERIFY(!pdf::PDFAppDocumentActions::openFiles(&orphan, { sample.invoice }));
    QVERIFY(!pdf::PDFAppDocumentActions::openFiles(fixture.currentPage(), {}));
    QCOMPARE(pdf::PDFAppDocumentActions::getPaths({ { QStringLiteral("relative.pdf"), 1 }, { sample.invoice, 2 }, { sample.invoice, 3 } }), QStringList({ sample.invoice }));

    // The documents of the window (none before step 3) without a test provider
    QVERIFY(fixture.window->getOpenDocuments().empty());
    QVERIFY(pdf::PDFAppDocumentActions::getOpenDocuments(fixture.currentPage()).empty());
    QVERIFY(pdf::PDFAppDocumentActions::getOpenDocuments(nullptr).empty());

    // The window deleted while the dialog runs: nothing more happens
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&)
    {
        delete fixture.window.data();
        return QStringList({ sample.presentation });
    });
    fixture.page<pdf::PDFAppOpenPage>()->browseFiles();
    QVERIFY(fixture.window.isNull());
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
}

void AppHomeTest::dropFiles()
{
    HomeFixture fixture;
    const QDir root(fixture.directory.path());
    const QString first = root.filePath(QStringLiteral("Drop/First.pdf"));
    const QString second = root.filePath(QStringLiteral("Drop/Second.PDF"));
    QVERIFY(HomeTestHelper::createPdf(first, 2));
    QVERIFY(HomeTestHelper::createPdf(second, 1));

    // Files dropped onto the window are opened and recorded in Recent
    QMimeData* data = HomeTestHelper::createMimeData({ QUrl::fromLocalFile(first), QUrl::fromLocalFile(second) });
    QVERIFY(HomeTestHelper::drop(fixture.window, data));
    delete data;
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles(), QStringList({ first, second }));
    QCOMPARE(fixture.window->getDocumentTabs().size(), 2);
    QCOMPARE(fixture.closeDocumentTabs(), 2);
    QCOMPARE(fixture.closeMessageBoxes(), 0);
    fixture.waitIdle();
    QCOMPARE(fixture.recent().getFileCount(), 2);

    // The drop zone of Open passes the drop to the window (a child, which does not take drops)
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    data = HomeTestHelper::createMimeData({ QUrl::fromLocalFile(first) });
    QPoint position = page->getDropZone()->mapTo(fixture.window, page->getDropZone()->rect().center());
    QDragEnterEvent enter(position, Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window, &enter);
    QVERIFY(enter.isAccepted());
    QCOMPARE(enter.dropAction(), Qt::CopyAction);
    QDropEvent drop(QPointF(position), Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window, &drop);
    delete data;
    HomeFixture::settle();
    QCOMPARE(fixture.closeDocumentTabs(), 1);
    QCOMPARE(fixture.closeMessageBoxes(), 0);

    // A file of the Recent list appears after the drop (notification)
    fixture.waitIdle();
    QCOMPARE(fixture.page<pdf::PDFAppOpenPage>()->getRecentFileRows().size(), 2);

    // Many files: at most MAXIMUM_FILES are taken
    QList<QUrl> many;
    for (int i = 0; i < pdf::PDFAppFileDrop::MAXIMUM_FILES + 5; ++i)
    {
        const QString path = root.filePath(QStringLiteral("Many/%1.pdf").arg(i));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7\n");
        file.close();
        many << QUrl::fromLocalFile(path);
    }
    data = HomeTestHelper::createMimeData(many);
    const pdf::PDFAppFileDrop::Result result = pdf::PDFAppFileDrop::getFiles(data);
    QCOMPARE(result.files.size(), pdf::PDFAppFileDrop::MAXIMUM_FILES);
    QCOMPARE(result.refused.size(), size_t(5));
    QCOMPARE(result.refused.front().refusal, pdf::PDFAppFileDrop::Refusal::TooMany);
    delete data;
}

void AppHomeTest::dropZoneFeedback()
{
    // While local files are dragged over the window, the drop zones (Open: large,
    // Home: compact) are highlighted - bg/accent-subtle with the dashed border in
    // the accent color; nothing is read from the disk during the drag (the file of
    // the drag does not exist); the feedback ends by the leave (also Esc), the drop
    HomeFixture fixture;
    fixture.hideBanner();
    const QString missing = QDir(fixture.directory.path()).filePath(QStringLiteral("Never/created.pdf"));
    QMimeData* files = HomeTestHelper::createMimeData({ QUrl::fromLocalFile(missing) });
    QMimeData text;
    text.setText(QStringLiteral("/not/a/file.pdf"));

    // The colors of a drop zone: the fill at the left middle, the accent ink of its border
    auto check = [](pdf::PDFAppDropZone* zone, bool isActive, qreal scale) -> QString
    {
        const QImage image = HomeTestHelper::render(zone, scale);
        const QColor fill = HomeTestHelper::pixel(image, QPoint(6, zone->height() / 2));
        // bg/accent-subtle is translucent: over the canvas of the rendering
        QColor expectedFill = HomeTestHelper::token(pdf::PDFThemeColors::BgSubtle);
        if (isActive)
        {
            const QColor over = HomeTestHelper::token(pdf::PDFThemeColors::BgAccentSubtle);
            const QColor under = HomeTestHelper::token(pdf::PDFThemeColors::BgCanvas);
            const qreal alpha = over.alphaF();
            expectedFill = QColor::fromRgbF(float(over.redF() * alpha + under.redF() * (1.0 - alpha)), float(over.greenF() * alpha + under.greenF() * (1.0 - alpha)),
                                            float(over.blueF() * alpha + under.blueF() * (1.0 - alpha)));
        }
        if (!HomeTestHelper::isClose(fill, expectedFill, 3))
        {
            return QStringLiteral("fill %1, expected %2").arg(fill.name(), expectedFill.name());
        }

        int accentPixels = 0;
        const QColor accent = HomeTestHelper::token(pdf::PDFThemeColors::BgAccent);
        for (int y = 0; y < image.height(); ++y)
        {
            accentPixels += HomeTestHelper::isClose(image.pixelColor(0, y), accent, 60) ? 1 : 0;
        }
        if (isActive != (accentPixels > image.height() / 4))
        {
            return QStringLiteral("accent pixels of the border %1 of %2").arg(accentPixels).arg(image.height());
        }
        return QString();
    };

    for (const QString& route : { QStringLiteral("/open"), QStringLiteral("/home") })
    {
        fixture.navigate(route);
        pdf::PDFAppDropZone* zone = nullptr;
        if (pdf::PDFAppOpenPage* openPage = fixture.page<pdf::PDFAppOpenPage>())
        {
            zone = openPage->getDropZone();
            QCOMPARE(zone->getVariant(), pdf::PDFAppDropZone::Variant::Large);
        }
        else if (pdf::PDFAppHomePage* homePage = fixture.page<pdf::PDFAppHomePage>())
        {
            zone = homePage->getDropZone();
            QCOMPARE(zone->getVariant(), pdf::PDFAppDropZone::Variant::Compact);
        }
        QVERIFY2(zone && zone->isVisible(), qPrintable(route));
        QVERIFY(!zone->isDropActive());

        // A drag of a text is no drag of files
        const QPoint position = zone->mapTo(fixture.window, zone->rect().center());
        QDragEnterEvent textEnter(position, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fixture.window, &textEnter);
        QVERIFY(!textEnter.isAccepted());
        QVERIFY(!zone->isDropActive());

        QDragEnterEvent enter(position, Qt::CopyAction, files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fixture.window, &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(zone->isDropActive());
        QVERIFY(!QFileInfo::exists(missing));

        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            HomeFixture::settle(1);
            for (const qreal scale : { 1.0, 1.5, 2.0 })
            {
                const QString result = check(zone, true, scale);
                QVERIFY2(result.isEmpty(), qPrintable(QStringLiteral("%1 scale %2: %3").arg(route).arg(scale).arg(result)));
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

        // The move keeps it, the leave (the pointer left the window, Esc) ends it
        QDragMoveEvent move(position + QPoint(5, 5), Qt::CopyAction, files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fixture.window, &move);
        QVERIFY(zone->isDropActive());
        QDragLeaveEvent leave;
        QApplication::sendEvent(fixture.window, &leave);
        QVERIFY(!zone->isDropActive());
        QVERIFY2(check(zone, false, 1.0).isEmpty(), qPrintable(check(zone, false, 1.0)));

        // The drop ends it (the missing file is refused by a message)
        QDragEnterEvent again(position, Qt::CopyAction, files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fixture.window, &again);
        QVERIFY(zone->isDropActive());
        QDropEvent drop(QPointF(position), Qt::CopyAction, files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fixture.window, &drop);
        QVERIFY(!zone->isDropActive());
        HomeFixture::settle();
        QCOMPARE(fixture.closeMessageBoxes(), 1);
    }

    // The page deleted during the drag: the leave and the next drag do not touch it
    QDragEnterEvent enter(QPoint(400, 400), Qt::CopyAction, files, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window, &enter);
    QPointer<pdf::PDFAppDropZone> homeZone = fixture.page<pdf::PDFAppHomePage>()->getDropZone();
    QVERIFY(homeZone && homeZone->isDropActive());
    fixture.navigate(QStringLiteral("/about"));
    QVERIFY(!homeZone);
    QDragLeaveEvent leave;
    QApplication::sendEvent(fixture.window, &leave);
    QVERIFY(!fixture.window->isFileDragActive());

    delete files;
}

void AppHomeTest::dialogScrim()
{
    // The dialogs of the pages cover the body of the window, not the top bar with
    // the tabs and the buttons of the window (941:135742: the scrim starts at y 38),
    // the card is centered over the body
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.window->setGeometry(fixture.window->screen()->availableGeometry());
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    page->getClearHistoryButton()->click();
    HomeFixture::settle();
    QPointer<pdf::PDFMessageBox> dialog = qobject_cast<pdf::PDFMessageBox*>(page->getDialog());
    QVERIFY(dialog);
    QWidget* body = fixture.window->getDialogHost();
    QCOMPARE(dialog->getScrimTarget(), body);
    QVERIFY(dialog->getScrim());
    QCOMPARE(dialog->getScrim()->parentWidget(), body);
    QCOMPARE(body->mapTo(fixture.window, QPoint(0, 0)).y(), fixture.window->getTopBar()->height());
    const QRect cardRect(dialog->mapToGlobal(QPoint(0, 0)) + QPoint(dialog->getShadowMargins().left(), dialog->getShadowMargins().top()), dialog->rect().marginsRemoved(dialog->getShadowMargins()).size());
    const QPoint cardCenter = cardRect.center();

    // Centered over the body, the card stays on the screen (the offscreen screen is
    // smaller than the window)
    QRect expectedCard(QPoint(0, 0), cardRect.size());
    expectedCard.moveCenter(body->mapToGlobal(body->rect().center()));
    const QRect available = fixture.window->screen()->availableGeometry();
    expectedCard.moveLeft(qBound(available.left(), expectedCard.left(), qMax(available.left(), available.right() - expectedCard.width() + 1)));
    expectedCard.moveTop(qBound(available.top(), expectedCard.top(), qMax(available.top(), available.bottom() - expectedCard.height() + 1)));
    const QPoint bodyCenter = expectedCard.center();
    QVERIFY2((cardCenter - bodyCenter).manhattanLength() <= 2, qPrintable(QStringLiteral("%1 %2 / %3 %4").arg(cardCenter.x()).arg(cardCenter.y()).arg(bodyCenter.x()).arg(bodyCenter.y())));

    // The page deleted with the dialog open: the scrim goes with the dialog
    fixture.navigate(QStringLiteral("/home"));
    HomeFixture::settle();
    QTRY_VERIFY(!dialog);
    for (QWidget* child : body->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    {
        QVERIFY2(qobject_cast<pdf::PDFAppTab*>(child), child->metaObject()->className());
    }
}

void AppHomeTest::dropFilesRefused()
{
    HomeFixture fixture;
    const QDir root(fixture.directory.path());
    const QString pdf = root.filePath(QStringLiteral("a.pdf"));
    const QString contentPdf = root.filePath(QStringLiteral("scan.bin"));
    const QString text = root.filePath(QStringLiteral("notes.txt"));
    const QString folder = root.filePath(QStringLiteral("Folder"));
    QVERIFY(HomeTestHelper::createPdf(pdf, 1));
    QVERIFY(HomeTestHelper::createPdf(contentPdf, 1));
    QVERIFY(QDir().mkpath(folder));
    {
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("plain text, not a PDF");
    }

    // Only remote addresses: the drag is refused at once (nothing is read)
    QMimeData* remote = HomeTestHelper::createMimeData({ QUrl(QStringLiteral("https://example.com/a.pdf")), QUrl(QStringLiteral("file://server/share/a.pdf")) });
    QVERIFY(!HomeTestHelper::drop(fixture.window, remote));
    QVERIFY(!pdf::PDFAppFileDrop::hasLocalFiles(remote));
    delete remote;
    QMimeData textOnly;
    textOnly.setText(QStringLiteral("C:/a.pdf"));
    QVERIFY(!pdf::PDFAppFileDrop::hasLocalFiles(&textOnly));
    QVERIFY(!pdf::PDFAppFileDrop::hasLocalFiles(nullptr));
    QVERIFY(pdf::PDFAppFileDrop::getFiles(nullptr).files.isEmpty());
    QVERIFY(pdf::PDFAppFileDrop::getFiles(&textOnly).files.isEmpty());

    // A mix: the PDF files (also by its content) are opened, the rest is listed
    QMimeData* mix = HomeTestHelper::createMimeData({ QUrl::fromLocalFile(pdf), QUrl::fromLocalFile(contentPdf), QUrl::fromLocalFile(text), QUrl::fromLocalFile(folder),
                                                      QUrl::fromLocalFile(root.filePath(QStringLiteral("missing.pdf"))), QUrl(QStringLiteral("https://example.com/b.pdf")),
                                                      QUrl::fromLocalFile(pdf) });
    const pdf::PDFAppFileDrop::Result result = pdf::PDFAppFileDrop::getFiles(mix);
    QCOMPARE(result.files, QStringList({ QFileInfo(pdf).absoluteFilePath(), QFileInfo(contentPdf).absoluteFilePath() }));
    QCOMPARE(result.refused.size(), size_t(4));
    QCOMPARE(result.refused[0].refusal, pdf::PDFAppFileDrop::Refusal::NotPdf);
    QCOMPARE(result.refused[1].refusal, pdf::PDFAppFileDrop::Refusal::Folder);
    QCOMPARE(result.refused[2].refusal, pdf::PDFAppFileDrop::Refusal::Missing);
    QCOMPARE(result.refused[3].refusal, pdf::PDFAppFileDrop::Refusal::Remote);
    for (const auto& refused : result.refused)
    {
        QVERIFY(!refused.text.isEmpty());
        QVERIFY(!pdf::PDFAppFileDrop::getRefusalText(refused.refusal).isEmpty());
    }
    QVERIFY(pdf::PDFAppFileDrop::getRefusalText(pdf::PDFAppFileDrop::Refusal(42)).isEmpty());

    QVERIFY(HomeTestHelper::drop(fixture.window, mix));
    delete mix;
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), 2);
    const QList<pdf::PDFMessageBox*> boxes = fixture.window->findChildren<pdf::PDFMessageBox*>();
    bool hasRefusal = false;
    for (pdf::PDFMessageBox* box : boxes)
    {
        hasRefusal = hasRefusal || (box->isVisible() && box->accessibleName() == QLatin1String("Some items were not opened"));
    }
    QVERIFY(hasRefusal);
    QVERIFY(!fixture.window->getDocumentTabs().isEmpty());
    QCOMPARE(fixture.closeMessageBoxes(), 1);

    // Only refused items: the message, nothing opened
    QMimeData* refusedOnly = HomeTestHelper::createMimeData({ QUrl::fromLocalFile(folder) });
    QVERIFY(HomeTestHelper::drop(fixture.window, refusedOnly));
    delete refusedOnly;
    QCOMPARE(fixture.window->getRequestedFiles().size(), 2);
    QCOMPARE(fixture.closeMessageBoxes(), 1);

    // Network paths are never read
    QVERIFY(pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("//server/share/a")));
    QVERIFY(pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("\\\\server\\share\\a")));
    QVERIFY(!pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("C:/a")));
    QVERIFY(!pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("/a")));
    QVERIFY(!pdf::PDFAppFileDrop::isNetworkPath(QString()));

    // Mixed separators are a UNC path for Windows too; a local file URL with an
    // encoded backslash gives one (file:///%5Cserver%5Cshare%5Ca.pdf -> "/\server\share\a.pdf")
    QVERIFY(pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("/\\server\\share\\a.pdf")));
    QVERIFY(pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("\\/server/share/a.pdf")));
    QVERIFY(pdf::PDFAppFileDrop::isNetworkPath(QStringLiteral("\\\\.\\pipe\\name")));
    QMimeData* encodedUnc = HomeTestHelper::createMimeData({ QUrl(QStringLiteral("file:///%5Cserver%5Cshare%5Ca.pdf")), QUrl(QStringLiteral("file:///%5C%5Cserver%5Cshare%5Cb.pdf")) });
    QVERIFY(!pdf::PDFAppFileDrop::hasLocalFiles(encodedUnc));
    const pdf::PDFAppFileDrop::Result encodedResult = pdf::PDFAppFileDrop::getFiles(encodedUnc);
    delete encodedUnc;
    QVERIFY(encodedResult.files.isEmpty());
    QCOMPARE(encodedResult.refused.size(), size_t(2));
    QCOMPARE(encodedResult.refused[0].refusal, pdf::PDFAppFileDrop::Refusal::Remote);
    QCOMPARE(encodedResult.refused[1].refusal, pdf::PDFAppFileDrop::Refusal::Remote);

    QVERIFY(pdf::PDFAppFileDrop::isPdfFile(QStringLiteral("//server/share/a.PDF")));
    QVERIFY(!pdf::PDFAppFileDrop::isPdfFile(QStringLiteral("//server/share/a.bin")));
    QVERIFY(!pdf::PDFAppFileDrop::isPdfFile(root.filePath(QStringLiteral("missing.bin"))));
    QVERIFY(!pdf::PDFAppFileDrop::isPdfFile(text));
    QVERIFY(pdf::PDFAppFileDrop::isPdfFile(contentPdf));

    // A path with a NUL or of an overlong text is refused as not local
    QMimeData hostile;
    hostile.setUrls({ QUrl::fromLocalFile(QStringLiteral("/a%1b.pdf").arg(QChar(0))), QUrl::fromLocalFile(QStringLiteral("/") + QString(40000, QLatin1Char('a'))) });
    const pdf::PDFAppFileDrop::Result hostileResult = pdf::PDFAppFileDrop::getFiles(&hostile);
    QVERIFY(hostileResult.files.isEmpty());
    for (const auto& refused : hostileResult.refused)
    {
        QVERIFY(refused.text.size() <= 260);
    }

    // Local URLs of another computer: a host, a network path without a host, a relative path
    for (const QString& urlText : { QStringLiteral("file://server/share/a.pdf"), QStringLiteral("file:////server/share/b.pdf"), QStringLiteral("file:relative.pdf") })
    {
        const QUrl url(urlText);
        QVERIFY2(url.isLocalFile(), qPrintable(urlText));
        QMimeData data;
        data.setUrls({ url });
        QVERIFY2(!pdf::PDFAppFileDrop::hasLocalFiles(&data) || urlText == QLatin1String("file:relative.pdf"), qPrintable(urlText));
        const pdf::PDFAppFileDrop::Result urlResult = pdf::PDFAppFileDrop::getFiles(&data);
        QVERIFY2(urlResult.files.isEmpty(), qPrintable(urlText));
        QCOMPARE(urlResult.refused.size(), size_t(1));
        QCOMPARE(urlResult.refused[0].refusal, pdf::PDFAppFileDrop::Refusal::Remote);
    }
}

void AppHomeTest::messageDetailsPlainText()
{
    // The details of a message (refused dropped items, requested files) are paths:
    // a name of a file with a tag is shown as it is, never as rich text
    const QString details = QStringLiteral("<b>bold</b> <img src=\"x.png\">.pdf");
    pdf::PDFMessageBox box(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok);
    box.setDetailedText(details);
    bool isFound = false;
    for (QLabel* label : box.findChildren<QLabel*>())
    {
        if (label->text() == details)
        {
            isFound = true;
            QCOMPARE(label->textFormat(), Qt::PlainText);
        }
    }
    QVERIFY(isFound);
}

void AppHomeTest::openDocumentsRecords()
{
    HomeFixture fixture;
    const QDir root(fixture.directory.path());
    const QString existing = root.filePath(QStringLiteral("Opened/Report.pdf"));
    QVERIFY(HomeTestHelper::createPdf(existing, 3));
    const QString missing = root.filePath(QStringLiteral("Opened/Missing.pdf"));
    const QString folder = root.filePath(QStringLiteral("Opened"));

    // The command line / another process / a page: only existing files are recorded
    fixture.window->openDocuments({ existing, missing, folder }, 2);
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), 3);

    // The dialog open-failed of step 3 (a card dialog) is shown from the event loop
    QTRY_VERIFY(fixture.window->getOpenFailedDialog());
    fixture.window->getOpenFailedDialog()->reject();
    HomeFixture::settle();
    QCOMPARE(fixture.recent().getFileCount(), 1);
    QVERIFY(fixture.recent().getFile(existing));

    // The thumbnail and the page count come from the thumbnailer
    fixture.waitIdle(300);
    const std::optional<pdf::PDFRecentFile> file = fixture.recent().getFile(existing);
    QCOMPARE(file->thumbnailState, pdf::PDFRecentThumbnailState::Ready);
    QCOMPARE(file->pageCount, 3);
    QVERIFY(!fixture.recent().getThumbnail(existing).toImage().isNull());

    // A request of another process is recorded the same way
    pdf::PDFAppInstanceRequest request;
    request.files = QStringList({ existing });
    fixture.window->handleOpenRequest(request);
    HomeFixture::settle();
    QCOMPARE(fixture.recent().getFile(existing)->openCount, qint64(2));
    fixture.closeMessageBoxes();
}

void AppHomeTest::notificationsFromOtherConnection()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QCOMPARE(page->getPinnedFileRows().size(), 3);

    // Another connection (a thread with its own connection, as another instance) pins a file
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    HomeTestHelper::runInThread([database, &sample]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.setPinned(sample.invoice, true);
    });

    // The timer of the notifier delivers it (no explicit poll of waitIdle)
    QTRY_COMPARE_WITH_TIMEOUT(fixture.page<pdf::PDFAppOpenPage>()->getPinnedFileRows().size(), 4, 20000);
    page = fixture.page<pdf::PDFAppOpenPage>();

    // A workspace and a location of another connection
    HomeTestHelper::runInThread([database, &sample]()
    {
        pdf::PDFWorkspaceRepository workspaces(database);
        workspaces.saveWorkspace(QStringLiteral("From elsewhere"), { { sample.presentation, 1 } }, false);
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.addPinnedLocation(QFileInfo(sample.presentation).absolutePath(), QStringLiteral("Elsewhere"));
    });
    fixture.waitIdle(200);
    QCOMPARE(page->getWorkspaceRows().size(), 4);
    QCOMPARE(page->getLocationCards().size(), 2);

    // Recent follows a removal of another connection; a hidden page reads at its show
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    const int count = recent->getChip(pdf::PDFAppRecentPage::Filter::Files)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt();
    HomeTestHelper::runInThread([database, &sample]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.removeFile(sample.presentation);
    });
    fixture.waitIdle(200);
    QCOMPARE(recent->getChip(pdf::PDFAppRecentPage::Filter::Files)->property(pdf::PDFWidgetStyleProperties::CHIP_COUNT).toInt(), count - 1);

    // Home: a tool used by another connection
    fixture.navigate(QStringLiteral("/home"));
    HomeTestHelper::runInThread([database]()
    {
        pdf::PDFToolRepository tools(database);
        tools.setCurrentTime(HomeFixture::at(0, 11, 20));
        tools.recordUsage(QStringLiteral("pages.merge"));
    });
    fixture.waitIdle(200);
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    QCOMPARE(home->getRecentToolRows()[0]->getName(), pdf::PDFDocumentToolCatalog::findTool(u"pages.merge")->getTitle());

    // Everything changed (a gap of the log): the page reads again
    const int refreshes = home->getRefreshCount();
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    fixture.waitIdle();
    QVERIFY(home->getRefreshCount() > refreshes);

    // A topic, which Home does not show, does not read it
    const int afterEverything = home->getRefreshCount();
    Q_EMIT fixture.context->getNotifier()->changed(QStringLiteral("setting"), QStringLiteral("x"));
    HomeFixture::settle();
    QCOMPARE(home->getRefreshCount(), afterEverything);
}

void AppHomeTest::notificationsCoalesced()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    const int before = page->getRefreshCount();

    // A burst of own writes is read once
    pdf::PDFRecentRepository repository = fixture.recent();
    for (int i = 0; i < 20; ++i)
    {
        repository.setPinned(sample.invoice, i % 2 == 0);
    }
    QCoreApplication::processEvents();
    fixture.waitIdle();
    QVERIFY(page->getRefreshCount() - before <= 3);
    QVERIFY(!page->isRefreshPending());

    // A topic, which the page does not show, does not read it
    const int after = page->getRefreshCount();
    Q_EMIT fixture.context->getNotifier()->changed(QStringLiteral("setting"), QStringLiteral("x"));
    HomeFixture::settle();
    QCOMPARE(page->getRefreshCount(), after);

    // A hidden page does not read (the Home tab shows another tab)
    pdf::PDFAppTab* tab = fixture.window->openNewTab();
    HomeFixture::settle();
    QVERIFY(!page->isVisible());
    const int hiddenBefore = page->getRefreshCount();
    QVERIFY(!fixture.recent().getFile(sample.invoice)->pinned);
    repository.setPinned(sample.invoice, true);
    HomeFixture::settle();
    QVERIFY(!page->isRefreshPending());
    QCOMPARE(page->getRefreshCount(), hiddenBefore);

    // ... it reads the database, when it is shown again
    fixture.window->closeTab(tab);
    HomeFixture::settle();
    QVERIFY(page->isVisible());
    QVERIFY(page->getRefreshCount() > hiddenBefore);
    QCOMPARE(page->getPinnedFileRows().size(), 4);
}

void AppHomeTest::thumbnailArrives()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture, false);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();

    // The fake files have no thumbnail (Failed): the placeholder lines
    QCOMPARE(page->getPinnedCards().size(), 3);
    for (pdf::PDFPinnedCard* card : page->getPinnedCards())
    {
        QVERIFY(card->getPixmap().isNull());
    }
    QVERIFY(page->getContinueCard()->getPixmap().isNull());

    // The thumbnail arrives from another connection after the page is shown
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    const QByteArray png = HomeTestHelper::createPng(QColor(0, 160, 0));
    const qint64 modified = QFileInfo(sample.projectReport).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    HomeTestHelper::runInThread([database, &sample, png, modified]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.setThumbnail(sample.projectReport, png, modified, pdf::PDFRecentThumbnailState::Ready, 24);
    });
    fixture.waitIdle(200);

    pdf::PDFPinnedCard* card = nullptr;
    for (pdf::PDFPinnedCard* candidate : page->getPinnedCards())
    {
        if (candidate->getName() == QLatin1String("Project_Report.pdf"))
        {
            card = candidate;
        }
    }
    QVERIFY(card);
    QVERIFY(!card->getPixmap().isNull());
    QCOMPARE(card->getPixmap().toImage().pixelColor(5, 5), QColor(0, 160, 0));
    QVERIFY(!page->getContinueCard()->getPixmap().isNull());
    bool hasListPixmap = false;
    for (int row = 0; row < page->getModel()->rowCount(); ++row)
    {
        const QModelIndex index = page->getModel()->index(row, 0);
        if (index.data(Qt::DisplayRole).toString() == QLatin1String("Project_Report.pdf"))
        {
            hasListPixmap = !index.data(pdf::PDFComponentRoles::PixmapRole).value<QPixmap>().isNull();
        }
    }
    QVERIFY(hasListPixmap);

    // The picture is drawn in the card (pixels of the green thumbnail)
    const QImage image = HomeTestHelper::render(card);
    bool hasGreen = false;
    for (int y = 0; y < image.height() && !hasGreen; ++y)
    {
        for (int x = 0; x < image.width() && !hasGreen; ++x)
        {
            hasGreen = HomeTestHelper::isClose(image.pixelColor(x, y), QColor(0, 160, 0), 6);
        }
    }
    QVERIFY(hasGreen);

    // A new version of the thumbnail (the file has changed) replaces the decoded picture
    const qint64 newModified = modified + 60000;
    {
        QFile file(sample.projectReport);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::fromMSecsSinceEpoch(newModified, QTimeZone::UTC), QFileDevice::FileModificationTime));
    }
    QCOMPARE(QFileInfo(sample.projectReport).lastModified(QTimeZone::UTC).toMSecsSinceEpoch(), newModified);
    const QByteArray bluePng = HomeTestHelper::createPng(QColor(0, 0, 200));
    HomeTestHelper::runInThread([database, &sample, bluePng, newModified]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.setThumbnail(sample.projectReport, bluePng, newModified, pdf::PDFRecentThumbnailState::Ready, 24);
    });
    fixture.waitIdle(200);
    QCOMPARE(card->getPixmap().toImage().pixelColor(5, 5), QColor(0, 0, 200));
    QCOMPARE(page->getContinueCard()->getPixmap().toImage().pixelColor(5, 5), QColor(0, 0, 200));

    // A broken PNG of a hostile row: no picture, no crash
    HomeTestHelper::runInThread([database, &sample, modified]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.setThumbnail(sample.technical, QByteArray("not a png"), modified, pdf::PDFRecentThumbnailState::Ready, 128);
    });
    fixture.waitIdle(200);
    for (pdf::PDFPinnedCard* candidate : page->getPinnedCards())
    {
        if (candidate->getName() == QLatin1String("Technical_Documentation.pdf"))
        {
            QVERIFY(candidate->getPixmap().isNull());
        }
    }
}

void AppHomeTest::thumbnailAfterDeletion()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture, false);

    // The page asks for thumbnails and is deleted before they arrive
    fixture.navigate(QStringLiteral("/recent"));
    QPointer<pdf::PDFAppPage> page = fixture.currentPage();
    QVERIFY(!fixture.recent().setThumbnail(sample.projectReport, QByteArray(), 0, pdf::PDFRecentThumbnailState::Pending, -1).isError());
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(!page);
    const QByteArray png = HomeTestHelper::createPng(Qt::blue);
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    HomeTestHelper::runInThread([database, &sample, png]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.setThumbnail(sample.projectReport, png, 1, pdf::PDFRecentThumbnailState::Ready, 24);
    });
    fixture.waitIdle(300);
    QVERIFY(fixture.page<pdf::PDFAppHomePage>());

    // The window goes with a pending request of the thumbnailer and of the folder information
    fixture.navigate(QStringLiteral("/recent"));
    delete fixture.window.data();
    HomeFixture::settle();
    fixture.waitIdle(100);
}

void AppHomeTest::realThumbnailer()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle(400);
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();

    // The thumbnails of the real documents are rendered by the worker and read by the page
    for (pdf::PDFPinnedCard* card : page->getPinnedCards())
    {
        QVERIFY2(!card->getPixmap().isNull(), qPrintable(card->getName()));
    }
    QVERIFY(!page->getContinueCard()->getPixmap().isNull());

    // The red quarter of page 1 is in the picture
    const QImage image = page->getPinnedCards()[0]->getPixmap().toImage();
    QVERIFY(HomeTestHelper::isClose(image.pixelColor(image.width() / 8, image.height() / 8), Qt::red, 10));
}

void AppHomeTest::hostileRows()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture, false);
    pdf::PDFDatabase* database = fixture.context->getDatabase();

    // Rows of another version or a damaged file, huge texts, invalid values
    const QString hugeName = QString(20000, QLatin1Char('N')) + QStringLiteral("\n\t") + QChar(0x202E) + QStringLiteral(".pdf");
    const QStringList statements = {
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('relative.pdf', 'r', '/', 1)"),
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('', 'empty', '/', 3)"),
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened, open_count, pinned, page_count, file_size, last_page, reached_end, locked, thumbnail_state, view_state) "
                       "VALUES ('/hostile/types.pdf', zeroblob(10), 42, 'yesterday', 'many', 'yes', -5, 1.5, 'x', 7, 2, 99, zeroblob(100))"),
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened, page_count, last_page, pinned, pin_order) VALUES ('/hostile/huge.pdf', '%1', '/hostile/%2', 9223372036854775807, 9223372036854775807, 2000000000, 1, -7)")
            .arg(hugeName, QString(20000, QLatin1Char('f'))),
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened, thumbnail, thumbnail_state, thumbnail_mtime) VALUES ('/hostile/thumb.pdf', 't', '/hostile', 5, randomblob(5000), 1, 1)"),
        QStringLiteral("INSERT INTO recent_folder(path, last_used, kind) VALUES ('/hostile/folder', 'x', 99)"),
        QStringLiteral("INSERT INTO recent_folder(path, last_used, kind) VALUES ('/hostile/%1', -5, 1)").arg(QString(5000, QLatin1Char('d'))),
        QStringLiteral("INSERT INTO pinned_location(path, name, kind, position) VALUES ('/hostile/location', x'00FF', 'z', 'last')"),
        QStringLiteral("INSERT INTO pinned_location(path, name, kind, position) VALUES ('/hostile/location2', '%1', 2, -1)").arg(QString(3000, QLatin1Char('L'))),
        QStringLiteral("INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('', 'x', 'y', 'z', 0)"),
        QStringLiteral("INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('%1', 1, 9223372036854775807, -1, 0)").arg(QString(4000, QLatin1Char('W'))),
        QStringLiteral("INSERT INTO workspace_document(workspace_id, position, path, page) VALUES ((SELECT MAX(id) FROM workspace), 0, 'relative', 'x')"),
        QStringLiteral("INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('unknown.tool', 1, 1)"),
        QStringLiteral("INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('review.compare', 'x', -4)"),
    };
    for (const QString& sql : statements)
    {
        const QByteArray utf8 = sql.toUtf8();
        pdf::PDFDatabaseStatement statement(database->getConnection(), utf8.constData());
        QVERIFY2(statement.execute(), qPrintable(sql.left(200)));
    }
    Q_EMIT fixture.context->getNotifier()->everythingChanged();

    // Every page shows them without a crash, the texts are bounded
    for (const QString& route : { QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("/recent"), QStringLiteral("/recent?filter=folders"),
                                  QStringLiteral("/recent?filter=workspaces"), QStringLiteral("/recent?filter=pinned&sort=name"), QStringLiteral("/recent?sort=folder") })
    {
        fixture.navigate(route);
        fixture.waitIdle();
        QVERIFY(fixture.currentPage());
        QVERIFY2(qobject_cast<pdf::PDFAppHomeBasePage*>(fixture.currentPage()), qPrintable(route));
        const QImage image = HomeTestHelper::render(fixture.window);
        QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);

        for (pdf::PDFRecentFileRow* row : fixture.currentPage()->findChildren<pdf::PDFRecentFileRow*>())
        {
            QVERIFY(row->getFileName().size() <= pdf::PDFAppHomeFormat::MAXIMUM_DISPLAY_LENGTH);
            QVERIFY(row->getPath().size() <= pdf::PDFAppHomeFormat::MAXIMUM_DISPLAY_LENGTH);
            QVERIFY(!row->getFileName().contains(QLatin1Char('\n')));
        }
        for (pdf::PDFWorkspaceRow* row : fixture.currentPage()->findChildren<pdf::PDFWorkspaceRow*>())
        {
            QVERIFY(row->getName().size() <= pdf::PDFAppHomeFormat::MAXIMUM_DISPLAY_LENGTH);
        }
        if (pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>())
        {
            for (const pdf::PDFAppRecentItem& item : recent->getModel()->getItems())
            {
                QVERIFY(item.name.size() <= pdf::PDFAppHomeFormat::MAXIMUM_DISPLAY_LENGTH);
                QVERIFY(item.details.size() <= 2 * pdf::PDFAppHomeFormat::MAXIMUM_DISPLAY_LENGTH);
            }
        }
    }

    // Menus and dialogs of the hostile items
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    for (pdf::PDFWorkspaceRow* row : open->getWorkspaceRows())
    {
        open->performAction(pdf::PDFAppHomeItem(), pdf::PDFAppHomeAction::CopyPath);
        QVERIFY(row->getMenu());
    }
    for (pdf::PDFFolderCard* card : open->getLocationCards())
    {
        QVERIFY(card->getName().size() <= pdf::PDFAppHomeFormat::MAXIMUM_DISPLAY_LENGTH);
    }
}

void AppHomeTest::geometryHome()
{
    // pdf4qt-home 28:4 (1440 x 961): positions in the window, the page has the
    // height of the frame (741) without a scroll bar
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 961));
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/home"));
    fixture.hideBanner();
    fixture.waitIdle();
    pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
    QWidget* window = fixture.window;

    // The subtitle 28:35 stresses "PDF" (Semi Bold text/accent), the whole line is shown
    pdf::PDFAppHeaderText* subtitle = fixture.homeTab()->getHeader()->getSubtitleLabel();
    QCOMPARE(page->getHeaderSubtitleAccent(), QStringLiteral("PDF"));
    QCOMPARE(subtitle->getAccentIndex(), page->getHeaderSubtitle().indexOf(QStringLiteral("PDF")));
    QVERIFY(!subtitle->isElided());

    auto check = [](const QRect& actual, const QRect& expected, int tolerance, const char* name)
    {
        const bool isOk = qAbs(actual.x() - expected.x()) <= tolerance && qAbs(actual.y() - expected.y()) <= tolerance &&
                          qAbs(actual.width() - expected.width()) <= tolerance && qAbs(actual.height() - expected.height()) <= tolerance;
        if (!isOk)
        {
            qWarning("%s: actual %d,%d %dx%d expected %d,%d %dx%d", name, actual.x(), actual.y(), actual.width(), actual.height(), expected.x(), expected.y(), expected.width(), expected.height());
        }
        return isOk;
    };

    const QList<pdf::PDFQuickActionTile*> tiles = page->getQuickActionTiles();
    QVERIFY(check(HomeTestHelper::rectIn(tiles[0], window), QRect(242, 180, 224, 88), 1, "tile Open PDF"));
    QVERIFY(check(HomeTestHelper::rectIn(tiles[4], window), QRect(1184, 180, 224, 88), 2, "tile Edit Content"));
    QVERIFY(check(HomeTestHelper::rectIn(tiles[5], window), QRect(242, 280, 224, 88), 1, "tile Merge"));
    QVERIFY(check(HomeTestHelper::rectIn(tiles[12], window), QRect(713, 380, 224, 88), 2, "tile All Tools"));

    // The tile Remove Hidden Data has the short description of the frame ("Strip
    // metadata", tools.md): the description of the catalog needs two lines more
    // than the frame and the text reaches into the padding of the tile
    bool isSanitizeFound = false;
    for (pdf::PDFQuickActionTile* tile : tiles)
    {
        if (tile->objectName() == QLatin1String("tile-protect.sanitize"))
        {
            QCOMPARE(tile->getDescription(), QStringLiteral("Strip metadata"));
            isSanitizeFound = true;
        }
        else if (const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(tile->objectName().mid(5)))
        {
            QCOMPARE(tile->getDescription(), entry->getDescription());
        }
    }
    QVERIFY(isSanitizeFound);

    QVERIFY(check(HomeTestHelper::rectIn(page->getRecentFilesCard(), window), QRect(242, 522, 397, 368), 3, "recent files card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getRecentFileRows()[0], window), QRect(251, 531, 381, 51), 3, "recent file row"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getRecentFileRows()[1], window), QRect(251, 582, 381, 51), 3, "recent file row 2"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getDropZone(), window), QRect(251, 786, 381, 43), 3, "drop zone"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getResourceCards()[0], window), QRect(663, 522, 112, 154), 3, "resource card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSupportCard(), window), QRect(663, 726, 360, 164), 3, "support card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSupportRows()[0], window), QRect(670, 733, 348, 50), 3, "support row"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getRecentToolsCard(), window), QRect(1047, 522, 361, 368), 3, "recent tools card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getRecentToolRows()[0], window), QRect(1056, 531, 343, 48), 3, "recent tool row"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getRecentToolRows()[1], window), QRect(1056, 581, 343, 48), 3, "recent tool row 2"));

    // The page fits the window without a vertical scroll bar (as the frame)
    QCOMPARE(page->height(), 741);
    QVERIFY2(!fixture.homeTab()->getPageScrollArea()->verticalScrollBar()->isVisible(),
             qPrintable(QStringLiteral("page %1, viewport %2").arg(page->height()).arg(fixture.homeTab()->getPageScrollArea()->viewport()->height())));
}

void AppHomeTest::geometryOpen()
{
    // pdf4qt-open 61:1201 (1440 x 1303)
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 1303));
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.hideBanner();
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QWidget* window = fixture.window;

    auto check = [](const QRect& actual, const QRect& expected, int tolerance, const char* name)
    {
        const bool isOk = qAbs(actual.x() - expected.x()) <= tolerance && qAbs(actual.y() - expected.y()) <= tolerance &&
                          qAbs(actual.width() - expected.width()) <= tolerance && qAbs(actual.height() - expected.height()) <= tolerance;
        if (!isOk)
        {
            qWarning("%s: actual %d,%d %dx%d expected %d,%d %dx%d", name, actual.x(), actual.y(), actual.width(), actual.height(), expected.x(), expected.y(), expected.width(), expected.height());
        }
        return isOk;
    };

    QVERIFY(check(HomeTestHelper::rectIn(page->getDropZone(), window), QRect(242, 149, 782, 246), 2, "drop zone"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getDropZone()->getBrowseButton(), window), QRect(559, 305, 148, 36), 2, "browse files"));
    QCOMPARE(page->getDropZone()->height(), 246);
    QCOMPARE(HomeTestHelper::rectIn(page->getDropZone(), window).top(), 149);
    QCOMPARE(HomeTestHelper::rectIn(page->getDropZone()->getBrowseButton(), window).top(), 305);
    QCOMPARE(HomeTestHelper::rectIn(page->getDropZone()->findChild<QWidget*>(QStringLiteral("dropTitle")), window).top(), 250);
    QCOMPARE(page->getDropZone()->findChild<QWidget*>(QStringLiteral("dropTitle"))->height(), 19);
    QCOMPARE(HomeTestHelper::rectIn(page->getDropZone()->findChild<QWidget*>(QStringLiteral("dropHint")), window).top(), 357);
    QCOMPARE(page->getDropZone()->findChild<QWidget*>(QStringLiteral("dropHint"))->height(), 13);
    QVERIFY(check(HomeTestHelper::rectIn(page->getFolderCards()[0], window), QRect(242, 449, 253, 84), 2, "folder card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getFolderCards()[4], window), QRect(507, 545, 253, 84), 2, "folder card 5"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getPinnedFileRows()[0], window), QRect(251, 693, 764, 51), 3, "pinned row"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getWorkspacesCard(), window), QRect(1048, 179, 360, 272), 3, "workspaces card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSessionRow(), window), QRect(1057, 188, 342, 54), 2, "session row"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSessionRow()->getRestoreButton(), window), QRect(1303, 199, 85, 32), 3, "restore button"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getWorkspaceRows()[0], window), QRect(1057, 244, 342, 52), 2, "workspace row"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSaveWorkspaceButton(), window), QRect(1057, 406, 342, 36), 3, "save workspace"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getLocationsCard(), window), QRect(1048, 505, 360, 102), 3, "locations card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getLocationCards()[0], window), QRect(1057, 514, 342, 84), 2, "location card"));
}

void AppHomeTest::geometryRecent()
{
    // pdf4qt-recent 80:910 (1440 x 948)
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 948));
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.hideBanner();
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QWidget* window = fixture.window;

    auto check = [](const QRect& actual, const QRect& expected, int tolerance, const char* name)
    {
        const bool isOk = qAbs(actual.x() - expected.x()) <= tolerance && qAbs(actual.y() - expected.y()) <= tolerance &&
                          qAbs(actual.width() - expected.width()) <= tolerance && qAbs(actual.height() - expected.height()) <= tolerance;
        if (!isOk)
        {
            qWarning("%s: actual %d,%d %dx%d expected %d,%d %dx%d", name, actual.x(), actual.y(), actual.width(), actual.height(), expected.x(), expected.y(), expected.width(), expected.height());
        }
        return isOk;
    };

    QVERIFY(check(HomeTestHelper::rectIn(page->getSearchField(), window), QRect(242, 149, 473, 36), 3, "search"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSortBox(), window), QRect(727, 151, 154, 32), 6, "sort"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getClearHistoryButton(), window), QRect(893, 151, 151, 32), 6, "clear history"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getChip(pdf::PDFAppRecentPage::Filter::All), window), QRect(242, 197, 64, 32), 4, "chip all"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getPinnedCards()[0], window), QRect(242, 283, 259, 100), 2, "pinned card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getPinnedCards()[2], window), QRect(785, 283, 259, 100), 2, "pinned card 3"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getHistoryCard(), window), QRect(242, 407, 802, 470), 3, "history"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getContinueCard(), window), QRect(1068, 179, 340, 156), 3, "continue card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getContinueCard()->getContinueButton(), window), QRect(1177, 288, 167, 32), 4, "continue button"));
    QCOMPARE(page->getContinueCard()->height(), 156);
    QCOMPARE(HomeTestHelper::rectIn(page->getContinueCard()->getContinueButton(), window).top(), 288);
    QCOMPARE(HomeTestHelper::rectIn(page->getContinueCard()->getContinueButton(), window).top() -
             HomeTestHelper::rectIn(page->getContinueCard()->findChild<QWidget*>(QStringLiteral("continueBar")), window).bottom() - 1, 14);
    QVERIFY(check(HomeTestHelper::rectIn(page->getWorkspacesCard(), window), QRect(1068, 389, 340, 234), 3, "workspaces card"));
    QVERIFY(check(HomeTestHelper::rectIn(page->getSessionRow(), window), QRect(1077, 398, 322, 54), 3, "session row"));

    // The rows of the history (header 33, items 56) beside the scroll bar
    pdf::PDFAppRecentListView* view = page->getListView();
    const QRect header = view->visualRect(page->getModel()->index(0, 0));
    const QRect item = view->visualRect(page->getModel()->index(1, 0));
    QCOMPARE(header.height(), 33);
    QCOMPARE(item.height(), 56);
    QVERIFY(check(QRect(view->viewport()->mapTo(window, item.topLeft()), item.size()), QRect(251, 446, 774, 56), 4, "history item"));
}

void AppHomeTest::geometryDialogs()
{
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 1303));
    HomeSample sample = HomeTestHelper::fill(fixture);
    pdf::PDFAppDocumentActions::setOpenDocumentsProvider([&sample](const QWidget*)
    {
        std::vector<pdf::PDFAppOpenDocument> documents(3);
        documents[0].path = sample.projectReport;
        documents[0].page = 12;
        documents[0].pageCount = 24;
        documents[1].path = sample.invoice;
        documents[1].isModified = true;
        documents[2].title = QStringLiteral("Untitled 1");
        return documents;
    });
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    struct Case
    {
        std::function<void()> show;
        QSize size;
        int tolerance;
        const char* name;
    };

    const Case cases[] = {
        { [&]() { page->showDeleteWorkspaceDialog(sample.thesisId); }, QSize(440, 161), 6, "delete" },
        { [&]() { page->showRenameWorkspaceDialog(sample.thesisId); }, QSize(440, 215), 6, "rename" },
        { [&]() { page->showClearHistoryDialog(); }, QSize(520, 299), 8, "clear history" },
        { [&]() { page->showSaveWorkspaceDialog(); }, QSize(520, 434), 16, "save workspace" },
    };

    for (const Case& testCase : cases)
    {
        testCase.show();
        HomeFixture::settle();
        pdf::PDFAppCardDialog* dialog = qobject_cast<pdf::PDFAppCardDialog*>(page->getDialog());
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        const QRect card = dialog->rect().marginsRemoved(dialog->getShadowMargins());
        const bool isOk = qAbs(card.width() - testCase.size.width()) <= 1 && qAbs(card.height() - testCase.size.height()) <= testCase.tolerance;
        QVERIFY2(isOk, qPrintable(QStringLiteral("%1: %2x%3").arg(QString::fromLatin1(testCase.name)).arg(card.width()).arg(card.height())));

        // The buttons lie in the card, the primary one is the default
        for (QPushButton* button : dialog->findChildren<QPushButton*>())
        {
            if (button->isVisible())
            {
                QVERIFY(card.contains(QRect(button->mapTo(dialog, QPoint(0, 0)), button->size())));
            }
        }
        dialog->reject();
        HomeFixture::settle();
    }
}

void AppHomeTest::pixels_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<qreal>("ratio");
    QTest::newRow("light 1x") << false << 1.0;
    QTest::newRow("light 2x") << false << 2.0;
    QTest::newRow("dark 1x") << true << 1.0;
    QTest::newRow("dark 1.5x") << true << 1.5;
}

void AppHomeTest::pixels()
{
    QFETCH(bool, dark);
    QFETCH(qreal, ratio);

    HomeFixture fixture;
    HomeTestHelper::fill(fixture, false);
    pdf::PDFThemeManager::getInstance()->setScheme(dark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    HomeFixture::settle();

    // Open: the drop zone bg/subtle, its dashed border border/strong, the session row accent/blue/bg
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    QImage image = HomeTestHelper::render(open->getDropZone(), ratio);
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(40, 40)), HomeTestHelper::token(pdf::PDFThemeColors::BgSubtle)));
    int borderPixels = 0;
    int gapPixels = 0;
    for (int x = 20; x < 200; ++x)
    {
        const QColor color = HomeTestHelper::pixel(image, QPoint(x, 0));
        borderPixels += HomeTestHelper::isClose(color, HomeTestHelper::token(pdf::PDFThemeColors::BorderStrong), 12) ? 1 : 0;
        gapPixels += HomeTestHelper::isClose(color, HomeTestHelper::token(pdf::PDFThemeColors::BgSubtle), 12) ? 1 : 0;
    }
    QVERIFY2(borderPixels > 60 && gapPixels > 40, qPrintable(QStringLiteral("dashes %1 gaps %2").arg(borderPixels).arg(gapPixels)));
    QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);

    image = HomeTestHelper::render(open->getSessionRow(), ratio);
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(open->getSessionRow()->width() / 2, 4)), HomeTestHelper::token(pdf::PDFThemeColors::AccentBlueBg)));
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(open->getSessionRow()->width() / 2, 0)), HomeTestHelper::token(pdf::PDFThemeColors::AccentBlueBorder), 8));
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(27, 27)), HomeTestHelper::token(pdf::PDFThemeColors::BgSurface), 6)
            || HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(20, 20)), HomeTestHelper::token(pdf::PDFThemeColors::BgSurface), 6));

    // Recent: the session row bg/subtle, the continue card accent/blue/bg, the empty state accent/neutral/bg
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    image = HomeTestHelper::render(recent->getSessionRow(), ratio);
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(recent->getSessionRow()->width() / 2, 4)), HomeTestHelper::token(pdf::PDFThemeColors::BgSubtle)));
    image = HomeTestHelper::render(recent->getContinueCard(), ratio);
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(recent->getContinueCard()->width() - 6, recent->getContinueCard()->height() - 6)), HomeTestHelper::token(pdf::PDFThemeColors::AccentBlueBg)));
    const QRect bar = recent->getContinueCard()->getBarRect();
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(bar.left() + 4, bar.center().y())), HomeTestHelper::token(pdf::PDFThemeColors::BgAccent), 12));
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(bar.right() - 4, bar.center().y())), HomeTestHelper::token(pdf::PDFThemeColors::BorderDefault), 12));
    QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);

    QVERIFY(!fixture.recent().clearHistory(true).isError());
    fixture.waitIdle();
    QVERIFY(recent->isEmptyStateShown());
    image = HomeTestHelper::render(recent->getEmptyState(), ratio);
    const QWidget* circle = recent->getEmptyState()->findChild<QWidget*>(QStringLiteral("emptyIcon"));
    QVERIFY(circle);
    const QPoint circlePoint = circle->mapTo(recent->getEmptyState(), QPoint(10, circle->height() / 2));
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, circlePoint), HomeTestHelper::token(pdf::PDFThemeColors::AccentNeutralBg), 6));
    QCOMPARE(HomeTestHelper::countLeakPixels(HomeTestHelper::render(fixture.window, ratio)), 0);

    // Home: the compact drop zone bg/subtle with border/default dashes
    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    image = HomeTestHelper::render(home->getDropZone(), ratio);
    QVERIFY(HomeTestHelper::isClose(HomeTestHelper::pixel(image, QPoint(10, home->getDropZone()->height() / 2)), HomeTestHelper::token(pdf::PDFThemeColors::BgSubtle)));
    QCOMPARE(HomeTestHelper::countLeakPixels(HomeTestHelper::render(fixture.window, ratio)), 0);

    // A dialog: the card bg/surface
    fixture.navigate(QStringLiteral("/recent"));
    fixture.page<pdf::PDFAppRecentPage>()->showClearHistoryDialog();
    HomeFixture::settle();
    QDialog* dialog = fixture.page<pdf::PDFAppRecentPage>()->getDialog();
    image = HomeTestHelper::render(dialog, ratio);
    QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);
    dialog->reject();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppHomeTest::rightToLeft()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    QApplication::setLayoutDirection(Qt::RightToLeft);
    HomeFixture::settle();

    // Open: the side column at the left, the drop zone at the right
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    QVERIFY(HomeTestHelper::rectIn(open->getWorkspacesCard(), fixture.window).right() < HomeTestHelper::rectIn(open->getDropZone(), fixture.window).left());
    QCOMPARE(open->getFolderCards()[0]->layoutDirection(), Qt::RightToLeft);

    // Recent: the continue card at the left, the rows of the list mirrored
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(HomeTestHelper::rectIn(recent->getContinueCard(), fixture.window).right() < HomeTestHelper::rectIn(recent->getHistoryCard(), fixture.window).left());
    const QModelIndex item = recent->getModel()->index(1, 0);
    const QRect itemRect = recent->getListView()->visualRect(item);
    const QRect star = recent->getDelegate()->getActionRect(recent->getListView(), item, pdf::PDFRecentItemDelegate::PinAction);
    QVERIFY(star.center().x() < itemRect.center().x());

    // A dialog: Delete Workspace at the right, Cancel at the left
    fixture.navigate(QStringLiteral("/open"));
    open = fixture.page<pdf::PDFAppOpenPage>();
    open->showDeleteWorkspaceDialog(open->getWorkspaceRows()[0]->property("pdf4qtId").toLongLong());
    HomeFixture::settle();
    pdf::PDFAppDeleteWorkspaceDialog* dialog = qobject_cast<pdf::PDFAppDeleteWorkspaceDialog*>(open->getDialog());
    QVERIFY(dialog);
    QVERIFY(dialog->getDeleteButton()->mapTo(dialog, QPoint(0, 0)).x() > dialog->getCancelButton()->mapTo(dialog, QPoint(0, 0)).x());
    QCOMPARE(HomeTestHelper::countLeakPixels(HomeTestHelper::render(dialog)), 0);
    dialog->reject();

    // The pictures have no leak color in right-to-left
    for (const QString& route : { QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("/recent") })
    {
        fixture.navigate(route);
        fixture.waitIdle();
        QCOMPARE(HomeTestHelper::countLeakPixels(HomeTestHelper::render(fixture.window)), 0);
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppHomeTest::minimalWindow()
{
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(720, 480));
    HomeTestHelper::fill(fixture);
    fixture.window->resize(720, 480);
    HomeFixture::settle();

    for (const QString& route : { QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("/recent") })
    {
        fixture.navigate(route);
        fixture.waitIdle();
        pdf::PDFAppHomeBasePage* page = qobject_cast<pdf::PDFAppHomeBasePage*>(fixture.currentPage());
        QVERIFY(page);
        pdf::PDFAppScrollArea* area = fixture.homeTab()->getPageScrollArea();

        // No horizontal scroll bar, the columns are below each other
        QVERIFY2(!area->horizontalScrollBar()->isVisible(), qPrintable(route));
        QVERIFY(page->width() <= area->viewport()->width());
        for (pdf::PDFAppColumns* columns : page->findChildren<pdf::PDFAppColumns*>())
        {
            QVERIFY2(columns->isStacked(), qPrintable(route));
        }

        // No visible widget of the page leaves it at the right
        for (QWidget* widget : page->findChildren<QWidget*>())
        {
            if (widget->isVisible() && !widget->isWindow() && widget->width() > 0 && !qobject_cast<QScrollBar*>(widget) && !widget->inherits("QAbstractScrollArea"))
            {
                const QRect rect = HomeTestHelper::rectIn(widget, page);
                QVERIFY2(rect.right() <= page->width() + 1, qPrintable(QStringLiteral("%1 %2 %3").arg(route, QString::fromLatin1(widget->metaObject()->className()), widget->objectName())));
            }
        }
        QCOMPARE(HomeTestHelper::countLeakPixels(HomeTestHelper::render(fixture.window)), 0);
    }

    // Recent: the sort and Clear history… below the search
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(recent->getSortBox()->mapTo(recent, QPoint(0, 0)).y() > recent->getSearchField()->mapTo(recent, QPoint(0, 0)).y());
    QVERIFY(recent->getHistoryCard()->height() >= 240);

    // Back to the full window: beside each other again
    fixture.window->resize(1440, 1000);
    HomeFixture::settle();
    QVERIFY(!recent->getColumns()->isStacked());
}

void AppHomeTest::keyboard()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // Tab reaches Browse Files…, the folder cards, the rows and their buttons, the workspaces
    QSet<QWidget*> reached;
    page->getDropZone()->getBrowseButton()->setFocus(Qt::TabFocusReason);
    for (int i = 0; i < 200; ++i)
    {
        QWidget* focus = QApplication::focusWidget();
        if (!focus || !page->isAncestorOf(focus))
        {
            break;
        }
        reached.insert(focus);
        QTest::keyClick(focus, Qt::Key_Tab);
    }
    QVERIFY(reached.contains(page->getFolderCards()[0]));
    QVERIFY(reached.contains(page->getRecentFileRows()[0]));
    QVERIFY(reached.contains(page->getRecentFileRows()[0]->getStarButton()));
    QVERIFY(reached.contains(page->getRecentFileRows()[0]->getMenuButton()));

    // Enter on a focused row opens the file; Space on the star pins it
    pdf::PDFRecentFileRow* row = page->getRecentFileRows()[1];
    row->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row, Qt::Key_Return);
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), 1);
    fixture.closeMessageBoxes();
    fixture.waitIdle();
    page = fixture.page<pdf::PDFAppOpenPage>();
    row = nullptr;
    for (pdf::PDFRecentFileRow* candidate : page->getRecentFileRows())
    {
        row = (!row && !candidate->isPinned()) ? candidate : row;
    }
    QVERIFY(row);
    const QString path = row->property("pdf4qtPath").toString();
    row->getStarButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row->getStarButton(), Qt::Key_Space);
    fixture.waitIdle();
    QVERIFY(fixture.recent().getFile(path)->pinned);
    QCOMPARE(fixture.recent().getPinnedCount(), 4);

    // Recent: the chips by the keyboard (an exclusive group), the search field
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    recent->getChip(pdf::PDFAppRecentPage::Filter::Files)->setFocus(Qt::TabFocusReason);
    QTest::keyClick(recent->getChip(pdf::PDFAppRecentPage::Filter::Files), Qt::Key_Space);
    QCOMPARE(recent->getFilter(), pdf::PDFAppRecentPage::Filter::Files);
    recent->getSearchField()->setFocus();
    QTest::keyClicks(recent->getSearchField(), QStringLiteral("invoice"));
    QCOMPARE(recent->getSearchText(), QStringLiteral("invoice"));
    int files = 0;
    for (const pdf::PDFAppRecentItem& item : recent->getModel()->getItems())
    {
        files += item.type == pdf::PDFAppRecentItem::Type::File ? 1 : 0;
    }
    QCOMPARE(files, 1);
}

void AppHomeTest::accessibleNames()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);

    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    auto name = [](QWidget* widget)
    {
        QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(widget);
        return interface ? interface->text(QAccessible::Name) : QString();
    };
    QCOMPARE(name(home->getQuickActionTiles()[5]), QStringLiteral("Merge"));
    QCOMPARE(name(home->getRecentFileRows()[0]), QStringLiteral("Project_Report.pdf"));
    QCOMPARE(name(home->getRecentFileRows()[0]->getMenuButton()), QStringLiteral("More actions"));
    QVERIFY(!name(home->getDropZone()).isEmpty());

    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    QCOMPARE(name(open->getWorkspaceRows()[0]), QStringLiteral("Thesis review"));
    QCOMPARE(name(open->getLocationCards()[0]), QStringLiteral("Work Projects"));
    QCOMPARE(name(open->getSessionRow()->getRestoreButton()), QStringLiteral("Restore"));
    QVERIFY(!open->getSessionRow()->getRestoreButton()->accessibleDescription().isEmpty());

    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    QCOMPARE(name(recent->getSearchField()), QStringLiteral("Search in Recent"));
    QCOMPARE(recent->getSortBox()->accessibleName(), QStringLiteral("Sort"));
    QCOMPARE(name(recent->getChip(pdf::PDFAppRecentPage::Filter::Pinned)), QStringLiteral("Pinned"));
    QCOMPARE(name(recent->getListView()), QStringLiteral("History"));
    QCOMPARE(recent->getModel()->index(1, 0).data(Qt::AccessibleTextRole).toString(), QStringLiteral("Project_Report.pdf"));
    QVERIFY(!recent->getModel()->index(1, 0).data(Qt::AccessibleDescriptionRole).toString().isEmpty());
    QCOMPARE(name(recent->getPinnedCards()[0]), QStringLiteral("Building_Plan.pdf"));

    // A dialog field has its name
    recent->showClearHistoryDialog();
    QCOMPARE(name(recent->getDialog()), QStringLiteral("Clear the Recent history?"));
    recent->getDialog()->reject();
    fixture.navigate(QStringLiteral("/open"));
    fixture.page<pdf::PDFAppOpenPage>()->showRenameWorkspaceDialog(fixture.workspaces().getWorkspaces().front().id);
    pdf::PDFAppRenameWorkspaceDialog* dialog = qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(fixture.page<pdf::PDFAppOpenPage>()->getDialog());
    QCOMPARE(name(dialog->getNameField()), QStringLiteral("Name"));
    dialog->reject();
}

void AppHomeTest::viewState()
{
    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 700));
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    QScrollBar* scrollBar = page->getListView()->verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);
    scrollBar->setValue(scrollBar->maximum());
    const int value = scrollBar->value();
    const QVariantMap state = page->saveViewState();
    QCOMPARE(state.value(QStringLiteral("listScroll")).toInt(), value);

    // Back returns to the position of the list
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(fixture.navigator()->goBack());
    HomeFixture::settle();
    fixture.waitIdle();
    page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(page);
    QCOMPARE(page->getListView()->verticalScrollBar()->value(), value);

    // A hostile state is ignored
    page->restoreViewState(QVariantMap({ { QStringLiteral("listScroll"), QStringLiteral("x") } }));
    page->restoreViewState(QVariantMap({ { QStringLiteral("listScroll"), -5 } }));
    HomeFixture::settle();
}

void AppHomeTest::deleteWithOpenMenu()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);

    // The menu of a row is open, the page goes (navigation by the sidebar)
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QPointer<QMenu> menu = page->getRecentFileRows()[0]->getMenu();
    page->getRecentFileRows()[0]->getMenuButton()->click();
    HomeFixture::settle();
    QVERIFY(menu && menu->isVisible());
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(!menu);

    // The menu of the Recent list is open, the database changes and the page goes
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    recent->showListMenu(1);
    menu = recent->getListMenu();
    QVERIFY(menu && menu->isVisible());
    fixture.recent().setPinned(fixture.recent().getFiles().front().path, false);
    fixture.waitIdle();
    QVERIFY(menu);
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(!menu);

    // An action of a menu, whose page has gone meanwhile (an action triggered late)
    fixture.navigate(QStringLiteral("/open"));
    page = fixture.page<pdf::PDFAppOpenPage>();
    QPointer<QAction> action = page->getRecentFileRows()[0]->getMenu()->actions().front();
    fixture.navigate(QStringLiteral("/home"));
    QVERIFY(!action);

    // The window goes with an open menu
    fixture.navigate(QStringLiteral("/open"));
    page = fixture.page<pdf::PDFAppOpenPage>();
    menu = page->getFolderCards()[0]->getMenu();
    page->getFolderCards()[0]->getMenuButton()->click();
    HomeFixture::settle();
    delete fixture.window.data();
    HomeFixture::settle();
    QVERIFY(!menu);
}

void AppHomeTest::deleteWithOpenDialog()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // A dialog of the page goes with the page (another tab navigates the Home tab)
    for (int kind = 0; kind < 4; ++kind)
    {
        fixture.navigate(QStringLiteral("/open"));
        pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
        switch (kind)
        {
            case 0:
                page->showDeleteWorkspaceDialog(sample.thesisId);
                break;
            case 1:
                page->showRenameWorkspaceDialog(sample.thesisId);
                break;
            case 2:
                page->showClearHistoryDialog();
                break;
            default:
                page->showSaveWorkspaceDialog();
                break;
        }
        HomeFixture::settle();
        QPointer<QDialog> dialog = page->getDialog();
        QVERIFY(dialog && dialog->isVisible());
        fixture.navigate(QStringLiteral("/recent"));
        HomeFixture::settle();
        QVERIFY(!dialog);
        QVERIFY(fixture.workspaces().getWorkspace(sample.thesisId));
    }

    // A dialog accepted, while its page goes in the slot of the result
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    page->showDeleteWorkspaceDialog(sample.contractsId);
    QPointer<QDialog> dialog = page->getDialog();
    QObject::connect(dialog.data(), &QDialog::finished, fixture.navigator(), [&fixture]() { fixture.navigator()->navigate(pdf::PDFAppRoutes::home()); });
    qobject_cast<pdf::PDFAppDeleteWorkspaceDialog*>(dialog.data())->getDeleteButton()->click();
    HomeFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(fixture.page<pdf::PDFAppHomePage>());

    // The window goes with an open dialog and an open error box
    fixture.navigate(QStringLiteral("/open"));
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showRenameWorkspaceDialog(sample.thesisId);
    dialog = page->getDialog();
    delete fixture.window.data();
    HomeFixture::settle();
    QVERIFY(!dialog);
}

void AppHomeTest::deleteWithPendingThumbnail()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // Requests of the thumbnails and of the folder information are pending, the page goes
    QVERIFY(!fixture.recent().setThumbnail(sample.projectReport, QByteArray(), 0, pdf::PDFRecentThumbnailState::Pending, -1).isError());
    QVERIFY(!fixture.recent().setThumbnail(sample.technical, QByteArray(), 0, pdf::PDFRecentThumbnailState::Pending, -1).isError());
    fixture.context->getFolderInfo()->setTestDelay(1500);
    fixture.navigate(QStringLiteral("/recent"));
    QPointer<pdf::PDFAppPage> page = fixture.currentPage();
    QVERIFY(fixture.context->getFolderInfo()->getPendingCount() > 0);
    fixture.navigate(QStringLiteral("/open"));
    QVERIFY(!page);
    fixture.waitIdle(400);
    QVERIFY(fixture.page<pdf::PDFAppOpenPage>());

    // The tab goes (a New tab with Open) with pending requests
    pdf::PDFAppTab* tab = fixture.window->openNewTab();
    QVERIFY(fixture.context->getFolderInfo()->getPendingCount() > 0);
    fixture.window->closeTab(tab);
    HomeFixture::settle();
    fixture.waitIdle(400);
    fixture.context->getFolderInfo()->setTestDelay(0);
}

void AppHomeTest::deleteWithNativeDialog()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // The native dialog of the files runs its own event loop: the page goes meanwhile
    // (another instance asks the window to open a file and the Home tab navigates)
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&)
    {
        fixture.navigator()->navigate(pdf::PDFAppRoutes::recent());
        HomeFixture::settle();
        return QStringList({ sample.presentation });
    });
    fixture.page<pdf::PDFAppOpenPage>()->getDropZone()->getBrowseButton()->click();
    HomeFixture::settle();
    QVERIFY(qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab()));
    QCOMPARE(fixture.closeDocumentTabs(), 1);
    QVERIFY(fixture.page<pdf::PDFAppRecentPage>());

    // The window opens the chosen files, although the page has gone
    QVERIFY(fixture.window->getRequestedFiles().contains(sample.presentation));
    fixture.closeMessageBoxes();

    // The same for the folder dialog of Add…
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&)
    {
        fixture.navigator()->navigate(pdf::PDFAppRoutes::home());
        HomeFixture::settle();
        return fixture.directory.path();
    });
    fixture.page<pdf::PDFAppOpenPage>()->getAddLocationLink()->click();
    HomeFixture::settle();
    QVERIFY(fixture.page<pdf::PDFAppHomePage>());
    QVERIFY(!fixture.recent().hasPinnedLocation(fixture.directory.path()));

    // The window goes in the native dialog
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&)
    {
        fixture.window->deleteLater();
        HomeFixture::settle();
        return QStringList({ sample.presentation });
    });
    fixture.page<pdf::PDFAppOpenPage>()->browseFiles();
    HomeFixture::settle();
    QVERIFY(!fixture.window);
}

void AppHomeTest::deleteFromOwnSignal()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // A row deleted in the signal of its own click (the slot navigates away)
    fixture.navigate(QStringLiteral("/home"));
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    QPointer<pdf::PDFRecentFileRow> row = home->getRecentFileRows()[0];
    QObject::connect(row.data(), &pdf::PDFRecentFileRow::clicked, fixture.navigator(), [&fixture]() { fixture.navigator()->navigate(pdf::PDFAppRoutes::recent()); });
    row->click();
    HomeFixture::settle();
    QVERIFY(!row);
    QVERIFY(fixture.page<pdf::PDFAppRecentPage>());
    fixture.closeMessageBoxes();

    // The pin of a pinned card refreshes the cards (the card goes later, not in its signal)
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    fixture.waitIdle();
    QPointer<pdf::PDFPinnedCard> card = recent->getPinnedCards()[0];
    card->getStarButton()->click();
    QVERIFY(card);
    fixture.waitIdle();
    QVERIFY(!card);
    QCOMPARE(recent->getPinnedCards().size(), 2);

    // A menu action, whose slot deletes the page (Open of a workspace navigates in a slot of the window)
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    QPointer<pdf::PDFAppOpenPage> guard(page);
    QAction* open = HomeTestHelper::findAction(page->getWorkspaceRows()[0]->getMenu(), QStringLiteral("Open"));
    QObject::connect(open, &QAction::triggered, fixture.navigator(), [&fixture]() { fixture.navigator()->navigate(pdf::PDFAppRoutes::home()); }, Qt::DirectConnection);
    open->trigger();
    HomeFixture::settle();
    fixture.closeMessageBoxes();
    Q_UNUSED(sample);
}

void AppHomeTest::deleteWindow()
{
    // The window goes with a pending refresh, pending folder information and a scheduled list scroll
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/recent"));
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    Q_EMIT fixture.context->getNotifier()->changed(pdf::PDFRecentRepository::getFileTopic(), sample.invoice);
    QVERIFY(page->isRefreshPending());
    page->restoreViewState(QVariantMap({ { QStringLiteral("listScroll"), 30 } }));
    delete fixture.window.data();
    HomeFixture::settle();
    fixture.waitIdle(200);

    // A page without a context (and without a window) does nothing
    pdf::PDFAppHomePage home(nullptr);
    pdf::PDFAppOpenPage open(nullptr);
    pdf::PDFAppRecentPage recent(nullptr);
    home.refreshNow();
    open.refreshNow();
    recent.refreshNow();
    open.openWorkspace(1);
    open.restoreLastSession();
    open.setFilePinned(sample.invoice, true);
    open.setFolderPinned(sample.invoice, true);
    open.addPinnedLocation();
    open.showRenameLocationDialog(1);
    open.showRenameWorkspaceDialog(1);
    open.showDeleteWorkspaceDialog(1);
    QVERIFY(!open.getDialog());
    QVERIFY(open.performAction(pdf::PDFAppHomeItem(), pdf::PDFAppHomeAction::RemoveFromList));
    QVERIFY(open.performAction(pdf::PDFAppHomeItem(), pdf::PDFAppHomeAction::RemoveLocation));
    QVERIFY(open.performAction(pdf::PDFAppHomeItem(), pdf::PDFAppHomeAction::ShowInFolder));
    QVERIFY(recent.getModel()->getItems().empty());
    QVERIFY(recent.isEmptyStateShown());
}

void AppHomeTest::pageRoutesRefused()
{
    HomeFixture fixture;

    // The pages refuse a query or an anchor by themselves (created also by other means than the table)
    pdf::PDFAppHomePage home(fixture.context.get());
    QVERIFY(home.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/home"))));
    QVERIFY(!home.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/home?x=1"))));
    QVERIFY(!home.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/home#a"))));
    pdf::PDFAppOpenPage open(fixture.context.get());
    QVERIFY(!open.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/open?x=1"))));
    QVERIFY(!open.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/open#a"))));
    pdf::PDFAppRecentPage recent(fixture.context.get());
    QVERIFY(!recent.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/recent?filter=bogus"))));
    QVERIFY(!recent.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/recent?sort=bogus"))));
    QVERIFY(!recent.setRoute(pdf::PDFAppRoute::parse(QStringLiteral("/recent#a"))));
    QVERIFY(!recent.setRoute(pdf::PDFAppRoute::create({ QStringLiteral("recent") }, { { QStringLiteral("q"), QString(300, QLatin1Char('a')) } })));
    QCOMPARE(pdf::PDFAppRecentPage::getFilterId(pdf::PDFAppRecentPage::Filter(77)), QString());
    QVERIFY(!pdf::PDFAppRecentPage::parseFilter(QStringLiteral("all")));
    QCOMPARE(home.getTitle(), QStringLiteral("Home"));
    QCOMPARE(open.getTitle(), QStringLiteral("Open"));
    QCOMPARE(recent.getTitle(), QStringLiteral("Recent"));
}

void AppHomeTest::databaseErrors()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // A refused write shows its error in a message box (a second one replaces the first)
    pdf::PDFAppHomeItem item;
    item.kind = pdf::PDFAppHomeItemKind::File;
    item.path = QStringLiteral("relative.pdf");
    QVERIFY(page->performAction(item, pdf::PDFAppHomeAction::RemoveFromList));
    QPointer<QDialog> first = page->getErrorBox();
    QVERIFY(first);
    QCOMPARE(first->objectName(), QStringLiteral("databaseErrorBox"));
    QVERIFY(first->isVisible());
    QVERIFY(page->performAction(item, pdf::PDFAppHomeAction::Pin));
    HomeFixture::settle();
    QVERIFY(!first);
    QVERIFY(page->getErrorBox());

    // The error box goes with the page
    QPointer<QDialog> second = page->getErrorBox();
    fixture.navigate(QStringLiteral("/home"));
    HomeFixture::settle();
    QVERIFY(!second);

    // A workspace of an invalid document is refused, the dialog shows the error
    pdf::PDFAppDocumentActions::setOpenDocumentsProvider([](const QWidget*)
    {
        std::vector<pdf::PDFAppOpenDocument> documents(1);
        documents[0].path = QStringLiteral("relative.pdf");
        return documents;
    });
    fixture.navigate(QStringLiteral("/open"));
    page = fixture.page<pdf::PDFAppOpenPage>();
    page->showSaveWorkspaceDialog();
    pdf::PDFAppSaveWorkspaceDialog* dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog());
    dialog->getNameField()->setText(QStringLiteral("Invalid"));
    dialog->getSaveButton()->click();
    QVERIFY(!dialog->getErrorText().isEmpty());
    QVERIFY(!dialog->isDuplicateShown());
    dialog->reject();

    // A dialog without a database
    pdf::PDFAppOpenDocument document;
    document.path = QStringLiteral("C:/x.pdf");
    pdf::PDFAppSaveWorkspaceDialog detached(nullptr, { document }, nullptr);
    detached.getNameField()->setText(QStringLiteral("x"));
    detached.getReplaceButton()->click();
    QCOMPARE(detached.getErrorText(), QStringLiteral("The settings of PDF4QT are not available."));
    QCOMPARE(detached.getContentWidth(), 520 - 2 * 22);
    Q_UNUSED(sample);
}

void AppHomeTest::smallParts()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // More than four pinned files: four rows and View all (5)
    fixture.recent().setPinned(sample.invoice, true);
    fixture.recent().setPinned(sample.presentation, true);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* open = fixture.page<pdf::PDFAppOpenPage>();
    QCOMPARE(open->getPinnedFileRows().size(), pdf::PDFAppOpenPage::MAXIMUM_PINNED_FILES);
    QCOMPARE(open->getPinnedViewAllLink()->text(), QStringLiteral("View all (5)"));

    // Recent: the menu of the list replaces its previous menu
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    recent->showListMenu(1);
    QPointer<QMenu> firstMenu = recent->getListMenu();
    recent->showListMenu(2);
    QVERIFY(recent->getListMenu() && recent->getListMenu() != firstMenu.data());
    HomeFixture::settle();
    QVERIFY(!firstMenu || !firstMenu->isVisible());

    // The offscreen platform may close the second popup itself (the focus window
    // goes with the first popup and the application is inactive for a moment)
    if (QMenu* menu = recent->getListMenu())
    {
        menu->close();
    }
    HomeFixture::settle();

    // A pinned card opens the file in its own document tab (step 3)
    recent->getPinnedCards()[0]->click();
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), 1);
    QVERIFY(qobject_cast<pdf::PDFAppDocumentTab*>(fixture.currentTab()));
    QCOMPARE(fixture.closeDocumentTabs(), 1);
    fixture.closeMessageBoxes();
    fixture.waitIdle();
    recent = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(recent);

    // The model: tooltips of the items, no flags of an invalid index, no data of another model
    const pdf::PDFAppRecentModel* model = recent->getModel();
    QVERIFY(!model->index(1, 0).data(Qt::ToolTipRole).toString().isEmpty());
    QVERIFY(model->index(0, 0).data(Qt::ToolTipRole).isNull());
    QCOMPARE(model->flags(QModelIndex()), Qt::NoItemFlags);
    QVERIFY(model->data(QModelIndex(), Qt::DisplayRole).isNull());
    QVERIFY(model->index(1, 0).data(Qt::UserRole + 7).isNull());
    QCOMPARE(model->rowCount(model->index(0, 0)), 0);
    QVERIFY(!model->getItem(-1));

    // An elided label: the tooltip only for an elided text
    pdf::PDFAppElidedLabel label(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body), pdf::PDFThemeColors::TextPrimary, pdf::PDFComponentPainter::TextKind::FileName);
    label.setText(QStringLiteral("A_very_long_name_of_a_document_which_does_not_fit.pdf"));
    label.resize(60, 20);
    QVERIFY(label.isElided());
    QVERIFY(label.sizeHint().width() > 60);
    QCOMPARE(label.minimumSizeHint().width(), 1);
    QHelpEvent help(QEvent::ToolTip, QPoint(5, 5), QPoint(5, 5));
    QVERIFY(QApplication::sendEvent(&label, &help));
    label.resize(1000, 20);
    QVERIFY(!label.isElided());
    QHelpEvent help2(QEvent::ToolTip, QPoint(5, 5), QPoint(5, 5));
    QApplication::sendEvent(&label, &help2);
    QVERIFY(!help2.isAccepted());
    QImage image(label.size(), QImage::Format_ARGB32_Premultiplied);
    label.render(&image);
    label.setText(QString());
    label.render(&image);
    label.setEnabled(false);
    label.setText(QStringLiteral("x"));
    label.render(&image);

    // Display texts cut at a pair of surrogates
    QString emoji = QStringLiteral("a");
    for (int i = 0; i < 20; ++i)
    {
        emoji += QString::fromUcs4(U"\U0001F600");
    }
    const QString cut = pdf::PDFAppHomeFormat::toDisplayText(emoji, 6);
    QVERIFY(cut.endsWith(QChar(0x2026)));
    QVERIFY(!cut.at(cut.size() - 2).isHighSurrogate());

    // The refusal text of too many files; a drop of more items than examined
    QVERIFY(!pdf::PDFAppFileDrop::getRefusalText(pdf::PDFAppFileDrop::Refusal::TooMany).isEmpty());
    QList<QUrl> urls;
    for (int i = 0; i < pdf::PDFAppFileDrop::MAXIMUM_ITEMS + 10; ++i)
    {
        urls << QUrl(QStringLiteral("https://example.com/%1.pdf").arg(i));
    }
    QMimeData* data = HomeTestHelper::createMimeData(urls);
    QVERIFY(!pdf::PDFAppFileDrop::hasLocalFiles(data));
    const pdf::PDFAppFileDrop::Result result = pdf::PDFAppFileDrop::getFiles(data);
    QVERIFY(result.files.isEmpty());
    QCOMPARE(result.refused.size(), size_t(pdf::PDFAppFileDrop::MAXIMUM_ITEMS));
    delete data;

    // The columns of a hidden widget start beside each other
    pdf::PDFAppColumns columns;
    columns.addColumn(100, 0, 0);
    columns.addColumn(0, 1, 100);
    QVERIFY(!columns.isStacked());
    QVERIFY(columns.isStackedAt(150));
    QVERIFY(!columns.isStackedAt(300));
    QVERIFY(!columns.getColumn(5));
    QVERIFY(!columns.getColumnLayout(-1));
    QCOMPARE(columns.getColumnCount(), 2);
}

void AppHomeTest::networkRows()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);

    // Rows of network paths (also copied from the database of another computer): a
    // UNC file in progress, a pinned one, a network folder; they are inserted by SQL,
    // the test never touches them (the server name does not exist)
    const QString server = QStringLiteral("//pdf4qt-test-server.invalid/share");
    const QString progressFile = server + QStringLiteral("/Network_Report.pdf");
    const QString pinnedFile = server + QStringLiteral("/Network_Pinned.pdf");
    const QString gvfsFile = QStringLiteral("/run/user/1000/gvfs/smb-share:server=nas,share=docs/Gvfs.pdf");
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    const qint64 time = HomeFixture::at(0, 11, 0);
    const QStringList statements = {
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened, last_page, page_count, thumbnail, thumbnail_mtime, thumbnail_state) "
                       "VALUES ('%1', 'Network_Report.pdf', '%2', %3, 5, 40, NULL, 0, 0)").arg(progressFile, server).arg(time),
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened, pinned, pin_order) VALUES ('%1', 'Network_Pinned.pdf', '%2', %3, 1, 0)").arg(pinnedFile, server).arg(time - 1000),
        QStringLiteral("INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('%1', 'Gvfs.pdf', '/run/user/1000/gvfs', %2)").arg(gvfsFile).arg(time - 2000),
        QStringLiteral("INSERT INTO recent_folder(path, last_used, kind) VALUES ('%1', %2, 0)").arg(server).arg(time),
    };
    for (const QString& sql : statements)
    {
        const QByteArray utf8 = sql.toUtf8();
        pdf::PDFDatabaseStatement statement(database->getConnection(), utf8.constData());
        QVERIFY2(statement.execute(), qPrintable(sql));
    }

    // The stored thumbnail of the last opening is shown (the database, not the network)
    QVERIFY(!fixture.recent().setThumbnail(pinnedFile, HomeTestHelper::createPng(Qt::green), 77, pdf::PDFRecentThumbnailState::Ready, 2).isError());
    QVERIFY(QFile::remove(sample.invoice));

    pdf::PDFAppFolderInfoService* service = fixture.context->getFolderInfo();
    pdf::PDFAppThumbnailer* thumbnailer = fixture.context->getThumbnailer();
    QSignalSpy infoSpy(service, &pdf::PDFAppFolderInfoService::infoReady);
    QSignalSpy thumbnailSpy(thumbnailer, &pdf::PDFAppThumbnailer::thumbnailFinished);

    for (const QString& route : { QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("/recent"), QStringLiteral("/recent?filter=folders") })
    {
        fixture.navigate(route);
        fixture.waitIdle();
        pdf::PDFAppHomeBasePage* page = qobject_cast<pdf::PDFAppHomeBasePage*>(fixture.currentPage());
        QVERIFY(page);
        QVERIFY(page->isNetworkPath(progressFile));
        QVERIFY(page->isNetworkPath(gvfsFile));
        QVERIFY(!page->isNetworkPath(sample.invoice));
    }

    // Nothing of the network was asked for: no existence, no PDF count, no thumbnail
    for (const QList<QVariant>& arguments : infoSpy)
    {
        const QString path = arguments.at(0).value<pdf::PDFAppFolderInfo>().path;
        QVERIFY2(!path.startsWith(server) && path != gvfsFile, qPrintable(path));
    }
    for (const QList<QVariant>& arguments : thumbnailSpy)
    {
        const QString path = arguments.at(0).toString();
        QVERIFY2(!path.startsWith(server) && path != gvfsFile, qPrintable(path));
    }
    QVERIFY(!infoSpy.isEmpty());

    // Home and Open: the network rows are neutral (Available), the missing local file is Missing
    fixture.navigate(QStringLiteral("/home"));
    fixture.waitIdle();
    pdf::PDFAppHomePage* home = fixture.page<pdf::PDFAppHomePage>();
    bool isNetworkRowFound = false;
    for (pdf::PDFRecentFileRow* row : home->getRecentFileRows())
    {
        if (row->getFileName() == QLatin1String("Network_Report.pdf"))
        {
            isNetworkRowFound = true;
            QCOMPARE(row->getStatus(), pdf::PDFRecentFileRow::Status::Available);
        }
        if (row->getFileName() == QLatin1String("Invoice_2026.pdf"))
        {
            QCOMPARE(row->getStatus(), pdf::PDFRecentFileRow::Status::Missing);
        }
    }
    QVERIFY(isNetworkRowFound);

    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    for (pdf::PDFRecentFileRow* row : fixture.page<pdf::PDFAppOpenPage>()->getPinnedFileRows() + fixture.page<pdf::PDFAppOpenPage>()->getRecentFileRows())
    {
        if (row->getFileName().startsWith(QLatin1String("Network")) || row->getFileName() == QLatin1String("Gvfs.pdf"))
        {
            QCOMPARE(row->getStatus(), pdf::PDFRecentFileRow::Status::Available);
        }
    }

    // Recent: no "No longer exists", the stored thumbnail of the pinned card, the
    // network file in progress is Continue where you left off (not checked)
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    for (int row = 0; row < recent->getModel()->rowCount(); ++row)
    {
        const QModelIndex index = recent->getModel()->index(row, 0);
        const QString name = index.data(Qt::DisplayRole).toString();
        if (name.startsWith(QLatin1String("Network")) || name == QLatin1String("Gvfs.pdf") || name == QLatin1String("share"))
        {
            QVERIFY2(!index.data(pdf::PDFComponentRoles::MissingRole).toBool(), qPrintable(name));
        }
    }
    QVERIFY(recent->getContinueSection()->isVisible());
    QCOMPARE(recent->getContinueCard()->getName(), QStringLiteral("Network_Report.pdf"));
    bool isPinnedCardFound = false;
    for (pdf::PDFPinnedCard* card : recent->getPinnedCards())
    {
        if (card->getName() == QLatin1String("Network_Pinned.pdf"))
        {
            isPinnedCardFound = true;
            QCOMPARE(card->getStatus(), pdf::PDFPinnedCard::Status::Available);
            QVERIFY(!card->getPixmap().isNull());
        }
    }
    QVERIFY(isPinnedCardFound);

    // The folder row has no PDF count (it would enumerate the share)
    recent->setFilter(pdf::PDFAppRecentPage::Filter::Folders, pdf::PDFRecentSort::LastOpened, QString());
    fixture.waitIdle();
    for (int row = 0; row < recent->getModel()->rowCount(); ++row)
    {
        const QModelIndex index = recent->getModel()->index(row, 0);
        if (index.data(Qt::DisplayRole).toString() == QLatin1String("share"))
        {
            QVERIFY(!index.data(pdf::PDFComponentRoles::SubtitleRole).toString().contains(QLatin1String("PDF")));
        }
    }
}

void AppHomeTest::refusedPathRequests()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    QVERIFY(QFile::remove(sample.buildingPlan));
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();

    // The service is full: its results are not delivered, until the event loop runs
    pdf::PDFAppFolderInfoService* service = fixture.context->getFolderInfo();
    const QString base = fixture.directory.filePath(QStringLiteral("filler"));
    for (int i = 0; i < pdf::PDFAppFolderInfoService::MAXIMUM_PENDING; ++i)
    {
        QVERIFY(service->requestPathInfo(QStringLiteral("%1/%2.pdf").arg(base).arg(i)));
    }
    QVERIFY(service->isFull());
    QVERIFY(!service->requestPathInfo(base + QStringLiteral("/refused.pdf")));
    QVERIFY(service->requestPathInfo(base + QStringLiteral("/0.pdf")));
    QVERIFY(!service->requestPathInfo(QStringLiteral("relative.pdf")));

    // The page asks while the service is full: the requests are refused and asked again
    pdf::PDFAppHomePage* home = nullptr;
    fixture.navigator()->navigate(pdf::PDFAppRoute::parse(QStringLiteral("/home")), pdf::PDFAppNavigationMode::Push);
    home = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(home);
    QVERIFY(home->getRefusedRequestCount() > 0);

    // The service drains, the refused requests are asked again and answered
    fixture.waitIdle();
    QTRY_COMPARE_WITH_TIMEOUT(home->getRefusedRequestCount(), 0, 10000);
    fixture.waitIdle();
    bool isMissingShown = false;
    for (pdf::PDFRecentFileRow* row : home->getRecentFileRows())
    {
        isMissingShown = isMissingShown || (row->getFileName() == QLatin1String("Building_Plan.pdf") && row->getStatus() == pdf::PDFRecentFileRow::Status::Missing);
    }
    QVERIFY(isMissingShown);

    // A page deleted with refused requests: its retry timer goes with it
    for (int i = 0; i < pdf::PDFAppFolderInfoService::MAXIMUM_PENDING; ++i)
    {
        service->requestPathInfo(QStringLiteral("%1/again-%2.pdf").arg(base).arg(i));
    }
    fixture.navigator()->navigate(pdf::PDFAppRoute::parse(QStringLiteral("/open")), pdf::PDFAppNavigationMode::Push);
    QVERIFY(fixture.page<pdf::PDFAppOpenPage>()->getRefusedRequestCount() > 0);
    fixture.navigate(QStringLiteral("/about"));
    fixture.waitIdle();
    QCOMPARE(service->getPendingCount(), 0);
}

/// Page of the test: the thumbnails of the base without a refresh
class ThumbnailTestPage : public pdf::PDFAppHomeBasePage
{
public:
    using pdf::PDFAppHomeBasePage::PDFAppHomeBasePage;
    using pdf::PDFAppHomeBasePage::getThumbnail;

    virtual QString getTitle() const override { return QStringLiteral("Thumbnails"); }

protected:
    virtual void refresh() override { }
};

void AppHomeTest::thumbnailCacheLru()
{
    HomeFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    const QByteArray png = HomeTestHelper::createPng(Qt::blue, QSize(4, 4));
    constexpr int count = pdf::PDFAppHomeBasePage::MAXIMUM_THUMBNAILS + 20;
    {
        pdf::PDFDatabaseStatement statement(database->getConnection(),
            "INSERT INTO recent_file(path, name, folder, last_opened, thumbnail, thumbnail_mtime, thumbnail_state) VALUES (?1, 'f.pdf', '/lru', ?2, ?3, 5, 1)");
        for (int i = 0; i < count; ++i)
        {
            statement.bindText(1, QStringLiteral("/lru/%1.pdf").arg(i));
            statement.bindInt64(2, i + 1);
            statement.bindBlob(3, png);
            QVERIFY(statement.execute());
            statement.reset();
        }
    }

    ThumbnailTestPage page(fixture.context.get());
    auto file = [&](int i) { return *fixture.recent().getFile(QStringLiteral("/lru/%1.pdf").arg(i)); };
    const pdf::PDFRecentFile first = file(0);
    const QPixmap firstPixmap = page.getThumbnail(first);
    QVERIFY(!firstPixmap.isNull());
    for (int i = 1; i < pdf::PDFAppHomeBasePage::MAXIMUM_THUMBNAILS; ++i)
    {
        QVERIFY(!page.getThumbnail(file(i)).isNull());
    }
    QCOMPARE(page.getThumbnailCacheSize(), pdf::PDFAppHomeBasePage::MAXIMUM_THUMBNAILS);

    // The first one is used again (a shown row), then new pictures come: the least
    // recently used ones go one by one, nothing is cleared at once
    QCOMPARE(page.getThumbnail(first).cacheKey(), firstPixmap.cacheKey());
    for (int i = pdf::PDFAppHomeBasePage::MAXIMUM_THUMBNAILS; i < count; ++i)
    {
        QVERIFY(!page.getThumbnail(file(i)).isNull());
        QCOMPARE(page.getThumbnailCacheSize(), pdf::PDFAppHomeBasePage::MAXIMUM_THUMBNAILS);
    }
    QVERIFY(page.hasCachedThumbnail(first.path));
    QCOMPARE(page.getThumbnail(first).cacheKey(), firstPixmap.cacheKey());
    QVERIFY(!page.hasCachedThumbnail(QStringLiteral("/lru/1.pdf")));
    QVERIFY(!page.hasCachedThumbnail(QStringLiteral("/lru/20.pdf")));
    QVERIFY(page.hasCachedThumbnail(QStringLiteral("/lru/21.pdf")));
    QVERIFY(page.hasCachedThumbnail(QStringLiteral("/lru/%1.pdf").arg(count - 1)));

    // A new version of a thumbnail is decoded again in its place; a file without
    // a thumbnail forgets its picture
    pdf::PDFRecentFile changed = first;
    changed.thumbnailModified = 6;
    QVERIFY(page.getThumbnail(changed).cacheKey() != firstPixmap.cacheKey());
    QCOMPARE(page.getThumbnailCacheSize(), pdf::PDFAppHomeBasePage::MAXIMUM_THUMBNAILS);
    changed.hasThumbnail = false;
    QVERIFY(page.getThumbnail(changed).isNull());
    QVERIFY(!page.hasCachedThumbnail(first.path));
}

void AppHomeTest::focusAfterPin()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // Space on the star of a recent row pins it: the rows are rebuilt, the focus
    // returns to the star of the row of the same file in the same list
    pdf::PDFRecentFileRow* row = nullptr;
    for (pdf::PDFRecentFileRow* candidate : page->getRecentFileRows())
    {
        row = (!row && !candidate->isPinned()) ? candidate : row;
    }
    QVERIFY(row);
    const QString path = row->property("pdf4qtPath").toString();
    QWidget* container = row->parentWidget();
    QPointer<pdf::PDFRecentFileRow> oldRow = row;
    row->getStarButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row->getStarButton(), Qt::Key_Space);
    fixture.waitIdle();
    QVERIFY(fixture.recent().getFile(path)->pinned);
    QVERIFY(!oldRow || !oldRow->isVisible());

    QWidget* focus = QApplication::focusWidget();
    QVERIFY(focus);
    QVERIFY(qobject_cast<pdf::PDFStarButton*>(focus));
    QWidget* focusedRow = focus->parentWidget();
    QVERIFY(focusedRow);
    QCOMPARE(focusedRow->property("pdf4qtPath").toString(), path);
    QCOMPARE(focusedRow->parentWidget(), container);
    QVERIFY(focusedRow->isVisible());

    // Space again unpins it, the focus stays on the same item
    QTest::keyClick(focus, Qt::Key_Space);
    fixture.waitIdle();
    QVERIFY(!fixture.recent().getFile(path)->pinned);
    focus = QApplication::focusWidget();
    QVERIFY(qobject_cast<pdf::PDFStarButton*>(focus));
    QCOMPARE(focus->parentWidget()->property("pdf4qtPath").toString(), path);

    // A row focused itself (not its star) keeps the focus on the row
    row = page->getRecentFileRows()[0];
    const QString rowPath = row->property("pdf4qtPath").toString();
    row->setFocus(Qt::TabFocusReason);
    QVERIFY(!fixture.recent().setPinned(rowPath, !fixture.recent().getFile(rowPath)->pinned).isError());
    fixture.waitIdle();
    focus = QApplication::focusWidget();
    QVERIFY(focus);
    QCOMPARE(focus->property("pdf4qtPath").toString(), rowPath);

    // An item, which is gone (removed by another connection), leaves the focus alone
    QVERIFY(!fixture.recent().removeFile(rowPath).isError());
    fixture.waitIdle();
    focus = QApplication::focusWidget();
    QVERIFY(!focus || focus->property("pdf4qtPath").toString() != rowPath);

    // Recent: the current item of the list survives the reset of the model by a pin
    fixture.navigate(QStringLiteral("/recent?filter=files"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* recent = fixture.page<pdf::PDFAppRecentPage>();
    pdf::PDFAppRecentModel* model = recent->getModel();
    int fileRow = -1;
    for (int i = 0; i < model->rowCount() && fileRow < 0; ++i)
    {
        const pdf::PDFAppRecentItem* item = model->getItem(i);
        fileRow = (item->type == pdf::PDFAppRecentItem::Type::File && !item->isPinned) ? i : -1;
    }
    QVERIFY(fileRow >= 0);
    const QString filePath = model->getItem(fileRow)->item.path;
    QAbstractItemView* view = recent->findChild<QAbstractItemView*>(QStringLiteral("historyList"));
    QVERIFY(view);
    view->setFocus(Qt::TabFocusReason);
    view->setCurrentIndex(model->index(fileRow, 0));
    // The model asks the page (it changes after the notification, setData says false)
    QVERIFY(!model->setData(model->index(fileRow, 0), true, pdf::PDFComponentRoles::PinnedRole));
    fixture.waitIdle();
    QVERIFY(fixture.recent().getFile(filePath)->pinned);
    QVERIFY(view->currentIndex().isValid());
    QCOMPARE(model->getItem(view->currentIndex().row())->item.path, filePath);
    QCOMPARE(model->findItem(pdf::PDFAppRecentItem::Type::File, filePath, 0), view->currentIndex().row());
    QCOMPARE(model->findItem(pdf::PDFAppRecentItem::Type::Folder, filePath, 0), -1);
}

void AppHomeTest::homeEmptyStates()
{
    HomeFixture fixture;
    fixture.hideBanner();
    fixture.navigate(QStringLiteral("/home"));
    fixture.waitIdle();
    pdf::PDFAppHomePage* page = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(page);

    // The small empty states of the list cards (scale of 955:142865)
    struct Case
    {
        pdf::PDFAppEmptyState* state;
        const char* title;
        const char* text;
    };
    const Case cases[] = {
        { page->getRecentFilesEmptyState(), "No recent documents", "Documents you open appear here." },
        { page->getRecentToolsEmptyState(), "No recent tools", "Tools you use appear here." },
    };
    for (const Case& emptyCase : cases)
    {
        pdf::PDFAppEmptyState* state = emptyCase.state;
        QVERIFY(state);
        QVERIFY(state->isVisible());
        QCOMPARE(state->getSize(), pdf::PDFAppEmptyState::Size::Small);
        QCOMPARE(state->getTitle(), QString::fromLatin1(emptyCase.title));
        QCOMPARE(state->getText(), QString::fromLatin1(emptyCase.text));
        QCOMPARE(state->accessibleName(), QString::fromLatin1(emptyCase.title));
        QVERIFY(!state->getBrowseButton());

        // Geometry: the circle 40 centered 20 below the top, the title 12 below it
        QWidget* icon = state->getIcon();
        QCOMPARE(icon->size(), QSize(pdf::PDFAppEmptyState::SMALL_CIRCLE, pdf::PDFAppEmptyState::SMALL_CIRCLE));
        QCOMPARE(icon->y(), 20);
        QVERIFY(qAbs(icon->geometry().center().x() - state->rect().center().x()) <= 1);
        QWidget* title = state->findChild<QWidget*>(QStringLiteral("emptyTitle"));
        QWidget* text = state->findChild<QWidget*>(QStringLiteral("emptyText"));
        QVERIFY(title && text);
        QCOMPARE(title->y(), icon->geometry().bottom() + 1 + 12);
        QCOMPARE(text->y(), title->geometry().bottom() + 1 + 4);
        QVERIFY(text->width() <= 280);
        QVERIFY(state->height() >= text->geometry().bottom() + 1 + 20 - 1);
    }

    // Pixels in both themes: the circle of the neutral hue, no leak color
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        HomeFixture::settle();
        for (const Case& emptyCase : cases)
        {
            for (qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = HomeTestHelper::render(emptyCase.state, ratio);
                const QWidget* icon = emptyCase.state->getIcon();
                const QColor fill = HomeTestHelper::pixel(image, icon->mapTo(emptyCase.state, QPoint(8, icon->height() / 2)));
                const QColor expected = HomeTestHelper::token(pdf::PDFThemeColors::AccentNeutralBg);
                QVERIFY2(HomeTestHelper::isClose(fill, expected, 6), qPrintable(fill.name() + QLatin1Char(' ') + expected.name()));
                QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);
            }
        }
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // Data: the empty states go, the rows come
    HomeTestHelper::fill(fixture);
    fixture.waitIdle();
    page = fixture.page<pdf::PDFAppHomePage>();
    QVERIFY(page->getRecentFilesEmptyState()->isHidden());
    QVERIFY(page->getRecentToolsEmptyState()->isHidden());

    // The large variant keeps its layout (Recent)
    pdf::PDFAppEmptyState large;
    QCOMPARE(large.getSize(), pdf::PDFAppEmptyState::Size::Large);
    QVERIFY(large.getBrowseButton());
    QCOMPARE(large.getIcon()->sizeHint(), QSize(72, 72));
}

void AppHomeTest::recentModel_data()
{
    // A change of one shown field of an item: the model must show it (a reset);
    // the same content keeps the model, a new picture changes only its role
    QTest::addColumn<QString>("field");
    QTest::addColumn<bool>("isReset");

    QTest::newRow("same") << QStringLiteral("same") << false;
    QTest::newRow("pixmap") << QStringLiteral("pixmap") << false;
    for (const char* field : { "type", "kind", "path", "id", "name", "details", "progressText", "progress", "opened", "isPinned", "count", "isMissing", "isLocked",
                               "isRemovable", "documentCount", "itemName", "rows" })
    {
        QTest::newRow(field) << QString::fromLatin1(field) << true;
    }
}

void AppHomeTest::recentModel()
{
    QFETCH(QString, field);
    QFETCH(bool, isReset);

    using Item = pdf::PDFAppRecentItem;
    auto createItems = []()
    {
        // A header, a file, a folder and a workspace
        std::vector<Item> items(4);
        items[0].type = Item::Type::Header;
        items[0].name = QStringLiteral("Today");
        items[0].count = 3;
        items[1].type = Item::Type::File;
        items[1].item.kind = pdf::PDFAppHomeItemKind::File;
        items[1].item.path = QStringLiteral("C:/Documents/Report.pdf");
        items[1].item.name = QStringLiteral("Report.pdf");
        items[1].name = QStringLiteral("Report.pdf");
        items[1].details = QStringLiteral("C:\\Documents · 24 pages");
        items[1].progressText = QStringLiteral("Page 12 of 24");
        items[1].progress = 0.5;
        items[1].opened = QStringLiteral("Today 10:24");
        items[2].type = Item::Type::Folder;
        items[2].item.kind = pdf::PDFAppHomeItemKind::Folder;
        items[2].item.path = QStringLiteral("C:/Documents");
        items[2].item.name = QStringLiteral("Documents");
        items[2].name = QStringLiteral("Documents");
        items[2].details = QStringLiteral("C:\\Documents");
        items[2].progressText = QStringLiteral("—");
        items[3].type = Item::Type::Workspace;
        items[3].item.kind = pdf::PDFAppHomeItemKind::Workspace;
        items[3].item.id = 7;
        items[3].item.name = QStringLiteral("Thesis");
        items[3].item.documentCount = 3;
        items[3].name = QStringLiteral("Thesis");
        return items;
    };

    pdf::PDFAppRecentModel model;
    model.setItems(createItems());
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    std::vector<Item> items = createItems();
    QPixmap red(8, 8);
    red.fill(Qt::red);
    const std::map<QString, std::function<void()>> changes = {
        { QStringLiteral("same"), []() {} },
        { QStringLiteral("pixmap"), [&]() { items[1].pixmap = red; } },
        { QStringLiteral("type"), [&]() { items[1].type = Item::Type::Folder; } },
        { QStringLiteral("kind"), [&]() { items[1].item.kind = pdf::PDFAppHomeItemKind::Folder; } },
        { QStringLiteral("path"), [&]() { items[1].item.path = QStringLiteral("C:/Documents/Other.pdf"); } },
        { QStringLiteral("id"), [&]() { items[3].item.id = 8; } },
        { QStringLiteral("name"), [&]() { items[1].name = QStringLiteral("Report (2).pdf"); } },
        { QStringLiteral("details"), [&]() { items[1].details += QStringLiteral(" · 2.4 MB"); } },
        { QStringLiteral("progressText"), [&]() { items[1].progressText = QStringLiteral("Finished"); } },
        { QStringLiteral("progress"), [&]() { items[1].progress = 1.0; } },
        { QStringLiteral("opened"), [&]() { items[1].opened = QStringLiteral("Yesterday 16:15"); } },
        { QStringLiteral("isPinned"), [&]() { items[1].isPinned = true; } },
        { QStringLiteral("count"), [&]() { items[0].count = 4; } },
        { QStringLiteral("isMissing"), [&]() { items[1].isMissing = true; } },
        { QStringLiteral("isLocked"), [&]() { items[1].isLocked = true; } },
        { QStringLiteral("isRemovable"), [&]() { items[2].item.isRemovable = false; } },
        { QStringLiteral("documentCount"), [&]() { items[3].item.documentCount = 4; } },
        { QStringLiteral("itemName"), [&]() { items[3].item.name = QStringLiteral("Thesis 2"); } },
        { QStringLiteral("rows"), [&]() { items.pop_back(); } },
    };
    changes.at(field)();
    model.setItems(items);

    QCOMPARE(resetSpy.count(), isReset ? 1 : 0);
    QCOMPARE(changedSpy.count(), field == QLatin1String("pixmap") ? 1 : 0);
    if (field == QLatin1String("pixmap"))
    {
        QCOMPARE(changedSpy[0][0].toModelIndex().row(), 1);
        QVERIFY(changedSpy[0][2].value<QList<int>>().contains(pdf::PDFComponentRoles::PixmapRole));
    }

    // The model shows the new items (every role as a model filled with them at once)
    pdf::PDFAppRecentModel expected;
    expected.setItems(items);
    QCOMPARE(model.rowCount(), expected.rowCount());
    const int roles[] = { Qt::DisplayRole, Qt::ToolTipRole, pdf::PDFComponentRoles::SubtitleRole, pdf::PDFComponentRoles::ProgressTextRole, pdf::PDFComponentRoles::ProgressRole,
                          pdf::PDFComponentRoles::DateRole, pdf::PDFComponentRoles::ItemTypeRole, pdf::PDFComponentRoles::PinnedRole, pdf::PDFComponentRoles::StarVisibleRole,
                          pdf::PDFComponentRoles::MissingRole, pdf::PDFComponentRoles::LockedRole, pdf::PDFAppRecentModel::HeaderRole, pdf::PDFAppRecentModel::CountRole };
    for (int row = 0; row < model.rowCount(); ++row)
    {
        for (int role : roles)
        {
            QVERIFY2(model.index(row, 0).data(role) == expected.index(row, 0).data(role), qPrintable(QStringLiteral("row %1 role %2").arg(row).arg(role)));
        }
        QCOMPARE(model.index(row, 0).data(pdf::PDFComponentRoles::PixmapRole).isNull(), expected.index(row, 0).data(pdf::PDFComponentRoles::PixmapRole).isNull());
        const pdf::PDFAppHomeItem& item = model.getItem(row)->item;
        const pdf::PDFAppHomeItem& expectedItem = expected.getItem(row)->item;
        QVERIFY(item.kind == expectedItem.kind && item.path == expectedItem.path && item.id == expectedItem.id && item.name == expectedItem.name &&
                item.isRemovable == expectedItem.isRemovable && item.documentCount == expectedItem.documentCount);
        QCOMPARE(model.flags(model.index(row, 0)), expected.flags(expected.index(row, 0)));
    }

    if (field != QLatin1String("same"))
    {
        return;
    }

    // The folder information: the details change once, the same details nothing
    model.setDetails(QStringLiteral("C:/Documents"), QStringLiteral("C:\\Documents · 6 PDF files"));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.index(2, 0).data(pdf::PDFComponentRoles::SubtitleRole).toString(), QStringLiteral("C:\\Documents · 6 PDF files"));
    model.setDetails(QStringLiteral("C:/Documents"), QStringLiteral("C:\\Documents · 6 PDF files"));
    model.setDetails(QStringLiteral("C:/Documents/Report.pdf"), QStringLiteral("not a folder"));
    QCOMPARE(changedSpy.count(), 1);
    model.setMissing(QStringLiteral("C:/Documents/Report.pdf"), true);
    model.setMissing(QStringLiteral("C:/Documents/Report.pdf"), true);
    model.setMissing(QStringLiteral("C:/Documents"), true);
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(model.index(1, 0).data(pdf::PDFComponentRoles::ProgressTextRole).toString(), QStringLiteral("No longer exists"));
    QVERIFY(!model.index(2, 0).data(pdf::PDFComponentRoles::MissingRole).toBool());

    // The star asks the page (files and folders only), the model does not change itself
    QSignalSpy pinSpy(&model, &pdf::PDFAppRecentModel::pinRequested);
    QVERIFY(!model.setData(model.index(0, 0), true, pdf::PDFComponentRoles::PinnedRole));
    QVERIFY(!model.setData(model.index(3, 0), true, pdf::PDFComponentRoles::PinnedRole));
    QVERIFY(!model.setData(model.index(1, 0), true, Qt::DisplayRole));
    QVERIFY(!model.setData(expected.index(1, 0), true, pdf::PDFComponentRoles::PinnedRole));
    QCOMPARE(pinSpy.count(), 0);
    QVERIFY(!model.setData(model.index(1, 0), true, pdf::PDFComponentRoles::PinnedRole));
    QVERIFY(!model.setData(model.index(2, 0), false, pdf::PDFComponentRoles::PinnedRole));
    QCOMPARE(pinSpy.count(), 2);
    QCOMPARE(pinSpy[0][0].value<pdf::PDFAppHomeItem>().path, QStringLiteral("C:/Documents/Report.pdf"));
    QCOMPARE(pinSpy[0][1].toBool(), true);
    QCOMPARE(pinSpy[1][0].value<pdf::PDFAppHomeItem>().kind, pdf::PDFAppHomeItemKind::Folder);
    QCOMPARE(pinSpy[1][1].toBool(), false);
    QVERIFY(!model.index(1, 0).data(pdf::PDFComponentRoles::PinnedRole).toBool());

    // Headers are not selectable, data of another model is refused
    QCOMPARE(model.flags(model.index(0, 0)), Qt::ItemIsEnabled);
    QCOMPARE(model.flags(model.index(1, 0)), Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    QVERIFY(model.data(expected.index(1, 0), Qt::DisplayRole).isNull());
    QCOMPARE(model.flags(expected.index(1, 0)), Qt::NoItemFlags);
}

void AppHomeTest::recentLockArrives()
{
    // The thumbnailer finds a password after the page is shown: the item of the
    // list gets the lock, although nothing else of it has changed (no picture,
    // the same page count)
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture, false);
    fixture.navigate(QStringLiteral("/recent?filter=files"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();

    auto findIndex = [&]()
    {
        for (int row = 0; row < page->getModel()->rowCount(); ++row)
        {
            const QModelIndex index = page->getModel()->index(row, 0);
            if (index.data(Qt::DisplayRole).toString() == QLatin1String("Invoice_2026.pdf"))
            {
                return index;
            }
        }
        return QModelIndex();
    };
    QVERIFY(findIndex().isValid());
    QVERIFY(!findIndex().data(pdf::PDFComponentRoles::LockedRole).toBool());
    const QString details = findIndex().data(pdf::PDFComponentRoles::SubtitleRole).toString();

    const qint64 modified = QFileInfo(sample.invoice).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    HomeTestHelper::runInThread([database, &sample, modified]()
    {
        pdf::PDFRecentRepository repository(database, nullptr);
        repository.setThumbnail(sample.invoice, QByteArray(), modified, pdf::PDFRecentThumbnailState::PasswordProtected, -1);
    });
    fixture.waitIdle();
    page = fixture.page<pdf::PDFAppRecentPage>();
    QVERIFY(findIndex().isValid());
    QCOMPARE(findIndex().data(pdf::PDFComponentRoles::SubtitleRole).toString(), details);
    QVERIFY(findIndex().data(pdf::PDFComponentRoles::LockedRole).toBool());
}

void AppHomeTest::recentListKeys()
{
    HomeFixture fixture;
    HomeTestHelper::fill(fixture);
    QStringList browsed;
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString& folder) { browsed << folder; return QStringList(); });
    fixture.navigate(QStringLiteral("/recent?filter=folders"));
    fixture.waitIdle();
    pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
    pdf::PDFAppRecentListView* view = page->getListView();
    pdf::PDFAppRecentModel* model = page->getModel();

    int folderRow = -1;
    for (int row = 0; row < model->rowCount() && folderRow < 0; ++row)
    {
        folderRow = model->getItem(row)->type == pdf::PDFAppRecentItem::Type::Folder ? row : -1;
    }
    QVERIFY(folderRow >= 0);
    const QString folder = model->getItem(folderRow)->item.path;

    // Enter on a folder browses the files in it (the native dialog of Open…)
    view->setFocus();
    view->setCurrentIndex(model->index(folderRow, 0));
    QTest::keyClick(view, Qt::Key_Return);
    HomeFixture::settle();
    QCOMPARE(browsed, QStringList({ folder }));

    // Enter with a modifier and other keys do not open the item
    QTest::keyClick(view, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClick(view, Qt::Key_Enter, Qt::ControlModifier);
    HomeFixture::settle();
    QCOMPARE(browsed.size(), 1);
    QCOMPARE(fixture.window->getRequestedFiles().size(), 0);

    // A click on the folder browses as well
    const QRect rect = view->visualRect(model->index(folderRow, 0));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.left() + 120, rect.center().y()));
    HomeFixture::settle();
    QCOMPARE(browsed, QStringList({ folder, folder }));

    // The star of a folder pins it as a location: its text says, what it does
    pdf::PDFAppRecentDelegate* delegate = page->getDelegate();
    QStringList starTexts;
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const pdf::PDFAppRecentItem* item = model->getItem(row);
        if (item->type != pdf::PDFAppRecentItem::Type::Folder)
        {
            continue;
        }
        QStyleOptionViewItem option;
        option.rect = view->visualRect(model->index(row, 0));
        for (const pdf::PDFRowDelegate::Action& action : delegate->getActions(option, model->index(row, 0)))
        {
            if (action.id == int(pdf::PDFRecentItemDelegate::PinAction))
            {
                QCOMPARE(action.text, item->isPinned ? QStringLiteral("Unpin from Pinned Locations") : QStringLiteral("Pin to Pinned Locations"));
                QCOMPARE(action.toolTip, action.text);
                starTexts << action.text;
            }
        }
    }
    QVERIFY(starTexts.contains(QStringLiteral("Unpin from Pinned Locations")));
    QVERIFY(starTexts.contains(QStringLiteral("Pin to Pinned Locations")));

    // A folder is found by its path too (Docs lies in Projects/PDF4QT)
    page->setFilter(pdf::PDFAppRecentPage::Filter::Folders, pdf::PDFRecentSort::LastOpened, QStringLiteral("PDF4QT"));
    QStringList names;
    for (const pdf::PDFAppRecentItem& item : model->getItems())
    {
        if (item.type != pdf::PDFAppRecentItem::Type::Header)
        {
            names << item.name;
        }
    }
    QCOMPARE(names, QStringList({ QStringLiteral("Docs") }));
    page->setFilter(pdf::PDFAppRecentPage::Filter::Folders, pdf::PDFRecentSort::LastOpened, QString());

    // The same filter, sort and search do not read the database again
    const int refreshes = page->getRefreshCount();
    page->setFilter(page->getFilter(), page->getSort(), page->getSearchText());
    QCOMPARE(page->getRefreshCount(), refreshes);
    page->setFilter(page->getFilter(), pdf::PDFRecentSort::Name, page->getSearchText());
    QCOMPARE(page->getRefreshCount(), refreshes + 1);

    // Right-to-left: the menu of a row opens at the menu button on the left side
    QApplication::setLayoutDirection(Qt::RightToLeft);
    HomeFixture::settle();
    page = fixture.page<pdf::PDFAppRecentPage>();
    page->showListMenu(folderRow);
    HomeFixture::settle();
    QPointer<QMenu> menu = page->getListMenu();
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    const QRect button = page->getDelegate()->getActionRect(page->getListView(), page->getModel()->index(folderRow, 0), pdf::PDFRecentItemDelegate::MenuAction);
    const QRect rowRect = page->getListView()->visualRect(page->getModel()->index(folderRow, 0));
    QVERIFY(button.isValid());
    QVERIFY(button.center().x() < rowRect.center().x());
    const QPoint buttonCenter = page->getListView()->viewport()->mapToGlobal(button.center());
    const QPoint rowCenter = page->getListView()->viewport()->mapToGlobal(rowRect.center());
    QVERIFY2(menu->geometry().center().x() < rowCenter.x(), qPrintable(QStringLiteral("menu %1, button %2").arg(menu->geometry().center().x()).arg(buttonCenter.x())));
    menu->close();
    HomeFixture::settle();
}

void AppHomeTest::withoutDatabase()
{
    // A context, whose storage is not open: no database, no notifier, no workers.
    // The pages show their empty states and every action does nothing.
    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT"), QStringLiteral("--portable") }, QDir::currentPath()));
    QVERIFY(!context.getDatabase());
    QVERIFY(!context.getNotifier());
    QVERIFY(!context.getThumbnailer());
    QVERIFY(!context.getFolderInfo());
    int dialogCalls = 0;
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&) { ++dialogCalls; return QDir::tempPath(); });
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget*, const QString&) { ++dialogCalls; return QStringList(); });

    const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("x.pdf"));
    std::vector<std::unique_ptr<pdf::PDFAppHomeBasePage>> pages;
    pages.push_back(std::make_unique<pdf::PDFAppHomePage>(&context));
    pages.push_back(std::make_unique<pdf::PDFAppOpenPage>(&context));
    pages.push_back(std::make_unique<pdf::PDFAppRecentPage>(&context));
    for (const std::unique_ptr<pdf::PDFAppHomeBasePage>& page : pages)
    {
        page->resize(1200, 900);
        page->show();
        HomeFixture::settle();
        QCOMPARE(page->getRefreshCount(), 1);
        QVERIFY(!page->getBrowseShortcutText().isEmpty());

        for (pdf::PDFAppHomeItemKind kind : { pdf::PDFAppHomeItemKind::File, pdf::PDFAppHomeItemKind::Folder, pdf::PDFAppHomeItemKind::Workspace, pdf::PDFAppHomeItemKind::Location })
        {
            pdf::PDFAppHomeItem item;
            item.kind = kind;
            item.path = kind == pdf::PDFAppHomeItemKind::File ? path : QDir::tempPath();
            item.id = 1;
            for (int action = int(pdf::PDFAppHomeAction::Open); action <= int(pdf::PDFAppHomeAction::RemoveLocation); ++action)
            {
                if (pdf::PDFAppHomeAction(action) == pdf::PDFAppHomeAction::ShowInFolder)
                {
                    // Not a part of the storage (the navigator of a window opens it)
                    continue;
                }
                QVERIFY(page->performAction(item, pdf::PDFAppHomeAction(action)));
            }
        }
        page->openWorkspace(1);
        page->restoreLastSession();
        page->setFilePinned(path, true);
        page->setFolderPinned(QDir::tempPath(), true);
        page->setFolderPinned(QDir::tempPath(), false);
        page->addPinnedLocation();
        page->openFile(path);
        page->browseFiles();
        page->showRenameWorkspaceDialog(1);
        page->showDeleteWorkspaceDialog(1);
        page->showRenameLocationDialog(1);
        HomeFixture::settle();
        QVERIFY(!page->getDialog());
        QVERIFY(!page->getErrorBox());

        // A menu without a parent belongs to the page
        pdf::PDFAppHomeItem file;
        file.path = path;
        QMenu* menu = page->createItemMenu(file, nullptr);
        QCOMPARE(menu->parent(), page.get());
        QCOMPARE(HomeTestHelper::menuTexts(menu), QStringList({ QStringLiteral("Open"), QStringLiteral("Pin"), QStringLiteral("Show in Folder"), QStringLiteral("Copy Path"), QStringLiteral("|"),
                                                                QStringLiteral("Remove from List") }));
        delete menu;

        // Clear history shows zeros and clears nothing
        page->showClearHistoryDialog();
        QPointer<pdf::PDFAppClearHistoryDialog> dialog = qobject_cast<pdf::PDFAppClearHistoryDialog*>(page->getDialog());
        QVERIFY(dialog);
        QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("0 entries"));
        QVERIFY(!dialog->getPinnedCheckBox()->isVisibleTo(dialog));
        dialog->getClearButton()->click();
        HomeFixture::settle();
        QVERIFY(!dialog);
        QVERIFY(!page->getErrorBox());
    }
    QCOMPARE(dialogCalls, 0);

    pdf::PDFAppRecentPage* recent = static_cast<pdf::PDFAppRecentPage*>(pages[2].get());
    QVERIFY(recent->isEmptyStateShown());
    QVERIFY(!recent->getContinueSection()->isVisible());
    QVERIFY(!recent->getWorkspacesCard()->isVisible());
    QVERIFY(!recent->getPinnedSection()->isVisible());
    pages.clear();
    HomeFixture::settle();
    pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
}

void AppHomeTest::dialogEdgeCases()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();

    // Enter in a name of spaces does nothing (the action is disabled)
    page->showRenameWorkspaceDialog(sample.thesisId);
    QPointer<pdf::PDFAppRenameWorkspaceDialog> rename = qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(page->getDialog());
    QVERIFY(rename);
    rename->getNameField()->setText(QStringLiteral("   "));
    QVERIFY(!rename->getActionButton()->isEnabled());
    QSignalSpy returnSpy(rename->getNameField(), &QLineEdit::returnPressed);
    QTest::keyClick(rename->getNameField(), Qt::Key_Return);
    HomeFixture::settle();
    QCOMPARE(returnSpy.count(), 1);
    QVERIFY(rename);
    QVERIFY(rename->isVisible());
    QVERIFY(rename->getErrorText().isEmpty());
    QCOMPARE(fixture.workspaces().getWorkspace(sample.thesisId)->name, QStringLiteral("Thesis review"));
    rename->reject();
    HomeFixture::settle();
    QVERIFY(!rename);

    // The last session and missing ids have no dialogs
    const qint64 sessionId = fixture.workspaces().getLastSession()->id;
    for (qint64 id : { sessionId, qint64(987654) })
    {
        page->showRenameWorkspaceDialog(id);
        QVERIFY(!page->getDialog());
        page->showDeleteWorkspaceDialog(id);
        QVERIFY(!page->getDialog());
        page->showRenameLocationDialog(id);
        QVERIFY(!page->getDialog());
    }
    QVERIFY(fixture.workspaces().getLastSession());

    // The second location is found by its id
    qint64 secondId = 0;
    QVERIFY(!fixture.recent().addPinnedLocation(QFileInfo(sample.invoice).absolutePath(), QStringLiteral("Downloads here"), &secondId).isError());
    page->showRenameLocationDialog(secondId);
    QPointer<pdf::PDFAppRenameLocationDialog> location = qobject_cast<pdf::PDFAppRenameLocationDialog*>(page->getDialog());
    QVERIFY(location);
    QCOMPARE(location->getLocationId(), secondId);
    QCOMPARE(location->getNameField()->text(), QStringLiteral("Downloads here"));
    location->reject();
    HomeFixture::settle();

    // Save as workspace: at most MAXIMUM_DOCUMENTS rows, a page without the page count
    std::vector<pdf::PDFAppOpenDocument> documents(pdf::PDFWorkspaceRepository::MAXIMUM_DOCUMENTS + 5);
    for (size_t i = 0; i < documents.size(); ++i)
    {
        documents[i].path = QDir(fixture.directory.path()).filePath(QStringLiteral("Many/%1.pdf").arg(i));
    }
    documents[0].page = 7;
    documents[1].page = 3;
    documents[1].pageCount = 9;
    {
        pdf::PDFAppSaveWorkspaceDialog save(fixture.context.get(), documents, nullptr);
        QCOMPARE(save.getDocumentCheckBoxes().size(), pdf::PDFWorkspaceRepository::MAXIMUM_DOCUMENTS);
        QCOMPARE(save.getSelectionText(), QStringLiteral("1000 of 1000 selected"));
        QCOMPARE(save.getSelectedDocuments().size(), size_t(pdf::PDFWorkspaceRepository::MAXIMUM_DOCUMENTS));
        QStringList details;
        for (QWidget* row : save.findChildren<QWidget*>(QStringLiteral("documentRow")))
        {
            for (pdf::PDFAppElidedLabel* label : row->findChildren<pdf::PDFAppElidedLabel*>())
            {
                if (label->getTextKind() == pdf::PDFComponentPainter::TextKind::Data)
                {
                    details << label->text();
                }
                if (details.size() >= 2)
                {
                    break;
                }
            }
            if (details.size() >= 2)
            {
                break;
            }
        }
        QCOMPARE(details.size(), 2);
        QVERIFY2(details[0].endsWith(QStringLiteral(" · page 7")), qPrintable(details[0]));
        QVERIFY2(details[1].endsWith(QStringLiteral(" · page 3 of 9")), qPrintable(details[1]));
    }

    // Clear history of one pinned file without other entries: the singular texts,
    // the option stays while it is checked, also when the pin goes meanwhile
    QVERIFY(!fixture.recent().clearHistory(true).isError());
    const QString pinned = HomeTestHelper::record(fixture, sample.invoice, HomeFixture::at(0, 9, 0), 0, 0, false, true);
    QVERIFY(!pinned.isEmpty());
    for (const pdf::PDFRecentFolder& usedFolder : fixture.recent().getUsedFolders())
    {
        QVERIFY(!fixture.recent().removeFolder(usedFolder.path).isError());
    }
    for (const pdf::PDFWorkspace& workspace : fixture.workspaces().getRecentWorkspaces())
    {
        QVERIFY(!fixture.workspaces().deleteWorkspace(workspace.id).isError());
    }
    QCOMPARE(fixture.recent().getClearHistorySummary(false).getRemovedEntries(), 0);
    page->showClearHistoryDialog();
    QPointer<pdf::PDFAppClearHistoryDialog> clear = qobject_cast<pdf::PDFAppClearHistoryDialog*>(page->getDialog());
    QVERIFY(clear);
    QCOMPARE(clear->getFacts()->getValue(0), QStringLiteral("0 entries"));
    QCOMPARE(clear->getFacts()->getValue(2), QStringLiteral("1 pinned item, saved workspaces"));
    QCOMPARE(clear->getPinnedCheckBox()->text(), QStringLiteral("Also remove the pinned items (1)"));
    clear->getPinnedCheckBox()->click();
    QCOMPARE(clear->getFacts()->getValue(0), QStringLiteral("1 entry"));
    clear->getPinnedCheckBox()->click();
    QVERIFY(!fixture.recent().setPinned(pinned, false).isError());
    clear->getPinnedCheckBox()->click();
    QVERIFY(clear->getPinnedCheckBox()->isChecked());
    QVERIFY(clear->getPinnedCheckBox()->isVisibleTo(clear));
    QCOMPARE(clear->getPinnedCheckBox()->text(), QStringLiteral("Also remove the pinned items (0)"));
    clear->getPinnedCheckBox()->click();
    QVERIFY(!clear->getPinnedCheckBox()->isVisibleTo(clear));
    clear->reject();
    HomeFixture::settle();

    // A folder without a path is not pinned; Add… of Pinned Locations does not open
    // a second dialog from inside of the first one
    const size_t locations = fixture.recent().getPinnedLocations().size();
    page->setFolderPinned(QString(), true);
    QCOMPARE(fixture.recent().getPinnedLocations().size(), locations);
    int folderDialogs = 0;
    QPointer<pdf::PDFAppOpenPage> guard(page);
    const QString newLocation = QDir(fixture.directory.path()).filePath(QStringLiteral("Desktop"));
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString&)
    {
        ++folderDialogs;
        if (guard)
        {
            guard->addPinnedLocation();
        }
        return newLocation;
    });
    page->addPinnedLocation();
    HomeFixture::settle();
    QCOMPARE(folderDialogs, 1);
    QVERIFY(fixture.recent().hasPinnedLocation(newLocation));
    QCOMPARE(fixture.recent().getPinnedLocations().size(), locations + 1);
}

void AppHomeTest::databaseFaults()
{
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    fixture.navigate(QStringLiteral("/open"));
    pdf::PDFAppOpenPage* page = fixture.page<pdf::PDFAppOpenPage>();
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    auto execute = [database](const char* sql)
    {
        pdf::PDFDatabaseStatement statement(database->getConnection(), sql);
        return statement.execute();
    };

    // A write refused by the database (temporary triggers of the connection of the
    // GUI thread): the dialogs keep the error and stay open, nothing changes
    QVERIFY(execute("CREATE TEMP TRIGGER test_refuse_workspace BEFORE UPDATE ON workspace BEGIN SELECT RAISE(ABORT, 'refused by the test'); END"));
    QVERIFY(execute("CREATE TEMP TRIGGER test_refuse_location BEFORE UPDATE ON pinned_location BEGIN SELECT RAISE(ABORT, 'refused by the test'); END"));

    page->showRenameWorkspaceDialog(sample.thesisId);
    QPointer<pdf::PDFAppRenameWorkspaceDialog> rename = qobject_cast<pdf::PDFAppRenameWorkspaceDialog*>(page->getDialog());
    QVERIFY(rename);
    rename->getNameField()->setText(QStringLiteral("Refused"));
    rename->getActionButton()->click();
    HomeFixture::settle();
    QVERIFY(rename);
    QVERIFY(!rename->getErrorText().isEmpty());
    QCOMPARE(fixture.workspaces().getWorkspace(sample.thesisId)->name, QStringLiteral("Thesis review"));
    rename->reject();
    HomeFixture::settle();

    page->showRenameLocationDialog(sample.locationId);
    QPointer<pdf::PDFAppRenameLocationDialog> location = qobject_cast<pdf::PDFAppRenameLocationDialog*>(page->getDialog());
    QVERIFY(location);
    location->getNameField()->setText(QStringLiteral("Refused"));
    location->getActionButton()->click();
    HomeFixture::settle();
    QVERIFY(location);
    QVERIFY(!location->getErrorText().isEmpty());
    QCOMPARE(fixture.recent().getPinnedLocations().front().name, QStringLiteral("Work Projects"));
    location->reject();
    HomeFixture::settle();

    QVERIFY(execute("DROP TRIGGER temp.test_refuse_workspace"));
    QVERIFY(execute("DROP TRIGGER temp.test_refuse_location"));

    // Another connection holds the write lock: the page says, that another window
    // is saving, and the change is not made
    QSemaphore locked;
    QSemaphore release;
    bool isLocked = false;
    QThread* holder = QThread::create([database, &locked, &release, &isLocked]()
    {
        pdf::PDFDatabaseStatement begin(database->getConnection(), "BEGIN IMMEDIATE");
        isLocked = begin.execute();
        locked.release();
        release.acquire();
        if (isLocked)
        {
            pdf::PDFDatabaseStatement rollback(database->getConnection(), "ROLLBACK");
            rollback.execute();
        }
    });
    holder->start();
    locked.acquire();
    QVERIFY(isLocked);
    database->getConnection()->setBusyTimeout(0);

    pdf::PDFAppHomeItem item;
    item.kind = pdf::PDFAppHomeItemKind::File;
    item.path = sample.invoice;
    const bool isAlive = page->performAction(item, pdf::PDFAppHomeAction::Pin);
    release.release();
    holder->wait();
    delete holder;
    database->getConnection()->setBusyTimeout(5000);

    QVERIFY(isAlive);
    QPointer<QDialog> box = page->getErrorBox();
    QVERIFY(box);
    QVERIFY(box->isVisible());
    QCOMPARE(box->accessibleDescription(), QStringLiteral("Another PDF4QT window is saving its settings. Try it again in a moment."));
    QVERIFY(!fixture.recent().getFile(sample.invoice)->pinned);
    box->close();
    HomeFixture::settle();

    // The lock is gone: the same action works
    QVERIFY(page->performAction(item, pdf::PDFAppHomeAction::Pin));
    QVERIFY(!page->getErrorBox());
    QVERIFY(fixture.recent().getFile(sample.invoice)->pinned);

    // A workspace of rows of another version without a valid document: it is
    // marked opened, no file is opened (a last session without a valid document
    // is not read by the repository at all)
    QVERIFY(!fixture.workspaces().deleteLastSession().isError());
    QVERIFY(execute("INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('Only invalid', 1, 1, 1, 0)"));
    QVERIFY(execute("INSERT INTO workspace_document(workspace_id, position, path, page) VALUES ((SELECT MAX(id) FROM workspace), 0, 'relative.pdf', 1)"));
    QVERIFY(execute("INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('Last session', 1, 1, 1, 1)"));
    QVERIFY(execute("INSERT INTO workspace_document(workspace_id, position, path, page) VALUES ((SELECT MAX(id) FROM workspace), 0, 'relative.pdf', 1)"));
    QVERIFY(!fixture.workspaces().getLastSession());
    const std::optional<pdf::PDFWorkspace> invalid = fixture.workspaces().findWorkspace(QStringLiteral("Only invalid"));
    QVERIFY(invalid);
    const qsizetype requested = fixture.window->getRequestedFiles().size();
    page->openWorkspace(invalid->id);
    page->restoreLastSession();
    HomeFixture::settle();
    QCOMPARE(fixture.window->getRequestedFiles().size(), requested);
    QVERIFY(fixture.workspaces().getWorkspace(invalid->id)->opened > 1);
    QVERIFY(!fixture.workspaces().getLastSession());
    QVERIFY(!page->getErrorBox());
}

void AppHomeTest::manyThumbnails()
{
    // More files with thumbnails than the page keeps decoded: every item of the
    // list still has its picture (the cache is emptied and filled again)
    HomeFixture fixture;
    const int count = 530;
    QByteArray png = HomeTestHelper::createPng(QColor(0, 120, 0), QSize(6, 8));
    const QByteArray sql = QByteArrayLiteral("INSERT INTO recent_file(path, name, folder, last_opened, thumbnail, thumbnail_state, thumbnail_mtime) "
                                             "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < ") + QByteArray::number(count) +
                           QByteArrayLiteral(") SELECT '/many/file' || i || '.pdf', 'file' || i || '.pdf', '/many', i, X'") + png.toHex() + QByteArrayLiteral("', 1, 1 FROM n");
    {
        pdf::PDFDatabaseStatement statement(fixture.context->getDatabase()->getConnection(), sql.constData());
        QVERIFY(statement.execute());
    }
    Q_EMIT fixture.context->getNotifier()->everythingChanged();

    fixture.navigate(QStringLiteral("/recent?filter=files"));
    fixture.waitIdle();
    for (int pass = 0; pass < 2; ++pass)
    {
        pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>();
        int pictures = 0;
        for (int row = 0; row < page->getModel()->rowCount(); ++row)
        {
            const QModelIndex index = page->getModel()->index(row, 0);
            if (!index.data(pdf::PDFAppRecentModel::HeaderRole).toBool())
            {
                const QPixmap pixmap = index.data(pdf::PDFComponentRoles::PixmapRole).value<QPixmap>();
                pictures += !pixmap.isNull() && pixmap.toImage().pixelColor(2, 2) == QColor(0, 120, 0) ? 1 : 0;
            }
        }
        QCOMPARE(pictures, count);

        // Read again (the decoded pictures are reused or decoded again)
        page->refreshNow();
        HomeFixture::settle();
    }
}

void AppHomeTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: pictures of the pages in both themes at 1x and
    // 2x (the comparison with Figma 28:4, 61:1201, 80:910, 955:142865, 955:150880,
    // 955:152339 and the dialogs 941:135742, 941:136915, 955:147918, 955:149658, 955:150171)
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    HomeFixture fixture;
    HomeSample sample = HomeTestHelper::fill(fixture);
    pdf::PDFAppDocumentActions::setOpenDocumentsProvider([](const QWidget*)
    {
        std::vector<pdf::PDFAppOpenDocument> documents(3);
        documents[0].path = QStringLiteral("C:/Users/Jakub/Documents/Project_Report.pdf");
        documents[0].page = 12;
        documents[0].pageCount = 24;
        documents[1].path = QStringLiteral("C:/Users/Jakub/Downloads/Invoice.pdf");
        documents[1].page = 1;
        documents[1].pageCount = 2;
        documents[1].isModified = true;
        documents[2].title = QStringLiteral("Untitled 1");
        return documents;
    });

    struct Shot
    {
        QString route;
        QString name;
        QSize size;
        std::function<void(HomeFixture&)> prepare;
    };

    const auto noPreparation = [](HomeFixture&) { };
    const auto openMenu = [](HomeFixture& home)
    {
        pdf::PDFAppOpenPage* page = home.page<pdf::PDFAppOpenPage>();
        if (page && !page->getRecentFileRows().isEmpty())
        {
            page->getRecentFileRows().front()->getMenuButton()->click();
        }
    };
    const auto sortPopup = [](HomeFixture& home)
    {
        pdf::PDFAppRecentPage* page = home.page<pdf::PDFAppRecentPage>();
        if (page)
        {
            page->getSortBox()->showPopup();
        }
    };
    const auto clearDialog = [](HomeFixture& home)
    {
        if (pdf::PDFAppRecentPage* page = home.page<pdf::PDFAppRecentPage>())
        {
            page->showClearHistoryDialog();
        }
    };
    const auto saveDialog = [](HomeFixture& home)
    {
        if (pdf::PDFAppOpenPage* page = home.page<pdf::PDFAppOpenPage>())
        {
            page->showSaveWorkspaceDialog();
        }
    };
    const auto duplicateDialog = [](HomeFixture& home)
    {
        if (pdf::PDFAppOpenPage* page = home.page<pdf::PDFAppOpenPage>())
        {
            page->showSaveWorkspaceDialog();
            if (pdf::PDFAppSaveWorkspaceDialog* dialog = qobject_cast<pdf::PDFAppSaveWorkspaceDialog*>(page->getDialog()))
            {
                dialog->getNameField()->setText(QStringLiteral("Invoices 2026"));
                dialog->getSaveButton()->click();
            }
        }
    };
    const auto deleteDialog = [&sample](HomeFixture& home)
    {
        if (pdf::PDFAppOpenPage* page = home.page<pdf::PDFAppOpenPage>())
        {
            page->showDeleteWorkspaceDialog(sample.thesisId);
        }
    };
    const auto renameDialog = [&sample](HomeFixture& home)
    {
        if (pdf::PDFAppOpenPage* page = home.page<pdf::PDFAppOpenPage>())
        {
            page->showRenameWorkspaceDialog(sample.thesisId);
        }
    };

    const Shot shots[] = { { QStringLiteral("/home"), QStringLiteral("home"), QSize(1440, 961), noPreparation },
                           { QStringLiteral("/open"), QStringLiteral("open"), QSize(1440, 1303), noPreparation },
                           { QStringLiteral("/recent"), QStringLiteral("recent"), QSize(1440, 948), noPreparation },
                           { QStringLiteral("/recent"), QStringLiteral("recent-sort"), QSize(1440, 948), sortPopup },
                           { QStringLiteral("/recent"), QStringLiteral("recent-clear-history"), QSize(1440, 948), clearDialog },
                           { QStringLiteral("/open"), QStringLiteral("open-menu"), QSize(1440, 1303), openMenu },
                           { QStringLiteral("/open"), QStringLiteral("open-save-workspace"), QSize(1440, 1303), saveDialog },
                           { QStringLiteral("/open"), QStringLiteral("open-save-workspace-duplicate"), QSize(1440, 1303), duplicateDialog },
                           { QStringLiteral("/open"), QStringLiteral("open-workspace-delete"), QSize(1440, 1303), deleteDialog },
                           { QStringLiteral("/open"), QStringLiteral("open-workspace-rename"), QSize(1440, 1303), renameDialog },
                           { QStringLiteral("/recent"), QStringLiteral("recent-portable"), QSize(1440, 1048), noPreparation },
                           { QStringLiteral("/home"), QStringLiteral("home-minimal"), QSize(720, 480), noPreparation },
                           { QStringLiteral("/open"), QStringLiteral("open-minimal"), QSize(720, 480), noPreparation },
                           { QStringLiteral("/recent"), QStringLiteral("recent-minimal"), QSize(720, 480), noPreparation } };

    for (const Shot& shot : shots)
    {
        fixture.window->resize(shot.size);
        fixture.navigate(shot.route);
        QVERIFY(fixture.currentPage());
        fixture.homeTab()->getBanner()->setVisible(shot.name.endsWith(QLatin1String("portable")));
        fixture.waitIdle();
        shot.prepare(fixture);
        HomeFixture::settle();

        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            HomeFixture::settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QImage image = HomeTestHelper::render(fixture.window, ratio);
                QVERIFY(!image.isNull());

                // A popup or a dialog is a window of its own: drawn over the picture
                QPainter painter(&image);
                for (QWidget* widget : QApplication::topLevelWidgets())
                {
                    if (widget != fixture.window && widget->isVisible() && (qobject_cast<QMenu*>(widget) || qobject_cast<QDialog*>(widget) || widget->inherits("QComboBoxPrivateContainer")))
                    {
                        const QPoint position = fixture.window->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0)));
                        widget->render(&painter, position, QRegion(), QWidget::DrawChildren);
                    }
                }
                painter.end();

                QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString name = QStringLiteral("%1-%2-%3x.png").arg(shot.name, scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(ratio));
                    QVERIFY(image.save(QDir(folder).filePath(name)));
                }
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

        // Popups and dialogs of the shot are closed
        if (QMenu* menu = HomeTestHelper::visibleMenu())
        {
            menu->close();
        }
        if (pdf::PDFAppHomeBasePage* page = qobject_cast<pdf::PDFAppHomeBasePage*>(fixture.currentPage()))
        {
            if (QDialog* dialog = page->getDialog())
            {
                dialog->reject();
            }
        }
        if (pdf::PDFAppRecentPage* page = fixture.page<pdf::PDFAppRecentPage>())
        {
            page->getSortBox()->hidePopup();
        }
        HomeFixture::settle();
    }
    Q_UNUSED(sample);
}

void AppHomeTest::screenshotsStates()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the states without a picture in screenshots():
    // New tab (955:152339), the empty history (955:142865), the empty Home, missing
    // and password protected files (no Figma frame) and right-to-left
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    auto save = [&folder](QWidget* window, const QString& name)
    {
        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            HomeFixture::settle();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                const QImage image = HomeTestHelper::render(window, ratio);
                QVERIFY(!image.isNull());
                QCOMPARE(HomeTestHelper::countLeakPixels(image), 0);
                if (!folder.isEmpty())
                {
                    QDir().mkpath(folder);
                    const QString fileName = QStringLiteral("%1-%2-%3x.png").arg(name, scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark")).arg(int(ratio));
                    QVERIFY(image.save(QDir(folder).filePath(fileName)));
                }
            }
        }
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        HomeFixture::settle();
    };

    {
        // The empty Home (the hints of Recent Files and Recent Tools)
        HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 961));
        fixture.navigate(QStringLiteral("/home"));
        fixture.hideBanner();
        fixture.waitIdle();
        save(fixture.window, QStringLiteral("home-empty"));
    }

    HomeFixture fixture(QStringList({ QStringLiteral("--portable") }), QSize(1440, 1264));
    HomeSample sample = HomeTestHelper::fill(fixture);

    // New tab
    pdf::PDFAppTab* tab = fixture.window->openNewTab();
    QVERIFY(tab);
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("new-tab"));
    fixture.window->activateTab(fixture.homeTab());
    HomeFixture::settle();

    // Missing and password protected files
    QVERIFY(QFile::remove(sample.buildingPlan));
    const QDateTime modified = QFileInfo(sample.invoice).lastModified(QTimeZone::UTC);
    QVERIFY(!fixture.recent().setThumbnail(sample.invoice, QByteArray(), modified.toMSecsSinceEpoch(), pdf::PDFRecentThumbnailState::PasswordProtected, 0).isError());
    fixture.hideBanner();
    fixture.window->resize(1440, 961);
    fixture.navigate(QStringLiteral("/home"));
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("home-states"));
    fixture.window->resize(1440, 948);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("recent-states"));

    // Right-to-left
    QApplication::setLayoutDirection(Qt::RightToLeft);
    HomeFixture::settle();
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("recent-rtl"));
    fixture.window->resize(1440, 961);
    fixture.navigate(QStringLiteral("/home"));
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("home-rtl"));
    fixture.window->resize(1440, 1303);
    fixture.navigate(QStringLiteral("/open"));
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("open-rtl"));
    QApplication::setLayoutDirection(Qt::LeftToRight);
    HomeFixture::settle();

    // The empty history (the pinned items and the workspaces stay)
    QVERIFY(!fixture.recent().clearHistory(false).isError());
    fixture.window->resize(1440, 948);
    fixture.navigate(QStringLiteral("/recent"));
    fixture.waitIdle();
    save(fixture.window, QStringLiteral("recent-empty"));
}

QTEST_MAIN(AppHomeTest)

#include "tst_apphometest.moc"
