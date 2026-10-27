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

#include "pdfmcprepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdfappsysteminfo.h"

#include "pdfmcpassistants.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>

#include <limits>

namespace pdf
{

/// Helpers of the repository of the MCP server
class PDFMcpRepositoryHelper
{
public:
    PDFMcpRepositoryHelper() = delete;

    static PDFDatabaseError invalidValue()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFMcpRepository", "Invalid value."));
    }

    /// Text of a row or of an argument for the user interface: invisible and
    /// control characters removed, bounded
    static QString sanitize(const QString& text, int maximumLength)
    {
        return PDFRepositoryHelper::removeInvisibleCharacters(text, true, maximumLength).trimmed();
    }

    static QString readSanitized(const PDFDatabaseStatement& statement, int column, int maximumLength)
    {
        // A longer text is read up to 4 times the limit and shortened (never refused)
        const QVariant value = statement.getVariant(column);
        if (value.typeId() != QMetaType::QString)
        {
            return QString();
        }
        return sanitize(value.toString().left(maximumLength * 4), maximumLength);
    }

    static QStringList getStatusNames()
    {
        return { QStringLiteral("ok"), QStringLiteral("waiting"), QStringLiteral("allowed"), QStringLiteral("denied_user"),
                 QStringLiteral("denied_timeout"), QStringLiteral("denied_closed"), QStringLiteral("denied_locked"),
                 QStringLiteral("denied_busy"), QStringLiteral("denied_dialog"), QStringLiteral("denied_disconnected"),
                 QStringLiteral("denied_stopped"), QStringLiteral("canceled"), QStringLiteral("failed"),
                 QStringLiteral("denied_permission") };
    }
};

bool PDFMcpServerRow::isAlive(qint64 now, qint64 staleTime) const
{
    return exists && state != PDFMcpServerRowState::Stopped && heartbeat >= now - staleTime;
}

PDFMcpRepository::PDFMcpRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFMcpRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFMcpRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

QString PDFMcpRepository::getStateName(PDFMcpServerRowState state)
{
    switch (state)
    {
        case PDFMcpServerRowState::Stopped:
            return QStringLiteral("stopped");
        case PDFMcpServerRowState::Starting:
            return QStringLiteral("starting");
        case PDFMcpServerRowState::Running:
            return QStringLiteral("running");
        case PDFMcpServerRowState::Stopping:
            return QStringLiteral("stopping");
    }
    return QStringLiteral("stopped");
}

PDFMcpServerRowState PDFMcpRepository::getStateFromName(const QString& name)
{
    for (PDFMcpServerRowState state : { PDFMcpServerRowState::Starting, PDFMcpServerRowState::Running, PDFMcpServerRowState::Stopping })
    {
        if (getStateName(state) == name)
        {
            return state;
        }
    }
    return PDFMcpServerRowState::Stopped;
}

QString PDFMcpRepository::getStatusName(PDFMcpActivityStatus status)
{
    const QStringList names = PDFMcpRepositoryHelper::getStatusNames();
    const int index = int(status);
    return (index >= 0 && index < names.size()) ? names[index] : QStringLiteral("failed");
}

PDFMcpActivityStatus PDFMcpRepository::getStatusFromName(const QString& name)
{
    const qsizetype index = PDFMcpRepositoryHelper::getStatusNames().indexOf(name);
    return index >= 0 ? PDFMcpActivityStatus(index) : PDFMcpActivityStatus::Failed;
}

QString PDFMcpRepository::getRequestKindName(PDFMcpRequestKind kind)
{
    switch (kind)
    {
        case PDFMcpRequestKind::Disconnect:
            return QStringLiteral("disconnect");
        case PDFMcpRequestKind::DisconnectAll:
            return QStringLiteral("disconnect-all");
        case PDFMcpRequestKind::ShowWindow:
            return QStringLiteral("show-window");
    }
    return QString();
}

std::optional<PDFMcpRequestKind> PDFMcpRepository::getRequestKindFromName(const QString& name)
{
    for (PDFMcpRequestKind kind : { PDFMcpRequestKind::Disconnect, PDFMcpRequestKind::DisconnectAll, PDFMcpRequestKind::ShowWindow })
    {
        if (getRequestKindName(kind) == name)
        {
            return kind;
        }
    }
    return std::nullopt;
}

