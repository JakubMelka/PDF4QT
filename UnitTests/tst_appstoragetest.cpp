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

#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabaseschema.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfshortcutrepository.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>
#include <thread>

/// Helper functions of the test and of its child processes
class AppStorageTestHelper
{
public:
    AppStorageTestHelper() = delete;

    /// Multiplier of the time limits: PDF4QT_TEST_TIMEOUT_FACTOR, or 20 under valgrind
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

    /// Number of transactions of a writer process: PDF4QT_STORAGE_TEST_TRANSACTIONS
    static int getTransactionCount(int defaultCount)
    {
        bool ok = false;
        const int count = qEnvironmentVariableIntValue("PDF4QT_STORAGE_TEST_TRANSACTIONS", &ok);
        return (ok && count > 0) ? count : defaultCount;
    }

    static pdf::PDFDatabaseOptions createOptions(const QString& filePath)
    {
        pdf::PDFDatabaseOptions options;
        options.filePath = filePath;
        options.memoryName = QStringLiteral("/pdf4qt-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_memoryCounter);
        return options;
    }

    static qint64 queryInteger(pdf::PDFDatabase* database, const char* sql, qint64 defaultValue = -1)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection ? connection->queryInteger(sql, defaultValue) : defaultValue;
    }

    static bool execute(pdf::PDFDatabase* database, const char* sql)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection && !connection->execute(sql).isError();
    }

    static QByteArray fileHash(const QString& filePath)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            return QByteArray();
        }
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }

    /// Schema (sql of sqlite_master) of a database, sorted
    static QStringList getSchema(pdf::PDFDatabase* database)
    {
        QStringList schema;
        pdf::PDFDatabaseStatement statement(database->getConnection(), "SELECT type, name, sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name");
        while (statement.next())
        {
            schema << statement.getText(0) + QLatin1Char(' ') + statement.getText(1) + QLatin1Char(' ') + statement.getText(2);
        }
        return schema;
    }

    /// Migrations of the application plus one more version, as a newer
    /// version of the application would have
    static std::vector<pdf::PDFDatabaseMigration> getExtendedMigrations()
    {
        std::vector<pdf::PDFDatabaseMigration> migrations = pdf::PDFDatabaseSchema::getMigrations();
        pdf::PDFDatabaseMigration migration;
        migration.version = int(migrations.size()) + 1;
        migration.sql = "ALTER TABLE setting ADD COLUMN test_column INTEGER; CREATE TABLE test_extra(id INTEGER PRIMARY KEY);";
        migrations.push_back(migration);
        return migrations;
    }

    /// Creates a database file of the given version by the migrations
    static bool createFile(const QString& filePath, const std::vector<pdf::PDFDatabaseMigration>& migrations, int version)
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, &error);
        if (!handle)
        {
            return false;
        }

        error = pdf::PDFDatabaseSchema::migrate(handle, migrations, version);
        if (!error.isError() && version >= 1)
        {
            error = pdf::PDFDatabase::executeOnHandle(handle, "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'dark', 0)");
        }
        pdf::PDFDatabase::closeHandle(handle);
        return !error.isError();
    }

    /// Value of the table storage_maintenance (-1 = no value)
    static qint64 getMaintenanceValue(pdf::PDFDatabase* database, const char* key)
    {
        pdf::PDFDatabaseStatement statement(database->getConnection(), "SELECT value FROM storage_maintenance WHERE key = ?1");
        statement.bindText(1, QString::fromLatin1(key));
        const qint64 value = statement.next() ? statement.getInt64(0) : -1;
        statement.reset();
        return value;
    }

    /// Value of the table storage_maintenance read from the file by a raw read-only connection
    static qint64 getMaintenanceValue(const QString& filePath, const char* key)
    {
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, true, 1000, nullptr);
        if (!handle)
        {
            return -1;
        }
        const QByteArray sql = QByteArray("SELECT value FROM storage_maintenance WHERE key = '") + key + "'";
        const qint64 value = pdf::PDFDatabase::queryIntegerOnHandle(handle, sql.constData(), -1);
        pdf::PDFDatabase::closeHandle(handle);
        return value;
    }

    /// Executes SQL on the file by a raw connection (no notification, no trigger of the application)
    static bool executeOnFile(const QString& filePath, const char* sql)
    {
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, nullptr);
        if (!handle)
        {
            return false;
        }
        const bool result = !pdf::PDFDatabase::executeOnHandle(handle, sql).isError();
        pdf::PDFDatabase::closeHandle(handle);
        return result;
    }

    /// Sets the time of the last integrity check of the file (ms since the epoch)
    static bool setLastCheck(const QString& filePath, qint64 time)
    {
        const QByteArray sql = "UPDATE storage_maintenance SET value = " + QByteArray::number(time) + " WHERE key = 'integrity.lastCheck'";
        return executeOnFile(filePath, sql.constData());
    }

    /// Opens the file with the options and returns, whether the full integrity check ran
    static bool openAndCheck(const QString& filePath, qint64 interval = pdf::PDFDatabaseOptions::DEFAULT_INTEGRITY_CHECK_INTERVAL)
    {
        pdf::PDFDatabase database;
        pdf::PDFDatabaseOptions options = createOptions(filePath);
        options.integrityCheckInterval = interval;
        database.open(options);
        return database.getState().mode == pdf::PDFDatabaseStorageMode::Disk && database.getState().integrityChecked;
    }

    /// Creates a checked database with thumbnails of the recent files and damages
    /// pages of its data at the end of the file (not the schema)
    static bool createDamagedThumbnails(const QString& filePath)
    {
        {
            pdf::PDFDatabase database;
            database.open(createOptions(filePath));
            pdf::PDFDatabaseConnection* connection = database.getConnection();
            pdf::PDFDatabaseTransaction transaction(connection);
            for (int i = 0; i < 400; ++i)
            {
                pdf::PDFDatabaseStatement statement(connection, "INSERT INTO recent_file(path, name, folder, last_opened, thumbnail) VALUES (?1, 'n', '/', 0, ?2)");
                statement.bindText(1, QStringLiteral("/file%1.pdf").arg(i));
                statement.bindBlob(2, QByteArray(300, char('a' + i % 20)));
                if (!statement.execute())
                {
                    return false;
                }
            }
            if (!transaction.commit() || database.checkpoint(false).busy || !database.getState().integrityChecked)
            {
                return false;
            }
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadWrite) || file.size() < 60 * 4096)
        {
            return false;
        }
        const qint64 pageCount = file.size() / 4096;
        for (qint64 page = pageCount - 12; page < pageCount - 1; ++page)
        {
            file.seek(page * 4096);
            file.write(QByteArray(4096, char(0x5A)));
        }
        return true;
    }

    /// Reads all thumbnails, \returns the error
    static pdf::PDFDatabaseError readThumbnails(pdf::PDFDatabase* database)
    {
        pdf::PDFDatabaseStatement statement(database->getConnection(), "SELECT SUM(LENGTH(thumbnail)) FROM recent_file");
        while (statement.next())
        {
        }
        return statement.getError();
    }

    /// Migrations of the application plus \p extraVersions newer versions, as
    /// newer versions of the application would have (O19: each of them has its file)
    static std::vector<pdf::PDFDatabaseMigration> getNewerMigrations(int extraVersions)
    {
        std::vector<pdf::PDFDatabaseMigration> migrations = getExtendedMigrations();
        for (int i = 1; i < extraVersions; ++i)
        {
            pdf::PDFDatabaseMigration migration;
            migration.version = int(migrations.size()) + 1;
            migration.sql = "ALTER TABLE setting ADD COLUMN test_column" + QByteArray::number(migration.version) + " INTEGER NOT NULL DEFAULT 0;";
            migrations.push_back(migration);
        }
        return migrations;
    }

    /// Options of a database in the folder (file per schema version, O19)
    static pdf::PDFDatabaseOptions createFolderOptions(const QString& directory, int extraVersions = 0)
    {
        pdf::PDFDatabaseOptions options = createOptions(QString());
        options.directory = directory;
        if (extraVersions > 0)
        {
            options.migrations = getNewerMigrations(extraVersions);
        }
        return options;
    }

    /// Creates a database file of \p version with the setting appearance.colorScheme =
    /// \p value and \p fileCount recent files with thumbnails of 8 kB (the file is closed,
    /// its write-ahead log is checkpointed and removed)
    static bool createSchemaFile(const QString& filePath, int version, const QByteArray& value, int fileCount = 0)
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, &error);
        if (!handle)
        {
            return false;
        }

        const std::vector<pdf::PDFDatabaseMigration> migrations = getNewerMigrations(qMax(1, version - pdf::PDFDatabaseSchema::getCurrentVersion()));
        error = pdf::PDFDatabaseSchema::migrate(handle, migrations, version);
        if (!error.isError())
        {
            const QByteArray sql = "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', '" + value + "', 0)";
            error = pdf::PDFDatabase::executeOnHandle(handle, sql.constData());
        }
        if (!error.isError() && fileCount > 0)
        {
            const QByteArray sql = "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < " + QByteArray::number(fileCount) + ") "
                                   "INSERT INTO recent_file(path, name, folder, last_opened, thumbnail) SELECT '/file' || i || '.pdf', 'n', '/', i, randomblob(8192) FROM n";
            error = pdf::PDFDatabase::executeOnHandle(handle, sql.constData());
        }
        pdf::PDFDatabase::closeHandle(handle);
        return !error.isError();
    }

    /// Text value of a setting (empty = no row)
    static QString getSettingText(pdf::PDFDatabase* database, const char* key)
    {
        pdf::PDFDatabaseStatement statement(database->getConnection(), "SELECT value FROM setting WHERE key = ?1");
        statement.bindText(1, QString::fromLatin1(key));
        const QString value = statement.next() ? statement.getText(0) : QString();
        statement.reset();
        return value;
    }

    /// Hash of a file and of its write-ahead log (an empty log = no log: a reader may
    /// leave an empty one)
    static QByteArray fileAndLogHash(const QString& filePath)
    {
        const QString logPath = filePath + QStringLiteral("-wal");
        return fileHash(filePath) + (QFileInfo(logPath).size() > 0 ? fileHash(logPath) : QByteArray());
    }

    /// Names of the files of the folder without the files of SQLite (-wal, -shm)
    static QStringList getDatabaseNames(const QString& directory)
    {
        QStringList result;
        for (const QString& name : QDir(directory).entryList(QDir::Files | QDir::Hidden, QDir::Name))
        {
            if (!name.endsWith(QLatin1String("-wal")) && !name.endsWith(QLatin1String("-shm")))
            {
                result << name;
            }
        }
        return result;
    }

    /// Temporary files of upgrades in the folder
    static QStringList getUpgradeTemporaryFiles(const QString& directory)
    {
        QStringList result;
        for (const QString& name : QDir(directory).entryList(QDir::Files | QDir::Hidden | QDir::System))
        {
            if (pdf::PDFDatabaseFiles::isUpgradeTemporaryFileName(name))
            {
                result << name;
            }
        }
        return result;
    }

    /// Copies a file byte by byte (a malformed name with valid content)
    static bool copyFile(const QString& source, const QString& target)
    {
        return QFile::copy(source, target);
    }

    static int s_memoryCounter;
};

int AppStorageTestHelper::s_memoryCounter = 0;

/// Modes of the test executable started as a child process
class AppStorageChild
{
public:
    AppStorageChild() = delete;

    static int run(const QStringList& arguments)
    {
        const QString mode = arguments.value(1);
        if (mode == QLatin1String("--storage-writer"))
        {
            return runWriter(arguments.value(2), arguments.value(3), arguments.value(4).toInt());
        }
        if (mode == QLatin1String("--storage-write-one"))
        {
            return runWriteOne(arguments.value(2), arguments.value(3));
        }
        if (mode == QLatin1String("--storage-hold-lock"))
        {
            return runHoldLock(arguments.value(2), arguments.value(3).toInt());
        }
        if (mode == QLatin1String("--storage-memdb-probe"))
        {
            return runMemoryProbe(arguments.value(2));
        }
        if (mode == QLatin1String("--storage-open-info"))
        {
            return runOpenInfo(arguments.value(2), arguments.value(3) == QLatin1String("scan"));
        }
        if (mode == QLatin1String("--storage-crash-writer"))
        {
            return runCrashWriter(arguments.value(2));
        }
        if (mode == QLatin1String("--storage-upgrade"))
        {
            return runUpgrade(arguments.value(2), arguments.value(3).toInt(), arguments.value(4) == QLatin1String("crash"));
        }
        return 2;
    }

private:
    static void print(const QByteArray& text)
    {
        fputs(text.constData(), stdout);
        fputs("\n", stdout);
        fflush(stdout);
    }

    static int runWriter(const QString& filePath, const QString& writerId, int count)
    {
        pdf::PDFDatabase database;
        pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(filePath);
        database.open(options);
        if (database.getState().mode != pdf::PDFDatabaseStorageMode::Disk)
        {
            print("not on disk: " + database.getState().error.toString().toUtf8());
            return 3;
        }

        for (int i = 0; i < count; ++i)
        {
            pdf::PDFDatabaseConnection* connection = database.getConnection();
            pdf::PDFDatabaseTransaction transaction(connection);
            if (!transaction.isActive())
            {
                print("begin failed: " + transaction.getError().toString().toUtf8());
                return 4;
            }

            pdf::PDFDatabaseStatement insert(connection, "INSERT INTO setting(key, value, modified) VALUES (?1, ?2, 0)");
            insert.bindText(1, QStringLiteral("test.w%1n%2").arg(writerId).arg(i));
            insert.bindInt64(2, i);
            pdf::PDFDatabaseStatement update(connection, "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('counter', 0, 1) "
                                                         "ON CONFLICT(tool_id) DO UPDATE SET count = count + 1");
            if (!insert.execute() || !update.execute())
            {
                print("write failed: " + insert.getError().toString().toUtf8() + update.getError().toString().toUtf8());
                return 5;
            }

            if (!transaction.commit())
            {
                print("commit failed: " + transaction.getError().toString().toUtf8());
                return 6;
            }
        }

        print("done");
        return 0;
    }

    static int runWriteOne(const QString& filePath, const QString& key)
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        pdf::PDFSettingRegistry registry;
        pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);
        pdf::PDFSettingsRepository settings(&database, &registry);
        const pdf::PDFDatabaseError error = settings.setValue(key, QStringLiteral("dark"));
        print(error.isError() ? "failed" : "committed");
        return error.isError() ? 1 : 0;
    }

    static int runHoldLock(const QString& filePath, int milliseconds)
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        pdf::PDFDatabaseConnection* connection = database.getConnection();
        pdf::PDFDatabaseTransaction transaction(connection);
        if (!transaction.isActive())
        {
            print("failed");
            return 1;
        }
        connection->execute("INSERT INTO setting(key, value, modified) VALUES ('test.locked', 1, 0)");
        print("locked");
        if (milliseconds >= 0)
        {
            QThread::msleep(milliseconds);
        }
        else
        {
            // Until the test writes a line (or closes the channel, or ends)
            QFile input;
            if (input.open(stdin, QIODevice::ReadOnly))
            {
                input.readLine();
            }
        }
        transaction.commit();
        print("released");
        return 0;
    }

    /// Opens the database, prints whether the full integrity check ran and optionally
    /// reads all thumbnails (prints "corrupt", when the read finds a damage). The damage
    /// is recorded by close() - this process runs no event loop.
    static int runOpenInfo(const QString& filePath, bool scan)
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        const pdf::PDFDatabaseState state = database.getState();
        print("mode=" + QByteArray::number(int(state.mode)) + " checked=" + QByteArray::number(state.integrityChecked ? 1 : 0));
        if (scan)
        {
            const pdf::PDFDatabaseError error = AppStorageTestHelper::readThumbnails(&database);
            if (error.getCode() == SQLITE_CORRUPT_CODE)
            {
                print("corrupt");
            }
        }
        database.close();
        return 0;
    }

    /// Opens the database of the folder as the version of the application, writes a row
    /// and ends without closing it: the row stays in the write-ahead log (a crash)
    static int runCrashWriter(const QString& directory)
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createFolderOptions(directory));
        const bool isWritten = AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('test.crash', 'yes', 0)");
        print(isWritten ? "written" : "failed");
        std::_Exit(isWritten ? CRASH_EXIT_CODE : 1);
    }

    /// Exit code of runCrashWriter
    static constexpr int CRASH_EXIT_CODE = 33;

    /// Opens the database of the folder (file per schema version) as a version of the
    /// application with \p extraVersions newer schema versions and prints the result.
    /// \p crash ends the process after the copy of the older file (test hook).
    static int runUpgrade(const QString& directory, int extraVersions, bool crash)
    {
        pdf::PDFDatabase database;
        pdf::PDFDatabaseOptions options = AppStorageTestHelper::createFolderOptions(directory, extraVersions);
        options.testExitAfterUpgradeCopy = crash;
        database.open(options);
        const pdf::PDFDatabaseState state = database.getState();
        print("mode=" + QByteArray::number(int(state.mode)) +
              " upgraded=" + QByteArray::number(state.upgradedFromFilePath.isEmpty() ? 0 : 1) +
              " version=" + QByteArray::number(state.schemaVersion) +
              " value=" + AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme").toUtf8());
        database.close();
        return 0;
    }

    /// SQLITE_CORRUPT (the test does not include SQLite)
    static constexpr int SQLITE_CORRUPT_CODE = 11;

    static int runMemoryProbe(const QString& memoryName)
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openMemoryConnection(memoryName, &error);
        if (!handle)
        {
            print("failed");
            return 1;
        }
        const qint64 tables = pdf::PDFDatabase::queryIntegerOnHandle(handle, "SELECT COUNT(*) FROM sqlite_master", -1);
        pdf::PDFDatabase::closeHandle(handle);
        print("tables=" + QByteArray::number(tables));
        return 0;
    }
};

