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

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfoperationcontrol.h"
#include "pdfsecurityhandler.h"

#include "pdfappcontext.h"
#include "pdfappfolderinfo.h"
#include "pdfappsysteminfo.h"
#include "pdfappthumbnailer.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabaseschema.h"
#include "pdfdatabasestatement.h"
#include "pdfrecentrepository.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftoolrepository.h"
#include "pdftoolcollectionrepository.h"
#include "pdfappbuiltintoolcollections.h"
#include "pdfapptoolcollections.h"
#include "pdfapptoolstate.h"
#include "pdfdocumenttoolsuggestions.h"
#include "pdfworkspacerepository.h"
#include "pdfdocumenttoolusage.h"
#include "pdftoolparameterrepository.h"
#include "testdocumenttool.h"

#include "pdfthemeiconengine.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTranslator>
#include <QtTest>

#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>

/// Helper functions of the test
class AppDataTestHelper
{
public:
    AppDataTestHelper() = delete;

    /// Time factor of the waits (valgrind is slower)
    static int getTimeoutFactor()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        if (ok && factor > 0)
        {
            return factor;
        }
        if (qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")))
        {
            return 20;
        }
        return 1;
    }

    static QString createMemoryName()
    {
        return QStringLiteral("/pdf4qt-appdata-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
    }

    /// Options of a portable memory database of this process
    static pdf::PDFDatabaseOptions createMemoryOptions()
    {
        pdf::PDFDatabaseOptions options;
        options.portable = true;
        options.memoryName = createMemoryName();
        return options;
    }

    /// Options of a database file
    static pdf::PDFDatabaseOptions createFileOptions(const QString& filePath)
    {
        pdf::PDFDatabaseOptions options;
        options.filePath = filePath;
        options.memoryName = createMemoryName();
        return options;
    }

    static bool execute(pdf::PDFDatabase* database, const char* sql)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection && !connection->execute(sql).isError();
    }

    static qint64 queryInteger(pdf::PDFDatabase* database, const char* sql, qint64 defaultValue = -1)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection ? connection->queryInteger(sql, defaultValue) : defaultValue;
    }

    /// Writes a file with the content
    static bool writeFile(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }
        return file.write(data) == data.size();
    }

    /// Creates a PDF file: pages of the size, page 1 has a red rectangle in its upper left quarter
    static bool createPdf(const QString& filePath, int pageCount = 1, QSizeF pageSize = QSizeF(300, 400), pdf::PageRotation rotation = pdf::PageRotation::None,
                          const QString& userPassword = QString(), const QString& ownerPassword = QString(), int extraRectangles = 0)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), pageSize));
            if (i == 0)
            {
                pdf::PDFPageContentStreamBuilder content(&builder);
                QPainter* painter = content.begin(page);
                if (!painter)
                {
                    return false;
                }
                painter->fillRect(QRectF(0, 0, pageSize.width() / 2, pageSize.height() / 2), Qt::red);
                for (int j = 0; j < extraRectangles; ++j)
                {
                    painter->fillRect(QRectF(pageSize.width() / 2 + (j % 100), pageSize.height() / 2 + (j / 100 % 100), 1, 1), QColor(0, 0, 255, 128));
                }
                content.end(painter);
            }
            if (rotation != pdf::PageRotation::None)
            {
                builder.setPageRotation(page, rotation);
            }
        }

        if (!userPassword.isEmpty() || !ownerPassword.isEmpty())
        {
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.userPassword = userPassword;
            settings.ownerPassword = ownerPassword.isEmpty() ? QStringLiteral("owner-%1").arg(userPassword) : ownerPassword;
            settings.permissions = 0xFFFFFFFCu;
            builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        }

        pdf::PDFDocument document = builder.build();
        return bool(pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false));
    }

    /// Creates a PDF file without any page
    static bool createEmptyPdf(const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFDocument document = builder.build();
        return bool(pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false));
    }

    /// Sets the time of the modification of a file
    static bool setModified(const QString& filePath, const QDateTime& time)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadWrite))
        {
            return false;
        }
        return file.setFileTime(time, QFileDevice::FileModificationTime);
    }

    /// PNG of a small picture
    static QByteArray createPng(QSize size = QSize(4, 4))
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return data;
    }

    /// PDF written by hand: the objects 1, 2, ... (object 1 is the catalog) with a
    /// correct cross-reference table
    static QByteArray createRawPdf(const QList<QByteArray>& objects)
    {
        QByteArray data = "%PDF-1.7\n";
        QList<qint64> offsets;
        for (qsizetype i = 0; i < objects.size(); ++i)
        {
            offsets << data.size();
            data += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
        }
        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
        for (qint64 offset : offsets)
        {
            data += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
        }
        data += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    /// Opens a portable memory database
    static std::unique_ptr<pdf::PDFDatabase> openMemoryDatabase()
    {
        auto database = std::make_unique<pdf::PDFDatabase>();
        database->open(createMemoryOptions());
        return database;
    }

    static QStringList getPaths(const std::vector<pdf::PDFRecentFile>& files)
    {
        QStringList paths;
        for (const pdf::PDFRecentFile& file : files)
        {
            paths << file.path;
        }
        return paths;
    }

    static QStringList getNames(const std::vector<pdf::PDFRecentFile>& files)
    {
        QStringList names;
        for (const pdf::PDFRecentFile& file : files)
        {
            names << file.name;
        }
        return names;
    }

    static int s_counter;
};

int AppDataTestHelper::s_counter = 0;

/// Fixture of the repository tests: a memory database, the registry of the
/// settings, a temporary folder with files
class DataFixture
{
public:
    DataFixture() :
        database(AppDataTestHelper::openMemoryDatabase())
    {
        pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);
    }

    pdf::PDFRecentRepository recent(qint64 time = 0) const
    {
        pdf::PDFRecentRepository repository(database.get(), &registry);
        repository.setCurrentTime(time);
        return repository;
    }

    pdf::PDFWorkspaceRepository workspaces(qint64 time = 0) const
    {
        pdf::PDFWorkspaceRepository repository(database.get());
        repository.setCurrentTime(time);
        return repository;
    }

    pdf::PDFToolRepository tools(qint64 time = 0) const
    {
        pdf::PDFToolRepository repository(database.get());
        repository.setCurrentTime(time);
        return repository;
    }

    pdf::PDFToolCollectionRepository collections(qint64 time = 0) const
    {
        pdf::PDFToolCollectionRepository repository(database.get());
        repository.setCurrentTime(time);
        return repository;
    }

    pdf::PDFSettingsRepository settings() const { return pdf::PDFSettingsRepository(database.get(), &registry); }

    /// Creates a file in the temporary folder, returns its stored (normalized) path
    QString createFile(const QString& relativePath, const QByteArray& data = QByteArray("%PDF-1.7\n"))
    {
        const QString filePath = directory.filePath(relativePath);
        QDir().mkpath(QFileInfo(filePath).absolutePath());
        if (!AppDataTestHelper::writeFile(filePath, data))
        {
            return QString();
        }
        return pdf::PDFRepositoryHelper::normalizePath(filePath);
    }

    /// Text of everything the repositories read (a failed operation must leave it unchanged)
    QString snapshot() const
    {
        QStringList lines;
        for (const pdf::PDFRecentFile& file : recent().getFiles())
        {
            lines << QStringLiteral("file %1 %2 %3 %4 %5 %6 %7 %8 %9").arg(file.path).arg(file.lastOpened).arg(file.openCount).arg(int(file.pinned)).arg(file.pinOrder)
                                                                    .arg(file.lastPage).arg(file.pageCount).arg(int(file.thumbnailState)).arg(int(file.hasThumbnail));
        }
        for (const pdf::PDFRecentFolder& folder : recent().getUsedFolders())
        {
            lines << QStringLiteral("folder %1 %2").arg(folder.path).arg(folder.lastUsed);
        }
        for (const pdf::PDFPinnedLocation& location : recent().getPinnedLocations())
        {
            lines << QStringLiteral("location %1 %2 %3 %4").arg(location.id).arg(location.path, location.name).arg(location.position);
        }
        std::vector<pdf::PDFWorkspace> workspaceList = workspaces().getWorkspaces(true);
        if (std::optional<pdf::PDFWorkspace> lastSession = workspaces().getLastSession())
        {
            workspaceList.push_back(*lastSession);
        }
        for (const pdf::PDFWorkspace& workspace : workspaceList)
        {
            lines << QStringLiteral("workspace %1 %2 %3 %4 %5").arg(workspace.id).arg(workspace.name).arg(workspace.opened).arg(workspace.saved).arg(int(workspace.isLastSession));
            for (const pdf::PDFWorkspaceDocument& document : workspace.documents)
            {
                lines << QStringLiteral("  document %1 %2").arg(document.path).arg(document.page);
            }
        }
        lines << QStringLiteral("tools ") + tools().getPinnedTools().join(QLatin1Char(','));
        for (const pdf::PDFToolUsage& usage : tools().getUsage())
        {
            lines << QStringLiteral("usage %1 %2 %3").arg(usage.toolId).arg(usage.lastUsed).arg(usage.count);
        }
        for (const pdf::PDFToolCollectionRow& collection : collections().getCollections())
        {
            lines << QStringLiteral("collection %1 %2 %3 %4 %5 %6").arg(collection.id).arg(collection.builtinId, collection.name, collection.iconName).arg(collection.modified)
                                                                  .arg(collection.toolIds.join(QLatin1Char(',')));
        }
        return lines.join(QLatin1Char('\n'));
    }

    QTemporaryDir directory;
    std::unique_ptr<pdf::PDFDatabase> database;
    pdf::PDFSettingRegistry registry;
};

/// Fault injection of the repository tests by TEMP objects of the connection of the
/// test thread. The repositories of the same thread use the same connection; the
/// objects are not a part of the database (another connection does not see them).
class AppDataFault
{
public:
    AppDataFault() = delete;

    /// Statements of the kind (INSERT, UPDATE, DELETE), which change the table, fail
    static bool abortWrites(pdf::PDFDatabase* database, const QString& kind, const QString& table)
    {
        const QString sql = QStringLiteral("CREATE TEMP TRIGGER fault_abort BEFORE %1 ON main.%2 BEGIN SELECT RAISE(ABORT, 'injected fault'); END").arg(kind, table);
        return AppDataTestHelper::execute(database, sql.toUtf8().constData());
    }

    /// COMMIT of a transaction, which has changed the table, fails: the triggers violate a
    /// deferred foreign key (the transaction stays open, the repository must roll it back)
    static bool failCommit(pdf::PDFDatabase* database, const QString& table)
    {
        bool ok = AppDataTestHelper::execute(database, "CREATE TEMP TABLE fault_parent(id INTEGER PRIMARY KEY)") &&
                  AppDataTestHelper::execute(database, "CREATE TEMP TABLE fault_child(parent INTEGER REFERENCES fault_parent(id) DEFERRABLE INITIALLY DEFERRED)");
        for (const QString& kind : getKinds())
        {
            const QString sql = QStringLiteral("CREATE TEMP TRIGGER fault_commit_%1 AFTER %1 ON main.%2 BEGIN INSERT INTO fault_child VALUES (1); END").arg(kind, table);
            ok = ok && AppDataTestHelper::execute(database, sql.toUtf8().constData());
        }
        return ok;
    }

    /// Reads of the table fail at their first step (a TEMP view with the name of the
    /// table hides it, its condition is an invalid JSON), its writes at the prepare
    static bool failReads(pdf::PDFDatabase* database, const QString& table)
    {
        const QString sql = QStringLiteral("CREATE TEMP VIEW %1 AS SELECT * FROM main.%1 WHERE json('injected fault') IS NOT NULL").arg(table);
        return AppDataTestHelper::execute(database, sql.toUtf8().constData());
    }

    /// Injects a fault "abort:<kind>:<table>", "commit:<table>" or "read:<table>"
    static bool inject(pdf::PDFDatabase* database, const QString& fault)
    {
        const QStringList parts = fault.split(QLatin1Char(':'));
        if (parts.size() == 3 && parts[0] == QLatin1String("abort"))
        {
            return abortWrites(database, parts[1], parts[2]);
        }
        if (parts.size() == 2 && parts[0] == QLatin1String("commit"))
        {
            return failCommit(database, parts[1]);
        }
        if (parts.size() == 2 && parts[0] == QLatin1String("read"))
        {
            return failReads(database, parts[1]);
        }
        return false;
    }

    /// Removes every injected fault
    static bool clear(pdf::PDFDatabase* database)
    {
        bool ok = AppDataTestHelper::execute(database, "DROP TRIGGER IF EXISTS temp.fault_abort");
        for (const QString& kind : getKinds())
        {
            ok = ok && AppDataTestHelper::execute(database, QStringLiteral("DROP TRIGGER IF EXISTS temp.fault_commit_%1").arg(kind).toUtf8().constData());
        }
        ok = ok && AppDataTestHelper::execute(database, "DROP TABLE IF EXISTS temp.fault_child") && AppDataTestHelper::execute(database, "DROP TABLE IF EXISTS temp.fault_parent");
        for (const char* table : { "recent_file", "recent_folder", "pinned_location", "workspace", "workspace_document", "tool_pin", "tool_usage", "tool_collection", "tool_collection_item" })
        {
            ok = ok && AppDataTestHelper::execute(database, QStringLiteral("DROP VIEW IF EXISTS temp.%1").arg(QLatin1String(table)).toUtf8().constData());
        }
        return ok;
    }

private:
    static QStringList getKinds() { return { QStringLiteral("INSERT"), QStringLiteral("UPDATE"), QStringLiteral("DELETE") }; }
};

/// Child process of the tests of two processes
class AppDataChild
{
public:
    AppDataChild() = delete;

    /// --appdata-record <database file> <file>: records the file as opened
    /// --appdata-thumbnail-crash <database file> <path>: the thumbnailer ends the
    ///     process inside of the rendering (PDFAppThumbnailer::TEST_EXIT_CODE)
    /// --appdata-sleep: waits (a running process of another instance), killed by the test
    /// --appdata-exit: ends at once
    /// --appdata-collection <database file> <name>: creates a custom collection of
    ///     the name (exit 0), 6 = the name exists (step 6)
    /// --appdata-toolusage <database file> <runs>: records the runs of Document Report
    ///     after Compare (PDFDocumentToolUsage, step 6 package D), exit 0
    static int run(const QStringList& arguments)
    {
        if (arguments.size() >= 4 && arguments[1] == QLatin1String("--appdata-collection"))
        {
            return runCollection(arguments[2], arguments[3]);
        }
        if (arguments.size() >= 4 && arguments[1] == QLatin1String("--appdata-toolusage"))
        {
            return runToolUsage(arguments[2], arguments[3].toInt());
        }
        if (arguments.size() >= 2 && arguments[1] == QLatin1String("--appdata-exit"))
        {
            return 0;
        }
        if (arguments.size() >= 2 && arguments[1] == QLatin1String("--appdata-sleep"))
        {
            QThread::sleep(std::chrono::seconds(120));
            return 0;
        }
        if (arguments.size() >= 4 && arguments[1] == QLatin1String("--appdata-thumbnail-crash"))
        {
            return runThumbnailCrash(arguments[2], arguments[3]);
        }
        if (arguments.size() < 4 || arguments[1] != QLatin1String("--appdata-record"))
        {
            return 2;
        }

        pdf::PDFDatabase database;
        database.open(AppDataTestHelper::createFileOptions(arguments[2]));
        if (database.getState().mode != pdf::PDFDatabaseStorageMode::Disk)
        {
            return 3;
        }

        pdf::PDFRecentRepository repository(&database, nullptr);
        const pdf::PDFDatabaseError error = repository.recordOpened(arguments[3]);
        database.close();
        return error.isError() ? 4 : 0;
    }

    static int runCollection(const QString& databasePath, const QString& name)
    {
        pdf::PDFDatabase database;
        database.open(AppDataTestHelper::createFileOptions(databasePath));
        if (database.getState().mode != pdf::PDFDatabaseStorageMode::Disk)
        {
            return 3;
        }

        bool isDuplicate = false;
        const pdf::PDFDatabaseError error = pdf::PDFToolCollectionRepository(&database).createCollection(name, QString(), { QStringLiteral("pages.merge") }, nullptr, &isDuplicate);
        database.close();
        if (isDuplicate)
        {
            return 6;
        }
        return error.isError() ? 4 : 0;
    }

    static int runToolUsage(const QString& databasePath, int runs)
    {
        pdf::PDFDatabase database;
        database.open(AppDataTestHelper::createFileOptions(databasePath));
        if (database.getState().mode != pdf::PDFDatabaseStorageMode::Disk)
        {
            return 3;
        }

        pdf::PDFDocumentToolUsage usage(&database);
        for (int i = 0; i < runs; ++i)
        {
            if (usage.recordRun(QStringLiteral("review.report"), QStringLiteral("review.compare")).isError())
            {
                database.close();
                return 4;
            }
        }
        database.close();
        return 0;
    }

    static int runThumbnailCrash(const QString& databasePath, const QString& path)
    {
        pdf::PDFDatabase database;
        database.open(AppDataTestHelper::createFileOptions(databasePath));
        if (database.getState().mode != pdf::PDFDatabaseStorageMode::Disk)
        {
            return 3;
        }

        pdf::PDFAppThumbnailer thumbnailer(&database);
        pdf::PDFAppThumbnailerOptions options;
        options.testExitWhileRendering = true;
        thumbnailer.setOptions(options);
        thumbnailer.requestThumbnail(path);

        // The worker ends the process; it must not come back
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 60000)
        {
            QThread::msleep(20);
        }
        return 5;
    }
};

/// Data and services of step 2 of the new application: the static tool catalog
/// of the core, the repositories of the recent files / folders / pinned
/// locations / workspaces / tools, the thumbnailer and the folder information
class AppDataTest : public QObject
{
    Q_OBJECT

private slots:
    void catalogTools();
    void catalogCategories();
    void catalogLookup();
    void catalogLookup_data();
    void catalogKeywords();
    void catalogTranslation();

    void repositoryHelper();
    void repositoryFaults_data();
    void repositoryFaults();
    void recentRecordOpened();
    void recentLimit_data();
    void recentLimit();
    void recentApplyLimit();
    void clearToolHistory();
    void recentPinning();
    void recentQueries_data();
    void recentQueries();
    void recentReadingState();
    void recentThumbnails();
    void recentClearHistory_data();
    void recentClearHistory();
    void recentFolders();
    void pinnedLocations();
    void hostileRows();
    void workspaces();
    void tools();
    void toolCollectionRepository();
    void toolCollectionNames();
    void toolCollectionHostileRows();
    void toolCollectionEdges();
    void toolCollectionTwoProcesses();
    void toolUsageClear();
    void toolPairsRead();
    void toolUsageRecording();
    void toolUsageTwoProcesses();
    void toolParameterValues();
    void schemaMigration();
    void notificationsOfAnotherConnection();
    void notificationsOfAnotherProcess();

    void thumbnailCreate_data();
    void thumbnailCreate();
    void thumbnailerService();
    void thumbnailerTimeBudget();
    void thumbnailerStop_data();
    void thumbnailerStop();
    void thumbnailerQueueLimit();
    void thumbnailerStopCancelsRendering();
    void thumbnailerStoreError();
    void folderInfo();
    void folderInfoPendingLimit();
    void folderInfoDeletedDuringWork();
    void abandonedWorkers();
    void thumbnailerExceptions();
    void thumbnailerBusyDatabase();
    void devicePaths();
    void recentRowLimit();
    void networkPaths_data();
    void networkPaths();
    void networkMountTable();
    void processInfo();
    void renderingMarkers();
    void thumbnailerMarkerAlive();
    void thumbnailerCrashLoop();
    void thumbnailerLiveMarker();
    void thumbnailerSweepErrors();
    void thumbnailerImageLimit();
    void thumbnailerReading();
    void renameDeleted();
};

void AppDataTest::catalogTools()
{
    const std::vector<pdf::PDFDocumentToolCatalogEntry>& tools = pdf::PDFDocumentToolCatalog::getTools();
    QCOMPARE(int(tools.size()), 46);
    QCOMPARE(pdf::PDFDocumentToolCatalog::getToolCount(), 46);

    QSet<QString> ids;
    QSet<QString> titles;
    int previousCategory = -1;
    int ownTabs = 0;
    int available = 0;
    int unavailable = 0;
    int noDocument = 0;
    for (size_t i = 0; i < tools.size(); ++i)
    {
        const pdf::PDFDocumentToolCatalogEntry& tool = tools[i];
        const QString id = tool.getId();
        QVERIFY2(!ids.contains(id), qPrintable(id));
        ids.insert(id);
        QVERIFY(!titles.contains(tool.getSourceTitle()));
        titles.insert(tool.getSourceTitle());

        // Identifier "<category>.<name>": lower case ASCII letters, digits and hyphens
        static const QRegularExpression idPattern(QStringLiteral("^[a-z]+\\.[a-z0-9]+(-[a-z0-9]+)*$"));
        QVERIFY2(idPattern.match(id).hasMatch(), qPrintable(id));
        QCOMPARE(id.section(QLatin1Char('.'), 0, 0), pdf::PDFDocumentToolCatalog::getCategoryId(tool.getCategory()));
        QVERIFY(id.size() <= pdf::PDFDocumentToolCatalog::MAXIMUM_ID_LENGTH);

        // The order of the catalog: categories are contiguous and ordered
        QVERIFY(int(tool.getCategory()) >= previousCategory);
        previousCategory = int(tool.getCategory());

        QVERIFY(!tool.getTitle().isEmpty());
        QVERIFY(!tool.getDescription().isEmpty());
        QCOMPARE(tool.getTitle(), tool.getSourceTitle());
        QCOMPARE(tool.getDescription(), tool.getSourceDescription());
        QVERIFY2(pdf::PDFThemeIcons::hasIcon(tool.getIconName()), qPrintable(tool.getIconName()));
        QCOMPARE(tool.getHue(), pdf::PDFDocumentToolCatalog::getCategoryInfo(tool.getCategory())->getHue());
        QCOMPARE(pdf::PDFDocumentToolCatalog::getToolIndex(id), int(i));
        QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(id), &tool);

        // Subgroups: every category except Edit has them, the subgroups of a
        // category are contiguous (headings of the catalog)
        if (tool.getCategory() == pdf::PDFDocumentToolCategory::Edit)
        {
            QVERIFY(tool.getSubgroupId().isEmpty());
            QVERIFY(tool.getSubgroupTitle().isEmpty());
        }
        else
        {
            QVERIFY(!tool.getSubgroupId().isEmpty());
            QVERIFY2(!tool.getSubgroupTitle().isEmpty(), qPrintable(id));
        }

        ownTabs += tool.hasOwnTab() ? 1 : 0;
        switch (tool.getLockedAccess())
        {
            case pdf::PDFDocumentToolLockedAccess::Available:
                ++available;
                break;
            case pdf::PDFDocumentToolLockedAccess::Unavailable:
                ++unavailable;
                break;
            case pdf::PDFDocumentToolLockedAccess::NoDocument:
                ++noDocument;
                break;
        }
    }

    // tools.md 4.7: 17 available, 26 unavailable, 3 without a document
    QCOMPARE(available, 17);
    QCOMPARE(unavailable, 26);
    QCOMPARE(noDocument, 3);

    // newapp_navigation.md 3: Merge, Interleave Pages, Manage Pages, Compare,
    // Manage Certificates, Images to PDF
    QCOMPARE(ownTabs, 6);
    for (const char* id : { "pages.merge", "pages.interleave", "pages.manage", "review.compare", "protect.certificates", "convert.images-to-pdf" })
    {
        QVERIFY(pdf::PDFDocumentToolCatalog::findTool(QString::fromLatin1(id))->hasOwnTab());
    }

    // Figma 272:12821: the tag Advanced
    QStringList advanced;
    for (const pdf::PDFDocumentToolCatalogEntry& tool : tools)
    {
        if (tool.isAdvanced())
        {
            advanced << tool.getId();
        }
    }
    QCOMPARE(advanced, QStringList({ QStringLiteral("pages.boxes"), QStringLiteral("review.output-preview"), QStringLiteral("review.ink-coverage"),
                                     QStringLiteral("review.soft-proof"), QStringLiteral("review.statistics"), QStringLiteral("review.object-inspector"),
                                     QStringLiteral("review.report") }));

    // Samples of the cards of Figma 272:12821
    const pdf::PDFDocumentToolCatalogEntry* merge = pdf::PDFDocumentToolCatalog::findTool(u"pages.merge");
    QVERIFY(merge);
    QCOMPARE(merge->getTitle(), QStringLiteral("Merge"));
    QCOMPARE(merge->getDescription(), QStringLiteral("Combine several files into one PDF"));
    QCOMPARE(merge->getIconName(), QStringLiteral("layers"));
    QCOMPARE(merge->getHue(), pdf::PDFDocumentToolHue::Amber);
    QCOMPARE(merge->getSubgroupId(), QStringLiteral("combine-split"));
    QCOMPARE(merge->getSubgroupTitle(), QStringLiteral("Combine & split"));

    const pdf::PDFDocumentToolCatalogEntry* rotate = pdf::PDFDocumentToolCatalog::findTool(u"pages.rotate");
    QVERIFY(rotate);
    QCOMPARE(rotate->getDescription(), QString::fromUtf8("Turn pages in 90° steps"));

    const pdf::PDFDocumentToolCatalogEntry* compress = pdf::PDFDocumentToolCatalog::findTool(u"optimize.compress");
    QVERIFY(compress);
    QCOMPARE(compress->getIconName(), QStringLiteral("minimize-2"));
    QCOMPARE(compress->getHue(), pdf::PDFDocumentToolHue::Violet);
    QCOMPARE(compress->getLockedAccess(), pdf::PDFDocumentToolLockedAccess::Unavailable);

    const pdf::PDFDocumentToolCatalogEntry* grayscale = pdf::PDFDocumentToolCatalog::findTool(u"optimize.grayscale");
    QVERIFY(grayscale);
    QCOMPARE(grayscale->getTitle(), QStringLiteral("Convert Images to Grayscale"));
    QCOMPARE(grayscale->getIconName(), QStringLiteral("sun"));
}

void AppDataTest::catalogCategories()
{
    const std::vector<pdf::PDFDocumentToolCategoryInfo>& categories = pdf::PDFDocumentToolCatalog::getCategories();
    QCOMPARE(int(categories.size()), 6);

    struct ExpectedCategory
    {
        pdf::PDFDocumentToolCategory category;
        const char* id;
        const char* title;
        const char* chip;
        const char* icon;
        pdf::PDFDocumentToolHue hue;
        const char* hueName;
        int count;
        QStringList subgroups;
    };

    const std::vector<ExpectedCategory> expected =
    {
        { pdf::PDFDocumentToolCategory::Pages, "pages", "Organize Pages", "Pages", "pages", pdf::PDFDocumentToolHue::Amber, "Amber", 12, { "combine-split", "arrange", "page-size" } },
        { pdf::PDFDocumentToolCategory::Convert, "convert", "Create & Convert", "Convert", "repeat", pdf::PDFDocumentToolHue::Blue, "Blue", 8, { "create", "recognize", "export" } },
        { pdf::PDFDocumentToolCategory::Edit, "edit", "Edit", "Edit", "edit", pdf::PDFDocumentToolHue::Green, "Green", 2, { } },
        { pdf::PDFDocumentToolCategory::Protect, "protect", "Protect & Sign", "Protect & Sign", "shield", pdf::PDFDocumentToolHue::Red, "Red", 11, { "encryption", "signatures", "privacy" } },
        { pdf::PDFDocumentToolCategory::Optimize, "optimize", "Optimize", "Optimize", "minimize-2", pdf::PDFDocumentToolHue::Violet, "Violet", 5, { "file-size", "color" } },
        { pdf::PDFDocumentToolCategory::Review, "review", "Review & Inspect", "Review", "columns", pdf::PDFDocumentToolHue::Teal, "Teal", 8, { "review", "print-production", "developer" } },
    };

    int total = 0;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        const pdf::PDFDocumentToolCategoryInfo& info = categories[i];
        QCOMPARE(info.getCategory(), expected[i].category);
        QCOMPARE(info.getId(), QLatin1String(expected[i].id));
        QCOMPARE(info.getTitle(), QLatin1String(expected[i].title));
        QCOMPARE(info.getChipText(), QLatin1String(expected[i].chip));
        QVERIFY(!info.getDescription().isEmpty());
        QCOMPARE(info.getIconName(), QLatin1String(expected[i].icon));
        QVERIFY(pdf::PDFThemeIcons::hasIcon(info.getIconName()));
        QCOMPARE(info.getHue(), expected[i].hue);
        QCOMPARE(pdf::PDFDocumentToolCatalog::getHueName(info.getHue()), QLatin1String(expected[i].hueName));
        QCOMPARE(info.getToolCount(), expected[i].count);
        QCOMPARE(int(pdf::PDFDocumentToolCatalog::getTools(info.getCategory()).size()), expected[i].count);
        QVERIFY(pdf::PDFDocumentToolCatalog::findCategory(info.getId()) == std::optional<pdf::PDFDocumentToolCategory>(info.getCategory()));
        QCOMPARE(pdf::PDFDocumentToolCatalog::getCategoryInfo(info.getCategory()), &info);
        total += info.getToolCount();

        QStringList subgroupIds;
        for (const pdf::PDFDocumentToolSubgroupInfo* subgroup : pdf::PDFDocumentToolCatalog::getSubgroups(info.getCategory()))
        {
            subgroupIds << subgroup->getId();
            QVERIFY(!subgroup->getTitle().isEmpty());
        }
        QCOMPARE(subgroupIds, expected[i].subgroups);

        // The tools of a subgroup are contiguous and the subgroups are in their order
        QStringList toolSubgroups;
        for (const pdf::PDFDocumentToolCatalogEntry* tool : pdf::PDFDocumentToolCatalog::getTools(info.getCategory()))
        {
            if (!toolSubgroups.isEmpty() && toolSubgroups.last() == tool->getSubgroupId())
            {
                continue;
            }
            toolSubgroups << tool->getSubgroupId();
        }
        if (!expected[i].subgroups.isEmpty())
        {
            QCOMPARE(toolSubgroups, expected[i].subgroups);
        }
    }
    QCOMPARE(total, 46);

    // Invalid values of the enumerations
    QVERIFY(!pdf::PDFDocumentToolCatalog::getCategoryInfo(static_cast<pdf::PDFDocumentToolCategory>(97)));
    QVERIFY(pdf::PDFDocumentToolCatalog::getCategoryId(static_cast<pdf::PDFDocumentToolCategory>(-1)).isEmpty());
    QVERIFY(pdf::PDFDocumentToolCatalog::getTools(static_cast<pdf::PDFDocumentToolCategory>(97)).empty());
    QVERIFY(pdf::PDFDocumentToolCatalog::getSubgroups(static_cast<pdf::PDFDocumentToolCategory>(97)).empty());
    QVERIFY(pdf::PDFDocumentToolCatalog::getHueName(static_cast<pdf::PDFDocumentToolHue>(42)).isEmpty());
}

void AppDataTest::catalogLookup_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<bool>("isTool");
    QTest::addColumn<bool>("isCategory");

    QTest::newRow("merge") << QStringLiteral("pages.merge") << true << false;
    QTest::newRow("compress") << QStringLiteral("optimize.compress") << true << false;
    QTest::newRow("report") << QStringLiteral("review.report") << true << false;
    QTest::newRow("timestamp") << QStringLiteral("protect.timestamp") << true << false;
    QTest::newRow("category") << QStringLiteral("pages") << false << true;
    QTest::newRow("category review") << QStringLiteral("review") << false << true;
    QTest::newRow("pinned is not a category") << QStringLiteral("pinned") << false << false;
    QTest::newRow("empty") << QString() << false << false;
    QTest::newRow("upper case") << QStringLiteral("PAGES.MERGE") << false << false;
    QTest::newRow("upper case category") << QStringLiteral("Pages") << false << false;
    QTest::newRow("prefix") << QStringLiteral("pages.mer") << false << false;
    QTest::newRow("suffix") << QStringLiteral("pages.merge ") << false << false;
    QTest::newRow("leading space") << QStringLiteral(" pages.merge") << false << false;
    QTest::newRow("nul") << QString::fromUtf16(u"pages.merge\u0000", 12) << false << false;
    QTest::newRow("old figma name") << QStringLiteral("security.encrypt") << false << false;
    QTest::newRow("not a catalog tool") << QStringLiteral("review.verify-signatures") << false << false;
    QTest::newRow("unicode") << QString::fromUtf8("pages.mérge") << false << false;
    QTest::newRow("long") << QString(10000, QLatin1Char('a')) << false << false;
    QTest::newRow("long prefix") << (QStringLiteral("pages.merge") + QString(100, QLatin1Char('x'))) << false << false;
}

void AppDataTest::catalogLookup()
{
    QFETCH(QString, id);
    QFETCH(bool, isTool);
    QFETCH(bool, isCategory);

    QCOMPARE(pdf::PDFDocumentToolCatalog::isValidToolId(id), isTool);
    QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(id) != nullptr, isTool);
    QCOMPARE(pdf::PDFDocumentToolCatalog::getToolIndex(id) >= 0, isTool);
    QCOMPARE(pdf::PDFDocumentToolCatalog::findCategory(id).has_value(), isCategory);
    if (isTool)
    {
        QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(id)->getId(), id);
    }
}

