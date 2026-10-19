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

#include "pdfdatabaseschema.h"
#include "pdfdatabase.h"

#include <QCoreApplication>

#include <sqlite3.h>

namespace pdf
{

/// Time of a change in milliseconds since the epoch, computed by SQLite
#define PDF4QT_SQL_NOW_MS "CAST((julianday('now') - 2440587.5) * 86400000.0 AS INTEGER)"

std::vector<PDFDatabaseMigration> PDFDatabaseSchema::getMigrations()
{
    std::vector<PDFDatabaseMigration> migrations;

    PDFDatabaseMigration version1;
    version1.version = 1;
    version1.sql = createVersion1();
    migrations.push_back(std::move(version1));

    PDFDatabaseMigration version2;
    version2.version = 2;
    version2.sql = createVersion2();
    migrations.push_back(std::move(version2));

    return migrations;
}

int PDFDatabaseSchema::getCurrentVersion()
{
    return getVersion(getMigrations());
}

int PDFDatabaseSchema::getVersion(const std::vector<PDFDatabaseMigration>& migrations)
{
    int version = 0;
    for (const PDFDatabaseMigration& migration : migrations)
    {
        version = qMax(version, migration.version);
    }
    return version;
}

bool PDFDatabaseSchema::isValid(const std::vector<PDFDatabaseMigration>& migrations)
{
    for (size_t i = 0; i < migrations.size(); ++i)
    {
        if (migrations[i].version != int(i + 1) || migrations[i].sql.isEmpty())
        {
            return false;
        }
    }
    return true;
}

std::vector<PDFDatabaseTableInfo> PDFDatabaseSchema::getTables()
{
    static const char* const tables[][2] =
    {
        { "setting", "%1.key" },
        { "recent_file", "%1.path" },
        { "recent_folder", "%1.path" },
        { "pinned_location", "CAST(%1.id AS TEXT)" },
        { "workspace", "CAST(%1.id AS TEXT)" },
        { "workspace_document", "CAST(%1.workspace_id AS TEXT)" },
        { "tool_pin", "%1.tool_id" },
        { "tool_usage", "%1.tool_id" },
        { "tool_collection", "CAST(%1.id AS TEXT)" },
        { "tool_collection_item", "CAST(%1.collection_id AS TEXT)" },
        { "tool_pair", "%1.from_tool" },
        { "tool_parameter", "%1.tool_id" },
        { "shortcut", "%1.command_id" },
        { "annotation_style", "%1.style_id" },
        { "bookmark", "%1.file_path" },
        { "trusted_certificate", "CAST(%1.id AS TEXT)" },
        { "mcp_server", "CAST(%1.id AS TEXT)" },
        { "mcp_client", "%1.session" },
        { "mcp_activity", "CAST(%1.id AS TEXT)" },
        { "mcp_request", "CAST(%1.id AS TEXT)" },
    };

    std::vector<PDFDatabaseTableInfo> result;
    for (const auto& table : tables)
    {
        PDFDatabaseTableInfo info;
        info.name = QString::fromLatin1(table[0]);
        info.keyExpression = QString::fromLatin1(table[1]);
        result.push_back(std::move(info));
    }
    return result;
}

QByteArray PDFDatabaseSchema::createTriggers(const PDFDatabaseTableInfo& table, const char* updateColumns)
{
    // Every change of a row writes a row of the change log in the same transaction,
    // so no write can forget to notify the other instances (newapp_storage.md 3)
    const QByteArray name = table.name.toLatin1();
    const QByteArray newKey = table.keyExpression.arg(QLatin1String("NEW")).toLatin1();
    const QByteArray oldKey = table.keyExpression.arg(QLatin1String("OLD")).toLatin1();
    const QByteArray insertPrefix = "INSERT INTO change_log(topic, key, time) VALUES ('" + name + "', ";
    const QByteArray now = PDF4QT_SQL_NOW_MS;

    QByteArray sql;
    sql += "CREATE TRIGGER " + name + "_log_insert AFTER INSERT ON " + name + " BEGIN " +
           insertPrefix + newKey + ", " + now + "); END;\n";

    const QByteArray updateOf = updateColumns ? QByteArray(" OF ") + updateColumns : QByteArray();
    sql += "CREATE TRIGGER " + name + "_log_update AFTER UPDATE" + updateOf + " ON " + name + " BEGIN " +
           insertPrefix + newKey + ", " + now + "); " +
           "INSERT INTO change_log(topic, key, time) SELECT '" + name + "', " + oldKey + ", " + now +
           " WHERE (" + oldKey + ") IS NOT (" + newKey + "); END;\n";

    sql += "CREATE TRIGGER " + name + "_log_delete AFTER DELETE ON " + name + " BEGIN " +
           insertPrefix + oldKey + ", " + now + "); END;\n";

    return sql;
}

QByteArray PDFDatabaseSchema::createVersion1()
{
    // Schema of phase 1 (newapp_storage.md 4). Times are milliseconds since the epoch (UTC).
    QByteArray sql = R"SQL(
CREATE TABLE change_log(
    seq INTEGER PRIMARY KEY AUTOINCREMENT,
    topic TEXT NOT NULL,
    key TEXT,
    time INTEGER NOT NULL);

CREATE TABLE setting(
    key TEXT PRIMARY KEY NOT NULL,
    value,
    modified INTEGER NOT NULL);

CREATE TABLE recent_file(
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    folder TEXT NOT NULL,
    last_opened INTEGER NOT NULL,
    open_count INTEGER NOT NULL DEFAULT 1,
    pinned INTEGER NOT NULL DEFAULT 0,
    pin_order INTEGER,
    page_count INTEGER,
    file_size INTEGER,
    file_mtime INTEGER,
    last_page INTEGER,
    reached_end INTEGER NOT NULL DEFAULT 0,
    view_state TEXT,
    locked INTEGER NOT NULL DEFAULT 0,
    thumbnail BLOB,
    thumbnail_mtime INTEGER);
CREATE INDEX recent_file_last_opened ON recent_file(last_opened);

CREATE TABLE recent_folder(
    path TEXT PRIMARY KEY NOT NULL,
    last_used INTEGER NOT NULL,
    kind INTEGER NOT NULL DEFAULT 0);

CREATE TABLE pinned_location(
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL,
    name TEXT NOT NULL,
    kind INTEGER NOT NULL DEFAULT 0,
    position INTEGER NOT NULL DEFAULT 0);

CREATE TABLE workspace(
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    created INTEGER NOT NULL,
    opened INTEGER,
    saved INTEGER,
    is_last_session INTEGER NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX workspace_name ON workspace(name COLLATE NOCASE) WHERE is_last_session = 0;

CREATE TABLE workspace_document(
    workspace_id INTEGER NOT NULL REFERENCES workspace(id) ON DELETE CASCADE,
    position INTEGER NOT NULL,
    path TEXT NOT NULL,
    page INTEGER,
    PRIMARY KEY(workspace_id, position));

CREATE TABLE tool_pin(
    tool_id TEXT PRIMARY KEY NOT NULL,
    position INTEGER NOT NULL DEFAULT 0,
    pinned INTEGER NOT NULL);

CREATE TABLE tool_usage(
    tool_id TEXT PRIMARY KEY NOT NULL,
    last_used INTEGER NOT NULL,
    count INTEGER NOT NULL DEFAULT 0);

CREATE TABLE tool_collection(
    id INTEGER PRIMARY KEY,
    builtin_id TEXT UNIQUE,
    name TEXT NOT NULL,
    icon TEXT,
    modified INTEGER NOT NULL);

CREATE TABLE tool_collection_item(
    collection_id INTEGER NOT NULL REFERENCES tool_collection(id) ON DELETE CASCADE,
    tool_id TEXT NOT NULL,
    position INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(collection_id, tool_id));

CREATE TABLE tool_pair(
    from_tool TEXT NOT NULL,
    to_tool TEXT NOT NULL,
    count INTEGER NOT NULL DEFAULT 0,
    last_used INTEGER NOT NULL,
    PRIMARY KEY(from_tool, to_tool));

CREATE TABLE tool_parameter(
    tool_id TEXT NOT NULL,
    name TEXT NOT NULL,
    value,
    PRIMARY KEY(tool_id, name));

CREATE TABLE shortcut(
    command_id TEXT PRIMARY KEY NOT NULL,
    keys TEXT NOT NULL);

CREATE TABLE annotation_style(
    style_id TEXT PRIMARY KEY NOT NULL,
    value);

CREATE TABLE bookmark(
    id INTEGER PRIMARY KEY,
    file_path TEXT NOT NULL,
    page INTEGER NOT NULL,
    title TEXT NOT NULL,
    auto INTEGER NOT NULL DEFAULT 0,
    position INTEGER NOT NULL DEFAULT 0);
CREATE INDEX bookmark_file_path ON bookmark(file_path);

CREATE TABLE trusted_certificate(
    id INTEGER PRIMARY KEY,
    data BLOB NOT NULL,
    added INTEGER NOT NULL);

CREATE TABLE mcp_server(
    id INTEGER PRIMARY KEY CHECK(id = 1),
    instance TEXT,
    pid INTEGER,
    port INTEGER,
    state TEXT,
    address TEXT,
    heartbeat INTEGER);

CREATE TABLE mcp_client(
    session TEXT PRIMARY KEY NOT NULL,
    name TEXT,
    address TEXT,
    connected INTEGER);

CREATE TABLE mcp_activity(
    id INTEGER PRIMARY KEY,
    time INTEGER NOT NULL,
    client TEXT,
    action TEXT,
    document TEXT,
    detail TEXT,
    status TEXT NOT NULL);
CREATE INDEX mcp_activity_time ON mcp_activity(time);

CREATE TABLE mcp_request(
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL,
    session TEXT,
    created INTEGER NOT NULL);
)SQL";

    for (const PDFDatabaseTableInfo& table : getTables())
    {
        // The heartbeat of the MCP server is written every 2 s; it alone is not
        // a change (staleness is checked by time), so it is not logged
        const char* updateColumns = (table.name == QLatin1String("mcp_server")) ? "id, instance, pid, port, state, address" : nullptr;
        sql += createTriggers(table, updateColumns);
    }

    return sql;
}

QByteArray PDFDatabaseSchema::createVersion2()
{
    // Internal state of the storage: time of the last full integrity check and the
    // damage reported by an instance (PDFDatabase::reportError). It has no triggers,
    // so its writes wake no other instance, and it is not a part of the settings.
    return R"SQL(
CREATE TABLE storage_maintenance(
    key TEXT PRIMARY KEY NOT NULL,
    value INTEGER) WITHOUT ROWID;
)SQL";
}

