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

#ifndef PDFDATABASESCHEMA_H
#define PDFDATABASESCHEMA_H

#include "pdfappglobal.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

struct sqlite3;

namespace pdf
{

class PDFDatabaseError;

/// One forward and additive step of the schema (newapp_storage.md 5). The SQL of
/// a released version is never changed, a change of the schema is a new migration
/// (a new schema version, whose database file is pdf4qt-<version>.db - O19).
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseMigration
{
public:
    /// Version of the schema after the migration (PRAGMA user_version), from 1
    int version = 0;

    /// Statements of the migration (separated by semicolons)
    QByteArray sql;
};

/// Description of a table of the schema for the tests and the notifications
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseTableInfo
{
public:
    /// Name of the table = topic of the change notifications (change_log.topic)
    QString name;

    /// SQL expression of the key of a changed row (change_log.key), with the
    /// placeholder %1 for the row (NEW / OLD)
    QString keyExpression;
};

/// Schema of the database of the application and its migrations
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseSchema
{
public:
    PDFDatabaseSchema() = delete;

    /// Migrations of the application, ordered by the version
    static std::vector<PDFDatabaseMigration> getMigrations();

    /// Version of the schema of the application
    static int getCurrentVersion();

    /// Version, which the migrations produce (the highest one), 0 for an empty list
    static int getVersion(const std::vector<PDFDatabaseMigration>& migrations);

    /// The migrations are ordered and contiguous from version 1
    static bool isValid(const std::vector<PDFDatabaseMigration>& migrations);

    /// Tables of the phase 1 schema, which have the triggers of the change log
    /// (every table except change_log itself)
    static std::vector<PDFDatabaseTableInfo> getTables();

    /// Migrates the database of the connection to \p targetVersion inside of one
    /// BEGIN EXCLUSIVE transaction, so only one instance migrates; the version is
    /// read again inside of the transaction. Sets PRAGMA user_version and
    /// application_id. A database with a newer or equal version is not changed.
    /// Tests use it to create databases of older versions.
    static PDFDatabaseError migrate(sqlite3* handle, const std::vector<PDFDatabaseMigration>& migrations, int targetVersion);

    /// Reads PRAGMA user_version (-1 on an error)
    static int readVersion(sqlite3* handle);

    /// Name of the table of the change log (it has no triggers)
    static constexpr const char* CHANGE_LOG_TABLE = "change_log";

    /// Internal state of the storage (key, value). It has no triggers:
    /// its changes are no notifications and Restore defaults never deletes it.
    static constexpr const char* MAINTENANCE_TABLE = "storage_maintenance";

    /// Key of MAINTENANCE_TABLE: time (ms since the epoch) of the start of the last
    /// successful full integrity check
    static constexpr const char* INTEGRITY_LAST_CHECK_KEY = "integrity.lastCheck";

    /// Key of MAINTENANCE_TABLE: time of the last damage (SQLITE_CORRUPT / SQLITE_NOTADB)
    /// met by an instance; a request of the full check, when it is not older than the check
    static constexpr const char* INTEGRITY_REQUESTED_KEY = "integrity.requested";

    /// Markers of the thumbnails being rendered (PDFRecentRepository::beginRendering).
    /// It has no triggers: a marker is no notification.
    static constexpr const char* THUMBNAIL_RENDERING_TABLE = "thumbnail_rendering";

private:
    static QByteArray createVersion1();
    static QByteArray createTriggers(const PDFDatabaseTableInfo& table, const char* updateColumns);
};

}   // namespace pdf

#endif // PDFDATABASESCHEMA_H