void AppDataTest::catalogKeywords()
{
    // Every tool has keywords (newapp_tools.md 2): lower case English, no empty
    // or duplicate items, its English title matches its keywords or title
    for (const pdf::PDFDocumentToolCatalogEntry& tool : pdf::PDFDocumentToolCatalog::getTools())
    {
        const QStringList keywords = tool.getKeywords();
        QVERIFY2(keywords.size() >= 5, qPrintable(tool.getId()));
        QCOMPARE(keywords.size(), QSet<QString>(keywords.cbegin(), keywords.cend()).size());
        for (const QString& keyword : keywords)
        {
            QVERIFY(!keyword.isEmpty());
            QCOMPARE(keyword, keyword.trimmed());
            QCOMPARE(keyword, keyword.toLower());
        }

        // No translation is installed: the translated list is empty
        QVERIFY(tool.getTranslatedKeywords().isEmpty());
    }

    // Samples of tools.md: Merge is found by join, combine and bind
    const QStringList mergeKeywords = pdf::PDFDocumentToolCatalog::findTool(u"pages.merge")->getKeywords();
    QVERIFY(mergeKeywords.contains(QStringLiteral("join")));
    QVERIFY(mergeKeywords.contains(QStringLiteral("combine")));
    QVERIFY(mergeKeywords.contains(QStringLiteral("bind")));
    QVERIFY(mergeKeywords.contains(QStringLiteral("united document")));
    QVERIFY(pdf::PDFDocumentToolCatalog::findTool(u"protect.sanitize")->getKeywords().contains(QStringLiteral("sanitize")));
    QVERIFY(pdf::PDFDocumentToolCatalog::findTool(u"pages.remove")->getKeywords().contains(QStringLiteral("delete")));
}

/// Translation of the catalog: the title and the keywords of Merge
class AppDataCatalogTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const pdf::PDFDocumentToolCatalogEntry* merge = pdf::PDFDocumentToolCatalog::findTool(u"pages.merge");
        if (!merge || qstrcmp(context, pdf::PDFDocumentToolCatalog::TRANSLATION_CONTEXT) != 0 || !sourceText)
        {
            return QString();
        }

        const QString source = QString::fromUtf8(sourceText);
        if (source == merge->getSourceTitle())
        {
            return QString::fromUtf8("Sloučit");
        }

        // The list of the keywords: empty items, white space and a duplicate
        QStringList keywords;
        for (const QString& keyword : source.split(QLatin1Char(',')))
        {
            keywords << keyword.trimmed();
        }
        if (keywords == merge->getKeywords())
        {
            return QString::fromUtf8("spojit,, \t ,sloučit , spojit,");
        }
        return QString();
    }
};

void AppDataTest::catalogTranslation()
{
    AppDataCatalogTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    auto removeTranslator = qScopeGuard([&translator]() { QCoreApplication::removeTranslator(&translator); });

    const pdf::PDFDocumentToolCatalogEntry* merge = pdf::PDFDocumentToolCatalog::findTool(u"pages.merge");
    QVERIFY(merge);
    QCOMPARE(merge->getTitle(), QString::fromUtf8("Sloučit"));
    QCOMPARE(merge->getSourceTitle(), QStringLiteral("Merge"));

    // The translated keywords: trimmed, without empty items and duplicates; the
    // English keywords stay (the search matches both)
    QCOMPARE(merge->getTranslatedKeywords(), QStringList({ QStringLiteral("spojit"), QString::fromUtf8("sloučit") }));
    QVERIFY(merge->getKeywords().contains(QStringLiteral("join")));

    // A tool without a translation
    const pdf::PDFDocumentToolCatalogEntry* split = pdf::PDFDocumentToolCatalog::findTool(u"pages.split");
    QVERIFY(split);
    QVERIFY(split->getTranslatedKeywords().isEmpty());
    QCOMPARE(split->getTitle(), split->getSourceTitle());
}

void AppDataTest::repositoryHelper()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("a.pdf"));
    QVERIFY(AppDataTestHelper::writeFile(filePath, "x"));

    // Paths: absolute, clean, '/' separators, canonical for an existing file
    const QString normalized = pdf::PDFRepositoryHelper::normalizePath(QDir::toNativeSeparators(directory.path() + QStringLiteral("/sub/../a.pdf")));
    QCOMPARE(normalized, QFileInfo(filePath).canonicalFilePath());
    QVERIFY(!normalized.contains(QLatin1Char('\\')));
    QVERIFY(pdf::PDFRepositoryHelper::normalizePath(QString()).isEmpty());
    QVERIFY(pdf::PDFRepositoryHelper::normalizePath(QStringLiteral("relative/a.pdf")).isEmpty());
    QVERIFY(pdf::PDFRepositoryHelper::normalizePath(directory.path() + QString(QChar(0)) + QStringLiteral("a.pdf")).isEmpty());
    QVERIFY(pdf::PDFRepositoryHelper::normalizePath(directory.path() + QLatin1Char('/') + QString(40000, QLatin1Char('a'))).isEmpty());

    // A missing file keeps its clean absolute path
    const QString missing = pdf::PDFRepositoryHelper::normalizePath(directory.path() + QStringLiteral("/missing/./b.pdf"));
    QVERIFY(missing.endsWith(QStringLiteral("/missing/b.pdf")));

    QVERIFY(pdf::PDFRepositoryHelper::isValidStoredPath(normalized));
    QVERIFY(!pdf::PDFRepositoryHelper::isValidStoredPath(QStringLiteral("a.pdf")));
    QVERIFY(!pdf::PDFRepositoryHelper::isValidStoredPath(QString()));

    // Names: control characters, white space, length
    QCOMPARE(pdf::PDFRepositoryHelper::normalizeName(QStringLiteral("  Thesis\nreview\t ")), QStringLiteral("Thesis review"));
    QVERIFY(pdf::PDFRepositoryHelper::normalizeName(QStringLiteral(" \n\t ")).isEmpty());
    QVERIFY(pdf::PDFRepositoryHelper::normalizeName(QString(300, QLatin1Char('x'))).isEmpty());
    QCOMPARE(pdf::PDFRepositoryHelper::sanitizeStoredName(QString(300, QLatin1Char('x'))).size(), pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH);
    QCOMPARE(pdf::PDFRepositoryHelper::normalizeName(QString::fromUtf8("Faktury \u2013 \u010Dervenec")), QString::fromUtf8("Faktury \u2013 \u010Dervenec"));

    // A surrogate pair is never cut into halves: with one leading character the
    // limit falls between the halves of a pair (an even offset would never test it)
    QString emoji = QStringLiteral("a");
    for (int i = 0; i < 200; ++i)
    {
        emoji += QString::fromUtf8("\U0001F600");
    }
    QVERIFY(QChar::isHighSurrogate(emoji.at(pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH - 1).unicode()));
    const QString sanitized = pdf::PDFRepositoryHelper::sanitizeStoredName(emoji);
    QCOMPARE(sanitized.size(), pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH - 1);
    QVERIFY(sanitized.back().isLowSurrogate());

    // Line and paragraph separators are white space of a name
    QCOMPARE(pdf::PDFRepositoryHelper::sanitizeStoredName(QString::fromUtf8("A B C")), QStringLiteral("A B C"));

    // Stored paths: length and form only
    QVERIFY(pdf::PDFRepositoryHelper::isValidStoredPath(QStringLiteral("/") + QString(pdf::PDFRepositoryHelper::MAXIMUM_PATH_LENGTH - 1, QLatin1Char('a'))));
    QVERIFY(!pdf::PDFRepositoryHelper::isValidStoredPath(QStringLiteral("/") + QString(pdf::PDFRepositoryHelper::MAXIMUM_PATH_LENGTH, QLatin1Char('a'))));
    QVERIFY(!pdf::PDFRepositoryHelper::isValidStoredPath(QStringLiteral("/a") + QString(QChar(0))));

    // Paths at the limit of Windows: a rooted path without a drive and a path with
    // "." are made absolute by GetFullPathName, which fails for 32767 characters (Qt
    // crashed then); they are refused, a path three characters shorter is accepted
    const int maximum = pdf::PDFRepositoryHelper::MAXIMUM_PATH_LENGTH;
    for (const int length : { maximum, maximum - 1, maximum - 2 })
    {
        QVERIFY(pdf::PDFRepositoryHelper::normalizePath(QStringLiteral("/") + QString(length - 1, QLatin1Char('a'))).isEmpty());
        QVERIFY(pdf::PDFRepositoryHelper::normalizePath(directory.path() + QLatin1Char('/') + QString(length - directory.path().size() - 3, QLatin1Char('a')) + QStringLiteral("/.")).isEmpty());
    }
    const QString rootedPath = QStringLiteral("/") + QString(maximum - 4, QLatin1Char('a'));
    const QString normalizedRooted = pdf::PDFRepositoryHelper::normalizePath(rootedPath);
    QVERIFY(normalizedRooted.endsWith(rootedPath));
    QVERIFY(normalizedRooted.size() <= maximum);
    const QString dotPath = directory.path() + QLatin1Char('/') + QString(maximum - 3 - directory.path().size() - 3, QLatin1Char('b')) + QStringLiteral("/.");
    QCOMPARE(dotPath.size(), maximum - 3);
    QCOMPARE(pdf::PDFRepositoryHelper::normalizePath(dotPath), dotPath.chopped(2));

    // Folders of paths without a separator and of a two letter folder, which is not a drive
    QVERIFY(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("Report.pdf")).isEmpty());
    QCOMPARE(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("ab/Report.pdf")), QStringLiteral("ab"));
    QCOMPARE(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("/ab/Report.pdf")), QStringLiteral("/ab"));

    QCOMPARE(pdf::PDFRepositoryHelper::getFileName(QStringLiteral("C:/Users/Jakub/Report.pdf")), QStringLiteral("Report.pdf"));
    QCOMPARE(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("/home/jakub/Report.pdf")), QStringLiteral("/home/jakub"));
    QCOMPARE(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("C:/Report.pdf")), QStringLiteral("C:/"));
    QCOMPARE(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("/Report.pdf")), QStringLiteral("/"));
#if defined(Q_OS_WIN)
    QCOMPARE(pdf::PDFRepositoryHelper::getFolder(QStringLiteral("//server/share/Report.pdf")), QStringLiteral("//server/share"));
#endif
#if defined(Q_OS_WIN)
    QCOMPARE(pdf::PDFRepositoryHelper::getFileName(QStringLiteral("C:/")), QDir::toNativeSeparators(QStringLiteral("C:/")));
#else
    QCOMPARE(pdf::PDFRepositoryHelper::getFileName(QStringLiteral("/")), QStringLiteral("/"));
#endif
    QVERIFY(pdf::PDFRepositoryHelper::isSamePath(QStringLiteral("/a/b/../c.pdf"), QStringLiteral("/a/c.pdf")));
#if defined(Q_OS_WIN)
    QVERIFY(pdf::PDFRepositoryHelper::isSamePath(QStringLiteral("C:\\Docs\\A.PDF"), QStringLiteral("c:/docs/a.pdf")));
#endif
    QVERIFY(pdf::PDFRepositoryHelper::containsText(QStringLiteral("Project_Report.pdf"), QStringLiteral("report")));
    QVERIFY(pdf::PDFRepositoryHelper::containsText(QStringLiteral("x"), QStringLiteral("  ")));
    QVERIFY(!pdf::PDFRepositoryHelper::containsText(QStringLiteral("x"), QStringLiteral("y")));
}

/// Data of the fault tests: three unpinned recent files at the limit of three files and
/// two pinned ones, twenty recent folders (the limit), two pinned locations, two
/// workspaces (one opened) and the last session, two pinned tools and a used one.
/// Every operation of getOperations() writes, so an injected fault makes it fail.
class AppDataFaultScene
{
public:
    explicit AppDataFaultScene(DataFixture& fixture) :
        m_fixture(fixture)
    {

    }

    bool prepare()
    {
        DataFixture& fixture = m_fixture;
        if (fixture.settings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), 3).isError())
        {
            return false;
        }

        for (int i = 0; i < 2; ++i)
        {
            pinned << fixture.createFile(QStringLiteral("pinned%1.pdf").arg(i));
            if (pinned.last().isEmpty() || fixture.recent(90 + i).recordOpened(pinned.last()).isError() || fixture.recent().setPinned(pinned.last(), true).isError())
            {
                return false;
            }
        }
        for (int i = 0; i < 3; ++i)
        {
            files << fixture.createFile(QStringLiteral("file%1.pdf").arg(i));
            if (files.last().isEmpty() || fixture.recent(100 + i).recordOpened(files.last()).isError())
            {
                return false;
            }
        }
        for (int i = 0; i < pdf::PDFRecentRepository::MAXIMUM_RECENT_FOLDERS; ++i)
        {
            const QString folder = fixture.directory.filePath(QStringLiteral("folder%1").arg(i));
            if (!QDir().mkpath(folder) || fixture.recent(200 + i).recordFolder(folder).isError())
            {
                return false;
            }
            folders << pdf::PDFRepositoryHelper::normalizePath(folder);
        }

        newFile = fixture.createFile(QStringLiteral("new/new.pdf"));
        newFolder = fixture.directory.filePath(QStringLiteral("newfolder"));
        newLocation = fixture.directory.filePath(QStringLiteral("location2"));
        if (newFile.isEmpty() || !QDir().mkpath(newFolder) || !QDir().mkpath(newLocation))
        {
            return false;
        }

        for (int i = 0; i < 2; ++i)
        {
            const QString path = fixture.directory.filePath(QStringLiteral("location%1").arg(i));
            if (!QDir().mkpath(path) || fixture.recent().addPinnedLocation(path, QString(), &locations[i]).isError())
            {
                return false;
            }
        }

        return !fixture.workspaces(300).saveWorkspace(QStringLiteral("Alpha"), { { files[0], 2 }, { files[1], 0 } }, false, &alpha).isError() &&
               !fixture.workspaces(310).saveWorkspace(QStringLiteral("Beta"), { { files[2], 4 } }, false, &beta).isError() &&
               !fixture.workspaces(320).markOpened(beta).isError() &&
               !fixture.workspaces(330).saveLastSession({ { files[0], 1 } }).isError() &&
               !fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError() &&
               !fixture.tools().setPinned(QStringLiteral("optimize.compress"), true).isError() &&
               !fixture.tools(340).recordUsage(QStringLiteral("pages.merge")).isError() && prepareCollections();
    }

    /// Collections of step 6 (review RT6): a custom collection, an override of a
    /// renamed built-in collection and an override, which differs from its built-in
    /// definition by one tool only
    bool prepareCollections()
    {
        DataFixture& fixture = m_fixture;
        const pdf::PDFAppBuiltinToolCollection* print = pdf::PDFAppBuiltinToolCollections::findCollection(QStringLiteral("prepare-for-print"));
        const pdf::PDFAppBuiltinToolCollection* extract = pdf::PDFAppBuiltinToolCollections::findCollection(QStringLiteral("extract-content"));
        if (!print || !extract)
        {
            return false;
        }

        pdf::PDFToolCollectionDraft printDraft;
        printDraft.builtinId = print->getId();
        printDraft.name = QStringLiteral("Print shop");
        printDraft.toolIds = { QStringLiteral("pages.boxes"), QStringLiteral("review.soft-proof") };
        pdf::PDFToolCollectionDraft extractDraft;
        extractDraft.builtinId = extract->getId();
        extractDraft.name = extract->getSourceName();
        extractDraft.toolIds = extract->getToolIds() + QStringList({ QStringLiteral("pages.split") });
        return !fixture.collections(350).createCollection(QStringLiteral("Invoices"), QStringLiteral("star"), { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }, &custom).isError() &&
               !fixture.collections(360).saveCollection(printDraft).isError() && !fixture.collections(370).saveCollection(extractDraft).isError();
    }

    /// The operations of the repositories by their names
    std::map<QString, std::function<pdf::PDFDatabaseError()>> getOperations()
    {
        DataFixture& fixture = m_fixture;
        std::map<QString, std::function<pdf::PDFDatabaseError()>> operations;
        operations["recordOpened"] = [&]() { return fixture.recent(1000).recordOpened(newFile); };
        operations["setReadingState"] = [&]() { return fixture.recent().setReadingState(files[0], 5, 10, false); };
        operations["removeFile"] = [&]() { return fixture.recent().removeFile(files[1]); };
        operations["pin"] = [&]() { return fixture.recent().setPinned(files[0], true); };
        operations["unpin"] = [&]() { return fixture.recent().setPinned(pinned[0], false); };
        operations["pinnedOrder"] = [&]() { return fixture.recent().setPinnedOrder({ pinned[1], pinned[0] }); };
        operations["thumbnail"] = [&]() { return fixture.recent().setThumbnail(files[0], QByteArray("png"), 5, pdf::PDFRecentThumbnailState::Ready, 3); };
        operations["recordFolder"] = [&]() { return fixture.recent(1000).recordFolder(newFolder); };
        operations["removeFolder"] = [&]() { return fixture.recent().removeFolder(folders.last()); };
        operations["addLocation"] = [&]() { return fixture.recent().addPinnedLocation(newLocation, QString()); };
        operations["renameLocation"] = [&]() { return fixture.recent().renamePinnedLocation(locations[0], QStringLiteral("Renamed")); };
        operations["removeLocation"] = [&]() { return fixture.recent().removePinnedLocation(locations[0]); };
        operations["locationOrder"] = [&]() { return fixture.recent().setPinnedLocationOrder({ locations[1], locations[0] }); };
        operations["clearHistory"] = [&]() { return fixture.recent().clearHistory(true); };
        operations["saveWorkspace"] = [&]() { return fixture.workspaces(1000).saveWorkspace(QStringLiteral("Gamma"), { { files[0], 1 } }, false); };
        operations["replaceWorkspace"] = [&]() { return fixture.workspaces(1000).saveWorkspace(QStringLiteral("alpha"), { { files[2], 1 } }, true); };
        operations["renameWorkspace"] = [&]() { return fixture.workspaces().renameWorkspace(alpha, QStringLiteral("Delta")); };
        operations["deleteWorkspace"] = [&]() { return fixture.workspaces().deleteWorkspace(alpha); };
        operations["markOpened"] = [&]() { return fixture.workspaces(5000).markOpened(alpha); };
        operations["saveLastSession"] = [&]() { return fixture.workspaces(1000).saveLastSession({ { files[1], 3 }, { files[2], 0 } }); };
        operations["pinTool"] = [&]() { return fixture.tools().setPinned(QStringLiteral("review.compare"), true); };
        operations["unpinTool"] = [&]() { return fixture.tools().setPinned(QStringLiteral("pages.merge"), false); };
        operations["toolOrder"] = [&]() { return fixture.tools().setPinnedOrder({ QStringLiteral("optimize.compress"), QStringLiteral("pages.merge") }); };
        operations["recordUsage"] = [&]() { return fixture.tools(1000).recordUsage(QStringLiteral("review.compare")); };
        operations["clearUsage"] = [&]() { return fixture.tools().clearUsage(); };
        operations["createCollection"] = [&]() { return fixture.collections(1000).createCollection(QStringLiteral("Gamma"), QStringLiteral("zap"), { QStringLiteral("pages.split") }); };
        operations["saveCustom"] = [&]()
        {
            pdf::PDFToolCollectionDraft draft;
            draft.id = custom;
            draft.name = QStringLiteral("Invoices 2");
            draft.toolIds = { QStringLiteral("pages.split"), QStringLiteral("pages.merge") };
            return fixture.collections(1000).saveCollection(draft);
        };
        operations["saveBuiltin"] = [&]()
        {
            pdf::PDFToolCollectionDraft draft;
            draft.builtinId = QStringLiteral("fix-a-scan");
            draft.name = QStringLiteral("Scans");
            draft.iconName = QStringLiteral("camera");
            draft.toolIds = { QStringLiteral("pages.rotate"), QStringLiteral("pages.crop") };
            return fixture.collections(1000).saveCollection(draft);
        };
        operations["saveOverride"] = [&]()
        {
            pdf::PDFToolCollectionDraft draft;
            draft.builtinId = QStringLiteral("prepare-for-print");
            draft.name = QStringLiteral("Print shop 2");
            draft.toolIds = { QStringLiteral("review.soft-proof") };
            return fixture.collections(1000).saveCollection(draft);
        };
        operations["saveDefault"] = [&]()
        {
            const pdf::PDFAppBuiltinToolCollection* print = pdf::PDFAppBuiltinToolCollections::findCollection(QStringLiteral("prepare-for-print"));
            pdf::PDFToolCollectionDraft draft;
            draft.builtinId = print->getId();
            draft.name = print->getSourceName();
            draft.toolIds = print->getToolIds();
            return fixture.collections(1000).saveCollection(draft);
        };
        operations["deleteCollection"] = [&]() { return fixture.collections().deleteCollection(custom); };
        operations["resetBuiltin"] = [&]() { return fixture.collections().resetBuiltin(QStringLiteral("prepare-for-print")); };
        operations["addToBuiltin"] = [&]() { return fixture.collections(1000).addTool(0, QStringLiteral("fix-a-scan"), QStringLiteral("pages.split")); };
        operations["addToCustom"] = [&]() { return fixture.collections(1000).addTool(custom, QString(), QStringLiteral("pages.split")); };
        operations["removeToDefault"] = [&]() { return fixture.collections(1000).setToolIncluded(0, QStringLiteral("extract-content"), QStringLiteral("pages.split"), false); };
        return operations;
    }

    QStringList pinned;
    QStringList files;
    QStringList folders;
    QString newFile;
    QString newFolder;
    QString newLocation;
    qint64 locations[2] = { };
    qint64 alpha = 0;
    qint64 beta = 0;
    qint64 custom = 0;

private:
    DataFixture& m_fixture;
};

void AppDataTest::repositoryFaults_data()
{
    QTest::addColumn<QString>("operation");
    QTest::addColumn<QString>("fault");

    const std::pair<const char*, QStringList> rows[] =
    {
        { "recordOpened", { "abort:INSERT:recent_file", "abort:INSERT:recent_folder", "abort:DELETE:recent_folder", "abort:DELETE:recent_file", "commit:recent_file", "read:recent_file" } },
        { "setReadingState", { "abort:UPDATE:recent_file", "commit:recent_file", "read:recent_file" } },
        { "removeFile", { "abort:DELETE:recent_file", "commit:recent_file" } },
        { "pin", { "abort:UPDATE:recent_file", "commit:recent_file", "read:recent_file" } },
        { "unpin", { "abort:UPDATE:recent_file", "abort:DELETE:recent_file" } },
        { "pinnedOrder", { "abort:UPDATE:recent_file", "commit:recent_file", "read:recent_file" } },
        { "thumbnail", { "abort:UPDATE:recent_file", "commit:recent_file" } },
        { "recordFolder", { "abort:INSERT:recent_folder", "abort:DELETE:recent_folder", "commit:recent_folder" } },
        { "removeFolder", { "abort:DELETE:recent_folder", "commit:recent_folder" } },
        { "addLocation", { "abort:INSERT:pinned_location", "commit:pinned_location" } },
        { "renameLocation", { "abort:UPDATE:pinned_location", "commit:pinned_location" } },
        { "removeLocation", { "abort:DELETE:pinned_location", "commit:pinned_location" } },
        { "locationOrder", { "abort:UPDATE:pinned_location", "commit:pinned_location" } },
        { "clearHistory", { "abort:DELETE:recent_file", "abort:UPDATE:workspace", "commit:recent_file" } },
        { "saveWorkspace", { "abort:INSERT:workspace", "abort:INSERT:workspace_document", "commit:workspace" } },
        { "replaceWorkspace", { "abort:UPDATE:workspace", "abort:DELETE:workspace_document" } },
        { "renameWorkspace", { "abort:UPDATE:workspace", "commit:workspace" } },
        { "deleteWorkspace", { "abort:DELETE:workspace_document", "abort:DELETE:workspace", "commit:workspace" } },
        { "markOpened", { "abort:UPDATE:workspace", "commit:workspace" } },
        { "saveLastSession", { "abort:DELETE:workspace_document", "abort:DELETE:workspace", "abort:INSERT:workspace", "abort:INSERT:workspace_document", "commit:workspace" } },
        { "pinTool", { "abort:INSERT:tool_pin", "commit:tool_pin" } },
        { "unpinTool", { "abort:DELETE:tool_pin" } },
        { "toolOrder", { "abort:UPDATE:tool_pin", "commit:tool_pin" } },
        { "recordUsage", { "abort:INSERT:tool_usage", "commit:tool_usage" } },
        { "clearUsage", { "abort:DELETE:tool_usage", "commit:tool_usage" } },
        { "createCollection", { "abort:INSERT:tool_collection", "abort:INSERT:tool_collection_item", "commit:tool_collection", "read:tool_collection" } },
        { "saveCustom", { "abort:UPDATE:tool_collection", "abort:DELETE:tool_collection_item", "abort:INSERT:tool_collection_item", "commit:tool_collection_item", "read:tool_collection_item" } },
        { "saveBuiltin", { "abort:INSERT:tool_collection", "abort:INSERT:tool_collection_item", "commit:tool_collection" } },
        { "saveOverride", { "abort:UPDATE:tool_collection", "abort:DELETE:tool_collection_item", "commit:tool_collection" } },
        { "saveDefault", { "abort:DELETE:tool_collection_item", "abort:DELETE:tool_collection", "commit:tool_collection" } },
        { "deleteCollection", { "abort:DELETE:tool_collection_item", "abort:DELETE:tool_collection", "commit:tool_collection", "read:tool_collection" } },
        { "resetBuiltin", { "abort:DELETE:tool_collection_item", "abort:DELETE:tool_collection", "commit:tool_collection" } },
        { "addToBuiltin", { "abort:INSERT:tool_collection", "abort:INSERT:tool_collection_item", "commit:tool_collection" } },
        { "addToCustom", { "abort:UPDATE:tool_collection", "abort:DELETE:tool_collection_item", "commit:tool_collection_item" } },
        { "removeToDefault", { "abort:DELETE:tool_collection_item", "abort:DELETE:tool_collection", "commit:tool_collection" } },
    };

    for (const auto& row : rows)
    {
        // A closed database: the transaction cannot begin
        QTest::addRow("%s closed", row.first) << QString::fromLatin1(row.first) << QStringLiteral("closed");
        for (const QString& fault : row.second)
        {
            QTest::addRow("%s %s", row.first, qPrintable(fault)) << QString::fromLatin1(row.first) << fault;
        }
    }
}

void AppDataTest::repositoryFaults()
{
    QFETCH(QString, operation);
    QFETCH(QString, fault);

    // A failed write of a repository reports the error and changes nothing: the
    // transaction is rolled back, also the parts written before the failure
    DataFixture fixture;
    AppDataFaultScene scene(fixture);
    QVERIFY(scene.prepare());
    std::map<QString, std::function<pdf::PDFDatabaseError()>> operations = scene.getOperations();
    QVERIFY(operations.count(operation));
    const std::function<pdf::PDFDatabaseError()> run = operations[operation];

    if (fault == QLatin1String("closed"))
    {
        fixture.database->close();
        QVERIFY(run().isError());
        return;
    }

    const QString before = fixture.snapshot();
    QVERIFY(AppDataFault::inject(fixture.database.get(), fault));
    const pdf::PDFDatabaseError error = run();
    QVERIFY(error.isError());
    QVERIFY(!fixture.database->getConnection()->isInTransaction());
    QVERIFY(AppDataFault::clear(fixture.database.get()));
    QCOMPARE(fixture.snapshot(), before);

    // Without the fault the same operation succeeds and changes the data (the
    // failure was caused by the fault, not by the input)
    QVERIFY2(!run().isError(), qPrintable(operation));
    QVERIFY(fixture.snapshot() != before);
}

void AppDataTest::recentRecordOpened()
{
    DataFixture fixture;
    const QString path = fixture.createFile(QStringLiteral("docs/Report.pdf"), QByteArray(1234, 'x'));
    QVERIFY(!path.isEmpty());
    QVERIFY(AppDataTestHelper::setModified(path, QDateTime(QDate(2026, 9, 14), QTime(10, 24), QTimeZone::UTC)));

    // Recorded by a native path with a redundant part: stored canonical
    QString storedPath = QStringLiteral("x");
    QVERIFY(!fixture.recent(1000).recordOpened(QDir::toNativeSeparators(QFileInfo(path).absolutePath() + QStringLiteral("/./Report.pdf")), &storedPath).isError());
    QCOMPARE(storedPath, path);

    std::optional<pdf::PDFRecentFile> file = fixture.recent().getFile(path);
    QVERIFY(file);
    QCOMPARE(file->path, path);
    QCOMPARE(file->name, QStringLiteral("Report.pdf"));
    QCOMPARE(file->folder, QFileInfo(path).absolutePath());
    QCOMPARE(file->lastOpened, qint64(1000));
    QCOMPARE(file->openCount, qint64(1));
    QCOMPARE(file->fileSize, qint64(1234));
    QCOMPARE(file->fileModified, QDateTime(QDate(2026, 9, 14), QTime(10, 24), QTimeZone::UTC).toMSecsSinceEpoch());
    QVERIFY(!file->pinned);
    QCOMPARE(file->pageCount, -1);
    QCOMPARE(file->thumbnailState, pdf::PDFRecentThumbnailState::Pending);
    QCOMPARE(file->getReadingState(), pdf::PDFRecentReadingState::NotStarted);
    QCOMPARE(file->getProgress(), 0.0);

    // The folder of the file is a recent folder
    const std::vector<pdf::PDFRecentFolder> folders = fixture.recent().getUsedFolders();
    QCOMPARE(int(folders.size()), 1);
    QCOMPARE(folders.front().path, file->folder);
    QCOMPARE(folders.front().lastUsed, qint64(1000));
    QCOMPARE(folders.front().name, QStringLiteral("docs"));

    // Opened again: the counter and the time
    QVERIFY(!fixture.recent(2000).recordOpened(path).isError());
    file = fixture.recent().getFile(path);
    QCOMPARE(file->openCount, qint64(2));
    QCOMPARE(file->lastOpened, qint64(2000));
    QCOMPARE(fixture.recent().getFileCount(), 1);

    // A missing file is recorded without its size (the page shows it missing)
    const QString missing = QFileInfo(path).absolutePath() + QStringLiteral("/Missing.pdf");
    QVERIFY(!fixture.recent(3000).recordOpened(missing).isError());
    QCOMPARE(fixture.recent().getFile(missing)->fileSize, qint64(-1));
    QCOMPARE(fixture.recent().getFile(missing)->fileModified, qint64(0));

    // Invalid paths are refused, nothing is written
    for (const QString& invalid : { QString(), QStringLiteral("Report.pdf"), QStringLiteral("../x.pdf"), path + QString(QChar(0)), QStringLiteral("/") + QString(40000, QLatin1Char('a')) })
    {
        QVERIFY(fixture.recent().recordOpened(invalid, &storedPath).isError());
        QVERIFY(storedPath.isEmpty());
    }
    QCOMPARE(fixture.recent().getFileCount(), 2);

    // Operations of a file, which is not recorded
    QVERIFY(fixture.recent().setPinned(path + QStringLiteral("x"), true).isError());
    QVERIFY(fixture.recent().setPinned(QStringLiteral("relative.pdf"), true).isError());
    QVERIFY(fixture.recent().setReadingState(path + QStringLiteral("x"), 2, 3, false).isError());
    QVERIFY(fixture.recent().setLocked(QStringLiteral("relative"), true).isError());
    QVERIFY(!fixture.recent().getFile(QStringLiteral("relative")));
    QVERIFY(!fixture.recent().removeFile(path + QStringLiteral("x")).isError());
    QVERIFY(fixture.recent().removeFile(QString()).isError());

    // Remove from List
    QVERIFY(!fixture.recent().removeFile(missing).isError());
    QCOMPARE(fixture.recent().getFileCount(), 1);

    // A closed database: errors, no crash
    fixture.database->close();
    QVERIFY(fixture.recent().recordOpened(path).isError());
    QVERIFY(fixture.recent().getFiles().empty());
    QVERIFY(!fixture.recent().getFile(path));
    const pdf::PDFRecentClearSummary closedSummary = fixture.recent().getClearHistorySummary(true);
    QCOMPARE(closedSummary.getRemovedEntries(), 0);
    QVERIFY(!closedSummary.hasLastSession);
    pdf::PDFRecentRepository nullRepository(nullptr, nullptr);
    QVERIFY(nullRepository.recordOpened(path).isError());
    QVERIFY(nullRepository.getFiles().empty());
    QVERIFY(nullRepository.getFolders().size() == pdf::PDFRecentRepository::getStandardFolders().size());
}

void AppDataTest::recentLimit_data()
{
    QTest::addColumn<int>("maximum");
    QTest::addColumn<int>("files");
    QTest::addColumn<int>("pinned");

    QTest::newRow("default 10, 15 files") << -1 << 15 << 0;
    QTest::newRow("1 file") << 1 << 5 << 0;
    QTest::newRow("3 files, 2 pinned") << 3 << 8 << 2;
    QTest::newRow("pinned over the limit") << 2 << 6 << 4;
    QTest::newRow("100") << 100 << 30 << 1;
}

