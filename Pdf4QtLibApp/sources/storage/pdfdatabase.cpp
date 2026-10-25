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
#include "pdfsettingdefinitions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QThreadStorage>
#include <QUrl>
#include <QLoggingCategory>

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>

Q_LOGGING_CATEGORY(lcPdf4QtStorage, "pdf4qt.storage")

namespace pdf
{

/// Connections of the threads of one database. It is shared by the database and
/// the data of the threads (weak pointers), so a thread, which ends after the
/// database has been destroyed, finds nothing to close.
class PDFDatabaseRegistry
{
public:
    QMutex mutex;
    bool isOpen = false;
    quint64 generation = 0;

    /// File path or URI of the memory database, which the connections open
    QString target;
    bool isMemory = false;
    int busyTimeout = 5000;

    std::map<Qt::HANDLE, std::unique_ptr<PDFDatabaseConnection>> connections;

    /// Opens the handle of a new connection (called with the mutex locked)
    void openConnection(PDFDatabaseConnection* connection);
};

/// Data of one thread: the registries, in which the thread has a connection.
/// It is destroyed by QThreadStorage, when the thread ends, and closes them.
class PDFDatabaseThreadData
{
public:
    ~PDFDatabaseThreadData();

    void addRegistry(const std::shared_ptr<PDFDatabaseRegistry>& registry);

private:
    std::vector<std::weak_ptr<PDFDatabaseRegistry>> m_registries;
};

/// Helper functions of the database
class PDFDatabaseHelper
{
public:
    PDFDatabaseHelper() = delete;

    /// Thread data of the current thread
    static QThreadStorage<PDFDatabaseThreadData*>& getThreadStorage();

    /// Applies the pragmas of newapp_storage.md 2, return values are checked
    static PDFDatabaseError applyPragmas(sqlite3* handle, bool isMemory, bool isReadOnly, int busyTimeout);

    /// Opens a handle, a failed handle is closed
    static sqlite3* openHandle(const QByteArray& target, int flags, PDFDatabaseError* error);

    /// Executes a statement returning one text value
    static QString queryText(sqlite3* handle, const char* sql, int* result);

    /// The result code means, that the file is not a database or it is corrupted
    static bool isDamageCode(int code);

    /// Restricts the permissions of a file to its owner (Unix)
    static void restrictPermissions(const QString& filePath);

    /// Copies the database \p source into \p target by the backup API
    static int copyDatabase(sqlite3* source, sqlite3* target);

    /// Result of readMaintenanceValue
    enum class MaintenanceValue
    {
        Read,       ///< An integer value was read
        Missing,    ///< No row of the key
        Invalid     ///< A value of another type, no table (an older schema) or an error
    };

    /// Reads an integer value of the table storage_maintenance
    static MaintenanceValue readMaintenanceValue(sqlite3* handle, const char* key, qint64* value);

    /// Writes an integer value into the table storage_maintenance. \returns the result code.
    static int writeMaintenanceValue(sqlite3* handle, const char* key, qint64 value);

    /// Writes the request of the full integrity check (a damage met at \p reportTime).
    /// The stored value is newer than the time of the last check also when the clock
    /// has been set back since that check, and it never decreases. \returns the result code.
    static int writeIntegrityRequest(sqlite3* handle, qint64 reportTime);

    /// Time of the damage written into the marker file \p markerPath (-1 = no valid time)
    static qint64 readMarkerTime(const QString& markerPath);

    /// Order of PDFDatabaseFiles::getFiles: the newest schema version first
    static bool isNewerFile(const PDFDatabaseFileInfo& left, const PDFDatabaseFileInfo& right);

    /// The error comes from the environment of a file (it cannot be created, opened,
    /// read or written, no memory, a lock), not from its content
    static bool isEnvironmentError(int code);

    /// Takes the lock of the upgrades of the folder (PDFDatabaseFiles::getUpgradeLockPath),
    /// \returns nullptr on an error (SQLITE_BUSY after the busy timeout = another process
    /// holds it)
    static sqlite3* lockUpgrades(const QString& directory, int busyTimeout, PDFDatabaseError* error);

    /// Releases the lock of the upgrades
    static void unlockUpgrades(sqlite3* lock);

    /// Removes the temporary files of the upgrades of the folder (called by the holder
    /// of the lock: such a file was left by a crash)
    static void removeUpgradeTemporaryFiles(const QString& directory);

    /// Removes the temporary file \p filePath and the files of SQLite next to it
    static void removeTemporaryFile(const QString& filePath);

    /// Tables of the state of the running instances, which a backup does not carry
    /// (change_log is handled separately: it is emptied and its sequence jumps)
    static std::vector<const char*> getRuntimeTables();

    /// Released schema versions and the versions of PDF4QT, which use them
    /// (PDFDatabaseFiles::getVersionLabel). A new schema version (a new migration)
    /// adds its row here.
    static std::vector<std::pair<int, const char*>> getVersionLabels();

    /// Schema of the database: one text per row of sqlite_master (type, name, table
    /// and SQL, without the root page), sorted. \returns false on an error.
    static bool readSchema(sqlite3* handle, QStringList* schema);

    /// Schema, which the migrations create for \p version in an empty database
    static bool createReferenceSchema(const std::vector<PDFDatabaseMigration>& migrations, int version, QStringList* schema);

    /// Memory database with the content of a database file (\p data): the defensive
    /// pragmas of the application, the header of a database with a rollback journal.
    /// \returns nullptr on an error.
    static sqlite3* openMemoryCopy(const QByteArray& data, bool readOnly, PDFDatabaseError* error);

    /// Memory database of a copy (empty, with the pragmas of the application)
    static sqlite3* openEmptyMemory(PDFDatabaseError* error);

    /// Copies \p source into \p target by the backup API in one step
    static int backupDatabase(sqlite3* source, sqlite3* target);

    /// Replaces the rows of the table of \p target by the rows of \p source (the
    /// columns of the table of \p target, named). \returns the result code.
    static int copyTableRows(sqlite3* source, sqlite3* target, const char* table);

    /// Executes SQL, \returns the result code
    static int execute(sqlite3* handle, const char* sql);

    /// Highest sequence number of the change log (the sequence of AUTOINCREMENT and
    /// the rows), -1 on an error
    static qint64 readChangeLogSequence(sqlite3* handle);

    /// Sets the sequence of the change log (sqlite_sequence)
    static int writeChangeLogSequence(sqlite3* handle, qint64 sequence);

    /// Sets every row of sqlite_sequence to the largest rowid of its table; a row of
    /// a name, which is not a table with rowids, and duplicate rows are removed (the
    /// sequences of an imported backup are untrusted: INT64_MAX would make every
    /// later insert of an AUTOINCREMENT table fail with SQLITE_FULL)
    static int normalizeSequences(sqlite3* handle);

    /// Removes the state of the running instances from a copy (export / import)
    static int clearRuntimeState(sqlite3* handle);

    /// Changes the page size of the memory copy to the page size of \p target (a
    /// database in the WAL mode cannot change its page size by the backup API)
    static int alignPageSize(sqlite3* copy, sqlite3* target);

    /// The path is a symbolic link, a junction or a shortcut
    static bool isLink(const QString& filePath);

    /// Error of a refused file of deleteOlderFile (\p text is a source text of the
    /// context pdf::PDFDatabaseFiles)
    static PDFDatabaseError refuseFile(const char* text);

    /// The database file is used by a connection of another process (or of this
    /// one): it cannot be locked exclusively without waiting. \returns an error
    /// (busy = in use)
    static PDFDatabaseError checkFileNotInUse(const QString& filePath);
};

PDFDatabaseError::PDFDatabaseError(int code, int extendedCode, QString message) :
    m_code(code),
    m_extendedCode(extendedCode),
    m_message(std::move(message))
{

}

PDFDatabaseError PDFDatabaseError::fromHandle(sqlite3* handle, int code)
{
    if (code == SQLITE_OK || code == SQLITE_ROW || code == SQLITE_DONE)
    {
        return PDFDatabaseError();
    }

    QString message;
    int extendedCode = code;
    if (handle)
    {
        extendedCode = sqlite3_extended_errcode(handle);
        message = QString::fromUtf8(sqlite3_errmsg(handle));
    }
    else
    {
        message = QString::fromUtf8(sqlite3_errstr(code));
    }

    return PDFDatabaseError(code & 0xFF, extendedCode, std::move(message));
}

PDFDatabaseError PDFDatabaseError::fromMessage(QString message)
{
    return PDFDatabaseError(SQLITE_ERROR, SQLITE_ERROR, std::move(message));
}

bool PDFDatabaseError::isBusy() const
{
    return m_code == SQLITE_BUSY || m_code == SQLITE_LOCKED;
}

QString PDFDatabaseError::toString() const
{
    if (!isError())
    {
        return QString();
    }
    return QStringLiteral("%1 (%2)").arg(m_message).arg(m_extendedCode);
}

QString PDFDatabaseState::getDescription() const
{
    switch (mode)
    {
        case PDFDatabaseStorageMode::Disk:
            if (!damagedFilePath.isEmpty())
            {
                return QCoreApplication::translate("pdf::PDFDatabase", "The settings database could not be read. It was kept as %1 and a new one was created.").arg(QDir::toNativeSeparators(damagedFilePath));
            }
            return QString();

        case PDFDatabaseStorageMode::Portable:
            return QCoreApplication::translate("pdf::PDFDatabase", "Portable mode. Settings and history are kept only in memory and are lost when PDF4QT closes.");

        case PDFDatabaseStorageMode::ReadOnlyNewerSchema:
            return QCoreApplication::translate("pdf::PDFDatabase", "The settings were saved by a newer version of PDF4QT. Changes are kept only until PDF4QT closes.");

        case PDFDatabaseStorageMode::MemoryFallback:
            return QCoreApplication::translate("pdf::PDFDatabase", "The settings database cannot be opened (%1). Changes are kept only until PDF4QT closes.").arg(error.getMessage());
    }

    return QString();
}

PDFDatabaseConnection::PDFDatabaseConnection(PDFDatabase* database, quint64 generation) :
    m_database(database),
    m_generation(generation)
{

}

PDFDatabaseConnection::~PDFDatabaseConnection()
{
    // close_v2 keeps the handle as a zombie until the last statement is finalized
    PDFDatabase::closeHandle(m_handle);
    m_handle = nullptr;
}

PDFDatabaseError PDFDatabaseConnection::execute(const char* sql)
{
    if (!m_handle)
    {
        return m_openError.isError() ? m_openError : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
    }

    const int result = sqlite3_exec(m_handle, sql, nullptr, nullptr, nullptr);
    PDFDatabaseError error = PDFDatabaseError::fromHandle(m_handle, result);
    if (error.isError() && m_database)
    {
        m_database->reportError(error);
    }
    return error;
}

qint64 PDFDatabaseConnection::queryInteger(const char* sql, qint64 defaultValue, PDFDatabaseError* error)
{
    if (!m_handle)
    {
        if (error)
        {
            *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
        }
        return defaultValue;
    }

    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(m_handle, sql, -1, &statement, nullptr);
    qint64 value = defaultValue;
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            value = sqlite3_column_int64(statement, 0);
            result = SQLITE_OK;
        }
    }

    const PDFDatabaseError resultError = PDFDatabaseError::fromHandle(m_handle, result);
    if (resultError.isError() && m_database)
    {
        m_database->reportError(resultError);
    }
    if (error)
    {
        *error = resultError;
    }

    sqlite3_finalize(statement);
    return value;
}

qint64 PDFDatabaseConnection::getLastInsertRowId() const
{
    return m_handle ? sqlite3_last_insert_rowid(m_handle) : 0;
}

qint64 PDFDatabaseConnection::getChanges() const
{
    return m_handle ? sqlite3_changes64(m_handle) : 0;
}

bool PDFDatabaseConnection::isInTransaction() const
{
    return m_handle && sqlite3_get_autocommit(m_handle) == 0;
}

void PDFDatabaseConnection::setBusyTimeout(int milliseconds)
{
    if (m_handle)
    {
        m_busyTimeout = qMax(0, milliseconds);
        sqlite3_busy_timeout(m_handle, m_busyTimeout);
    }
}

void PDFDatabaseRegistry::openConnection(PDFDatabaseConnection* connection)
{
    PDFDatabaseError error;
    const int flags = isMemory ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI) : SQLITE_OPEN_READWRITE;
    sqlite3* handle = PDFDatabaseHelper::openHandle(target.toUtf8(), flags, &error);
    if (handle)
    {
        connection->m_busyTimeout = qMax(0, busyTimeout);
        error = PDFDatabaseHelper::applyPragmas(handle, isMemory, false, busyTimeout);
        if (error.isError())
        {
            PDFDatabase::closeHandle(handle);
            handle = nullptr;
        }
    }

    if (error.isError())
    {
        qCWarning(lcPdf4QtStorage) << "Connection to the database cannot be opened:" << error.toString();
    }

    connection->m_handle = handle;
    connection->m_openError = error;
}

PDFDatabaseThreadData::~PDFDatabaseThreadData()
{
    // The thread ends: close its connections of all databases, which still exist
    const Qt::HANDLE thread = QThread::currentThreadId();
    for (const std::weak_ptr<PDFDatabaseRegistry>& weakRegistry : m_registries)
    {
        std::shared_ptr<PDFDatabaseRegistry> registry = weakRegistry.lock();
        if (!registry)
        {
            continue;
        }

        std::unique_ptr<PDFDatabaseConnection> connection;
        {
            QMutexLocker locker(&registry->mutex);
            auto it = registry->connections.find(thread);
            if (it != registry->connections.end())
            {
                connection = std::move(it->second);
                registry->connections.erase(it);
            }
        }
    }
}

void PDFDatabaseThreadData::addRegistry(const std::shared_ptr<PDFDatabaseRegistry>& registry)
{
    for (auto it = m_registries.begin(); it != m_registries.end();)
    {
        std::shared_ptr<PDFDatabaseRegistry> current = it->lock();
        if (!current)
        {
            it = m_registries.erase(it);
            continue;
        }

        if (current == registry)
        {
            return;
        }

        ++it;
    }

    m_registries.push_back(registry);
}