/// Worker thread, which reads and writes the database by its own connection
class AppStorageWorker : public QThread
{
public:
    AppStorageWorker(pdf::PDFDatabase* database, int count) : m_database(database), m_count(count) { }

    std::atomic<int> committed = 0;
    std::atomic<int> errors = 0;
    QVariant readValue;

protected:
    virtual void run() override
    {
        pdf::PDFDatabaseConnection* connection = m_database->getConnection();
        if (!connection || !connection->isValid())
        {
            ++errors;
            return;
        }

        pdf::PDFDatabaseStatement read(connection, "SELECT value FROM setting WHERE key = 'test.main'");
        if (read.next())
        {
            readValue = read.getVariant(0);
        }
        read.reset();

        for (int i = 0; i < m_count; ++i)
        {
            pdf::PDFDatabaseTransaction transaction(connection);
            pdf::PDFDatabaseStatement statement(connection, "INSERT INTO setting(key, value, modified) VALUES (?1, ?2, 0)");
            statement.bindText(1, QStringLiteral("test.thread%1.%2").arg(quintptr(QThread::currentThreadId())).arg(i));
            statement.bindInt64(2, i);
            if (transaction.isActive() && statement.execute() && transaction.commit())
            {
                ++committed;
            }
            else
            {
                ++errors;
            }
        }
    }

private:
    pdf::PDFDatabase* m_database;
    int m_count;
};

class AppStorageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void freshDatabase();
    void triggersOfAllTables();
    void statementsAndTransactions();
    void settingDefinitions();
    void settingsRepository();
    void shortcutRepository();
    void notifierOwnWrites();
    void notifierGap();
    void notifierOtherProcess();
    void pruneNeverBlocks();
    void pruneAndReaders();
    void concurrentProcesses_data();
    void concurrentProcesses();
    void concurrentThreads();
    void busyLockOtherProcess();
    void migrationsFromOlderVersions();
    void damagedFile_data();
    void damagedFile();
    void integrityCheckPolicy();
    void damageReportedByOtherProcess();
    void damageReportedInProcess();
    void readOnlyFile();
    void newerSchema();
    void cannotOpenAndRetry();
    void checkpointWithOpenRead();
    void threadConnectionsClosed();
    void portableSharedByThreads();
    void portableInvisibleToOtherProcess();
    void schemaFileNames();
    void schemaFileFresh();
    void schemaFileUpgrade();
    void schemaFileCrashedOlder();
    void schemaFileNewestOlder();
    void schemaFileOnlyNewer();
    void schemaFileConcurrent_data();
    void schemaFileConcurrent();
    void schemaFileCrash();
    void schemaFileUnusableOlder_data();
    void schemaFileUnusableOlder();
    void schemaFileBrokenMigration();
    void schemaFileFolderErrors();
    void schemaFilePortable();

private:
    QString createPath(const QString& name);
    QString createFolder(const QString& name);
    QProcess* startChild(const QStringList& arguments);
    QByteArray runUpgradeChild(const QString& directory, int extraVersions, bool crash, int* exitCode);

    QTemporaryDir m_directory;
    int m_fileCounter = 0;
};

QString AppStorageTest::createPath(const QString& name)
{
    const QString directory = m_directory.filePath(QStringLiteral("%1-%2").arg(name).arg(++m_fileCounter));
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("pdf4qt.db"));
}

QProcess* AppStorageTest::startChild(const QStringList& arguments)
{
    QProcess* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start(QCoreApplication::applicationFilePath(), arguments);
    return process;
}

void AppStorageTest::initTestCase()
{
    QVERIFY(m_directory.isValid());
    qInfo("SQLite %s, timeout factor %d", qPrintable(pdf::PDFDatabase::getSQLiteVersion()), AppStorageTestHelper::getTimeoutFactor());

    // memdb VFS of SQLite 3.36 and newer
    const QStringList version = pdf::PDFDatabase::getSQLiteVersion().split(QLatin1Char('.'));
    QVERIFY(version.size() >= 2);
    QVERIFY(version[0].toInt() > 3 || (version[0].toInt() == 3 && version[1].toInt() >= 36));
}

void AppStorageTest::freshDatabase()
{
    const QString filePath = createPath(QStringLiteral("fresh"));
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));

    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(state.isPersistent());
    QVERIFY(!state.isRetryPossible());
    QVERIFY(state.damagedFilePath.isEmpty());
    QCOMPARE(state.schemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());
    QCOMPARE(state.fileSchemaVersion, 0);
    QVERIFY(state.getDescription().isEmpty());
    QVERIFY(state.integrityChecked);

    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA application_id"), qint64(pdf::PDFDatabase::APPLICATION_ID));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA user_version"), qint64(pdf::PDFDatabaseSchema::getCurrentVersion()));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA foreign_keys"), qint64(1));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA synchronous"), qint64(1));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA temp_store"), qint64(2));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA busy_timeout"), qint64(5000));

    pdf::PDFDatabaseConnection* connection = database.getConnection();
    QVERIFY(connection);
    QVERIFY(connection->isValid());
    QCOMPARE(database.getConnection(), connection);

    // Every table of the schema has its three triggers
    for (const pdf::PDFDatabaseTableInfo& table : pdf::PDFDatabaseSchema::getTables())
    {
        pdf::PDFDatabaseStatement statement(connection, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND tbl_name = ?1");
        statement.bindText(1, table.name);
        QVERIFY(statement.next());
        QCOMPARE(statement.getInt64(0), qint64(3));
        statement.reset();
    }

    // The internal tables of the storage and of the thumbnailer have no triggers
    // (their writes are no notifications)
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND tbl_name = 'storage_maintenance'"), qint64(0));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND tbl_name = 'thumbnail_rendering'"), qint64(0));
    QVERIFY(qAbs(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY) - QDateTime::currentMSecsSinceEpoch()) < 60000);

#if defined(Q_OS_UNIX)
    const QFileDevice::Permissions permissions = QFile::permissions(filePath);
    QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)));

    // A folder created for the database is accessible only by the user
    {
        const QString nestedPath = m_directory.filePath(QStringLiteral("private-%1/data/pdf4qt.db").arg(++m_fileCounter));
        pdf::PDFDatabase nested;
        nested.open(AppStorageTestHelper::createOptions(nestedPath));
        QCOMPARE(nested.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        const QFileDevice::Permissions folderPermissions = QFile::permissions(QFileInfo(nestedPath).absolutePath());
        QVERIFY(!(folderPermissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther |
                                       QFileDevice::ExeGroup | QFileDevice::ExeOther)));
    }
#endif

    database.close();
    QVERIFY(!database.isOpen());
    QVERIFY(!database.getConnection());

    // Clean close: the WAL is checkpointed and removed
    QVERIFY(!QFile::exists(filePath + QStringLiteral("-wal")) || QFileInfo(filePath + QStringLiteral("-wal")).size() == 0);

    // Second open of the same file: no migration, the same schema, no full integrity check
    database.open(AppStorageTestHelper::createOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(!database.getState().integrityChecked);
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA user_version"), qint64(pdf::PDFDatabaseSchema::getCurrentVersion()));
}

void AppStorageTest::triggersOfAllTables()
{
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("triggers"))));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    pdf::PDFDatabaseConnection* connection = database.getConnection();

    struct TableCase
    {
        const char* table;
        const char* prerequisite;
        const char* insert;
        const char* update;
        const char* remove;
        const char* key;
    };

    const TableCase cases[] =
    {
        { "setting", nullptr, "INSERT INTO setting(key, value, modified) VALUES ('t.k', 1, 0)", "UPDATE setting SET value = 2 WHERE key = 't.k'", "DELETE FROM setting WHERE key = 't.k'", "t.k" },
        { "recent_file", nullptr, "INSERT INTO recent_file(path, name, folder, last_opened) VALUES ('/a.pdf', 'a.pdf', '/', 0)", "UPDATE recent_file SET open_count = 2 WHERE path = '/a.pdf'", "DELETE FROM recent_file WHERE path = '/a.pdf'", "/a.pdf" },
        { "recent_folder", nullptr, "INSERT INTO recent_folder(path, last_used) VALUES ('/f', 0)", "UPDATE recent_folder SET last_used = 1", "DELETE FROM recent_folder", "/f" },
        { "pinned_location", nullptr, "INSERT INTO pinned_location(id, path, name) VALUES (7, '/p', 'p')", "UPDATE pinned_location SET name = 'q'", "DELETE FROM pinned_location", "7" },
        { "workspace", nullptr, "INSERT INTO workspace(id, name, created) VALUES (5, 'w', 0)", "UPDATE workspace SET saved = 1 WHERE id = 5", "DELETE FROM workspace WHERE id = 5", "5" },
        { "workspace_document", "INSERT INTO workspace(id, name, created) VALUES (9, 'wd', 0)", "INSERT INTO workspace_document(workspace_id, position, path) VALUES (9, 0, '/d.pdf')", "UPDATE workspace_document SET page = 3", "DELETE FROM workspace_document", "9" },
        { "tool_pin", nullptr, "INSERT INTO tool_pin(tool_id, pinned) VALUES ('pages.merge', 1)", "UPDATE tool_pin SET position = 2", "DELETE FROM tool_pin", "pages.merge" },
        { "tool_usage", nullptr, "INSERT INTO tool_usage(tool_id, last_used, count) VALUES ('pages.split', 0, 1)", "UPDATE tool_usage SET count = 2", "DELETE FROM tool_usage", "pages.split" },
        { "tool_collection", nullptr, "INSERT INTO tool_collection(id, name, modified) VALUES (3, 'c', 0)", "UPDATE tool_collection SET name = 'd'", "DELETE FROM tool_collection WHERE id = 3", "3" },
        { "tool_collection_item", "INSERT INTO tool_collection(id, name, modified) VALUES (4, 'e', 0)", "INSERT INTO tool_collection_item(collection_id, tool_id) VALUES (4, 'x')", "UPDATE tool_collection_item SET position = 1", "DELETE FROM tool_collection_item", "4" },
        { "tool_pair", nullptr, "INSERT INTO tool_pair(from_tool, to_tool, last_used) VALUES ('a', 'b', 0)", "UPDATE tool_pair SET count = 3", "DELETE FROM tool_pair", "a" },
        { "tool_parameter", nullptr, "INSERT INTO tool_parameter(tool_id, name, value) VALUES ('t', 'p', 1)", "UPDATE tool_parameter SET value = 2", "DELETE FROM tool_parameter", "t" },
        { "shortcut", nullptr, "INSERT INTO shortcut(command_id, keys) VALUES ('window.newTab', 'Ctrl+T')", "UPDATE shortcut SET keys = 'Ctrl+N'", "DELETE FROM shortcut", "window.newTab" },
        { "annotation_style", nullptr, "INSERT INTO annotation_style(style_id, value) VALUES ('s', 'v')", "UPDATE annotation_style SET value = 'w'", "DELETE FROM annotation_style", "s" },
        { "bookmark", nullptr, "INSERT INTO bookmark(id, file_path, page, title) VALUES (2, '/b.pdf', 1, 't')", "UPDATE bookmark SET title = 'u'", "DELETE FROM bookmark", "/b.pdf" },
        { "trusted_certificate", nullptr, "INSERT INTO trusted_certificate(id, data, added) VALUES (6, x'00', 0)", "UPDATE trusted_certificate SET added = 1", "DELETE FROM trusted_certificate", "6" },
        { "mcp_server", nullptr, "INSERT INTO mcp_server(id, instance) VALUES (1, 'i')", "UPDATE mcp_server SET state = 'running'", "DELETE FROM mcp_server", "1" },
        { "mcp_client", nullptr, "INSERT INTO mcp_client(session) VALUES ('s1')", "UPDATE mcp_client SET name = 'Claude'", "DELETE FROM mcp_client", "s1" },
        { "mcp_activity", nullptr, "INSERT INTO mcp_activity(id, time, status) VALUES (8, 0, 'ok')", "UPDATE mcp_activity SET status = 'failed'", "DELETE FROM mcp_activity", "8" },
        { "mcp_request", nullptr, "INSERT INTO mcp_request(id, kind, created) VALUES (9, 'disconnect', 0)", "UPDATE mcp_request SET session = 'x'", "DELETE FROM mcp_request", "9" },
    };

    // The test covers every table of the schema (a new table needs its case here)
    QStringList testedTables;
    for (const TableCase& tableCase : cases)
    {
        testedTables << QString::fromLatin1(tableCase.table);
    }
    QStringList schemaTables;
    pdf::PDFDatabaseStatement tables(connection, "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' AND name NOT IN ('change_log', 'storage_maintenance', 'thumbnail_rendering') ORDER BY name");
    while (tables.next())
    {
        schemaTables << tables.getText(0);
    }
    testedTables.sort();
    QCOMPARE(testedTables, schemaTables);

    QStringList infoTables;
    for (const pdf::PDFDatabaseTableInfo& info : pdf::PDFDatabaseSchema::getTables())
    {
        infoTables << info.name;
    }
    infoTables.sort();
    QCOMPARE(infoTables, schemaTables);

    auto countLog = [connection](const char* table, const char* key, qint64 afterSequence)
    {
        pdf::PDFDatabaseStatement statement(connection, "SELECT COUNT(*) FROM change_log WHERE topic = ?1 AND key = ?2 AND seq > ?3");
        statement.bindText(1, QString::fromLatin1(table));
        statement.bindText(2, QString::fromUtf8(key));
        statement.bindInt64(3, afterSequence);
        const qint64 count = statement.next() ? statement.getInt64(0) : -1;
        statement.reset();
        return count;
    };

    for (const TableCase& tableCase : cases)
    {
        if (tableCase.prerequisite)
        {
            QVERIFY2(!connection->execute(tableCase.prerequisite).isError(), tableCase.table);
        }

        for (const char* sql : { tableCase.insert, tableCase.update, tableCase.remove })
        {
            const qint64 sequence = connection->queryInteger("SELECT COALESCE(MAX(seq), 0) FROM change_log", -1);
            const pdf::PDFDatabaseError error = connection->execute(sql);
            QVERIFY2(!error.isError(), qPrintable(QString::fromLatin1(tableCase.table) + QLatin1Char(' ') + error.toString()));
            QVERIFY2(connection->getChanges() > 0, sql);
            QVERIFY2(countLog(tableCase.table, tableCase.key, sequence) == 1, sql);

            const qint64 time = connection->queryInteger("SELECT time FROM change_log ORDER BY seq DESC LIMIT 1", 0);
            QVERIFY(qAbs(time - QDateTime::currentMSecsSinceEpoch()) < 60000);
        }
    }

    // A changed key logs the old and the new key
    QVERIFY(!connection->execute("INSERT INTO setting(key, value, modified) VALUES ('t.old', 1, 0)").isError());
    qint64 sequence = connection->queryInteger("SELECT MAX(seq) FROM change_log", -1);
    QVERIFY(!connection->execute("UPDATE setting SET key = 't.new' WHERE key = 't.old'").isError());
    QCOMPARE(countLog("setting", "t.old", sequence), qint64(1));
    QCOMPARE(countLog("setting", "t.new", sequence), qint64(1));

    // The heartbeat of the MCP server alone is not a change
    QVERIFY(!connection->execute("INSERT INTO mcp_server(id, instance, heartbeat) VALUES (1, 'i', 0)").isError());
    sequence = connection->queryInteger("SELECT MAX(seq) FROM change_log", -1);
    QVERIFY(!connection->execute("UPDATE mcp_server SET heartbeat = 2000").isError());
    QCOMPARE(countLog("mcp_server", "1", sequence), qint64(0));

    // INSERT OR REPLACE, which deletes a row by another unique column than the key:
    // the deleted row is logged too (recursive triggers)
    QVERIFY(!connection->execute("INSERT INTO tool_collection(id, builtin_id, name, modified) VALUES (21, 'builtin.replace', 'r', 0)").isError());
    sequence = connection->queryInteger("SELECT MAX(seq) FROM change_log", -1);
    QVERIFY(!connection->execute("INSERT OR REPLACE INTO tool_collection(id, builtin_id, name, modified) VALUES (22, 'builtin.replace', 's', 0)").isError());
    QCOMPARE(countLog("tool_collection", "21", sequence), qint64(1));
    QCOMPARE(countLog("tool_collection", "22", sequence), qint64(1));
    QCOMPARE(connection->queryInteger("PRAGMA recursive_triggers", -1), qint64(1));

    // Cascade of a foreign key is logged too
    QVERIFY(!connection->execute("INSERT INTO workspace(id, name, created) VALUES (11, 'cascade', 0); "
                                 "INSERT INTO workspace_document(workspace_id, position, path) VALUES (11, 0, '/c.pdf')").isError());
    sequence = connection->queryInteger("SELECT MAX(seq) FROM change_log", -1);
    QVERIFY(!connection->execute("DELETE FROM workspace WHERE id = 11").isError());
    QCOMPARE(countLog("workspace_document", "11", sequence), qint64(1));
    QCOMPARE(connection->queryInteger("SELECT COUNT(*) FROM workspace_document WHERE workspace_id = 11", -1), qint64(0));

    // Foreign keys are enforced
    QVERIFY(connection->execute("INSERT INTO workspace_document(workspace_id, position, path) VALUES (12345, 0, '/x.pdf')").isError());

    // Saved workspaces have unique names (case insensitive), the last session not
    QVERIFY(!connection->execute("INSERT INTO workspace(name, created) VALUES ('Work', 0)").isError());
    QVERIFY(connection->execute("INSERT INTO workspace(name, created) VALUES ('work', 0)").isError());
    QVERIFY(!connection->execute("INSERT INTO workspace(name, created, is_last_session) VALUES ('work', 0, 1)").isError());
}