void AppDataTest::recentApplyLimit()
{
    // Step 5: a lowered limit of Settings › General trims the history at once,
    // the pinned files stay
    DataFixture fixture;
    QStringList paths;
    for (int i = 0; i < 8; ++i)
    {
        const QString path = fixture.createFile(QStringLiteral("limit%1.pdf").arg(i));
        paths << path;
        QVERIFY(!fixture.recent(2000 + i).recordOpened(path).isError());
    }
    QVERIFY(!fixture.recent().setPinned(paths[0], true).isError());
    QVERIFY(!fixture.recent().setPinned(paths[1], true).isError());
    QCOMPARE(fixture.recent().getFiles().size(), size_t(8));

    // Nothing happens before applyLimit (the limit is read at the recording)
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), 3).isError());
    QCOMPARE(fixture.recent().getFiles().size(), size_t(8));
    QVERIFY(!fixture.recent().applyLimit().isError());

    const std::vector<pdf::PDFRecentFile> files = fixture.recent().getFiles();
    QCOMPARE(files.size(), size_t(5));
    QVERIFY(fixture.recent().getFile(paths[0]).has_value());
    QVERIFY(fixture.recent().getFile(paths[1]).has_value());
    for (int i = 5; i < 8; ++i)
    {
        QVERIFY(fixture.recent().getFile(paths[i]).has_value());
    }
    QVERIFY(!fixture.recent().getFile(paths[2]).has_value());

    // Again: nothing more to delete; a raised limit brings nothing back. Nothing to
    // delete takes no write lock (RC5-S: every instance gets the notification of
    // the setting) - also a connection, which cannot write, succeeds
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "PRAGMA query_only = 1"));
    QVERIFY(!fixture.recent().applyLimit().isError());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "PRAGMA query_only = 0"));
    QVERIFY(!fixture.recent().applyLimit().isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), 100).isError());
    QVERIFY(!fixture.recent().applyLimit().isError());
    QCOMPARE(fixture.recent().getFiles().size(), size_t(5));

    // A hostile stored limit is the default (10)
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('recent.maxFiles', -5, 0)"));
    QVERIFY(!fixture.recent().applyLimit().isError());
    QCOMPARE(fixture.recent().getFiles().size(), size_t(5));
}

void AppDataTest::clearToolHistory()
{
    // Step 5: Settings › General › Tool history - the pairs of the tools and the
    // count of the runs are cleared together
    DataFixture fixture;
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(0));
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES ('merge', 'compress', 30, 1), ('split', 'merge', 7, 2)"));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::toolsHistoryRuns(), 37).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(37));

    pdf::PDFDatabaseNotifier notifier(fixture.database.get());
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    QVERIFY(!fixture.tools().clearHistory().isError());
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(0));
    QVERIFY(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM tool_pair") == 0);

    // The pinned tools, the usage and the other settings are kept
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.merge") }));

    // Notifications of both topics
    QTRY_VERIFY(spy.size() >= 2);
    QStringList topics;
    for (const QList<QVariant>& arguments : spy)
    {
        topics << arguments.value(0).toString();
    }
    QVERIFY(topics.contains(pdf::PDFToolRepository::getPairTopic()));
    QVERIFY(topics.contains(pdf::PDFSettingsRepository::getTopic()));

    // Hostile stored counts are 0, a clear of an empty history is no error
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('tools.historyRuns', 'many', 0)"));
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(0));
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('tools.historyRuns', -3, 0)"));
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(0));
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('tools.historyRuns', 1.5, 0)"));
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(0));
    QVERIFY(!fixture.tools().clearHistory().isError());
    QVERIFY(!fixture.tools().clearHistory().isError());
}

void AppDataTest::recentLimit()
{
    QFETCH(int, maximum);
    QFETCH(int, files);
    QFETCH(int, pinned);

    DataFixture fixture;
    if (maximum > 0)
    {
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), maximum).isError());
    }
    const int limit = maximum > 0 ? maximum : 10;

    QStringList paths;
    for (int i = 0; i < files; ++i)
    {
        const QString path = fixture.createFile(QStringLiteral("file%1.pdf").arg(i, 3, 10, QLatin1Char('0')));
        paths << path;
        QVERIFY(!fixture.recent(1000 + i).recordOpened(path).isError());

        // The first files are pinned right after their opening: they are never deleted
        if (i < pinned)
        {
            QVERIFY(!fixture.recent().setPinned(path, true).isError());
        }
    }

    const std::vector<pdf::PDFRecentFile> recentFiles = fixture.recent().getFiles();
    int pinnedCount = 0;
    int unpinnedCount = 0;
    for (const pdf::PDFRecentFile& file : recentFiles)
    {
        (file.pinned ? pinnedCount : unpinnedCount) += 1;
    }
    QCOMPARE(pinnedCount, pinned);
    QCOMPARE(unpinnedCount, qMin(limit, files - pinned));

    // The newest files are kept
    QCOMPARE(recentFiles.front().path, paths.last());

    // Unpinning a file applies the limit again
    if (pinned > 0)
    {
        QVERIFY(!fixture.recent().setPinned(paths.first(), false).isError());
        int unpinnedAfter = 0;
        for (const pdf::PDFRecentFile& file : fixture.recent().getFiles())
        {
            unpinnedAfter += file.pinned ? 0 : 1;
        }
        QCOMPARE(unpinnedAfter, qMin(limit, files - pinned + 1));
        if (files - pinned >= limit)
        {
            // The unpinned file is the oldest one: it is deleted
            QVERIFY(!fixture.recent().getFile(paths.first()));
        }
    }

    // A hostile value of the setting is replaced by the default (10)
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT OR REPLACE INTO setting(key, value, modified) VALUES ('recent.maxFiles', 'many', 0)"));
    QVERIFY(!fixture.recent(5000).recordOpened(fixture.createFile(QStringLiteral("new.pdf"))).isError());
    int unpinnedFinal = 0;
    for (const pdf::PDFRecentFile& file : fixture.recent().getFiles())
    {
        unpinnedFinal += file.pinned ? 0 : 1;
    }
    QVERIFY(unpinnedFinal <= 10);
}

void AppDataTest::recentPinning()
{
    DataFixture fixture;
    QStringList paths;
    for (int i = 0; i < 4; ++i)
    {
        paths << fixture.createFile(QStringLiteral("p%1.pdf").arg(i));
        QVERIFY(!fixture.recent(100 + i).recordOpened(paths.last()).isError());
    }

    QVERIFY(!fixture.recent().setPinned(paths[2], true).isError());
    QVERIFY(!fixture.recent().setPinned(paths[0], true).isError());
    QVERIFY(!fixture.recent().setPinned(paths[3], true).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[2], paths[0], paths[3] }));
    QCOMPARE(fixture.recent().getPinnedCount(), 3);
    QCOMPARE(fixture.recent().getFile(paths[2])->pinOrder, qint64(1));

    // Pinning twice changes nothing
    QVERIFY(!fixture.recent().setPinned(paths[2], true).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[2], paths[0], paths[3] }));

    // Order: listed first, unknown and duplicate paths ignored, the rest after them
    QVERIFY(!fixture.recent().setPinnedOrder({ paths[3], QStringLiteral("/unknown.pdf"), paths[3], paths[1] }).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[3], paths[2], paths[0] }));
    QVERIFY(!fixture.recent().setPinnedOrder({}).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[3], paths[2], paths[0] }));

    // A duplicate keeps its first place (a second place would move it after the others)
    QVERIFY(!fixture.recent().setPinnedOrder({ paths[2], paths[0], paths[2] }).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[2], paths[0], paths[3] }));
    QVERIFY(!fixture.recent().setPinnedOrder({ paths[3], paths[2], paths[0] }).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[3], paths[2], paths[0] }));

    // Unpin
    QVERIFY(!fixture.recent().setPinned(paths[2], false).isError());
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles()), QStringList({ paths[3], paths[0] }));
    QCOMPARE(fixture.recent().getFile(paths[2])->pinOrder, qint64(0));
    QVERIFY(!fixture.recent().getFile(paths[2])->pinned);

    // Pinned only query
    pdf::PDFRecentQuery query;
    query.pinnedOnly = true;
    QCOMPARE(int(fixture.recent().getFiles(query).size()), 2);

    // A new pin goes after the others (also with a hostile pin_order)
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "UPDATE recent_file SET pin_order = 'x' WHERE pinned = 1 AND name = 'p3.pdf'"));
    QVERIFY(!fixture.recent().setPinned(paths[1], true).isError());
    const QStringList pinnedPaths = AppDataTestHelper::getPaths(fixture.recent().getPinnedFiles());
    QCOMPARE(pinnedPaths.size(), 3);
    QVERIFY(pinnedPaths.contains(paths[1]));
}

void AppDataTest::recentQueries_data()
{
    QTest::addColumn<QString>("sort");
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("pinnedOnly");
    QTest::addColumn<int>("limit");
    QTest::addColumn<QStringList>("expected");

    // Files (opened in this order): b/Zeta.pdf, a/alpha.pdf, c/Beta.pdf, a/Gamma report.pdf; Beta pinned
    QTest::newRow("last opened") << "last-opened" << QString() << false << -1 << QStringList({ "Gamma report.pdf", "Beta.pdf", "alpha.pdf", "Zeta.pdf" });
    QTest::newRow("name") << "name" << QString() << false << -1 << QStringList({ "alpha.pdf", "Beta.pdf", "Gamma report.pdf", "Zeta.pdf" });
    QTest::newRow("folder") << "folder" << QString() << false << -1 << QStringList({ "alpha.pdf", "Gamma report.pdf", "Zeta.pdf", "Beta.pdf" });
    QTest::newRow("limit") << "last-opened" << QString() << false << 2 << QStringList({ "Gamma report.pdf", "Beta.pdf" });
    QTest::newRow("limit 0") << "last-opened" << QString() << false << 0 << QStringList();
    QTest::newRow("text name") << "name" << "ETA" << false << -1 << QStringList({ "Beta.pdf", "Zeta.pdf" });
    QTest::newRow("text folder") << "name" << "/a/" << false << -1 << QStringList({ "alpha.pdf", "Gamma report.pdf" });
    QTest::newRow("text space") << "name" << " report " << false << -1 << QStringList({ "Gamma report.pdf" });
    QTest::newRow("text none") << "name" << "nothing" << false << -1 << QStringList();
    QTest::newRow("pinned") << "name" << QString() << true << -1 << QStringList({ "Beta.pdf" });
    QTest::newRow("pinned text") << "name" << "zeta" << true << -1 << QStringList();
    QTest::newRow("limit above count") << "folder" << QString() << false << 10 << QStringList({ "alpha.pdf", "Gamma report.pdf", "Zeta.pdf", "Beta.pdf" });

    // Native separators (Windows: backslashes match the native form of the stored path)
    QTest::newRow("text native folder") << "name" << QDir::toNativeSeparators(QStringLiteral("/a/")) << false << -1 << QStringList({ "alpha.pdf", "Gamma report.pdf" });
}

void AppDataTest::recentQueries()
{
    QFETCH(QString, sort);
    QFETCH(QString, text);
    QFETCH(bool, pinnedOnly);
    QFETCH(int, limit);
    QFETCH(QStringList, expected);

    DataFixture fixture;
    const QStringList files = { QStringLiteral("b/Zeta.pdf"), QStringLiteral("a/alpha.pdf"), QStringLiteral("c/Beta.pdf"), QStringLiteral("a/Gamma report.pdf") };
    for (int i = 0; i < files.size(); ++i)
    {
        QVERIFY(!fixture.recent(1000 + i).recordOpened(fixture.createFile(files[i])).isError());
    }
    QVERIFY(!fixture.recent().setPinned(pdf::PDFRepositoryHelper::normalizePath(fixture.directory.filePath(QStringLiteral("c/Beta.pdf"))), true).isError());

    pdf::PDFRecentQuery query;
    std::optional<pdf::PDFRecentSort> parsedSort = pdf::PDFRecentRepository::parseSort(sort);
    QVERIFY(parsedSort);
    QCOMPARE(pdf::PDFRecentRepository::getSortId(*parsedSort), sort);
    query.sort = *parsedSort;
    query.text = text;
    query.pinnedOnly = pinnedOnly;
    query.limit = limit;
    QCOMPARE(AppDataTestHelper::getNames(fixture.recent().getFiles(query)), expected);

    // An invalid sort keeps the order of the last opening
    if (*parsedSort == pdf::PDFRecentSort::LastOpened)
    {
        query.sort = pdf::PDFRecentSort(9);
        QCOMPARE(AppDataTestHelper::getNames(fixture.recent().getFiles(query)), expected);
    }

    // Equal names are ordered by their paths (also within a folder sort)
    if (text.isEmpty() && !pinnedOnly && limit < 0 && *parsedSort != pdf::PDFRecentSort::LastOpened)
    {
        const QString secondZeta = fixture.createFile(QStringLiteral("0/zeta.pdf"));
        QVERIFY(!fixture.recent(2000).recordOpened(secondZeta).isError());
        QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "UPDATE recent_file SET folder = '/same' WHERE name IN ('zeta.pdf', 'Zeta.pdf')"));
        const std::vector<pdf::PDFRecentFile> sortedFiles = fixture.recent().getFiles(query);
        QStringList zetaPaths;
        for (const pdf::PDFRecentFile& file : sortedFiles)
        {
            if (file.name.compare(QStringLiteral("zeta.pdf"), Qt::CaseInsensitive) == 0)
            {
                zetaPaths << file.path;
            }
        }
        QCOMPARE(zetaPaths.size(), 2);
        QVERIFY(QString::compare(zetaPaths[0], zetaPaths[1], Qt::CaseInsensitive) < 0);
    }
}

void AppDataTest::recentReadingState()
{
    DataFixture fixture;
    const QString first = fixture.createFile(QStringLiteral("first.pdf"));
    const QString second = fixture.createFile(QStringLiteral("second.pdf"));
    const QString third = fixture.createFile(QStringLiteral("third.pdf"));
    QVERIFY(!fixture.recent(100).recordOpened(first).isError());
    QVERIFY(!fixture.recent(200).recordOpened(second).isError());
    QVERIFY(!fixture.recent(300).recordOpened(third).isError());
    QVERIFY(fixture.recent().getContinueReading().empty());

    // Invalid pages
    QVERIFY(fixture.recent().setReadingState(first, 0, 10, false).isError());
    QVERIFY(fixture.recent().setReadingState(first, 11, 10, false).isError());
    QVERIFY(fixture.recent().setReadingState(first, 2, -1, false).isError());
    QVERIFY(fixture.recent().setReadingState(first, pdf::PDFRecentRepository::MAXIMUM_PAGE + 1, 0, false).isError());
    QVERIFY(fixture.recent().setReadingState(first, 1, pdf::PDFRecentRepository::MAXIMUM_PAGE + 1, false).isError());

    // Page 12 of 24
    QVERIFY(!fixture.recent().setReadingState(first, 12, 24, false).isError());
    std::optional<pdf::PDFRecentFile> file = fixture.recent().getFile(first);
    QCOMPARE(file->lastPage, 12);
    QCOMPARE(file->pageCount, 24);
    QCOMPARE(file->getReadingState(), pdf::PDFRecentReadingState::InProgress);
    QCOMPARE(file->getProgress(), 0.5);

    // Page 1 = Not started; unknown page count keeps the stored one
    QVERIFY(!fixture.recent().setReadingState(second, 1, 0, false).isError());
    QCOMPARE(fixture.recent().getFile(second)->getReadingState(), pdf::PDFRecentReadingState::NotStarted);
    QVERIFY(!fixture.recent().setReadingState(second, 3, 0, false).isError());
    QCOMPARE(fixture.recent().getFile(second)->pageCount, -1);
    QCOMPARE(fixture.recent().getFile(second)->getProgress(), 0.0);

    // Finished stays Finished
    QVERIFY(!fixture.recent().setReadingState(third, 36, 36, true).isError());
    QVERIFY(!fixture.recent().setReadingState(third, 5, 36, false).isError());
    file = fixture.recent().getFile(third);
    QCOMPARE(file->getReadingState(), pdf::PDFRecentReadingState::Finished);
    QCOMPARE(file->getProgress(), 1.0);
    QCOMPARE(file->lastPage, 5);

    // Continue where you left off: in progress, newest first, not finished
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getContinueReading()), QStringList({ second, first }));
    QCOMPARE(AppDataTestHelper::getPaths(fixture.recent().getContinueReading(1)), QStringList({ second }));
    QVERIFY(fixture.recent().getContinueReading(0).empty());

    // View state and lock
    QVERIFY(!fixture.recent().setViewState(first, QStringLiteral("{\"layout\":\"two-pages\"}")).isError());
    QCOMPARE(fixture.recent().getFile(first)->viewState, QStringLiteral("{\"layout\":\"two-pages\"}"));
    QVERIFY(fixture.recent().setViewState(first, QString(pdf::PDFRecentRepository::MAXIMUM_VIEW_STATE_LENGTH + 1, QLatin1Char('x'))).isError());
    QVERIFY(!fixture.recent().setLocked(first, true).isError());
    QVERIFY(fixture.recent().getFile(first)->locked);
    QVERIFY(!fixture.recent().setLocked(first, false).isError());
    QVERIFY(!fixture.recent().getFile(first)->locked);

    // A stored page after the end is limited to the page count
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "UPDATE recent_file SET last_page = 99 WHERE name = 'first.pdf'"));
    QCOMPARE(fixture.recent().getFile(first)->lastPage, 24);
}

void AppDataTest::recentThumbnails()
{
    DataFixture fixture;
    const QString path = fixture.createFile(QStringLiteral("thumb.pdf"));
    QVERIFY(!fixture.recent().recordOpened(path).isError());

    QImage image(40, 60, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    {
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
    }

    QVERIFY(!fixture.recent().setThumbnail(path, png, 1234, pdf::PDFRecentThumbnailState::Ready, 7).isError());
    pdf::PDFRecentThumbnail thumbnail = fixture.recent().getThumbnail(path);
    QCOMPARE(thumbnail.png, png);
    QCOMPARE(thumbnail.fileModified, qint64(1234));
    QCOMPARE(thumbnail.state, pdf::PDFRecentThumbnailState::Ready);
    QCOMPARE(thumbnail.toImage().size(), QSize(40, 60));
    QCOMPARE(fixture.recent().getFile(path)->pageCount, 7);
    QVERIFY(fixture.recent().getFile(path)->hasThumbnail);

    // Password protected: no picture, the page count is not changed by -1
    QVERIFY(!fixture.recent().setThumbnail(path, QByteArray(), 2000, pdf::PDFRecentThumbnailState::PasswordProtected, -1).isError());
    thumbnail = fixture.recent().getThumbnail(path);
    QVERIFY(thumbnail.png.isEmpty());
    QCOMPARE(thumbnail.state, pdf::PDFRecentThumbnailState::PasswordProtected);
    QCOMPARE(fixture.recent().getFile(path)->pageCount, 7);
    QVERIFY(!fixture.recent().getFile(path)->hasThumbnail);
    QVERIFY(thumbnail.toImage().isNull());

    // Invalid input
    QVERIFY(fixture.recent().setThumbnail(path, QByteArray(pdf::PDFRecentRepository::MAXIMUM_THUMBNAIL_BYTES + 1, 'x'), 1, pdf::PDFRecentThumbnailState::Ready, 1).isError());
    QVERIFY(fixture.recent().setThumbnail(path, png, 1, pdf::PDFRecentThumbnailState(9), 1).isError());
    QVERIFY(fixture.recent().setThumbnail(path, png, -1, pdf::PDFRecentThumbnailState::Ready, 1).isError());
    QVERIFY(fixture.recent().setThumbnail(QStringLiteral("x"), png, 1, pdf::PDFRecentThumbnailState::Ready, 1).isError());
    QVERIFY(fixture.recent().setThumbnail(path, png, 1, pdf::PDFRecentThumbnailState(-1), 1).isError());
    QVERIFY(fixture.recent().setThumbnail(path, png, 1, pdf::PDFRecentThumbnailState::Ready, pdf::PDFRecentRepository::MAXIMUM_PAGE + 1).isError());
    QVERIFY(fixture.recent().getThumbnail(QStringLiteral("relative.pdf")).png.isEmpty());
    QCOMPARE(fixture.recent().getThumbnail(path).state, pdf::PDFRecentThumbnailState::PasswordProtected);

    // A file, which is not recorded: nothing is inserted
    QVERIFY(!fixture.recent().setThumbnail(path + QStringLiteral(".other"), png, 1, pdf::PDFRecentThumbnailState::Ready, 1).isError());
    QCOMPARE(fixture.recent().getFileCount(), 1);
    QVERIFY(fixture.recent().getThumbnail(path + QStringLiteral(".other")).png.isEmpty());

    // Hostile PNG data: no image
    pdf::PDFRecentThumbnail hostile;
    hostile.state = pdf::PDFRecentThumbnailState::Ready;
    hostile.png = QByteArray("\x89PNG garbage", 12);
    QVERIFY(hostile.toImage().isNull());
    QImage large(400, 10, QImage::Format_RGB32);
    large.fill(Qt::blue);
    hostile.png.clear();
    {
        QBuffer buffer(&hostile.png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(large.save(&buffer, "PNG"));
    }
    QVERIFY(hostile.toImage().isNull());
    QImage tall(10, 400, QImage::Format_RGB32);
    tall.fill(Qt::blue);
    hostile.png.clear();
    {
        QBuffer buffer(&hostile.png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(tall.save(&buffer, "PNG"));
    }
    QVERIFY(hostile.toImage().isNull());

    // Too many bytes, and a PNG with a valid header and size, whose data are cut off
    hostile.png = QByteArray(pdf::PDFRecentRepository::MAXIMUM_THUMBNAIL_BYTES + 1, 'x');
    QVERIFY(hostile.toImage().isNull());
    hostile.png = png.left(png.size() / 2);
    QVERIFY(hostile.toImage().isNull());

    // An oversized blob of another version is not read
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "UPDATE recent_file SET thumbnail = zeroblob(3000000), thumbnail_state = 1"));
    QVERIFY(fixture.recent().getThumbnail(path).png.isEmpty());
    QVERIFY(!fixture.recent().getFile(path)->hasThumbnail);
}

void AppDataTest::recentClearHistory_data()
{
    QTest::addColumn<bool>("includePinned");
    QTest::newRow("keep pinned") << false;
    QTest::newRow("remove pinned") << true;
}

void AppDataTest::recentClearHistory()
{
    QFETCH(bool, includePinned);

    DataFixture fixture;
    QStringList paths;
    for (int i = 0; i < 6; ++i)
    {
        paths << fixture.createFile(QStringLiteral("f%1/doc%1.pdf").arg(i));
        QVERIFY(!fixture.recent(100 + i).recordOpened(paths.last()).isError());
        QVERIFY(!fixture.recent().setReadingState(paths.last(), 3, 10, false).isError());
    }
    QVERIFY(!fixture.recent().setPinned(paths[0], true).isError());
    QVERIFY(!fixture.recent().setPinned(paths[1], true).isError());
    QVERIFY(!fixture.recent().setLocked(paths[0], true).isError());

    // Workspaces: two saved (one opened), the last session
    const std::vector<pdf::PDFWorkspaceDocument> documents = { { paths[0], 2 }, { paths[1], 0 } };
    qint64 openedId = 0;
    QVERIFY(!fixture.workspaces(500).saveWorkspace(QStringLiteral("Opened"), documents, false, &openedId).isError());
    QVERIFY(!fixture.workspaces(600).markOpened(openedId).isError());
    QVERIFY(!fixture.workspaces(500).saveWorkspace(QStringLiteral("Never opened"), documents, false).isError());
    QVERIFY(!fixture.workspaces(700).saveLastSession(documents).isError());

    // Pinned locations and tools are not a part of the history
    QVERIFY(!fixture.recent().addPinnedLocation(fixture.directory.path(), QString()).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());

    const pdf::PDFRecentClearSummary summary = fixture.recent().getClearHistorySummary(includePinned);
    QCOMPARE(summary.pinnedFiles, 2);
    QCOMPARE(summary.removedFiles, includePinned ? 6 : 4);
    QCOMPARE(summary.removedFolders, 6);
    QCOMPARE(summary.removedWorkspaces, 1);
    QVERIFY(summary.hasLastSession);
    QVERIFY(summary.hasContinueReading);
    QCOMPARE(summary.getRemovedEntries(), summary.removedFiles + 7);

    QVERIFY(!fixture.recent().clearHistory(includePinned).isError());

    QCOMPARE(fixture.recent().getFileCount(), includePinned ? 0 : 2);
    QCOMPARE(fixture.recent().getPinnedCount(), includePinned ? 0 : 2);
    QVERIFY(fixture.recent().getUsedFolders().empty());
    QVERIFY(fixture.recent().getContinueReading().empty());
    QVERIFY(!fixture.workspaces().getLastSession());
    QVERIFY(fixture.workspaces().getRecentWorkspaces().empty());
    QCOMPARE(int(fixture.workspaces().getWorkspaces().size()), 2);
    QCOMPARE(int(fixture.recent().getPinnedLocations().size()), 1);
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.merge") }));

    if (!includePinned)
    {
        // Kept pinned files forget where the reading stopped, but keep the lock
        const std::optional<pdf::PDFRecentFile> file = fixture.recent().getFile(paths[0]);
        QVERIFY(file);
        QCOMPARE(file->lastPage, 0);
        QVERIFY(!file->reachedEnd);
        QVERIFY(file->locked);
    }

    const pdf::PDFRecentClearSummary empty = fixture.recent().getClearHistorySummary(true);
    QCOMPARE(empty.getRemovedEntries(), includePinned ? 0 : 2);
    QVERIFY(!empty.hasLastSession);
    QVERIFY(!empty.hasContinueReading);
}

void AppDataTest::recentFolders()
{
    DataFixture fixture;

    // Standard folders first, also never used ones, in the order of Figma
    const std::vector<pdf::PDFRecentFolder> standardFolders = pdf::PDFRecentRepository::getStandardFolders();
    std::vector<pdf::PDFRecentFolder> folders = fixture.recent().getFolders();
    QCOMPARE(folders.size(), standardFolders.size());
    int previousKind = 0;
    for (size_t i = 0; i < standardFolders.size(); ++i)
    {
        QVERIFY(standardFolders[i].isStandard());
        QVERIFY(int(standardFolders[i].kind) > previousKind);
        previousKind = int(standardFolders[i].kind);
        QVERIFY(!standardFolders[i].name.isEmpty());
        QCOMPARE(folders[i].lastUsed, qint64(0));
    }

    // 25 used folders: at most 20 are kept, the newest ones
    QStringList paths;
    for (int i = 0; i < 25; ++i)
    {
        const QString folderPath = fixture.directory.filePath(QStringLiteral("folder%1").arg(i, 2, 10, QLatin1Char('0')));
        QVERIFY(QDir().mkpath(folderPath));
        paths << pdf::PDFRepositoryHelper::normalizePath(folderPath);
        QVERIFY(!fixture.recent(1000 + i).recordFolder(folderPath).isError());
    }

    std::vector<pdf::PDFRecentFolder> usedFolders = fixture.recent().getUsedFolders();
    QCOMPARE(int(usedFolders.size()), pdf::PDFRecentRepository::MAXIMUM_RECENT_FOLDERS);
    QCOMPARE(usedFolders.front().path, paths.last());
    QCOMPARE(usedFolders.front().name, QStringLiteral("folder24"));
    QCOMPARE(usedFolders.back().path, paths[5]);

    folders = fixture.recent().getFolders();
    QCOMPARE(folders.size(), standardFolders.size() + pdf::PDFRecentRepository::MAXIMUM_RECENT_FOLDERS);
    QCOMPARE(folders[standardFolders.size()].path, paths.last());

    // A used standard folder keeps its place and gets its time; it does not
    // count to the limit of the other folders
    if (!standardFolders.empty())
    {
        QVERIFY(!fixture.recent(5000).recordFolder(standardFolders.front().path).isError());
        folders = fixture.recent().getFolders();
        QCOMPARE(folders.front().kind, standardFolders.front().kind);
        QCOMPARE(folders.front().lastUsed, qint64(5000));
        QCOMPARE(folders.size(), standardFolders.size() + pdf::PDFRecentRepository::MAXIMUM_RECENT_FOLDERS);

        usedFolders = fixture.recent().getUsedFolders();
        QCOMPARE(usedFolders.front().kind, standardFolders.front().kind);
        QCOMPARE(usedFolders.front().name, standardFolders.front().name);

        // Remove from List of a standard folder: only its time is forgotten
        QVERIFY(!fixture.recent().removeFolder(usedFolders.front().path).isError());
        QCOMPARE(fixture.recent().getFolders().front().lastUsed, qint64(0));
    }

    QVERIFY(!fixture.recent().removeFolder(paths.last()).isError());
    QCOMPARE(fixture.recent().getUsedFolders().front().path, paths[23]);
    QVERIFY(fixture.recent().removeFolder(QStringLiteral("relative")).isError());
    QVERIFY(fixture.recent().recordFolder(QStringLiteral("relative")).isError());

    // A standard folder of another computer (copied database) is an ordinary folder
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT INTO recent_folder(path, last_used, kind) VALUES ('/elsewhere/Documents', 99999, 1)"));
    usedFolders = fixture.recent().getUsedFolders();
    QCOMPARE(usedFolders.front().path, QStringLiteral("/elsewhere/Documents"));
    QCOMPARE(usedFolders.front().kind, pdf::PDFRecentFolderKind::Other);

    // More folders than the limit written by another version: Open shows the newest 20
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 25) "
                                                               "INSERT INTO recent_folder(path, last_used, kind) SELECT '/many/' || i, 200000 + i, 0 FROM n"));
    QVERIFY(int(fixture.recent().getUsedFolders().size()) > pdf::PDFRecentRepository::MAXIMUM_RECENT_FOLDERS);
    folders = fixture.recent().getFolders();
    QCOMPARE(folders.size(), standardFolders.size() + pdf::PDFRecentRepository::MAXIMUM_RECENT_FOLDERS);
    QCOMPARE(folders[standardFolders.size()].path, QStringLiteral("/many/25"));
    QCOMPARE(folders.back().path, QStringLiteral("/many/6"));

    // Folders used at the same time are ordered by their paths
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT INTO recent_folder(path, last_used, kind) VALUES ('/tie/b', 900000, 0), ('/tie/a', 900000, 0)"));
    usedFolders = fixture.recent().getUsedFolders();
    QCOMPARE(usedFolders[0].path, QStringLiteral("/tie/a"));
    QCOMPARE(usedFolders[1].path, QStringLiteral("/tie/b"));
}

void AppDataTest::pinnedLocations()
{
    DataFixture fixture;
    const QString projects = fixture.directory.filePath(QStringLiteral("Projects"));
    const QString shared = fixture.directory.filePath(QStringLiteral("Shared"));
    QVERIFY(QDir().mkpath(projects));
    QVERIFY(QDir().mkpath(shared));

    qint64 projectsId = 0;
    bool isDuplicate = true;
    QVERIFY(!fixture.recent().addPinnedLocation(projects, QStringLiteral("  Work Projects "), &projectsId, &isDuplicate).isError());
    QVERIFY(projectsId > 0);
    QVERIFY(!isDuplicate);

    qint64 sharedId = 0;
    QVERIFY(!fixture.recent().addPinnedLocation(QDir::toNativeSeparators(shared), QString(), &sharedId).isError());

    std::vector<pdf::PDFPinnedLocation> locations = fixture.recent().getPinnedLocations();
    QCOMPARE(int(locations.size()), 2);
    QCOMPARE(locations[0].name, QStringLiteral("Work Projects"));
    QCOMPARE(locations[0].path, pdf::PDFRepositoryHelper::normalizePath(projects));
    QCOMPARE(locations[0].kind, pdf::PDFPinnedLocationKind::Local);
    QCOMPARE(locations[1].name, QStringLiteral("Shared"));
    QVERIFY(fixture.recent().hasPinnedLocation(projects));

    // Duplicate path (also another spelling of it)
    QVERIFY(fixture.recent().addPinnedLocation(projects + QStringLiteral("/../Projects"), QStringLiteral("Again"), nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);
    QCOMPARE(int(fixture.recent().getPinnedLocations().size()), 2);
    QVERIFY(fixture.recent().addPinnedLocation(projects, QString()).isError());

    // Invalid input
    QVERIFY(fixture.recent().addPinnedLocation(QStringLiteral("relative"), QString()).isError());
    QVERIFY(fixture.recent().addPinnedLocation(fixture.directory.filePath(QStringLiteral("Other")), QStringLiteral("\n\t")).isError());
    QVERIFY(fixture.recent().renamePinnedLocation(projectsId, QStringLiteral("   ")).isError());
    QVERIFY(fixture.recent().renamePinnedLocation(projectsId, QString(1000, QLatin1Char('x'))).isError());

    // Rename, reorder, remove
    QVERIFY(!fixture.recent().renamePinnedLocation(projectsId, QStringLiteral("Projects 2026")).isError());
    QVERIFY(!fixture.recent().setPinnedLocationOrder({ sharedId, 12345, sharedId }).isError());
    locations = fixture.recent().getPinnedLocations();
    QCOMPARE(locations[0].id, sharedId);
    QCOMPARE(locations[1].name, QStringLiteral("Projects 2026"));
    QVERIFY(!fixture.recent().removePinnedLocation(sharedId).isError());
    QVERIFY(!fixture.recent().removePinnedLocation(99999).isError());
    QCOMPARE(int(fixture.recent().getPinnedLocations().size()), 1);

    // Kinds by the path
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("\\\\server\\documents")), pdf::PDFPinnedLocationKind::Network);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("//server/documents")), pdf::PDFPinnedLocationKind::Network);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("smb://server/share")), pdf::PDFPinnedLocationKind::Network);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("/run/user/1000/gvfs/smb-share:server=x/docs")), pdf::PDFPinnedLocationKind::Network);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("NFS://server/export")), pdf::PDFPinnedLocationKind::Network);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("/run/user/1000/documents")), pdf::PDFPinnedLocationKind::Local);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("C:\\Users\\Jakub\\OneDrive")), pdf::PDFPinnedLocationKind::Cloud);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("C:/Users/Jakub/OneDrive - Company/Docs")), pdf::PDFPinnedLocationKind::Cloud);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("/home/jakub/Dropbox (Personal)")), pdf::PDFPinnedLocationKind::Cloud);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("/Users/jakub/Library/CloudStorage/GoogleDrive-x")), pdf::PDFPinnedLocationKind::Cloud);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("D:/Projects")), pdf::PDFPinnedLocationKind::Local);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QStringLiteral("/home/jakub/OneDriveBackup")), pdf::PDFPinnedLocationKind::Local);
    QCOMPARE(pdf::PDFRecentRepository::detectLocationKind(QString()), pdf::PDFPinnedLocationKind::Local);
    QCOMPARE(pdf::PDFRecentRepository::getLocationKindText(pdf::PDFPinnedLocationKind::Local), QStringLiteral("This PC"));
    QCOMPARE(pdf::PDFRecentRepository::getLocationKindText(pdf::PDFPinnedLocationKind::Cloud), QStringLiteral("Personal cloud"));
    QCOMPARE(pdf::PDFRecentRepository::getLocationKindText(pdf::PDFPinnedLocationKind::Network), QStringLiteral("Network"));
    QVERIFY(pdf::PDFRecentRepository::getLocationKindText(pdf::PDFPinnedLocationKind(7)).isEmpty());
    QVERIFY(!pdf::PDFRecentRepository::parseSort(u"newest"));
    QVERIFY(pdf::PDFRecentRepository::getSortId(pdf::PDFRecentSort(9)).isEmpty());
}