QThreadStorage<PDFDatabaseThreadData*>& PDFDatabaseHelper::getThreadStorage()
{
    static QThreadStorage<PDFDatabaseThreadData*> storage;
    return storage;
}

sqlite3* PDFDatabaseHelper::openHandle(const QByteArray& target, int flags, PDFDatabaseError* error)
{
    sqlite3* handle = nullptr;
    const int result = sqlite3_open_v2(target.constData(), &handle, flags | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK)
    {
        if (error)
        {
            *error = PDFDatabaseError::fromHandle(handle, result);
        }
        PDFDatabase::closeHandle(handle);
        return nullptr;
    }

    if (error)
    {
        *error = PDFDatabaseError();
    }
    return handle;
}

QString PDFDatabaseHelper::queryText(sqlite3* handle, const char* sql, int* result)
{
    sqlite3_stmt* statement = nullptr;
    QString text;
    int code = sqlite3_prepare_v2(handle, sql, -1, &statement, nullptr);
    if (code == SQLITE_OK)
    {
        code = sqlite3_step(statement);
        if (code == SQLITE_ROW)
        {
            const unsigned char* data = sqlite3_column_text(statement, 0);
            const int size = sqlite3_column_bytes(statement, 0);
            if (data && size > 0)
            {
                text = QString::fromUtf8(reinterpret_cast<const char*>(data), size);
            }
            code = SQLITE_OK;
        }
        else if (code == SQLITE_DONE)
        {
            code = SQLITE_OK;
        }
    }
    sqlite3_finalize(statement);

    if (result)
    {
        *result = code;
    }
    return text;
}

PDFDatabaseError PDFDatabaseHelper::applyPragmas(sqlite3* handle, bool isMemory, bool isReadOnly, int busyTimeout)
{
    int result = sqlite3_busy_timeout(handle, qMax(0, busyTimeout));
    if (result != SQLITE_OK)
    {
        return PDFDatabaseError::fromHandle(handle, result);
    }

    // A damaged or a foreign file must not be able to run code or corrupt the memory
    sqlite3_db_config(handle, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(handle, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);

    // Recursive triggers: a row deleted by the conflict resolution REPLACE (INSERT OR
    // REPLACE, also of another unique column than the key) fires the delete trigger,
    // so the change log gets also its key. The triggers of the change log write only
    // into change_log, which has no triggers, so nothing recurses.
    result = sqlite3_exec(handle, "PRAGMA foreign_keys = ON; PRAGMA recursive_triggers = ON; PRAGMA temp_store = MEMORY", nullptr, nullptr, nullptr);
    if (result != SQLITE_OK)
    {
        return PDFDatabaseError::fromHandle(handle, result);
    }

    int queryResult = SQLITE_OK;
    if (queryText(handle, "PRAGMA foreign_keys", &queryResult) != QLatin1String("1"))
    {
        return queryResult != SQLITE_OK ? PDFDatabaseError::fromHandle(handle, queryResult)
                                        : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "Foreign keys cannot be enabled."));
    }

    if (!isMemory && !isReadOnly)
    {
        // The journal mode is returned as the result of the pragma, it must be "wal"
        const QString journalMode = queryText(handle, "PRAGMA journal_mode = WAL", &queryResult);
        if (queryResult != SQLITE_OK)
        {
            return PDFDatabaseError::fromHandle(handle, queryResult);
        }
        if (journalMode.compare(QLatin1String("wal"), Qt::CaseInsensitive) != 0)
        {
            return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database cannot be switched to the WAL mode (journal mode is '%1').").arg(journalMode));
        }

        result = sqlite3_exec(handle, "PRAGMA synchronous = NORMAL", nullptr, nullptr, nullptr);
        if (result != SQLITE_OK)
        {
            return PDFDatabaseError::fromHandle(handle, result);
        }
    }

    return PDFDatabaseError();
}

bool PDFDatabaseHelper::isDamageCode(int code)
{
    code &= 0xFF;
    return code == SQLITE_NOTADB || code == SQLITE_CORRUPT;
}

void PDFDatabaseHelper::restrictPermissions(const QString& filePath)
{
#if defined(Q_OS_UNIX)
    // Only the owner may read the file (0600). Permissions, which the owner has not,
    // are not added (a read-only file stays read-only).
    QFile file(filePath);
    const QFileDevice::Permissions permissions = file.permissions();
    const QFileDevice::Permissions ownerOnly = permissions & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
    if (permissions != ownerOnly)
    {
        file.setPermissions(ownerOnly);
    }
#else
    // Windows: the file is in the local application data of the user, whose folder
    // is accessible only by the user (and the administrators)
    Q_UNUSED(filePath);
#endif
}

int PDFDatabaseHelper::copyDatabase(sqlite3* source, sqlite3* target)
{
    // The file is a WAL database: its header says so (bytes 18 and 19 = 2). A memory
    // database cannot be a WAL database, so the copy gets the header of a database
    // with a rollback journal (1) before it is copied into the shared memory database.
    sqlite3_int64 size = 0;
    unsigned char* data = sqlite3_serialize(source, "main", &size, 0);
    if (!data)
    {
        return size == 0 ? SQLITE_OK : SQLITE_NOMEM;
    }

    if (size >= 100)
    {
        data[18] = 1;
        data[19] = 1;
    }

    sqlite3* temporary = nullptr;
    int result = sqlite3_open_v2(":memory:", &temporary, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK)
    {
        sqlite3_free(data);
        PDFDatabase::closeHandle(temporary);
        return result;
    }

    // The buffer is owned by the connection (also when the function fails)
    result = sqlite3_deserialize(temporary, "main", data, size, size, SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
    if (result == SQLITE_OK)
    {
        sqlite3_backup* backup = sqlite3_backup_init(target, "main", temporary, "main");
        if (!backup)
        {
            result = sqlite3_errcode(target);
        }
        else
        {
            const int stepResult = sqlite3_backup_step(backup, -1);
            const int finishResult = sqlite3_backup_finish(backup);
            result = (stepResult != SQLITE_DONE) ? stepResult : finishResult;
        }
    }

    PDFDatabase::closeHandle(temporary);
    return result;
}

PDFDatabaseHelper::MaintenanceValue PDFDatabaseHelper::readMaintenanceValue(sqlite3* handle, const char* key, qint64* value)
{
    sqlite3_stmt* statement = nullptr;
    MaintenanceValue result = MaintenanceValue::Invalid;
    if (sqlite3_prepare_v2(handle, "SELECT value FROM storage_maintenance WHERE key = ?1", -1, &statement, nullptr) == SQLITE_OK &&
        sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC) == SQLITE_OK)
    {
        const int stepResult = sqlite3_step(statement);
        if (stepResult == SQLITE_DONE)
        {
            result = MaintenanceValue::Missing;
        }
        else if (stepResult == SQLITE_ROW && sqlite3_column_type(statement, 0) == SQLITE_INTEGER)
        {
            // A value of another type (written by something else) is not trusted
            *value = sqlite3_column_int64(statement, 0);
            result = MaintenanceValue::Read;
        }
    }
    sqlite3_finalize(statement);
    return result;
}

int PDFDatabaseHelper::writeMaintenanceValue(sqlite3* handle, const char* key, qint64 value)
{
    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(handle, "INSERT INTO storage_maintenance(key, value) VALUES (?1, ?2) "
                                            "ON CONFLICT(key) DO UPDATE SET value = excluded.value", -1, &statement, nullptr);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 2, value);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_DONE)
        {
            result = SQLITE_OK;
        }
    }
    sqlite3_finalize(statement);
    return result;
}

int PDFDatabaseHelper::writeIntegrityRequest(sqlite3* handle, qint64 reportTime)
{
    // One statement (atomic against other instances): at least the time of the last
    // check + 1, so a check, whose time was recorded by a clock running ahead, does not
    // hide the request after the clock has been set back; a stored request of another
    // type (not written by PDF4QT) is replaced
    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(handle, "INSERT INTO storage_maintenance(key, value) VALUES (?1, "
                                            "MAX(?2, COALESCE((SELECT value + 1 FROM storage_maintenance WHERE key = ?3 AND typeof(value) = 'integer'), 0))) "
                                            "ON CONFLICT(key) DO UPDATE SET value = CASE WHEN typeof(value) = 'integer' THEN MAX(value, excluded.value) ELSE excluded.value END",
                                    -1, &statement, nullptr);
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(statement, 1, PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY, -1, SQLITE_STATIC);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_int64(statement, 2, reportTime);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_bind_text(statement, 3, PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY, -1, SQLITE_STATIC);
    }
    if (result == SQLITE_OK)
    {
        result = sqlite3_step(statement);
        if (result == SQLITE_DONE)
        {
            result = SQLITE_OK;
        }
    }
    sqlite3_finalize(statement);
    return result;
}

qint64 PDFDatabaseHelper::readMarkerTime(const QString& markerPath)
{
    // The file is written by PDFDatabase::writeDamageReport (milliseconds since the
    // epoch as a decimal number); anything else is no valid time
    QFile marker(markerPath);
    if (!marker.open(QIODevice::ReadOnly))
    {
        return -1;
    }

    bool isValid = false;
    const qint64 time = marker.read(32).trimmed().toLongLong(&isValid);
    return (isValid && time > 0) ? time : -1;
}

bool PDFDatabaseHelper::isNewerFile(const PDFDatabaseFileInfo& left, const PDFDatabaseFileInfo& right)
{
    return left.schemaVersion > right.schemaVersion;
}

bool PDFDatabaseHelper::isEnvironmentError(int code)
{
    switch (code & 0xFF)
    {
        case SQLITE_CANTOPEN:
        case SQLITE_PERM:
        case SQLITE_READONLY:
        case SQLITE_IOERR:
        case SQLITE_FULL:
        case SQLITE_NOMEM:
        case SQLITE_AUTH:
        case SQLITE_NOLFS:
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            return true;

        default:
            return false;
    }
}

sqlite3* PDFDatabaseHelper::lockUpgrades(const QString& directory, int busyTimeout, PDFDatabaseError* error)
{
    // The exclusive transaction of a rollback journal database is a lock of the file
    // by the operating system: it is released also, when the process crashes. No page
    // is written, so the file stays empty and no journal is created.
    const QString lockPath = PDFDatabaseFiles::getUpgradeLockPath(directory);
    sqlite3* lock = openHandle(lockPath.toUtf8(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, error);
    if (!lock)
    {
        return nullptr;
    }

    restrictPermissions(lockPath);
    sqlite3_busy_timeout(lock, qMax(0, busyTimeout));
    const int result = sqlite3_exec(lock, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr);
    if (result != SQLITE_OK)
    {
        *error = PDFDatabaseError::fromHandle(lock, result);
        PDFDatabase::closeHandle(lock);
        return nullptr;
    }

    *error = PDFDatabaseError();
    return lock;
}

void PDFDatabaseHelper::unlockUpgrades(sqlite3* lock)
{
    if (lock)
    {
        sqlite3_exec(lock, "ROLLBACK", nullptr, nullptr, nullptr);
        PDFDatabase::closeHandle(lock);
    }
}

void PDFDatabaseHelper::removeUpgradeTemporaryFiles(const QString& directory)
{
    const QStringList names = QDir(directory).entryList(QDir::Files | QDir::Hidden);
    for (const QString& name : names)
    {
        if (PDFDatabaseFiles::isUpgradeTemporaryFileName(name))
        {
            const QString filePath = QDir(directory).filePath(name);
            if (QFile::remove(filePath))
            {
                qCInfo(lcPdf4QtStorage) << "Temporary file of an interrupted upgrade removed:" << filePath;
            }
        }
    }
}

void PDFDatabaseHelper::removeTemporaryFile(const QString& filePath)
{
    for (const char* suffix : { "", "-journal", "-wal", "-shm" })
    {
        const QString name = filePath + QLatin1String(suffix);
        if (QFileInfo::exists(name))
        {
            QFile::remove(name);
        }
    }
}

std::vector<const char*> PDFDatabaseHelper::getRuntimeTables()
{
    // mcp_activity: the history of the MCP clients (names of documents) is not
    // exported and an import keeps the current one (owner 2026-10-25)
    return { "storage_maintenance", "thumbnail_rendering", "mcp_server", "mcp_client", "mcp_activity", "mcp_request" };
}

std::vector<std::pair<int, const char*>> PDFDatabaseHelper::getVersionLabels()
{
    return { { 1, "2.0" } };
}

int PDFDatabaseHelper::execute(sqlite3* handle, const char* sql)
{
    return handle ? sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) : SQLITE_MISUSE;
}

bool PDFDatabaseHelper::readSchema(sqlite3* handle, QStringList* schema)
{
    schema->clear();
    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(handle, "SELECT type, name, tbl_name, sql FROM sqlite_master", -1, &statement, nullptr);
    while (result == SQLITE_OK || result == SQLITE_ROW)
    {
        result = sqlite3_step(statement);
        if (result != SQLITE_ROW)
        {
            break;
        }

        QStringList columns;
        for (int column = 0; column < 4; ++column)
        {
            // A NULL (the SQL of an automatic index) differs from an empty text
            const unsigned char* text = sqlite3_column_text(statement, column);
            const int size = sqlite3_column_bytes(statement, column);
            columns << (text ? QString::fromUtf8(reinterpret_cast<const char*>(text), size) : QStringLiteral("<null>"));
        }
        schema->append(columns.join(QChar(0x1F)));
    }
    sqlite3_finalize(statement);

    schema->sort();
    return result == SQLITE_DONE;
}

sqlite3* PDFDatabaseHelper::openEmptyMemory(PDFDatabaseError* error)
{
    sqlite3* handle = openHandle(QByteArrayLiteral(":memory:"), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, error);
    if (!handle)
    {
        return nullptr;
    }

    *error = applyPragmas(handle, true, false, 0);
    if (error->isError())
    {
        PDFDatabase::closeHandle(handle);
        return nullptr;
    }
    return handle;
}

