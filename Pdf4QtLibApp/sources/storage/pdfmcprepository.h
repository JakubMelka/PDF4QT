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

#ifndef PDFMCPREPOSITORY_H
#define PDFMCPREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{

/// State of the MCP server of the row mcp_server (unknown = Stopped)
enum class PDFMcpServerRowState
{
    Stopped,
    Starting,
    Running,
    Stopping
};

/// Row mcp_server (id = 1): the instance, which owns the port (plan 2.6)
class PDF4QTLIBAPPSHARED_EXPORT PDFMcpServerRow
{
public:
    bool exists = false;            ///< The row exists
    QString instance;               ///< Id of the owning instance (random per process)
    qint64 pid = 0;
    int port = 0;
    PDFMcpServerRowState state = PDFMcpServerRowState::Stopped;
    QString address;                ///< Shown address "http://127.0.0.1:8765/mcp"
    qint64 heartbeat = 0;           ///< Milliseconds since the epoch

    /// The owner is alive: the state is not Stopped and the heartbeat is not
    /// older than \p staleTime
    bool isAlive(qint64 now, qint64 staleTime) const;
};

/// Row mcp_client: a connected assistant (plan 6). The key is the non-secret
/// assistant key ("a:…" / "s:…"), never a raw token or session id.
class PDF4QTLIBAPPSHARED_EXPORT PDFMcpClientRow
{
public:
    QString key;
    QString name;           ///< Display name (sanitized)
    QString address;        ///< Remote address (sanitized)
    qint64 connected = 0;   ///< Connected at (ms since the epoch)
};

/// Status of an activity row (plan 6; an unknown value is Failed)
enum class PDFMcpActivityStatus
{
    Ok,
    Waiting,
    Allowed,
    DeniedUser,
    DeniedTimeout,
    DeniedClosed,
    DeniedLocked,
    DeniedBusy,
    DeniedDialog,
    DeniedDisconnected,
    DeniedStopped,
    Canceled,
    Failed,
    DeniedPermission    ///< The permission of the action is set to Deny (coalesced within 60 s; appended - the names are stored by their index)
};

/// Row mcp_activity
class PDF4QTLIBAPPSHARED_EXPORT PDFMcpActivityRow
{
public:
    qint64 id = 0;
    qint64 time = 0;                ///< ms since the epoch
    QString client;                 ///< Display name of the assistant
    QString action;                 ///< Action id ("annotations.add")
    QString document;               ///< Title of the document (may be empty)
    QJsonObject detail;             ///< { count, kind, pages, address, callId, reason } (at most 4 KiB serialized)
    PDFMcpActivityStatus status = PDFMcpActivityStatus::Ok;
};

/// Kind of a row of mcp_request (plan 2.6)
enum class PDFMcpRequestKind
{
    Disconnect,     ///< Disconnect the assistant of the session column
    DisconnectAll,
    ShowWindow
};

/// Row mcp_request: a request of another instance to the owner of the server
class PDF4QTLIBAPPSHARED_EXPORT PDFMcpRequestRow
{
public:
    qint64 id = 0;
    PDFMcpRequestKind kind = PDFMcpRequestKind::DisconnectAll;
    QString key;            ///< Assistant key of Disconnect
    qint64 created = 0;
};