void AppDataTest::hostileRows()
{
    DataFixture fixture;
    pdf::PDFDatabase* database = fixture.database.get();
    const QString valid = fixture.createFile(QStringLiteral("valid.pdf"));
    QVERIFY(!fixture.recent(100).recordOpened(valid).isError());

    // Rows written by another version or a damaged file
    const char* statements[] =
    {
        "INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('relative.pdf', 'r', '/', 1)",
        "INSERT INTO recent_file(path, name, folder, last_opened) VALUES (x'2F612E706466', 'blob path', '/', 2)",
        "INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('', 'empty', '/', 3)",
        "INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('/a' || char(0) || 'b.pdf', 'nul', '/', 4)",
        "INSERT INTO recent_file(path, name, folder, last_opened, open_count, pinned, page_count, file_size, last_page, reached_end, locked, thumbnail_state, view_state) "
        "VALUES ('/hostile/types.pdf', zeroblob(10), 42, 'yesterday', 'many', 'yes', -5, 1.5, 'x', 7, 2, 99, zeroblob(100))",
        "INSERT INTO recent_file(path, name, folder, last_opened, page_count, last_page) VALUES ('/hostile/huge.pdf', 'huge', '/hostile', 9223372036854775807, 9223372036854775807, -1)",
        "INSERT INTO recent_folder(path, last_used, kind) VALUES (42, 'x', 'y')",
        "INSERT INTO recent_folder(path, last_used, kind) VALUES ('/hostile/folder', 'x', 99)",
        "INSERT INTO pinned_location(path, name, kind, position) VALUES ('relative', 'x', 0, 0)",
        "INSERT INTO pinned_location(path, name, kind, position) VALUES ('/hostile/location', x'00FF', 'z', 'last')",
        "INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('', 'x', 'y', 'z', 0)",
        "INSERT INTO workspace_document(workspace_id, position, path, page) VALUES ((SELECT MAX(id) FROM workspace), 0, 'relative', 'x')",
        "INSERT INTO workspace_document(workspace_id, position, path, page) VALUES ((SELECT MAX(id) FROM workspace), 1, '/hostile/doc.pdf', 99999999999)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('security.encrypt', 1, 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES (x'00', 'x', 1)",
        "INSERT INTO tool_pin(tool_id, position, pinned) VALUES ('pages.merge', 'first', 1)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('unknown.tool', 1, 1)",
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('optimize.compress', 'x', -4)",

        // Rows with the identifier 0 (the readers skip them)
        "INSERT INTO recent_file(id, path, name, folder, last_opened) VALUES (0, '/hostile/zero.pdf', 'zero', '/hostile', 5)",
        "INSERT INTO pinned_location(id, path, name, kind, position) VALUES (0, '/hostile/zero', 'zero', 0, 0)",
        "INSERT INTO workspace(id, name, created, is_last_session) VALUES (0, 'Zero', 1, 0)",

        // Names of 3000 / 800 characters outside of the BMP: SQL counts characters (within
        // its limit), Qt UTF-16 code units (over the limit of the reader) - the name of the path is used
        "INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('/hostile/astral.pdf', replace(hex(zeroblob(1500)), '0', char(128512)), '/hostile', 6)",
        "INSERT INTO pinned_location(path, name, kind, position) VALUES ('/hostile/astral', replace(hex(zeroblob(400)), '0', char(128512)), 0, 1)",

        // A page after the limit: not in progress (Continue where you left off skips it)
        "INSERT INTO recent_file(path, name, folder, last_opened, last_page) VALUES ('/hostile/far.pdf', 'far.pdf', '/hostile', 7, 99999999999)",

        // A last session without documents is no last session
        "INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('', 1, NULL, 1, 1)",
    };
    for (const char* sql : statements)
    {
        QVERIFY2(AppDataTestHelper::execute(database, sql), sql);
    }

    // Readers skip the invalid rows and replace invalid values
    const std::vector<pdf::PDFRecentFile> files = fixture.recent().getFiles();
    QStringList paths = AppDataTestHelper::getPaths(files);
    QVERIFY(paths.contains(valid));
    QVERIFY(paths.contains(QStringLiteral("/hostile/types.pdf")));
    QVERIFY(paths.contains(QStringLiteral("/hostile/huge.pdf")));
    QVERIFY(paths.contains(QStringLiteral("/hostile/astral.pdf")));
    QVERIFY(paths.contains(QStringLiteral("/hostile/far.pdf")));
    QCOMPARE(paths.size(), 5);
    QVERIFY(!fixture.recent().getFile(QStringLiteral("/hostile/zero.pdf")));
    QCOMPARE(fixture.recent().getFile(QStringLiteral("/hostile/astral.pdf"))->name, QStringLiteral("astral.pdf"));
    QCOMPARE(fixture.recent().getFile(QStringLiteral("/hostile/far.pdf"))->getReadingState(), pdf::PDFRecentReadingState::NotStarted);
    QVERIFY(fixture.recent().getContinueReading().empty());
    QVERIFY(!fixture.workspaces().getLastSession());

    const std::optional<pdf::PDFRecentFile> types = fixture.recent().getFile(QStringLiteral("/hostile/types.pdf"));
    QVERIFY(types);
    QCOMPARE(types->name, QStringLiteral("types.pdf"));
    QCOMPARE(types->folder, QStringLiteral("/hostile"));
    QCOMPARE(types->lastOpened, qint64(0));
    QCOMPARE(types->openCount, qint64(0));
    QVERIFY(!types->pinned);
    QCOMPARE(types->pageCount, -1);
    QCOMPARE(types->fileSize, qint64(-1));
    QCOMPARE(types->lastPage, 0);
    QVERIFY(!types->reachedEnd);
    QVERIFY(!types->locked);
    QCOMPARE(types->thumbnailState, pdf::PDFRecentThumbnailState::Pending);
    QVERIFY(types->viewState.isEmpty());

    const std::optional<pdf::PDFRecentFile> huge = fixture.recent().getFile(QStringLiteral("/hostile/huge.pdf"));
    QCOMPARE(huge->pageCount, -1);
    QCOMPARE(huge->lastPage, 0);

    // Writes survive the hostile rows: open_count restarts, pin goes after them
    QVERIFY(!fixture.recent(200).recordOpened(valid).isError());
    QCOMPARE(fixture.recent().getFile(valid)->openCount, qint64(2));
    QVERIFY(AppDataTestHelper::execute(database, "UPDATE recent_file SET open_count = 'many'"));
    QVERIFY(!fixture.recent(300).recordOpened(valid).isError());
    QCOMPARE(fixture.recent().getFile(valid)->openCount, qint64(1));
    QVERIFY(!fixture.recent().setPinned(valid, true).isError());

    QCOMPARE(fixture.recent().getUsedFolders().size(), size_t(2));
    const std::vector<pdf::PDFPinnedLocation> locations = fixture.recent().getPinnedLocations();
    QCOMPARE(int(locations.size()), 2);
    QCOMPARE(locations[0].path, QStringLiteral("/hostile/astral"));
    QCOMPARE(locations[0].name, QStringLiteral("astral"));
    QCOMPARE(locations[1].name, QStringLiteral("location"));
    QCOMPARE(locations[1].kind, pdf::PDFPinnedLocationKind::Local);

    // Workspaces: the hostile one has a generated name and only its valid document
    const std::vector<pdf::PDFWorkspace> workspaces = fixture.workspaces().getWorkspaces(true);
    QCOMPARE(int(workspaces.size()), 1);
    QVERIFY(workspaces.front().name.startsWith(QStringLiteral("Workspace ")));
    QCOMPARE(int(workspaces.front().documents.size()), 1);
    QCOMPARE(workspaces.front().documents.front().page, 0);
    QCOMPARE(workspaces.front().opened, qint64(0));

    // Tools: unknown ids skipped
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.merge") }));
    const std::vector<pdf::PDFToolUsage> usage = fixture.tools().getUsage();
    QCOMPARE(int(usage.size()), 1);
    QCOMPARE(usage.front().toolId, QStringLiteral("optimize.compress"));
    QCOMPARE(usage.front().count, qint64(0));
    QVERIFY(!fixture.tools().recordUsage(QStringLiteral("optimize.compress")).isError());
    QCOMPARE(fixture.tools().getUsage().front().count, qint64(1));

    // Clear history works over the hostile rows
    QVERIFY(!fixture.recent().clearHistory(true).isError());
    QVERIFY(fixture.recent().getFiles().empty());
}

void AppDataTest::workspaces()
{
    DataFixture fixture;
    const QString first = fixture.createFile(QStringLiteral("w/one.pdf"));
    const QString second = fixture.createFile(QStringLiteral("w/two.pdf"));
    const std::vector<pdf::PDFWorkspaceDocument> documents = { { first, 12 }, { second, 0 } };

    qint64 id = 0;
    bool isDuplicate = true;
    QVERIFY(!fixture.workspaces(1000).saveWorkspace(QStringLiteral(" Thesis review "), documents, false, &id, &isDuplicate).isError());
    QVERIFY(id > 0);
    QVERIFY(!isDuplicate);

    std::optional<pdf::PDFWorkspace> workspace = fixture.workspaces().getWorkspace(id);
    QVERIFY(workspace);
    QCOMPARE(workspace->name, QStringLiteral("Thesis review"));
    QCOMPARE(workspace->created, qint64(1000));
    QCOMPARE(workspace->saved, qint64(1000));
    QCOMPARE(workspace->opened, qint64(0));
    QVERIFY(!workspace->isLastSession);
    QCOMPARE(workspace->documentCount, 2);
    QVERIFY(workspace->documents == documents);

    // Duplicate name (case insensitive, also non-ASCII): refused, then replaced
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("THESIS REVIEW"), { { second, 1 } }, false, nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);
    QCOMPARE(fixture.workspaces().findWorkspace(QStringLiteral("thesis review"))->documentCount, 2);
    qint64 replacedId = 0;
    QVERIFY(!fixture.workspaces(2000).saveWorkspace(QStringLiteral("THESIS REVIEW"), { { second, 1 } }, true, &replacedId).isError());
    QCOMPARE(replacedId, id);
    workspace = fixture.workspaces().getWorkspace(id);
    QCOMPARE(workspace->name, QStringLiteral("THESIS REVIEW"));
    QCOMPARE(workspace->created, qint64(1000));
    QCOMPARE(workspace->saved, qint64(2000));
    QCOMPARE(workspace->documentCount, 1);
    QCOMPARE(int(fixture.workspaces().getWorkspaces().size()), 1);

    qint64 czechId = 0;
    QVERIFY(!fixture.workspaces(3000).saveWorkspace(QString::fromUtf8("\u010Cervenec"), documents, false, &czechId).isError());
    QVERIFY(fixture.workspaces().saveWorkspace(QString::fromUtf8("\u010Dervenec"), documents, false, nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);

    // Invalid input
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("  "), documents, false).isError());
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("Empty"), {}, false).isError());
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("Relative"), { { QStringLiteral("a.pdf"), 1 } }, false).isError());
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("Page"), { { first, -1 } }, false).isError());
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("Page"), { { first, pdf::PDFRecentRepository::MAXIMUM_PAGE + 1 } }, false).isError());
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("thesis REVIEW"), documents, false).isError());
    QVERIFY(!fixture.workspaces().findWorkspace(QStringLiteral("Page")));
    std::vector<pdf::PDFWorkspaceDocument> tooMany(size_t(pdf::PDFWorkspaceRepository::MAXIMUM_DOCUMENTS + 1), pdf::PDFWorkspaceDocument{ first, 1 });
    QVERIFY(fixture.workspaces().saveWorkspace(QStringLiteral("Many"), tooMany, false).isError());
    tooMany.pop_back();
    QVERIFY(!fixture.workspaces(4000).saveWorkspace(QStringLiteral("Many"), tooMany, false).isError());
    QCOMPARE(fixture.workspaces().findWorkspace(QStringLiteral("many"))->documentCount, pdf::PDFWorkspaceRepository::MAXIMUM_DOCUMENTS);

    // Rename: the duplicate of another workspace is refused, its own name in other case is not
    QVERIFY(fixture.workspaces().renameWorkspace(id, QString::fromUtf8("\u010CERVENEC"), &isDuplicate).isError());
    QVERIFY(isDuplicate);
    QVERIFY(fixture.workspaces().renameWorkspace(id, QString::fromUtf8("\u010Dervenec")).isError());
    QVERIFY(!fixture.workspaces().renameWorkspace(id, QStringLiteral("Thesis review"), &isDuplicate).isError());
    QVERIFY(!isDuplicate);
    QVERIFY(!fixture.workspaces().renameWorkspace(id, QString::fromUtf8("Thesis review \u2013 final")).isError());
    QCOMPARE(fixture.workspaces().getWorkspace(id)->name, QString::fromUtf8("Thesis review \u2013 final"));
    QVERIFY(fixture.workspaces().renameWorkspace(id, QString()).isError());

    // Opening: Recent and the order (most recently opened or saved first)
    QVERIFY(fixture.workspaces().getRecentWorkspaces().empty());
    QVERIFY(!fixture.workspaces(9000).markOpened(id).isError());
    QVERIFY(fixture.workspaces().markOpened(987654).isError());
    const std::vector<pdf::PDFWorkspace> recent = fixture.workspaces().getRecentWorkspaces();
    QCOMPARE(int(recent.size()), 1);
    QCOMPARE(recent.front().id, id);
    QCOMPARE(recent.front().opened, qint64(9000));
    QCOMPARE(fixture.workspaces().getWorkspaces().front().id, id);

    // Last session: not a saved workspace, replaced by each save
    QVERIFY(!fixture.workspaces().getLastSession());
    QVERIFY(!fixture.workspaces(10000).saveLastSession(documents).isError());
    QVERIFY(!fixture.workspaces(11000).saveLastSession({ { second, 3 } }).isError());
    std::optional<pdf::PDFWorkspace> lastSession = fixture.workspaces().getLastSession();
    QVERIFY(lastSession);
    QVERIFY(lastSession->isLastSession);
    QVERIFY(lastSession->name.isEmpty());
    QCOMPARE(lastSession->saved, qint64(11000));
    QCOMPARE(lastSession->documentCount, 1);
    QCOMPARE(lastSession->documents.front().page, 3);
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM workspace WHERE is_last_session = 1"), qint64(1));
    for (const pdf::PDFWorkspace& item : fixture.workspaces().getWorkspaces())
    {
        QVERIFY(!item.isLastSession);
    }
    QVERIFY(fixture.workspaces().markOpened(lastSession->id).isError());
    QVERIFY(!fixture.workspaces().renameWorkspace(lastSession->id, QStringLiteral("x")).isError());
    QVERIFY(fixture.workspaces().getLastSession()->name.isEmpty());
    QVERIFY(!fixture.workspaces().deleteLastSession().isError());
    QVERIFY(!fixture.workspaces().getLastSession());
    tooMany.resize(size_t(pdf::PDFWorkspaceRepository::MAXIMUM_DOCUMENTS + 1), pdf::PDFWorkspaceDocument{ first, 1 });
    QVERIFY(fixture.workspaces().saveLastSession(tooMany).isError());

    // Delete: the documents go with it
    QVERIFY(!fixture.workspaces().deleteWorkspace(id).isError());
    QVERIFY(!fixture.workspaces().getWorkspace(id));
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM workspace_document WHERE workspace_id NOT IN (SELECT id FROM workspace)"), qint64(0));
    QVERIFY(!fixture.workspaces().deleteWorkspace(id).isError());
    QVERIFY(!fixture.workspaces().getWorkspace(0));
    QVERIFY(!fixture.workspaces().findWorkspace(QString()));

    // No database: nothing is read, writes fail
    pdf::PDFWorkspaceRepository nullRepository(nullptr);
    QVERIFY(nullRepository.getWorkspaces().empty());
    QVERIFY(nullRepository.saveWorkspace(QStringLiteral("W"), documents, false).isError());
}

void AppDataTest::tools()
{
    DataFixture fixture;
    QVERIFY(fixture.tools().getPinnedTools().isEmpty());
    QVERIFY(fixture.tools().getUsage().empty());

    QVERIFY(!fixture.tools().setPinned(QStringLiteral("optimize.compress"), true).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("review.compare"), true).isError());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("optimize.compress"), QStringLiteral("pages.merge"), QStringLiteral("review.compare") }));
    QVERIFY(fixture.tools().isPinned(QStringLiteral("pages.merge")));
    QVERIFY(!fixture.tools().isPinned(QStringLiteral("pages.split")));

    QVERIFY(fixture.tools().setPinned(QStringLiteral("unknown.tool"), true).isError());
    QVERIFY(fixture.tools().setPinned(QString(), true).isError());

    QVERIFY(!fixture.tools().setPinnedOrder({ QStringLiteral("review.compare"), QStringLiteral("unknown"), QStringLiteral("pages.split") }).isError());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("review.compare"), QStringLiteral("optimize.compress"), QStringLiteral("pages.merge") }));

    QVERIFY(!fixture.tools().setPinned(QStringLiteral("optimize.compress"), false).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("optimize.compress"), false).isError());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("review.compare"), QStringLiteral("pages.merge") }));

    // Usage
    QVERIFY(!fixture.tools(100).recordUsage(QStringLiteral("pages.merge")).isError());
    QVERIFY(!fixture.tools(300).recordUsage(QStringLiteral("optimize.compress")).isError());
    QVERIFY(!fixture.tools(200).recordUsage(QStringLiteral("pages.merge")).isError());
    QVERIFY(fixture.tools().recordUsage(QStringLiteral("pages.unknown")).isError());

    const std::vector<pdf::PDFToolUsage> usage = fixture.tools().getUsage();
    QCOMPARE(int(usage.size()), 2);
    QCOMPARE(usage[0].toolId, QStringLiteral("optimize.compress"));
    QCOMPARE(usage[0].lastUsed, qint64(300));
    QCOMPARE(usage[0].count, qint64(1));
    QCOMPARE(usage[1].toolId, QStringLiteral("pages.merge"));
    QCOMPARE(usage[1].lastUsed, qint64(200));
    QCOMPARE(usage[1].count, qint64(2));
    QCOMPARE(int(fixture.tools().getUsage(1).size()), 1);
    QVERIFY(fixture.tools().getUsage(0).empty());
    QCOMPARE(fixture.tools().getUsedTools(), QStringList({ QStringLiteral("optimize.compress"), QStringLiteral("pages.merge") }));

    // A limit, which is reached after more than one tool
    QVERIFY(!fixture.tools(400).recordUsage(QStringLiteral("review.compare")).isError());
    const std::vector<pdf::PDFToolUsage> limitedUsage = fixture.tools().getUsage(2);
    QCOMPARE(int(limitedUsage.size()), 2);
    QCOMPARE(limitedUsage[0].toolId, QStringLiteral("review.compare"));
    QCOMPARE(limitedUsage[1].toolId, QStringLiteral("optimize.compress"));

    // A duplicate in the requested order is used once (at its first place)
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("review.compare"), QStringLiteral("pages.merge") }));
    QVERIFY(!fixture.tools().setPinnedOrder({ QStringLiteral("pages.merge"), QStringLiteral("review.compare"), QStringLiteral("pages.merge") }).isError());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.merge"), QStringLiteral("review.compare") }));

    // No database
    pdf::PDFToolRepository nullRepository(nullptr);
    QVERIFY(nullRepository.getPinnedTools().isEmpty());
    QVERIFY(nullRepository.setPinned(QStringLiteral("pages.merge"), true).isError());
}

void AppDataTest::schemaMigration()
{
    // A recent file written without the column thumbnail_state (as by an older
    // writer, which does not know it) has the default state Pending
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("old.db"));
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, &error);
        QVERIFY(handle);
        QVERIFY(!pdf::PDFDatabaseSchema::migrate(handle, pdf::PDFDatabaseSchema::getMigrations(), pdf::PDFDatabaseSchema::getCurrentVersion()).isError());
        QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT INTO recent_file(path, name, folder, last_opened, thumbnail, thumbnail_mtime) VALUES ('/old/a.pdf', 'a.pdf', '/old', 5, x'00', 7)").isError());
        pdf::PDFDatabase::closeHandle(handle);
    }

    pdf::PDFDatabase database;
    database.open(AppDataTestHelper::createFileOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(database.getState().fileSchemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());

    pdf::PDFRecentRepository repository(&database, nullptr);
    const std::optional<pdf::PDFRecentFile> file = repository.getFile(QStringLiteral("/old/a.pdf"));
    QVERIFY(file);
    QCOMPARE(file->thumbnailState, pdf::PDFRecentThumbnailState::Pending);
    QCOMPARE(file->thumbnailModified, qint64(7));
    QVERIFY(repository.getThumbnail(QStringLiteral("/old/a.pdf")).png.isEmpty());
    database.close();

    // Invalid lists of migrations and a migration, whose COMMIT fails (a deferred
    // foreign key): nothing of it stays, the version is not changed
    std::vector<pdf::PDFDatabaseMigration> migrations(1);
    migrations[0].version = 1;
    QVERIFY(!pdf::PDFDatabaseSchema::isValid(migrations));
    migrations[0].sql = "CREATE TABLE parent(id INTEGER PRIMARY KEY); "
                        "CREATE TABLE child(parent INTEGER REFERENCES parent(id) DEFERRABLE INITIALLY DEFERRED); "
                        "INSERT INTO child VALUES (7);";
    QVERIFY(pdf::PDFDatabaseSchema::isValid(migrations));
    QCOMPARE(pdf::PDFDatabaseSchema::readVersion(nullptr), -1);

    pdf::PDFDatabaseError error;
    sqlite3* handle = pdf::PDFDatabase::openFileConnection(directory.filePath(QStringLiteral("commit.db")), false, 5000, &error);
    QVERIFY(handle);
    std::vector<pdf::PDFDatabaseMigration> invalidMigrations = migrations;
    invalidMigrations[0].sql.clear();
    QVERIFY(pdf::PDFDatabaseSchema::migrate(handle, invalidMigrations, 1).isError());
    QVERIFY(pdf::PDFDatabaseSchema::migrate(handle, migrations, -1).isError());
    QVERIFY(pdf::PDFDatabaseSchema::migrate(handle, migrations, 2).isError());
    QCOMPARE(pdf::PDFDatabaseSchema::readVersion(handle), 0);
    QVERIFY(pdf::PDFDatabaseSchema::migrate(handle, migrations, 1).isError());
    QCOMPARE(pdf::PDFDatabaseSchema::readVersion(handle), 0);
    QCOMPARE(pdf::PDFDatabase::queryIntegerOnHandle(handle, "SELECT COUNT(*) FROM sqlite_schema WHERE name IN ('parent', 'child')", -1), qint64(0));

    // The same migration without the violation is applied
    migrations[0].sql = "CREATE TABLE parent(id INTEGER PRIMARY KEY);";
    QVERIFY(!pdf::PDFDatabaseSchema::migrate(handle, migrations, 1).isError());
    QCOMPARE(pdf::PDFDatabaseSchema::readVersion(handle), 1);
    pdf::PDFDatabase::closeHandle(handle);
}

void AppDataTest::notificationsOfAnotherConnection()
{
    // Two databases on one file = two instances: the writes of one are notified to the other
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    pdf::PDFDatabase writer;
    writer.open(AppDataTestHelper::createFileOptions(databasePath));
    pdf::PDFDatabase reader;
    reader.open(AppDataTestHelper::createFileOptions(databasePath));
    QCOMPARE(reader.getState().mode, pdf::PDFDatabaseStorageMode::Disk);

    pdf::PDFDatabaseNotifier notifier(&reader);
    notifier.setPollInterval(20);
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);

    const QString filePath = directory.filePath(QStringLiteral("a.pdf"));
    QVERIFY(AppDataTestHelper::writeFile(filePath, "x"));
    const QString path = pdf::PDFRepositoryHelper::normalizePath(filePath);

    pdf::PDFRecentRepository recent(&writer, nullptr);
    pdf::PDFWorkspaceRepository workspaces(&writer);
    pdf::PDFToolRepository tools(&writer);
    QVERIFY(!recent.recordOpened(path).isError());
    QVERIFY(!recent.addPinnedLocation(directory.path(), QString()).isError());
    qint64 id = 0;
    QVERIFY(!workspaces.saveWorkspace(QStringLiteral("W"), { { path, 1 } }, false, &id).isError());
    QVERIFY(!tools.setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!tools.recordUsage(QStringLiteral("pages.merge")).isError());

    const int timeout = 5000 * AppDataTestHelper::getTimeoutFactor();
    const QList<QPair<QString, QString>> expected =
    {
        { pdf::PDFRecentRepository::getFileTopic(), path },
        { pdf::PDFRecentRepository::getFolderTopic(), pdf::PDFRepositoryHelper::getFolder(path) },
        { pdf::PDFRecentRepository::getPinnedLocationTopic(), QString() },
        { pdf::PDFWorkspaceRepository::getWorkspaceTopic(), QString::number(id) },
        { pdf::PDFWorkspaceRepository::getDocumentTopic(), QString::number(id) },
        { pdf::PDFToolRepository::getPinTopic(), QStringLiteral("pages.merge") },
        { pdf::PDFToolRepository::getUsageTopic(), QStringLiteral("pages.merge") },
    };

    for (const auto& item : expected)
    {
        bool found = false;
        QElapsedTimer timer;
        timer.start();
        while (!found && timer.elapsed() < timeout)
        {
            for (const QList<QVariant>& arguments : std::as_const(spy))
            {
                found = found || (arguments.at(0).toString() == item.first && (item.second.isEmpty() || arguments.at(1).toString() == item.second));
            }
            if (!found)
            {
                QTest::qWait(20);
            }
        }
        QVERIFY2(found, qPrintable(item.first + QLatin1Char(' ') + item.second));
    }

    // The reader sees the data
    QCOMPARE(pdf::PDFRecentRepository(&reader, nullptr).getFileCount(), 1);
    QCOMPARE(pdf::PDFToolRepository(&reader).getPinnedTools(), QStringList({ QStringLiteral("pages.merge") }));

    writer.close();
    reader.close();
}

void AppDataTest::notificationsOfAnotherProcess()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    pdf::PDFDatabase database;
    database.open(AppDataTestHelper::createFileOptions(databasePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);

    pdf::PDFDatabaseNotifier notifier(&database);
    notifier.setPollInterval(20);
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);

    const QString filePath = directory.filePath(QStringLiteral("other process.pdf"));
    QVERIFY(AppDataTestHelper::writeFile(filePath, "x"));
    const QString path = pdf::PDFRepositoryHelper::normalizePath(filePath);

    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-record"), databasePath, filePath });
    const int factor = AppDataTestHelper::getTimeoutFactor();
    QVERIFY(process.waitForFinished(30000 * factor));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty() && spy.last().at(0).toString() == pdf::PDFRecentRepository::getFolderTopic(), 5000 * factor);
    bool fileNotified = false;
    for (const QList<QVariant>& arguments : std::as_const(spy))
    {
        fileNotified = fileNotified || (arguments.at(0).toString() == pdf::PDFRecentRepository::getFileTopic() && arguments.at(1).toString() == path);
    }
    QVERIFY(fileNotified);
    QCOMPARE(pdf::PDFRecentRepository(&database, nullptr).getFile(path)->name, QStringLiteral("other process.pdf"));
    database.close();
}

void AppDataTest::thumbnailCreate_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<int>("outcome");
    QTest::addColumn<int>("state");
    QTest::addColumn<int>("pageCount");
    QTest::addColumn<QSize>("size");

    const int done = int(pdf::PDFAppThumbnailResult::Outcome::Done);
    const int ready = int(pdf::PDFRecentThumbnailState::Ready);
    const int failed = int(pdf::PDFRecentThumbnailState::Failed);
    QTest::newRow("portrait") << "portrait" << done << ready << 3 << QSize(240, 320);
    QTest::newRow("landscape") << "landscape" << done << ready << 1 << QSize(320, 160);
    QTest::newRow("rotated") << "rotated" << done << ready << 1 << QSize(320, 240);
    QTest::newRow("huge page") << "huge" << done << ready << 1 << QSize(320, 320);
    QTest::newRow("tiny page") << "tiny" << done << ready << 1 << QSize(320, 160);
    QTest::newRow("encrypted") << "encrypted" << done << int(pdf::PDFRecentThumbnailState::PasswordProtected) << -1 << QSize();
    QTest::newRow("owner password only") << "owner" << done << ready << 1 << QSize(240, 320);
    QTest::newRow("damaged") << "damaged" << done << failed << -1 << QSize();
    QTest::newRow("truncated") << "truncated" << done << -1 << -1 << QSize();
    QTest::newRow("empty file") << "empty" << done << failed << -1 << QSize();
    QTest::newRow("no pages") << "nopages" << done << failed << 0 << QSize();
    QTest::newRow("too large") << "toolarge" << done << failed << -1 << QSize();
    QTest::newRow("missing") << "missing" << int(pdf::PDFAppThumbnailResult::Outcome::Missing) << failed << -1 << QSize();
    QTest::newRow("folder") << "folder" << int(pdf::PDFAppThumbnailResult::Outcome::Missing) << failed << -1 << QSize();
    QTest::newRow("canceled") << "canceled" << int(pdf::PDFAppThumbnailResult::Outcome::Canceled) << failed << -1 << QSize();
    QTest::newRow("canceled while rendering") << "rendercanceled" << int(pdf::PDFAppThumbnailResult::Outcome::Canceled) << failed << -1 << QSize();
    QTest::newRow("page without area") << "zero" << done << failed << 1 << QSize();
    QTest::newRow("page without height") << "flat" << done << failed << 1 << QSize();
    QTest::newRow("canceled after reading") << "readcanceled" << int(pdf::PDFAppThumbnailResult::Outcome::Canceled) << failed << -1 << QSize();
}

/// Cancellation, which is always set
class AppDataCanceledControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// Cancellation, which is set from the n-th question on (createThumbnail asks twice
/// before and after the reading of the document, the rendering asks later)
class AppDataCountingControl : public pdf::PDFOperationControl
{
public:
    explicit AppDataCountingControl(int firstCanceled) :
        m_firstCanceled(firstCanceled)
    {

    }

    virtual bool isOperationCancelled() const override { return m_count.fetch_add(1) + 1 >= m_firstCanceled; }
    int getCount() const { return m_count.load(); }

private:
    int m_firstCanceled;
    mutable std::atomic_int m_count = 0;
};