void AppStorageTest::statementsAndTransactions()
{
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("statements"))));
    pdf::PDFDatabaseConnection* connection = database.getConnection();

    // Invalid statements and null connections report errors, never crash
    pdf::PDFDatabaseStatement invalid(connection, "SELECT * FROM no_such_table");
    QVERIFY(!invalid.isValid());
    QVERIFY(invalid.getError().isError());
    QVERIFY(!invalid.next());
    QVERIFY(!invalid.execute());
    QCOMPARE(invalid.getText(0), QString());

    pdf::PDFDatabaseStatement nullStatement(nullptr, "SELECT 1");
    QVERIFY(!nullStatement.isValid());
    QVERIFY(nullStatement.getError().isError());

    pdf::PDFDatabaseTransaction nullTransaction(nullptr);
    QVERIFY(!nullTransaction.isActive());
    QVERIFY(!nullTransaction.commit());

    // Typed binding and reading
    QVERIFY(!connection->execute("CREATE TEMP TABLE values_test(id INTEGER PRIMARY KEY, value)").isError());
    const QList<QVariant> values = { QVariant(), QVariant(true), QVariant(qint64(-42)), QVariant(3.5), QVariant(QStringLiteral("Příliš žluťoučký")),
                                     QVariant(QByteArray("\0\1\2", 3)), QVariant(QByteArray()), QVariant(quint64(std::numeric_limits<quint64>::max())) };
    pdf::PDFDatabaseStatement insert(connection, "INSERT INTO values_test(id, value) VALUES (?1, ?2)");
    for (int i = 0; i < values.size(); ++i)
    {
        insert.bindInt64(1, i);
        insert.bindVariant(2, values[i]);
        QVERIFY(insert.execute());
    }

    pdf::PDFDatabaseStatement select(connection, "SELECT value, typeof(value) FROM values_test ORDER BY id");
    QStringList types;
    QList<QVariant> readValues;
    while (select.next())
    {
        readValues << select.getVariant(0);
        types << select.getText(1);
    }
    QVERIFY(!select.getError().isError());
    QCOMPARE(types, QStringList({ "null", "integer", "integer", "real", "text", "blob", "blob", "real" }));
    QVERIFY(!readValues[0].isValid());
    QCOMPARE(readValues[1], QVariant(qint64(1)));
    QCOMPARE(readValues[2], QVariant(qint64(-42)));
    QCOMPARE(readValues[3], QVariant(3.5));
    QCOMPARE(readValues[4], QVariant(QStringLiteral("Příliš žluťoučký")));
    QCOMPARE(readValues[5], QVariant(QByteArray("\0\1\2", 3)));
    QCOMPARE(readValues[6].toByteArray(), QByteArray());

    // Reading after the end and columns out of the range
    QCOMPARE(select.getInt64(0), qint64(0));
    QCOMPARE(select.getInt64(-1), qint64(0));
    QCOMPARE(select.getInt64(100), qint64(0));
    QVERIFY(select.isNull(100));

    // A statement is reset at its end, so it can run again
    int rows = 0;
    while (select.next())
    {
        ++rows;
    }
    QCOMPARE(rows, int(values.size()));

    // Rollback by the destructor
    {
        pdf::PDFDatabaseTransaction transaction(connection);
        QVERIFY(transaction.isActive());
        QVERIFY(connection->isInTransaction());
        QVERIFY(!connection->execute("INSERT INTO setting(key, value, modified) VALUES ('t.rollback', 1, 0)").isError());
    }
    QVERIFY(!connection->isInTransaction());
    QCOMPARE(connection->queryInteger("SELECT COUNT(*) FROM setting WHERE key = 't.rollback'", -1), qint64(0));

    // Nested transaction = savepoint
    {
        pdf::PDFDatabaseTransaction outer(connection);
        QVERIFY(outer.isActive());
        QVERIFY(!connection->execute("INSERT INTO setting(key, value, modified) VALUES ('t.outer', 1, 0)").isError());
        {
            pdf::PDFDatabaseTransaction inner(connection);
            QVERIFY(inner.isActive());
            QVERIFY(!connection->execute("INSERT INTO setting(key, value, modified) VALUES ('t.inner1', 1, 0)").isError());
            inner.rollback();
            QVERIFY(!inner.isActive());
        }
        {
            pdf::PDFDatabaseTransaction inner(connection);
            QVERIFY(!connection->execute("INSERT INTO setting(key, value, modified) VALUES ('t.inner2', 1, 0)").isError());
            QVERIFY(inner.commit());
        }
        QVERIFY(connection->isInTransaction());
        QVERIFY(outer.commit());
        QVERIFY(!outer.commit());
    }
    QCOMPARE(connection->queryInteger("SELECT COUNT(*) FROM setting WHERE key IN ('t.outer', 't.inner2')", -1), qint64(2));
    QCOMPARE(connection->queryInteger("SELECT COUNT(*) FROM setting WHERE key = 't.inner1'", -1), qint64(0));

    // A failed statement inside of a transaction: the transaction can be rolled back
    {
        pdf::PDFDatabaseTransaction transaction(connection);
        pdf::PDFDatabaseStatement duplicate(connection, "INSERT INTO setting(key, value, modified) VALUES ('t.outer', 1, 0)");
        QVERIFY(!duplicate.execute());
        QVERIFY(duplicate.getError().isError());
    }
    QVERIFY(!connection->isInTransaction());

    // Read transaction (snapshot) does not notify
    QSignalSpy committedSpy(&database, &pdf::PDFDatabase::committed);
    {
        pdf::PDFDatabaseTransaction transaction(connection, pdf::PDFDatabaseTransaction::Mode::Read);
        QVERIFY(transaction.isActive());
        QVERIFY(transaction.commit());
    }
    {
        pdf::PDFDatabaseTransaction transaction(connection, pdf::PDFDatabaseTransaction::Mode::Exclusive);
        QVERIFY(transaction.isActive());
        QVERIFY(transaction.commit());
    }
    QTRY_COMPARE(committedSpy.count(), 1);

    // A read transaction, in which a nested write (savepoint) has changed rows, notifies
    {
        pdf::PDFDatabaseTransaction outer(connection, pdf::PDFDatabaseTransaction::Mode::Read);
        QVERIFY(outer.isActive());
        {
            pdf::PDFDatabaseTransaction inner(connection);
            QVERIFY(inner.isActive());
            QVERIFY(!connection->execute("INSERT INTO setting(key, value, modified) VALUES ('t.readwrite', 1, 0)").isError());
            QVERIFY(inner.commit());
        }
        QVERIFY(outer.commit());
    }
    QTRY_COMPARE(committedSpy.count(), 2);
    QCOMPARE(connection->queryInteger("SELECT COUNT(*) FROM setting WHERE key = 't.readwrite'", -1), qint64(1));
}

/// Options of a setting from the system
class AppStorageTestOptionsProvider : public pdf::PDFSettingOptionsProvider
{
public:
    QStringList available = { QStringLiteral("voice-a"), QStringLiteral("voice-b") };

    virtual std::vector<pdf::PDFSettingOption> getOptions(const pdf::PDFSettingDefinition& definition) const override
    {
        Q_UNUSED(definition);
        std::vector<pdf::PDFSettingOption> options;
        for (const QString& id : available)
        {
            options.push_back({ id, id.toUpper(), true });
        }
        return options;
    }
};

void AppStorageTest::settingDefinitions()
{
    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    // Keys needed in step 1
    const QStringList keys = { "appearance.colorScheme", "update.autoCheck", "update.lastCheck", "update.latestVersion", "update.lastError",
                               "mcp.port", "mcp.allowRemote", "mcp.requireToken", "mcp.startWithApp", "mcp.token", "mcp.permissionRead",
                               "mcp.permissionEdit", "mcp.permissionFiles", "tools.suggestNext", "tools.historyRuns", "bookmarks.autoGenerate", "recent.maxFiles" };
    for (const QString& key : keys)
    {
        const pdf::PDFSettingDefinition* definition = registry.findDefinition(key);
        QVERIFY2(definition, qPrintable(key));
        QVERIFY(definition->isValid());
        QCOMPARE(definition->validate(QVariant()), definition->getDefaultValue());
    }

    for (const pdf::PDFSettingDefinition* definition : registry.getDefinitions())
    {
        QVERIFY(definition->isValid());
        if (definition->isVisible())
        {
            // Visible settings can be found and linked
            QVERIFY2(!definition->getAnchor().isEmpty(), qPrintable(definition->getKey()));
            QVERIFY2(!definition->getTitle().isEmpty(), qPrintable(definition->getKey()));
            QVERIFY2(!definition->getGroupTitle().isEmpty(), qPrintable(definition->getKey()));
        }
    }

    // Defaults of newapp_mcp.md 1
    QCOMPARE(registry.findDefinition("mcp.port")->getDefaultValue(), QVariant(qint64(8765)));
    QCOMPARE(registry.findDefinition("mcp.requireToken")->getDefaultValue(), QVariant(true));
    QCOMPARE(registry.findDefinition("mcp.allowRemote")->getDefaultValue(), QVariant(false));
    QCOMPARE(registry.findDefinition("mcp.startWithApp")->getDefaultValue(), QVariant(false));
    QCOMPARE(registry.findDefinition("mcp.permissionRead")->getDefaultValue(), QVariant(QStringLiteral("allow")));
    QCOMPARE(registry.findDefinition("mcp.permissionEdit")->getDefaultValue(), QVariant(QStringLiteral("ask")));
    QCOMPARE(registry.findDefinition("mcp.permissionFiles")->getDefaultValue(), QVariant(QStringLiteral("ask")));
    QCOMPARE(registry.findDefinition("update.autoCheck")->getDefaultValue(), QVariant(true));
    QCOMPARE(registry.findDefinition("recent.maxFiles")->getDefaultValue(), QVariant(qint64(10)));
    QCOMPARE(registry.findDefinition("appearance.colorScheme")->getDefaultValue(), QVariant(QStringLiteral("auto")));
    // Step 3 has registered the settings, which the document tab reads, in their sections
    QCOMPARE(registry.getSections(), QStringList({ "general", "viewing", "color-modes", "editing", "security", "speech", "color-management", "advanced" }));
    QVERIFY(registry.getSectionDefinitions(QString()).empty());
    QVERIFY(!registry.findDefinition(QStringLiteral("unknown.key")));

    // Registration: invalid keys, invalid defaults and duplicates are refused
    QVERIFY(!registry.registerDefinition(pdf::PDFSettingDefinition::createBool(QStringLiteral("appearance.colorScheme"), true)));
    QVERIFY(!registry.registerDefinition(pdf::PDFSettingDefinition::createBool(QStringLiteral("nosection"), true)));
    QVERIFY(!registry.registerDefinition(pdf::PDFSettingDefinition::createBool(QStringLiteral("a.b.c"), true)));
    QVERIFY(!registry.registerDefinition(pdf::PDFSettingDefinition::createBool(QStringLiteral("Upper.case"), true)));
    QVERIFY(!registry.registerDefinition(pdf::PDFSettingDefinition::createInteger(QStringLiteral("test.range"), 50, 0, 10)));
    QVERIFY(!registry.registerDefinition(pdf::PDFSettingDefinition::createOption(QStringLiteral("test.option"), QStringLiteral("x"), std::vector<pdf::PDFSettingOption>())));
    QVERIFY(registry.registerDefinition(pdf::PDFSettingDefinition::createReal(QStringLiteral("test.real"), 1.0, 0.5, 2.0).setSection(QStringLiteral("test"))));
    QCOMPARE(registry.getSections().last(), QStringLiteral("test"));

    // Validation: another type or a value out of the range = default
    const pdf::PDFSettingDefinition boolean = pdf::PDFSettingDefinition::createBool(QStringLiteral("test.bool"), true);
    QCOMPARE(boolean.validate(QVariant(qint64(0))), QVariant(false));
    QCOMPARE(boolean.validate(QVariant(qint64(1))), QVariant(true));
    QCOMPARE(boolean.validate(QVariant(false)), QVariant(false));
    QCOMPARE(boolean.validate(QVariant(qint64(2))), QVariant(true));
    QCOMPARE(boolean.validate(QVariant(QStringLiteral("false"))), QVariant(true));
    QCOMPARE(boolean.validate(QVariant(0.0)), QVariant(true));

    const pdf::PDFSettingDefinition integer = pdf::PDFSettingDefinition::createInteger(QStringLiteral("test.integer"), 10, 1, 100);
    QCOMPARE(integer.validate(QVariant(qint64(50))), QVariant(qint64(50)));
    QCOMPARE(integer.validate(QVariant(int(1))), QVariant(qint64(1)));
    QCOMPARE(integer.validate(QVariant(qint64(0))), QVariant(qint64(10)));
    QCOMPARE(integer.validate(QVariant(qint64(101))), QVariant(qint64(10)));
    QCOMPARE(integer.validate(QVariant(20.0)), QVariant(qint64(20)));
    QCOMPARE(integer.validate(QVariant(20.5)), QVariant(qint64(10)));
    QCOMPARE(integer.validate(QVariant(std::numeric_limits<double>::infinity())), QVariant(qint64(10)));
    QCOMPARE(integer.validate(QVariant(std::numeric_limits<quint64>::max())), QVariant(qint64(10)));
    QCOMPARE(integer.validate(QVariant(QStringLiteral("20"))), QVariant(qint64(10)));
    QCOMPARE(integer.validate(QVariant(QByteArray("20"))), QVariant(qint64(10)));

    const pdf::PDFSettingDefinition real = pdf::PDFSettingDefinition::createReal(QStringLiteral("test.real"), 1.0, 0.5, 2.0);
    QCOMPARE(real.validate(QVariant(1.5)), QVariant(1.5));
    QCOMPARE(real.validate(QVariant(qint64(2))), QVariant(2.0));
    QCOMPARE(real.validate(QVariant(std::numeric_limits<double>::quiet_NaN())), QVariant(1.0));
    QCOMPARE(real.validate(QVariant(2.5)), QVariant(1.0));

    const pdf::PDFSettingDefinition string = pdf::PDFSettingDefinition::createString(QStringLiteral("test.string"), QStringLiteral("x"), 4);
    QCOMPARE(string.validate(QVariant(QStringLiteral("abcd"))), QVariant(QStringLiteral("abcd")));
    QCOMPARE(string.validate(QVariant(QStringLiteral("abcde"))), QVariant(QStringLiteral("x")));
    QCOMPARE(string.validate(QVariant(qint64(5))), QVariant(QStringLiteral("x")));

    const pdf::PDFSettingDefinition* colorScheme = registry.findDefinition(QStringLiteral("appearance.colorScheme"));
    QCOMPARE(colorScheme->getOptions().size(), size_t(3));
    QCOMPARE(colorScheme->validate(QVariant(QStringLiteral("dark"))), QVariant(QStringLiteral("dark")));
    QCOMPARE(colorScheme->validate(QVariant(QStringLiteral("Dark"))), QVariant(QStringLiteral("auto")));
    QCOMPARE(colorScheme->validate(QVariant(QStringLiteral("sepia"))), QVariant(QStringLiteral("auto")));
    QCOMPARE(colorScheme->getOptions()[1].getTitle(), QStringLiteral("Dark"));

    // Options of the system: a stored option, which the system does not offer, is the default
    std::shared_ptr<AppStorageTestOptionsProvider> provider = std::make_shared<AppStorageTestOptionsProvider>();
    const pdf::PDFSettingDefinition voice = pdf::PDFSettingDefinition::createOption(QStringLiteral("speech.voice"), QStringLiteral("voice-a"), provider);
    QVERIFY(voice.isValid());
    QVERIFY(voice.hasOptionsProvider());
    QCOMPARE(voice.validate(QVariant(QStringLiteral("voice-b"))), QVariant(QStringLiteral("voice-b")));
    provider->available = { QStringLiteral("voice-a") };
    QCOMPARE(voice.validate(QVariant(QStringLiteral("voice-b"))), QVariant(QStringLiteral("voice-a")));
    QCOMPARE(voice.getOptions()[0].getTitle(), QStringLiteral("VOICE-A"));

    // Random values never crash the validation
    QRandomGenerator random(1234);
    for (int i = 0; i < 2000; ++i)
    {
        QVariant value;
        switch (random.bounded(5))
        {
            case 0: value = QVariant(qint64(random.generate64())); break;
            case 1: value = QVariant(random.generateDouble() * 1e6 - 5e5); break;
            case 2: value = QVariant(QString::number(random.generate())); break;
            case 3: value = QVariant(QByteArray::number(random.generate())); break;
            default: value = QVariant(); break;
        }
        for (const pdf::PDFSettingDefinition* definition : registry.getDefinitions())
        {
            const QVariant validated = definition->validate(value);
            QVERIFY(definition->normalize(validated).has_value());
        }
    }
}