bool PDFDatabaseHelper::createReferenceSchema(const std::vector<PDFDatabaseMigration>& migrations, int version, QStringList* schema)
{
    PDFDatabaseError error;
    sqlite3* handle = openEmptyMemory(&error);
    if (!handle)
    {
        return false;
    }

    error = PDFDatabaseSchema::migrate(handle, migrations, version);
    const bool isRead = !error.isError() && readSchema(handle, schema);
    PDFDatabase::closeHandle(handle);
    return isRead;
}

sqlite3* PDFDatabaseHelper::openMemoryCopy(const QByteArray& data, bool readOnly, PDFDatabaseError* error)
{
    sqlite3* handle = openEmptyMemory(error);
    if (!handle)
    {
        return nullptr;
    }

    // The buffer is owned by SQLite (also when the function fails). The header of
    // a WAL database (bytes 18 and 19) is changed to a rollback journal: a memory
    // database is never a WAL database.
    const sqlite3_int64 size = data.size();
    unsigned char* buffer = static_cast<unsigned char*>(sqlite3_malloc64(sqlite3_uint64(qMax<sqlite3_int64>(size, 1))));
    if (!buffer)
    {
        *error = PDFDatabaseError::fromHandle(nullptr, SQLITE_NOMEM);
        PDFDatabase::closeHandle(handle);
        return nullptr;
    }
    memcpy(buffer, data.constData(), size_t(size));
    if (size >= 100)
    {
        buffer[18] = 1;
        buffer[19] = 1;
    }

    const unsigned flags = SQLITE_DESERIALIZE_FREEONCLOSE | (readOnly ? SQLITE_DESERIALIZE_READONLY : SQLITE_DESERIALIZE_RESIZEABLE);
    const int result = sqlite3_deserialize(handle, "main", buffer, size, size, flags);
    if (result != SQLITE_OK)
    {
        *error = PDFDatabaseError::fromHandle(handle, result);
        PDFDatabase::closeHandle(handle);
        return nullptr;
    }

    *error = PDFDatabaseError();
    return handle;
}

int PDFDatabaseHelper::backupDatabase(sqlite3* source, sqlite3* target)
{
    sqlite3_backup* backup = sqlite3_backup_init(target, "main", source, "main");
    if (!backup)
    {
        const int code = sqlite3_errcode(target);
        return code != SQLITE_OK ? code : SQLITE_ERROR;
    }

    const int stepResult = sqlite3_backup_step(backup, -1);
    const int finishResult = sqlite3_backup_finish(backup);
    return (stepResult != SQLITE_DONE) ? stepResult : finishResult;
}

int PDFDatabaseHelper::copyTableRows(sqlite3* source, sqlite3* target, const char* table)
{
    // The columns of the table of the target (the schema of the application),
    // named in both statements
    QStringList columns;
    {
        sqlite3_stmt* info = nullptr;
        const QByteArray sql = QByteArray("PRAGMA table_info(") + table + ")";
        int result = sqlite3_prepare_v2(target, sql.constData(), -1, &info, nullptr);
        while (result == SQLITE_OK || result == SQLITE_ROW)
        {
            result = sqlite3_step(info);
            if (result == SQLITE_ROW)
            {
                columns << QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(info, 1)));
            }
        }
        sqlite3_finalize(info);
        if (result != SQLITE_DONE || columns.isEmpty())
        {
            return result != SQLITE_DONE ? result : SQLITE_ERROR;
        }
    }

    const QByteArray columnList = columns.join(QStringLiteral(", ")).toUtf8();
    QByteArray placeholders;
    for (qsizetype i = 0; i < columns.size(); ++i)
    {
        placeholders += (i > 0 ? ", ?" : "?") + QByteArray::number(i + 1);
    }

    int result = execute(target, (QByteArray("DELETE FROM ") + table).constData());
    if (result != SQLITE_OK)
    {
        return result;
    }

    sqlite3_stmt* select = nullptr;
    sqlite3_stmt* insert = nullptr;
    result = sqlite3_prepare_v2(source, (QByteArray("SELECT ") + columnList + " FROM " + table).constData(), -1, &select, nullptr);
    if (result == SQLITE_OK)
    {
        result = sqlite3_prepare_v2(target, (QByteArray("INSERT INTO ") + table + "(" + columnList + ") VALUES (" + placeholders + ")").constData(), -1, &insert, nullptr);
    }

    while (result == SQLITE_OK)
    {
        const int stepResult = sqlite3_step(select);
        if (stepResult == SQLITE_DONE)
        {
            break;
        }
        if (stepResult != SQLITE_ROW)
        {
            result = stepResult;
            break;
        }

        for (int i = 0; i < int(columns.size()) && result == SQLITE_OK; ++i)
        {
            result = sqlite3_bind_value(insert, i + 1, sqlite3_column_value(select, i));
        }
        if (result == SQLITE_OK)
        {
            const int insertResult = sqlite3_step(insert);
            result = (insertResult == SQLITE_DONE) ? SQLITE_OK : insertResult;
        }
        sqlite3_reset(insert);
    }

    sqlite3_finalize(select);
    sqlite3_finalize(insert);
    return result;
}

qint64 PDFDatabaseHelper::readChangeLogSequence(sqlite3* handle)
{
    return PDFDatabase::queryIntegerOnHandle(handle, "SELECT MAX(COALESCE((SELECT seq FROM sqlite_sequence WHERE name = 'change_log'), 0), "
                                                     "COALESCE((SELECT MAX(seq) FROM change_log), 0))", -1);
}

int PDFDatabaseHelper::writeChangeLogSequence(sqlite3* handle, qint64 sequence)
{
    // sqlite_sequence has no key: the row is updated, or inserted, when there is none
    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(handle, "UPDATE sqlite_sequence SET seq = ?1 WHERE name = 'change_log'", -1, &statement, nullptr);
    if (result == SQLITE_OK)
    {
        sqlite3_bind_int64(statement, 1, sequence);
        result = sqlite3_step(statement);
        result = (result == SQLITE_DONE) ? SQLITE_OK : result;
    }
    sqlite3_finalize(statement);

    if (result == SQLITE_OK && sqlite3_changes(handle) == 0)
    {
        statement = nullptr;
        result = sqlite3_prepare_v2(handle, "INSERT INTO sqlite_sequence(name, seq) VALUES ('change_log', ?1)", -1, &statement, nullptr);
        if (result == SQLITE_OK)
        {
            sqlite3_bind_int64(statement, 1, sequence);
            result = sqlite3_step(statement);
            result = (result == SQLITE_DONE) ? SQLITE_OK : result;
        }
        sqlite3_finalize(statement);
    }
    return result;
}

int PDFDatabaseHelper::normalizeSequences(sqlite3* handle)
{
    // One row per name of a table (the first one), no row of another name
    int result = execute(handle, "DELETE FROM sqlite_sequence WHERE name IS NULL OR typeof(name) != 'text' "
                                 "OR name NOT IN (SELECT name FROM sqlite_master WHERE type = 'table'); "
                                 "DELETE FROM sqlite_sequence WHERE rowid NOT IN (SELECT MIN(rowid) FROM sqlite_sequence GROUP BY name)");
    if (result != SQLITE_OK)
    {
        return result;
    }

    QStringList names;
    sqlite3_stmt* statement = nullptr;
    result = sqlite3_prepare_v2(handle, "SELECT name FROM sqlite_sequence", -1, &statement, nullptr);
    while (result == SQLITE_OK)
    {
        const int stepResult = sqlite3_step(statement);
        if (stepResult == SQLITE_DONE)
        {
            break;
        }
        if (stepResult != SQLITE_ROW)
        {
            result = stepResult;
            break;
        }
        names << QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)), sqlite3_column_bytes(statement, 0));
    }
    sqlite3_finalize(statement);

    for (const QString& name : std::as_const(names))
    {
        if (result != SQLITE_OK)
        {
            break;
        }

        // The name is a table of sqlite_master (quoted as an identifier); a table
        // without rowids has no sequence - its row is removed
        QString quotedName = name;
        quotedName.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        const QByteArray sql = "UPDATE sqlite_sequence SET seq = COALESCE((SELECT MAX(rowid) FROM \"" + quotedName.toUtf8() + "\"), 0) WHERE name = ?1";
        const QByteArray nameData = name.toUtf8();
        statement = nullptr;
        if (sqlite3_prepare_v2(handle, sql.constData(), -1, &statement, nullptr) != SQLITE_OK)
        {
            sqlite3_finalize(statement);
            statement = nullptr;
            result = sqlite3_prepare_v2(handle, "DELETE FROM sqlite_sequence WHERE name = ?1", -1, &statement, nullptr);
        }
        if (result == SQLITE_OK)
        {
            sqlite3_bind_text(statement, 1, nameData.constData(), int(nameData.size()), SQLITE_TRANSIENT);
            const int stepResult = sqlite3_step(statement);
            result = (stepResult == SQLITE_DONE) ? SQLITE_OK : stepResult;
        }
        sqlite3_finalize(statement);
    }
    return result;
}

int PDFDatabaseHelper::clearRuntimeState(sqlite3* handle)
{
    // The statistics of the query planner (PRAGMA optimize) belong to the file,
    // not to the data; the change log is emptied last (the deletes write into it)
    int result = execute(handle, "DROP TABLE IF EXISTS sqlite_stat1; DROP TABLE IF EXISTS sqlite_stat4");
    for (const char* table : getRuntimeTables())
    {
        if (result == SQLITE_OK)
        {
            result = execute(handle, (QByteArray("DELETE FROM ") + table).constData());
        }
    }
    if (result == SQLITE_OK)
    {
        result = execute(handle, "DELETE FROM change_log");
    }
    return result;
}

int PDFDatabaseHelper::alignPageSize(sqlite3* copy, sqlite3* target)
{
    const qint64 targetPageSize = PDFDatabase::queryIntegerOnHandle(target, "PRAGMA page_size", -1);
    const qint64 copyPageSize = PDFDatabase::queryIntegerOnHandle(copy, "PRAGMA page_size", -1);
    if (targetPageSize <= 0 || copyPageSize <= 0)
    {
        return SQLITE_ERROR;
    }
    if (targetPageSize == copyPageSize)
    {
        return SQLITE_OK;
    }

    const QByteArray sql = "PRAGMA page_size = " + QByteArray::number(targetPageSize) + "; VACUUM";
    const int result = execute(copy, sql.constData());
    if (result != SQLITE_OK)
    {
        return result;
    }
    return PDFDatabase::queryIntegerOnHandle(copy, "PRAGMA page_size", -1) == targetPageSize ? SQLITE_OK : SQLITE_ERROR;
}

bool PDFDatabaseHelper::isLink(const QString& filePath)
{
    const QFileInfo info(filePath);
    return info.isSymLink() || info.isJunction() || info.isShortcut();
}

PDFDatabaseError PDFDatabaseHelper::refuseFile(const char* text)
{
    return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabaseFiles", text));
}