void AppDataTest::thumbnailCreate()
{
    QFETCH(QString, kind);
    QFETCH(int, outcome);
    QFETCH(int, state);
    QFETCH(int, pageCount);
    QFETCH(QSize, size);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("document.pdf"));
    pdf::PDFAppThumbnailerOptions options;

    if (kind == QLatin1String("portrait") || kind == QLatin1String("canceled") || kind == QLatin1String("rendercanceled") || kind == QLatin1String("readcanceled"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 3));
    }
    else if (kind == QLatin1String("zero") || kind == QLatin1String("flat"))
    {
        // A page with an empty media box and no content
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(kind == QLatin1String("zero") ? QRectF(0, 0, 0, 0) : QRectF(0, 0, 100, 0));
        pdf::PDFDocument document = builder.build();
        QVERIFY(bool(pdf::PDFDocumentWriter(nullptr).write(path, &document, false)));
    }
    else if (kind == QLatin1String("landscape"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1, QSizeF(800, 400)));
    }
    else if (kind == QLatin1String("rotated"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1, QSizeF(300, 400), pdf::PageRotation::Rotate90));
    }
    else if (kind == QLatin1String("huge"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1, QSizeF(14400, 14400)));
    }
    else if (kind == QLatin1String("tiny"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1, QSizeF(2, 1)));
    }
    else if (kind == QLatin1String("encrypted"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1, QSizeF(300, 400), pdf::PageRotation::None, QStringLiteral("secret")));
    }
    else if (kind == QLatin1String("owner"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1, QSizeF(300, 400), pdf::PageRotation::None, QString(), QStringLiteral("owner")));
    }
    else if (kind == QLatin1String("damaged"))
    {
        QByteArray garbage("%PDF-1.7\n");
        for (int i = 0; i < 5000; ++i)
        {
            garbage.append(char((i * 7919) % 251));
        }
        QVERIFY(AppDataTestHelper::writeFile(path, garbage));
    }
    else if (kind == QLatin1String("truncated"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 2));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(file.size() / 3));
    }
    else if (kind == QLatin1String("empty"))
    {
        QVERIFY(AppDataTestHelper::writeFile(path, QByteArray()));
    }
    else if (kind == QLatin1String("nopages"))
    {
        QVERIFY(AppDataTestHelper::createEmptyPdf(path));
    }
    else if (kind == QLatin1String("toolarge"))
    {
        QVERIFY(AppDataTestHelper::createPdf(path, 1));
        options.maximumFileSize = 100;
    }
    else if (kind == QLatin1String("folder"))
    {
        QVERIFY(QDir().mkpath(path));
    }

    AppDataCanceledControl canceled;
    AppDataCountingControl canceledWhileRendering(3);
    AppDataCountingControl canceledAfterReading(2);
    const pdf::PDFOperationControl* control = nullptr;
    if (kind == QLatin1String("canceled"))
    {
        control = &canceled;
    }
    else if (kind == QLatin1String("rendercanceled"))
    {
        control = &canceledWhileRendering;
    }
    else if (kind == QLatin1String("readcanceled"))
    {
        control = &canceledAfterReading;
    }
    const pdf::PDFAppThumbnailResult result = pdf::PDFAppThumbnailer::createThumbnail(path, options, control);
    QCOMPARE(int(result.outcome), outcome);
    if (kind == QLatin1String("rendercanceled"))
    {
        // The document was read, the rendering was canceled
        QVERIFY(canceledWhileRendering.getCount() >= 3);
    }
    if (result.outcome != pdf::PDFAppThumbnailResult::Outcome::Done)
    {
        QVERIFY(result.png.isEmpty());
        return;
    }

    if (state < 0)
    {
        // A truncated file may be recovered by the permissive reader or not, it never crashes
        return;
    }

    QCOMPARE(int(result.state), state);
    QCOMPARE(result.pageCount, pageCount);
    QCOMPARE(result.fileModified, QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch());

    if (result.state != pdf::PDFRecentThumbnailState::Ready)
    {
        QVERIFY(result.png.isEmpty());
        return;
    }

    pdf::PDFRecentThumbnail thumbnail;
    thumbnail.png = result.png;
    thumbnail.state = result.state;
    const QImage image = thumbnail.toImage();
    QCOMPARE(image.size(), size);
    QVERIFY(result.png.size() <= pdf::PDFRecentRepository::MAXIMUM_THUMBNAIL_BYTES);

    // Page 1 has a red upper left quarter (rotated: the quarter moves to the upper right)
    const QPoint redPoint = kind == QLatin1String("rotated") ? QPoint(image.width() * 3 / 4, image.height() / 4) : QPoint(image.width() / 4, image.height() / 4);
    const QPoint whitePoint = kind == QLatin1String("rotated") ? QPoint(image.width() / 4, image.height() * 3 / 4) : QPoint(image.width() * 3 / 4, image.height() * 3 / 4);
    if (kind != QLatin1String("tiny"))
    {
        const QColor red = image.pixelColor(redPoint);
        QVERIFY2(red.red() > 200 && red.green() < 60 && red.blue() < 60, qPrintable(red.name()));
        const QColor white = image.pixelColor(whitePoint);
        QVERIFY2(white.red() > 240 && white.green() > 240 && white.blue() > 240, qPrintable(white.name()));
    }

    // Invalid inputs of the renderer
    QVERIFY(pdf::PDFAppThumbnailer::renderFirstPage(nullptr, 320, nullptr).isNull());
    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
    const pdf::PDFDocument document = reader.readFromFile(path);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(pdf::PDFAppThumbnailer::renderFirstPage(&document, 0, nullptr).isNull(), true);
    QCOMPARE(pdf::PDFAppThumbnailer::renderFirstPage(&document, 16, nullptr).size().width() <= 16, true);
}

/// Fixture of the tests of the thumbnailer: a memory database and a recorded PDF
class ThumbnailFixture : public DataFixture
{
public:
    ThumbnailFixture() :
        thumbnailer(std::make_unique<pdf::PDFAppThumbnailer>(database.get())),
        notifier(std::make_unique<pdf::PDFDatabaseNotifier>(database.get()))
    {
        notifier->setPollInterval(20);
    }

    ~ThumbnailFixture()
    {
        thumbnailer.reset();
        notifier.reset();
    }

    QString createRecordedPdf(const QString& name, int pages = 1)
    {
        const QString filePath = directory.filePath(name);
        if (!AppDataTestHelper::createPdf(filePath, pages))
        {
            return QString();
        }
        const QString path = pdf::PDFRepositoryHelper::normalizePath(filePath);
        return recent().recordOpened(path).isError() ? QString() : path;
    }

    std::unique_ptr<pdf::PDFAppThumbnailer> thumbnailer;
    std::unique_ptr<pdf::PDFDatabaseNotifier> notifier;
};

void AppDataTest::thumbnailerService()
{
    ThumbnailFixture fixture;
    const int timeout = 30000 * AppDataTestHelper::getTimeoutFactor();
    const QString path = fixture.createRecordedPdf(QStringLiteral("report.pdf"), 4);
    QVERIFY(!path.isEmpty());

    QSignalSpy finished(fixture.thumbnailer.get(), &pdf::PDFAppThumbnailer::thumbnailFinished);
    QSignalSpy changed(fixture.notifier.get(), &pdf::PDFDatabaseNotifier::changed);

    // Two requests of the same file: one result
    QVERIFY(fixture.thumbnailer->isIdle());
    fixture.thumbnailer->requestThumbnail(path);
    fixture.thumbnailer->requestThumbnail(path);
    QVERIFY(fixture.thumbnailer->getPendingCount() <= 1);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeout);
    QCOMPARE(finished.first().at(0).toString(), path);
    QCOMPARE(finished.first().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Ready);
    QVERIFY(finished.first().at(2).toBool());
    QVERIFY(fixture.thumbnailer->isIdle());

    pdf::PDFRecentThumbnail thumbnail = fixture.recent().getThumbnail(path);
    QCOMPARE(thumbnail.state, pdf::PDFRecentThumbnailState::Ready);
    QCOMPARE(thumbnail.toImage().size(), QSize(240, 320));
    QCOMPARE(thumbnail.fileModified, QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
    QCOMPARE(fixture.recent().getFile(path)->pageCount, 4);

    // The page is notified by the database
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), timeout);
    QCOMPARE(changed.last().at(0).toString(), pdf::PDFRecentRepository::getFileTopic());
    QCOMPARE(changed.last().at(1).toString(), path);

    // Up to date: nothing is written again
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, timeout);
    QVERIFY(!finished.last().at(2).toBool());

    // The file changes: the thumbnail is made again
    QVERIFY(AppDataTestHelper::createPdf(path, 2));
    QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(60)));
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, timeout);
    QVERIFY(finished.last().at(2).toBool());
    QCOMPARE(fixture.recent().getFile(path)->pageCount, 2);
    QCOMPARE(fixture.recent().getThumbnail(path).fileModified, QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch());

    // An encrypted document: PasswordProtected once, then no retry (not written again)
    const QString encryptedFile = fixture.directory.filePath(QStringLiteral("secret.pdf"));
    QVERIFY(AppDataTestHelper::createPdf(encryptedFile, 1, QSizeF(300, 400), pdf::PageRotation::None, QStringLiteral("secret")));
    const QString encrypted = pdf::PDFRepositoryHelper::normalizePath(encryptedFile);
    QVERIFY(!fixture.recent().recordOpened(encrypted).isError());
    fixture.thumbnailer->requestThumbnail(encrypted);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, timeout);
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::PasswordProtected);
    QVERIFY(finished.last().at(2).toBool());
    QCOMPARE(fixture.recent().getFile(encrypted)->thumbnailState, pdf::PDFRecentThumbnailState::PasswordProtected);
    fixture.thumbnailer->requestThumbnail(encrypted);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 5, timeout);
    QVERIFY(!finished.last().at(2).toBool());

    // A damaged file: Failed once, no retry loop
    const QString damaged = fixture.createFile(QStringLiteral("damaged.pdf"), QByteArray("%PDF-1.7\nnot a pdf"));
    QVERIFY(!fixture.recent().recordOpened(damaged).isError());
    fixture.thumbnailer->requestThumbnail(damaged);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 6, timeout);
    QCOMPARE(fixture.recent().getFile(damaged)->thumbnailState, pdf::PDFRecentThumbnailState::Failed);
    fixture.thumbnailer->requestThumbnail(damaged);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 7, timeout);
    QVERIFY(!finished.last().at(2).toBool());

    // A missing file and a file, which is not recorded: nothing is written
    QVERIFY(QFile::remove(damaged));
    fixture.thumbnailer->requestThumbnail(damaged);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 8, timeout);
    QVERIFY(!finished.last().at(2).toBool());
    QCOMPARE(fixture.recent().getFile(damaged)->thumbnailState, pdf::PDFRecentThumbnailState::Failed);
    const QString other = pdf::PDFRepositoryHelper::normalizePath(fixture.directory.filePath(QStringLiteral("other.pdf")));
    QVERIFY(AppDataTestHelper::createPdf(other, 1));
    fixture.thumbnailer->requestThumbnail(other);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 9, timeout);
    QVERIFY(!finished.last().at(2).toBool());
    QVERIFY(!fixture.recent().getFile(other));

    // A recorded path, which is a folder now: not a document, nothing is written
    const QString folderFile = fixture.directory.filePath(QStringLiteral("folder.pdf"));
    QVERIFY(QDir().mkpath(folderFile));
    QVERIFY(!fixture.recent().recordOpened(folderFile).isError());
    fixture.thumbnailer->requestThumbnail(pdf::PDFRepositoryHelper::normalizePath(folderFile));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 10, timeout);
    QVERIFY(!finished.last().at(2).toBool());
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Pending);
    QCOMPARE(fixture.recent().getFile(pdf::PDFRepositoryHelper::normalizePath(folderFile))->thumbnailState, pdf::PDFRecentThumbnailState::Pending);

    // Invalid paths are ignored
    fixture.thumbnailer->requestThumbnails({ QString(), QStringLiteral("relative.pdf") });
    QVERIFY(fixture.thumbnailer->isIdle());

    // After stop nothing is accepted
    fixture.thumbnailer->stop();
    fixture.thumbnailer->requestThumbnail(path);
    QVERIFY(fixture.thumbnailer->isIdle());
    fixture.thumbnailer->stop();

    // A thumbnailer without a database accepts nothing
    pdf::PDFAppThumbnailer withoutDatabase(nullptr);
    withoutDatabase.requestThumbnail(path);
    QVERIFY(withoutDatabase.isIdle());
}

void AppDataTest::thumbnailerTimeBudget()
{
    ThumbnailFixture fixture;
    pdf::PDFAppThumbnailerOptions options;
    options.timeBudget = 1;
    fixture.thumbnailer->setOptions(options);
    QCOMPARE(fixture.thumbnailer->getOptions().timeBudget, 1);

    // A page with many objects cannot be rendered in 1 ms: Failed, page count known
    const QString filePath = fixture.directory.filePath(QStringLiteral("slow.pdf"));
    QVERIFY(AppDataTestHelper::createPdf(filePath, 2, QSizeF(300, 400), pdf::PageRotation::None, QString(), QString(), 20000));
    const QString path = pdf::PDFRepositoryHelper::normalizePath(filePath);
    QVERIFY(!fixture.recent().recordOpened(path).isError());

    QSignalSpy finished(fixture.thumbnailer.get(), &pdf::PDFAppThumbnailer::thumbnailFinished);
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000 * AppDataTestHelper::getTimeoutFactor());
    QCOMPARE(finished.first().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Failed);
    const std::optional<pdf::PDFRecentFile> file = fixture.recent().getFile(path);
    QCOMPARE(file->thumbnailState, pdf::PDFRecentThumbnailState::Failed);
    QVERIFY(file->pageCount == 2 || file->pageCount == -1);

    // Time budget 0 = no limit: the same page is rendered (after a change of the file)
    options.timeBudget = 0;
    fixture.thumbnailer->setOptions(options);
    QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(60)));
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 60000 * AppDataTestHelper::getTimeoutFactor());
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Ready);
    QCOMPARE(fixture.recent().getFile(path)->pageCount, 2);

    // Options are limited
    options.maximumDimension = 100000;
    options.stopWaitTime = -5;
    fixture.thumbnailer->setOptions(options);
    QCOMPARE(fixture.thumbnailer->getOptions().maximumDimension, pdf::PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION);
    QCOMPARE(fixture.thumbnailer->getOptions().stopWaitTime, pdf::PDFAppThumbnailer::MINIMUM_STOP_WAIT_TIME);
    options.stopWaitTime = 5000;
    fixture.thumbnailer->setOptions(options);
}

void AppDataTest::thumbnailerStop_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("destroy thumbnailer") << "thumbnailer";
    QTest::newRow("stop") << "stop";
    QTest::newRow("close context") << "context";
    QTest::newRow("delete context") << "deleteContext";
}

void AppDataTest::thumbnailerStop()
{
    QFETCH(QString, mode);

    if (mode == QLatin1String("context") || mode == QLatin1String("deleteContext"))
    {
        // The context owns the thumbnailer: closing it during the work stops the
        // worker before the database is closed
        auto context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT"), QStringLiteral("--portable") }, QDir::currentPath()));
        context->open(AppDataTestHelper::createMemoryOptions());
        QVERIFY(context->getThumbnailer());
        QVERIFY(context->getFolderInfo());

        QTemporaryDir directory;
        QStringList paths;
        for (int i = 0; i < 5; ++i)
        {
            const QString filePath = directory.filePath(QStringLiteral("doc%1.pdf").arg(i));
            QVERIFY(AppDataTestHelper::createPdf(filePath, 1));
            paths << pdf::PDFRepositoryHelper::normalizePath(filePath);
            QVERIFY(!context->getRecent().recordOpened(paths.last()).isError());
        }

        QVERIFY(context->getWorkspaces().getWorkspaces().empty());
        QVERIFY(context->getTools().getPinnedTools().isEmpty());

        // The workers run (the thumbnailer has opened its connection to the database,
        // the folder information sleeps in its test delay), when the context is closed
        const int factor = AppDataTestHelper::getTimeoutFactor();
        const int connections = context->getDatabase()->getConnectionCount();
        pdf::PDFAppThumbnailerOptions options;
        options.testDelay = 200;
        context->getThumbnailer()->setOptions(options);
        context->getThumbnailer()->requestThumbnails(paths);
        context->getFolderInfo()->setTestDelay(200 * factor);
        context->getFolderInfo()->requestFolderInfo(directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(context->getDatabase()->getConnectionCount() > connections, 10000 * factor);
        QTRY_VERIFY_WITH_TIMEOUT(context->getFolderInfo()->getRunningCount() > 0, 10000 * factor);

        QPointer<pdf::PDFAppThumbnailer> thumbnailer = context->getThumbnailer();
        if (mode == QLatin1String("context"))
        {
            context->close();
            QVERIFY(!thumbnailer);
            QVERIFY(!context->getThumbnailer());
            QVERIFY(!context->getFolderInfo());
            QVERIFY(context->getRecent().getFiles().empty());
        }
        context.reset();
        QVERIFY(!thumbnailer);
        QTest::qWait(300);
        return;
    }

    ThumbnailFixture fixture;
    QStringList paths;
    for (int i = 0; i < 5; ++i)
    {
        paths << fixture.createRecordedPdf(QStringLiteral("doc%1.pdf").arg(i));
    }

    pdf::PDFAppThumbnailerOptions options;
    options.testDelay = 300;
    fixture.thumbnailer->setOptions(options);
    QSignalSpy finished(fixture.thumbnailer.get(), &pdf::PDFAppThumbnailer::thumbnailFinished);
    const int connections = fixture.database->getConnectionCount();
    fixture.thumbnailer->requestThumbnails(paths);
    QCOMPARE(fixture.thumbnailer->getPendingCount(), 5);

    // The worker has read the stored state of the first document (its own connection
    // to the database): the stop comes during the work
    QTRY_VERIFY_WITH_TIMEOUT(fixture.database->getConnectionCount() > connections, 10000 * AppDataTestHelper::getTimeoutFactor());

    QElapsedTimer timer;
    timer.start();
    if (mode == QLatin1String("stop"))
    {
        fixture.thumbnailer->stop();
        QVERIFY(fixture.thumbnailer->isIdle());
    }
    else
    {
        fixture.thumbnailer.reset();
    }
    QVERIFY2(timer.elapsed() < 5000 * AppDataTestHelper::getTimeoutFactor(), qPrintable(QString::number(timer.elapsed())));

    // Nothing more is written, no result is delivered after the stop
    QTest::qWait(500);
    for (const QString& path : paths)
    {
        QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Pending);
    }
    QCOMPARE(finished.count(), 0);
}

void AppDataTest::thumbnailerQueueLimit()
{
    // While the worker is busy with the first document, at most MAXIMUM_QUEUE_LENGTH
    // requests are pending, further ones are ignored
    ThumbnailFixture fixture;
    const int factor = AppDataTestHelper::getTimeoutFactor();
    const QString first = fixture.createRecordedPdf(QStringLiteral("first.pdf"));
    QVERIFY(!first.isEmpty());

    pdf::PDFAppThumbnailerOptions options;
    options.testDelay = 300 * factor;
    options.stopWaitTime = 60000 * factor;
    fixture.thumbnailer->setOptions(options);

    const int connections = fixture.database->getConnectionCount();
    fixture.thumbnailer->requestThumbnail(first);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.database->getConnectionCount() > connections, 10000 * factor);

    for (int i = 0; i < pdf::PDFAppThumbnailer::MAXIMUM_QUEUE_LENGTH + 10; ++i)
    {
        fixture.thumbnailer->requestThumbnail(fixture.directory.filePath(QStringLiteral("queued%1.pdf").arg(i)));
    }
    QCOMPARE(fixture.thumbnailer->getPendingCount(), pdf::PDFAppThumbnailer::MAXIMUM_QUEUE_LENGTH);
    QVERIFY(!fixture.thumbnailer->isIdle());

    fixture.thumbnailer->stop();
    QVERIFY(fixture.thumbnailer->isIdle());
}

void AppDataTest::thumbnailerStopCancelsRendering()
{
    // stop() cancels the running rendering: it returns long before the rendering would
    // end. The times are relative to the measured reading and rendering of the page
    // (no fixed time), the page gets more objects until its rendering is slow enough.
    ThumbnailFixture fixture;
    const int factor = AppDataTestHelper::getTimeoutFactor();
    const QString filePath = fixture.directory.filePath(QStringLiteral("slow.pdf"));
    qint64 readTime = 0;
    qint64 totalTime = 0;
    for (int rectangles = 40000; rectangles <= 640000 && totalTime - readTime < 300; rectangles *= 2)
    {
        QVERIFY(AppDataTestHelper::createPdf(filePath, 1, QSizeF(300, 400), pdf::PageRotation::None, QString(), QString(), rectangles));

        QElapsedTimer timer;
        timer.start();
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        readTime = timer.elapsed();

        timer.restart();
        pdf::PDFAppThumbnailerOptions unlimited;
        unlimited.timeBudget = 0;
        QCOMPARE(pdf::PDFAppThumbnailer::createThumbnail(filePath, unlimited, nullptr).state, pdf::PDFRecentThumbnailState::Ready);
        totalTime = timer.elapsed();
    }
    if (totalTime - readTime < 300)
    {
        QSKIP("The page is rendered too fast to measure the cancellation");
    }

    const QString path = pdf::PDFRepositoryHelper::normalizePath(filePath);
    QVERIFY(!fixture.recent().recordOpened(path).isError());
    pdf::PDFAppThumbnailerOptions options;
    options.timeBudget = 0;
    options.stopWaitTime = 600000;
    fixture.thumbnailer->setOptions(options);

    // The worker has read the stored state (its connection), now it reads and renders
    // (its rendering marker is written)
    const int connections = fixture.database->getConnectionCount();
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.database->getConnectionCount() > connections, 10000 * factor);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.recent().getRenderingMarker(path).has_value(), 10000 * factor);

    QElapsedTimer timer;
    timer.start();
    fixture.thumbnailer->stop();
    const qint64 stopTime = timer.elapsed();
    QVERIFY2(stopTime < readTime + (totalTime - readTime) / 2,
             qPrintable(QStringLiteral("stop %1 ms, reading %2 ms, reading and rendering %3 ms").arg(stopTime).arg(readTime).arg(totalTime)));
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Pending);

    // A stopped rendering is no crash: stop() has removed its marker
    QVERIFY(fixture.recent().getRenderingMarkers().empty());
}

void AppDataTest::thumbnailerStoreError()
{
    // The database refuses the result (a trigger of the database, the worker has its
    // own connection): a warning, the document stays Pending and is tried again
    ThumbnailFixture fixture;
    const int timeout = 30000 * AppDataTestHelper::getTimeoutFactor();
    const QString path = fixture.createRecordedPdf(QStringLiteral("refused.pdf"));
    QVERIFY(!path.isEmpty());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "CREATE TRIGGER fault_store BEFORE UPDATE OF thumbnail_state ON recent_file "
                                                               "BEGIN SELECT RAISE(ABORT, 'injected fault'); END"));

    QSignalSpy finished(fixture.thumbnailer.get(), &pdf::PDFAppThumbnailer::thumbnailFinished);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Thumbnail of .*refused\\.pdf cannot be stored: .*injected fault")));
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeout);
    QCOMPARE(finished.first().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Ready);
    QVERIFY(!finished.first().at(2).toBool());
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Pending);
    QVERIFY(!fixture.recent().getFile(path)->hasThumbnail);

    // Without the fault the next request stores the thumbnail
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "DROP TRIGGER fault_store"));
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, timeout);
    QVERIFY(finished.last().at(2).toBool());
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Ready);
}

void AppDataTest::folderInfo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString folder = directory.path();
    for (const char* name : { "a.pdf", "B.PDF", "c.Pdf", "notes.txt", "pdf", "x.pdf.bak" })
    {
        QVERIFY(AppDataTestHelper::writeFile(directory.filePath(QLatin1String(name)), "x"));
    }
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("sub"))));
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("folder.pdf"))));
    QVERIFY(AppDataTestHelper::writeFile(directory.filePath(QStringLiteral("sub/d.pdf")), "x"));

    pdf::PDFAppFolderInfo info = pdf::PDFAppFolderInfoService::computeInfo(folder, true, pdf::PDFAppFolderInfoService::DEFAULT_MAXIMUM_ENTRIES);
    QVERIFY(info.exists);
    QVERIFY(info.isDirectory);
    QVERIFY(!info.isFile);
    QCOMPARE(info.pdfCount, 3);
    QVERIFY(!info.isCountLimited);
    QCOMPARE(info.path, folder);

    // Native separators are accepted
    QCOMPARE(pdf::PDFAppFolderInfoService::computeInfo(QDir::toNativeSeparators(folder), true, 100).pdfCount, 3);

    // The limit of the enumerated entries (8 entries in the folder)
    info = pdf::PDFAppFolderInfoService::computeInfo(folder, true, 2);
    QVERIFY(info.isCountLimited);
    QVERIFY(info.pdfCount >= 0 && info.pdfCount <= 2);
    info = pdf::PDFAppFolderInfoService::computeInfo(folder, true, 8);
    QVERIFY(!info.isCountLimited);
    QCOMPARE(info.pdfCount, 3);

    // Only the existence
    info = pdf::PDFAppFolderInfoService::computeInfo(folder, false, 100);
    QVERIFY(info.exists);
    QCOMPARE(info.pdfCount, -1);

    info = pdf::PDFAppFolderInfoService::computeInfo(directory.filePath(QStringLiteral("a.pdf")), true, 100);
    QVERIFY(info.exists);
    QVERIFY(info.isFile);
    QCOMPARE(info.pdfCount, -1);

    info = pdf::PDFAppFolderInfoService::computeInfo(directory.filePath(QStringLiteral("missing")), true, 100);
    QVERIFY(!info.exists);
    QCOMPARE(info.pdfCount, -1);

    for (const QString& invalid : { QString(), QStringLiteral("relative"), folder + QString(QChar(0)) })
    {
        info = pdf::PDFAppFolderInfoService::computeInfo(invalid, true, 100);
        QVERIFY(!info.exists);
    }

    // The service: results in the GUI thread, merged requests
    pdf::PDFAppFolderInfoService service;
    QCOMPARE(service.getMaximumEntries(), pdf::PDFAppFolderInfoService::DEFAULT_MAXIMUM_ENTRIES);
    QSignalSpy spy(&service, &pdf::PDFAppFolderInfoService::infoReady);
    service.setTestDelay(100);
    service.requestFolderInfo(folder);
    service.requestFolderInfo(folder);
    service.requestPathInfo(folder);
    service.requestPathInfo(directory.filePath(QStringLiteral("missing.pdf")));
    service.requestFolderInfo(QStringLiteral("relative"));
    QCOMPARE(service.getPendingCount(), 3);

    const int timeout = 10000 * AppDataTestHelper::getTimeoutFactor();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, timeout);
    QCOMPARE(service.getPendingCount(), 0);

    int counted = 0;
    int missing = 0;
    for (const QList<QVariant>& arguments : std::as_const(spy))
    {
        const pdf::PDFAppFolderInfo result = arguments.at(0).value<pdf::PDFAppFolderInfo>();
        if (result.pdfCount == 3)
        {
            ++counted;
        }
        if (!result.exists)
        {
            ++missing;
            QCOMPARE(result.path, directory.filePath(QStringLiteral("missing.pdf")));
        }
    }
    QCOMPARE(counted, 1);
    QCOMPARE(missing, 1);

    service.setMaximumEntries(0);
    QCOMPARE(service.getMaximumEntries(), 1);
    service.setStopWaitTime(-1);
    QCOMPARE(service.getStopWaitTime(), 0);
}

void AppDataTest::folderInfoPendingLimit()
{
    // The results are delivered by the event loop: without it the pending requests
    // grow up to MAXIMUM_PENDING, further requests are ignored
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdf::PDFAppFolderInfoService service;
    QSignalSpy spy(&service, &pdf::PDFAppFolderInfoService::infoReady);
    for (int i = 0; i < pdf::PDFAppFolderInfoService::MAXIMUM_PENDING + 10; ++i)
    {
        const bool isAccepted = service.requestPathInfo(directory.filePath(QStringLiteral("missing%1.pdf").arg(i)));
        QCOMPARE(isAccepted, i < pdf::PDFAppFolderInfoService::MAXIMUM_PENDING);
    }
    QCOMPARE(service.getPendingCount(), pdf::PDFAppFolderInfoService::MAXIMUM_PENDING);
    QVERIFY(service.isFull());

    // A request merged with a pending one is accepted (its result comes), a folder refused
    QVERIFY(service.requestPathInfo(directory.filePath(QStringLiteral("missing0.pdf"))));
    QVERIFY(!service.requestFolderInfo(directory.path()));
    QVERIFY(!service.requestPathInfo(QStringLiteral("relative.pdf")));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), pdf::PDFAppFolderInfoService::MAXIMUM_PENDING, 30000 * AppDataTestHelper::getTimeoutFactor());
    QCOMPARE(service.getPendingCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(service.getRunningCount(), 0, 10000 * AppDataTestHelper::getTimeoutFactor());

    // A request accepted again after the queue has emptied
    service.requestPathInfo(directory.path());
    QCOMPARE(service.getPendingCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), pdf::PDFAppFolderInfoService::MAXIMUM_PENDING + 1, 10000 * AppDataTestHelper::getTimeoutFactor());
    QVERIFY(spy.last().at(0).value<pdf::PDFAppFolderInfo>().isDirectory);
}

void AppDataTest::folderInfoDeletedDuringWork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const int factor = AppDataTestHelper::getTimeoutFactor();

    auto service = std::make_unique<pdf::PDFAppFolderInfoService>();
    QSignalSpy spy(service.get(), &pdf::PDFAppFolderInfoService::infoReady);
    service->setTestDelay(300);
    for (int i = 0; i < 10; ++i)
    {
        service->requestFolderInfo(directory.filePath(QStringLiteral("f%1").arg(i)));
    }

    // The workers sleep in their test delay (4 threads, the other requests are queued)
    QTRY_VERIFY_WITH_TIMEOUT(service->getRunningCount() > 0, 10000 * factor);

    // The destruction waits for the running workers, the queued requests are dropped,
    // no result reaches a destroyed service
    QElapsedTimer timer;
    timer.start();
    service.reset();
    QVERIFY(timer.elapsed() < 5000 * AppDataTestHelper::getTimeoutFactor());
    QTest::qWait(500);
    QCOMPARE(spy.count(), 0);

    // A result, which is posted while the service is being deleted, is dropped:
    // delete the service from the slot of the first result
    auto second = new pdf::PDFAppFolderInfoService();
    QPointer<pdf::PDFAppFolderInfoService> guard(second);
    QObject context;
    QObject::connect(second, &pdf::PDFAppFolderInfoService::infoReady, &context, [second]() { delete second; });
    for (int i = 0; i < 5; ++i)
    {
        second->requestPathInfo(directory.filePath(QStringLiteral("p%1").arg(i)));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!guard, 10000 * AppDataTestHelper::getTimeoutFactor());
    QTest::qWait(200);
}

void AppDataTest::abandonedWorkers()
{
    // A worker, which does not stop in time (a file system call of a network path,
    // simulated by the test delay), is abandoned: the owner does not wait for it,
    // the worker never touches the database again and ends by itself
    ThumbnailFixture fixture;
    const int factor = AppDataTestHelper::getTimeoutFactor();
    const QString path = fixture.createRecordedPdf(QStringLiteral("slow.pdf"));
    QVERIFY(!path.isEmpty());

    pdf::PDFAppThumbnailerOptions options;
    options.testDelay = 1500 * factor;
    options.stopWaitTime = 100;
    fixture.thumbnailer->setOptions(options);

    // The worker reads the stored state by its own connection, then it sleeps in the
    // test delay: the stop comes certainly during the sleep (a fixed wait would not
    // guarantee, that the worker has started on a loaded machine)
    const int connections = fixture.database->getConnectionCount();
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.database->getConnectionCount() > connections, 10000 * factor);

    QElapsedTimer timer;
    timer.start();
    QTest::ignoreMessage(QtWarningMsg, "The thumbnail worker did not stop in 100 ms, it is abandoned.");
    fixture.thumbnailer.reset();
    QVERIFY(timer.elapsed() < 1000 * factor);

    // The database can be used at once (the context closes it after the thumbnailer);
    // the abandoned worker ends by itself (its connection is closed with its thread)
    // and writes nothing
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.database->getConnectionCount(), connections, 30000 * factor);
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Pending);

    // The pool of the folder information: the worker sleeps in its test delay
    QTemporaryDir directory;
    auto service = std::make_unique<pdf::PDFAppFolderInfoService>();
    service->setStopWaitTime(0);
    service->setTestDelay(500 * factor);
    QSignalSpy spy(service.get(), &pdf::PDFAppFolderInfoService::infoReady);
    service->requestFolderInfo(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(service->getRunningCount() > 0, 10000 * factor);
    QTest::ignoreMessage(QtWarningMsg, "The folder information workers did not stop in 0 ms, they are abandoned.");
    service.reset();

    // Negative check: the result of the abandoned worker (after its delay) never comes
    QTest::qWait(1000 * factor);
    QCOMPARE(spy.count(), 0);
}

/// Operation control of a test, which throws (an exception of the library inside of
/// the reading or the rendering of a document)
class AppDataThrowingControl : public pdf::PDFOperationControl
{
public:
    explicit AppDataThrowingControl(bool isStandardException) : m_isStandardException(isStandardException) { }

    virtual bool isOperationCancelled() const override
    {
        if (m_isStandardException)
        {
            throw std::bad_alloc();
        }
        throw pdf::PDFException(QStringLiteral("Injected failure"));
    }

private:
    bool m_isStandardException;
};

void AppDataTest::thumbnailerExceptions()
{
    // An exception of the library never leaves createThumbnail (it would end the
    // worker thread and the process): the document gets the state Failed
    QTemporaryDir directory;
    const QString filePath = directory.filePath(QStringLiteral("doc.pdf"));
    QVERIFY(AppDataTestHelper::createPdf(filePath, 1));
    const qint64 modified = QFileInfo(filePath).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();

    for (const bool isStandardException : { false, true })
    {
        AppDataThrowingControl control(isStandardException);
        const pdf::PDFAppThumbnailResult result = pdf::PDFAppThumbnailer::createThumbnail(filePath, pdf::PDFAppThumbnailerOptions(), &control);
        QCOMPARE(result.outcome, pdf::PDFAppThumbnailResult::Outcome::Done);
        QCOMPARE(result.state, pdf::PDFRecentThumbnailState::Failed);
        QCOMPARE(result.fileModified, modified);
        QVERIFY(result.png.isEmpty());
    }
}

void AppDataTest::thumbnailerBusyDatabase()
{
    // The worker writes its result while another instance holds the write lock of
    // the database (it waits for the busy timeout): a request of the GUI thread must
    // not wait for it
    const int factor = AppDataTestHelper::getTimeoutFactor();
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("storage.sqlite"));

    pdf::PDFDatabaseOptions options = AppDataTestHelper::createFileOptions(databasePath);
    options.busyTimeout = qMin(600000, 30000 * factor);
    pdf::PDFDatabase database;
    database.open(options);
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);

    pdf::PDFDatabase otherInstance;
    otherInstance.open(AppDataTestHelper::createFileOptions(databasePath));
    QCOMPARE(otherInstance.getState().mode, pdf::PDFDatabaseStorageMode::Disk);

    QStringList paths;
    for (const QString& name : { QStringLiteral("first.pdf"), QStringLiteral("second.pdf") })
    {
        const QString filePath = directory.filePath(name);
        QVERIFY(AppDataTestHelper::createPdf(filePath, 1));
        paths << pdf::PDFRepositoryHelper::normalizePath(filePath);
        QVERIFY(!pdf::PDFRecentRepository(&database, nullptr).recordOpened(paths.back()).isError());
    }

    pdf::PDFAppThumbnailer thumbnailer(&database);
    QSignalSpy finished(&thumbnailer, &pdf::PDFAppThumbnailer::thumbnailFinished);

    {
        pdf::PDFDatabaseTransaction lock(otherInstance.getConnection());
        QVERIFY(lock.isActive());

        thumbnailer.requestThumbnail(paths[0]);

        // The tiny document is rendered at once, then the worker waits for the lock
        QTest::qWait(1500 * factor);
        QCOMPARE(finished.count(), 0);

        QElapsedTimer timer;
        timer.start();
        thumbnailer.requestThumbnail(paths[1]);
        QVERIFY(thumbnailer.getPendingCount() >= 1);
        QVERIFY2(timer.elapsed() < 1000 * factor, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(lock.commit());
    }

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 30000 * factor);
    QVERIFY(finished.at(0).at(2).toBool());
    QVERIFY(finished.at(1).at(2).toBool());
    for (const QString& path : paths)
    {
        QCOMPARE(pdf::PDFRecentRepository(&database, nullptr).getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Ready);
    }
    thumbnailer.stop();
}