bool PDFMcpRepository::isValidAction(const QString& action)
{
    if (action.isEmpty() || action.size() > MAXIMUM_ACTION_LENGTH)
    {
        return false;
    }

    for (const QChar c : action)
    {
        const char16_t unit = c.unicode();
        const bool isValid = (unit >= u'a' && unit <= u'z') || (unit >= u'0' && unit <= u'9') || unit == u'_' || unit == u'.' || unit == u'-';
        if (!isValid)
        {
            return false;
        }
    }
    return true;
}

QJsonObject PDFMcpRepository::boundDetail(const QJsonObject& detail)
{
    QJsonObject result;
    for (auto it = detail.constBegin(); it != detail.constEnd(); ++it)
    {
        QJsonValue value = it.value();
        if (value.isString())
        {
            value = PDFMcpRepositoryHelper::sanitize(value.toString(), 256);
        }
        result.insert(PDFMcpRepositoryHelper::sanitize(it.key(), 64), value);
    }

    while (!result.isEmpty() && QJsonDocument(result).toJson(QJsonDocument::Compact).size() > MAXIMUM_DETAIL_SIZE)
    {
        result.remove(result.keys().constLast());
    }
    return result;
}

PDFMcpServerRow PDFMcpRepository::getServer() const
{
    PDFMcpServerRow row;
    PDFDatabaseStatement statement(getConnection(), "SELECT instance, pid, port, state, address, heartbeat FROM mcp_server WHERE id = 1");
    if (statement.next())
    {
        row.exists = true;
        row.instance = PDFRepositoryHelper::readText(statement, 0, MAXIMUM_INSTANCE_LENGTH);
        row.pid = PDFRepositoryHelper::readInteger(statement, 1, 0, 0, std::numeric_limits<qint64>::max());
        row.port = int(PDFRepositoryHelper::readInteger(statement, 2, 0, 0, 65535));
        row.state = getStateFromName(PDFRepositoryHelper::readText(statement, 3, 16));
        row.address = PDFMcpRepositoryHelper::readSanitized(statement, 4, MAXIMUM_ADDRESS_LENGTH);
        row.heartbeat = PDFRepositoryHelper::readInteger(statement, 5, 0, 0, std::numeric_limits<qint64>::max());

        // A row without an owner is a stopped server
        if (row.instance.isEmpty())
        {
            row.state = PDFMcpServerRowState::Stopped;
        }
        statement.reset();
    }
    return row;
}

