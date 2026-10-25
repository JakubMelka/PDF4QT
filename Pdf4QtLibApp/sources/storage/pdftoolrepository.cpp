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

#include "pdftoolrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttoolcatalog.h"

#include <QCoreApplication>
#include <QDateTime>

#include <limits>

namespace pdf
{

/// Helpers of the repository of the tools
class PDFToolRepositoryHelper
{
public:
    PDFToolRepositoryHelper() = delete;

    static PDFDatabaseError unknownTool()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolRepository", "Unknown tool."));
    }
};

PDFToolRepository::PDFToolRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFToolRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFToolRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

QStringList PDFToolRepository::getPinnedTools() const
{
    QStringList tools;
    PDFDatabaseStatement statement(getConnection(), "SELECT CASE WHEN typeof(tool_id) = 'text' AND length(tool_id) <= 64 THEN tool_id END FROM tool_pin "
                                                    "WHERE pinned IS 1 ORDER BY CASE WHEN typeof(position) = 'integer' THEN position END IS NULL, position, tool_id LIMIT 1000");
    while (statement.next())
    {
        const QString toolId = PDFRepositoryHelper::readText(statement, 0, PDFDocumentToolCatalog::MAXIMUM_ID_LENGTH);
        if (PDFDocumentToolCatalog::isValidToolId(toolId) && !tools.contains(toolId))
        {
            tools << toolId;
        }
    }
    return tools;
}

bool PDFToolRepository::isPinned(const QString& toolId) const
{
    return getPinnedTools().contains(toolId);
}

PDFDatabaseError PDFToolRepository::setPinned(const QString& toolId, bool pinned)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return PDFToolRepositoryHelper::unknownTool();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    if (isPinned(toolId) == pinned)
    {
        return PDFDatabaseError();
    }

    if (pinned)
    {
        PDFDatabaseStatement statement(connection, "INSERT INTO tool_pin(tool_id, position, pinned) VALUES (?1, "
                                                   "(SELECT COALESCE(MAX(CASE WHEN typeof(position) = 'integer' AND position BETWEEN 0 AND 1000000000 THEN position END), 0) + 1 FROM tool_pin WHERE pinned IS 1), 1) "
                                                   "ON CONFLICT(tool_id) DO UPDATE SET position = excluded.position, pinned = 1");
        statement.bindText(1, toolId);
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    else
    {
        PDFDatabaseStatement statement(connection, "DELETE FROM tool_pin WHERE tool_id = ?1");
        statement.bindText(1, toolId);
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

PDFDatabaseError PDFToolRepository::setPinnedOrder(const QStringList& toolIds)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const QStringList current = getPinnedTools();
    QStringList order;
    for (const QString& toolId : toolIds)
    {
        if (current.contains(toolId) && !order.contains(toolId))
        {
            order << toolId;
        }
    }
    for (const QString& toolId : current)
    {
        if (!order.contains(toolId))
        {
            order << toolId;
        }
    }

    PDFDatabaseStatement statement(connection, "UPDATE tool_pin SET position = ?2 WHERE tool_id = ?1 AND position IS NOT ?2");
    for (int i = 0; i < order.size(); ++i)
    {
        statement.bindText(1, order[i]);
        statement.bindInt64(2, i + 1);
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

std::vector<PDFToolUsage> PDFToolRepository::getUsage(int limit) const
{
    std::vector<PDFToolUsage> usage;
    if (limit == 0)
    {
        return usage;
    }

    PDFDatabaseStatement statement(getConnection(), "SELECT CASE WHEN typeof(tool_id) = 'text' AND length(tool_id) <= 64 THEN tool_id END, last_used, count FROM tool_usage "
                                                    "ORDER BY CASE WHEN typeof(last_used) = 'integer' THEN last_used END DESC, tool_id LIMIT 1000");
    while (statement.next())
    {
        PDFToolUsage item;
        item.toolId = PDFRepositoryHelper::readText(statement, 0, PDFDocumentToolCatalog::MAXIMUM_ID_LENGTH);
        if (!PDFDocumentToolCatalog::isValidToolId(item.toolId))
        {
            continue;
        }

        item.lastUsed = PDFRepositoryHelper::readInteger(statement, 1, 0, 0, std::numeric_limits<qint64>::max());
        item.count = PDFRepositoryHelper::readInteger(statement, 2, 0, 0, std::numeric_limits<qint64>::max());
        usage.push_back(item);
        if (limit > 0 && int(usage.size()) >= limit)
        {
            statement.reset();
            break;
        }
    }
    return usage;
}

QStringList PDFToolRepository::getUsedTools() const
{
    QStringList tools;
    for (const PDFToolUsage& usage : getUsage())
    {
        tools << usage.toolId;
    }
    return tools;
}

PDFDatabaseError PDFToolRepository::recordUsage(const QString& toolId)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return PDFToolRepositoryHelper::unknownTool();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection,
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES (?1, ?2, 1) "
        "ON CONFLICT(tool_id) DO UPDATE SET last_used = excluded.last_used, "
        "count = CASE WHEN typeof(tool_usage.count) = 'integer' AND tool_usage.count BETWEEN 0 AND 1000000000 THEN tool_usage.count + 1 ELSE 1 END");
    statement.bindText(1, toolId);
    statement.bindInt64(2, getCurrentTime());
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

qint64 PDFToolRepository::getHistoryRuns() const
{
    // A state setting, which is not a part of the registry of this repository:
    // only a non-negative integer is a count
    PDFDatabaseStatement statement(getConnection(), "SELECT CASE WHEN typeof(value) = 'integer' AND value >= 0 THEN value END FROM setting WHERE key = ?1");
    statement.bindText(1, PDFSettingKeys::toolsHistoryRuns());
    qint64 runs = 0;
    if (statement.next())
    {
        runs = PDFRepositoryHelper::readInteger(statement, 0, 0, 0, std::numeric_limits<qint64>::max());
        statement.reset();
    }
    return runs;
}

PDFDatabaseError PDFToolRepository::clearHistory()
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement pairs(connection, "DELETE FROM tool_pair");
    if (!pairs.execute())
    {
        return pairs.getError();
    }

    PDFDatabaseStatement runs(connection, "DELETE FROM setting WHERE key = ?1");
    runs.bindText(1, PDFSettingKeys::toolsHistoryRuns());
    if (!runs.execute())
    {
        return runs.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

}   // namespace pdf