void AppDataTest::devicePaths()
{
    // Device namespaces of Windows are no documents and no folders: they are never
    // stored, a row of another program with such a path is skipped
    const QStringList devicePaths =
    {
        QStringLiteral("//./PhysicalDrive0"),
        QStringLiteral("\\\\.\\pipe\\name"),
        QStringLiteral("//?/GLOBALROOT/Device/HarddiskVolume1/a.pdf"),
        QStringLiteral("\\\\?\\C:\\a.pdf"),
        QStringLiteral("/\\.\\pipe\\name"),
        QStringLiteral("\\/?/C:/a.pdf"),
    };

    DataFixture fixture;
    const QString valid = fixture.createFile(QStringLiteral("valid.pdf"));
    QVERIFY(pdf::PDFRepositoryHelper::isValidStoredPath(valid));
    QVERIFY(!fixture.recent().recordOpened(valid).isError());

    for (const QString& path : devicePaths)
    {
        QVERIFY2(!pdf::PDFRepositoryHelper::isValidStoredPath(path), qPrintable(path));
        // A long path of a drive (the prefix of the long paths) may be normalized to the drive path
        const QString normalized = pdf::PDFRepositoryHelper::normalizePath(path);
        QVERIFY2(!pdf::PDFRepositoryHelper::isDevicePath(normalized), qPrintable(path));
        QString stored;
        (void)fixture.recent().recordOpened(path, &stored);
        QVERIFY2(!pdf::PDFRepositoryHelper::isDevicePath(stored), qPrintable(path));
        (void)fixture.recent().addPinnedLocation(path, QString());

        pdf::PDFDatabaseStatement insert(fixture.database->getConnection(), "INSERT INTO recent_file(path, name, folder, last_opened, open_count) VALUES (?1, 'x.pdf', '/', 1, 1)");
        insert.bindText(1, path);
        QVERIFY(insert.execute());
    }

    // The rows with a device path are skipped
    const std::vector<pdf::PDFRecentFile> files = fixture.recent().getFiles();
    QVERIFY(!files.empty());
    QVERIFY(std::any_of(files.cbegin(), files.cend(), [&valid](const pdf::PDFRecentFile& file) { return file.path == valid; }));
    for (const pdf::PDFRecentFile& file : files)
    {
        QVERIFY2(!pdf::PDFRepositoryHelper::isDevicePath(file.path), qPrintable(file.path));
    }
    for (const pdf::PDFPinnedLocation& location : fixture.recent().getPinnedLocations())
    {
        QVERIFY2(!pdf::PDFRepositoryHelper::isDevicePath(location.path), qPrintable(location.path));
    }

    // The thumbnailer and the folder information ignore them
    pdf::PDFAppThumbnailer thumbnailer(fixture.database.get());
    thumbnailer.requestThumbnails(devicePaths);
    QVERIFY(thumbnailer.isIdle());
    for (const QString& path : devicePaths)
    {
        const pdf::PDFAppFolderInfo info = pdf::PDFAppFolderInfoService::computeInfo(path, true, 100);
        QVERIFY(!info.exists);
    }
}

void AppDataTest::recentRowLimit()
{
    // A table of another program with more rows than any list shows: the lists read
    // at most MAXIMUM_FILE_ROWS rows (memory and time of a page are bounded)
    DataFixture fixture;
    const int rowCount = pdf::PDFRecentRepository::MAXIMUM_FILE_ROWS + 50;
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "BEGIN"));
    {
        pdf::PDFDatabaseStatement insert(fixture.database->getConnection(), "INSERT INTO recent_file(path, name, folder, last_opened, open_count, pinned, pin_order, last_page) "
                                                                           "VALUES (?1, 'a.pdf', '/', ?2, 1, 1, ?2, 5)");
        for (int i = 0; i < rowCount; ++i)
        {
            insert.bindText(1, QStringLiteral("/folder/file%1.pdf").arg(i));
            insert.bindInt64(2, i + 1);
            QVERIFY(insert.execute());
        }
    }
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "COMMIT"));

    QCOMPARE(int(fixture.recent().getFiles().size()), pdf::PDFRecentRepository::MAXIMUM_FILE_ROWS);
    QCOMPARE(int(fixture.recent().getPinnedFiles().size()), pdf::PDFRecentRepository::MAXIMUM_FILE_ROWS);
    QCOMPARE(int(fixture.recent().getContinueReading(rowCount).size()), pdf::PDFRecentRepository::MAXIMUM_FILE_ROWS);

    // The newest ones are read
    QCOMPARE(fixture.recent().getFiles().front().path, QStringLiteral("/folder/file%1.pdf").arg(rowCount - 1));
}

void AppDataTest::networkPaths_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("isNetwork");

    // By the name alone (every system)
    QTest::newRow("unc backslashes") << QStringLiteral("\\\\server\\share\\a.pdf") << true;
    QTest::newRow("unc slashes") << QStringLiteral("//server/share/a.pdf") << true;
    QTest::newRow("unc mixed") << QStringLiteral("\\/server/share") << true;
    QTest::newRow("long unc") << QStringLiteral("\\\\?\\UNC\\server\\share\\a.pdf") << true;
    QTest::newRow("device unc") << QStringLiteral("\\\\.\\UNC\\server\\share\\a.pdf") << true;
    QTest::newRow("device unc slashes") << QStringLiteral("//./unc/server/share") << true;
    QTest::newRow("device") << QStringLiteral("\\\\.\\PhysicalDrive0") << false;
    QTest::newRow("long local") << QStringLiteral("\\\\?\\C:\\a.pdf") << false;
    QTest::newRow("smb url") << QStringLiteral("smb://nas/docs/a.pdf") << true;
    QTest::newRow("nfs url") << QStringLiteral("nfs://nas/export") << true;
    QTest::newRow("sftp url") << QStringLiteral("sftp://host/home/a.pdf") << true;
    QTest::newRow("dav+ url") << QStringLiteral("dav+sd://host/a.pdf") << true;
    QTest::newRow("bad scheme") << QStringLiteral("1x://host/a.pdf") << false;
    QTest::newRow("short scheme") << QStringLiteral("c://a.pdf") << false;
    QTest::newRow("scheme with space") << QStringLiteral("a b://host") << false;
    QTest::newRow("gvfs") << QStringLiteral("/run/user/1000/gvfs/smb-share:server=nas,share=docs/a.pdf") << true;
    QTest::newRow("gvfs root") << QStringLiteral("/run/user/1000/gvfs") << true;
    QTest::newRow("run user other") << QStringLiteral("/run/user/1000/doc/a.pdf") << false;
    QTest::newRow("run user short") << QStringLiteral("/run/user/1000") << false;
    QTest::newRow("local unix") << QStringLiteral("/home/user/a.pdf") << false;
    QTest::newRow("local windows") << QStringLiteral("C:/Users/a.pdf") << false;
    QTest::newRow("local backslashes") << QStringLiteral("C:\\Users\\a.pdf") << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("relative") << QStringLiteral("docs/a.pdf") << false;
    // By the classifier of the test (mounts /mnt/nas, /media/user/My Share; remote drives Z, y)
    QTest::newRow("mount") << QStringLiteral("/mnt/nas/a.pdf") << true;
    QTest::newRow("mount itself") << QStringLiteral("/mnt/nas") << true;
    QTest::newRow("mount prefix") << QStringLiteral("/mnt/nasty/a.pdf") << false;
    QTest::newRow("mount dots") << QStringLiteral("/mnt/other/../nas/a.pdf") << true;
    QTest::newRow("mount with space") << QStringLiteral("/media/user/My Share/a.pdf") << true;
    QTest::newRow("remote drive") << QStringLiteral("Z:/docs/a.pdf") << true;
    QTest::newRow("remote drive lower") << QStringLiteral("y:\\docs\\a.pdf") << true;
    QTest::newRow("local drive") << QStringLiteral("D:/docs/a.pdf") << false;
    QTest::newRow("non ascii drive") << QString(QChar(0x10D)) + QStringLiteral(":/a.pdf") << false;
}

void AppDataTest::networkPaths()
{
    QFETCH(QString, path);
    QFETCH(bool, isNetwork);

    std::vector<pdf::PDFAppMount> mounts(3);
    mounts[0].mountPoint = QStringLiteral("/mnt/nas/");
    mounts[0].fileSystemType = QStringLiteral("nfs4");
    mounts[1].mountPoint = QStringLiteral("/media/user/My Share");
    mounts[1].fileSystemType = QStringLiteral("cifs");
    mounts[2].mountPoint = QString();
    const pdf::PDFAppNetworkPaths classifier(mounts, QStringLiteral("Zy"));
    QCOMPARE(int(classifier.getNetworkMounts().size()), 2);
    QCOMPARE(classifier.getNetworkMounts().front().mountPoint, QStringLiteral("/media/user/My Share"));
    QCOMPARE(classifier.isNetworkPath(path), isNetwork);
}

void AppDataTest::networkMountTable()
{
    // /proc/self/mountinfo: escapes, optional fields, the separator, malformed lines
    const QByteArray text =
        "22 1 8:1 / / rw,relatime shared:1 - ext4 /dev/sda1 rw\n"
        "36 22 0:40 / /mnt/nas rw,relatime shared:20 - nfs4 nas:/export rw,vers=4.2\n"
        "37 22 0:41 / /media/user/My\\040Share rw - cifs //nas/share rw\n"
        "38 22 0:42 / /home/user/remote rw,nosuid master:3 - fuse.sshfs user@host: rw\n"
        "39 22 0:43 / /run/user/1000/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
        "40 22 0:44 / /mnt/c rw - 9p drvfs rw\n"
        "41 22 0:45 / /mnt/tab\\011name rw - smb3 //x/y rw\n"
        "42 22 0:46 / /mnt/bad\\9 rw - ceph a rw\n"
        "43 22 0:47 / relative rw - nfs a rw\n"
        "malformed line\n"
        "44 22 0:48 / /mnt/nosep rw nfs a rw\n"
        "45 22 0:49 / /mnt/end rw -\n"
        "\n";
    const std::vector<pdf::PDFAppMount> mounts = pdf::PDFAppNetworkPaths::parseMountInfo(text);
    QStringList points;
    for (const pdf::PDFAppMount& mount : mounts)
    {
        points << mount.mountPoint + QLatin1Char('|') + mount.fileSystemType;
    }
    QCOMPARE(points, QStringList({ QStringLiteral("/mnt/nas|nfs4"), QStringLiteral("/media/user/My Share|cifs"), QStringLiteral("/home/user/remote|fuse.sshfs"),
                                   QStringLiteral("/run/user/1000/gvfs|fuse.gvfsd-fuse"), QStringLiteral("/mnt/tab\tname|smb3"), QDir::cleanPath(QStringLiteral("/mnt/bad\\9")) + QStringLiteral("|ceph") }));

    QVERIFY(pdf::PDFAppNetworkPaths::isNetworkFileSystem(QStringLiteral("NFS")));
    QVERIFY(pdf::PDFAppNetworkPaths::isNetworkFileSystem(QStringLiteral("fuse.rclone")));
    QVERIFY(!pdf::PDFAppNetworkPaths::isNetworkFileSystem(QStringLiteral("9p")));
    QVERIFY(!pdf::PDFAppNetworkPaths::isNetworkFileSystem(QStringLiteral("ext4")));
    QVERIFY(!pdf::PDFAppNetworkPaths::isNetworkFileSystem(QString()));

    // A network root: everything below "/" is on the network
    std::vector<pdf::PDFAppMount> root(1);
    root[0].mountPoint = QStringLiteral("/");
    root[0].fileSystemType = QStringLiteral("nfs");
    QVERIFY(pdf::PDFAppNetworkPaths(root, QString()).isNetworkPath(QStringLiteral("/home/a.pdf")));

    // A huge table is read up to the limit of the lines
    QByteArray huge;
    for (int i = 0; i < pdf::PDFAppNetworkPaths::MAXIMUM_MOUNTS + 10; ++i)
    {
        huge += "1 1 0:1 / /mnt/n" + QByteArray::number(i) + " rw - nfs a rw\n";
    }
    QCOMPARE(int(pdf::PDFAppNetworkPaths::parseMountInfo(huge).size()), pdf::PDFAppNetworkPaths::MAXIMUM_MOUNTS);

    // The classifier of the system: the temporary folder of the test is local
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const pdf::PDFAppNetworkPaths system;
    QVERIFY(!system.isNetworkPath(QDir::fromNativeSeparators(directory.path())));
    QVERIFY(!system.isNetworkPath(QString()));
    QVERIFY(system.isNetworkPath(QStringLiteral("//server/share")));
#if defined(Q_OS_WIN)
    // A drive, which does not exist, is no network drive (twice: the cached answer)
    for (int i = 0; i < 2; ++i)
    {
        bool isAnyRemote = false;
        for (char letter = 'A'; letter <= 'Z'; ++letter)
        {
            isAnyRemote = system.isNetworkPath(QString(QLatin1Char(letter)) + QStringLiteral(":/a.pdf")) || isAnyRemote;
        }
        Q_UNUSED(isAnyRemote);
    }
    QVERIFY(!system.isNetworkPath(QDir::fromNativeSeparators(QDir::tempPath())));
#endif
}

void AppDataTest::processInfo()
{
    QVERIFY(pdf::PDFAppProcessInfo::isProcessRunning(QCoreApplication::applicationPid()));
    QVERIFY(!pdf::PDFAppProcessInfo::isProcessRunning(0));
    QVERIFY(!pdf::PDFAppProcessInfo::isProcessRunning(-5));
    QVERIFY(!pdf::PDFAppProcessInfo::isProcessRunning(std::numeric_limits<qint64>::max()));

    // A child, which has ended
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-exit") });
    QVERIFY(process.waitForStarted(30000));
    const qint64 processId = process.processId();
    QVERIFY(process.waitForFinished(30000));
    QVERIFY(!pdf::PDFAppProcessInfo::isProcessRunning(processId));
}

void AppDataTest::renderingMarkers()
{
    DataFixture fixture;
    const QString path = fixture.createFile(QStringLiteral("a.pdf"));
    QVERIFY(!fixture.recent().recordOpened(path).isError());

    // The table exists, it has no triggers: a marker is no notification
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND tbl_name = 'thumbnail_rendering'"), qint64(0));
    const qint64 logBefore = AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM change_log");

    pdf::PDFThumbnailRenderingMarker marker;
    marker.path = path;
    marker.fileModified = 10;
    marker.instance = QStringLiteral("first");
    marker.processId = 1234;
    marker.started = 99;

    bool written = false;
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(written);
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM change_log"), logBefore);

    // A second marker of the file is not written (another instance renders it)
    pdf::PDFThumbnailRenderingMarker second = marker;
    second.instance = QStringLiteral("second");
    QVERIFY(!fixture.recent().beginRendering(second, &written).isError());
    QVERIFY(!written);
    QVERIFY(!fixture.recent().beginRendering(second, nullptr).isError());

    std::optional<pdf::PDFThumbnailRenderingMarker> stored = fixture.recent().getRenderingMarker(path);
    QVERIFY(stored);
    QCOMPARE(stored->instance, QStringLiteral("first"));
    QCOMPARE(stored->fileModified, qint64(10));
    QCOMPARE(stored->processId, qint64(1234));
    QCOMPARE(stored->started, qint64(99));
    QCOMPARE(int(fixture.recent().getRenderingMarkers().size()), 1);
    QVERIFY(!fixture.recent().getRenderingMarker(QStringLiteral("relative.pdf")));
    QVERIFY(!fixture.recent().getRenderingMarker(fixture.directory.filePath(QStringLiteral("other.pdf"))));

    // Invalid markers are refused
    for (int i = 0; i < 5; ++i)
    {
        pdf::PDFThumbnailRenderingMarker invalid = marker;
        switch (i)
        {
            case 0: invalid.path = QStringLiteral("relative.pdf"); break;
            case 1: invalid.instance.clear(); break;
            case 2: invalid.instance = QString(pdf::PDFRecentRepository::MAXIMUM_INSTANCE_LENGTH + 1, QLatin1Char('i')); break;
            case 3: invalid.fileModified = -1; break;
            default: invalid.processId = 0; break;
        }
        QVERIFY(fixture.recent().beginRendering(invalid, &written).isError());
        QVERIFY(!written);
    }
    QVERIFY(fixture.recent().endRendering(QStringLiteral("relative.pdf"), QStringLiteral("first")).isError());
    QVERIFY(fixture.recent().failRendering(pdf::PDFThumbnailRenderingMarker()).isError());

    // The end of another instance does not remove it, the own end does
    QVERIFY(!fixture.recent().endRendering(path, QStringLiteral("second")).isError());
    QVERIFY(fixture.recent().getRenderingMarker(path));
    QVERIFY(!fixture.recent().endRendering(path, QStringLiteral("first")).isError());
    QVERIFY(!fixture.recent().getRenderingMarker(path));

    // A crashed rendering: Failed with the time of its file, the marker is removed
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    bool failed = false;
    pdf::PDFThumbnailRenderingMarker replaced = marker;
    replaced.started = 100;
    QVERIFY(!fixture.recent().failRendering(replaced, &failed).isError());
    QVERIFY(!failed);
    QVERIFY(fixture.recent().getRenderingMarker(path));
    QVERIFY(!fixture.recent().failRendering(marker, &failed).isError());
    QVERIFY(failed);
    QVERIFY(!fixture.recent().getRenderingMarker(path));
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Failed);
    QCOMPARE(fixture.recent().getFile(path)->thumbnailModified, qint64(10));
    QVERIFY(!fixture.recent().getFile(path)->hasThumbnail);

    // A result of the same version stored before the crash is kept
    QVERIFY(!fixture.recent().setThumbnail(path, AppDataTestHelper::createPng(), 20, pdf::PDFRecentThumbnailState::Ready, 3).isError());
    marker.fileModified = 20;
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(!fixture.recent().failRendering(marker, &failed).isError());
    QVERIFY(!failed);
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Ready);
    QVERIFY(!fixture.recent().getRenderingMarker(path));

    // A marker of a file removed from the list: removed, nothing is written
    const QString removed = fixture.createFile(QStringLiteral("removed.pdf"));
    QVERIFY(!fixture.recent().recordOpened(removed).isError());
    pdf::PDFThumbnailRenderingMarker orphan = marker;
    orphan.path = removed;
    QVERIFY(!fixture.recent().beginRendering(orphan, &written).isError());
    QVERIFY(!fixture.recent().removeFile(removed).isError());
    QVERIFY(!fixture.recent().failRendering(orphan, &failed).isError());
    QVERIFY(!failed);
    QVERIFY(fixture.recent().getRenderingMarkers().empty());

    // Hostile rows: wrong types, invalid paths, an oversized instance are skipped
    // or bounded; nothing crashes
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(),
        "INSERT INTO thumbnail_rendering(path, file_mtime, instance, pid, started) VALUES ('relative.pdf', 1, 'x', 1, 1);"
        "INSERT INTO thumbnail_rendering(path, file_mtime, instance, pid, started) VALUES ('/hostile/a.pdf', 'x', 'x', 1, 1);"
        "INSERT INTO thumbnail_rendering(path, file_mtime, instance, pid, started) VALUES ('/hostile/b.pdf', 5, zeroblob(10), 'pid', 'started');"
        "INSERT INTO thumbnail_rendering(path, file_mtime, instance, pid, started) VALUES (x'00', 5, 'x', 1, 1);"));
    const std::vector<pdf::PDFThumbnailRenderingMarker> hostile = fixture.recent().getRenderingMarkers();
    QCOMPARE(int(hostile.size()), 1);
    QCOMPARE(hostile.front().path, QStringLiteral("/hostile/b.pdf"));
    QCOMPARE(hostile.front().processId, qint64(0));
    QVERIFY(hostile.front().instance.isEmpty());

    // Errors of the database are returned
    QVERIFY(AppDataFault::abortWrites(fixture.database.get(), QStringLiteral("INSERT"), QStringLiteral("thumbnail_rendering")));
    QVERIFY(fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "DROP TRIGGER temp.fault_abort"));
    QVERIFY(AppDataFault::abortWrites(fixture.database.get(), QStringLiteral("DELETE"), QStringLiteral("thumbnail_rendering")));
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(fixture.recent().endRendering(path, marker.instance).isError());
    QVERIFY(fixture.recent().failRendering(marker).isError());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "DROP TRIGGER temp.fault_abort"));

    // A marker of another version of the file (the file row is changed by failRendering)
    QVERIFY(!fixture.recent().endRendering(path, marker.instance).isError());
    marker.fileModified = 30;
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(written);
    QVERIFY(AppDataFault::abortWrites(fixture.database.get(), QStringLiteral("UPDATE"), QStringLiteral("recent_file")));
    QVERIFY(fixture.recent().failRendering(marker).isError());
    QVERIFY(fixture.recent().getRenderingMarker(path));
    QVERIFY(AppDataFault::clear(fixture.database.get()));
    QVERIFY(AppDataFault::failCommit(fixture.database.get(), QStringLiteral("recent_file")));
    QVERIFY(fixture.recent().failRendering(marker).isError());
    QVERIFY(fixture.recent().getRenderingMarker(path));
    QVERIFY(AppDataFault::clear(fixture.database.get()));
    QVERIFY(AppDataFault::failReads(fixture.database.get(), QStringLiteral("thumbnail_rendering")));
    QVERIFY(!fixture.recent().getRenderingMarker(path));
    QVERIFY(fixture.recent().getRenderingMarkers().empty());
    QVERIFY(fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(fixture.recent().endRendering(path, marker.instance).isError());
    QVERIFY(fixture.recent().failRendering(marker).isError());
}

void AppDataTest::thumbnailerMarkerAlive()
{
    const qint64 now = 1000000;
    pdf::PDFThumbnailRenderingMarker marker;
    marker.processId = QCoreApplication::applicationPid();
    marker.started = now - 10;
    QVERIFY(pdf::PDFAppThumbnailer::isMarkerAlive(marker, now, 1000));
    marker.started = now - 1000;
    QVERIFY(!pdf::PDFAppThumbnailer::isMarkerAlive(marker, now, 1000));
    marker.started = now + 999;
    QVERIFY(pdf::PDFAppThumbnailer::isMarkerAlive(marker, now, 1000));
    marker.started = now + 1000;
    QVERIFY(!pdf::PDFAppThumbnailer::isMarkerAlive(marker, now, 1000));
    marker.started = now;
    marker.processId = 0;
    QVERIFY(!pdf::PDFAppThumbnailer::isMarkerAlive(marker, now, 1000));

    pdf::PDFAppThumbnailer first(nullptr);
    pdf::PDFAppThumbnailer second(nullptr);
    QVERIFY(!first.getInstanceId().isEmpty());
    QVERIFY(first.getInstanceId().size() <= pdf::PDFRecentRepository::MAXIMUM_INSTANCE_LENGTH);
    QVERIFY(first.getInstanceId() != second.getInstanceId());

    pdf::PDFAppThumbnailerOptions options;
    options.maximumImagePixels = -5;
    options.markerLifetime = -5;
    first.setOptions(options);
    QCOMPARE(first.getOptions().maximumImagePixels, qint64(1));
    QCOMPARE(first.getOptions().markerLifetime, qint64(0));
}

void AppDataTest::thumbnailerCrashLoop()
{
    // A document, whose rendering ends the process (the child process ends inside of
    // the rendering), is not rendered again: Failed until the file changes
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const int timeout = 60000 * AppDataTestHelper::getTimeoutFactor();
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    const QString filePath = directory.filePath(QStringLiteral("crash.pdf"));
    QVERIFY(AppDataTestHelper::createPdf(filePath, 2));
    const QString otherPath = directory.filePath(QStringLiteral("other.pdf"));
    QVERIFY(AppDataTestHelper::createPdf(otherPath, 1));

    pdf::PDFDatabase database;
    database.open(AppDataTestHelper::createFileOptions(databasePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    pdf::PDFRecentRepository recent(&database, nullptr);
    QString path;
    QString other;
    QVERIFY(!recent.recordOpened(filePath, &path).isError());
    QVERIFY(!recent.recordOpened(otherPath, &other).isError());

    for (int run = 0; run < 2; ++run)
    {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-thumbnail-crash"), databasePath, path });
        QVERIFY(process.waitForFinished(timeout));
        QCOMPARE(process.exitCode(), pdf::PDFAppThumbnailer::TEST_EXIT_CODE);

        // The marker of the dead process is left, nothing else is written
        std::optional<pdf::PDFThumbnailRenderingMarker> marker = recent.getRenderingMarker(path);
        QVERIFY(marker);
        QCOMPARE(marker->fileModified, QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
        QVERIFY(!pdf::PDFAppProcessInfo::isProcessRunning(marker->processId));
        QCOMPARE(recent.getFile(path)->thumbnailState, run == 0 ? pdf::PDFRecentThumbnailState::Pending : pdf::PDFRecentThumbnailState::Ready);

        pdf::PDFAppThumbnailer thumbnailer(&database);
        QSignalSpy finished(&thumbnailer, &pdf::PDFAppThumbnailer::thumbnailFinished);
        if (run == 0)
        {
            // The next start requests the file: Failed without a rendering (the worker
            // has resolved the marker at its start, the request finds it up to date)
            thumbnailer.requestThumbnail(path);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeout);
            QCOMPARE(finished.first().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Failed);
        }
        else
        {
            // The worker resolves the markers at its start (a request of another file)
            thumbnailer.requestThumbnail(other);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeout);
            QCOMPARE(finished.first().at(0).toString(), other);
        }
        QVERIFY(!recent.getRenderingMarker(path));
        QCOMPARE(recent.getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Failed);
        QCOMPARE(recent.getFile(path)->thumbnailModified, marker->fileModified);
        QCOMPARE(recent.getFile(path)->pageCount, run == 0 ? -1 : 2);
        QVERIFY(!recent.getFile(path)->hasThumbnail);

        // No retry loop: the next request finds it up to date
        thumbnailer.requestThumbnail(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, timeout);
        QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Failed);
        QVERIFY(!finished.last().at(2).toBool());

        // The file changes: it is rendered again
        QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(60 * (run + 1))));
        if (run == 0)
        {
            thumbnailer.requestThumbnail(path);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, timeout);
            QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Ready);
            QCOMPARE(recent.getFile(path)->pageCount, 2);
            QVERIFY(recent.getRenderingMarkers().empty());
            QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(30)));
        }
    }

    // A crash of an older version of the file, met by a request (after the start of
    // the worker): the marker is removed, the current version is rendered
    {
        pdf::PDFAppThumbnailer thumbnailer(&database);
        QSignalSpy finished(&thumbnailer, &pdf::PDFAppThumbnailer::thumbnailFinished);
        thumbnailer.requestThumbnail(other);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeout);

        pdf::PDFThumbnailRenderingMarker old;
        old.path = path;
        old.fileModified = 5;
        old.instance = QStringLiteral("crashed");
        old.processId = std::numeric_limits<qint32>::max() - 7;
        old.started = QDateTime::currentMSecsSinceEpoch();
        bool written = false;
        QVERIFY(!recent.beginRendering(old, &written).isError());
        QVERIFY(written);

        thumbnailer.requestThumbnail(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, timeout);
        QVERIFY(recent.getRenderingMarkers().empty());
        QCOMPARE(recent.getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Ready);
    }
    database.close();
}

void AppDataTest::thumbnailerLiveMarker()
{
    // A marker of another running instance is no crash: the file is skipped (that
    // instance stores the result); an old one or one of an ended process is a crash
    ThumbnailFixture fixture;
    const int timeout = 30000 * AppDataTestHelper::getTimeoutFactor();
    const QString path = fixture.createRecordedPdf(QStringLiteral("shared.pdf"), 1);
    QVERIFY(!path.isEmpty());
    const qint64 fileModified = QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();

    QProcess sleeper;
    sleeper.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-sleep") });
    QVERIFY(sleeper.waitForStarted(30000));

    pdf::PDFThumbnailRenderingMarker marker;
    marker.path = path;
    marker.fileModified = fileModified;
    marker.instance = QStringLiteral("other-instance");
    marker.processId = sleeper.processId();
    marker.started = QDateTime::currentMSecsSinceEpoch();
    bool written = false;
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(written);

    QSignalSpy finished(fixture.thumbnailer.get(), &pdf::PDFAppThumbnailer::thumbnailFinished);
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, timeout);
    QCOMPARE(finished.first().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Pending);
    QVERIFY(!finished.first().at(2).toBool());
    QVERIFY(fixture.recent().getRenderingMarker(path));
    QCOMPARE(fixture.recent().getFile(path)->thumbnailState, pdf::PDFRecentThumbnailState::Pending);

    // The same marker older than the lifetime: the rendering hangs or has crashed
    pdf::PDFAppThumbnailerOptions options;
    options.markerLifetime = 60000;
    fixture.thumbnailer->setOptions(options);
    QVERIFY(!fixture.recent().endRendering(path, marker.instance).isError());
    marker.started = QDateTime::currentMSecsSinceEpoch() - 120000;
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, timeout);
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Failed);
    QVERIFY(finished.last().at(2).toBool());
    QVERIFY(!fixture.recent().getRenderingMarker(path));

    // A fresh marker of a process, which has ended (the sleeper is killed)
    QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(60)));
    const qint64 changedModified = QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    sleeper.kill();
    QVERIFY(sleeper.waitForFinished(30000));
    marker.fileModified = changedModified;
    marker.started = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, timeout);
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Failed);
    QCOMPARE(fixture.recent().getFile(path)->thumbnailModified, changedModified);

    // The own marker, which was left (a busy database at the end): the file is rendered
    QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(120)));
    marker.instance = fixture.thumbnailer->getInstanceId();
    marker.fileModified = QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
    marker.processId = QCoreApplication::applicationPid();
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, timeout);
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Ready);
    QVERIFY(fixture.recent().getRenderingMarkers().empty());

    // The database refuses the marker: the file is not rendered (never without a marker)
    QVERIFY(AppDataTestHelper::setModified(path, QDateTime::currentDateTimeUtc().addSecs(180)));
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "CREATE TRIGGER fault_marker BEFORE INSERT ON thumbnail_rendering "
                                                               "BEGIN SELECT RAISE(ABORT, 'injected fault'); END"));
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 5, timeout);
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Pending);
    QVERIFY(!finished.last().at(2).toBool());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "DROP TRIGGER fault_marker"));

    // The marker cannot be removed after the rendering: a warning, the result is stored
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "CREATE TRIGGER fault_marker BEFORE DELETE ON thumbnail_rendering "
                                                               "BEGIN SELECT RAISE(ABORT, 'injected fault'); END"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^The rendering marker of .*shared\\.pdf cannot be removed: .*injected fault")));
    fixture.thumbnailer->requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 6, timeout);
    QCOMPARE(finished.last().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Ready);
    QVERIFY(fixture.recent().getRenderingMarker(path));

    // stop() tries to remove it too (its warning), the marker of a crashed process stays
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^The rendering marker of .*shared\\.pdf cannot be removed: .*injected fault")));
    fixture.thumbnailer->stop();
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "DROP TRIGGER fault_marker"));
}

void AppDataTest::thumbnailerSweepErrors()
{
    // A marker, which cannot be resolved at the start (the database refuses it): a warning
    DataFixture fixture;
    const QString path = fixture.createFile(QStringLiteral("a.pdf"));
    QVERIFY(!fixture.recent().recordOpened(path).isError());
    pdf::PDFThumbnailRenderingMarker marker;
    marker.path = path;
    marker.fileModified = 1;
    marker.instance = QStringLiteral("dead");
    marker.processId = std::numeric_limits<qint32>::max() - 3;
    marker.started = 0;
    bool written = false;
    QVERIFY(!fixture.recent().beginRendering(marker, &written).isError());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "CREATE TRIGGER fault_sweep BEFORE DELETE ON thumbnail_rendering "
                                                               "BEGIN SELECT RAISE(ABORT, 'injected fault'); END"));

    pdf::PDFAppThumbnailer thumbnailer(fixture.database.get());
    QSignalSpy finished(&thumbnailer, &pdf::PDFAppThumbnailer::thumbnailFinished);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^The rendering marker of .*a\\.pdf cannot be resolved: .*injected fault")));
    thumbnailer.requestThumbnail(path);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000 * AppDataTestHelper::getTimeoutFactor());

    // The request itself meets the same marker: the file is skipped, nothing is rendered
    QCOMPARE(finished.first().at(1).value<pdf::PDFRecentThumbnailState>(), pdf::PDFRecentThumbnailState::Pending);
    QVERIFY(fixture.recent().getRenderingMarker(path));
    thumbnailer.stop();
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "DROP TRIGGER fault_sweep"));
}