void AppStorageTest::settingsRepository()
{
    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("settings"))));
    pdf::PDFSettingsRepository settings(&database, &registry);

    // Defaults without any row
    QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("auto"));
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(8765));
    QCOMPARE(settings.getBool(QStringLiteral("update.autoCheck")), true);
    QVERIFY(!settings.getValue(QStringLiteral("unknown.key")).isValid());
    QVERIFY(settings.getChangedKeys(QStringLiteral("general")).isEmpty());

    // Typed values
    QVERIFY(!settings.setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("dark")).isError());
    QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 9000).isError());
    QVERIFY(!settings.setValue(QStringLiteral("update.autoCheck"), false).isError());
    QVERIFY(!settings.setValue(QStringLiteral("update.lastCheck"), qint64(1760000000000)).isError());
    QVERIFY(!settings.setValue(QStringLiteral("recent.maxFiles"), 25).isError());
    QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("dark"));
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));
    QCOMPARE(settings.getBool(QStringLiteral("update.autoCheck")), false);
    QCOMPARE(settings.getInteger(QStringLiteral("update.lastCheck")), qint64(1760000000000));
    QCOMPARE(settings.getStoredValue(QStringLiteral("update.autoCheck")), QVariant(qint64(0)));
    QVERIFY(settings.isChanged(QStringLiteral("appearance.colorScheme")));

    // Invalid values are refused, nothing is stored
    QVERIFY(settings.setValue(QStringLiteral("mcp.port"), 80).isError());
    QVERIFY(settings.setValue(QStringLiteral("mcp.port"), QStringLiteral("9001")).isError());
    QVERIFY(settings.setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("sepia")).isError());
    QVERIFY(settings.setValue(QStringLiteral("unknown.key"), 1).isError());
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));

    // The default value is not stored
    QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 8765).isError());
    QVERIFY(!settings.getStoredValue(QStringLiteral("mcp.port")).isValid());
    QVERIFY(!settings.isChanged(QStringLiteral("mcp.port")));

    // The same value again is not a change
    const qint64 logRows = AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log");
    QVERIFY(!settings.setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("dark")).isError());
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log"), logRows);

    // A value of another version (other type, out of range) is the default
    QVERIFY(AppStorageTestHelper::execute(&database, "UPDATE setting SET value = 'yes' WHERE key = 'update.autoCheck'"));
    QCOMPARE(settings.getBool(QStringLiteral("update.autoCheck")), true);
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('mcp.port', 70000, 0)"));
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(8765));
    QVERIFY(!settings.isChanged(QStringLiteral("mcp.port")));
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('removed.setting', 1, 0)"));

    // Restore defaults of a section: only its keys
    QCOMPARE(settings.getChangedKeys(QStringLiteral("general")), QStringList({ "appearance.colorScheme", "recent.maxFiles" }));
    QVERIFY(!settings.restoreDefaults(QStringLiteral("general")).isError());
    QVERIFY(settings.getChangedKeys(QStringLiteral("general")).isEmpty());
    QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("auto"));
    QCOMPARE(settings.getInteger(QStringLiteral("recent.maxFiles")), qint64(10));
    QCOMPARE(settings.getInteger(QStringLiteral("update.lastCheck")), qint64(1760000000000));
    QVERIFY(!settings.getStoredValue(QStringLiteral("update.autoCheck")).isValid());
    QVERIFY(settings.getStoredValue(QStringLiteral("mcp.port")).isValid());
    QVERIFY(settings.getStoredValue(QStringLiteral("removed.setting")).isValid());
    QVERIFY(settings.restoreDefaults(QString()).isError());

    QVERIFY(!settings.resetValue(QStringLiteral("update.lastCheck")).isError());
    QCOMPARE(settings.getInteger(QStringLiteral("update.lastCheck")), qint64(0));

    // A repository of a destroyed or closed database reports errors
    database.close();
    QVERIFY(settings.setValue(QStringLiteral("mcp.port"), 9000).isError());
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(8765));
    pdf::PDFSettingsRepository nullRepository(nullptr, nullptr);
    QVERIFY(!nullRepository.getValue(QStringLiteral("mcp.port")).isValid());
    QVERIFY(nullRepository.setValue(QStringLiteral("mcp.port"), 9000).isError());
}

void AppStorageTest::shortcutRepository()
{
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("shortcuts"))));
    pdf::PDFShortcutRepository shortcuts(&database);

    QVERIFY(shortcuts.getShortcuts().empty());
    QVERIFY(!shortcuts.setShortcut(QStringLiteral("window.newTab"), QStringLiteral("Ctrl+N")).isError());
    QVERIFY(!shortcuts.setShortcut(QStringLiteral("window.closeTab"), QString()).isError());
    QVERIFY(shortcuts.setShortcut(QString(), QStringLiteral("Ctrl+N")).isError());
    QVERIFY(shortcuts.setShortcut(QStringLiteral("x.y"), QString(1000, QLatin1Char('a'))).isError());

    std::map<QString, QString> stored = shortcuts.getShortcuts();
    QCOMPARE(stored.size(), size_t(2));
    QCOMPARE(stored[QStringLiteral("window.newTab")], QStringLiteral("Ctrl+N"));
    QCOMPARE(stored[QStringLiteral("window.closeTab")], QString());

    QVERIFY(!shortcuts.resetShortcut(QStringLiteral("window.newTab")).isError());
    QCOMPARE(shortcuts.getShortcuts().size(), size_t(1));
    QVERIFY(!shortcuts.resetAll().isError());
    QVERIFY(shortcuts.getShortcuts().empty());
}

void AppStorageTest::notifierOwnWrites()
{
    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("notifier"))));
    pdf::PDFDatabaseNotifier notifier(&database);
    QSignalSpy changedSpy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    QSignalSpy everythingSpy(&notifier, &pdf::PDFDatabaseNotifier::everythingChanged);

    // Own commit wakes the notifier at once (the timer is practically off)
    notifier.setPollInterval(3600 * 1000);
    pdf::PDFSettingsRepository settings(&database, &registry);
    QVERIFY(!settings.setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("dark")).isError());
    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 1, 1000);
    QCOMPARE(changedSpy[0][0].toString(), QStringLiteral("setting"));
    QCOMPARE(changedSpy[0][1].toString(), QStringLiteral("appearance.colorScheme"));

    // More changes of one key in a poll = one signal
    changedSpy.clear();
    {
        pdf::PDFDatabaseTransaction transaction(database.getConnection());
        settings.setValue(QStringLiteral("mcp.port"), 9001);
        settings.setValue(QStringLiteral("mcp.port"), 9002);
        settings.setValue(QStringLiteral("mcp.port"), 9003);
        QVERIFY(transaction.commit());
    }
    QTRY_COMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy[0][1].toString(), QStringLiteral("mcp.port"));

    // A write without an explicit transaction is found by the poll (own changes counter)
    changedSpy.clear();
    notifier.setPollInterval(20);
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO bookmark(file_path, page, title) VALUES ('/x.pdf', 1, 'x')"));
    QTRY_COMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy[0][0].toString(), QStringLiteral("bookmark"));

    // Writes of another thread change data_version of the GUI connection
    changedSpy.clear();
    AppStorageWorker worker(&database, 5);
    worker.start();
    QVERIFY(worker.wait(30000 * AppStorageTestHelper::getTimeoutFactor()));
    QCOMPARE(worker.committed.load(), 5);
    QTRY_COMPARE(changedSpy.count(), 5);

    // A slot deleting the notifier does not crash it
    pdf::PDFDatabaseNotifier* notifier2 = new pdf::PDFDatabaseNotifier(&database);
    QPointer<pdf::PDFDatabaseNotifier> guard(notifier2);
    connect(notifier2, &pdf::PDFDatabaseNotifier::changed, notifier2, [notifier2]() { delete notifier2; });
    settings.setValue(QStringLiteral("mcp.port"), 9010);
    QTRY_VERIFY(!guard);

    // Closed database: nothing happens
    database.close();
    notifier.poll();
    QCOMPARE(everythingSpy.count(), 0);
}

void AppStorageTest::notifierGap()
{
    const QString filePath = createPath(QStringLiteral("gap"));
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    pdf::PDFDatabaseNotifier notifier(&database);
    notifier.setPollInterval(20);
    QSignalSpy changedSpy(&notifier, &pdf::PDFDatabaseNotifier::changed);
    QSignalSpy everythingSpy(&notifier, &pdf::PDFDatabaseNotifier::everythingChanged);

    // Another instance writes and prunes rows, which this one has not read yet
    {
        pdf::PDFDatabase other;
        other.open(AppStorageTestHelper::createOptions(filePath));
        QVERIFY(AppStorageTestHelper::execute(&other, "BEGIN IMMEDIATE; "
                                                      "INSERT INTO setting(key, value, modified) VALUES ('t.a', 1, 0); "
                                                      "INSERT INTO setting(key, value, modified) VALUES ('t.b', 1, 0); "
                                                      "DELETE FROM change_log WHERE seq = (SELECT MIN(seq) FROM change_log WHERE key = 't.a'); "
                                                      "COMMIT"));
    }
    QTRY_COMPARE(everythingSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 0);

    // Normal changes afterwards
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('t.c', 1, 0)"));
    QTRY_COMPARE(changedSpy.count(), 1);

    // Many rows in one poll = everything
    notifier.setPollInterval(3600 * 1000);
    {
        pdf::PDFDatabase other;
        other.open(AppStorageTestHelper::createOptions(filePath));
        pdf::PDFDatabaseTransaction transaction(other.getConnection());
        for (int i = 0; i < pdf::PDFDatabaseNotifier::MAXIMUM_ROWS_PER_POLL + 10; ++i)
        {
            pdf::PDFDatabaseStatement statement(other.getConnection(), "INSERT INTO setting(key, value, modified) VALUES (?1, 1, 0)");
            statement.bindText(1, QStringLiteral("t.many%1").arg(i));
            QVERIFY(statement.execute());
        }
        QVERIFY(transaction.commit());
    }
    notifier.poll();
    QCOMPARE(everythingSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 1);

    // Rows, which this instance has not read, were all pruned (it was suspended and
    // another instance pruned the log): no row is new, but the sequence has moved on
    {
        pdf::PDFDatabase other;
        other.open(AppStorageTestHelper::createOptions(filePath));
        QVERIFY(AppStorageTestHelper::execute(&other, "BEGIN IMMEDIATE; "
                                                      "INSERT INTO setting(key, value, modified) VALUES ('t.pruned', 1, 0); "
                                                      "DELETE FROM change_log WHERE key = 't.pruned'; "
                                                      "COMMIT"));
    }
    notifier.poll();
    QCOMPARE(everythingSpy.count(), 3);
    QCOMPARE(changedSpy.count(), 1);
    notifier.poll();
    QCOMPARE(everythingSpy.count(), 3);

    // Pruning keeps at most LOG_MAXIMUM_ROWS rows and removes old rows
    QVERIFY(AppStorageTestHelper::execute(&database, "UPDATE change_log SET time = 0 WHERE seq < 10"));
    notifier.prune();
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE seq < 10"), qint64(0));
    QVERIFY(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log") <= pdf::PDFDatabaseNotifier::LOG_MAXIMUM_ROWS);

    // Pruning of everything: the sequence continues (AUTOINCREMENT), no false gap
    QVERIFY(AppStorageTestHelper::execute(&database, "DELETE FROM change_log"));
    notifier.poll();
    const int everythingCount = everythingSpy.count();
    changedSpy.clear();
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('t.after', 1, 0)"));
    notifier.poll();
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(everythingSpy.count(), everythingCount);
}

void AppStorageTest::notifierOtherProcess()
{
    const QString filePath = createPath(QStringLiteral("otherprocess"));
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    pdf::PDFDatabaseNotifier notifier(&database);
    QSignalSpy changedSpy(&notifier, &pdf::PDFDatabaseNotifier::changed);

    const int factor = AppStorageTestHelper::getTimeoutFactor();
    QProcess* process = startChild({ QStringLiteral("--storage-write-one"), filePath, QStringLiteral("appearance.colorScheme") });
    QVERIFY(process->waitForStarted(10000 * factor));

    QElapsedTimer timer;
    QByteArray output;
    QDeadlineTimer deadline(30000 * factor);
    while (!output.contains("committed") && !deadline.hasExpired())
    {
        process->waitForReadyRead(100);
        output += process->readAllStandardOutput();
    }
    timer.start();
    QVERIFY2(output.contains("committed"), output.constData());

    // newapp_storage.md 6: the notification comes within 1 s
    QTRY_VERIFY_WITH_TIMEOUT(!changedSpy.isEmpty(), 1000 * factor);
    QVERIFY2(timer.elapsed() <= 1000 * factor, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(changedSpy[0][1].toString(), QStringLiteral("appearance.colorScheme"));

    QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(30000 * factor));
    QCOMPARE(process->exitCode(), 0);
}

void AppStorageTest::pruneNeverBlocks()
{
    const QString filePath = createPath(QStringLiteral("prune"));
    const int factor = AppStorageTestHelper::getTimeoutFactor();

    // Default busy timeout (5 s): a waiting pruning would freeze the GUI for 10 s
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('t.old1', 1, 0); "
                                                     "INSERT INTO setting(key, value, modified) VALUES ('t.old2', 1, 0); "
                                                     "UPDATE change_log SET time = 0"));
    const qint64 oldRows = AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE time = 0");
    QVERIFY(oldRows >= 2);

    // Another process holds the write lock until the test releases it
    QProcess* process = startChild({ QStringLiteral("--storage-hold-lock"), filePath, QStringLiteral("-1") });
    QByteArray output;
    QDeadlineTimer deadline(30000 * factor);
    while (!output.contains("locked") && !deadline.hasExpired())
    {
        process->waitForReadyRead(100);
        output += process->readAllStandardOutput();
    }
    QVERIFY2(output.contains("locked"), output.constData());

    // The notifier prunes at its start: it does not wait for the lock
    QElapsedTimer timer;
    timer.start();
    pdf::PDFDatabaseNotifier notifier(&database);
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(notifier.isPruneRetryPending());
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE time = 0"), oldRows);
    QCOMPARE(database.getConnection()->getBusyTimeout(), 5000);
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA busy_timeout"), qint64(5000));

    // Every hour, too
    timer.restart();
    QVERIFY(!notifier.prune());
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(notifier.isPruneRetryPending());

    // The retries meet the lock again, the event loop runs
    notifier.setPruneRetryInterval(50);
    QTest::qWait(300);
    QVERIFY(process->state() == QProcess::Running);
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE time = 0"), oldRows);

    // Released: a retry prunes the log
    process->write("release\n");
    process->closeWriteChannel();
    QVERIFY(process->waitForFinished(30000 * factor));
    QCOMPARE(process->exitCode(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE time = 0"), qint64(0), 10000 * factor);
    QTRY_VERIFY(!notifier.isPruneRetryPending());
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT value FROM setting WHERE key = 'test.locked'"), qint64(1));

    // Inside of a transaction of the GUI thread: not a part of it, tried later
    {
        pdf::PDFDatabaseTransaction transaction(database.getConnection());
        QVERIFY(!notifier.prune());
        QVERIFY(notifier.isPruneRetryPending());
    }
    QTRY_VERIFY(!notifier.isPruneRetryPending());
}

void AppStorageTest::pruneAndReaders()
{
    // Pruning of one instance against the notifier of another one (gap detection)
    const QString filePath = createPath(QStringLiteral("prune-readers"));
    pdf::PDFDatabase writer;
    writer.open(AppStorageTestHelper::createOptions(filePath));
    pdf::PDFDatabaseNotifier writerNotifier(&writer);
    writerNotifier.setPollInterval(3600 * 1000);

    pdf::PDFDatabase reader;
    reader.open(AppStorageTestHelper::createOptions(filePath));
    pdf::PDFDatabaseNotifier readerNotifier(&reader);
    readerNotifier.setPollInterval(3600 * 1000);
    QSignalSpy changedSpy(&readerNotifier, &pdf::PDFDatabaseNotifier::changed);
    QSignalSpy everythingSpy(&readerNotifier, &pdf::PDFDatabaseNotifier::everythingChanged);

    // Rows, which the reader has not read, are pruned: everything has changed
    QVERIFY(AppStorageTestHelper::execute(&writer, "INSERT INTO setting(key, value, modified) VALUES ('t.p1', 1, 0); "
                                                   "INSERT INTO setting(key, value, modified) VALUES ('t.p2', 1, 0); "
                                                   "UPDATE change_log SET time = 0"));
    QVERIFY(writerNotifier.prune());
    QCOMPARE(AppStorageTestHelper::queryInteger(&writer, "SELECT COUNT(*) FROM change_log"), qint64(0));
    readerNotifier.poll();
    QCOMPARE(everythingSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 0);

    // Normal changes afterwards
    QVERIFY(AppStorageTestHelper::execute(&writer, "INSERT INTO setting(key, value, modified) VALUES ('t.p3', 1, 0)"));
    readerNotifier.poll();
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy[0][1].toString(), QStringLiteral("t.p3"));

    // Rows, which the reader has read, are pruned: no false gap
    QVERIFY(AppStorageTestHelper::execute(&writer, "UPDATE change_log SET time = 0"));
    QVERIFY(writerNotifier.prune());
    readerNotifier.poll();
    QCOMPARE(everythingSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(AppStorageTestHelper::execute(&writer, "INSERT INTO setting(key, value, modified) VALUES ('t.p4', 1, 0)"));
    readerNotifier.poll();
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(everythingSpy.count(), 1);

    // The limit of the rows: the newest LOG_MAXIMUM_ROWS rows are kept
    {
        pdf::PDFDatabaseTransaction transaction(writer.getConnection());
        for (int i = 0; i < pdf::PDFDatabaseNotifier::LOG_MAXIMUM_ROWS + 5; ++i)
        {
            pdf::PDFDatabaseStatement statement(writer.getConnection(), "INSERT INTO change_log(topic, key, time) VALUES ('setting', 't.bulk', ?1)");
            statement.bindInt64(1, QDateTime::currentMSecsSinceEpoch());
            QVERIFY(statement.execute());
        }
        QVERIFY(transaction.commit());
    }
    QVERIFY(writerNotifier.prune());
    QCOMPARE(AppStorageTestHelper::queryInteger(&writer, "SELECT COUNT(*) FROM change_log"), qint64(pdf::PDFDatabaseNotifier::LOG_MAXIMUM_ROWS));
    readerNotifier.poll();
    QCOMPARE(everythingSpy.count(), 2);

    // The clock was set back: a newer row of the log has an older time, the pruning
    // removes it from the middle of the rows, which the reader has not read - a hole
    // after the first unread row is a gap too (the change of t.h2 is not lost)
    QVERIFY(AppStorageTestHelper::execute(&writer, "INSERT INTO setting(key, value, modified) VALUES ('t.h1', 1, 0); "
                                                   "INSERT INTO setting(key, value, modified) VALUES ('t.h2', 1, 0); "
                                                   "INSERT INTO setting(key, value, modified) VALUES ('t.h3', 1, 0); "
                                                   "UPDATE change_log SET time = 0 WHERE key = 't.h2'"));
    QVERIFY(writerNotifier.prune());
    QCOMPARE(AppStorageTestHelper::queryInteger(&writer, "SELECT COUNT(*) FROM change_log WHERE key LIKE 't.h%'"), qint64(2));
    changedSpy.clear();
    readerNotifier.poll();
    QCOMPARE(everythingSpy.count(), 3);
    QCOMPARE(changedSpy.count(), 0);
}

void AppStorageTest::concurrentProcesses_data()
{
    QTest::addColumn<int>("processCount");
    QTest::addColumn<int>("transactionCount");

    QTest::newRow("2 processes") << 2 << AppStorageTestHelper::getTransactionCount(3000);
    QTest::newRow("4 processes") << 4 << AppStorageTestHelper::getTransactionCount(1500);
}

void AppStorageTest::concurrentProcesses()
{
    QFETCH(int, processCount);
    QFETCH(int, transactionCount);

    // All processes open a new file at once: one migrates, the others wait
    const QString filePath = createPath(QStringLiteral("processes"));
    const int factor = AppStorageTestHelper::getTimeoutFactor();

    QList<QProcess*> processes;
    for (int i = 0; i < processCount; ++i)
    {
        processes << startChild({ QStringLiteral("--storage-writer"), filePath, QString::number(i), QString::number(transactionCount) });
    }

    QElapsedTimer timer;
    timer.start();
    for (QProcess* process : processes)
    {
        QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(600000 * factor));
        const QByteArray output = process->readAllStandardOutput() + process->readAllStandardError();
        QVERIFY2(process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0, output.constData());
    }
    qInfo("%d processes x %d transactions: %lld ms", processCount, transactionCount, timer.elapsed());

    // Nothing is lost: every row and every increment of the shared counter
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM setting WHERE key LIKE 'test.w%'"), qint64(processCount) * transactionCount);
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT count FROM tool_usage WHERE tool_id = 'counter'"), qint64(processCount) * transactionCount);
    for (int i = 0; i < processCount; ++i)
    {
        const QByteArray sql = "SELECT COUNT(*) FROM setting WHERE key LIKE 'test.w" + QByteArray::number(i) + "n%'";
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, sql.constData()), qint64(transactionCount));
    }
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA user_version"), qint64(pdf::PDFDatabaseSchema::getCurrentVersion()));

    qDeleteAll(processes);
}