PDFDatabaseError PDFDatabaseHelper::checkFileNotInUse(const QString& filePath)
{
    // A connection in the WAL mode holds a shared lock of the database file as long
    // as it is open. The exclusive locking mode set before the first read takes the
    // exclusive lock of the file (no wal-index in the shared memory is used), which
    // fails at once, when any other connection has the file open.
    PDFDatabaseError error;
    sqlite3* handle = openHandle(filePath.toUtf8(), SQLITE_OPEN_READWRITE, &error);
    if (!handle)
    {
        return error;
    }

    sqlite3_db_config(handle, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(handle, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(handle, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, nullptr);
    sqlite3_busy_timeout(handle, 0);

    int result = execute(handle, "PRAGMA locking_mode = EXCLUSIVE");
    if (result == SQLITE_OK)
    {
        result = execute(handle, "BEGIN EXCLUSIVE");
        if (result == SQLITE_OK)
        {
            // The first read takes the lock of the file
            sqlite3_stmt* statement = nullptr;
            result = sqlite3_prepare_v2(handle, "SELECT COUNT(*) FROM sqlite_master", -1, &statement, nullptr);
            if (result == SQLITE_OK)
            {
                const int stepResult = sqlite3_step(statement);
                result = (stepResult == SQLITE_ROW || stepResult == SQLITE_DONE) ? SQLITE_OK : stepResult;
            }
            sqlite3_finalize(statement);
            execute(handle, "ROLLBACK");
        }
    }

    error = PDFDatabaseError::fromHandle(handle, result);
    PDFDatabase::closeHandle(handle);
    return error;
}

QString PDFDatabaseFiles::getFileName(int schemaVersion)
{
    return QStringLiteral("pdf4qt-%1.db").arg(schemaVersion);
}

QString PDFDatabaseFiles::getFilePath(const QString& directory, int schemaVersion)
{
    return directory.isEmpty() ? QString() : QDir(directory).filePath(getFileName(schemaVersion));
}

int PDFDatabaseFiles::getSchemaVersion(const QString& fileName)
{
    // Only the exact names written by PDF4QT: no leading zero, no sign, at most
    // 9 digits (no overflow), no suffix - anchored at the real end (\z): '$' also
    // matches before a trailing newline, "pdf4qt-1.db\n" is another file on Unix
    static const QRegularExpression expression(QRegularExpression::anchoredPattern(QStringLiteral("pdf4qt-([1-9][0-9]{0,8})\\.db")));
    const QRegularExpressionMatch match = expression.match(fileName);
    if (!match.hasMatch())
    {
        return -1;
    }

    bool isValid = false;
    const int version = match.captured(1).toInt(&isValid);
    return (isValid && version > 0) ? version : -1;
}

std::vector<PDFDatabaseFileInfo> PDFDatabaseFiles::getFiles(const QString& directory, int currentSchemaVersion)
{
    std::vector<PDFDatabaseFileInfo> files;
    if (directory.isEmpty())
    {
        return files;
    }

    const QDir folder(directory);
    const QStringList names = folder.entryList(QDir::Files | QDir::Hidden);
    for (const QString& name : names)
    {
        const int version = getSchemaVersion(name);
        if (version < 0)
        {
            continue;
        }

        PDFDatabaseFileInfo info;
        info.filePath = QDir::cleanPath(folder.absoluteFilePath(name));
        info.schemaVersion = version;
        info.isCurrent = (version == currentSchemaVersion);

        const QFileInfo fileInfo(info.filePath);
        info.size = fileInfo.size();
        info.modified = fileInfo.lastModified();

        const QFileInfo walInfo(info.filePath + QStringLiteral("-wal"));
        if (walInfo.exists())
        {
            info.size += walInfo.size();
            if (walInfo.lastModified() > info.modified)
            {
                info.modified = walInfo.lastModified();
            }
        }

        files.push_back(std::move(info));
    }

    std::sort(files.begin(), files.end(), PDFDatabaseHelper::isNewerFile);
    return files;
}

QString PDFDatabaseFiles::getUpgradeLockPath(const QString& directory)
{
    return QDir(directory).filePath(QLatin1String(UPGRADE_LOCK_FILE_NAME));
}

bool PDFDatabaseFiles::isUpgradeTemporaryFileName(const QString& fileName)
{
    static const QRegularExpression expression(QStringLiteral("^pdf4qt-[1-9][0-9]{0,8}\\.db\\.upgrade-"));
    return expression.match(fileName).hasMatch();
}

PDFDatabaseError PDFDatabaseFiles::deleteOlderFile(const QString& directory, const QString& filePath, int currentSchemaVersion, int lockTimeout)
{
    if (directory.isEmpty() || filePath.isEmpty())
    {
        return PDFDatabaseHelper::refuseFile(QT_TRANSLATE_NOOP("pdf::PDFDatabaseFiles", "The file is not a database file of PDF4QT."));
    }

    // Only a file listed by getFiles: the exact path of a pdf4qt-<n>.db of the folder
    const QString folder = QDir::cleanPath(QDir(directory).absolutePath());
    const QString path = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    std::optional<PDFDatabaseFileInfo> found;
    for (const PDFDatabaseFileInfo& file : getFiles(folder, currentSchemaVersion))
    {
        if (file.filePath == path)
        {
            found = file;
            break;
        }
    }

    if (!found)
    {
        return PDFDatabaseHelper::refuseFile(QT_TRANSLATE_NOOP("pdf::PDFDatabaseFiles", "The file is not a database file of PDF4QT."));
    }
    if (found->schemaVersion == currentSchemaVersion)
    {
        return PDFDatabaseHelper::refuseFile(QT_TRANSLATE_NOOP("pdf::PDFDatabaseFiles", "The file is the database of this PDF4QT."));
    }
    if (found->schemaVersion > currentSchemaVersion)
    {
        return PDFDatabaseHelper::refuseFile(QT_TRANSLATE_NOOP("pdf::PDFDatabaseFiles", "The file is used by a newer PDF4QT."));
    }

    // A link would delete the link only, but its target could be opened by the check:
    // no file of the folder may lead elsewhere
    const QStringList companions = { path, path + QStringLiteral("-wal"), path + QStringLiteral("-shm"), path + QStringLiteral("-journal") };
    const QString canonicalFolder = QFileInfo(folder).canonicalFilePath();
    for (const QString& companion : companions)
    {
        const QFileInfo info(companion);
        if (PDFDatabaseHelper::isLink(companion) || (info.exists() && !info.isFile()))
        {
            return PDFDatabaseHelper::refuseFile(QT_TRANSLATE_NOOP("pdf::PDFDatabaseFiles", "The file is a link to another file. PDF4QT does not delete it."));
        }
    }
    if (canonicalFolder.isEmpty() || QFileInfo(QFileInfo(path).canonicalFilePath()).absolutePath() != canonicalFolder)
    {
        return PDFDatabaseHelper::refuseFile(QT_TRANSLATE_NOOP("pdf::PDFDatabaseFiles", "The file is a link to another file. PDF4QT does not delete it."));
    }

    // Under the lock of the upgrades: no other PDF4QT creates its file from this one meanwhile
    PDFDatabaseError error;
    sqlite3* lock = PDFDatabaseHelper::lockUpgrades(folder, lockTimeout, &error);
    if (!lock)
    {
        if (error.isBusy())
        {
            return PDFDatabaseError(SQLITE_BUSY, SQLITE_BUSY, QCoreApplication::translate("pdf::PDFDatabaseFiles", "Another PDF4QT is changing the settings right now. Try again in a moment."));
        }
        return error;
    }

    error = PDFDatabaseHelper::checkFileNotInUse(path);
    if (error.isBusy())
    {
        PDFDatabaseHelper::unlockUpgrades(lock);
        return PDFDatabaseError(SQLITE_BUSY, error.getExtendedCode(), QCoreApplication::translate("pdf::PDFDatabaseFiles", "The file is used by another PDF4QT. Close it and try again."));
    }

    // A damaged file (the check could not read it) is deleted as well: it is not used
    // by anything, which could read it. Errors of the environment are reported.
    if (error.isError() && !PDFDatabaseHelper::isDamageCode(error.getCode()))
    {
        PDFDatabaseHelper::unlockUpgrades(lock);
        return error;
    }

    PDFDatabaseError result;
    for (const QString& companion : { companions[1], companions[2], companions[3], companions[0] })
    {
        if (QFileInfo::exists(companion) && !QFile::remove(companion))
        {
            result = PDFDatabaseError(SQLITE_IOERR, SQLITE_IOERR, QCoreApplication::translate("pdf::PDFDatabaseFiles", "The file %1 could not be deleted.").arg(QDir::toNativeSeparators(companion)));
            break;
        }
    }
    PDFDatabaseHelper::unlockUpgrades(lock);

    if (!result.isError())
    {
        qCInfo(lcPdf4QtStorage) << "Older database file deleted:" << path;
    }
    return result;
}

QString PDFDatabaseFiles::getVersionLabel(int schemaVersion)
{
    for (const auto& [schema, version] : PDFDatabaseHelper::getVersionLabels())
    {
        if (schema == schemaVersion)
        {
            return QCoreApplication::translate("pdf::PDFDatabaseFiles", "PDF4QT %1").arg(QString::fromLatin1(version));
        }
    }

    if (schemaVersion > getNewestLabeledSchemaVersion())
    {
        return QCoreApplication::translate("pdf::PDFDatabaseFiles", "Newer PDF4QT");
    }
    return QCoreApplication::translate("pdf::PDFDatabaseFiles", "PDF4QT (schema %1)").arg(schemaVersion);
}

int PDFDatabaseFiles::getNewestLabeledSchemaVersion()
{
    int newest = 0;
    for (const auto& label : PDFDatabaseHelper::getVersionLabels())
    {
        newest = qMax(newest, label.first);
    }
    return newest;
}

PDFDatabaseBackup PDFDatabaseBackup::read(const QString& filePath, const std::vector<PDFDatabaseMigration>& migrations)
{
    PDFDatabaseBackup backup;
    backup.m_filePath = filePath;
    backup.m_applicationSchemaVersion = PDFDatabaseSchema::getVersion(migrations.empty() ? PDFDatabaseSchema::getMigrations() : migrations);

    const QFileInfo info(filePath);
    if (filePath.isEmpty() || !info.exists() || !info.isFile())
    {
        backup.m_detail = QStringLiteral("missing");
        return backup;
    }

    backup.m_size = info.size();
    if (backup.m_size > MAXIMUM_SIZE)
    {
        backup.m_status = PDFDatabaseBackupStatus::TooLarge;
        return backup;
    }

    // The whole file is read once: the checked content is the imported content
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        backup.m_detail = file.errorString();
        return backup;
    }

    const QByteArray data = file.read(MAXIMUM_SIZE + 1);
    file.close();
    if (data.size() > MAXIMUM_SIZE)
    {
        backup.m_status = PDFDatabaseBackupStatus::TooLarge;
        backup.m_size = data.size();
        return backup;
    }

    PDFDatabaseBackup result = fromData(data, filePath, migrations);
    if (result.isValid() && !result.m_created.isValid())
    {
        result.m_created = info.lastModified();
    }
    return result;
}

PDFDatabaseBackup PDFDatabaseBackup::fromData(const QByteArray& data, const QString& filePath, const std::vector<PDFDatabaseMigration>& migrations)
{
    const std::vector<PDFDatabaseMigration>& usedMigrations = (migrations.empty() || !PDFDatabaseSchema::isValid(migrations)) ? PDFDatabaseSchema::getMigrations() : migrations;

    PDFDatabaseBackup backup;
    backup.m_filePath = filePath;
    backup.m_size = data.size();
    backup.m_applicationSchemaVersion = PDFDatabaseSchema::getVersion(usedMigrations);
    backup.m_status = PDFDatabaseBackupStatus::NotABackup;

    if (data.size() > MAXIMUM_SIZE)
    {
        backup.m_status = PDFDatabaseBackupStatus::TooLarge;
        return backup;
    }

    // The header of every database file of SQLite
    static constexpr char HEADER[] = "SQLite format 3";
    if (data.size() < 512 || !data.startsWith(QByteArrayView(HEADER, sizeof(HEADER))))
    {
        backup.m_detail = QStringLiteral("not a database file");
        return backup;
    }

    PDFDatabaseError error;
    sqlite3* handle = PDFDatabaseHelper::openMemoryCopy(data, true, &error);
    if (!handle)
    {
        backup.m_status = PDFDatabaseHelper::isDamageCode(error.getCode()) ? PDFDatabaseBackupStatus::Damaged : PDFDatabaseBackupStatus::NotABackup;
        backup.m_detail = error.toString();
        return backup;
    }

    int queryResult = SQLITE_OK;
    const qint64 applicationId = PDFDatabaseHelper::queryText(handle, "PRAGMA application_id", &queryResult).toLongLong();
    error = PDFDatabaseError::fromHandle(handle, queryResult);
    const int schemaVersion = error.isError() ? -1 : PDFDatabaseSchema::readVersion(handle);

    if (error.isError())
    {
        backup.m_status = PDFDatabaseHelper::isDamageCode(error.getCode()) ? PDFDatabaseBackupStatus::Damaged : PDFDatabaseBackupStatus::NotABackup;
        backup.m_detail = error.toString();
    }
    else if (applicationId != PDFDatabase::APPLICATION_ID || schemaVersion <= 0)
    {
        backup.m_detail = QStringLiteral("application id %1, schema %2").arg(applicationId).arg(schemaVersion);
    }
    else if (schemaVersion > backup.m_applicationSchemaVersion)
    {
        backup.m_status = PDFDatabaseBackupStatus::Newer;
        backup.m_schemaVersion = schemaVersion;
    }
    else
    {
        backup.m_schemaVersion = schemaVersion;

        // Every page of the file (the schema is parsed here too). The full integrity
        // check, not quick_check: the pages of a backup are copied into the live
        // database as they are, so an index, whose content does not match its table
        // (two indexes of the same shape with swapped root pages pass quick_check),
        // or rows breaking UNIQUE / NOT NULL / CHECK must not get there.
        int result = SQLITE_OK;
        const QString check = PDFDatabaseHelper::queryText(handle, "PRAGMA integrity_check(1)", &result);
        const QString encoding = (result == SQLITE_OK) ? PDFDatabaseHelper::queryText(handle, "PRAGMA encoding", &result) : QString();
        QStringList schema;
        QStringList reference;
        if (result != SQLITE_OK || check != QLatin1String("ok"))
        {
            backup.m_status = PDFDatabaseBackupStatus::Damaged;
            backup.m_detail = result != SQLITE_OK ? PDFDatabaseError::fromHandle(handle, result).toString() : check.left(512);
        }
        else if (encoding != QLatin1String("UTF-8"))
        {
            // The application creates UTF-8 databases only; pages of another text
            // encoding copied into the live database break every open connection
            // ("attached databases must use the same text encoding")
            backup.m_detail = QStringLiteral("text encoding %1").arg(encoding.left(32));
        }
        else if (!PDFDatabaseHelper::readSchema(handle, &schema))
        {
            backup.m_status = PDFDatabaseBackupStatus::Damaged;
            backup.m_detail = QStringLiteral("schema cannot be read");
        }
        else if (!PDFDatabaseHelper::createReferenceSchema(usedMigrations, schemaVersion, &reference))
        {
            backup.m_detail = QStringLiteral("reference schema cannot be created");
        }
        else if (schema != reference)
        {
            // A foreign table, index, trigger or view, or a changed definition
            backup.m_detail = QStringLiteral("schema differs from the schema of version %1").arg(schemaVersion);
        }
        else
        {
            backup.m_status = PDFDatabaseBackupStatus::Valid;
            qint64 created = 0;
            if (PDFDatabaseHelper::readMaintenanceValue(handle, CREATED_KEY, &created) == PDFDatabaseHelper::MaintenanceValue::Read && created > 0)
            {
                backup.m_created = QDateTime::fromMSecsSinceEpoch(created);
            }
            backup.readSecurityData(handle);
        }
    }

    PDFDatabase::closeHandle(handle);

    if (backup.isValid())
    {
        backup.m_data = data;
    }
    else
    {
        qCWarning(lcPdf4QtStorage) << "Backup refused:" << filePath << backup.m_detail;
    }
    return backup;
}

QString PDFDatabaseBackup::getDefaultFileName(const QDate& date)
{
    return QStringLiteral("PDF4QT settings %1.%2").arg(date.toString(QStringLiteral("yyyy-MM-dd")), QLatin1String(FILE_SUFFIX));
}

QString PDFDatabaseBackup::getErrorText() const
{
    switch (m_status)
    {
        case PDFDatabaseBackupStatus::Valid:
            return QString();
        case PDFDatabaseBackupStatus::Missing:
            return QCoreApplication::translate("pdf::PDFDatabaseBackup", "The file cannot be read.");
        case PDFDatabaseBackupStatus::TooLarge:
            return QCoreApplication::translate("pdf::PDFDatabaseBackup", "The file is too large to be a PDF4QT settings backup.");
        case PDFDatabaseBackupStatus::NotABackup:
            return QCoreApplication::translate("pdf::PDFDatabaseBackup", "It is not a PDF4QT settings backup.");
        case PDFDatabaseBackupStatus::Damaged:
            return QCoreApplication::translate("pdf::PDFDatabaseBackup", "The backup is damaged and cannot be read.");
        case PDFDatabaseBackupStatus::Newer:
            return QCoreApplication::translate("pdf::PDFDatabaseBackup", "The backup was made by a newer version of PDF4QT. Update PDF4QT, then import it again.");
    }
    return QString();
}

QStringList PDFDatabaseBackup::getSecuritySettingKeys()
{
    return { PDFSettingKeys::securityOpenLinks(), PDFSettingKeys::securityLaunchFiles(), PDFSettingKeys::signaturesVerify(),
             PDFSettingKeys::signaturesStrict(), PDFSettingKeys::signaturesIgnoreExpiration(), PDFSettingKeys::signaturesUseSystemStore() };
}

QVariant PDFDatabaseBackup::getSecuritySetting(const QString& key) const
{
    auto it = m_securitySettings.find(key);
    return it != m_securitySettings.end() ? it->second : QVariant();
}

void PDFDatabaseBackup::readSecurityData(sqlite3* handle)
{
    // The data are untrusted: bounded by SQL (a huge value is never copied), a
    // failing statement (an older schema without the table) reads nothing
    m_trustedCertificates.clear();
    m_securitySettings.clear();

    const QByteArray certificateSql = "SELECT data FROM trusted_certificate WHERE typeof(data) = 'blob' AND length(data) BETWEEN 1 AND " +
                                      QByteArray::number(qint64(MAXIMUM_CERTIFICATE_SIZE)) + " ORDER BY id LIMIT " + QByteArray::number(MAXIMUM_CERTIFICATE_COUNT);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(handle, certificateSql.constData(), -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW && int(m_trustedCertificates.size()) < MAXIMUM_CERTIFICATE_COUNT)
        {
            const void* data = sqlite3_column_blob(statement, 0);
            const int size = sqlite3_column_bytes(statement, 0);
            if (data && size > 0 && size <= MAXIMUM_CERTIFICATE_SIZE)
            {
                m_trustedCertificates.emplace_back(static_cast<const char*>(data), qsizetype(size));
            }
        }
    }
    sqlite3_finalize(statement);

    statement = nullptr;
    const QByteArray settingSql = "SELECT key, CASE WHEN typeof(value) IN ('text', 'blob') AND length(CAST(value AS BLOB)) > " +
                                  QByteArray::number(qint64(MAXIMUM_SECURITY_VALUE_SIZE)) + " THEN NULL ELSE value END FROM setting WHERE key = ?1";
    if (sqlite3_prepare_v2(handle, settingSql.constData(), -1, &statement, nullptr) == SQLITE_OK)
    {
        for (const QString& key : getSecuritySettingKeys())
        {
            const QByteArray keyData = key.toUtf8();
            sqlite3_reset(statement);
            sqlite3_bind_text(statement, 1, keyData.constData(), int(keyData.size()), SQLITE_TRANSIENT);
            if (sqlite3_step(statement) != SQLITE_ROW)
            {
                continue;
            }

            // The types of PDFDatabaseStatement::getVariant (what the settings read)
            QVariant value;
            switch (sqlite3_column_type(statement, 1))
            {
                case SQLITE_INTEGER:
                    value = QVariant(qint64(sqlite3_column_int64(statement, 1)));
                    break;
                case SQLITE_FLOAT:
                    value = QVariant(sqlite3_column_double(statement, 1));
                    break;
                case SQLITE_TEXT:
                    value = QVariant(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)), sqlite3_column_bytes(statement, 1)));
                    break;
                case SQLITE_BLOB:
                {
                    const void* data = sqlite3_column_blob(statement, 1);
                    value = QVariant(data ? QByteArray(static_cast<const char*>(data), sqlite3_column_bytes(statement, 1)) : QByteArray());
                    break;
                }
                default:
                    break;
            }
            if (value.isValid())
            {
                m_securitySettings[key] = value;
            }
        }
    }
    sqlite3_finalize(statement);
}