void AppDataTest::thumbnailerImageLimit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // A page with a form (its image with a soft mask, the form referring to itself)
    // and a tiling pattern with an image; the content draws none of them
    const QString filePath = directory.filePath(QStringLiteral("images.pdf"));
    const auto write = [&](qint64 formImage, qint64 softMask, qint64 patternImage, const QByteArray& extraImage)
    {
        const QByteArray image = "<< /Type /XObject /Subtype /Image /Width %1 /Height 1000 /ColorSpace /DeviceGray /BitsPerComponent 8 %2/Length 1 >>\nstream\nX\nendstream";
        QList<QByteArray> objects = {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
            "/Resources << /XObject << /Fm1 5 0 R /Im0 7 0 R /Fm2 5 0 R /Bad 11 0 R /Num 3 >> /Pattern << /P1 8 0 R >> >> >>",
            "<< /Length 3 >>\nstream\nq Q\nendstream",
            "<< /Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /Im1 6 0 R /Self 5 0 R >> >> /Length 0 >>\nstream\n\nendstream",
            QByteArray(image).replace("%1", QByteArray::number(formImage)).replace("%2", "/SMask 9 0 R "),
            extraImage,
            "<< /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1 1] /XStep 1 /YStep 1 /Resources << /XObject << /Im2 10 0 R >> >> /Length 0 >>\nstream\n\nendstream",
            QByteArray(image).replace("%1", QByteArray::number(softMask)).replace("%2", ""),
            QByteArray(image).replace("%1", QByteArray::number(patternImage)).replace("%2", ""),
            "<< /Subtype /Image /Width 5 >>",
        };
        return AppDataTestHelper::writeFile(filePath, AppDataTestHelper::createRawPdf(objects));
    };

    struct Case
    {
        qint64 formImage;
        qint64 softMask;
        qint64 patternImage;
        QByteArray extraImage;
        qint64 expected;
    };
    const QByteArray invalidImage = "<< /Type /XObject /Subtype /Image /Width 2.5 /Height -7 /Length 1 >>\nstream\nX\nendstream";
    const QByteArray hugeImage = "<< /Type /XObject /Subtype /Image /Width 3000000000 /Height 3000000000 /Length 1 >>\nstream\nX\nendstream";
    const Case cases[] = {
        { 3, 2, 1, invalidImage, 3000 },
        { 3, 9, 1, invalidImage, 9000 },
        { 3, 2, 50, invalidImage, 50000 },
        { 3, 2, 1, hugeImage, qint64(std::numeric_limits<int>::max()) * std::numeric_limits<int>::max() },
    };
    for (const Case& imageCase : cases)
    {
        QVERIFY(write(imageCase.formImage, imageCase.softMask, imageCase.patternImage, imageCase.extraImage));
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        QCOMPARE(pdf::PDFAppThumbnailer::getLargestImagePixels(&document), imageCase.expected);
    }
    QCOMPARE(pdf::PDFAppThumbnailer::getLargestImagePixels(nullptr), qint64(0));

    // Over the limit: Failed (the page count is known), under it: rendered
    QVERIFY(write(3, 2, 1, invalidImage));
    pdf::PDFAppThumbnailerOptions options;
    options.timeBudget = 0;
    options.maximumImagePixels = 2999;
    pdf::PDFAppThumbnailResult result = pdf::PDFAppThumbnailer::createThumbnail(filePath, options, nullptr);
    QCOMPARE(result.outcome, pdf::PDFAppThumbnailResult::Outcome::Done);
    QCOMPARE(result.state, pdf::PDFRecentThumbnailState::Failed);
    QCOMPARE(result.pageCount, 1);
    QVERIFY(result.png.isEmpty());
    options.maximumImagePixels = 3000;
    result = pdf::PDFAppThumbnailer::createThumbnail(filePath, options, nullptr);
    QCOMPARE(result.state, pdf::PDFRecentThumbnailState::Ready);
    QVERIFY(!result.png.isEmpty());

    // Deep nesting of forms is bounded: forms nested 100 levels, the image at the end is not reached
    QList<QByteArray> nested = { "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /XObject << /F 4 0 R >> >> >>" };
    for (int i = 0; i < 100; ++i)
    {
        nested << QByteArray("<< /Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << /F %1 0 R >> >> /Length 0 >>\nstream\n\nendstream")
                      .replace("%1", QByteArray::number(nested.size() + 2));
    }
    nested << "<< /Type /XObject /Subtype /Image /Width 1000 /Height 1000 /Length 1 >>\nstream\nX\nendstream";
    QVERIFY(AppDataTestHelper::writeFile(filePath, AppDataTestHelper::createRawPdf(nested)));
    {
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        QCOMPARE(pdf::PDFAppThumbnailer::getLargestImagePixels(&document), qint64(0));
    }

    // Too many XObjects: the search stops at its limit
    QByteArray many = "<< ";
    for (int i = 0; i < 10010; ++i)
    {
        many += "/X" + QByteArray::number(i) + " 4 0 R ";
    }
    many += "/ZZLast 5 0 R >>";
    const QList<QByteArray> manyObjects = { "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /XObject " + many + " >> >>",
                                            "<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /Length 1 >>\nstream\nX\nendstream",
                                            "<< /Type /XObject /Subtype /Image /Width 2000 /Height 2000 /Length 1 >>\nstream\nX\nendstream" };
    QVERIFY(AppDataTestHelper::writeFile(filePath, AppDataTestHelper::createRawPdf(manyObjects)));
    {
        pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, true, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        QCOMPARE(pdf::PDFAppThumbnailer::getLargestImagePixels(&document), qint64(4));
    }
}

void AppDataTest::thumbnailerReading()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("big.pdf"));

    // An unused stream of 3 MB: a file of several parts of the reading
    const QList<QByteArray> objects = { "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
                                        "<< /Length 3145728 >>\nstream\n" + QByteArray(3 * 1024 * 1024, 'x') + "\nendstream" };
    QVERIFY(AppDataTestHelper::writeFile(filePath, AppDataTestHelper::createRawPdf(objects)));
    const int parts = int((QFileInfo(filePath).size() + 1024 * 1024 - 1) / (1024 * 1024));
    QCOMPARE(parts, 4);

    pdf::PDFAppThumbnailerOptions options;
    options.timeBudget = 0;

    // Canceled before the reading (the first question), between the parts of the
    // reading (one question before each part and one at the end of the file)
    for (int cancelAt = 1; cancelAt <= parts + 2; ++cancelAt)
    {
        AppDataCountingControl control(cancelAt);
        QCOMPARE(pdf::PDFAppThumbnailer::createThumbnail(filePath, options, &control).outcome, pdf::PDFAppThumbnailResult::Outcome::Canceled);
    }

    // Canceled after the parsing (a stop), never: rendered
    AppDataCountingControl afterParsing(parts + 3);
    QCOMPARE(pdf::PDFAppThumbnailer::createThumbnail(filePath, options, &afterParsing).outcome, pdf::PDFAppThumbnailResult::Outcome::Canceled);
    QCOMPARE(pdf::PDFAppThumbnailer::createThumbnail(filePath, options, nullptr).state, pdf::PDFRecentThumbnailState::Ready);

    // Larger than the limit (also a file, which grows - the size is checked again)
    options.maximumFileSize = 1024;
    pdf::PDFAppThumbnailResult result = pdf::PDFAppThumbnailer::createThumbnail(filePath, options, nullptr);
    QCOMPARE(result.state, pdf::PDFRecentThumbnailState::Failed);
    QCOMPARE(result.pageCount, -1);

    // A file, which cannot be opened for the reading (a folder named like a PDF)
    const QString folderPath = directory.filePath(QStringLiteral("folder.pdf"));
    QVERIFY(QDir().mkpath(folderPath));
    options.maximumFileSize = qint64(256) * 1024 * 1024;
    QCOMPARE(pdf::PDFAppThumbnailer::createThumbnail(folderPath, options, nullptr).outcome, pdf::PDFAppThumbnailResult::Outcome::Missing);
}

void AppDataTest::renameDeleted()
{
    // A workspace / a location deleted by another instance: the rename is an error
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    pdf::PDFDatabase first;
    first.open(AppDataTestHelper::createFileOptions(databasePath));
    pdf::PDFDatabase second;
    second.open(AppDataTestHelper::createFileOptions(databasePath));
    QCOMPARE(second.getState().mode, pdf::PDFDatabaseStorageMode::Disk);

    pdf::PDFWorkspaceRepository firstWorkspaces(&first);
    pdf::PDFWorkspaceRepository secondWorkspaces(&second);
    qint64 id = 0;
    QVERIFY(!firstWorkspaces.saveWorkspace(QStringLiteral("Thesis"), { { QStringLiteral("/a/b.pdf"), 1 } }, false, &id).isError());

    // The same name: no change, a success
    QVERIFY(!firstWorkspaces.renameWorkspace(id, QStringLiteral("Thesis")).isError());
    QVERIFY(!secondWorkspaces.deleteWorkspace(id).isError());
    const pdf::PDFDatabaseError workspaceError = firstWorkspaces.renameWorkspace(id, QStringLiteral("Renamed"));
    QVERIFY(workspaceError.isError());
    QCOMPARE(workspaceError.getMessage(), QStringLiteral("The workspace does not exist."));
    QVERIFY(firstWorkspaces.renameWorkspace(id, QStringLiteral("Thesis")).isError());

    // The last session is not renamed (no change, as before), a deleted one is an error
    QVERIFY(!firstWorkspaces.saveLastSession({ { QStringLiteral("/a/b.pdf"), 1 } }).isError());
    const qint64 sessionId = firstWorkspaces.getLastSession()->id;
    QVERIFY(!firstWorkspaces.renameWorkspace(sessionId, QStringLiteral("Session")).isError());
    QCOMPARE(firstWorkspaces.getLastSession()->name, secondWorkspaces.getLastSession()->name);
    QVERIFY(!secondWorkspaces.deleteLastSession().isError());
    QVERIFY(firstWorkspaces.renameWorkspace(sessionId, QStringLiteral("Session")).isError());

    pdf::PDFRecentRepository firstRecent(&first, nullptr);
    pdf::PDFRecentRepository secondRecent(&second, nullptr);
    qint64 locationId = 0;
    QVERIFY(!firstRecent.addPinnedLocation(QDir::fromNativeSeparators(directory.path()), QStringLiteral("Folder"), &locationId).isError());
    QVERIFY(!firstRecent.renamePinnedLocation(locationId, QStringLiteral("Folder")).isError());
    QVERIFY(!secondRecent.removePinnedLocation(locationId).isError());
    const pdf::PDFDatabaseError locationError = firstRecent.renamePinnedLocation(locationId, QStringLiteral("Renamed"));
    QVERIFY(locationError.isError());
    QCOMPARE(locationError.getMessage(), QStringLiteral("The pinned location does not exist."));

    first.close();
    second.close();
}

/// Czech names of the built-in collections (the names of the user interface are
/// compared with a new name, step 6)
class AppDataCollectionTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (qstrcmp(context, pdf::PDFAppBuiltinToolCollections::TRANSLATION_CONTEXT) != 0 || !sourceText)
        {
            return QString();
        }
        if (qstrcmp(sourceText, "Prepare for sharing") == 0)
        {
            return QString::fromUtf8("Připravit ke sdílení");
        }
        if (qstrcmp(sourceText, "Fix a scan") == 0)
        {
            return QString::fromUtf8("Opravit sken");
        }
        return QString();
    }
};

void AppDataTest::toolCollectionRepository()
{
    // Step 6: custom collections, overrides of the built-in collections, Reset to
    // default, Add to collection - every write one transaction
    DataFixture fixture;
    pdf::PDFToolCollectionRepository repository = fixture.collections(1000);
    QVERIFY(repository.getCollections().empty());
    QCOMPARE(repository.getCustomCollectionCount(), 0);

    // The merged collections: the seven built-in collections of 272:12821
    std::vector<pdf::PDFAppToolCollection> merged = pdf::PDFAppToolCollections::read(fixture.database.get());
    QCOMPARE(merged.size(), size_t(7));
    QCOMPARE(merged.front().routeId, QStringLiteral("prepare-for-sharing"));
    QVERIFY(merged.front().isBuiltin && !merged.front().isOverridden && merged.front().rowId == 0);

    // Create a custom collection
    qint64 id = 0;
    bool isDuplicate = true;
    bool isLimit = true;
    QVERIFY(!repository.createCollection(QStringLiteral("  Monthly invoices "), QStringLiteral("folder"),
                                         { QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }, &id, &isDuplicate, &isLimit).isError());
    QVERIFY(id > 0);
    QVERIFY(!isDuplicate && !isLimit);
    std::optional<pdf::PDFToolCollectionRow> row = repository.getCollection(id);
    QVERIFY(row);
    QCOMPARE(row->name, QStringLiteral("Monthly invoices"));
    QCOMPARE(row->iconName, QStringLiteral("folder"));
    QCOMPARE(row->modified, qint64(1000));
    QCOMPARE(row->toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }));
    QVERIFY(!row->isBuiltin());

    // Save of the custom collection: name, icon and the order of the tools replaced
    pdf::PDFToolCollectionDraft draft;
    draft.id = id;
    draft.name = QStringLiteral("Invoices");
    draft.iconName = QStringLiteral("zap");
    draft.toolIds = { QStringLiteral("optimize.compress"), QStringLiteral("protect.password"), QStringLiteral("pages.merge") };
    qint64 savedId = 0;
    QVERIFY(!fixture.collections(2000).saveCollection(draft, &savedId).isError());
    QCOMPARE(savedId, id);
    row = repository.getCollection(id);
    QCOMPARE(row->name, QStringLiteral("Invoices"));
    QCOMPARE(row->iconName, QStringLiteral("zap"));
    QCOMPARE(row->modified, qint64(2000));
    QCOMPARE(row->toolIds, draft.toolIds);

    // An icon outside of the chooser is the default icon (NULL)
    draft.iconName = QStringLiteral("book-open");
    QVERIFY(!repository.saveCollection(draft).isError());
    QVERIFY(repository.getCollection(id)->iconName.isEmpty());
    merged = pdf::PDFAppToolCollections::read(fixture.database.get());
    QCOMPARE(merged.size(), size_t(8));
    QCOMPARE(merged.back().routeId, QStringLiteral("custom-%1").arg(id));
    QCOMPARE(merged.back().iconName, QStringLiteral("folder"));

    // Refused: an empty name, no tool, an unknown / duplicate tool, an unknown built-in collection
    pdf::PDFToolCollectionDraft invalid = draft;
    invalid.name = QStringLiteral(" \t ");
    QVERIFY(repository.saveCollection(invalid).isError());
    invalid = draft;
    invalid.toolIds.clear();
    QVERIFY(repository.saveCollection(invalid).isError());
    invalid.toolIds = { QStringLiteral("pages.merge"), QStringLiteral("pages.unknown") };
    QVERIFY(repository.saveCollection(invalid).isError());
    invalid.toolIds = { QStringLiteral("pages.merge"), QStringLiteral("pages.merge") };
    QVERIFY(repository.saveCollection(invalid).isError());
    invalid = draft;
    invalid.builtinId = QStringLiteral("not-builtin");
    QVERIFY(repository.saveCollection(invalid).isError());
    QCOMPARE(repository.getCollection(id)->toolIds, draft.toolIds);

    // A built-in collection: the first change creates its override
    QVERIFY(!repository.getBuiltinOverride(QStringLiteral("fix-a-scan")));
    QVERIFY(!repository.addTool(0, QStringLiteral("fix-a-scan"), QStringLiteral("convert.ocr")).isError());
    std::optional<pdf::PDFToolCollectionRow> override = repository.getBuiltinOverride(QStringLiteral("fix-a-scan"));
    QVERIFY(override);
    QVERIFY(override->name.isEmpty());          // not renamed
    QVERIFY(override->iconName.isEmpty());      // the default icon
    QCOMPARE(override->toolIds, QStringList({ QStringLiteral("pages.rotate"), QStringLiteral("pages.crop"), QStringLiteral("pages.interleave"),
                                               QStringLiteral("pages.remove"), QStringLiteral("convert.ocr") }));
    merged = pdf::PDFAppToolCollections::read(fixture.database.get());
    const pdf::PDFAppToolCollection* fixAScan = pdf::PDFAppToolCollections::find(merged, QStringLiteral("fix-a-scan"));
    QVERIFY(fixAScan && fixAScan->isOverridden && fixAScan->rowId == override->id);
    QCOMPARE(fixAScan->name, QStringLiteral("Fix a scan"));
    QCOMPARE(fixAScan->iconName, QStringLiteral("rotate-cw"));
    QCOMPARE(fixAScan->toolIds, override->toolIds);

    // A tool, which is there, is no write; the last tool is not removed
    QVERIFY(!repository.addTool(0, QStringLiteral("fix-a-scan"), QStringLiteral("convert.ocr")).isError());
    QVERIFY(!repository.setToolIncluded(id, QString(), QStringLiteral("pages.merge"), false).isError());
    QVERIFY(!repository.setToolIncluded(id, QString(), QStringLiteral("protect.password"), false).isError());
    QVERIFY(repository.setToolIncluded(id, QString(), QStringLiteral("optimize.compress"), false).isError());
    QCOMPARE(repository.getCollection(id)->toolIds, QStringList({ QStringLiteral("optimize.compress") }));
    QVERIFY(repository.addTool(id, QString(), QStringLiteral("unknown.tool")).isError());
    QVERIFY(repository.addTool(id + 100, QString(), QStringLiteral("pages.merge")).isError());

    // Save of a built-in collection: renamed, own icon
    pdf::PDFToolCollectionDraft builtinDraft;
    builtinDraft.builtinId = QStringLiteral("fix-a-scan");
    builtinDraft.name = QStringLiteral("My scans");
    builtinDraft.iconName = QStringLiteral("camera");
    builtinDraft.toolIds = { QStringLiteral("pages.rotate") };
    QVERIFY(!repository.saveCollection(builtinDraft, &savedId).isError());
    QCOMPARE(savedId, override->id);
    override = repository.getBuiltinOverride(QStringLiteral("fix-a-scan"));
    QCOMPARE(override->name, QStringLiteral("My scans"));
    QCOMPARE(override->iconName, QStringLiteral("camera"));

    // The built-in name is stored as "not renamed", the built-in definition removes the override
    builtinDraft.name = QStringLiteral("Fix a scan");
    builtinDraft.iconName = QStringLiteral("rotate-cw");
    QVERIFY(!repository.saveCollection(builtinDraft).isError());
    QVERIFY(repository.getBuiltinOverride(QStringLiteral("fix-a-scan"))->name.isEmpty());
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM tool_collection WHERE builtin_id = 'fix-a-scan' AND name = 'Fix a scan' AND icon IS NULL"), qint64(1));
    builtinDraft.toolIds = { QStringLiteral("pages.rotate"), QStringLiteral("pages.crop"), QStringLiteral("pages.interleave"), QStringLiteral("pages.remove") };
    QVERIFY(!repository.saveCollection(builtinDraft).isError());
    QVERIFY(!repository.getBuiltinOverride(QStringLiteral("fix-a-scan")));

    // Reset to default deletes the override and its items; a built-in collection cannot be deleted
    QVERIFY(!repository.addTool(0, QStringLiteral("extract-content"), QStringLiteral("convert.ocr")).isError());
    const qint64 overrideId = repository.getBuiltinOverride(QStringLiteral("extract-content"))->id;
    QVERIFY(repository.deleteCollection(overrideId).isError());
    QVERIFY(!repository.resetBuiltin(QStringLiteral("extract-content")).isError());
    QVERIFY(!repository.getBuiltinOverride(QStringLiteral("extract-content")));
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM tool_collection_item WHERE collection_id NOT IN (SELECT id FROM tool_collection)"), qint64(0));
    QVERIFY(!repository.resetBuiltin(QStringLiteral("extract-content")).isError());
    QVERIFY(repository.resetBuiltin(QStringLiteral("unknown")).isError());

    // Delete a custom collection with its items; a missing one is an error
    QVERIFY(!repository.deleteCollection(id).isError());
    QVERIFY(!repository.getCollection(id));
    QVERIFY(repository.deleteCollection(id).isError());
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM tool_collection_item"), qint64(0));

    // A saved collection, which another instance has deleted, is not created again by Save
    QVERIFY(repository.saveCollection(draft).isError());

    // One transaction: a failed write of the items leaves nothing
    QVERIFY(AppDataFault::abortWrites(fixture.database.get(), QStringLiteral("INSERT"), QStringLiteral("tool_collection_item")));
    QVERIFY(repository.createCollection(QStringLiteral("Broken"), QString(), { QStringLiteral("pages.merge") }).isError());
    QVERIFY(repository.addTool(0, QStringLiteral("sign-a-contract"), QStringLiteral("pages.merge")).isError());
    QVERIFY(AppDataFault::clear(fixture.database.get()));
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM tool_collection"), qint64(0));

    // The limit of the custom collections
    for (int i = 0; i < pdf::PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS; ++i)
    {
        QVERIFY(!repository.createCollection(QStringLiteral("Collection %1").arg(i), QString(), { QStringLiteral("pages.merge") }).isError());
    }
    isLimit = false;
    QVERIFY(repository.createCollection(QStringLiteral("One more"), QString(), { QStringLiteral("pages.merge") }, nullptr, nullptr, &isLimit).isError());
    QVERIFY(isLimit);
    QCOMPARE(repository.getCustomCollectionCount(), pdf::PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS);

    // Notifications of the topics of the collections
    pdf::PDFDatabaseNotifier notifier(fixture.database.get());
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    QVERIFY(!repository.addTool(0, QStringLiteral("prepare-for-print"), QStringLiteral("pages.merge")).isError());
    QTRY_VERIFY(!spy.isEmpty());
    QStringList topics;
    for (const QList<QVariant>& arguments : std::as_const(spy))
    {
        topics << arguments.value(0).toString();
    }
    QVERIFY(topics.contains(pdf::PDFToolCollectionRepository::getCollectionTopic()));
    QVERIFY(topics.contains(pdf::PDFToolCollectionRepository::getItemTopic()));
}

void AppDataTest::toolCollectionNames()
{
    // Names are unique without case (also of other than ASCII letters) against the
    // custom collections and the built-in names in English and in the language of
    // the user interface
    DataFixture fixture;
    pdf::PDFToolCollectionRepository repository = fixture.collections();
    const QStringList tools = { QStringLiteral("pages.merge") };

    qint64 id = 0;
    QVERIFY(!repository.createCollection(QString::fromUtf8("Účty"), QString(), tools, &id).isError());
    bool isDuplicate = false;
    QVERIFY(repository.createCollection(QString::fromUtf8("úČTY"), QString(), tools, nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);
    QVERIFY(repository.findByName(QString::fromUtf8("ÚČTY")));
    QCOMPARE(repository.findByName(QString::fromUtf8("účty"))->id, id);
    QVERIFY(!repository.findByName(QString::fromUtf8("účty"), id));

    // The built-in names in English
    isDuplicate = false;
    QVERIFY(repository.createCollection(QStringLiteral("PREPARE FOR SHARING"), QString(), tools, nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);
    QCOMPARE(repository.findByName(QStringLiteral("prepare for sharing"))->builtinId, QStringLiteral("prepare-for-sharing"));

    // A renamed built-in collection: its new name and its built-in names are taken
    pdf::PDFToolCollectionDraft draft;
    draft.builtinId = QStringLiteral("fix-a-scan");
    draft.name = QStringLiteral("Scans");
    draft.toolIds = tools;
    QVERIFY(!repository.saveCollection(draft).isError());
    QVERIFY(repository.createCollection(QStringLiteral("scans"), QString(), tools).isError());
    QVERIFY(repository.createCollection(QStringLiteral("fix a scan"), QString(), tools).isError());

    // The edited collection is not compared with itself
    draft.name = QStringLiteral("SCANS");
    QVERIFY(!repository.saveCollection(draft).isError());
    pdf::PDFToolCollectionDraft custom;
    custom.id = id;
    custom.name = QString::fromUtf8("ÚČTY");
    custom.toolIds = tools;
    QVERIFY(!repository.saveCollection(custom).isError());
    QCOMPARE(repository.getCollection(id)->name, QString::fromUtf8("ÚČTY"));

    // The names of the language of the user interface
    AppDataCollectionTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    isDuplicate = false;
    QVERIFY(repository.createCollection(QString::fromUtf8("připravit KE sdílení"), QString(), tools, nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);
    std::vector<pdf::PDFAppToolCollection> merged = pdf::PDFAppToolCollections::read(fixture.database.get());
    QCOMPARE(merged.front().name, QString::fromUtf8("Připravit ke sdílení"));
    QCOMPARE(pdf::PDFAppToolCollections::find(merged, QStringLiteral("fix-a-scan"))->name, QStringLiteral("SCANS"));

    // A built-in collection saved with its translated name is not renamed
    draft.name = QString::fromUtf8("Opravit sken");
    QVERIFY(!repository.saveCollection(draft).isError());
    QVERIFY(repository.getBuiltinOverride(QStringLiteral("fix-a-scan"))->name.isEmpty());
    QCOMPARE(AppDataTestHelper::queryInteger(fixture.database.get(), "SELECT COUNT(*) FROM tool_collection WHERE builtin_id = 'fix-a-scan' AND name = 'Fix a scan'"), qint64(1));
    QVERIFY(QCoreApplication::removeTranslator(&translator));

    // Control and format characters are removed, the joiners stay, too long names are refused
    const QString bidi = QStringLiteral("Re") + QChar(0x202E) + QStringLiteral("ports") + QChar(0x200B) + QChar(0x0007);
    QCOMPARE(pdf::PDFToolCollectionRepository::normalizeName(bidi), QStringLiteral("Reports"));
    const QString family = QString::fromUtf8("\xF0\x9F\x91\xA8") + QChar(0x200D) + QString::fromUtf8("\xF0\x9F\x91\xA9");
    QCOMPARE(pdf::PDFToolCollectionRepository::normalizeName(family), family);
    QVERIFY(pdf::PDFToolCollectionRepository::normalizeName(QString(pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH + 1, QLatin1Char('x'))).isEmpty());
    QVERIFY(pdf::PDFToolCollectionRepository::normalizeName(QString(1024 * 1024, QLatin1Char('x'))).isEmpty());
    QVERIFY(pdf::PDFToolCollectionRepository::normalizeName(QString(QChar(0x202E))).isEmpty());
    QCOMPARE(pdf::PDFToolCollectionRepository::normalizeName(QString(pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH, QLatin1Char('y'))).size(),
             qsizetype(pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH));
    QVERIFY(!repository.createCollection(bidi, QString(), tools).isError());
    QVERIFY(repository.findByName(QStringLiteral("reports")));

    // Format characters outside of the BMP (the tag characters, the musical format
    // controls) never make a name different: a surrogate pair is checked as one code point
    const QString tagged = QStringLiteral("Reports") + QString::fromUcs4(U"\U000E0041\U000E0001\U0001D173");
    QCOMPARE(pdf::PDFToolCollectionRepository::normalizeName(tagged), QStringLiteral("Reports"));
    QCOMPARE(pdf::PDFToolCollectionRepository::sanitizeName(tagged), QStringLiteral("Reports"));
    isDuplicate = false;
    QVERIFY(repository.createCollection(tagged, QString(), tools, nullptr, &isDuplicate).isError());
    QVERIFY(isDuplicate);
    const QString emoji = QString::fromUcs4(U"\U0001F4C4 Reports");
    QCOMPARE(pdf::PDFToolCollectionRepository::normalizeName(emoji), emoji);
    QCOMPARE(pdf::PDFToolCollectionRepository::normalizeName(QStringLiteral("A") + QChar(0xD800) + QStringLiteral("B") + QChar(0xDC00)), QStringLiteral("AB"));

    // Review RS6: names, which look the same, are the same name - a no-break space,
    // a run of spaces, a joiner inside of a word, full-width letters, a decomposed accent
    for (const QString& lookalike : { QStringLiteral("Prepare for sharing"), QStringLiteral("Prepare  for sharing"), QStringLiteral("Pre‍pare for sharing"),
                                      QStringLiteral("Ｐrepare for sharing"), QStringLiteral("Účty"), QStringLiteral("Rep‌orts") })
    {
        isDuplicate = false;
        QVERIFY2(repository.createCollection(lookalike, QString(), tools, nullptr, &isDuplicate).isError(), qPrintable(lookalike));
        QVERIFY(isDuplicate);
    }
    QVERIFY(!repository.createCollection(QStringLiteral("Prepare for sharing 2"), QString(), tools).isError());
}

void AppDataTest::toolCollectionEdges()
{
    // Edges of PDFToolCollectionRepository (review RT6): invalid ids and names, the
    // order of more overrides of the built-in collections, the excluded built-in
    // collection of the name check, an unknown built-in collection
    DataFixture fixture;
    pdf::PDFToolCollectionRepository repository = fixture.collections(10);
    QVERIFY(!repository.getCollection(0));
    QVERIFY(!repository.getCollection(-5));
    QVERIFY(!repository.getBuiltinOverride(QStringLiteral("no-such-collection")));
    QVERIFY(!repository.getBuiltinOverride(QString()));
    QVERIFY(!repository.findByName(QString()));
    QVERIFY(!repository.findByName(QStringLiteral("   ")));

    // Overrides in the order of the built-in collections, also when written in another order
    QVERIFY(!repository.addTool(0, QStringLiteral("extract-content"), QStringLiteral("pages.split")).isError());
    QVERIFY(!repository.addTool(0, QStringLiteral("prepare-for-sharing"), QStringLiteral("pages.split")).isError());
    QVERIFY(!repository.addTool(0, QStringLiteral("fix-a-scan"), QStringLiteral("pages.split")).isError());
    QStringList order;
    for (const pdf::PDFToolCollectionRow& row : repository.getCollections())
    {
        order << row.builtinId;
    }
    QCOMPARE(order, QStringList({ QStringLiteral("prepare-for-sharing"), QStringLiteral("fix-a-scan"), QStringLiteral("extract-content") }));

    // The name of a built-in collection: found, except for the collection itself
    const pdf::PDFAppBuiltinToolCollection* scan = pdf::PDFAppBuiltinToolCollections::findCollection(QStringLiteral("fix-a-scan"));
    QVERIFY(scan);
    const std::optional<pdf::PDFToolCollectionRow> found = repository.findByName(scan->getSourceName().toUpper());
    QVERIFY(found);
    QCOMPARE(found->builtinId, scan->getId());
    QVERIFY(!repository.findByName(scan->getSourceName(), 0, scan->getId()));

    // An unknown built-in collection, an unknown tool, a custom collection, which does not exist
    QVERIFY(repository.setToolIncluded(0, QStringLiteral("no-such-collection"), QStringLiteral("pages.split"), true).isError());
    QVERIFY(repository.setToolIncluded(0, QString(), QStringLiteral("unknown.tool"), true).isError());
    QVERIFY(repository.setToolIncluded(424242, QString(), QStringLiteral("pages.split"), true).isError());
    QVERIFY(repository.resetBuiltin(QStringLiteral("no-such-collection")).isError());
    QVERIFY(repository.deleteCollection(424242).isError());
    pdf::PDFToolCollectionDraft draft;
    draft.builtinId = QStringLiteral("no-such-collection");
    draft.name = QStringLiteral("Name");
    draft.toolIds = { QStringLiteral("pages.split") };
    QVERIFY(repository.saveCollection(draft).isError());
    draft.builtinId.clear();
    draft.id = 424242;
    QVERIFY(repository.saveCollection(draft).isError());

    // The merge of rows, which did not come from the repository (an icon outside of
    // the allowlist, an override with an icon of another version): the default icons
    pdf::PDFToolCollectionRow custom;
    custom.id = 77;
    custom.name = QStringLiteral("Hostile");
    custom.iconName = QStringLiteral("evil-icon");
    custom.toolIds = { QStringLiteral("pages.merge") };
    pdf::PDFToolCollectionRow override;
    override.id = 78;
    override.builtinId = QStringLiteral("fix-a-scan");
    override.iconName = QStringLiteral("../../icon");
    override.toolIds = { QStringLiteral("pages.crop") };
    const std::vector<pdf::PDFAppToolCollection> merged = pdf::PDFAppToolCollections::merge({ custom, override });
    const pdf::PDFAppToolCollection* hostile = pdf::PDFAppToolCollections::find(merged, QStringLiteral("custom-77"));
    QVERIFY(hostile);
    QCOMPARE(hostile->iconName, pdf::PDFAppBuiltinToolCollections::getDefaultIconName());
    const pdf::PDFAppToolCollection* overridden = pdf::PDFAppToolCollections::find(merged, QStringLiteral("fix-a-scan"));
    QVERIFY(overridden && overridden->isOverridden);
    QCOMPARE(overridden->iconName, scan->getIconName());

    // Rows of ids, which no route can name (0, negative - written explicitly by a hostile writer): skipped
    const size_t count = repository.getCollections().size();
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT INTO tool_collection(id, builtin_id, name, icon, modified) VALUES (0, NULL, 'Zero', NULL, 1), (-3, NULL, 'Negative', NULL, 1)"));
    QCOMPARE(repository.getCollections().size(), count);
    QVERIFY(!repository.findByName(QStringLiteral("Zero")));
}

void AppDataTest::toolCollectionHostileRows()
{
    // Rows of another version or of a hostile writer: never a crash, bounded values
    DataFixture fixture;
    pdf::PDFDatabase* database = fixture.database.get();
    QVERIFY(AppDataTestHelper::execute(database,
        "INSERT INTO tool_collection(id, builtin_id, name, icon, modified) VALUES "
        "(1, 'unknown-builtin', 'Foreign', 'mail', 5), "                                   // unknown built-in: skipped
        "(2, NULL, zeroblob(4), 'evil', 'yesterday'), "                                    // name of another type, icon not allowed, modified text
        "(3, NULL, '', NULL, -7), "                                                         // empty name
        "(4, NULL, replace(hex(zeroblob(32768)), '0', 'n'), 'star', 9), "                               // 64 kB name
        "(5, NULL, X'41424344', 'zap', 1), "                                                // blob name
        "(6, 42, 'Integer builtin', NULL, 1), "                                             // builtin_id of another type: skipped
        "(7, 'prepare-for-sharing', 'Prepare for sharing', 'not-an-icon', 3), "             // override, icon not allowed
        "(8, NULL, 'Unknown tools', NULL, 4)"));
    QVERIFY(AppDataTestHelper::execute(database,
        "INSERT INTO tool_collection_item(collection_id, tool_id, position) VALUES "
        "(2, 'optimize.compress', 2), (2, 'pages.merge', 1), (2, 'unknown.tool', 0), (2, 42, 3), "
        "(3, 'review.compare', 5), (3, 'pages.split', 5), (3, 'pages.merge', 5), "         // equal positions: the order of the catalog
        "(4, 'pages.rotate', 'first'), (4, 'pages.crop', 0), "                             // a text position goes last
        "(7, 'protect.password', 0), "
        "(8, 'nothing.known', 0), (8, 'other.unknown', 1)"));

    pdf::PDFToolCollectionRepository repository = fixture.collections();
    const std::vector<pdf::PDFToolCollectionRow> rows = repository.getCollections();
    QStringList ids;
    for (const pdf::PDFToolCollectionRow& row : rows)
    {
        ids << QString::number(row.id);
    }
    QCOMPARE(ids, QStringList({ QStringLiteral("7"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("8") }));

    QCOMPARE(rows[0].builtinId, QStringLiteral("prepare-for-sharing"));
    QVERIFY(rows[0].name.isEmpty());
    QVERIFY(rows[0].iconName.isEmpty());
    QCOMPARE(rows[1].name, QStringLiteral("Untitled collection"));
    QVERIFY(rows[1].iconName.isEmpty());
    QCOMPARE(rows[1].modified, qint64(0));
    QCOMPARE(rows[1].toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress") }));
    QCOMPARE(rows[2].name, QStringLiteral("Untitled collection"));
    QCOMPARE(rows[2].modified, qint64(0));
    QCOMPARE(rows[2].toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("pages.split"), QStringLiteral("review.compare") }));
    QCOMPARE(rows[3].name.size(), qsizetype(pdf::PDFRepositoryHelper::MAXIMUM_NAME_LENGTH));
    QCOMPARE(rows[3].iconName, QStringLiteral("star"));
    QCOMPARE(rows[3].toolIds, QStringList({ QStringLiteral("pages.crop"), QStringLiteral("pages.rotate") }));
    QCOMPARE(rows[4].name, QStringLiteral("Untitled collection"));
    QVERIFY(rows[5].toolIds.isEmpty());

    // The merged collections: an icon of the chooser always, "0 tools"
    const std::vector<pdf::PDFAppToolCollection> merged = pdf::PDFAppToolCollections::merge(rows);
    QCOMPARE(merged.size(), size_t(12));
    QCOMPARE(merged[0].iconName, QStringLiteral("mail"));
    QCOMPARE(merged[0].name, QStringLiteral("Prepare for sharing"));
    QCOMPARE(merged[0].toolIds, QStringList({ QStringLiteral("protect.password") }));
    for (const pdf::PDFAppToolCollection& collection : merged)
    {
        QVERIFY(pdf::PDFAppBuiltinToolCollections::isValidIconName(collection.iconName));
        QVERIFY(!collection.name.isEmpty());
    }
    QCOMPARE(merged.back().routeId, QStringLiteral("custom-8"));
    QVERIFY(merged.back().toolIds.isEmpty());

    // Writes over hostile rows work, the names of the hostile rows are compared
    QVERIFY(!repository.addTool(2, QString(), QStringLiteral("review.measure")).isError());
    QCOMPARE(repository.getCollection(2)->toolIds, QStringList({ QStringLiteral("pages.merge"), QStringLiteral("optimize.compress"), QStringLiteral("review.measure") }));

    // The items of the tools of a newer version (the same schema, the same file) stay
    // over the writes of this version: Add to collection, Remove, Save
    QVERIFY(!repository.setToolIncluded(8, QString(), QStringLiteral("pages.merge"), true).isError());
    pdf::PDFToolCollectionDraft unknownDraft;
    unknownDraft.id = 8;
    unknownDraft.name = QStringLiteral("Unknown tools");
    unknownDraft.toolIds = { QStringLiteral("pages.split"), QStringLiteral("pages.merge") };
    QVERIFY(!repository.saveCollection(unknownDraft).isError());
    QVERIFY(!repository.setToolIncluded(8, QString(), QStringLiteral("pages.merge"), false).isError());
    QCOMPARE(repository.getCollection(8)->toolIds, QStringList({ QStringLiteral("pages.split") }));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_collection_item WHERE collection_id = 8 AND tool_id IN ('nothing.known', 'other.unknown')"), qint64(2));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_collection_item WHERE collection_id = 2 AND tool_id = 'unknown.tool'"), qint64(1));
    QVERIFY(repository.createCollection(QStringLiteral("untitled COLLECTION"), QString(), { QStringLiteral("pages.merge") }).isError());
    QVERIFY(!repository.deleteCollection(4).isError());
    QVERIFY(repository.deleteCollection(1).isError());
    QVERIFY(repository.deleteCollection(6).isError());

    // Route ids of the custom collections: a bounded parser
    QCOMPARE(pdf::PDFAppToolCollections::parseCustomRouteId(QStringLiteral("custom-12")), qint64(12));
    QCOMPARE(pdf::PDFAppToolCollections::parseCustomRouteId(QStringLiteral("custom-999999999999999999")), qint64(999999999999999999));
    for (const char* invalid : { "custom-0", "custom-012", "custom-", "custom--1", "custom-+1", "custom-1a", "custom-9999999999999999999", "Custom-1", "custom-1 ", "prepare-for-sharing", "" })
    {
        QCOMPARE(pdf::PDFAppToolCollections::parseCustomRouteId(QString::fromLatin1(invalid)), qint64(0));
    }
    QVERIFY(pdf::PDFAppToolCollections::isValidRouteId(QStringLiteral("prepare-for-sharing")));
    QVERIFY(pdf::PDFAppToolCollections::isValidRouteId(QStringLiteral("custom-99999999999")));
    QVERIFY(!pdf::PDFAppToolCollections::isValidRouteId(QStringLiteral("custom-0")));
    QVERIFY(!pdf::PDFAppToolCollections::isValidRouteId(QStringLiteral("prepare")));
}

void AppDataTest::toolCollectionTwoProcesses()
{
    // Two processes create a collection of the same name at once: one of them wins,
    // the other one is refused as a duplicate (the name check is inside of BEGIN IMMEDIATE)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    {
        pdf::PDFDatabase database;
        database.open(AppDataTestHelper::createFileOptions(databasePath));
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        database.close();
    }

    const int factor = AppDataTestHelper::getTimeoutFactor();
    for (int round = 0; round < 3; ++round)
    {
        const QString name = QStringLiteral("Shared %1").arg(round);
        QProcess first;
        QProcess second;
        first.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-collection"), databasePath, name });
        second.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-collection"), databasePath, name.toUpper() });
        QVERIFY(first.waitForFinished(30000 * factor));
        QVERIFY(second.waitForFinished(30000 * factor));
        QCOMPARE(first.exitStatus(), QProcess::NormalExit);
        QCOMPARE(second.exitStatus(), QProcess::NormalExit);

        // 0 = created, 6 = duplicate
        const QList<int> codes = { first.exitCode(), second.exitCode() };
        QVERIFY2(codes.contains(0) && codes.contains(6), qPrintable(QStringLiteral("%1 %2").arg(codes[0]).arg(codes[1])));
    }

    pdf::PDFDatabase database;
    database.open(AppDataTestHelper::createFileOptions(databasePath));
    QCOMPARE(pdf::PDFToolCollectionRepository(&database).getCustomCollectionCount(), 3);
    database.close();
}