void AppStorageTest::concurrentThreads()
{
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("threads"))));
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('test.main', 42, 0)"));

    const int count = AppStorageTestHelper::getTransactionCount(500);
    std::vector<std::unique_ptr<AppStorageWorker>> workers;
    for (int i = 0; i < 4; ++i)
    {
        workers.push_back(std::make_unique<AppStorageWorker>(&database, count));
        workers.back()->start();
    }

    for (const std::unique_ptr<AppStorageWorker>& worker : workers)
    {
        QVERIFY(worker->wait(600000 * AppStorageTestHelper::getTimeoutFactor()));
        QCOMPARE(worker->committed.load(), count);
        QCOMPARE(worker->errors.load(), 0);
        QCOMPARE(worker->readValue, QVariant(qint64(42)));
    }

    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM setting WHERE key LIKE 'test.thread%'"), qint64(4 * count));
}

void AppStorageTest::busyLockOtherProcess()
{
    const QString filePath = createPath(QStringLiteral("busy"));
    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    pdf::PDFDatabase database;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(filePath);
    options.busyTimeout = 200;
    database.open(options);
    pdf::PDFSettingsRepository settings(&database, &registry);
    QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 9000).isError());

    const int factor = AppStorageTestHelper::getTimeoutFactor();
    const int holdTime = 4000 * factor;
    QProcess* process = startChild({ QStringLiteral("--storage-hold-lock"), filePath, QString::number(holdTime) });
    QByteArray output;
    QDeadlineTimer deadline(30000 * factor);
    while (!output.contains("locked") && !deadline.hasExpired())
    {
        process->waitForReadyRead(100);
        output += process->readAllStandardOutput();
    }
    QVERIFY2(output.contains("locked"), output.constData());
    QElapsedTimer lockTimer;
    lockTimer.start();

    // Write: busy timeout, one retry, then an error; the value stays
    QElapsedTimer timer;
    timer.start();
    const pdf::PDFDatabaseError error = settings.setValue(QStringLiteral("mcp.port"), 9001);
    const qint64 elapsed = timer.elapsed();
    QVERIFY(error.isError());
    QVERIFY(error.isBusy());
    QVERIFY2(elapsed >= 350, qPrintable(QString::number(elapsed)));
    QVERIFY(timer.elapsed() < holdTime);

    // Reads are not blocked by the writer (WAL)
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));

    pdf::PDFDatabaseOptions migratingOptions = AppStorageTestHelper::createOptions(filePath);
    migratingOptions.busyTimeout = 200;
    migratingOptions.migrations = AppStorageTestHelper::getExtendedMigrations();
    const int extendedVersion = pdf::PDFDatabaseSchema::getVersion(migratingOptions.migrations);
    QVERIFY(extendedVersion > pdf::PDFDatabaseSchema::getCurrentVersion());

    // The limit of the wait (only tests set it): the opening, which needs a migration
    // while the other process holds the lock, ends in the memory fallback
    {
        pdf::PDFDatabase migrating;
        pdf::PDFDatabaseOptions limitedOptions = migratingOptions;
        limitedOptions.lockWaitLimit = 300;
        QElapsedTimer openTimer;
        openTimer.start();
        migrating.open(limitedOptions);
        const qint64 openTime = openTimer.elapsed();
        QVERIFY(process->state() == QProcess::Running);
        QCOMPARE(migrating.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
        QVERIFY(migrating.getState().error.isBusy());
        QVERIFY2(openTime >= 300, qPrintable(QString::number(openTime)));
        QVERIFY2(openTime < 2500 * factor, qPrintable(QString::number(openTime)));
    }

    // Default (newapp_storage.md 5): the opening waits until the other process releases
    // the lock, then it migrates the file on the disk - no memory fallback
    {
        QVERIFY(process->state() == QProcess::Running);
        pdf::PDFDatabase migrating;
        QCOMPARE(migratingOptions.lockWaitLimit, -1);
        migrating.open(migratingOptions);
        const qint64 waited = lockTimer.elapsed();
        const pdf::PDFDatabaseState state = migrating.getState();
        QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
        QVERIFY2(!state.error.isError(), qPrintable(state.error.toString()));
        QCOMPARE(state.schemaVersion, extendedVersion);
        QVERIFY2(waited >= holdTime - 1000, qPrintable(QString::number(waited)));

        QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(60000 * factor));
        QCOMPARE(process->exitCode(), 0);
        QCOMPARE(AppStorageTestHelper::queryInteger(&migrating, "PRAGMA user_version"), qint64(extendedVersion));
        QCOMPARE(AppStorageTestHelper::queryInteger(&migrating, "SELECT value FROM setting WHERE key = 'test.locked'"), qint64(1));

        // After the lock is released, the write succeeds
        pdf::PDFSettingsRepository migratedSettings(&migrating, &registry);
        QVERIFY(!migratedSettings.setValue(QStringLiteral("mcp.port"), 9001).isError());
        QCOMPARE(migratedSettings.getInteger(QStringLiteral("mcp.port")), qint64(9001));
    }

    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9001));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT value FROM setting WHERE key = 'test.locked'"), qint64(1));
}

void AppStorageTest::migrationsFromOlderVersions()
{
    const std::vector<pdf::PDFDatabaseMigration> builtin = pdf::PDFDatabaseSchema::getMigrations();
    const std::vector<pdf::PDFDatabaseMigration> extended = AppStorageTestHelper::getExtendedMigrations();
    QVERIFY(pdf::PDFDatabaseSchema::isValid(builtin));
    QVERIFY(pdf::PDFDatabaseSchema::isValid(extended));
    QCOMPARE(pdf::PDFDatabaseSchema::getVersion(builtin), pdf::PDFDatabaseSchema::getCurrentVersion());

    // Invalid lists of migrations
    std::vector<pdf::PDFDatabaseMigration> gap = extended;
    gap.erase(gap.begin());
    QVERIFY(!pdf::PDFDatabaseSchema::isValid(gap));
    QVERIFY(pdf::PDFDatabaseSchema::migrate(nullptr, builtin, 1).isError());

    for (const std::vector<pdf::PDFDatabaseMigration>* migrations : { &builtin, &extended })
    {
        const int targetVersion = pdf::PDFDatabaseSchema::getVersion(*migrations);

        // Reference: a new database of the target version
        pdf::PDFDatabase reference;
        pdf::PDFDatabaseOptions referenceOptions = AppStorageTestHelper::createOptions(createPath(QStringLiteral("reference")));
        referenceOptions.migrations = *migrations;
        reference.open(referenceOptions);
        const QStringList referenceSchema = AppStorageTestHelper::getSchema(&reference);
        QVERIFY(!referenceSchema.isEmpty());

        for (int version = 0; version < targetVersion; ++version)
        {
            const QString filePath = createPath(QStringLiteral("migration%1").arg(version));
            QVERIFY(AppStorageTestHelper::createFile(filePath, *migrations, version));

            pdf::PDFDatabase database;
            pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(filePath);
            options.migrations = *migrations;
            database.open(options);

            const pdf::PDFDatabaseState state = database.getState();
            QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
            QCOMPARE(state.fileSchemaVersion, version);
            QCOMPARE(state.schemaVersion, targetVersion);
            QCOMPARE(AppStorageTestHelper::getSchema(&database), referenceSchema);
            QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA application_id"), qint64(pdf::PDFDatabase::APPLICATION_ID));

            // Data of the older version are kept
            if (version >= 1)
            {
                QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM setting WHERE key = 'appearance.colorScheme'"), qint64(1));
            }
        }
    }

    // A failing migration is rolled back completely
    std::vector<pdf::PDFDatabaseMigration> failing = builtin;
    pdf::PDFDatabaseMigration broken;
    broken.version = int(failing.size()) + 1;
    broken.sql = "CREATE TABLE test_partial(id INTEGER); THIS IS NOT SQL;";
    failing.push_back(broken);

    const QString filePath = createPath(QStringLiteral("failing"));
    QVERIFY(AppStorageTestHelper::createFile(filePath, builtin, int(builtin.size())));
    pdf::PDFDatabase database;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(filePath);
    options.migrations = failing;
    database.open(options);
    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    QVERIFY(state.error.isError());
    QVERIFY(state.contentCopied);
    database.close();

    sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, true, 1000, nullptr);
    QVERIFY(handle);
    QCOMPARE(pdf::PDFDatabaseSchema::readVersion(handle), int(builtin.size()));
    QCOMPARE(pdf::PDFDatabase::queryIntegerOnHandle(handle, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'test_partial'", -1), qint64(0));
    pdf::PDFDatabase::closeHandle(handle);
}

void AppStorageTest::damagedFile_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("garbage") << 0;
    QTest::newRow("truncated header") << 1;
    QTest::newRow("foreign application") << 2;
    QTest::newRow("corrupted page") << 3;
    QTest::newRow("garbage header of a checked file") << 4;
    QTest::newRow("foreign application of a checked file") << 5;
}

void AppStorageTest::damagedFile()
{
    QFETCH(int, kind);

    const QString filePath = createPath(QStringLiteral("damaged"));
    switch (kind)
    {
        case 0:
        {
            QFile file(filePath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QRandomGenerator random(42);
            QByteArray data(64 * 1024, Qt::Uninitialized);
            random.fillRange(reinterpret_cast<quint32*>(data.data()), data.size() / 4);
            file.write(data);
            break;
        }

        case 1:
        {
            QFile file(filePath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("SQLite format 3\0\x10\0", 19);
            break;
        }

        case 2:
        {
            sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 1000, nullptr);
            QVERIFY(handle);
            QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "PRAGMA application_id = 42; CREATE TABLE foreign_data(x)").isError());
            pdf::PDFDatabase::closeHandle(handle);
            break;
        }

        case 3:
        {
            {
                pdf::PDFDatabase database;
                database.open(AppStorageTestHelper::createOptions(filePath));
                pdf::PDFDatabaseConnection* connection = database.getConnection();
                pdf::PDFDatabaseTransaction transaction(connection);
                for (int i = 0; i < 400; ++i)
                {
                    pdf::PDFDatabaseStatement statement(connection, "INSERT INTO recent_file(path, name, folder, last_opened, thumbnail) VALUES (?1, 'n', '/', 0, ?2)");
                    statement.bindText(1, QStringLiteral("/file%1.pdf").arg(i));
                    statement.bindBlob(2, QByteArray(300, char('a' + i % 20)));
                    QVERIFY(statement.execute());
                }
                QVERIFY(transaction.commit());
                QVERIFY(!database.checkpoint(false).busy);
            }

            QFile file(filePath);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.size() > 40 * 4096);
            for (qint64 page = 30; page < 40; ++page)
            {
                file.seek(page * 4096);
                file.write(QByteArray(4096, char(0x5A)));
            }
            break;
        }

        case 4:
        case 5:
        {
            // A file checked just now: the next opening does not run the full check,
            // the header is checked always
            QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
            QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));
            if (kind == 4)
            {
                QFile file(filePath);
                QVERIFY(file.open(QIODevice::ReadWrite));
                file.write("Not a database!", 16);
            }
            else
            {
                QVERIFY(AppStorageTestHelper::executeOnFile(filePath, "PRAGMA application_id = 42"));
            }
            break;
        }
    }

    // A corrupted page of the data is found only by the full check (an interval of 0
    // = at every opening; damageReportedByOtherProcess tests the default interval)
    pdf::PDFDatabase database;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(filePath);
    if (kind == 3)
    {
        options.integrityCheckInterval = 0;
    }
    database.open(options);
    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(!state.damagedFilePath.isEmpty());
    QVERIFY(QFile::exists(state.damagedFilePath));
    QVERIFY(state.damagedFilePath.startsWith(filePath + QStringLiteral(".damaged-")));
    QVERIFY(!state.getDescription().isEmpty());

    // The new database works
    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);
    pdf::PDFSettingsRepository settings(&database, &registry);
    QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 9000).isError());
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA application_id"), qint64(pdf::PDFDatabase::APPLICATION_ID));
}

