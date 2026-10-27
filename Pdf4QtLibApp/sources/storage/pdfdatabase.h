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

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QTimer>
#include <QVariant>

#include <atomic>
#include <map>
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
    /// a new database was created (the user gets a message, mode is Disk). It is
    /// also an older database file (O19), which could not be copied and migrated
    /// into the file of the current schema version: that file is kept unchanged
    /// under its name and the new database is empty.
    QString damagedFilePath;

    /// When not empty, the file of the database was created at this opening from
    /// a copy of this older database file (O19, PDFDatabaseOptions::directory),
    /// the older file is unchanged
    QString upgradedFromFilePath;

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

/// Database file of the folder of the data (owner decision O19)
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseFileInfo
{
public:
    /// Absolute path of the file
    QString filePath;

    /// Version of the schema given by the name of the file (pdf4qt-<schema>.db)
    int schemaVersion = 0;

    /// Size of the file and of its write-ahead log in bytes
    qint64 size = 0;

    /// Last modification of the file or of its write-ahead log
    QDateTime modified;

    /// The file of the schema version of the application
    bool isCurrent = false;
};

/// Files of the databases in the folder of the data (owner decision O19, newapp_storage.md 5).
/// Every schema version has its own file pdf4qt-<schema>.db: a new version of the
/// application creates its file from a copy of the newest older file, which is never
/// changed, so more versions of PDF4QT may run at the same time and the older file
/// is a backup. Database files are only the names pdf4qt-<n>.db with n from 1 without
/// leading zeros (at most 9 digits); the development file pdf4qt.db, damaged files
/// (.damaged-<date>), temporary files of an upgrade and the files of SQLite (-wal,
/// -shm, -journal) are not. Old files are never deleted automatically.
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseFiles
{
public:
    PDFDatabaseFiles() = delete;

    /// Name of the database file of the schema version: pdf4qt-<schema>.db
    static QString getFileName(int schemaVersion);

    /// Path of the database file of the schema version in the folder
    static QString getFilePath(const QString& directory, int schemaVersion);

    /// Schema version of the name of a database file, -1 for any other name
    static int getSchemaVersion(const QString& fileName);

    /// Database files of the folder, the newest schema version first. \p currentSchemaVersion
    /// marks the file of the application (Settings > Storage of step 5).
    static std::vector<PDFDatabaseFileInfo> getFiles(const QString& directory, int currentSchemaVersion);

    /// Lock of the upgrades: a small SQLite file, whose exclusive transaction is held
    /// while a database file is created (the operating system releases it, when the
    /// process ends). It is never deleted (a deleted lock file would let two processes
    /// lock two different files).
    static QString getUpgradeLockPath(const QString& directory);

    /// A temporary file of an upgrade (pdf4qt-<schema>.db.upgrade-<pid>-<random>, also
    /// its -journal / -wal / -shm). Such a file exists only while its process holds
    /// the lock, a file found by the holder of the lock was left by a crash.
    static bool isUpgradeTemporaryFileName(const QString& fileName);

    /// Name of the lock file
    static constexpr const char* UPGRADE_LOCK_FILE_NAME = "pdf4qt-upgrade.lock";

    /// Time, for which deleteOlderFile waits for the lock of the upgrades (ms)
    static constexpr int DELETE_LOCK_TIMEOUT = 2000;

    /// Deletes an older database file of the folder (Settings › Storage, step 5):
    /// \p filePath must be one of the files of getFiles of \p directory (no path of
    /// the user interface is trusted), its schema version must be older than
    /// \p currentSchemaVersion, it must not be a symbolic link, a junction or a
    /// shortcut (also its -wal / -shm / -journal), and no process may use it. The
    /// check runs under the lock of the upgrades: the file is opened in the exclusive
    /// locking mode of SQLite without waiting (a running PDF4QT holds a shared lock
    /// of its database file in the WAL mode the whole time), closed, then the file
    /// and its -wal, -shm and -journal are deleted. A file used by another process
    /// gives a busy error (PDFDatabaseError::isBusy). The messages are translated.
    static PDFDatabaseError deleteOlderFile(const QString& directory, const QString& filePath, int currentSchemaVersion,
                                            int lockTimeout = DELETE_LOCK_TIMEOUT);

    /// Name of the version of PDF4QT, which uses the schema version, for the user
    /// (owner decision 2026-10-25: "PDF4QT 2.0" instead of "Schema 1"). A static
    /// table of the released schema versions; a version newer than the table is
    /// "Newer PDF4QT", an unknown older one "PDF4QT (schema N)".
    static QString getVersionLabel(int schemaVersion);

    /// Newest schema version of the table of getVersionLabel
    static int getNewestLabeledSchemaVersion();
};