PDFDatabase::PDFDatabase(QObject* parent) :
    QObject(parent),
    m_builtinMigrations(PDFDatabaseSchema::getMigrations()),
    m_registry(std::make_shared<PDFDatabaseRegistry>())
{
    // Another instance writes: the damage report is written a bit later
    m_damageReportTimer.setSingleShot(true);
    m_damageReportTimer.setInterval(2000);
    connect(&m_damageReportTimer, &QTimer::timeout, this, &PDFDatabase::persistDamageReport);
}

PDFDatabase::~PDFDatabase()
{
    close();
}

sqlite3* PDFDatabase::openFileConnection(const QString& filePath, bool readOnly, int busyTimeout, PDFDatabaseError* error)
{
    const int flags = readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite3* handle = PDFDatabaseHelper::openHandle(filePath.toUtf8(), flags, error);
    if (!handle)
    {
        return nullptr;
    }

    PDFDatabaseError pragmaError = PDFDatabaseHelper::applyPragmas(handle, false, readOnly, busyTimeout);
    if (pragmaError.isError())
    {
        if (error)
        {
            *error = pragmaError;
        }
        closeHandle(handle);
        return nullptr;
    }

    return handle;
}

void PDFDatabase::closeHandle(sqlite3* handle)
{
    if (handle)
    {
        sqlite3_close_v2(handle);
    }
}

sqlite3* PDFDatabase::openMemoryConnection(const QString& memoryName, PDFDatabaseError* error)
{
    QString name = memoryName;
    if (!name.startsWith(QLatin1Char('/')))
    {
        name.prepend(QLatin1Char('/'));
    }

    const QString uri = QStringLiteral("file:%1?vfs=memdb").arg(QString::fromLatin1(QUrl::toPercentEncoding(name, "/")));
    return PDFDatabaseHelper::openHandle(uri.toUtf8(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, error);
}

PDFDatabaseError PDFDatabase::executeOnHandle(sqlite3* handle, const char* sql)
{
    if (!handle)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
    }
    return PDFDatabaseError::fromHandle(handle, sqlite3_exec(handle, sql, nullptr, nullptr, nullptr));
}

qint64 PDFDatabase::queryIntegerOnHandle(sqlite3* handle, const char* sql, qint64 defaultValue)
{
    if (!handle)
    {
        return defaultValue;
    }

    PDFDatabaseConnection probe(nullptr, 0);
    probe.m_handle = handle;
    const qint64 value = probe.queryInteger(sql, defaultValue);
    probe.m_handle = nullptr;
    return value;
}

QString PDFDatabase::getSQLiteVersion()
{
    return QString::fromLatin1(sqlite3_libversion());
}

const std::vector<PDFDatabaseMigration>& PDFDatabase::getMigrations() const
{
    return m_options.migrations.empty() ? m_builtinMigrations : m_options.migrations;
}

void PDFDatabase::open(const PDFDatabaseOptions& options)
{
    close();

    m_options = options;
    if (!m_options.directory.isEmpty())
    {
        m_options.directory = QDir::cleanPath(QDir(m_options.directory).absolutePath());
    }
    if (!PDFDatabaseSchema::isValid(m_options.migrations))
    {
        qCWarning(lcPdf4QtStorage) << "Invalid migrations, the built-in schema is used.";
        m_options.migrations.clear();
    }
    if (!m_options.memoryName.startsWith(QLatin1Char('/')))
    {
        m_options.memoryName.prepend(QLatin1Char('/'));
    }
    m_options.busyTimeout = qBound(0, m_options.busyTimeout, 600000);
    m_options.lockWaitLimit = qMax(-1, m_options.lockWaitLimit);
    m_options.integrityCheckInterval = qMax(qint64(0), m_options.integrityCheckInterval);

    openImpl();
}

void PDFDatabase::openImpl()
{
    PDFDatabaseState state;
    state.applicationSchemaVersion = PDFDatabaseSchema::getVersion(getMigrations());
    state.filePath = m_options.portable ? QString() : getDiskFilePath();
    m_fallbackFilePath = state.filePath;

    {
        QMutexLocker locker(&m_registry->mutex);
        m_registry->generation = ++m_generation;
        m_registry->busyTimeout = m_options.busyTimeout;
    }

    setState(state);

    if (m_options.portable)
    {
        openMemory(QString(), PDFDatabaseStorageMode::Portable);
        return;
    }

    PDFDatabaseError error;
    if (!openDiskWithRetry(&error))
    {
        PDFDatabaseState fallbackState = getState();
        fallbackState.error = error;
        setState(fallbackState);
        qCWarning(lcPdf4QtStorage) << "Database cannot be opened, memory fallback is used:" << error.toString();
        openMemory(m_fallbackFilePath, PDFDatabaseStorageMode::MemoryFallback);
    }
}

bool PDFDatabase::isFileDamaged(sqlite3* handle, const QString& filePath, bool forceFullCheck, bool* fullCheckPerformed, PDFDatabaseError* error) const
{
    *fullCheckPerformed = false;

    // Cheap checks of every opening. The header: a file, which is not a database,
    // fails here (SQLITE_NOTADB); a foreign file (another application id) is treated
    // as a damaged file.
    PDFDatabaseConnection probe(nullptr, 0);
    probe.m_handle = handle;

    PDFDatabaseError queryError;
    const qint64 applicationId = probe.queryInteger("PRAGMA application_id", 0, &queryError);

    if (!queryError.isError() && applicationId != 0 && applicationId != APPLICATION_ID)
    {
        probe.m_handle = nullptr;
        *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The file is not a database of PDF4QT."));
        return true;
    }

    // The schema (it is parsed by every statement, also by the read of the time of
    // the last integrity check)
    if (!queryError.isError())
    {
        probe.queryInteger("SELECT COUNT(*) FROM sqlite_master", 0, &queryError);
    }
    probe.m_handle = nullptr;

    if (queryError.isError())
    {
        *error = queryError;
        return PDFDatabaseHelper::isDamageCode(queryError.getCode());
    }

    // The full check reads every page of the file (thumbnails of the recent files):
    // only when it is due (PDFDatabaseOptions::integrityCheckInterval)
    if (!forceFullCheck && !isIntegrityCheckDue(handle, filePath))
    {
        *error = PDFDatabaseError();
        return false;
    }

    int result = SQLITE_OK;
    const QString check = PDFDatabaseHelper::queryText(handle, "PRAGMA quick_check", &result);
    if (result != SQLITE_OK)
    {
        *error = PDFDatabaseError::fromHandle(handle, result);
        return PDFDatabaseHelper::isDamageCode(result);
    }

    if (check != QLatin1String("ok"))
    {
        *error = PDFDatabaseError(SQLITE_CORRUPT, SQLITE_CORRUPT, check);
        return true;
    }

    *fullCheckPerformed = true;
    *error = PDFDatabaseError();
    return false;
}

bool PDFDatabase::isIntegrityCheckDue(sqlite3* handle, const QString& filePath) const
{
    if (m_options.integrityCheckInterval <= 0)
    {
        return true;
    }

    // An instance could not write its damage report into the database
    if (!filePath.isEmpty() && QFileInfo::exists(getIntegrityCheckMarkerPath(filePath)))
    {
        return true;
    }

    // No time of the check (a new file, an older schema, the value cannot be read) = due
    qint64 lastCheck = 0;
    if (PDFDatabaseHelper::readMaintenanceValue(handle, PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY, &lastCheck) != PDFDatabaseHelper::MaintenanceValue::Read)
    {
        return true;
    }

    // An instance has met a damage since the start of the last check (a request,
    // which cannot be read, is a request too)
    qint64 requested = 0;
    switch (PDFDatabaseHelper::readMaintenanceValue(handle, PDFDatabaseSchema::INTEGRITY_REQUESTED_KEY, &requested))
    {
        case PDFDatabaseHelper::MaintenanceValue::Read:
            if (requested >= lastCheck)
            {
                return true;
            }
            break;

        case PDFDatabaseHelper::MaintenanceValue::Missing:
            break;

        case PDFDatabaseHelper::MaintenanceValue::Invalid:
            return true;
    }

    // The time of the check is in the future (the clock was set back): due
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastCheck > now + 60 * 1000)
    {
        return true;
    }

    return now - lastCheck >= m_options.integrityCheckInterval;
}

void PDFDatabase::recordIntegrityCheck(sqlite3* handle, const QString& filePath, qint64 checkTime) const
{
    // The time of the start of the check: a damage reported during the check is newer,
    // so the next opening checks the file again. The table has no triggers, the write
    // is not a change of the data for the other instances.
    const int result = PDFDatabaseHelper::writeMaintenanceValue(handle, PDFDatabaseSchema::INTEGRITY_LAST_CHECK_KEY, checkTime);
    if (result != SQLITE_OK)
    {
        // The next opening checks the file again
        qCWarning(lcPdf4QtStorage) << "Time of the integrity check cannot be written:" << PDFDatabaseError::fromHandle(handle, result).toString();
        return;
    }

    // A request of another type (not written by PDF4QT) would request the check at
    // every opening; this check has answered it
    sqlite3_exec(handle, "DELETE FROM storage_maintenance WHERE key = 'integrity.requested' AND typeof(value) <> 'integer'", nullptr, nullptr, nullptr);

    // The marker is removed only, when the check has started after the damage: another
    // instance may have written it during the check (the damage was met after the
    // start). A time in the future (a clock set back, not written by PDF4QT) would
    // request the check at every opening, it is not kept.
    const QString markerPath = getIntegrityCheckMarkerPath(filePath);
    if (QFileInfo::exists(markerPath))
    {
        const qint64 markerTime = PDFDatabaseHelper::readMarkerTime(markerPath);
        if (markerTime < checkTime || markerTime > QDateTime::currentMSecsSinceEpoch() + 60 * 1000)
        {
            QFile::remove(markerPath);
        }
    }
}