void AppStorageTest::integrityCheckPolicy()
{
    const QString filePath = createPath(QStringLiteral("integrity"));
    const qint64 day = 24 * 3600 * 1000;
    const int factor = AppStorageTestHelper::getTimeoutFactor();

    // A new file is checked, the time is recorded
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    const qint64 lastCheck = AppStorageTestHelper::getMaintenanceValue(filePath, pdf::PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY);
    QVERIFY(qAbs(lastCheck - QDateTime::currentMSecsSinceEpoch()) < 60000);

    // Within the interval: only the header and the schema (also another process)
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));
    auto openInOtherProcess = [this, factor](const QString& path)
    {
        QProcess* process = startChild({ QStringLiteral("--storage-open-info"), path });
        process->waitForFinished(30000 * factor);
        return process->readAllStandardOutput();
    };
    QByteArray output = openInOtherProcess(filePath);
    QVERIFY2(output.contains("mode=0 checked=0"), output.constData());

    // The interval has elapsed: the check runs once (the other process), then not
    QVERIFY(AppStorageTestHelper::setLastCheck(filePath, QDateTime::currentMSecsSinceEpoch() - 8 * day));
    output = openInOtherProcess(filePath);
    QVERIFY2(output.contains("mode=0 checked=1"), output.constData());
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // The interval is injected; 0 = at every opening
    QVERIFY(AppStorageTestHelper::setLastCheck(filePath, QDateTime::currentMSecsSinceEpoch() - 2000));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath, 1000));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath, 3600 * 1000));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath, 0));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath, 0));

    // The clock was set back, a value of another type, no value at all: checked
    QVERIFY(AppStorageTestHelper::setLastCheck(filePath, QDateTime::currentMSecsSinceEpoch() + day));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(AppStorageTestHelper::executeOnFile(filePath, "UPDATE storage_maintenance SET value = 'yesterday' WHERE key = 'integrity.lastCheck'"));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(AppStorageTestHelper::executeOnFile(filePath, "DELETE FROM storage_maintenance"));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // A damage reported by an instance (after the last check) requests the check
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(AppStorageTestHelper::setLastCheck(filePath, now - 10000));
    QVERIFY(AppStorageTestHelper::executeOnFile(filePath, QByteArray("INSERT INTO storage_maintenance(key, value) VALUES ('integrity.requested', " +
                                                                     QByteArray::number(now - 5000) + ")").constData()));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // The marker file (the request could not be written into the database)
    const QString markerPath = pdf::PDFDatabase::getIntegrityCheckMarkerPath(filePath);
    {
        QFile marker(markerPath);
        QVERIFY(marker.open(QIODevice::WriteOnly));
    }
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!QFile::exists(markerPath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // A marker written by another instance after the start of the check (its damage
    // may be newer than the check) is kept for the next opening; a time far in the
    // future (not written by PDF4QT, a clock set back) is not kept
    auto writeMarker = [&markerPath](qint64 time)
    {
        QFile marker(markerPath);
        return marker.open(QIODevice::WriteOnly | QIODevice::Truncate) && marker.write(QByteArray::number(time)) > 0;
    };
    QVERIFY(writeMarker(QDateTime::currentMSecsSinceEpoch() + 30000));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(QFile::exists(markerPath));
    QVERIFY(writeMarker(QDateTime::currentMSecsSinceEpoch() - 1000));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!QFile::exists(markerPath));
    QVERIFY(writeMarker(QDateTime::currentMSecsSinceEpoch() + day));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!QFile::exists(markerPath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // A request of another type (not written by PDF4QT) is a request, the check removes it
    QVERIFY(AppStorageTestHelper::executeOnFile(filePath, "INSERT INTO storage_maintenance(key, value) VALUES ('integrity.requested', 'damaged') "
                                                          "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QCOMPARE(AppStorageTestHelper::getMaintenanceValue(filePath, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY), qint64(-1));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // The clock ran ahead at the last check and it has been set back since: a damage
    // reported now is still newer than the check (also after the clock has passed the
    // time of the check - both times are shifted back by two hours)
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        const qint64 aheadTime = QDateTime::currentMSecsSinceEpoch() + 3600 * 1000;
        QVERIFY(AppStorageTestHelper::setLastCheck(filePath, aheadTime));
        database.reportError(pdf::PDFDatabaseError(11, 11, QStringLiteral("test")));
        QTRY_VERIFY(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY) > aheadTime);
    }
    QVERIFY(AppStorageTestHelper::executeOnFile(filePath, "UPDATE storage_maintenance SET value = value - 7200000"));
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // A damage reported after the database was closed (also by PRAGMA optimize of
    // close()) is recorded by the marker file: by the event loop and by the destructor.
    // A check started in the same tick of the clock as the damage keeps the marker
    // (one more check), so the test waits for the next tick.
    auto waitForClockAfterMarker = [&markerPath]()
    {
        QFile marker(markerPath);
        const qint64 markerTime = marker.open(QIODevice::ReadOnly) ? marker.readAll().toLongLong() : 0;
        QTRY_VERIFY(QDateTime::currentMSecsSinceEpoch() > markerTime);
    };
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        database.close();
        database.reportError(pdf::PDFDatabaseError(11, 11, QStringLiteral("test")));
        QTRY_VERIFY(QFile::exists(markerPath));
    }
    waitForClockAfterMarker();
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!QFile::exists(markerPath));
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        database.close();
        database.reportError(pdf::PDFDatabaseError(11, 11, QStringLiteral("test")));
    }
    QVERIFY(QFile::exists(markerPath));
    waitForClockAfterMarker();
    QVERIFY(AppStorageTestHelper::openAndCheck(filePath));
    QVERIFY(!QFile::exists(markerPath));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // Restore defaults (all settings deleted) keeps the time of the check
    QVERIFY(AppStorageTestHelper::executeOnFile(filePath, "DELETE FROM setting"));
    QVERIFY(!AppStorageTestHelper::openAndCheck(filePath));

    // The record of a check is no notification: an instance, which has the file open,
    // gets no signal, the change log does not grow
    {
        pdf::PDFDatabase observer;
        observer.open(AppStorageTestHelper::createOptions(filePath));
        pdf::PDFDatabaseNotifier notifier(&observer);
        notifier.setPollInterval(3600 * 1000);
        QSignalSpy changedSpy(&notifier, &pdf::PDFDatabaseNotifier::changed);
        QSignalSpy everythingSpy(&notifier, &pdf::PDFDatabaseNotifier::everythingChanged);
        const qint64 sequence = AppStorageTestHelper::queryInteger(&observer, "SELECT COALESCE(MAX(seq), 0) FROM change_log");

        QVERIFY(AppStorageTestHelper::openAndCheck(filePath, 0));
        output = openInOtherProcess(filePath);
        QVERIFY2(output.contains("mode=0 checked=0"), output.constData());
        QVERIFY(AppStorageTestHelper::setLastCheck(filePath, 0));
        output = openInOtherProcess(filePath);
        QVERIFY2(output.contains("mode=0 checked=1"), output.constData());
        notifier.poll();
        QTest::qWait(50);
        notifier.poll();
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(everythingSpy.count(), 0);
        QCOMPARE(AppStorageTestHelper::queryInteger(&observer, "SELECT COALESCE(MAX(seq), 0) FROM change_log"), sequence);
    }

    // A file without the time of the check is checked
    const QString olderPath = createPath(QStringLiteral("integrity-v1"));
    QVERIFY(AppStorageTestHelper::createFile(olderPath, pdf::PDFDatabaseSchema::getMigrations(), 1));
    QVERIFY(AppStorageTestHelper::openAndCheck(olderPath));
    QVERIFY(AppStorageTestHelper::getMaintenanceValue(olderPath, pdf::PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY) > 0);
    QVERIFY(!AppStorageTestHelper::openAndCheck(olderPath));
}

void AppStorageTest::damageReportedByOtherProcess()
{
    // A damaged page of the data of a file checked just now: the opening does not find it
    const QString filePath = createPath(QStringLiteral("damage-process"));
    QVERIFY(AppStorageTestHelper::createDamagedThumbnails(filePath));
    const int factor = AppStorageTestHelper::getTimeoutFactor();

    // Another process opens it without the full check, meets the damage at a read and
    // records it (by close - it runs no event loop)
    QProcess* process = startChild({ QStringLiteral("--storage-open-info"), filePath, QStringLiteral("scan") });
    QVERIFY(process->waitForFinished(60000 * factor));
    const QByteArray output = process->readAllStandardOutput();
    QVERIFY2(output.contains("mode=0 checked=0"), output.constData());
    QVERIFY2(output.contains("corrupt"), output.constData());
    QCOMPARE(process->exitCode(), 0);

    const qint64 requested = AppStorageTestHelper::getMaintenanceValue(filePath, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY);
    QVERIFY2(requested > 0 || QFile::exists(pdf::PDFDatabase::getIntegrityCheckMarkerPath(filePath)), qPrintable(QString::number(requested)));

    // The next opening (this process) runs the full check: the file is renamed
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(!state.damagedFilePath.isEmpty());
    QVERIFY(state.integrityChecked);
    QVERIFY(!QFile::exists(pdf::PDFDatabase::getIntegrityCheckMarkerPath(filePath)));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM recent_file"), qint64(0));
}

void AppStorageTest::damageReportedInProcess()
{
    const QString filePath = createPath(QStringLiteral("damage-inprocess"));
    QVERIFY(AppStorageTestHelper::createDamagedThumbnails(filePath));

    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        QVERIFY(!database.getState().integrityChecked);
        QCOMPARE(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY), qint64(-1));

        // The damage is met inside of a transaction, which is rolled back: the record
        // is written later in the GUI thread, not as a part of the transaction
        const qint64 before = QDateTime::currentMSecsSinceEpoch();
        {
            pdf::PDFDatabaseTransaction transaction(database.getConnection());
            QVERIFY(transaction.isActive());
            QCOMPARE(AppStorageTestHelper::readThumbnails(&database).getCode(), 11);
            QCOMPARE(AppStorageTestHelper::readThumbnails(&database).getCode(), 11);
        }
        QTRY_VERIFY(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY) >= before);

        // Errors, which are no damage, request nothing
        QVERIFY(AppStorageTestHelper::execute(&database, "DELETE FROM storage_maintenance WHERE key = 'integrity.requested'"));
        QVERIFY(!AppStorageTestHelper::execute(&database, "SELECT * FROM no_such_table"));
        QTest::qWait(50);
        QCOMPARE(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY), qint64(-1));

        // A damage of a thread: recorded too
        std::atomic<int> code = 0;
        std::thread thread([&database, &code]() { code = AppStorageTestHelper::readThumbnails(&database).getCode(); });
        thread.join();
        QCOMPARE(code.load(), 11);
        QTRY_VERIFY(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY) >= before);
    }

    // The next opening renames the damaged file
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(!database.getState().damagedFilePath.isEmpty());

    // A memory database records nothing
    pdf::PDFDatabase portable;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(QString());
    options.portable = true;
    portable.open(options);
    portable.reportError(pdf::PDFDatabaseError(11, 11, QStringLiteral("test")));
    QTest::qWait(50);
    portable.close();
}

void AppStorageTest::readOnlyFile()
{
    const QString filePath = createPath(QStringLiteral("readonly"));
    QVERIFY(AppStorageTestHelper::createFile(filePath, pdf::PDFDatabaseSchema::getMigrations(), pdf::PDFDatabaseSchema::getCurrentVersion()));
    QVERIFY(QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    const QByteArray hash = AppStorageTestHelper::fileHash(filePath);

    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        const pdf::PDFDatabaseState state = database.getState();
        QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
        QVERIFY(state.isRetryPossible());
        QVERIFY(state.error.isError());
        QVERIFY(!state.getDescription().isEmpty());

        // The readable content is kept, changes go to memory
        pdf::PDFSettingsRepository settings(&database, &registry);
        if (state.contentCopied)
        {
            QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("dark"));
        }
        QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 9000).isError());
        QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));

        // Try Again while the file is still read-only
        QVERIFY(!database.retryOpen());
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
        QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));
    }
    QCOMPARE(AppStorageTestHelper::fileHash(filePath), hash);

    // Writable again: Try Again opens the file, the changes in memory are lost
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
        QVERIFY(QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser));
        QSignalSpy stateSpy(&database, &pdf::PDFDatabase::stateChanged);
        QVERIFY(database.retryOpen());
        QVERIFY(!stateSpy.isEmpty());
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        pdf::PDFSettingsRepository settings(&database, &registry);
        QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("dark"));
        QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(8765));
        QVERIFY(database.retryOpen());
    }
}

void AppStorageTest::newerSchema()
{
    const QString filePath = createPath(QStringLiteral("newer"));
    const std::vector<pdf::PDFDatabaseMigration> extended = AppStorageTestHelper::getExtendedMigrations();
    QVERIFY(AppStorageTestHelper::createFile(filePath, extended, pdf::PDFDatabaseSchema::getVersion(extended)));
    const QByteArray hash = AppStorageTestHelper::fileHash(filePath);

    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createOptions(filePath));
        const pdf::PDFDatabaseState state = database.getState();
        QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema);
        QCOMPARE(state.fileSchemaVersion, pdf::PDFDatabaseSchema::getVersion(extended));
        QCOMPARE(state.applicationSchemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());
        QVERIFY(state.contentCopied);
        QVERIFY(!state.isPersistent());
        QVERIFY(!state.isRetryPossible());
        QVERIFY(!state.getDescription().isEmpty());

        // The content of the newer version is used, changes are kept in memory
        pdf::PDFSettingsRepository settings(&database, &registry);
        QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("dark"));
        QVERIFY(!settings.setValue(QStringLiteral("appearance.colorScheme"), QStringLiteral("light")).isError());
        QCOMPARE(settings.getString(QStringLiteral("appearance.colorScheme")), QStringLiteral("light"));
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM test_extra"), qint64(0));

        // Other threads see the same memory database
        AppStorageWorker worker(&database, 3);
        worker.start();
        QVERIFY(worker.wait(30000 * AppStorageTestHelper::getTimeoutFactor()));
        QCOMPARE(worker.committed.load(), 3);
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM setting WHERE key LIKE 'test.thread%'"), qint64(3));
    }

    // Nothing was written to the file
    QCOMPARE(AppStorageTestHelper::fileHash(filePath), hash);
}

void AppStorageTest::cannotOpenAndRetry()
{
    // The folder of the database is a file
    const QString blocker = m_directory.filePath(QStringLiteral("blocker"));
    {
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    const QString filePath = QDir(blocker).filePath(QStringLiteral("pdf4qt.db"));

    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    QVERIFY(database.getState().error.isError());
    QVERIFY(!database.getState().contentCopied);

    pdf::PDFSettingsRepository settings(&database, &registry);
    QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 9000).isError());

    // Try Again fails: the memory database (and its data) is kept
    pdf::PDFDatabaseNotifier notifier(&database);
    QSignalSpy everythingSpy(&notifier, &pdf::PDFDatabaseNotifier::everythingChanged);
    QVERIFY(!database.retryOpen());
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));

    // Repaired: Try Again succeeds, the notifier reports that everything has changed
    QVERIFY(QFile::remove(blocker));
    QVERIFY(database.retryOpen());
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(QFile::exists(filePath));
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(8765));
    QTRY_VERIFY(everythingSpy.count() >= 1);

    // No path at all
    pdf::PDFDatabase noPath;
    noPath.open(AppStorageTestHelper::createOptions(QString()));
    QCOMPARE(noPath.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    QVERIFY(noPath.getConnection());
}

void AppStorageTest::checkpointWithOpenRead()
{
    const QString filePath = createPath(QStringLiteral("checkpoint"));
    pdf::PDFDatabase writer;
    writer.open(AppStorageTestHelper::createOptions(filePath));
    pdf::PDFDatabase reader;
    reader.open(AppStorageTestHelper::createOptions(filePath));

    QVERIFY(AppStorageTestHelper::execute(&writer, "INSERT INTO setting(key, value, modified) VALUES ('t.initial', 1, 0)"));
    QVERIFY(!writer.checkpoint(true).busy);

    // An open read transaction holds its snapshot
    pdf::PDFDatabaseConnection* readerConnection = reader.getConnection();
    {
        pdf::PDFDatabaseTransaction readTransaction(readerConnection, pdf::PDFDatabaseTransaction::Mode::Read);
        QVERIFY(readTransaction.isActive());
        QCOMPARE(readerConnection->queryInteger("SELECT COUNT(*) FROM setting", -1), qint64(1));

        for (int i = 0; i < 50; ++i)
        {
            pdf::PDFDatabaseTransaction transaction(writer.getConnection());
            pdf::PDFDatabaseStatement statement(writer.getConnection(), "INSERT INTO setting(key, value, modified) VALUES (?1, 1, 0)");
            statement.bindText(1, QStringLiteral("t.c%1").arg(i));
            QVERIFY(statement.execute());
            QVERIFY(transaction.commit());
        }

        // The checkpoint cannot pass the reader, but it does not fail
        const pdf::PDFDatabaseCheckpointResult result = writer.checkpoint(true);
        QVERIFY(!result.error.isError());
        QVERIFY(result.logFrames > 0);
        QVERIFY(result.busy);
        QVERIFY(result.checkpointedFrames < result.logFrames);

        // The reader still sees its snapshot
        QCOMPARE(readerConnection->queryInteger("SELECT COUNT(*) FROM setting", -1), qint64(1));
        QVERIFY(readTransaction.commit());
    }

    // The reader is finished: the checkpoint completes
    const pdf::PDFDatabaseCheckpointResult result = writer.checkpoint(true);
    QVERIFY(!result.error.isError());
    QVERIFY(!result.busy);
    QCOMPARE(result.checkpointedFrames, result.logFrames);
    QCOMPARE(readerConnection->queryInteger("SELECT COUNT(*) FROM setting", -1), qint64(51));

    // A statement read to its end does not hold the snapshot
    pdf::PDFDatabaseStatement statement(readerConnection, "SELECT key FROM setting");
    while (statement.next())
    {
    }
    QVERIFY(AppStorageTestHelper::execute(&writer, "INSERT INTO setting(key, value, modified) VALUES ('t.last', 1, 0)"));
    QVERIFY(!writer.checkpoint(true).busy);
}

