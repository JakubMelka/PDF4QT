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

#include "pdfdocumenttoolusage.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsuggestions.h"

#include <QCoreApplication>
#include <QDateTime>

#include <limits>

namespace pdf
{

/// Helpers of the use of the tools
class PDFDocumentToolUsageHelper
{
public:
    PDFDocumentToolUsageHelper() = delete;

    /// The state setting tools.suggestNext: a stored 0 / 1, anything else is the
    /// default (on) - a row of another version is untrusted
    static bool readSuggestNext(PDFDatabaseConnection* connection)
    {
        PDFDatabaseStatement statement(connection, "SELECT CASE WHEN typeof(value) = 'integer' AND value IN (0, 1) THEN value END FROM setting WHERE key = ?1");
        statement.bindText(1, PDFSettingKeys::toolsSuggestNext());
        bool isEnabled = true;
        if (statement.next())
        {
            isEnabled = PDFRepositoryHelper::readInteger(statement, 0, 1, 0, 1) != 0;
            statement.reset();
        }
        return isEnabled;
    }

    /// Pairs of the tool (rows of tool_pair; hostile rows are filtered by the
    /// suggestions); \p error gets the error of the read
    static std::vector<PDFDocumentToolPair> readPairs(PDFDatabaseConnection* connection, const QString& fromTool, PDFDatabaseError* error)
    {
        std::vector<PDFDocumentToolPair> pairs;
        PDFDatabaseStatement statement(connection, "SELECT CASE WHEN typeof(to_tool) = 'text' AND length(to_tool) <= 64 THEN to_tool END, count, last_used "
                                                   "FROM tool_pair WHERE from_tool = ?1 ORDER BY rowid LIMIT ?2");
        statement.bindText(1, fromTool);
        statement.bindInt64(2, PDFDocumentToolUsage::MAXIMUM_PAIR_ROWS);
        while (statement.next())
        {
            PDFDocumentToolPair pair;
            pair.fromTool = fromTool;
            pair.toTool = PDFRepositoryHelper::readText(statement, 0, PDFDocumentToolCatalog::MAXIMUM_ID_LENGTH);
            pair.count = PDFRepositoryHelper::readInteger(statement, 1, 0, 0, std::numeric_limits<qint64>::max());
            pair.lastUsed = PDFRepositoryHelper::readInteger(statement, 2, 0, 0, std::numeric_limits<qint64>::max());
            pairs.push_back(pair);
        }

        if (error)
        {
            *error = statement.getError();
        }
        return pairs;
    }

    /// Replaces all pairs of the tool by \p pairs. A pair with a tool, which this
    /// version does not know (a tool of a newer version, a hostile row), is dropped:
    /// the history of the pairs has no forward compatibility (owner decision O25),
    /// unlike the items of the collections (PDFToolCollectionRepository::writeItems),
    /// which the user has put together.
    static PDFDatabaseError writePairs(PDFDatabaseConnection* connection, const QString& fromTool, const std::vector<PDFDocumentToolPair>& pairs)
    {
        PDFDatabaseStatement remove(connection, "DELETE FROM tool_pair WHERE from_tool = ?1");
        remove.bindText(1, fromTool);
        if (!remove.execute())
        {
            return remove.getError();
        }

        PDFDatabaseStatement insert(connection, "INSERT INTO tool_pair(from_tool, to_tool, count, last_used) VALUES (?1, ?2, ?3, ?4)");
        for (const PDFDocumentToolPair& pair : pairs)
        {
            insert.bindText(1, pair.fromTool);
            insert.bindText(2, pair.toTool);
            insert.bindInt64(3, pair.count);
            insert.bindInt64(4, pair.lastUsed);
            if (!insert.execute())
            {
                return insert.getError();
            }
        }
        return PDFDatabaseError();
    }
};

PDFDocumentToolUsage::PDFDocumentToolUsage(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFDocumentToolUsage::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFDocumentToolUsage::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

PDFDatabaseError PDFDocumentToolUsage::recordRun(const QString& toolId, const QString& previousToolId, Origin origin)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolRepository", "Unknown tool."));
    }

    // Runs of an assistant never change the suggestions of the user
    if (origin == Origin::Mcp)
    {
        return PDFDatabaseError();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const qint64 time = getCurrentTime();
    PDFDatabaseStatement usage(connection,
        "INSERT INTO tool_usage(tool_id, last_used, count) VALUES (?1, ?2, 1) "
        "ON CONFLICT(tool_id) DO UPDATE SET last_used = excluded.last_used, "
        "count = CASE WHEN typeof(tool_usage.count) = 'integer' AND tool_usage.count BETWEEN 0 AND 1000000000 THEN tool_usage.count + 1 ELSE 1 END");
    usage.bindText(1, toolId);
    usage.bindInt64(2, time);
    if (!usage.execute())
    {
        return usage.getError();
    }

    const bool isPair = previousToolId != toolId && PDFDocumentToolCatalog::isValidToolId(previousToolId);
    if (isPair && PDFDocumentToolUsageHelper::readSuggestNext(connection))
    {
        PDFDatabaseError error;
        const std::vector<PDFDocumentToolPair> pairs = PDFDocumentToolUsageHelper::readPairs(connection, previousToolId, &error);
        if (error.isError())
        {
            return error;
        }

        error = PDFDocumentToolUsageHelper::writePairs(connection, previousToolId, PDFDocumentToolSuggestions::recordPair(pairs, previousToolId, toolId, time));
        if (error.isError())
        {
            return error;
        }

        // The runs recorded in the tool history (Settings › General); a hostile stored value starts again
        PDFDatabaseStatement runs(connection,
            "INSERT INTO setting(key, value, modified) VALUES (?1, 1, ?2) "
            "ON CONFLICT(key) DO UPDATE SET modified = excluded.modified, "
            "value = CASE WHEN typeof(setting.value) = 'integer' AND setting.value BETWEEN 0 AND 1000000000000 THEN setting.value + 1 ELSE 1 END");
        runs.bindText(1, PDFSettingKeys::toolsHistoryRuns());
        runs.bindInt64(2, time);
        if (!runs.execute())
        {
            return runs.getError();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

QStringList PDFDocumentToolUsage::getSuggestions(const QString& finishedToolId, const PDFDocumentToolAvailability& availability) const
{
    if (!PDFDocumentToolCatalog::isValidToolId(finishedToolId))
    {
        return QStringList();
    }

    PDFDocumentToolSuggestionInput input;
    input.fromTools = QStringList({ finishedToolId });
    input.unavailableTools = PDFDocumentToolSuggestions::getUnavailableTools(availability);

    PDFDatabaseConnection* connection = getConnection();
    if (connection)
    {
        PDFDatabaseTransaction transaction(connection, PDFDatabaseTransaction::Mode::Read);
        input.isSuggestNextEnabled = PDFDocumentToolUsageHelper::readSuggestNext(connection);
        if (input.isSuggestNextEnabled)
        {
            input.pairs = PDFDocumentToolUsageHelper::readPairs(connection, finishedToolId, nullptr);
        }
    }
    return PDFDocumentToolSuggestions::suggest(input);
}

bool PDFDocumentToolUsage::isSuggestNextEnabled() const
{
    PDFDatabaseConnection* connection = getConnection();
    return connection ? PDFDocumentToolUsageHelper::readSuggestNext(connection) : true;
}

}   // namespace pdf