QString PDFDatabase::getIntegrityCheckMarkerPath(const QString& filePath)
{
    return filePath + QStringLiteral("-integrity-check");
}

void PDFDatabase::reportError(const PDFDatabaseError& error)
{
    if (!PDFDatabaseHelper::isDamageCode(error.getCode()))
    {
        return;
    }

    // Only the first damage is reported, until it is written
    qint64 expected = 0;
    if (m_damageReportTime.compare_exchange_strong(expected, QDateTime::currentMSecsSinceEpoch()))
    {
        qCWarning(lcPdf4QtStorage) << "Database reports a damage, the next opening checks its integrity:" << error.toString();

        // The write waits for the GUI thread: the reporting connection may be inside
        // of a transaction, which would roll the report back
        QMetaObject::invokeMethod(this, &PDFDatabase::persistDamageReport, Qt::QueuedConnection);
    }
}

void PDFDatabase::persistDamageReport()
{
    if (m_damageReportTime.load() != 0 && !writeDamageReport(false))
    {
        // Another instance writes or a transaction of this thread is open
        m_damageReportTimer.start();
    }
}

bool PDFDatabase::writeDamageReport(bool isClosing)
{
    qint64 reportTime = m_damageReportTime.load();
    if (reportTime == 0)
    {
        return true;
    }

    const PDFDatabaseState state = getState();
    if (!state.isPersistent() || state.filePath.isEmpty())
    {
        // A database in memory is not checked
        m_damageReportTime.compare_exchange_strong(reportTime, 0);
        return true;
    }

    bool isWritten = false;
    bool isBusy = false;

    // A closed database has no connection: the marker file is written at once (a damage
    // met by close() itself or reported after it must not wait for a timer forever)
    PDFDatabaseConnection* connection = getConnection();
    if (connection && connection->isValid())
    {
        if (connection->isInTransaction())
        {
            // A transaction of this thread is open: the record would become a part of it
            isBusy = true;
        }
        else
        {
            // Never waits for another instance (the thread of the user interface)
            const int busyTimeout = connection->getBusyTimeout();
            connection->setBusyTimeout(0);
            const int result = PDFDatabaseHelper::writeIntegrityRequest(connection->getHandle(), reportTime);
            connection->setBusyTimeout(busyTimeout);

            isWritten = (result == SQLITE_OK);
            isBusy = (result & 0xFF) == SQLITE_BUSY || (result & 0xFF) == SQLITE_LOCKED;
        }
    }

    if (!isWritten)
    {
        if (isBusy && !isClosing)
        {
            return false;
        }

        // The database refuses the write (a damaged page, a full disk, it is closing
        // while another instance writes, it is closed): a marker file next to the database
        QFile marker(getIntegrityCheckMarkerPath(state.filePath));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        {
            marker.write(QByteArray::number(reportTime));
            marker.close();
        }
        else
        {
            qCWarning(lcPdf4QtStorage) << "Damage of the database cannot be recorded:" << marker.errorString();
        }
    }

    m_damageReportTime.compare_exchange_strong(reportTime, 0);
    return true;
}

QString PDFDatabase::renameDamagedFile(const QString& filePath) const
{
    const QString baseName = filePath + QStringLiteral(".damaged-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    QString targetName = baseName;
    for (int i = 2; QFileInfo::exists(targetName) && i < 1000; ++i)
    {
        targetName = baseName + QStringLiteral("-%1").arg(i);
    }

    if (!QFile::rename(filePath, targetName))
    {
        return QString();
    }

    // The log belongs to the damaged file, it keeps its name pair, the shared
    // memory index is rebuilt by SQLite
    const QString walName = filePath + QStringLiteral("-wal");
    if (QFileInfo::exists(walName))
    {
        QFile::rename(walName, targetName + QStringLiteral("-wal"));
    }
    QFile::remove(filePath + QStringLiteral("-shm"));
    return targetName;
}

bool PDFDatabase::openDiskWithRetry(PDFDatabaseError* error)
{
    // Instances started at the same time open a new file together: the change of
    // the journal mode and the migration may report SQLITE_BUSY without waiting
    // (SQLite avoids a deadlock of two readers upgrading to a writer). Another
    // instance holding the lock (a migration in progress) is waited for until it
    // releases it, there is no memory fallback after a time (newapp_storage.md 5,
    // decision of the owner; PDFDatabaseOptions::lockWaitLimit only in tests). Each
    // attempt waits inside of SQLite for the busy timeout, a short pause between the
    // attempts covers busy errors reported without waiting. Every other error ends
    // the opening at once.
    constexpr qint64 LOG_INTERVAL = 10000;
    QElapsedTimer waitTimer;
    waitTimer.start();
    qint64 nextLogTime = LOG_INTERVAL;
    int pause = 20;

    m_forceIntegrityCheck = false;
    bool result = false;
    for (;;)
    {
        if (openDisk(error))
        {
            result = true;
            break;
        }

        if (PDFDatabaseHelper::isDamageCode(error->getCode()) && !m_forceIntegrityCheck)
        {
            // A damage found after the cheap checks (version, migration): the full
            // integrity check decides, a damaged file is renamed
            m_forceIntegrityCheck = true;
            continue;
        }

        if (!error->isBusy())
        {
            break;
        }

        const qint64 waited = waitTimer.elapsed();
        if (m_options.lockWaitLimit >= 0 && waited >= m_options.lockWaitLimit)
        {
            break;
        }

        if (waited >= nextLogTime)
        {
            qCInfo(lcPdf4QtStorage) << "Database is locked by another instance, waiting for" << waited / 1000 << "s:" << error->toString();
            nextLogTime = waited + LOG_INTERVAL;
        }

        QThread::msleep(static_cast<unsigned long>(pause));
        pause = qMin(pause + 20, 200);
    }

    m_forceIntegrityCheck = false;
    return result;
}

QString PDFDatabase::getDiskFilePath() const
{
    if (!m_options.directory.isEmpty())
    {
        return PDFDatabaseFiles::getFilePath(m_options.directory, PDFDatabaseSchema::getVersion(getMigrations()));
    }
    return m_options.filePath;
}

std::vector<PDFDatabaseFileInfo> PDFDatabase::getDatabaseFiles() const
{
    if (m_options.directory.isEmpty())
    {
        return std::vector<PDFDatabaseFileInfo>();
    }
    return PDFDatabaseFiles::getFiles(m_options.directory, PDFDatabaseSchema::getVersion(getMigrations()));
}

bool PDFDatabase::prepareSchemaFile(PDFDatabaseError* error, QString* newerFilePath)
{
    // Owner decision O19: the file of the schema version of the application. When it
    // exists, it is opened. Otherwise the newest older file is copied (backup API: a
    // consistent snapshot also while an older version writes it), the copy is migrated
    // and checked in a temporary file and renamed - the older file is never written.
    // Only newer files: the newest of them is copied into memory (nothing is written).
    const QString directory = m_options.directory;
    const int applicationVersion = PDFDatabaseSchema::getVersion(getMigrations());
    const QString targetPath = PDFDatabaseFiles::getFilePath(directory, applicationVersion);
    *error = PDFDatabaseError();
    newerFilePath->clear();

    if (QFileInfo::exists(targetPath))
    {
        m_fallbackFilePath = targetPath;
        return true;
    }

    QString olderFilePath;
    QString newestFilePath;
    for (const PDFDatabaseFileInfo& file : PDFDatabaseFiles::getFiles(directory, applicationVersion))
    {
        if (file.schemaVersion > applicationVersion && newestFilePath.isEmpty())
        {
            newestFilePath = file.filePath;
        }
        if (file.schemaVersion < applicationVersion)
        {
            olderFilePath = file.filePath;
            break;
        }
    }

    if (olderFilePath.isEmpty() && !newestFilePath.isEmpty())
    {
        // An older version of the application started after a newer one
        *newerFilePath = newestFilePath;
        m_fallbackFilePath = newestFilePath;
        return true;
    }

    // One process creates the file, the others wait for the lock and find it then.
    // SQLITE_BUSY after the busy timeout is waited for by openDiskWithRetry.
    sqlite3* lock = PDFDatabaseHelper::lockUpgrades(directory, m_options.busyTimeout, error);
    if (!lock)
    {
        m_fallbackFilePath = olderFilePath.isEmpty() ? targetPath : olderFilePath;
        return false;
    }

    // A temporary file found by the holder of the lock was left by a crash
    PDFDatabaseHelper::removeUpgradeTemporaryFiles(directory);

    if (QFileInfo::exists(targetPath))
    {
        // Another process has created it meanwhile
        PDFDatabaseHelper::unlockUpgrades(lock);
        m_fallbackFilePath = targetPath;
        return true;
    }

    // The files are listed again under the lock (an older version may have created
    // its file meanwhile)
    olderFilePath.clear();
    for (const PDFDatabaseFileInfo& file : PDFDatabaseFiles::getFiles(directory, applicationVersion))
    {
        if (file.schemaVersion < applicationVersion)
        {
            olderFilePath = file.filePath;
            break;
        }
    }
    m_fallbackFilePath = olderFilePath.isEmpty() ? targetPath : olderFilePath;

    const QString temporaryPath = targetPath + QStringLiteral(".upgrade-%1-%2").arg(QCoreApplication::applicationPid()).arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    bool isSourceUnusable = false;
    bool isCreated = createSchemaFile(olderFilePath, temporaryPath, targetPath, error, &isSourceUnusable);
    QString unusableFilePath;
    if (!isCreated && isSourceUnusable && !olderFilePath.isEmpty())
    {
        // The older file cannot be copied or migrated (damaged, a foreign file, its
        // migration fails): it is kept unchanged under its name, the new file starts
        // empty and the user is told (the banner of a replaced database)
        qCWarning(lcPdf4QtStorage) << "Older database file cannot be upgraded, it is kept and a new database is created:" << olderFilePath << error->toString();
        unusableFilePath = olderFilePath;
        PDFDatabaseHelper::removeTemporaryFile(temporaryPath);
        isCreated = createSchemaFile(QString(), temporaryPath, targetPath, error, &isSourceUnusable);
    }

    if (isCreated)
    {
        // The rename is atomic: the file of the schema version appears only complete.
        // A scanner of the system may hold the new file for a moment (Windows).
        bool isRenamed = false;
        for (int attempt = 0; attempt < 10 && !isRenamed; ++attempt)
        {
            if (attempt > 0)
            {
                QThread::msleep(50);
            }
            isRenamed = QFile::rename(temporaryPath, targetPath) || QFileInfo::exists(targetPath);
        }

        if (!isRenamed)
        {
            *error = PDFDatabaseError(SQLITE_CANTOPEN, SQLITE_CANTOPEN, QCoreApplication::translate("pdf::PDFDatabase", "The file %1 cannot be created.").arg(QDir::toNativeSeparators(targetPath)));
            isCreated = false;
        }
    }

    PDFDatabaseHelper::removeTemporaryFile(temporaryPath);
    PDFDatabaseHelper::unlockUpgrades(lock);

    if (!isCreated)
    {
        // A busy older file is tried again by openDiskWithRetry, other errors lead to
        // the memory fallback with the content of the older file (Try Again)
        qCWarning(lcPdf4QtStorage) << "Database file" << targetPath << "cannot be created:" << error->toString();
        return false;
    }

    m_fallbackFilePath = targetPath;
    if (unusableFilePath.isEmpty() && !olderFilePath.isEmpty())
    {
        qCInfo(lcPdf4QtStorage) << "Database file" << targetPath << "was created from" << olderFilePath;
    }

    // The next attempt of the opening starts from the stored state (no signal, the
    // opening goes on)
    QMutexLocker locker(&m_stateMutex);
    m_state.damagedFilePath = unusableFilePath;
    m_state.upgradedFromFilePath = unusableFilePath.isEmpty() ? olderFilePath : QString();
    return true;
}

bool PDFDatabase::createSchemaFile(const QString& sourcePath, const QString& temporaryPath, const QString& targetPath, PDFDatabaseError* error, bool* isSourceUnusable)
{
    *isSourceUnusable = false;
    PDFDatabaseHelper::removeTemporaryFile(temporaryPath);

    // The temporary file has a rollback journal: it is complete and alone, when it is
    // renamed (the opening switches the file of the schema version to WAL)
    sqlite3* destination = PDFDatabaseHelper::openHandle(temporaryPath.toUtf8(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, error);
    if (!destination)
    {
        return false;
    }
    PDFDatabaseHelper::restrictPermissions(temporaryPath);

    if (!sourcePath.isEmpty())
    {
        // Read only, never a checkpoint at the closing: the older file is not written
        // (also when its write-ahead log has frames of a crashed older version)
        PDFDatabaseError sourceError;
        sqlite3* source = openFileConnection(sourcePath, true, m_options.busyTimeout, &sourceError);
        int result = source ? SQLITE_OK : sourceError.getCode();
        if (source)
        {
            sqlite3_db_config(source, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, nullptr);

            // A foreign file with the name of a database file of PDF4QT is not copied
            PDFDatabaseConnection probe(nullptr, 0);
            probe.m_handle = source;
            const qint64 applicationId = probe.queryInteger("PRAGMA application_id", 0, &sourceError);
            probe.m_handle = nullptr;
            if (sourceError.isError())
            {
                result = sourceError.getCode();
            }
            else if (applicationId != 0 && applicationId != APPLICATION_ID)
            {
                sourceError = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The file is not a database of PDF4QT."));
                result = SQLITE_NOTADB;
            }
        }

        if (result == SQLITE_OK)
        {
            // One step: the whole file in one read transaction of the source
            sqlite3_backup* backup = sqlite3_backup_init(destination, "main", source, "main");
            if (!backup)
            {
                result = sqlite3_errcode(destination);
                sourceError = PDFDatabaseError::fromHandle(destination, result);
            }
            else
            {
                const int stepResult = sqlite3_backup_step(backup, -1);
                const int finishResult = sqlite3_backup_finish(backup);
                result = (stepResult != SQLITE_DONE) ? stepResult : finishResult;
                if (result != SQLITE_OK)
                {
                    sourceError = PDFDatabaseError::fromHandle(destination, result);
                    if (!sourceError.isError())
                    {
                        sourceError = PDFDatabaseError::fromHandle(nullptr, result);
                    }
                }
            }
        }
        closeHandle(source);

        if (result != SQLITE_OK)
        {
            *error = sourceError.isError() ? sourceError : PDFDatabaseError::fromHandle(nullptr, result);
            *isSourceUnusable = !PDFDatabaseHelper::isEnvironmentError(error->getCode());
            closeHandle(destination);
            return false;
        }

        if (m_options.testExitAfterUpgradeCopy)
        {
            // Tests: the process dies after the copy (a crash in the middle of the upgrade)
            std::_Exit(PDFDatabaseOptions::TEST_EXIT_CODE);
        }

        // The copy of a WAL file has the header of a WAL file
        int queryResult = SQLITE_OK;
        const QString journalMode = PDFDatabaseHelper::queryText(destination, "PRAGMA journal_mode = DELETE", &queryResult);
        if (queryResult != SQLITE_OK || journalMode.compare(QLatin1String("delete"), Qt::CaseInsensitive) != 0)
        {
            *error = queryResult != SQLITE_OK ? PDFDatabaseError::fromHandle(destination, queryResult)
                                              : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The journal mode of the copy cannot be changed."));
            *isSourceUnusable = !PDFDatabaseHelper::isEnvironmentError(error->getCode());
            closeHandle(destination);
            return false;
        }
    }

    // The pragmas of the application except WAL (the content of the copy is not trusted)
    *error = PDFDatabaseHelper::applyPragmas(destination, false, true, m_options.busyTimeout);
    const int applicationVersion = PDFDatabaseSchema::getVersion(getMigrations());
    if (!error->isError())
    {
        *error = PDFDatabaseSchema::migrate(destination, getMigrations(), applicationVersion);
    }

    // The result is checked as a whole: the version, the header and every page
    const qint64 checkTime = QDateTime::currentMSecsSinceEpoch();
    if (!error->isError())
    {
        int result = SQLITE_OK;
        const QString check = PDFDatabaseHelper::queryText(destination, "PRAGMA quick_check", &result);
        if (result != SQLITE_OK)
        {
            *error = PDFDatabaseError::fromHandle(destination, result);
        }
        else if (check != QLatin1String("ok"))
        {
            *error = PDFDatabaseError(SQLITE_CORRUPT, SQLITE_CORRUPT, check);
        }
        else if (PDFDatabaseSchema::readVersion(destination) != applicationVersion ||
                 queryIntegerOnHandle(destination, "PRAGMA application_id", 0) != APPLICATION_ID)
        {
            *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The version of the database schema is not supported."));
        }
    }

    if (error->isError())
    {
        *isSourceUnusable = !sourcePath.isEmpty() && !PDFDatabaseHelper::isEnvironmentError(error->getCode());
        closeHandle(destination);
        return false;
    }

    // The integrity check of the new file has been done just now
    recordIntegrityCheck(destination, targetPath, checkTime);

    const int closeResult = sqlite3_close(destination);
    if (closeResult != SQLITE_OK)
    {
        *error = PDFDatabaseError::fromHandle(nullptr, closeResult);
        closeHandle(destination);
        return false;
    }

    return true;
}

bool PDFDatabase::openDisk(PDFDatabaseError* error)
{
    const QString filePath = getDiskFilePath();
    if (filePath.isEmpty())
    {
        *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "No file of the database is set."));
        return false;
    }

    // A new folder is accessible only by the user (Unix); the file is 0600 as well
    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
    {
        *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The folder %1 cannot be created.").arg(QDir::toNativeSeparators(fileInfo.absolutePath())));
        return false;
    }

    if (!m_options.directory.isEmpty())
    {
        QString newerFilePath;
        if (!prepareSchemaFile(error, &newerFilePath))
        {
            return false;
        }

        if (!newerFilePath.isEmpty())
        {
            // Only newer files (an older version of the application started after
            // a newer one): the content of the newest one is copied into memory
            PDFDatabaseState state = getState();
            state.filePath = newerFilePath;
            state.fileSchemaVersion = PDFDatabaseFiles::getSchemaVersion(QFileInfo(newerFilePath).fileName());
            setState(state);
            return openMemory(newerFilePath, PDFDatabaseStorageMode::ReadOnlyNewerSchema);
        }
    }

    PDFDatabaseState state = getState();
    state.integrityChecked = false;
    sqlite3* handle = nullptr;
    qint64 checkTime = 0;

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (!QFileInfo::exists(filePath))
        {
            // The file is created with the permissions of the owner only. An empty
            // file is a valid empty database.
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner) && !QFileInfo::exists(filePath))
            {
                *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The file %1 cannot be created: %2").arg(QDir::toNativeSeparators(filePath), file.errorString()));
                return false;
            }
        }
        PDFDatabaseHelper::restrictPermissions(filePath);

        handle = PDFDatabaseHelper::openHandle(filePath.toUtf8(), SQLITE_OPEN_READWRITE, error);
        bool isDamaged = false;
        if (!handle)
        {
            isDamaged = PDFDatabaseHelper::isDamageCode(error->getCode());
        }
        else
        {
            sqlite3_busy_timeout(handle, m_options.busyTimeout);
            if (sqlite3_db_readonly(handle, "main") == 1)
            {
                *error = PDFDatabaseError(SQLITE_READONLY, SQLITE_READONLY, QCoreApplication::translate("pdf::PDFDatabase", "The database file is read-only."));
                closeHandle(handle);
                return false;
            }

            checkTime = QDateTime::currentMSecsSinceEpoch();
            isDamaged = isFileDamaged(handle, filePath, m_forceIntegrityCheck, &state.integrityChecked, error);
            if (!isDamaged && error->isError())
            {
                closeHandle(handle);
                return false;
            }
        }

        if (!isDamaged)
        {
            break;
        }

        // Damaged file (newapp_storage.md 5): renamed, a new one is created
        qCWarning(lcPdf4QtStorage) << "Database file is damaged:" << error->toString();
        closeHandle(handle);
        handle = nullptr;

        if (attempt > 0)
        {
            return false;
        }

        const QString damagedFilePath = renameDamagedFile(filePath);
        if (damagedFilePath.isEmpty())
        {
            *error = PDFDatabaseError(SQLITE_CORRUPT, SQLITE_CORRUPT, QCoreApplication::translate("pdf::PDFDatabase", "The database file is damaged and it cannot be renamed."));
            return false;
        }
        state.damagedFilePath = damagedFilePath;

        // The user must be told about the renamed file also when the opening of the new
        // file is tried again (openDiskWithRetry, another instance holds its lock): the
        // next attempt starts from the stored state (no signal, the opening goes on)
        QMutexLocker locker(&m_stateMutex);
        m_state.damagedFilePath = damagedFilePath;
    }

    if (!handle)
    {
        return false;
    }

    *error = PDFDatabaseHelper::applyPragmas(handle, false, false, m_options.busyTimeout);
    if (error->isError())
    {
        closeHandle(handle);
        return false;
    }

    const int fileVersion = PDFDatabaseSchema::readVersion(handle);
    if (fileVersion < 0)
    {
        *error = PDFDatabaseError::fromHandle(handle, sqlite3_errcode(handle));
        closeHandle(handle);
        return false;
    }

    state.fileSchemaVersion = fileVersion;
    if (fileVersion > state.applicationSchemaVersion)
    {
        // Newer version of the application wrote the file: its content is copied
        // into memory, nothing is written to the disk
        closeHandle(handle);
        setState(state);
        return openMemory(filePath, PDFDatabaseStorageMode::ReadOnlyNewerSchema);
    }

    if (fileVersion < state.applicationSchemaVersion)
    {
        *error = PDFDatabaseSchema::migrate(handle, getMigrations(), state.applicationSchemaVersion);
        if (error->isError())
        {
            closeHandle(handle);
            return false;
        }

        // A newer version of the application, started at the same time, may have
        // migrated the file before this one got the exclusive lock
        const int migratedVersion = PDFDatabaseSchema::readVersion(handle);
        if (migratedVersion > state.applicationSchemaVersion)
        {
            closeHandle(handle);
            state.fileSchemaVersion = migratedVersion;
            setState(state);
            return openMemory(filePath, PDFDatabaseStorageMode::ReadOnlyNewerSchema);
        }
    }

    if (state.integrityChecked)
    {
        // After the migration: the table of the time exists now
        recordIntegrityCheck(handle, filePath, checkTime);
    }

    closeHandle(handle);

    {
        QMutexLocker locker(&m_registry->mutex);
        m_registry->target = filePath;
        m_registry->isMemory = false;
        m_registry->isOpen = true;
    }

    m_isOpen = true;
    state.mode = PDFDatabaseStorageMode::Disk;
    state.schemaVersion = state.applicationSchemaVersion;
    state.error = PDFDatabaseError();
    setState(state);
    return true;
}