/// Result of the check of a backup file of the settings
enum class PDFDatabaseBackupStatus
{
    Valid,          ///< A backup of PDF4QT of this or of an older schema version
    Missing,        ///< The file does not exist or it cannot be read
    TooLarge,       ///< The file is larger than PDFDatabaseBackup::MAXIMUM_SIZE
    NotABackup,     ///< Not a database of PDF4QT, or its schema is not the schema of its version
    Damaged,        ///< A database of PDF4QT, which fails the integrity check
    Newer           ///< A backup of a newer PDF4QT (a newer schema version)
};

/// Backup of the settings (Settings › Storage, owner decision Q1): one SQLite file
/// with the whole database of the application. A backup to be imported is untrusted
/// input: it is read whole into memory (at most MAXIMUM_SIZE bytes, so the file
/// cannot change between its check and its import) and checked there - the header
/// of SQLite, PRAGMA application_id, user_version (a newer schema is refused), the
/// full integrity check (integrity_check: also the indexes against their tables), the
/// text encoding UTF-8, in a connection with the defensive mode and without
/// a trusted schema, and the schema itself: every table, index, trigger and view
/// (sqlite_master) must equal the schema, which the migrations of the application
/// create for that version, so no foreign trigger or view can reach the database.
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseBackup
{
public:
    /// Largest backup file (owner 2026-10-25: 128 MB - the file is read and checked
    /// in the thread of the user interface)
    static constexpr qint64 MAXIMUM_SIZE = qint64(128) * 1024 * 1024;

    /// Most rows of the trusted certificates read from a backup and the largest
    /// certificate (the limits of PDFTrustedCertificateRepository)
    static constexpr int MAXIMUM_CERTIFICATE_COUNT = 1000;
    static constexpr qsizetype MAXIMUM_CERTIFICATE_SIZE = 64 * 1024;

    /// Longest stored text / blob value of a security setting read from a backup
    /// (a longer one is read as not stored - no valid value of these settings is long)
    static constexpr qsizetype MAXIMUM_SECURITY_VALUE_SIZE = 4096;

    /// Suffix of the backup files (without the dot)
    static constexpr const char* FILE_SUFFIX = "pdf4qt-backup";

    /// Key of the table storage_maintenance of a backup: the time of the export
    static constexpr const char* CREATED_KEY = "backup.created";

    /// Reads and checks the file. \p migrations are the migrations of the
    /// application (empty = PDFDatabaseSchema::getMigrations).
    static PDFDatabaseBackup read(const QString& filePath, const std::vector<PDFDatabaseMigration>& migrations = std::vector<PDFDatabaseMigration>());

    /// Checks the content of a file (read uses it; tests)
    static PDFDatabaseBackup fromData(const QByteArray& data, const QString& filePath, const std::vector<PDFDatabaseMigration>& migrations = std::vector<PDFDatabaseMigration>());

    /// Default name of an exported file: "PDF4QT settings yyyy-MM-dd.pdf4qt-backup"
    static QString getDefaultFileName(const QDate& date);

    bool isValid() const { return m_status == PDFDatabaseBackupStatus::Valid; }
    PDFDatabaseBackupStatus getStatus() const { return m_status; }
    const QString& getFilePath() const { return m_filePath; }

    /// Schema version of the backup (0 = unknown)
    int getSchemaVersion() const { return m_schemaVersion; }

    /// Schema version of the application, which has checked the backup
    int getApplicationSchemaVersion() const { return m_applicationSchemaVersion; }

    /// Time of the export (the time of the file, when the backup does not have it)
    const QDateTime& getCreated() const { return m_created; }

    /// Size of the file in bytes
    qint64 getSize() const { return m_size; }

    /// Content of a valid backup (empty otherwise)
    const QByteArray& getData() const { return m_data; }

    /// Technical detail of a refused file (the log)
    const QString& getDetail() const { return m_detail; }

    /// Translated reason of a refused file (the import error 1271:15560 and the note
    /// 1271:167948), without "Nothing was changed."
    QString getErrorText() const;

    /// Keys of the security, signature and AI settings, which the import confirmation
    /// compares with the current values (owner 2026-10-25, Q7; the AI settings of
    /// owner 2026-10-26, Q4 of step 7)
    static QStringList getSecuritySettingKeys();

    /// Keys of the settings, which are secrets and never a part of a backup: an
    /// export omits their rows, an import keeps the rows of the open database (also
    /// none) and ignores the rows of the backup - the access token of the MCP server
    /// (owner 2026-10-26, Q4 of step 7)
    static QStringList getPrivateSettingKeys();

    /// Data of the rows of trusted_certificate of a valid backup in the order of
    /// adding: blobs of 1 to MAXIMUM_CERTIFICATE_SIZE bytes, at most
    /// MAXIMUM_CERTIFICATE_COUNT; untrusted - not checked to be certificates
    const std::vector<QByteArray>& getTrustedCertificates() const { return m_trustedCertificates; }

    /// Stored value of a key of getSecuritySettingKeys in a valid backup (an invalid
    /// QVariant = not stored, the default applies); untrusted - not validated
    QVariant getSecuritySetting(const QString& key) const;

private:
    /// Reads the certificates and the security settings of a checked backup
    void readSecurityData(sqlite3* handle);

    PDFDatabaseBackupStatus m_status = PDFDatabaseBackupStatus::Missing;
    QString m_filePath;
    int m_schemaVersion = 0;
    int m_applicationSchemaVersion = 0;
    QDateTime m_created;
    qint64 m_size = 0;
    QByteArray m_data;
    QString m_detail;
    std::vector<QByteArray> m_trustedCertificates;
    std::map<QString, QVariant> m_securitySettings;
};