void AppStorageTest::threadConnectionsClosed()
{
    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("threadclose"))));
    QVERIFY(database.getConnection());
    QCOMPARE(database.getConnectionCount(), 1);

    // QThread
    std::vector<std::unique_ptr<AppStorageWorker>> workers;
    for (int i = 0; i < 3; ++i)
    {
        workers.push_back(std::make_unique<AppStorageWorker>(&database, 1));
        workers.back()->start();
    }
    for (const std::unique_ptr<AppStorageWorker>& worker : workers)
    {
        QVERIFY(worker->wait(30000 * AppStorageTestHelper::getTimeoutFactor()));
        QCOMPARE(worker->errors.load(), 0);
    }
    QTRY_COMPARE(database.getConnectionCount(), 1);

    // A thread, which is not a QThread
    std::atomic<int> connectionCount = 0;
    std::thread thread([&database, &connectionCount]()
    {
        pdf::PDFDatabaseConnection* connection = database.getConnection();
        if (connection && connection->isValid() && connection->queryInteger("SELECT 1", 0) == 1)
        {
            connectionCount = database.getConnectionCount();
        }
    });
    thread.join();
    QCOMPARE(connectionCount.load(), 2);
    QTRY_COMPARE(database.getConnectionCount(), 1);

    // A thread, which ends after the database has been destroyed
    std::unique_ptr<pdf::PDFDatabase> shortLived = std::make_unique<pdf::PDFDatabase>();
    shortLived->open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("shortlived"))));
    std::atomic<bool> isConnected = false;
    std::atomic<bool> mayEnd = false;
    std::thread lateThread([&shortLived, &isConnected, &mayEnd]()
    {
        isConnected = shortLived->getConnection() != nullptr;
        while (!mayEnd)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    QTRY_VERIFY(isConnected.load());
    shortLived.reset();
    mayEnd = true;
    lateThread.join();
}

void AppStorageTest::portableSharedByThreads()
{
    pdf::PDFSettingRegistry registry;
    pdf::PDFSettingDefinitions::registerBuiltinDefinitions(&registry);

    pdf::PDFDatabase database;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(m_directory.filePath(QStringLiteral("portable-must-not-exist.db")));
    options.portable = true;
    database.open(options);

    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Portable);
    QVERIFY(state.filePath.isEmpty());
    QVERIFY(!state.isPersistent());
    QVERIFY(!state.isRetryPossible());
    QVERIFY(!database.retryOpen());
    QCOMPARE(state.schemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());

    pdf::PDFSettingsRepository settings(&database, &registry);
    QVERIFY(!settings.setValue(QStringLiteral("mcp.port"), 9000).isError());
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('test.main', 42, 0)"));

    pdf::PDFDatabaseNotifier notifier(&database);
    notifier.setPollInterval(20);
    QSignalSpy changedSpy(&notifier, &pdf::PDFDatabaseNotifier::changed);

    // Threads share the memory database (transactions, triggers, notifications)
    AppStorageWorker worker(&database, 10);
    worker.start();
    QVERIFY(worker.wait(30000 * AppStorageTestHelper::getTimeoutFactor()));
    QCOMPARE(worker.errors.load(), 0);
    QCOMPARE(worker.readValue, QVariant(qint64(42)));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM setting WHERE key LIKE 'test.thread%'"), qint64(10));
    QTRY_COMPARE(changedSpy.count(), 10);

    // The database lives as long as the storage, also without any thread connection
    QTRY_COMPARE(database.getConnectionCount(), 1);
    QCOMPARE(settings.getInteger(QStringLiteral("mcp.port")), qint64(9000));

    // Nothing was written to the disk
    QVERIFY(!QFile::exists(options.filePath));
    database.close();
}

void AppStorageTest::portableInvisibleToOtherProcess()
{
    pdf::PDFDatabase database;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createOptions(QString());
    options.portable = true;
    database.open(options);
    QVERIFY(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM sqlite_master") > 20);

    // The same memory database in this process (another connection)
    pdf::PDFDatabaseError error;
    sqlite3* handle = pdf::PDFDatabase::openMemoryConnection(options.memoryName, &error);
    QVERIFY(handle);
    QVERIFY(pdf::PDFDatabase::queryIntegerOnHandle(handle, "SELECT COUNT(*) FROM sqlite_master", -1) > 20);
    pdf::PDFDatabase::closeHandle(handle);

    // Another process does not see it
    const int factor = AppStorageTestHelper::getTimeoutFactor();
    QProcess* process = startChild({ QStringLiteral("--storage-memdb-probe"), options.memoryName });
    QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(30000 * factor));
    const QByteArray output = process->readAllStandardOutput();
    QVERIFY2(output.contains("tables=0"), output.constData());
}

QString AppStorageTest::createFolder(const QString& name)
{
    const QString directory = m_directory.filePath(QStringLiteral("%1-%2").arg(name).arg(++m_fileCounter));
    QDir().mkpath(directory);
    return QDir::cleanPath(QDir(directory).absolutePath());
}

QByteArray AppStorageTest::runUpgradeChild(const QString& directory, int extraVersions, bool crash, int* exitCode)
{
    QProcess* process = startChild({ QStringLiteral("--storage-upgrade"), directory, QString::number(extraVersions), crash ? QStringLiteral("crash") : QString() });
    const int factor = AppStorageTestHelper::getTimeoutFactor();
    if (!(process->state() == QProcess::NotRunning || process->waitForFinished(120000 * factor)))
    {
        *exitCode = -1;
        return QByteArray("timeout");
    }
    *exitCode = process->exitStatus() == QProcess::NormalExit ? process->exitCode() : -2;
    const QByteArray output = process->readAllStandardOutput() + process->readAllStandardError();
    delete process;
    return output;
}

void AppStorageTest::schemaFileNames()
{
    using Files = pdf::PDFDatabaseFiles;

    QCOMPARE(Files::getFileName(1), QStringLiteral("pdf4qt-1.db"));
    QCOMPARE(Files::getFileName(12), QStringLiteral("pdf4qt-12.db"));
    QCOMPARE(Files::getFilePath(QStringLiteral("C:/data"), 3), QStringLiteral("C:/data/pdf4qt-3.db"));
    QVERIFY(Files::getFilePath(QString(), 3).isEmpty());

    QCOMPARE(Files::getSchemaVersion(QStringLiteral("pdf4qt-1.db")), 1);
    QCOMPARE(Files::getSchemaVersion(QStringLiteral("pdf4qt-10.db")), 10);
    QCOMPARE(Files::getSchemaVersion(QStringLiteral("pdf4qt-999999999.db")), 999999999);

    // The development file, other files of SQLite and of the storage, malformed names
    const char* const invalidNames[] =
    {
        "pdf4qt.db", "pdf4qt-0.db", "pdf4qt-01.db", "pdf4qt--1.db", "pdf4qt-+1.db", "pdf4qt-1x.db", "pdf4qt-1.db-wal",
        "pdf4qt-1.db-shm", "pdf4qt-1.db-journal", "pdf4qt-1.db.damaged-2026-10-23-120000", "pdf4qt-1.db.upgrade-12-abc",
        "pdf4qt-1.db-integrity-check", "pdf4qt-1234567890.db", "pdf4qt-99999999999999999999.db", "PDF4QT-1.DB", "xpdf4qt-1.db",
        "pdf4qt-1.dbx", "pdf4qt-1 .db", "pdf4qt-upgrade.lock", "", "pdf4qt-.db"
    };
    for (const char* name : invalidNames)
    {
        QVERIFY2(Files::getSchemaVersion(QString::fromLatin1(name)) == -1, name);
    }

    // Digits of other scripts are no version
    QCOMPARE(Files::getSchemaVersion(QStringLiteral("pdf4qt-") + QChar(0x0661) + QStringLiteral(".db")), -1);

    QVERIFY(Files::isUpgradeTemporaryFileName(QStringLiteral("pdf4qt-2.db.upgrade-12-0badf00d")));
    QVERIFY(Files::isUpgradeTemporaryFileName(QStringLiteral("pdf4qt-2.db.upgrade-12-0badf00d-journal")));
    QVERIFY(!Files::isUpgradeTemporaryFileName(QStringLiteral("pdf4qt-2.db")));
    QVERIFY(!Files::isUpgradeTemporaryFileName(QStringLiteral("pdf4qt.db.upgrade-12-0badf00d")));
    QVERIFY(!Files::isUpgradeTemporaryFileName(QStringLiteral("other-2.db.upgrade-1")));

    QVERIFY(Files::getFiles(QString(), 1).empty());
    QVERIFY(Files::getFiles(m_directory.filePath(QStringLiteral("does-not-exist")), 1).empty());
}

void AppStorageTest::schemaFileFresh()
{
    // A fresh start creates the file of the schema version of the application; the
    // development file pdf4qt.db is ignored and not touched
    const QString directory = createFolder(QStringLiteral("schema-fresh"));
    const QString legacyPath = QDir(directory).filePath(QStringLiteral("pdf4qt.db"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(legacyPath, 1, "legacy"));
    const QByteArray legacyHash = AppStorageTestHelper::fileAndLogHash(legacyPath);

    const QString filePath = pdf::PDFDatabaseFiles::getFilePath(directory, pdf::PDFDatabaseSchema::getCurrentVersion());
    QCOMPARE(QFileInfo(filePath).fileName(), QStringLiteral("pdf4qt-1.db"));
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createFolderOptions(directory));
        const pdf::PDFDatabaseState state = database.getState();
        QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
        QCOMPARE(state.filePath, filePath);
        QVERIFY(state.upgradedFromFilePath.isEmpty());
        QVERIFY(state.damagedFilePath.isEmpty());
        QVERIFY(state.getDescription().isEmpty());
        QCOMPARE(state.schemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());
        QVERIFY(QFile::exists(filePath));
        QVERIFY(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme").isEmpty());
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA application_id"), qint64(pdf::PDFDatabase::APPLICATION_ID));
        QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'dark', 0)"));

        // The file is a WAL database, created with its integrity check
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM pragma_journal_mode WHERE journal_mode = 'wal'"), qint64(1));
        QVERIFY(AppStorageTestHelper::getMaintenanceValue(&database, pdf::PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY) > 0);

        // The list of the files for Settings > Storage
        const std::vector<pdf::PDFDatabaseFileInfo> files = database.getDatabaseFiles();
        QCOMPARE(int(files.size()), 1);
        QCOMPARE(files[0].filePath, filePath);
        QCOMPARE(files[0].schemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());
        QVERIFY(files[0].isCurrent);
        QVERIFY(files[0].size > 0);
        QVERIFY(files[0].modified.isValid());
        QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());
    }

    // The second start opens the same file
    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createFolderOptions(directory));
        QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        QCOMPARE(database.getState().filePath, filePath);
        QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("dark"));
    }

    QCOMPARE(AppStorageTestHelper::fileAndLogHash(legacyPath), legacyHash);

    // A database without a folder (filePath mode) has no list of the files
    pdf::PDFDatabase single;
    single.open(AppStorageTestHelper::createOptions(createPath(QStringLiteral("schema-single"))));
    QVERIFY(single.getDatabaseFiles().empty());
}

void AppStorageTest::schemaFileUpgrade()
{
    // An older version (schema 1) runs and has rows in its write-ahead log, a newer
    // version (schema 2, the test-only list of migrations) starts: its file is a copy
    // of the older file with all data, the older file is not written
    const QString directory = createFolder(QStringLiteral("schema-upgrade"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    const QString newerPath = pdf::PDFDatabaseFiles::getFilePath(directory, 2);
    QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 1, "dark", 50));

    pdf::PDFDatabase older;
    older.open(AppStorageTestHelper::createFolderOptions(directory));
    QCOMPARE(older.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(older.getState().filePath, olderPath);
    QVERIFY(older.getState().upgradedFromFilePath.isEmpty());
    QVERIFY(AppStorageTestHelper::execute(&older, "INSERT INTO setting(key, value, modified) VALUES ('test.inLog', 'yes', 0)"));
    QVERIFY(QFileInfo(olderPath + QStringLiteral("-wal")).size() > 0);
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);

    pdf::PDFDatabase newer;
    newer.open(AppStorageTestHelper::createFolderOptions(directory, 1));
    const pdf::PDFDatabaseState state = newer.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(state.filePath, newerPath);
    QCOMPARE(state.upgradedFromFilePath, olderPath);
    QVERIFY(state.damagedFilePath.isEmpty());
    QCOMPARE(state.schemaVersion, 2);
    QCOMPARE(state.applicationSchemaVersion, 2);
    QVERIFY(state.getDescription().isEmpty());

    // All data, also the rows of the write-ahead log of the running older version
    QCOMPARE(AppStorageTestHelper::getSettingText(&newer, "appearance.colorScheme"), QStringLiteral("dark"));
    QCOMPARE(AppStorageTestHelper::getSettingText(&newer, "test.inLog"), QStringLiteral("yes"));
    QCOMPARE(AppStorageTestHelper::queryInteger(&newer, "SELECT COUNT(*) FROM recent_file WHERE LENGTH(thumbnail) = 8192"), qint64(50));
    QCOMPARE(AppStorageTestHelper::queryInteger(&newer, "PRAGMA user_version"), qint64(2));
    QCOMPARE(AppStorageTestHelper::queryInteger(&newer, "SELECT COUNT(*) FROM test_extra"), qint64(0));
    QCOMPARE(AppStorageTestHelper::queryInteger(&newer, "SELECT COUNT(*) FROM pragma_journal_mode WHERE journal_mode = 'wal'"), qint64(1));
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());

    // The same schema as a new database of the newer version
    {
        pdf::PDFDatabase reference;
        reference.open(AppStorageTestHelper::createFolderOptions(createFolder(QStringLiteral("schema-reference")), 1));
        QCOMPARE(AppStorageTestHelper::getSchema(&newer), AppStorageTestHelper::getSchema(&reference));
    }

    // The older file was not written
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

    // Both versions run with their own files
    QVERIFY(AppStorageTestHelper::execute(&older, "INSERT INTO setting(key, value, modified) VALUES ('test.older', 1, 0)"));
    QVERIFY(AppStorageTestHelper::execute(&newer, "INSERT INTO setting(key, value, modified) VALUES ('test.newer', 1, 0)"));
    QCOMPARE(AppStorageTestHelper::queryInteger(&older, "SELECT COUNT(*) FROM setting WHERE key = 'test.newer'"), qint64(0));
    QCOMPARE(AppStorageTestHelper::queryInteger(&newer, "SELECT COUNT(*) FROM setting WHERE key = 'test.older'"), qint64(0));

    // Another instance of the older version opens and writes its file
    {
        pdf::PDFDatabase secondOlder;
        secondOlder.open(AppStorageTestHelper::createFolderOptions(directory));
        QCOMPARE(secondOlder.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        QCOMPARE(secondOlder.getState().filePath, olderPath);
        QCOMPARE(AppStorageTestHelper::queryInteger(&secondOlder, "SELECT COUNT(*) FROM setting WHERE key = 'test.older'"), qint64(1));
        QVERIFY(AppStorageTestHelper::execute(&secondOlder, "INSERT INTO setting(key, value, modified) VALUES ('test.secondOlder', 1, 0)"));
    }
    QCOMPARE(AppStorageTestHelper::queryInteger(&older, "SELECT COUNT(*) FROM setting WHERE key = 'test.secondOlder'"), qint64(1));

    // The list of the files: the newer one is the current one
    const std::vector<pdf::PDFDatabaseFileInfo> files = newer.getDatabaseFiles();
    QCOMPARE(int(files.size()), 2);
    QCOMPARE(files[0].filePath, newerPath);
    QVERIFY(files[0].isCurrent);
    QCOMPARE(files[1].filePath, olderPath);
    QCOMPARE(files[1].schemaVersion, 1);
    QVERIFY(!files[1].isCurrent);

    older.close();
    newer.close();

    // The next start of the newer version opens its file, nothing is copied again
    pdf::PDFDatabase again;
    again.open(AppStorageTestHelper::createFolderOptions(directory, 1));
    QCOMPARE(again.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(again.getState().upgradedFromFilePath.isEmpty());
    QCOMPARE(AppStorageTestHelper::queryInteger(&again, "SELECT COUNT(*) FROM setting WHERE key = 'test.newer'"), qint64(1));
    QCOMPARE(AppStorageTestHelper::queryInteger(&again, "SELECT COUNT(*) FROM setting WHERE key = 'test.older'"), qint64(0));
}

void AppStorageTest::schemaFileCrashedOlder()
{
    // An older version crashed and left rows in its write-ahead log: the copy has them,
    // the older file and its log are not written (no checkpoint by the copy)
    const QString directory = createFolder(QStringLiteral("schema-crashed-older"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 1, "dark", 10));

    QProcess* process = startChild({ QStringLiteral("--storage-crash-writer"), directory });
    QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(120000 * AppStorageTestHelper::getTimeoutFactor()));
    const QByteArray output = process->readAllStandardOutput();
    QVERIFY2(process->exitCode() == 33 && output.contains("written"), output.constData());
    delete process;
    QVERIFY(QFileInfo(olderPath + QStringLiteral("-wal")).size() > 0);
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);

    {
        pdf::PDFDatabase newer;
        newer.open(AppStorageTestHelper::createFolderOptions(directory, 1));
        QCOMPARE(newer.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
        QCOMPARE(newer.getState().upgradedFromFilePath, olderPath);
        QCOMPARE(AppStorageTestHelper::getSettingText(&newer, "test.crash"), QStringLiteral("yes"));
        QCOMPARE(AppStorageTestHelper::getSettingText(&newer, "appearance.colorScheme"), QStringLiteral("dark"));
    }
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

    // The older version recovers its log as usual
    pdf::PDFDatabase older;
    older.open(AppStorageTestHelper::createFolderOptions(directory));
    QCOMPARE(older.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(AppStorageTestHelper::getSettingText(&older, "test.crash"), QStringLiteral("yes"));
}

void AppStorageTest::schemaFileNewestOlder()
{
    // Schema 3 of the application: the newest older file (2) is copied; malformed names,
    // the development file and a newer file are ignored and not touched; a temporary
    // file of an interrupted upgrade is removed
    const QString directory = createFolder(QStringLiteral("schema-newest"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(QDir(directory).filePath(QStringLiteral("pdf4qt-1.db")), 1, "one"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(QDir(directory).filePath(QStringLiteral("pdf4qt-2.db")), 2, "two"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(QDir(directory).filePath(QStringLiteral("pdf4qt.db")), 1, "legacy"));

    const QString wrongSource = m_directory.filePath(QStringLiteral("schema-wrong-%1.db").arg(++m_fileCounter));
    QVERIFY(AppStorageTestHelper::createSchemaFile(wrongSource, 2, "wrong"));
    const QStringList ignoredNames = { QStringLiteral("pdf4qt-02.db"), QStringLiteral("pdf4qt-2x.db"), QStringLiteral("pdf4qt-0.db"), QStringLiteral("pdf4qt-99999999999.db"),
                                       QStringLiteral("pdf4qt-2.db.damaged-2026-10-23-120000"), QStringLiteral("pdf4qt-2.5.db"), QStringLiteral("pdf4qt-+2.db") };
    for (const QString& name : ignoredNames)
    {
        QVERIFY(AppStorageTestHelper::copyFile(wrongSource, QDir(directory).filePath(name)));
    }
    {
        QFile newerGarbage(QDir(directory).filePath(QStringLiteral("pdf4qt-9.db")));
        QVERIFY(newerGarbage.open(QIODevice::WriteOnly));
        newerGarbage.write(QByteArray(8192, 'x'));
        QFile temporary(QDir(directory).filePath(QStringLiteral("pdf4qt-3.db.upgrade-1-0badf00d")));
        QVERIFY(temporary.open(QIODevice::WriteOnly));
        temporary.write("partial");
        QFile temporaryJournal(QDir(directory).filePath(QStringLiteral("pdf4qt-3.db.upgrade-1-0badf00d-journal")));
        QVERIFY(temporaryJournal.open(QIODevice::WriteOnly));
        temporaryJournal.write("partial");
    }
    QVERIFY(QDir(directory).mkdir(QStringLiteral("pdf4qt-4.db")));

    QMap<QString, QByteArray> hashes;
    for (const QString& name : QDir(directory).entryList(QDir::Files))
    {
        if (!pdf::PDFDatabaseFiles::isUpgradeTemporaryFileName(name))
        {
            hashes[name] = AppStorageTestHelper::fileHash(QDir(directory).filePath(name));
        }
    }

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createFolderOptions(directory, 2));
    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(state.filePath, QDir(directory).filePath(QStringLiteral("pdf4qt-3.db")));
    QCOMPARE(state.upgradedFromFilePath, QDir(directory).filePath(QStringLiteral("pdf4qt-2.db")));
    QCOMPARE(state.schemaVersion, 3);
    QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("two"));
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM pragma_table_info('setting') WHERE name = 'test_column3'"), qint64(1));
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());

    for (auto it = hashes.cbegin(); it != hashes.cend(); ++it)
    {
        QVERIFY2(AppStorageTestHelper::fileHash(QDir(directory).filePath(it.key())) == it.value(), qPrintable(it.key()));
    }

    // The files of the list: 9 (newer), 3 (current), 2, 1 - nothing else
    const std::vector<pdf::PDFDatabaseFileInfo> files = database.getDatabaseFiles();
    QCOMPARE(int(files.size()), 4);
    QCOMPARE(files[0].schemaVersion, 9);
    QCOMPARE(files[1].schemaVersion, 3);
    QVERIFY(files[1].isCurrent);
    QCOMPARE(files[2].schemaVersion, 2);
    QCOMPARE(files[3].schemaVersion, 1);
}

void AppStorageTest::schemaFileOnlyNewer()
{
    // Only a newer file (an older version started after a newer one): its content is
    // copied into memory, nothing is written (no file of the own version, no lock)
    const QString directory = createFolder(QStringLiteral("schema-newer"));
    const QString newerPath = QDir(directory).filePath(QStringLiteral("pdf4qt-2.db"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(newerPath, 2, "dark"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(QDir(directory).filePath(QStringLiteral("pdf4qt.db")), 1, "legacy"));
    const QByteArray hash = AppStorageTestHelper::fileAndLogHash(newerPath);
    const QStringList namesBefore = AppStorageTestHelper::getDatabaseNames(directory);

    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createFolderOptions(directory));
        const pdf::PDFDatabaseState state = database.getState();
        QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::ReadOnlyNewerSchema);
        QCOMPARE(state.filePath, newerPath);
        QCOMPARE(state.fileSchemaVersion, 2);
        QCOMPARE(state.schemaVersion, 2);
        QCOMPARE(state.applicationSchemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());
        QVERIFY(state.contentCopied);
        QVERIFY(!state.isPersistent());
        QVERIFY(!state.getDescription().isEmpty());
        QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("dark"));
        QVERIFY(AppStorageTestHelper::execute(&database, "UPDATE setting SET value = 'light' WHERE key = 'appearance.colorScheme'"));
        QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("light"));
    }

    QCOMPARE(AppStorageTestHelper::fileAndLogHash(newerPath), hash);
    QCOMPARE(AppStorageTestHelper::getDatabaseNames(directory), namesBefore);
    QVERIFY(!QFile::exists(QDir(directory).filePath(QStringLiteral("pdf4qt-1.db"))));
    QVERIFY(!QFile::exists(pdf::PDFDatabaseFiles::getUpgradeLockPath(directory)));
}