QString PDFDatabase::getMemoryUri() const
{
    // Each generation has its own memory database, so a database of a previous
    // generation, which a thread still holds, is never mixed with a new one
    QString name = m_options.memoryName;
    const quint64 generation = m_generation.load();
    if (generation > 1)
    {
        name += QStringLiteral("-g%1").arg(generation);
    }
    return QStringLiteral("file:%1?vfs=memdb").arg(QString::fromLatin1(QUrl::toPercentEncoding(name, "/")));
}

bool PDFDatabase::openMemory(const QString& copyFromFile, PDFDatabaseStorageMode mode)
{
    PDFDatabaseState state = getState();
    state.mode = mode;
    state.contentCopied = false;

    const QString uri = getMemoryUri();
    PDFDatabaseError error;
    closeHandle(m_memoryKeeper);
    m_memoryKeeper = PDFDatabaseHelper::openHandle(uri.toUtf8(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, &error);
    if (m_memoryKeeper)
    {
        error = PDFDatabaseHelper::applyPragmas(m_memoryKeeper, true, false, m_options.busyTimeout);
    }

    if (!m_memoryKeeper || error.isError())
    {
        // Not even a memory database: the storage reports errors for every operation
        qCCritical(lcPdf4QtStorage) << "Memory database cannot be opened:" << error.toString();
        closeHandle(m_memoryKeeper);
        m_memoryKeeper = nullptr;
        state.mode = PDFDatabaseStorageMode::MemoryFallback;
        if (!state.error.isError())
        {
            state.error = error;
        }
        setState(state);
        return false;
    }

    if (!copyFromFile.isEmpty() && QFileInfo::exists(copyFromFile))
    {
        // The readable content of the file is kept: the user sees the own settings
        PDFDatabaseError copyError;
        sqlite3* source = openFileConnection(copyFromFile, true, m_options.busyTimeout, &copyError);
        bool fullCheckPerformed = false;
        if (source && !isFileDamaged(source, copyFromFile, false, &fullCheckPerformed, &copyError))
        {
            const int result = PDFDatabaseHelper::copyDatabase(source, m_memoryKeeper);
            state.contentCopied = (result == SQLITE_OK);
            if (!state.contentCopied)
            {
                qCWarning(lcPdf4QtStorage) << "Database content cannot be copied into memory:" << PDFDatabaseError::fromHandle(m_memoryKeeper, result).toString();
            }
        }
        closeHandle(source);

    }

    const int memoryVersion = PDFDatabaseSchema::readVersion(m_memoryKeeper);
    if (memoryVersion < state.applicationSchemaVersion)
    {
        error = PDFDatabaseSchema::migrate(m_memoryKeeper, getMigrations(), state.applicationSchemaVersion);
        if (error.isError())
        {
            qCCritical(lcPdf4QtStorage) << "Memory database cannot be migrated:" << error.toString();
            if (!state.error.isError())
            {
                state.error = error;
            }
        }
    }
    state.schemaVersion = qMax(0, PDFDatabaseSchema::readVersion(m_memoryKeeper));

    {
        QMutexLocker locker(&m_registry->mutex);
        m_registry->target = uri;
        m_registry->isMemory = true;
        m_registry->isOpen = true;
    }

    m_isOpen = true;
    setState(state);
    return true;
}

bool PDFDatabase::retryOpen()
{
    const PDFDatabaseState currentState = getState();
    if (!currentState.isRetryPossible() || m_options.portable)
    {
        return currentState.isPersistent();
    }

    // Connections of all threads are reopened at their next use
    {
        QMutexLocker locker(&m_registry->mutex);
        m_registry->generation = ++m_generation;
    }

    PDFDatabaseState state = currentState;
    state.damagedFilePath.clear();
    state.upgradedFromFilePath.clear();
    setState(state);

    PDFDatabaseError error;
    if (openDiskWithRetry(&error))
    {
        closeHandle(m_memoryKeeper);
        m_memoryKeeper = nullptr;
        return getState().isPersistent();
    }

    // Still in memory: the memory database of the previous generation is kept
    state = getState();
    state.mode = PDFDatabaseStorageMode::MemoryFallback;
    state.error = error;
    setState(state);
    return false;
}

void PDFDatabase::close()
{
    m_damageReportTimer.stop();
    if (!m_isOpen)
    {
        // A damage reported after the database was closed belongs to the file, which
        // was open (the marker file; nothing, when it was a memory database)
        writeDamageReport(true);
        m_damageReportTime.store(0);
        closeHandle(m_memoryKeeper);
        m_memoryKeeper = nullptr;
        return;
    }

    if (getState().isPersistent())
    {
        // newapp_storage.md 2: keeps the size of the WAL small
        if (PDFDatabaseConnection* connection = getConnection())
        {
            if (connection->isValid() && !connection->isInTransaction())
            {
                connection->execute("PRAGMA optimize");
                sqlite3_wal_checkpoint_v2(connection->getHandle(), nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
            }
        }
    }

    // A damage met in this run must not be forgotten (newapp_storage.md 5), also
    // a damage met by PRAGMA optimize above
    writeDamageReport(true);

    closeConnections();
    closeHandle(m_memoryKeeper);
    m_memoryKeeper = nullptr;
    m_isOpen = false;
}

void PDFDatabase::closeConnections()
{
    std::map<Qt::HANDLE, std::unique_ptr<PDFDatabaseConnection>> connections;
    {
        QMutexLocker locker(&m_registry->mutex);
        m_registry->isOpen = false;
        m_registry->target.clear();
        connections.swap(m_registry->connections);
    }
    connections.clear();
}

bool PDFDatabase::isOpen() const
{
    QMutexLocker locker(&m_registry->mutex);
    return m_registry->isOpen;
}

PDFDatabaseState PDFDatabase::getState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_state;
}

void PDFDatabase::setState(const PDFDatabaseState& state)
{
    {
        QMutexLocker locker(&m_stateMutex);
        m_state = state;
    }

    if (QThread::currentThread() == thread())
    {
        Q_EMIT stateChanged();
    }
}

PDFDatabaseConnection* PDFDatabase::getConnection()
{
    std::shared_ptr<PDFDatabaseRegistry> registry = m_registry;
    std::unique_ptr<PDFDatabaseConnection> oldConnection;
    PDFDatabaseConnection* result = nullptr;
    bool isNewThread = false;

    {
        QMutexLocker locker(&registry->mutex);
        if (!registry->isOpen)
        {
            return nullptr;
        }

        const Qt::HANDLE thread = QThread::currentThreadId();
        auto it = registry->connections.find(thread);
        if (it != registry->connections.end() && it->second->getGeneration() == registry->generation)
        {
            return it->second.get();
        }

        std::unique_ptr<PDFDatabaseConnection> connection(new PDFDatabaseConnection(this, registry->generation));
        registry->openConnection(connection.get());
        result = connection.get();

        if (it != registry->connections.end())
        {
            oldConnection = std::move(it->second);
            it->second = std::move(connection);
        }
        else
        {
            registry->connections.emplace(thread, std::move(connection));
            isNewThread = true;
        }
    }

    if (isNewThread)
    {
        // The connection is closed, when the thread ends
        QThreadStorage<PDFDatabaseThreadData*>& storage = PDFDatabaseHelper::getThreadStorage();
        if (!storage.hasLocalData())
        {
            storage.setLocalData(new PDFDatabaseThreadData());
        }
        storage.localData()->addRegistry(registry);
    }

    return result;
}

int PDFDatabase::getConnectionCount() const
{
    QMutexLocker locker(&m_registry->mutex);
    return int(m_registry->connections.size());
}

PDFDatabaseCheckpointResult PDFDatabase::checkpoint(bool passive)
{
    PDFDatabaseCheckpointResult result;
    PDFDatabaseConnection* connection = getConnection();
    if (!connection || !connection->isValid())
    {
        result.error = connection ? connection->getOpenError() : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
        return result;
    }

    int logFrames = -1;
    int checkpointedFrames = -1;
    const int code = sqlite3_wal_checkpoint_v2(connection->getHandle(), nullptr, passive ? SQLITE_CHECKPOINT_PASSIVE : SQLITE_CHECKPOINT_FULL, &logFrames, &checkpointedFrames);
    result.logFrames = logFrames;
    result.checkpointedFrames = checkpointedFrames;
    if (code == SQLITE_BUSY || code == SQLITE_LOCKED)
    {
        result.busy = true;
    }
    else if (code != SQLITE_OK)
    {
        result.error = PDFDatabaseError::fromHandle(connection->getHandle(), code);
    }

    if (logFrames >= 0 && checkpointedFrames < logFrames)
    {
        result.busy = true;
    }

    return result;
}

void PDFDatabase::notifyCommitted()
{
    if (!m_commitPending.exchange(true))
    {
        QMetaObject::invokeMethod(this, &PDFDatabase::processCommitted, Qt::QueuedConnection);
    }
}

void PDFDatabase::processCommitted()
{
    m_commitPending.store(false);
    Q_EMIT committed();
}

sqlite3* PDFDatabase::openTargetHandle(int busyTimeout, PDFDatabaseError* error) const
{
    // A connection of its own to the open database (the file or the memory database
    // of the generation), with the pragmas of the application
    QString target;
    bool isMemory = false;
    {
        QMutexLocker locker(&m_registry->mutex);
        if (!m_registry->isOpen)
        {
            *error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
            return nullptr;
        }
        target = m_registry->target;
        isMemory = m_registry->isMemory;
    }

    const int flags = isMemory ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI) : SQLITE_OPEN_READWRITE;
    sqlite3* handle = PDFDatabaseHelper::openHandle(target.toUtf8(), flags, error);
    if (!handle)
    {
        return nullptr;
    }

    *error = PDFDatabaseHelper::applyPragmas(handle, isMemory, false, busyTimeout);
    if (error->isError())
    {
        closeHandle(handle);
        return nullptr;
    }
    return handle;
}