/// Storage of the MCP server (tables mcp_server, mcp_client, mcp_activity,
/// mcp_request of the schema 1 - no schema change in step 7, plan 6). Rows are
/// untrusted (another version, another process, a hostile writer): every read
/// is bounded by LIMIT and every value is checked and sanitized; an unknown
/// state is Stopped, an unknown status Failed, an unknown request kind is
/// skipped. A value object: reads go to the database, writes are short BEGIN
/// IMMEDIATE transactions; the heartbeat alone is not logged by the triggers.
/// Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFMcpRepository
{
public:
    static QString getServerTopic() { return QStringLiteral("mcp_server"); }
    static QString getClientTopic() { return QStringLiteral("mcp_client"); }
    static QString getActivityTopic() { return QStringLiteral("mcp_activity"); }
    static QString getRequestTopic() { return QStringLiteral("mcp_request"); }

    /// Interval of the heartbeat of the owner and the age of a stale one (ms)
    static constexpr qint64 HEARTBEAT_INTERVAL = 2000;
    static constexpr qint64 STALE_TIME = 10000;

    /// Activity kept: the newest rows, not older than the age
    static constexpr int MAXIMUM_ACTIVITY_ROWS = 500;
    static constexpr qint64 MAXIMUM_ACTIVITY_AGE = qint64(30) * 24 * 3600 * 1000;

    /// Largest serialized detail of an activity row
    static constexpr int MAXIMUM_DETAIL_SIZE = 4096;

    /// Requests older than this are ignored and deleted
    static constexpr qint64 REQUEST_LIFETIME = 60000;

    /// Most client rows and request rows, which are read
    static constexpr int MAXIMUM_CLIENTS = 256;
    static constexpr int MAXIMUM_REQUESTS = 256;

    /// Longest texts of the rows
    static constexpr int MAXIMUM_NAME_LENGTH = 64;
    static constexpr int MAXIMUM_ADDRESS_LENGTH = 256;
    static constexpr int MAXIMUM_ACTION_LENGTH = 64;
    static constexpr int MAXIMUM_DOCUMENT_LENGTH = 256;
    static constexpr int MAXIMUM_INSTANCE_LENGTH = 64;

    explicit PDFMcpRepository(PDFDatabase* database);

    /// Fixed time of the writes and of the staleness (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }
    qint64 getCurrentTime() const;

    // Server row

    PDFMcpServerRow getServer() const;

    /// Claims the server row for the instance in one transaction: it succeeds,
    /// when the row is missing, stopped, owned by the instance, or its owner is
    /// gone (a heartbeat older than STALE_TIME, or a process, which does not run).
    /// \p claimed is false, when another live instance owns it.
    PDFDatabaseError claimServer(const QString& instance, qint64 pid, int port, PDFMcpServerRowState state, const QString& address, bool* claimed);

    /// Writes the heartbeat of the owner (only when the instance owns the row;
    /// \p isOwner false otherwise)
    PDFDatabaseError heartbeat(const QString& instance, bool* isOwner = nullptr);

    /// Changes the state, the port and the address of the owned row
    PDFDatabaseError setServerState(const QString& instance, PDFMcpServerRowState state, int port, const QString& address);

    /// Releases the owned row (state stopped) and deletes the client rows (step 8 of the quit order)
    PDFDatabaseError releaseServer(const QString& instance);

    // Clients

    std::vector<PDFMcpClientRow> getClients() const;
    PDFDatabaseError upsertClient(const PDFMcpClientRow& client);
    PDFDatabaseError removeClient(const QString& key);
    PDFDatabaseError removeClients(const QStringList& keys);

    /// Deletes every client row (a stale owner, the stop of the server)
    PDFDatabaseError clearClients();

    // Activity

    /// Adds the rows in one transaction (their ids in \p ids) and trims the table
    /// to MAXIMUM_ACTIVITY_ROWS / MAXIMUM_ACTIVITY_AGE
    PDFDatabaseError addActivity(const std::vector<PDFMcpActivityRow>& rows, std::vector<qint64>* ids = nullptr);

    /// Changes the status (and the detail, when given) of a row. \p isUpdated
    /// says, whether the row still exists (Clear or the trim may have deleted it).
    PDFDatabaseError updateActivity(qint64 id, PDFMcpActivityStatus status, const std::optional<QJsonObject>& detail = std::nullopt, bool* isUpdated = nullptr);

    /// The newest rows first
    std::vector<PDFMcpActivityRow> getActivity(int limit) const;

    /// Clear of the page: deletes every finished row (status other than waiting)
    PDFDatabaseError clearActivity();

    // Requests to the owner

    PDFDatabaseError postRequest(PDFMcpRequestKind kind, const QString& key = QString());

    /// Reads and deletes the requests in one transaction; rows older than
    /// REQUEST_LIFETIME and hostile rows are deleted and not returned
    std::vector<PDFMcpRequestRow> takeRequests();

    // Names

    static QString getStateName(PDFMcpServerRowState state);
    static PDFMcpServerRowState getStateFromName(const QString& name);
    static QString getStatusName(PDFMcpActivityStatus status);
    static PDFMcpActivityStatus getStatusFromName(const QString& name);
    static QString getRequestKindName(PDFMcpRequestKind kind);
    static std::optional<PDFMcpRequestKind> getRequestKindFromName(const QString& name);

    /// Detail of an activity row bounded to MAXIMUM_DETAIL_SIZE (members are
    /// dropped from the end until it fits; strings longer than 256 characters
    /// are shortened)
    static QJsonObject boundDetail(const QJsonObject& detail);

    /// Action id is valid: [a-z0-9_.-], 1 to MAXIMUM_ACTION_LENGTH characters
    static bool isValidAction(const QString& action);

private:
    PDFDatabaseConnection* getConnection() const;

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFMCPREPOSITORY_H