/// What an import does with the trusted certificates (owner 2026-10-25, Q7)
enum class PDFDatabaseImportCertificates
{
    KeepCurrent,    ///< The current rows of trusted_certificate stay (the backup does not add trust)
    FromBackup      ///< The rows of the backup replace them
};

/// Parameters of PDFDatabase::open
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseOptions
{
public:
    /// Folder of the database files (PDFAppPaths::getDataDirectory, owner decision O19):
    /// the database is the file pdf4qt-<schema>.db of the schema version of the
    /// application (PDFDatabaseFiles). When it does not exist, it is created from
    /// a copy of the newest older file, which is migrated in a temporary file under
    /// the lock of the upgrades and renamed (the older file is never written); only
    /// newer files = the newest of them is copied into memory (ReadOnlyNewerSchema).
    /// When it is set, filePath is ignored. Ignored in the portable mode.
    QString directory;

    /// File of the database, when no directory is set (tests and tools: one file
    /// migrated in place). Ignored in the portable mode.
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

    /// Tests: the process ends (std::_Exit with TEST_EXIT_CODE) right after an older
    /// database file has been copied into the temporary file of an upgrade, before
    /// its migration (a crash in the middle of the upgrade)
    bool testExitAfterUpgradeCopy = false;

    /// Exit code of a process ended by testExitAfterUpgradeCopy
    static constexpr int TEST_EXIT_CODE = 87;
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

    /// Database files of the folder of the options (PDFDatabaseOptions::directory),
    /// the newest schema version first; empty without a folder (step 5: Settings > Storage)
    std::vector<PDFDatabaseFileInfo> getDatabaseFiles() const;

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
    /// refuses it or it has been closed already, a marker file next to the database
    /// (the time of the damage) is created instead.
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

    /// Time, for which an export or an import waits for another instance, which
    /// writes the database or holds the lock of the upgrades (ms)
    static constexpr int BACKUP_BUSY_TIMEOUT = 2000;

    /// Export of the settings (Settings › Storage): the open database (any mode) is
    /// copied by the backup API of SQLite into memory (a consistent snapshot also
    /// while other instances write), the state of the running instances (the change
    /// log, the markers of the thumbnails, the integrity check, the MCP server and its
    /// activity log - history with the names of documents, owner 2026-10-25) and the
    /// secret settings (PDFDatabaseBackup::getPrivateSettingKeys - the MCP access
    /// token) are removed, the time of the export is stored, and the copy is written to a
    /// temporary file of the folder of \p filePath, which is renamed to it (never a
    /// half-written file; only the owner may read it). GUI thread.
    PDFDatabaseError exportBackup(const QString& filePath);

    /// Import of a valid backup (PDFDatabaseBackup::read): its content is checked
    /// again, copied into memory and migrated to the schema of the application, the
    /// state of the running instances is taken from the open database, and the open
    /// database is replaced by the backup API in one step (one write transaction),
    /// under the lock of the upgrades of the folder of the database. Every connection
    /// of this and of other instances sees the new content; their notifiers report
    /// everythingChanged (the sequence of the change log jumps). Refused for a
    /// database of a newer schema (ReadOnlyNewerSchema). Another instance holding the
    /// lock or writing = a busy error. Nothing is changed on an error. GUI thread.
    /// The MCP activity log (mcp_activity) is no part of a backup: the open database
    /// keeps its rows (owner 2026-10-25), and so are the secret settings: the rows of
    /// PDFDatabaseBackup::getPrivateSettingKeys of the open database stay, the rows of
    /// the backup are ignored (owner 2026-10-26, Q4 of step 7). \p certificates: KeepCurrent keeps the rows
    /// of trusted_certificate of the open database in the same step (an import never
    /// adds trust, unless the user has asked for it - owner 2026-10-25, Q7).
    /// Every row of sqlite_sequence is set to the largest rowid of its table (a hostile
    /// sequence of a backup never blocks later inserts), the change log jumps.
    PDFDatabaseError importBackup(const PDFDatabaseBackup& backup, PDFDatabaseImportCertificates certificates);

    /// Deletes an older database file of the folder of the options
    /// (PDFDatabaseFiles::deleteOlderFile); refused without a folder, in the portable
    /// mode and for the file, which is open
    PDFDatabaseError deleteOlderDatabaseFile(const QString& filePath);

    /// Jump of the sequence of the change log at an import (every notifier finds
    /// a sequence it has not seen and rereads everything)
    static constexpr qint64 IMPORT_SEQUENCE_JUMP = 1000000;

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
    QString getDiskFilePath() const;
    bool prepareSchemaFile(PDFDatabaseError* error, QString* newerFilePath);
    bool createSchemaFile(const QString& sourcePath, const QString& temporaryPath, const QString& targetPath, PDFDatabaseError* error, bool* isSourceUnusable);
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
    sqlite3* openTargetHandle(int busyTimeout, PDFDatabaseError* error) const;

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

    /// File, whose content is copied into the memory fallback (the file of the
    /// database, or the older / newer file of an upgrade, which could not be done)
    QString m_fallbackFilePath;
};

}   // namespace pdf

#endif // PDFDATABASE_H