void AppStorageTest::schemaFileConcurrent_data()
{
    QTest::addColumn<int>("processCount");
    QTest::newRow("2 processes") << 2;
    QTest::newRow("4 processes") << 4;
}

void AppStorageTest::schemaFileConcurrent()
{
    // Instances of the newer version start at once: exactly one creates the file,
    // the others wait for the lock and open it
    QFETCH(int, processCount);
    const QString directory = createFolder(QStringLiteral("schema-concurrent"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 1, "dark", 2000));
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);

    QList<QProcess*> processes;
    for (int i = 0; i < processCount; ++i)
    {
        processes << startChild({ QStringLiteral("--storage-upgrade"), directory, QStringLiteral("1"), QString() });
    }

    const int factor = AppStorageTestHelper::getTimeoutFactor();
    int upgradeCount = 0;
    for (QProcess* process : processes)
    {
        QVERIFY(process->state() == QProcess::NotRunning || process->waitForFinished(300000 * factor));
        const QByteArray output = process->readAllStandardOutput() + process->readAllStandardError();
        QVERIFY2(process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0, output.constData());
        QVERIFY2(output.contains("mode=0 ") && output.contains(" version=2 value=dark"), output.constData());
        if (output.contains("upgraded=1"))
        {
            ++upgradeCount;
        }
    }
    qDeleteAll(processes);

    QCOMPARE(upgradeCount, 1);
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createFolderOptions(directory, 1));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(database.getState().upgradedFromFilePath.isEmpty());
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM recent_file"), qint64(2000));
}

void AppStorageTest::schemaFileCrash()
{
    // The process dies after the copy: only its temporary file is left, the next start
    // removes it and does the upgrade again
    const QString directory = createFolder(QStringLiteral("schema-crash"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    const QString newerPath = pdf::PDFDatabaseFiles::getFilePath(directory, 2);
    QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 1, "dark", 20));
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);

    int exitCode = 0;
    const QByteArray output = runUpgradeChild(directory, 1, true, &exitCode);
    QVERIFY2(exitCode == pdf::PDFDatabaseOptions::TEST_EXIT_CODE, output.constData());
    QVERIFY(!QFile::exists(newerPath));
    QVERIFY(!AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

    // The lock was released by the end of the process: another process does the upgrade
    const QByteArray secondOutput = runUpgradeChild(directory, 1, false, &exitCode);
    QCOMPARE(exitCode, 0);
    QVERIFY2(secondOutput.contains("mode=0 upgraded=1 version=2 value=dark"), secondOutput.constData());
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());
    QVERIFY(QFile::exists(newerPath));

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createFolderOptions(directory, 1));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM recent_file"), qint64(20));
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

    // In this process: a temporary file left by a crash is removed by the next upgrade
    const QString secondDirectory = createFolder(QStringLiteral("schema-crash-local"));
    QVERIFY(AppStorageTestHelper::createSchemaFile(pdf::PDFDatabaseFiles::getFilePath(secondDirectory, 1), 1, "light"));
    const QByteArray secondCrash = runUpgradeChild(secondDirectory, 1, true, &exitCode);
    QVERIFY2(exitCode == pdf::PDFDatabaseOptions::TEST_EXIT_CODE, secondCrash.constData());
    QVERIFY(!AppStorageTestHelper::getUpgradeTemporaryFiles(secondDirectory).isEmpty());
    pdf::PDFDatabase local;
    local.open(AppStorageTestHelper::createFolderOptions(secondDirectory, 1));
    QCOMPARE(local.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(!local.getState().upgradedFromFilePath.isEmpty());
    QCOMPARE(AppStorageTestHelper::getSettingText(&local, "appearance.colorScheme"), QStringLiteral("light"));
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(secondDirectory).isEmpty());
}

void AppStorageTest::schemaFileUnusableOlder_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("garbage") << 0;
    QTest::newRow("foreign application") << 1;
    QTest::newRow("corrupted page") << 2;
    QTest::newRow("version of the name differs") << 3;
}

void AppStorageTest::schemaFileUnusableOlder()
{
    // The older file cannot be copied and migrated: it is kept unchanged under its
    // name, the file of the new version starts empty and the user is told
    QFETCH(int, kind);
    const QString directory = createFolder(QStringLiteral("schema-unusable"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    const QString newerPath = pdf::PDFDatabaseFiles::getFilePath(directory, 2);
    switch (kind)
    {
        case 0:
        {
            QFile file(olderPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QRandomGenerator random(7);
            QByteArray data(64 * 1024, Qt::Uninitialized);
            random.fillRange(reinterpret_cast<quint32*>(data.data()), data.size() / 4);
            file.write(data);
            break;
        }

        case 1:
        {
            sqlite3* handle = pdf::PDFDatabase::openFileConnection(olderPath, false, 1000, nullptr);
            QVERIFY(handle);
            QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "PRAGMA application_id = 42; CREATE TABLE foreign_data(x)").isError());
            pdf::PDFDatabase::closeHandle(handle);
            break;
        }

        case 2:
            QVERIFY(AppStorageTestHelper::createDamagedThumbnails(olderPath));
            break;

        case 3:
            // A file of the schema 5 with the name of the schema 1
            QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 5, "dark"));
            break;
    }
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);

    {
        pdf::PDFDatabase database;
        database.open(AppStorageTestHelper::createFolderOptions(directory, 1));
        const pdf::PDFDatabaseState state = database.getState();
        QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
        QCOMPARE(state.filePath, newerPath);
        QCOMPARE(state.damagedFilePath, olderPath);
        QVERIFY(state.upgradedFromFilePath.isEmpty());
        QVERIFY(state.getDescription().contains(QDir::toNativeSeparators(olderPath)));
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM setting"), qint64(0));
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "SELECT COUNT(*) FROM recent_file"), qint64(0));
        QCOMPARE(AppStorageTestHelper::queryInteger(&database, "PRAGMA user_version"), qint64(2));
        QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'light', 0)"));
    }

    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());

    // The next start opens the new file without a message
    pdf::PDFDatabase again;
    again.open(AppStorageTestHelper::createFolderOptions(directory, 1));
    QCOMPARE(again.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QVERIFY(again.getState().damagedFilePath.isEmpty());
    QCOMPARE(AppStorageTestHelper::getSettingText(&again, "appearance.colorScheme"), QStringLiteral("light"));
}

void AppStorageTest::schemaFileBrokenMigration()
{
    // A migration of the new version fails (also for a new file): no file of the new
    // version, the memory fallback with the content of the older file, Try Again
    const QString directory = createFolder(QStringLiteral("schema-broken"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 1, "dark"));
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);

    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createFolderOptions(directory);
    options.migrations = pdf::PDFDatabaseSchema::getMigrations();
    pdf::PDFDatabaseMigration broken;
    broken.version = int(options.migrations.size()) + 1;
    broken.sql = "CREATE TABLE test_partial(id INTEGER); THIS IS NOT SQL;";
    options.migrations.push_back(broken);

    pdf::PDFDatabase database;
    database.open(options);
    const pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    QVERIFY(state.error.isError());
    QVERIFY(state.contentCopied);
    QVERIFY(state.isRetryPossible());
    QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("dark"));
    QVERIFY(!QFile::exists(pdf::PDFDatabaseFiles::getFilePath(directory, broken.version)));
    QVERIFY(AppStorageTestHelper::getUpgradeTemporaryFiles(directory).isEmpty());

    QVERIFY(!database.retryOpen());
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    database.close();
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);
}

void AppStorageTest::schemaFileFolderErrors()
{
    // The file of the new version cannot be created (the lock cannot be created - a
    // folder of its name): the memory fallback with the content of the older file
    // migrated in memory, the older file unchanged; Try Again does the upgrade
    const QString directory = createFolder(QStringLiteral("schema-folder"));
    const QString olderPath = pdf::PDFDatabaseFiles::getFilePath(directory, 1);
    const QString newerPath = pdf::PDFDatabaseFiles::getFilePath(directory, 2);
    QVERIFY(AppStorageTestHelper::createSchemaFile(olderPath, 1, "dark"));
    const QByteArray olderHash = AppStorageTestHelper::fileAndLogHash(olderPath);
    const QString lockPath = pdf::PDFDatabaseFiles::getUpgradeLockPath(directory);
    QVERIFY(QDir().mkpath(lockPath));

    pdf::PDFDatabase database;
    database.open(AppStorageTestHelper::createFolderOptions(directory, 1));
    pdf::PDFDatabaseState state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
    QVERIFY(state.isRetryPossible());
    QVERIFY(state.error.isError());
    QVERIFY(state.contentCopied);
    QCOMPARE(state.filePath, newerPath);
    QCOMPARE(state.schemaVersion, 2);
    QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("dark"));
    QVERIFY(!QFile::exists(newerPath));
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

    // Repaired: Try Again creates the file from the older one
    QVERIFY(QDir(lockPath).removeRecursively());
    QVERIFY(database.retryOpen());
    state = database.getState();
    QCOMPARE(state.mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(state.upgradedFromFilePath, olderPath);
    QCOMPARE(AppStorageTestHelper::getSettingText(&database, "appearance.colorScheme"), QStringLiteral("dark"));
    database.close();
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(olderPath), olderHash);

#if defined(Q_OS_UNIX)
    // A read-only folder (Unix: Windows ignores the read-only attribute of a folder)
    const QString readOnlyDirectory = createFolder(QStringLiteral("schema-readonly"));
    const QString readOnlyOlder = pdf::PDFDatabaseFiles::getFilePath(readOnlyDirectory, 1);
    QVERIFY(AppStorageTestHelper::createSchemaFile(readOnlyOlder, 1, "dark"));
    const QByteArray readOnlyHash = AppStorageTestHelper::fileAndLogHash(readOnlyOlder);
    QVERIFY(QFile::setPermissions(readOnlyDirectory, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QFile probe(QDir(readOnlyDirectory).filePath(QStringLiteral("probe")));
    if (probe.open(QIODevice::WriteOnly))
    {
        probe.close();
        probe.remove();
        QVERIFY(QFile::setPermissions(readOnlyDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QSKIP("The folder cannot be made read-only (root)");
    }
    {
        pdf::PDFDatabase readOnly;
        readOnly.open(AppStorageTestHelper::createFolderOptions(readOnlyDirectory, 1));
        QCOMPARE(readOnly.getState().mode, pdf::PDFDatabaseStorageMode::MemoryFallback);
        QVERIFY(readOnly.getState().isRetryPossible());
        QCOMPARE(readOnly.getState().schemaVersion, 2);
    }
    QVERIFY(QFile::setPermissions(readOnlyDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QCOMPARE(AppStorageTestHelper::fileAndLogHash(readOnlyOlder), readOnlyHash);
    QVERIFY(!QFile::exists(pdf::PDFDatabaseFiles::getFilePath(readOnlyDirectory, 2)));
#endif
}

void AppStorageTest::schemaFilePortable()
{
    // The portable mode writes nothing: no folder, no file, no lock
    const QString directory = QDir(createFolder(QStringLiteral("schema-portable"))).filePath(QStringLiteral("data"));
    pdf::PDFDatabase database;
    pdf::PDFDatabaseOptions options = AppStorageTestHelper::createFolderOptions(directory, 1);
    options.portable = true;
    database.open(options);
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Portable);
    QVERIFY(database.getState().filePath.isEmpty());
    QCOMPARE(database.getState().schemaVersion, 2);
    QVERIFY(AppStorageTestHelper::execute(&database, "INSERT INTO setting(key, value, modified) VALUES ('appearance.colorScheme', 'dark', 0)"));
    database.close();
    QVERIFY(!QDir(directory).exists());
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of more processes
    if (argc > 1 && QByteArray(argv[1]).startsWith("--storage-"))
    {
        QCoreApplication application(argc, argv);
        return AppStorageChild::run(application.arguments());
    }

    QCoreApplication application(argc, argv);
    AppStorageTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appstoragetest.moc"