PDFDatabaseError PDFDatabase::exportBackup(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return PDFDatabaseError::fromMessage(tr("No file of the backup is set."));
    }

    PDFDatabaseError error;
    sqlite3* source = openTargetHandle(BACKUP_BUSY_TIMEOUT, &error);
    if (!source)
    {
        return error;
    }

    // A consistent snapshot in memory (one read transaction of the source)
    sqlite3* copy = PDFDatabaseHelper::openEmptyMemory(&error);
    if (!copy)
    {
        closeHandle(source);
        return error;
    }

    int result = PDFDatabaseHelper::backupDatabase(source, copy);
    error = PDFDatabaseError::fromHandle(result == SQLITE_OK ? nullptr : source, result);
    closeHandle(source);

    // The state of the running instances is not a part of the backup; the time of
    // the export is (the import dialog shows it). VACUUM removes the deleted rows
    // from the file.
    if (!error.isError())
    {
        result = PDFDatabaseHelper::clearRuntimeState(copy);
        if (result == SQLITE_OK)
        {
            result = PDFDatabaseHelper::writeMaintenanceValue(copy, PDFDatabaseBackup::CREATED_KEY, QDateTime::currentMSecsSinceEpoch());
        }
        if (result == SQLITE_OK)
        {
            result = PDFDatabaseHelper::execute(copy, "VACUUM");
        }
        error = PDFDatabaseError::fromHandle(copy, result);
    }

    QByteArray data;
    if (!error.isError())
    {
        sqlite3_int64 size = 0;
        unsigned char* buffer = sqlite3_serialize(copy, "main", &size, 0);
        if (!buffer)
        {
            error = PDFDatabaseError::fromHandle(nullptr, SQLITE_NOMEM);
        }
        else
        {
            data = QByteArray(reinterpret_cast<const char*>(buffer), qsizetype(size));
            sqlite3_free(buffer);

            // The file has a rollback journal (a reader needs no -wal / -shm files)
            if (data.size() >= 100)
            {
                data[18] = 1;
                data[19] = 1;
            }
        }
    }
    closeHandle(copy);

    if (error.isError())
    {
        qCWarning(lcPdf4QtStorage) << "Settings cannot be exported:" << error.toString();
        return error;
    }

    // A temporary file of the target folder renamed to the target: never a
    // half-written backup; only the owner may read it (it holds the paths of the
    // documents of the user)
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return PDFDatabaseError(SQLITE_CANTOPEN, SQLITE_CANTOPEN, tr("The file %1 cannot be written: %2").arg(QDir::toNativeSeparators(filePath), file.errorString()));
    }
#if defined(Q_OS_UNIX)
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    if (file.write(data) != data.size() || !file.commit())
    {
        const QString message = file.errorString();
        file.cancelWriting();
        return PDFDatabaseError(SQLITE_IOERR, SQLITE_IOERR, tr("The file %1 cannot be written: %2").arg(QDir::toNativeSeparators(filePath), message));
    }
    PDFDatabaseHelper::restrictPermissions(filePath);

    qCInfo(lcPdf4QtStorage) << "Settings exported to" << filePath;
    return PDFDatabaseError();
}

PDFDatabaseError PDFDatabase::importBackup(const PDFDatabaseBackup& backup, PDFDatabaseImportCertificates certificates)
{
    const PDFDatabaseState state = getState();
    if (state.mode == PDFDatabaseStorageMode::ReadOnlyNewerSchema)
    {
        return PDFDatabaseError::fromMessage(tr("The settings were saved by a newer version of PDF4QT, a backup cannot replace them."));
    }

    // The content is checked again (the caller may pass any object)
    const std::vector<PDFDatabaseMigration>& migrations = getMigrations();
    const int applicationVersion = PDFDatabaseSchema::getVersion(migrations);
    const PDFDatabaseBackup checked = PDFDatabaseBackup::fromData(backup.getData(), backup.getFilePath(), migrations);
    if (!checked.isValid())
    {
        return PDFDatabaseError::fromMessage(checked.getErrorText());
    }

    // A copy in memory migrated to the schema of the application
    PDFDatabaseError error;
    sqlite3* copy = PDFDatabaseHelper::openMemoryCopy(checked.getData(), false, &error);
    if (!copy)
    {
        return error;
    }

    error = PDFDatabaseSchema::migrate(copy, migrations, applicationVersion);
    if (!error.isError() && PDFDatabaseSchema::readVersion(copy) != applicationVersion)
    {
        error = PDFDatabaseError::fromMessage(tr("The version of the database schema is not supported."));
    }

    sqlite3* target = nullptr;
    if (!error.isError())
    {
        target = openTargetHandle(BACKUP_BUSY_TIMEOUT, &error);
    }

    // The state of the running instances is not taken from the backup; the page size
    // of the copy is changed (VACUUM) before the state of the target is read
    if (!error.isError())
    {
        int result = PDFDatabaseHelper::clearRuntimeState(copy);
        if (result == SQLITE_OK)
        {
            result = PDFDatabaseHelper::alignPageSize(copy, target);
        }
        error = PDFDatabaseError::fromHandle(copy, result);
    }

    // One step under the lock of the upgrades: the backup API replaces the content
    // in one write transaction of the target
    sqlite3* lock = nullptr;
    if (!error.isError() && state.mode == PDFDatabaseStorageMode::Disk && !m_options.directory.isEmpty())
    {
        lock = PDFDatabaseHelper::lockUpgrades(m_options.directory, BACKUP_BUSY_TIMEOUT, &error);
    }

    // The state of the running instances stays (the markers of the thumbnails, the
    // integrity check, the MCP server), the change log is empty and its sequence
    // jumps: every notifier finds a sequence it has not read and rereads everything.
    // It is read as late as possible - under the lock, right before the step (no
    // transaction of the target may stay open across sqlite3_backup_init): a row,
    // which another instance writes after this read, is replaced by its older copy
    // (the wait for the lock and VACUUM are not inside of that window).
    if (!error.isError())
    {
        // The trusted certificates of the open database stay, unless the user has
        // chosen to trust the certificates of the backup (owner 2026-10-25, Q7)
        std::vector<const char*> keptTables = PDFDatabaseHelper::getRuntimeTables();
        if (certificates == PDFDatabaseImportCertificates::KeepCurrent)
        {
            keptTables.push_back("trusted_certificate");
        }

        int result = SQLITE_OK;
        for (const char* table : keptTables)
        {
            if (result == SQLITE_OK)
            {
                result = PDFDatabaseHelper::copyTableRows(target, copy, table);
            }
        }
        if (result == SQLITE_OK)
        {
            result = PDFDatabaseHelper::execute(copy, "DELETE FROM change_log");
        }
        if (result == SQLITE_OK)
        {
            result = PDFDatabaseHelper::normalizeSequences(copy);
        }

        const qint64 sequence = PDFDatabaseHelper::readChangeLogSequence(target);
        if (result == SQLITE_OK && sequence < 0)
        {
            result = SQLITE_ERROR;
        }
        if (result == SQLITE_OK)
        {
            result = PDFDatabaseHelper::writeChangeLogSequence(copy, sequence + IMPORT_SEQUENCE_JUMP);
        }
        error = PDFDatabaseError::fromHandle(copy, result);
    }

    if (!error.isError())
    {
        const int result = PDFDatabaseHelper::backupDatabase(copy, target);
        error = PDFDatabaseError::fromHandle(result == SQLITE_OK ? nullptr : target, result);
    }

    PDFDatabaseHelper::unlockUpgrades(lock);
    closeHandle(target);
    closeHandle(copy);

    if (error.isError())
    {
        qCWarning(lcPdf4QtStorage) << "Settings cannot be imported:" << error.toString();
        if (error.isBusy())
        {
            return PDFDatabaseError(SQLITE_BUSY, error.getExtendedCode(), tr("Another PDF4QT is changing the settings right now. Try again in a moment."));
        }
        return error;
    }

    qCInfo(lcPdf4QtStorage) << "Settings imported from" << backup.getFilePath();

    // The own notifier reads the new content at once (other connections see the
    // change of PRAGMA data_version)
    notifyCommitted();
    return PDFDatabaseError();
}

PDFDatabaseError PDFDatabase::deleteOlderDatabaseFile(const QString& filePath)
{
    if (m_options.portable || m_options.directory.isEmpty())
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabaseFiles", "The file is not a database file of PDF4QT."));
    }

    // The file, which is open (also a newer file copied into memory), is never deleted
    const PDFDatabaseState state = getState();
    const QString path = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    if (!state.filePath.isEmpty() && QDir::cleanPath(QFileInfo(state.filePath).absoluteFilePath()) == path)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabaseFiles", "The file is the database of this PDF4QT."));
    }

    return PDFDatabaseFiles::deleteOlderFile(m_options.directory, path, PDFDatabaseSchema::getVersion(getMigrations()));
}

}   // namespace pdf