int PDFDatabaseSchema::readVersion(sqlite3* handle)
{
    if (!handle)
    {
        return -1;
    }

    sqlite3_stmt* statement = nullptr;
    int version = -1;
    if (sqlite3_prepare_v2(handle, "PRAGMA user_version", -1, &statement, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(statement) == SQLITE_ROW)
        {
            version = sqlite3_column_int(statement, 0);
        }
    }
    sqlite3_finalize(statement);
    return version;
}

PDFDatabaseError PDFDatabaseSchema::migrate(sqlite3* handle, const std::vector<PDFDatabaseMigration>& migrations, int targetVersion)
{
    if (!handle)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabaseSchema", "The database is not open."));
    }

    if (!isValid(migrations) || targetVersion < 0 || targetVersion > getVersion(migrations))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabaseSchema", "Invalid migrations of the database schema."));
    }

    // BEGIN EXCLUSIVE: only one instance migrates. SQLITE_BUSY after the busy timeout
    // is tried once more (newapp_storage.md 2).
    int result = sqlite3_exec(handle, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr);
    if (result == SQLITE_BUSY)
    {
        result = sqlite3_exec(handle, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr);
    }
    if (result != SQLITE_OK)
    {
        return PDFDatabaseError::fromHandle(handle, result);
    }

    // Read the version again: another instance may have migrated the database meanwhile
    const int currentVersion = readVersion(handle);
    if (currentVersion < 0)
    {
        PDFDatabaseError error = PDFDatabaseError::fromHandle(handle, sqlite3_errcode(handle));
        sqlite3_exec(handle, "ROLLBACK", nullptr, nullptr, nullptr);
        return error.isError() ? error : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabaseSchema", "The version of the database schema cannot be read."));
    }

    if (currentVersion >= targetVersion)
    {
        sqlite3_exec(handle, "ROLLBACK", nullptr, nullptr, nullptr);
        return PDFDatabaseError();
    }

    for (const PDFDatabaseMigration& migration : migrations)
    {
        if (migration.version <= currentVersion || migration.version > targetVersion)
        {
            continue;
        }

        result = sqlite3_exec(handle, migration.sql.constData(), nullptr, nullptr, nullptr);
        if (result != SQLITE_OK)
        {
            PDFDatabaseError error = PDFDatabaseError::fromHandle(handle, result);
            sqlite3_exec(handle, "ROLLBACK", nullptr, nullptr, nullptr);
            return error;
        }
    }

    const QByteArray pragmas = QByteArray("PRAGMA user_version = ") + QByteArray::number(targetVersion) +
                               "; PRAGMA application_id = " + QByteArray::number(PDFDatabase::APPLICATION_ID) + ";";
    result = sqlite3_exec(handle, pragmas.constData(), nullptr, nullptr, nullptr);
    if (result == SQLITE_OK)
    {
        result = sqlite3_exec(handle, "COMMIT", nullptr, nullptr, nullptr);
    }

    if (result != SQLITE_OK)
    {
        PDFDatabaseError error = PDFDatabaseError::fromHandle(handle, result);
        sqlite3_exec(handle, "ROLLBACK", nullptr, nullptr, nullptr);
        return error;
    }

    return PDFDatabaseError();
}

}   // namespace pdf