PDFDatabaseError PDFMcpRepository::claimServer(const QString& instance, qint64 pid, int port, PDFMcpServerRowState state, const QString& address, bool* claimed)
{
    if (claimed)
    {
        *claimed = false;
    }

    if (instance.isEmpty() || instance.size() > MAXIMUM_INSTANCE_LENGTH || port < 0 || port > 65535)
    {
        return PDFMcpRepositoryHelper::invalidValue();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const qint64 now = getCurrentTime();
    const PDFMcpServerRow row = getServer();
    const bool isOwn = row.instance == instance;
    const bool isProcessGone = row.pid > 0 && row.pid != pid && !PDFAppProcessInfo::isProcessRunning(row.pid);
    const bool isFree = !row.exists || isOwn || !row.isAlive(now, STALE_TIME) || isProcessGone;
    if (!isFree)
    {
        return PDFDatabaseError();
    }

    PDFDatabaseStatement statement(connection,
        "INSERT INTO mcp_server(id, instance, pid, port, state, address, heartbeat) VALUES (1, ?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(id) DO UPDATE SET instance = excluded.instance, pid = excluded.pid, port = excluded.port, "
        "state = excluded.state, address = excluded.address, heartbeat = excluded.heartbeat");
    statement.bindText(1, instance);
    statement.bindInt64(2, pid);
    statement.bindInt64(3, port);
    statement.bindText(4, getStateName(state));
    statement.bindText(5, PDFMcpRepositoryHelper::sanitize(address, MAXIMUM_ADDRESS_LENGTH));
    statement.bindInt64(6, now);
    if (!statement.execute())
    {
        return statement.getError();
    }

    // The clients of a gone owner are not connected any more
    if (!isOwn)
    {
        PDFDatabaseStatement clients(connection, "DELETE FROM mcp_client");
        if (!clients.execute())
        {
            return clients.getError();
        }

        // Only the owner asks the user, so a waiting row of a gone owner (a
        // crash, a killed process) never gets its answer: it is not waiting
        // forever (kept by Clear and by the trim)
        PDFDatabaseStatement waiting(connection, "UPDATE mcp_activity SET status = 'denied_stopped' WHERE status = 'waiting'");
        if (!waiting.execute())
        {
            return waiting.getError();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    if (claimed)
    {
        *claimed = true;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::heartbeat(const QString& instance, bool* isOwner)
{
    if (isOwner)
    {
        *isOwner = false;
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseStatement statement(connection, "UPDATE mcp_server SET heartbeat = ?2 WHERE id = 1 AND instance = ?1");
    statement.bindText(1, instance);
    statement.bindInt64(2, getCurrentTime());
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (isOwner)
    {
        *isOwner = connection && connection->getChanges() == 1;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::setServerState(const QString& instance, PDFMcpServerRowState state, int port, const QString& address)
{
    if (port < 0 || port > 65535)
    {
        return PDFMcpRepositoryHelper::invalidValue();
    }

    PDFDatabaseStatement statement(getConnection(), "UPDATE mcp_server SET state = ?2, port = ?3, address = ?4, heartbeat = ?5 WHERE id = 1 AND instance = ?1");
    statement.bindText(1, instance);
    statement.bindText(2, getStateName(state));
    statement.bindInt64(3, port);
    statement.bindText(4, PDFMcpRepositoryHelper::sanitize(address, MAXIMUM_ADDRESS_LENGTH));
    statement.bindInt64(5, getCurrentTime());
    if (!statement.execute())
    {
        return statement.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::releaseServer(const QString& instance)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "UPDATE mcp_server SET state = 'stopped' WHERE id = 1 AND instance = ?1");
    statement.bindText(1, instance);
    if (!statement.execute())
    {
        return statement.getError();
    }

    // Only the owner deletes the client rows
    if (connection->getChanges() == 1)
    {
        PDFDatabaseStatement clients(connection, "DELETE FROM mcp_client");
        if (!clients.execute())
        {
            return clients.getError();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

std::vector<PDFMcpClientRow> PDFMcpRepository::getClients() const
{
    std::vector<PDFMcpClientRow> clients;
    PDFDatabaseStatement statement(getConnection(), "SELECT CASE WHEN typeof(session) = 'text' AND length(session) <= 64 THEN session END, name, address, connected FROM mcp_client "
                                                    "ORDER BY CASE WHEN typeof(connected) = 'integer' THEN connected END, session LIMIT ?1");
    statement.bindInt64(1, MAXIMUM_CLIENTS);
    while (statement.next())
    {
        PDFMcpClientRow client;
        client.key = PDFRepositoryHelper::readText(statement, 0, 64);
        if (!PDFMcpAssistantRegistry::isValidKey(client.key))
        {
            continue;
        }

        client.name = PDFMcpRepositoryHelper::readSanitized(statement, 1, MAXIMUM_NAME_LENGTH);
        if (client.name.isEmpty())
        {
            client.name = PDFMcpAssistantRegistry::getDefaultClientName();
        }
        client.address = PDFMcpRepositoryHelper::readSanitized(statement, 2, MAXIMUM_ADDRESS_LENGTH);
        client.connected = PDFRepositoryHelper::readInteger(statement, 3, 0, 0, std::numeric_limits<qint64>::max());
        clients.push_back(client);
    }
    return clients;
}

PDFDatabaseError PDFMcpRepository::upsertClient(const PDFMcpClientRow& client)
{
    if (!PDFMcpAssistantRegistry::isValidKey(client.key) || client.connected < 0)
    {
        return PDFMcpRepositoryHelper::invalidValue();
    }

    PDFDatabaseStatement statement(getConnection(),
        "INSERT INTO mcp_client(session, name, address, connected) VALUES (?1, ?2, ?3, ?4) "
        "ON CONFLICT(session) DO UPDATE SET name = excluded.name, address = excluded.address, connected = excluded.connected");
    statement.bindText(1, client.key);
    statement.bindText(2, PDFMcpRepositoryHelper::sanitize(client.name, MAXIMUM_NAME_LENGTH));
    statement.bindText(3, PDFMcpRepositoryHelper::sanitize(client.address, MAXIMUM_ADDRESS_LENGTH));
    statement.bindInt64(4, client.connected);
    if (!statement.execute())
    {
        return statement.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::removeClient(const QString& key)
{
    return removeClients({ key });
}

PDFDatabaseError PDFMcpRepository::removeClients(const QStringList& keys)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM mcp_client WHERE session = ?1");
    for (const QString& key : keys)
    {
        statement.bindText(1, key);
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::clearClients()
{
    PDFDatabaseStatement statement(getConnection(), "DELETE FROM mcp_client");
    if (!statement.execute())
    {
        return statement.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::addActivity(const std::vector<PDFMcpActivityRow>& rows, std::vector<qint64>* ids)
{
    if (ids)
    {
        ids->clear();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO mcp_activity(time, client, action, document, detail, status) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    for (const PDFMcpActivityRow& row : rows)
    {
        if (!isValidAction(row.action) || row.time < 0)
        {
            return PDFMcpRepositoryHelper::invalidValue();
        }

        statement.bindInt64(1, row.time);
        statement.bindText(2, PDFMcpRepositoryHelper::sanitize(row.client, MAXIMUM_NAME_LENGTH));
        statement.bindText(3, row.action);
        statement.bindText(4, PDFMcpRepositoryHelper::sanitize(row.document, MAXIMUM_DOCUMENT_LENGTH));
        const QJsonObject detail = boundDetail(row.detail);
        if (detail.isEmpty())
        {
            statement.bindNull(5);
        }
        else
        {
            statement.bindText(5, QString::fromUtf8(QJsonDocument(detail).toJson(QJsonDocument::Compact)));
        }
        statement.bindText(6, getStatusName(row.status));
        if (!statement.execute())
        {
            return statement.getError();
        }

        if (ids)
        {
            ids->push_back(connection->getLastInsertRowId());
        }
    }

    // Trim: the newest rows, not older than the age; a waiting row stays
    PDFDatabaseStatement trim(connection,
        "DELETE FROM mcp_activity WHERE status IS NOT 'waiting' AND "
        "(CASE WHEN typeof(time) = 'integer' THEN time ELSE 0 END < ?1 OR "
        "id NOT IN (SELECT id FROM mcp_activity ORDER BY CASE WHEN typeof(time) = 'integer' THEN time END DESC, id DESC LIMIT ?2))");
    trim.bindInt64(1, getCurrentTime() - MAXIMUM_ACTIVITY_AGE);
    trim.bindInt64(2, MAXIMUM_ACTIVITY_ROWS);
    if (!trim.execute())
    {
        return trim.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::updateActivity(qint64 id, PDFMcpActivityStatus status, const std::optional<QJsonObject>& detail, bool* isUpdated)
{
    if (isUpdated)
    {
        *isUpdated = false;
    }

    PDFDatabaseConnection* connection = getConnection();
    if (detail)
    {
        const QJsonObject bounded = boundDetail(*detail);
        PDFDatabaseStatement statement(connection, "UPDATE mcp_activity SET status = ?2, detail = ?3 WHERE id = ?1");
        statement.bindInt64(1, id);
        statement.bindText(2, getStatusName(status));
        if (bounded.isEmpty())
        {
            statement.bindNull(3);
        }
        else
        {
            statement.bindText(3, QString::fromUtf8(QJsonDocument(bounded).toJson(QJsonDocument::Compact)));
        }
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    else
    {
        PDFDatabaseStatement statement(connection, "UPDATE mcp_activity SET status = ?2 WHERE id = ?1");
        statement.bindInt64(1, id);
        statement.bindText(2, getStatusName(status));
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    if (isUpdated)
    {
        *isUpdated = connection && connection->getChanges() == 1;
    }
    return PDFDatabaseError();
}

std::vector<PDFMcpActivityRow> PDFMcpRepository::getActivity(int limit) const
{
    std::vector<PDFMcpActivityRow> rows;
    if (limit <= 0)
    {
        return rows;
    }

    PDFDatabaseStatement statement(getConnection(),
        "SELECT id, time, client, action, document, CASE WHEN typeof(detail) = 'text' AND length(detail) <= 4096 THEN detail END, status FROM mcp_activity "
        "ORDER BY CASE WHEN typeof(time) = 'integer' THEN time END DESC, id DESC LIMIT ?1");
    statement.bindInt64(1, qMin(limit, MAXIMUM_ACTIVITY_ROWS));
    while (statement.next())
    {
        PDFMcpActivityRow row;
        row.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 0, std::numeric_limits<qint64>::max());
        row.time = PDFRepositoryHelper::readInteger(statement, 1, 0, 0, std::numeric_limits<qint64>::max());
        row.client = PDFMcpRepositoryHelper::readSanitized(statement, 2, MAXIMUM_NAME_LENGTH);
        row.action = PDFRepositoryHelper::readText(statement, 3, MAXIMUM_ACTION_LENGTH);
        if (!isValidAction(row.action))
        {
            row.action.clear();
        }
        row.document = PDFMcpRepositoryHelper::readSanitized(statement, 4, MAXIMUM_DOCUMENT_LENGTH);

        const QString detail = PDFRepositoryHelper::readText(statement, 5, MAXIMUM_DETAIL_SIZE);
        if (!detail.isEmpty())
        {
            const QJsonDocument document = QJsonDocument::fromJson(detail.toUtf8());
            if (document.isObject())
            {
                row.detail = boundDetail(document.object());
            }
        }
        row.status = getStatusFromName(PDFRepositoryHelper::readText(statement, 6, 32));
        rows.push_back(row);
    }
    return rows;
}

PDFDatabaseError PDFMcpRepository::clearActivity()
{
    PDFDatabaseStatement statement(getConnection(), "DELETE FROM mcp_activity WHERE status IS NOT 'waiting'");
    if (!statement.execute())
    {
        return statement.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMcpRepository::postRequest(PDFMcpRequestKind kind, const QString& key)
{
    if (kind == PDFMcpRequestKind::Disconnect && !PDFMcpAssistantRegistry::isValidKey(key))
    {
        return PDFMcpRepositoryHelper::invalidValue();
    }

    PDFDatabaseStatement statement(getConnection(), "INSERT INTO mcp_request(kind, session, created) VALUES (?1, ?2, ?3)");
    statement.bindText(1, getRequestKindName(kind));
    if (kind == PDFMcpRequestKind::Disconnect)
    {
        statement.bindText(2, key);
    }
    else
    {
        statement.bindNull(2);
    }
    statement.bindInt64(3, getCurrentTime());
    if (!statement.execute())
    {
        return statement.getError();
    }
    return PDFDatabaseError();
}

std::vector<PDFMcpRequestRow> PDFMcpRepository::takeRequests()
{
    std::vector<PDFMcpRequestRow> requests;

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return requests;
    }

    const qint64 now = getCurrentTime();
    qint64 maximumId = -1;
    PDFDatabaseStatement statement(connection, "SELECT id, CASE WHEN typeof(kind) = 'text' AND length(kind) <= 32 THEN kind END, "
                                               "CASE WHEN typeof(session) = 'text' AND length(session) <= 64 THEN session END, created FROM mcp_request "
                                               "WHERE typeof(id) = 'integer' ORDER BY id LIMIT ?1");
    statement.bindInt64(1, MAXIMUM_REQUESTS);
    while (statement.next())
    {
        const qint64 id = statement.getInt64(0);
        maximumId = qMax(maximumId, id);

        const std::optional<PDFMcpRequestKind> kind = getRequestKindFromName(PDFRepositoryHelper::readText(statement, 1, 32));
        const QString key = PDFRepositoryHelper::readText(statement, 2, 64);
        const qint64 created = PDFRepositoryHelper::readInteger(statement, 3, -1, 0, std::numeric_limits<qint64>::max());
        if (!kind || created < now - REQUEST_LIFETIME || created > now + REQUEST_LIFETIME)
        {
            continue;
        }
        if (*kind == PDFMcpRequestKind::Disconnect && !PDFMcpAssistantRegistry::isValidKey(key))
        {
            continue;
        }

        PDFMcpRequestRow request;
        request.id = id;
        request.kind = *kind;
        request.key = (*kind == PDFMcpRequestKind::Disconnect) ? key : QString();
        request.created = created;
        requests.push_back(request);
    }

    // Every row read is deleted (also the old and the hostile ones)
    if (maximumId >= 0)
    {
        PDFDatabaseStatement remove(connection, "DELETE FROM mcp_request WHERE id <= ?1");
        remove.bindInt64(1, maximumId);
        if (!remove.execute())
        {
            return std::vector<PDFMcpRequestRow>();
        }
    }

    if (!transaction.commit())
    {
        return std::vector<PDFMcpRequestRow>();
    }
    return requests;
}

}   // namespace pdf
