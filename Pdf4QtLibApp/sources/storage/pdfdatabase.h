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

#ifndef PDFDATABASE_H
#define PDFDATABASE_H

#include "pdfappglobal.h"
#include "pdfdatabaseschema.h"

#include <QObject>
#include <QString>
#include <QMutex>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

struct sqlite3;

namespace pdf
{

class PDFDatabase;
class PDFDatabaseRegistry;

/// Error of an operation of the database. An operation of the storage never throws,
/// it returns this object (code SQLITE_OK = 0 means no error).
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseError
{
public:
    PDFDatabaseError() = default;
    PDFDatabaseError(int code, int extendedCode, QString message);

    /// Error of the last operation of the connection (\p code is the result of the operation)
    static PDFDatabaseError fromHandle(sqlite3* handle, int code);

    /// Error, which does not come from SQLite (invalid argument, unknown setting, ...)
    static PDFDatabaseError fromMessage(QString message);

    bool isError() const { return m_code != 0; }

    /// Another connection (another instance) holds the lock of the database
    bool isBusy() const;

    int getCode() const { return m_code; }
    int getExtendedCode() const { return m_extendedCode; }
    const QString& getMessage() const { return m_message; }

    /// Text for the log (code and message)
    QString toString() const;

private:
    int m_code = 0;
    int m_extendedCode = 0;
    QString m_message;
};

/// Where the data of the application are stored (newapp_storage.md 1 and 5).
/// Every mode except Disk is shown by a banner on the pages of the Home tab
/// (Figma pdf4qt-settings-storage-banners 958:28036).
enum class PDFDatabaseStorageMode
{
    Disk,                   ///< Database file on the disk, shared by all instances
    Portable,               ///< --portable: database only in memory, nothing is written to the disk
    ReadOnlyNewerSchema,    ///< The file has a newer schema: its content was copied into memory, changes are lost at exit
    MemoryFallback          ///< The file cannot be opened: database in memory, PDFDatabase::retryOpen is offered (Try Again)
};

/// State of the storage for the user interface
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseState
{
public:
    PDFDatabaseStorageMode mode = PDFDatabaseStorageMode::MemoryFallback;

    /// File of the database on the disk (empty in the portable mode)
    QString filePath;

    /// When not empty, the file was damaged and has been renamed to this path,
    /// a new database was created (the user gets a message, mode is Disk)
    QString damagedFilePath;

    /// Version of the schema stored in the file (ReadOnlyNewerSchema: newer than the application knows)
    int fileSchemaVersion = 0;

    /// Version of the schema of the open database
    int schemaVersion = 0;

    /// Version of the schema, which the application knows
    int applicationSchemaVersion = 0;

    /// Reason of the memory fallback (error of the disk database)
    PDFDatabaseError error;

    /// The content of the disk file could be copied into memory (ReadOnlyNewerSchema,
    /// MemoryFallback with a readable file)
    bool contentCopied = false;

    /// The full integrity check of the file (PRAGMA quick_check) ran at this opening
    /// and the file passed it (see PDFDatabaseOptions::integrityCheckInterval)
    bool integrityChecked = false;

    /// Data are written to the disk
    bool isPersistent() const { return mode == PDFDatabaseStorageMode::Disk; }

    /// Try Again (PDFDatabase::retryOpen) makes sense
    bool isRetryPossible() const { return mode == PDFDatabaseStorageMode::MemoryFallback; }

    /// Translated text of the reason for the banners and the log
    QString getDescription() const;
};

/// Parameters of PDFDatabase::open
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseOptions
{
public:
    /// File of the database (PDFAppPaths::getDatabaseFilePath). Ignored in the portable mode.
    QString filePath;

    /// Portable mode: database in memory (memdb VFS), nothing is written to the disk
    bool portable = false;

    /// Name of the memory database shared by all connections of the process,
    /// it must start by '/' (URI file:/pdf4qt?vfs=memdb). Tests use their own names.
    QString memoryName = QStringLiteral("/pdf4qt");

    /// Busy timeout of the connections (newapp_storage.md 2: 5000 ms)
    int busyTimeout = 5000;

    /// Longest time in milliseconds, for which the opening waits for a database file
    /// locked by another instance (a migration in progress, SQLITE_BUSY), before it
    /// falls back to the memory database. -1 (default) = it waits until the lock is
    /// released (newapp_storage.md 5, decision of the owner). Only tests set a limit.
    int lockWaitLimit = -1;

    /// Default interval of the full integrity check (7 days)
    static constexpr qint64 DEFAULT_INTEGRITY_CHECK_INTERVAL = qint64(7) * 24 * 3600 * 1000;

    /// Interval of the full integrity check of the disk file in milliseconds.
    /// PRAGMA quick_check reads the whole file (thumbnails of the recent files), so it
    /// runs at the opening only when the last successful check is older than this,
    /// when an instance has met SQLITE_CORRUPT / SQLITE_NOTADB since then, or when
    /// the time of the check cannot be read. The header (application id) and the
    /// schema are checked at every opening. 0 = full check at every opening.
    qint64 integrityCheckInterval = DEFAULT_INTEGRITY_CHECK_INTERVAL;

    /// Migrations of the schema. Empty = PDFDatabaseSchema::getMigrations().
    /// Tests pass a longer list to simulate a newer version of the application.
    std::vector<PDFDatabaseMigration> migrations;
};

/// Result of PDFDatabase::checkpoint
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseCheckpointResult
{
public:
    PDFDatabaseError error;
    int logFrames = -1;             ///< Frames in the WAL (-1 = not a WAL database)
    int checkpointedFrames = -1;    ///< Frames written back into the database file
    bool busy = false;              ///< The checkpoint could not run to completion (a reader or a writer)
};

/// One connection to the database. It belongs to one thread, it is created by
/// PDFDatabase::getConnection and closed when its thread ends, when the database
/// is closed or reopened. Never keep the pointer: ask PDFDatabase::getConnection
/// each time (it is a lookup of the current thread).
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseConnection
{
public:
    ~PDFDatabaseConnection();

    PDFDatabaseConnection(const PDFDatabaseConnection&) = delete;
    PDFDatabaseConnection& operator=(const PDFDatabaseConnection&) = delete;

    /// Handle of SQLite (nullptr, if the connection could not be opened)
    sqlite3* getHandle() const { return m_handle; }

    bool isValid() const { return m_handle != nullptr; }

    /// Database of the connection
    PDFDatabase* getDatabase() const { return m_database; }

    /// Error of the opening of the connection
    const PDFDatabaseError& getOpenError() const { return m_openError; }

    /// Executes SQL without parameters and without result (may contain more statements)
    PDFDatabaseError execute(const char* sql);

    /// Executes a statement returning one integer (PRAGMA user_version, ...);
    /// \returns \p defaultValue on an error
    qint64 queryInteger(const char* sql, qint64 defaultValue, PDFDatabaseError* error = nullptr);

    /// Row id of the last insertion
    qint64 getLastInsertRowId() const;

    /// Rows changed by the last statement
    qint64 getChanges() const;

    /// The connection is inside of a transaction
    bool isInTransaction() const;

    /// Generation of the database, for which the connection was opened
    quint64 getGeneration() const { return m_generation; }

    /// Busy timeout of the connection in milliseconds (PDFDatabaseOptions::busyTimeout).
    /// An operation, which must never block the thread of the user interface, sets
    /// it to 0 for its time and tries again later on SQLITE_BUSY.
    int getBusyTimeout() const { return m_busyTimeout; }
    void setBusyTimeout(int milliseconds);

private:
    friend class PDFDatabase;
    friend class PDFDatabaseRegistry;

    PDFDatabaseConnection(PDFDatabase* database, quint64 generation);

    PDFDatabase* m_database = nullptr;
    sqlite3* m_handle = nullptr;
    quint64 m_generation = 0;
    int m_busyTimeout = 0;
    PDFDatabaseError m_openError;
};

/// The database of the application (newapp_storage.md): one SQLite file per user in
/// the WAL mode shared by all running instances, or a database in memory (portable
/// mode, fallbacks). Every thread has its own connection (getConnection). Reads go
/// always to the database, nothing is cached.
///
/// Use: create it in the thread of the user interface, call open() once (it never
/// fails - the worst case is a database in memory, see getState()), then use the
/// repositories. Threads, which use the database (thumbnails, MCP), must stop before
/// the database is destroyed or closed. Functions are thread safe unless noted.
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabase : public QObject
{
    Q_OBJECT

public:
    explicit PDFDatabase(QObject* parent = nullptr);
    virtual ~PDFDatabase() override;

    /// Value of PRAGMA application_id of the database of PDF4QT ('P4QT')
    static constexpr qint32 APPLICATION_ID = 0x50345154;

    /// Opens the database (GUI thread). Damaged file is renamed and replaced,
    /// a newer schema is copied into memory, a file, which cannot be opened,
    /// leads to the memory fallback. A file locked by another instance is waited
    /// for until the lock is released (PDFDatabaseOptions::lockWaitLimit).
    /// Previously open database is closed.
    void open(const PDFDatabaseOptions& options);

    /// Try Again of the memory fallback: opens the disk database again (GUI thread).
    /// Changes made in the memory database are lost. \returns true, if the database
    /// is on the disk now. Connections of all threads are reopened at their next use.
    bool retryOpen();

    /// Closes the database: PRAGMA optimize and wal_checkpoint(PASSIVE) on a disk
    /// database, then closes the connections of all threads (GUI thread)
    void close();

    /// Database is open (in any mode)
    bool isOpen() const;

    /// State of the storage (copy, thread safe)
    PDFDatabaseState getState() const;

    /// Options of the last open()
    const PDFDatabaseOptions& getOptions() const { return m_options; }

    /// Returns the connection of the current thread, it is opened at the first use.
    /// Returns nullptr only, if the database is not open. The connection may be
    /// invalid (isValid), when SQLite cannot open it - operations then report errors.
    PDFDatabaseConnection* getConnection();

    /// Number of open connections of all threads (without the connection, which
    /// keeps the memory database alive)
    int getConnectionCount() const;

    /// Runs a checkpoint of the WAL on the connection of the current thread
    /// (passive = never waits for readers and writers)
    PDFDatabaseCheckpointResult checkpoint(bool passive = true);

    /// Generation of the database, it is increased by each open / retryOpen
    quint64 getGeneration() const { return m_generation.load(); }

    /// Reports an error of an operation (any thread; statements, transactions and
    /// connections call it). SQLITE_CORRUPT / SQLITE_NOTADB of the disk database is
    /// recorded in the database (table storage_maintenance, it is not a change of the
    /// data and it is never notified), so the next opening by any instance runs the
    /// full integrity check. The record is written later in the GUI thread without
    /// waiting for other instances, at the latest by close(); when the database
    /// refuses it, a marker file next to the database is created instead.
    void reportError(const PDFDatabaseError& error);

    /// Marker file requesting the full integrity check of \p filePath at the next
    /// opening (used, when the request cannot be written into the database)
    static QString getIntegrityCheckMarkerPath(const QString& filePath);

    /// Called by PDFDatabaseTransaction after a commit of a write transaction (any
    /// thread): the notifier of the changes is woken up in the GUI thread, so the
    /// own instance sees its own changes at once (newapp_storage.md 3, point 3)
    void notifyCommitted();

    /// Opens a raw connection to the database file \p filePath with the pragmas of the
    /// application (tests use it to prepare files). \returns nullptr on an error.
    static sqlite3* openFileConnection(const QString& filePath, bool readOnly, int busyTimeout, PDFDatabaseError* error);

    /// Closes a raw connection
    static void closeHandle(sqlite3* handle);

    /// Opens a raw connection to the memory database \p memoryName of this process
    /// (memdb VFS, the name starts by '/'). \returns nullptr on an error.
    static sqlite3* openMemoryConnection(const QString& memoryName, PDFDatabaseError* error);

    /// Executes SQL on a raw connection (tools and tests, which must not link SQLite
    /// themselves - a second copy of SQLite in the process breaks the file locks)
    static PDFDatabaseError executeOnHandle(sqlite3* handle, const char* sql);

    /// Executes a statement returning one integer on a raw connection
    static qint64 queryIntegerOnHandle(sqlite3* handle, const char* sql, qint64 defaultValue);

    /// Version of the linked SQLite library
    static QString getSQLiteVersion();

Q_SIGNALS:
    /// Mode of the storage has changed (open, retryOpen, close)
    void stateChanged();

    /// A write transaction was committed in this process (emitted in the GUI thread, queued)
    void committed();

private:
    friend class PDFDatabaseConnection;

    void openImpl();
    void closeConnections();
    bool openDisk(PDFDatabaseError* error);
    bool openDiskWithRetry(PDFDatabaseError* error);
    bool openMemory(const QString& copyFromFile, PDFDatabaseStorageMode mode);
    bool isFileDamaged(sqlite3* handle, const QString& filePath, bool forceFullCheck, bool* fullCheckPerformed, PDFDatabaseError* error) const;
    bool isIntegrityCheckDue(sqlite3* handle, const QString& filePath) const;
    void recordIntegrityCheck(sqlite3* handle, const QString& filePath, qint64 checkTime) const;
    void persistDamageReport();
    bool writeDamageReport(bool isClosing);
    QString renameDamagedFile(const QString& filePath) const;
    void setState(const PDFDatabaseState& state);
    void processCommitted();
    QString getMemoryUri() const;
    const std::vector<PDFDatabaseMigration>& getMigrations() const;

    PDFDatabaseOptions m_options;
    std::vector<PDFDatabaseMigration> m_builtinMigrations;
    std::shared_ptr<PDFDatabaseRegistry> m_registry;

    mutable QMutex m_stateMutex;
    PDFDatabaseState m_state;
    bool m_isOpen = false;

    /// Connection, which keeps the memory database alive for the whole run
    sqlite3* m_memoryKeeper = nullptr;

    std::atomic<quint64> m_generation = 0;
    std::atomic_bool m_commitPending = false;

    /// Time of the first damage (SQLITE_CORRUPT / SQLITE_NOTADB) reported by reportError,
    /// which is not yet written into the database (0 = none)
    std::atomic<qint64> m_damageReportTime = 0;

    /// Retry of the write of the damage report, when another instance writes
    QTimer m_damageReportTimer;

    /// The opening runs the full integrity check (damage found after the cheap checks)
    bool m_forceIntegrityCheck = false;
};

}   // namespace pdf

#endif // PDFDATABASE_H