void AppDataTest::toolUsageClear()
{
    // Clear of Recent tools (Q2): only the use is deleted - the pins, the collections
    // and the pairs stay
    DataFixture fixture;
    QVERIFY(!fixture.tools(100).recordUsage(QStringLiteral("pages.merge")).isError());
    QVERIFY(!fixture.tools(200).recordUsage(QStringLiteral("optimize.compress")).isError());
    QVERIFY(!fixture.tools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(!fixture.collections().createCollection(QStringLiteral("Mine"), QString(), { QStringLiteral("pages.merge") }).isError());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(), "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES ('pages.merge', 'optimize.compress', 3, 1)"));

    pdf::PDFDatabaseNotifier notifier(fixture.database.get());
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    QVERIFY(!fixture.tools().clearUsage().isError());
    QVERIFY(fixture.tools().getUsage().empty());
    QCOMPARE(fixture.tools().getPinnedTools(), QStringList({ QStringLiteral("pages.merge") }));
    QCOMPARE(fixture.collections().getCustomCollectionCount(), 1);
    QCOMPARE(fixture.tools().getPairs().size(), size_t(1));
    QTRY_VERIFY(!spy.isEmpty());
    QCOMPARE(spy.last().value(0).toString(), pdf::PDFToolRepository::getUsageTopic());

    // A clear of an empty list, a failed clear keeps the rows
    QVERIFY(!fixture.tools().clearUsage().isError());
    QVERIFY(!fixture.tools(300).recordUsage(QStringLiteral("review.compare")).isError());
    QVERIFY(AppDataFault::abortWrites(fixture.database.get(), QStringLiteral("DELETE"), QStringLiteral("tool_usage")));
    QVERIFY(fixture.tools().clearUsage().isError());
    QVERIFY(AppDataFault::clear(fixture.database.get()));
    QCOMPARE(fixture.tools().getUsage().size(), size_t(1));

    // The state of the pages follows
    const pdf::PDFAppToolState state = pdf::PDFAppToolState::read(fixture.database.get(), 5);
    QCOMPARE(state.usage.size(), size_t(1));
    QCOMPARE(state.getLastUsed(QStringLiteral("review.compare")), qint64(300));
    QVERIFY(state.isPinned(QStringLiteral("pages.merge")));
    QCOMPARE(state.collections.size(), size_t(8));
    QVERIFY(state.isSuggestNextEnabled);
    QVERIFY(pdf::PDFAppToolState::isRelevantTopic(pdf::PDFToolRepository::getUsageTopic()));
    QVERIFY(pdf::PDFAppToolState::isRelevantTopic(pdf::PDFToolCollectionRepository::getItemTopic()));
    QVERIFY(pdf::PDFAppToolState::isRelevantTopic(pdf::PDFSettingsRepository::getTopic(), pdf::PDFSettingKeys::toolsSuggestNext()));
    QVERIFY(!pdf::PDFAppToolState::isRelevantTopic(pdf::PDFSettingsRepository::getTopic(), QStringLiteral("viewing.zoom")));
    QVERIFY(!pdf::PDFAppToolState::isRelevantTopic(QStringLiteral("recent_file")));
}

void AppDataTest::toolPairsRead()
{
    // Counters of the pairs for the suggestions: hostile rows skipped, filtered by the first tool
    DataFixture fixture;
    QVERIFY(fixture.tools().getPairs().empty());
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(),
        "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES "
        "('pages.merge', 'optimize.compress', 7, 100), "
        "('pages.merge', 'protect.password', 9, 50), "
        "('pages.merge', 'pages.merge', 4, 1), "                 // a tool with itself
        "('pages.merge', 'unknown.tool', 4, 1), "                // unknown tool
        "('optimize.compress', 'protect.password', 0, 1), "      // no counter
        "('optimize.compress', 'protect.sanitize', -3, 1), "     // negative counter
        "('optimize.compress', 'pages.split', 'many', 1), "      // text counter
        "('optimize.compress', 'pages.extract', 2, 'late'), "    // text time
        "('review.compare', 'review.measure', 1, 7)"));

    const std::vector<pdf::PDFDocumentToolPair> all = fixture.tools().getPairs();
    QCOMPARE(all.size(), size_t(4));
    QCOMPARE(all[0].toTool, QStringLiteral("protect.password"));
    QCOMPARE(all[0].count, qint64(9));
    QCOMPARE(all[1].toTool, QStringLiteral("optimize.compress"));
    QCOMPARE(all[1].lastUsed, qint64(100));

    const std::vector<pdf::PDFDocumentToolPair> compress = fixture.tools().getPairs({ QStringLiteral("optimize.compress") });
    QCOMPARE(compress.size(), size_t(1));
    QCOMPARE(compress[0].toTool, QStringLiteral("pages.extract"));
    QCOMPARE(compress[0].lastUsed, qint64(0));

    const std::vector<pdf::PDFDocumentToolPair> two = pdf::PDFAppToolState::readPairs(fixture.database.get(), { QStringLiteral("pages.merge"), QStringLiteral("review.compare") });
    QCOMPARE(two.size(), size_t(3));
    QVERIFY(pdf::PDFAppToolState::readPairs(nullptr, QStringList()).empty());

    // The pairs are the input of the suggestions of the collections editor
    pdf::PDFDocumentToolSuggestionInput input;
    input.fromTools = { QStringLiteral("pages.merge") };
    input.pairs = all;
    input.defaultSuccessors = QStringList();
    const QStringList suggestions = pdf::PDFDocumentToolSuggestions::suggest(input);
    QCOMPARE(suggestions, QStringList({ QStringLiteral("protect.password"), QStringLiteral("optimize.compress") }));

    // More rows of other tools than the limit with higher counters (another version,
    // a hostile writer) never hide the pairs of the requested tools
    QVERIFY(AppDataTestHelper::execute(fixture.database.get(),
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 10050) "
        "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) SELECT 'other.tool' || x, 'pages.split', 1000000, 1 FROM n"));
    const std::vector<pdf::PDFDocumentToolPair> merge = fixture.tools().getPairs({ QStringLiteral("pages.merge"), QStringLiteral("unknown.tool"), QStringLiteral("pages.merge") });
    QCOMPARE(merge.size(), size_t(2));
    QCOMPARE(merge[0].toTool, QStringLiteral("protect.password"));
    QCOMPARE(merge[1].toTool, QStringLiteral("optimize.compress"));
    QCOMPARE(fixture.tools().getPairs({ QStringLiteral("review.compare"), QStringLiteral("pages.merge") }).size(), size_t(3));
    QVERIFY(fixture.tools().getPairs({ QStringLiteral("unknown.tool") }).empty());
    QVERIFY(fixture.tools().getPairs().size() <= size_t(pdf::PDFToolRepository::MAXIMUM_PAIRS));
}

void AppDataTest::toolUsageRecording()
{
    // PDFDocumentToolUsage (step 6, package D): the use and the pairs of an accepted
    // run in one BEGIN IMMEDIATE, MCP not counted, the setting tools.suggestNext
    DataFixture fixture;
    pdf::PDFDatabase* database = fixture.database.get();
    pdf::PDFDocumentToolUsage usage(database);
    pdf::PDFDocumentToolAvailability availability;

    // The defaults of the catalog before the first pair
    QVERIFY(usage.isSuggestNextEnabled());
    QCOMPARE(usage.getSuggestions(QStringLiteral("pages.merge"), availability), QStringList({ QStringLiteral("edit.page-numbers"), QStringLiteral("protect.password"), QStringLiteral("optimize.compress") }));
    QVERIFY(usage.getSuggestions(QStringLiteral("unknown.tool"), availability).isEmpty());

    // A run without a previous tool: only the use
    usage.setCurrentTime(1000);
    QVERIFY(!usage.recordRun(QStringLiteral("pages.merge")).isError());
    QCOMPARE(fixture.tools().getUsage().size(), size_t(1));
    QCOMPARE(fixture.tools().getUsage().front().lastUsed, qint64(1000));
    QVERIFY(fixture.tools().getPairs().empty());
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(0));

    // The next tool over the result: the use, the pair with the inserted defaults and the
    // run of the history - notified in one commit
    pdf::PDFDatabaseNotifier notifier(database);
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    usage.setCurrentTime(2000);
    QVERIFY(!usage.recordRun(QStringLiteral("optimize.compress"), QStringLiteral("pages.merge")).isError());
    QCOMPARE(fixture.tools().getPairs({ QStringLiteral("pages.merge") }).size(), size_t(3));
    QCOMPARE(fixture.tools().getPairs().front().toTool, QStringLiteral("optimize.compress"));
    QCOMPARE(fixture.tools().getPairs().front().count, qint64(3));
    QCOMPARE(fixture.tools().getPairs().front().lastUsed, qint64(2000));
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(1));
    QCOMPARE(fixture.tools().getUsage().front().toolId, QStringLiteral("optimize.compress"));
    QCOMPARE(usage.getSuggestions(QStringLiteral("pages.merge"), availability), QStringList({ QStringLiteral("optimize.compress"), QStringLiteral("edit.page-numbers"), QStringLiteral("protect.password") }));
    QTRY_VERIFY(!spy.isEmpty());
    QStringList topics;
    for (const QList<QVariant>& arguments : std::as_const(spy))
    {
        topics << arguments.value(0).toString();
    }
    QVERIFY(topics.contains(pdf::PDFToolRepository::getUsageTopic()));
    QVERIFY(topics.contains(pdf::PDFToolRepository::getPairTopic()));

    // Unavailable tools are skipped
    availability.isDocumentLocked = true;
    QCOMPARE(usage.getSuggestions(QStringLiteral("pages.merge"), availability), QStringList());
    availability.isDocumentLocked = false;

    // MCP never counts; an unknown tool is refused; an unknown or the same previous tool records no pair
    const qint64 runs = fixture.tools().getHistoryRuns();
    QVERIFY(!usage.recordRun(QStringLiteral("protect.password"), QStringLiteral("pages.merge"), pdf::PDFDocumentToolUsage::Origin::Mcp).isError());
    QCOMPARE(fixture.tools().getUsage().size(), size_t(2));
    QVERIFY(usage.recordRun(QStringLiteral("unknown.tool"), QStringLiteral("pages.merge")).isError());
    QVERIFY(usage.recordRun(QString(100000, QLatin1Char('x'))).isError());
    QVERIFY(!usage.recordRun(QStringLiteral("protect.password"), QStringLiteral("unknown.tool")).isError());
    QVERIFY(!usage.recordRun(QStringLiteral("protect.password"), QStringLiteral("protect.password")).isError());
    QCOMPARE(fixture.tools().getUsage().size(), size_t(3));
    QCOMPARE(fixture.tools().getHistoryRuns(), runs);
    QCOMPARE(fixture.tools().getPairs().size(), size_t(3));

    // A failure of any part rolls the whole run back
    for (const char* table : { "tool_pair", "setting", "tool_usage" })
    {
        const std::vector<pdf::PDFToolUsage> usageBefore = fixture.tools().getUsage();
        const std::vector<pdf::PDFDocumentToolPair> pairsBefore = fixture.tools().getPairs();
        QVERIFY(AppDataFault::abortWrites(database, QStringLiteral("INSERT"), QString::fromLatin1(table)));
        QVERIFY2(usage.recordRun(QStringLiteral("edit.page-numbers"), QStringLiteral("pages.merge")).isError(), table);
        QVERIFY(AppDataFault::clear(database));
        QCOMPARE(fixture.tools().getUsage().size(), usageBefore.size());
        QCOMPARE(fixture.tools().getUsage().front().toolId, usageBefore.front().toolId);
        QCOMPARE(fixture.tools().getPairs(), pairsBefore);
        QCOMPARE(fixture.tools().getHistoryRuns(), runs);
    }
    QVERIFY(AppDataFault::failCommit(database, QStringLiteral("tool_pair")));
    QVERIFY(usage.recordRun(QStringLiteral("edit.page-numbers"), QStringLiteral("pages.merge")).isError());
    QVERIFY(AppDataFault::clear(database));
    QVERIFY(AppDataFault::abortWrites(database, QStringLiteral("DELETE"), QStringLiteral("tool_pair")));
    QVERIFY(usage.recordRun(QStringLiteral("edit.page-numbers"), QStringLiteral("pages.merge")).isError());
    QVERIFY(AppDataFault::clear(database));
    QVERIFY(AppDataFault::failReads(database, QStringLiteral("tool_pair")));
    QVERIFY(usage.recordRun(QStringLiteral("edit.page-numbers"), QStringLiteral("pages.merge")).isError());
    QVERIFY(AppDataTestHelper::execute(database, "DROP VIEW IF EXISTS temp.tool_pair"));
    QCOMPARE(fixture.tools().getHistoryRuns(), runs);
    QCOMPARE(fixture.tools().getPairs().size(), size_t(3));

    // The setting off: the use is recorded (Recent tools), the history is not; only the defaults are suggested
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::toolsSuggestNext(), false).isError());
    QVERIFY(!usage.isSuggestNextEnabled());
    const std::vector<pdf::PDFDocumentToolPair> pairsBefore = fixture.tools().getPairs();
    usage.setCurrentTime(3000);
    QVERIFY(!usage.recordRun(QStringLiteral("review.compare"), QStringLiteral("pages.merge")).isError());
    QCOMPARE(fixture.tools().getUsage().front().toolId, QStringLiteral("review.compare"));
    QCOMPARE(fixture.tools().getPairs(), pairsBefore);
    QCOMPARE(fixture.tools().getHistoryRuns(), runs);
    QCOMPARE(usage.getSuggestions(QStringLiteral("pages.merge"), availability), QStringList({ QStringLiteral("edit.page-numbers"), QStringLiteral("protect.password"), QStringLiteral("optimize.compress") }));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::toolsSuggestNext(), true).isError());

    // Hostile rows are dropped by the next write of the tool, a hostile counter of the
    // runs starts again; a pair with a tool, which this version does not know (a tool of
    // a newer version), is dropped too - no forward compatibility of the history (O25)
    QVERIFY(AppDataTestHelper::execute(database,
        "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES "
        "('review.compare', 'unknown.tool', 5, 1), ('review.compare', 'review.compare', 5, 1), "
        "('review.compare', 'review.report', 'many', 1), ('review.compare', 'review.measure', 3, 'late')"));
    QVERIFY(AppDataTestHelper::execute(database, "UPDATE setting SET value = 'abc' WHERE key = 'tools.historyRuns'"));
    QVERIFY(!usage.recordRun(QStringLiteral("review.report"), QStringLiteral("review.compare")).isError());
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_pair WHERE from_tool = 'review.compare'"), qint64(2));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_pair WHERE from_tool = 'review.compare' AND to_tool = 'review.compare'"), qint64(0));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT count FROM tool_pair WHERE from_tool = 'review.compare' AND to_tool = 'review.report'"), qint64(1));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT count FROM tool_pair WHERE from_tool = 'review.compare' AND to_tool = 'review.measure'"), qint64(3));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_pair WHERE to_tool = 'unknown.tool'"), qint64(0));
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(1));

    // Aging: the sum of the counters stays bounded
    for (int i = 0; i < 120; ++i)
    {
        QVERIFY(!usage.recordRun(QStringLiteral("review.report"), QStringLiteral("review.compare")).isError());
    }
    QVERIFY(AppDataTestHelper::queryInteger(database, "SELECT SUM(count) FROM tool_pair WHERE from_tool = 'review.compare'") <= pdf::PDFDocumentToolSuggestions::AGING_LIMIT + 1);
    QCOMPARE(fixture.tools().getHistoryRuns(), qint64(121));
    QCOMPARE(usage.getSuggestions(QStringLiteral("review.compare"), availability), QStringList({ QStringLiteral("review.report") }));

    // Clear history (Settings): the defaults again, the use stays
    QVERIFY(!fixture.tools().clearHistory().isError());
    QCOMPARE(usage.getSuggestions(QStringLiteral("pages.merge"), availability), QStringList({ QStringLiteral("edit.page-numbers"), QStringLiteral("protect.password"), QStringLiteral("optimize.compress") }));
    QVERIFY(!fixture.tools().getUsage().empty());

    // No database
    pdf::PDFDocumentToolUsage none(nullptr);
    QVERIFY(none.recordRun(QStringLiteral("pages.merge")).isError());
    QVERIFY(none.isSuggestNextEnabled());
    QCOMPARE(none.getSuggestions(QStringLiteral("optimize.compress"), availability), QStringList({ QStringLiteral("protect.password") }));
}

void AppDataTest::toolUsageTwoProcesses()
{
    // Three processes record runs at once: nothing is lost (BEGIN IMMEDIATE)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pdf4qt.db"));
    {
        pdf::PDFDatabase database;
        database.open(AppDataTestHelper::createFileOptions(databasePath));
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        database.close();
    }

    const int factor = AppDataTestHelper::getTimeoutFactor();
    constexpr int PROCESS_COUNT = 3;
    constexpr int RUN_COUNT = 15;
    std::vector<std::unique_ptr<QProcess>> processes;
    for (int i = 0; i < PROCESS_COUNT; ++i)
    {
        processes.push_back(std::make_unique<QProcess>());
        processes.back()->start(QCoreApplication::applicationFilePath(), { QStringLiteral("--appdata-toolusage"), databasePath, QString::number(RUN_COUNT) });
    }
    for (const std::unique_ptr<QProcess>& process : processes)
    {
        QVERIFY(process->waitForFinished(60000 * factor));
        QCOMPARE(process->exitStatus(), QProcess::NormalExit);
        QCOMPARE(process->exitCode(), 0);
    }

    // 45 runs: below the aging limit, Compare has no default successors
    pdf::PDFDatabase database;
    database.open(AppDataTestHelper::createFileOptions(databasePath));
    pdf::PDFToolRepository tools(&database);
    const std::vector<pdf::PDFDocumentToolPair> pairs = tools.getPairs();
    QCOMPARE(pairs.size(), size_t(1));
    QCOMPARE(pairs.front().count, qint64(PROCESS_COUNT * RUN_COUNT));
    QCOMPARE(tools.getUsage().front().count, qint64(PROCESS_COUNT * RUN_COUNT));
    QCOMPARE(tools.getHistoryRuns(), qint64(PROCESS_COUNT * RUN_COUNT));
    database.close();
}

void AppDataTest::toolParameterValues()
{
    // PDFToolParameterRepository (step 6, package D): the last values of the parameters
    // validated by their definitions, a Secret never stored, hostile rows = the defaults
    DataFixture fixture;
    pdf::PDFDatabase* database = fixture.database.get();
    pdf::PDFToolParameterRepository repository(database);
    pdftest::TestDocumentTool tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    const QString toolId = tool.getId();
    const QVariantMap defaults = pdf::PDFDocumentToolParameters::getDefaultValues(definitions);

    QCOMPARE(repository.getValues(toolId, definitions), defaults);

    QVariantMap values;
    values.insert(QStringLiteral("pages"), QStringLiteral("2-3"));
    values.insert(QStringLiteral("mode"), QStringLiteral("title"));
    values.insert(QStringLiteral("detailed"), true);
    values.insert(QStringLiteral("repeat"), 9);
    values.insert(QStringLiteral("scale"), 0.5);
    values.insert(QStringLiteral("title"), QStringLiteral("Annual report"));
    values.insert(QStringLiteral("color"), QStringLiteral("#102030"));
    values.insert(QStringLiteral("reportFile"), QStringLiteral("C:/a.txt"));
    values.insert(QStringLiteral("outputFolder"), QStringLiteral("C:/out"));
    values.insert(QStringLiteral("fileName"), QStringLiteral("{name}.txt"));
    values.insert(QStringLiteral("password"), QStringLiteral("secret"));

    pdf::PDFDatabaseNotifier notifier(database);
    QSignalSpy spy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    QVERIFY(!repository.setValues(toolId, definitions, values).isError());
    QTRY_VERIFY(!spy.isEmpty());
    QCOMPARE(spy.last().value(0).toString(), pdf::PDFToolParameterRepository::getTopic());
    QCOMPARE(spy.last().value(1).toString(), toolId);

    QVariantMap expected;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, values, &expected).empty());
    expected.insert(QStringLiteral("password"), QString());
    QCOMPARE(repository.getValues(toolId, definitions), expected);

    // The secret is never in the table, the types of SQLite are native
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_parameter WHERE name = 'password'"), qint64(0));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_parameter"), qint64(10));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT typeof(value) = 'integer' FROM tool_parameter WHERE name = 'repeat'"), qint64(1));
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT typeof(value) = 'real' FROM tool_parameter WHERE name = 'scale'"), qint64(1));
    const pdf::PDFDocumentToolParameterDefinition* secret = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("password"));
    QVERIFY(repository.setValue(toolId, *secret, QStringLiteral("secret")).isError());

    // A value equal to the default removes its row; one value alone
    const pdf::PDFDocumentToolParameterDefinition* repeat = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("repeat"));
    QVERIFY(!repository.setValue(toolId, *repeat, 1).isError());
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_parameter WHERE name = 'repeat'"), qint64(0));
    QVERIFY(!repository.setValue(toolId, *repeat, 42).isError());
    QCOMPARE(repository.getValues(toolId, definitions).value(QStringLiteral("repeat")), QVariant(qint64(42)));

    // Invalid input refuses the whole write
    QVariantMap invalid;
    invalid.insert(QStringLiteral("repeat"), 7);
    invalid.insert(QStringLiteral("color"), QStringLiteral("blue"));
    QVERIFY(repository.setValues(toolId, definitions, invalid).isError());
    QCOMPARE(repository.getValues(toolId, definitions).value(QStringLiteral("repeat")), QVariant(qint64(42)));
    invalid.clear();
    invalid.insert(QStringLiteral("unknown"), 1);
    QVERIFY(repository.setValues(toolId, definitions, invalid).isError());
    QVERIFY(repository.setValues(QStringLiteral("unknown.tool"), definitions, values).isError());
    QVERIFY(repository.setValues(toolId, { definitions.front(), definitions.front() }, QVariantMap()).isError());
    QCOMPARE(repository.getValues(QStringLiteral("unknown.tool"), definitions), defaults);

    // Hostile rows of another version or of another program
    QVERIFY(!repository.clearValues(toolId).isError());
    QVERIFY(AppDataTestHelper::execute(database,
        "INSERT INTO tool_parameter(tool_id, name, value) VALUES "
        "('review.statistics', 'repeat', 'abc'), ('review.statistics', 'detailed', 5), ('review.statistics', 'password', 'leaked'), "
        "('review.statistics', 'unknown', 1), ('review.statistics', 'pages', '{\"pages\":\"0\"}'), ('review.statistics', 'mode', 'hack'), "
        "('review.statistics', 'title', zeroblob(100)), ('review.statistics', 'reportFile', 12), ('review.statistics', 'scale', 1e308), "
        "('review.statistics', 'color', 'red'), ('review.statistics', 'fileName', 'a/b'), ('unknown.tool', 'repeat', 3), "
        "('review.statistics', 'outputFolder', printf('%.*c', 100000, 'x'))"));
    QCOMPARE(repository.getValues(toolId, definitions), defaults);

    // The next write removes the stored secret
    QVERIFY(!repository.setValues(toolId, definitions, QVariantMap()).isError());
    QCOMPARE(AppDataTestHelper::queryInteger(database, "SELECT COUNT(*) FROM tool_parameter WHERE name = 'password'"), qint64(0));

    // A write failure keeps the stored values
    QVERIFY(!repository.setValue(toolId, *repeat, 5).isError());
    QVERIFY(AppDataFault::abortWrites(database, QStringLiteral("INSERT"), QStringLiteral("tool_parameter")));
    values.insert(QStringLiteral("password"), QString());
    QVERIFY(repository.setValues(toolId, definitions, values).isError());
    QVERIFY(AppDataFault::clear(database));
    QCOMPARE(repository.getValues(toolId, definitions).value(QStringLiteral("repeat")), QVariant(qint64(5)));
    QCOMPARE(repository.getValues(toolId, definitions).value(QStringLiteral("mode")), QVariant(QStringLiteral("count")));

    // A failed commit keeps the stored values too
    QVERIFY(AppDataFault::failCommit(database, QStringLiteral("tool_parameter")));
    QVERIFY(repository.setValues(toolId, definitions, values).isError());
    QVERIFY(repository.clearValues(toolId).isError());
    QVERIFY(AppDataFault::clear(database));
    QVERIFY(AppDataFault::abortWrites(database, QStringLiteral("DELETE"), QStringLiteral("tool_parameter")));
    QVERIFY(repository.clearValues(toolId).isError());
    QVERIFY(AppDataFault::clear(database));
    QCOMPARE(repository.getValues(toolId, definitions).value(QStringLiteral("repeat")), QVariant(qint64(5)));

    // Invalid definitions read the defaults only
    QCOMPARE(repository.getValues(toolId, { definitions.front(), definitions.front() }), QVariantMap({ { definitions.front().getName(), definitions.front().getDefaultValue() } }));

    // Clear
    QVERIFY(!repository.clearValues(toolId).isError());
    QCOMPARE(repository.getValues(toolId, definitions), defaults);
    QVERIFY(repository.clearValues(QStringLiteral("unknown.tool")).isError());

    // No database
    pdf::PDFToolParameterRepository none(nullptr);
    QCOMPARE(none.getValues(toolId, definitions), defaults);
    QVERIFY(none.setValues(toolId, definitions, values).isError());
    QVERIFY(none.clearValues(toolId).isError());
}

int main(int argc, char* argv[])
{
    // Child process of the tests of two processes
    if (argc > 1 && QByteArray(argv[1]).startsWith("--appdata-"))
    {
        QCoreApplication application(argc, argv);
        return AppDataChild::run(application.arguments());
    }

    QApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppDataTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appdatatest.moc"
