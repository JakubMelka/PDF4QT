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

#include "pdfworkspacerepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrecentrepository.h"
#include "pdfrepositoryhelper.h"

#include <QCoreApplication>
#include <QDateTime>

#include <limits>

namespace pdf
{

/// Helpers of the repository of the workspaces
class PDFWorkspaceRepositoryHelper
{
public:
    PDFWorkspaceRepositoryHelper() = delete;

    /// Columns of a workspace row: id, name, created, opened, saved, is_last_session, number of the documents
    static constexpr const char* WORKSPACE_COLUMNS =
        "SELECT id, CASE WHEN typeof(name) = 'text' AND length(name) <= 1024 THEN name END, created, opened, saved, is_last_session, "
        "(SELECT min(COUNT(*), 1000) FROM workspace_document WHERE workspace_id = workspace.id) FROM workspace ";

    static bool readWorkspace(const PDFDatabaseStatement& statement, PDFWorkspace& workspace)
    {
        constexpr qint64 maximumInteger = std::numeric_limits<qint64>::max();

        workspace = PDFWorkspace();
        workspace.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, maximumInteger);
        if (workspace.id <= 0)
        {
            return false;
        }

        workspace.isLastSession = PDFRepositoryHelper::readInteger(statement, 5, 0, 0, 1) != 0;
        workspace.name = PDFRepositoryHelper::sanitizeStoredName(PDFRepositoryHelper::readText(statement, 1, 1024));
        if (workspace.name.isEmpty() && !workspace.isLastSession)
        {
            workspace.name = QCoreApplication::translate("pdf::PDFWorkspaceRepository", "Workspace %1").arg(workspace.id);
        }
        workspace.created = PDFRepositoryHelper::readInteger(statement, 2, 0, 0, maximumInteger);
        workspace.opened = PDFRepositoryHelper::readInteger(statement, 3, 0, 0, maximumInteger);
        workspace.saved = PDFRepositoryHelper::readInteger(statement, 4, 0, 0, maximumInteger);
        workspace.documentCount = int(PDFRepositoryHelper::readInteger(statement, 6, 0, 0, PDFWorkspaceRepository::MAXIMUM_DOCUMENTS));
        return true;
    }

    static PDFDatabaseError invalidName()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFWorkspaceRepository", "The name of the workspace is not valid."));
    }

    static PDFDatabaseError duplicateName(const QString& name)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFWorkspaceRepository", "A workspace named “%1” already exists.").arg(name));
    }

    static PDFDatabaseError notFound()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFWorkspaceRepository", "The workspace does not exist."));
    }
};

PDFWorkspaceRepository::PDFWorkspaceRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFWorkspaceRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFWorkspaceRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

QString PDFWorkspaceRepository::normalizeName(const QString& name)
{
    return PDFRepositoryHelper::normalizeName(name);
}

std::vector<PDFWorkspaceDocument> PDFWorkspaceRepository::readDocuments(PDFDatabaseConnection* connection, qint64 id) const
{
    std::vector<PDFWorkspaceDocument> documents;
    PDFDatabaseStatement statement(connection, "SELECT CASE WHEN typeof(path) = 'text' AND length(path) <= 32767 THEN path END, page "
                                               "FROM workspace_document WHERE workspace_id = ?1 ORDER BY position LIMIT ?2");
    statement.bindInt64(1, id);
    statement.bindInt64(2, MAXIMUM_DOCUMENTS);
    while (statement.next())
    {
        PDFWorkspaceDocument document;
        document.path = PDFRepositoryHelper::readText(statement, 0, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
        if (!PDFRepositoryHelper::isValidStoredPath(document.path))
        {
            continue;
        }
        document.page = int(PDFRepositoryHelper::readInteger(statement, 1, 0, 0, PDFRecentRepository::MAXIMUM_PAGE));
        documents.push_back(document);
    }
    return documents;
}

std::vector<PDFWorkspace> PDFWorkspaceRepository::readWorkspaces(const char* sql, bool withDocuments) const
{
    std::vector<PDFWorkspace> workspaces;
    PDFDatabaseConnection* connection = getConnection();
    {
        PDFDatabaseStatement statement(connection, sql);
        statement.bindInt64(1, MAXIMUM_WORKSPACES);
        PDFWorkspace workspace;
        while (statement.next())
        {
            if (PDFWorkspaceRepositoryHelper::readWorkspace(statement, workspace))
            {
                workspaces.push_back(workspace);
            }
        }
    }

    if (withDocuments)
    {
        for (PDFWorkspace& workspace : workspaces)
        {
            workspace.documents = readDocuments(connection, workspace.id);
            workspace.documentCount = int(workspace.documents.size());
        }
    }
    return workspaces;
}

std::vector<PDFWorkspace> PDFWorkspaceRepository::getWorkspaces(bool withDocuments) const
{
    const QByteArray sql = QByteArray(PDFWorkspaceRepositoryHelper::WORKSPACE_COLUMNS) +
                           "WHERE is_last_session IS NOT 1 ORDER BY max(ifnull(opened, 0), ifnull(saved, 0), ifnull(created, 0)) DESC, id DESC LIMIT ?1";
    return readWorkspaces(sql.constData(), withDocuments);
}

std::vector<PDFWorkspace> PDFWorkspaceRepository::getRecentWorkspaces() const
{
    const QByteArray sql = QByteArray(PDFWorkspaceRepositoryHelper::WORKSPACE_COLUMNS) +
                           "WHERE is_last_session IS NOT 1 AND typeof(opened) = 'integer' AND opened > 0 ORDER BY opened DESC, id DESC LIMIT ?1";
    return readWorkspaces(sql.constData(), false);
}

std::optional<PDFWorkspace> PDFWorkspaceRepository::getWorkspace(qint64 id) const
{
    if (id <= 0)
    {
        return std::nullopt;
    }

    PDFDatabaseConnection* connection = getConnection();
    const QByteArray sql = QByteArray(PDFWorkspaceRepositoryHelper::WORKSPACE_COLUMNS) + "WHERE id = ?2 LIMIT ?1";
    PDFDatabaseStatement statement(connection, sql.constData());
    statement.bindInt64(1, 1);
    statement.bindInt64(2, id);

    std::optional<PDFWorkspace> result;
    if (statement.next())
    {
        PDFWorkspace workspace;
        if (PDFWorkspaceRepositoryHelper::readWorkspace(statement, workspace))
        {
            result = workspace;
        }
        statement.reset();
    }

    if (result)
    {
        result->documents = readDocuments(connection, result->id);
        result->documentCount = int(result->documents.size());
    }
    return result;
}

std::optional<PDFWorkspace> PDFWorkspaceRepository::findWorkspaceImpl(const QString& name, qint64 excludedId) const
{
    const QString normalizedName = normalizeName(name);
    if (normalizedName.isEmpty())
    {
        return std::nullopt;
    }

    for (const PDFWorkspace& workspace : getWorkspaces(false))
    {
        if (workspace.id != excludedId && QString::compare(workspace.name, normalizedName, Qt::CaseInsensitive) == 0)
        {
            return workspace;
        }
    }
    return std::nullopt;
}

std::optional<PDFWorkspace> PDFWorkspaceRepository::findWorkspace(const QString& name) const
{
    return findWorkspaceImpl(name, 0);
}

PDFDatabaseError PDFWorkspaceRepository::validateDocuments(const std::vector<PDFWorkspaceDocument>& documents, bool allowEmpty)
{
    if ((!allowEmpty && documents.empty()) || documents.size() > size_t(MAXIMUM_DOCUMENTS))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFWorkspaceRepository", "Invalid number of the documents of the workspace."));
    }

    for (const PDFWorkspaceDocument& document : documents)
    {
        if (!PDFRepositoryHelper::isValidStoredPath(document.path) || document.page < 0 || document.page > PDFRecentRepository::MAXIMUM_PAGE)
        {
            return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFWorkspaceRepository", "Invalid document of the workspace."));
        }
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFWorkspaceRepository::writeDocuments(PDFDatabaseConnection* connection, qint64 id, const std::vector<PDFWorkspaceDocument>& documents)
{
    {
        PDFDatabaseStatement statement(connection, "DELETE FROM workspace_document WHERE workspace_id = ?1");
        statement.bindInt64(1, id);
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO workspace_document(workspace_id, position, path, page) VALUES (?1, ?2, ?3, ?4)");
    for (size_t i = 0; i < documents.size(); ++i)
    {
        statement.bindInt64(1, id);
        statement.bindInt64(2, qint64(i));
        statement.bindText(3, documents[i].path);
        if (documents[i].page > 0)
        {
            statement.bindInt64(4, documents[i].page);
        }
        else
        {
            statement.bindNull(4);
        }

        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFWorkspaceRepository::saveWorkspace(const QString& name, const std::vector<PDFWorkspaceDocument>& documents, bool replaceExisting,
                                                       qint64* id, bool* isDuplicate)
{
    if (id)
    {
        *id = 0;
    }
    if (isDuplicate)
    {
        *isDuplicate = false;
    }

    const QString normalizedName = normalizeName(name);
    if (normalizedName.isEmpty())
    {
        return PDFWorkspaceRepositoryHelper::invalidName();
    }

    PDFDatabaseError error = validateDocuments(documents, false);
    if (error.isError())
    {
        return error;
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // The duplicate check is inside of the write transaction (another instance)
    const qint64 now = getCurrentTime();
    qint64 workspaceId = 0;
    if (std::optional<PDFWorkspace> existing = findWorkspaceImpl(normalizedName, 0))
    {
        if (!replaceExisting)
        {
            if (isDuplicate)
            {
                *isDuplicate = true;
            }
            return PDFWorkspaceRepositoryHelper::duplicateName(existing->name);
        }

        workspaceId = existing->id;
        PDFDatabaseStatement statement(connection, "UPDATE workspace SET name = ?2, saved = ?3 WHERE id = ?1");
        statement.bindInt64(1, workspaceId);
        statement.bindText(2, normalizedName);
        statement.bindInt64(3, now);
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    else
    {
        PDFDatabaseStatement statement(connection, "INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES (?1, ?2, NULL, ?2, 0)");
        statement.bindText(1, normalizedName);
        statement.bindInt64(2, now);
        if (!statement.execute())
        {
            return statement.getError();
        }
        workspaceId = connection->getLastInsertRowId();
    }

    error = writeDocuments(connection, workspaceId, documents);
    if (error.isError())
    {
        return error;
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    if (id)
    {
        *id = workspaceId;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFWorkspaceRepository::renameWorkspace(qint64 id, const QString& name, bool* isDuplicate)
{
    if (isDuplicate)
    {
        *isDuplicate = false;
    }

    const QString normalizedName = normalizeName(name);
    if (normalizedName.isEmpty())
    {
        return PDFWorkspaceRepositoryHelper::invalidName();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    if (std::optional<PDFWorkspace> existing = findWorkspaceImpl(normalizedName, id))
    {
        if (isDuplicate)
        {
            *isDuplicate = true;
        }
        return PDFWorkspaceRepositoryHelper::duplicateName(existing->name);
    }

    PDFDatabaseStatement statement(connection, "UPDATE workspace SET name = ?2 WHERE id = ?1 AND is_last_session IS NOT 1 AND name IS NOT ?2");
    statement.bindInt64(1, id);
    statement.bindText(2, normalizedName);
    if (!statement.execute())
    {
        return statement.getError();
    }

    // Nothing changed: the same name or the last session (it is never renamed, a
    // success as before), or the workspace has been deleted meanwhile (by another
    // instance) - that is an error, not a success
    if (connection->getChanges() == 0)
    {
        PDFDatabaseStatement exists(connection, "SELECT COUNT(*) FROM workspace WHERE id = ?1");
        exists.bindInt64(1, id);
        const bool isFound = exists.next() && exists.getInt64(0) > 0;
        exists.reset();
        if (!isFound)
        {
            return PDFWorkspaceRepositoryHelper::notFound();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFWorkspaceRepository::deleteWorkspace(qint64 id)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // Documents first: the cascade of the foreign key works only with
    // foreign_keys=ON, a row of another version is deleted too
    for (const char* sql : { "DELETE FROM workspace_document WHERE workspace_id = ?1", "DELETE FROM workspace WHERE id = ?1" })
    {
        PDFDatabaseStatement statement(connection, sql);
        statement.bindInt64(1, id);
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

PDFDatabaseError PDFWorkspaceRepository::markOpened(qint64 id)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "UPDATE workspace SET opened = ?2 WHERE id = ?1 AND is_last_session IS NOT 1");
    statement.bindInt64(1, id);
    statement.bindInt64(2, getCurrentTime());
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (connection->getChanges() == 0)
    {
        return PDFWorkspaceRepositoryHelper::notFound();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFWorkspaceRepository::saveLastSession(const std::vector<PDFWorkspaceDocument>& documents)
{
    PDFDatabaseError error = validateDocuments(documents, true);
    if (error.isError())
    {
        return error;
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // One last session: the previous one (and rows of another version) is replaced
    {
        PDFDatabaseStatement statement(connection, "DELETE FROM workspace_document WHERE workspace_id IN (SELECT id FROM workspace WHERE is_last_session IS 1)");
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    {
        PDFDatabaseStatement statement(connection, "DELETE FROM workspace WHERE is_last_session IS 1");
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    if (!documents.empty())
    {
        const qint64 now = getCurrentTime();
        PDFDatabaseStatement statement(connection, "INSERT INTO workspace(name, created, opened, saved, is_last_session) VALUES ('', ?1, NULL, ?1, 1)");
        statement.bindInt64(1, now);
        if (!statement.execute())
        {
            return statement.getError();
        }

        error = writeDocuments(connection, connection->getLastInsertRowId(), documents);
        if (error.isError())
        {
            return error;
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

std::optional<PDFWorkspace> PDFWorkspaceRepository::getLastSession() const
{
    const QByteArray sql = QByteArray(PDFWorkspaceRepositoryHelper::WORKSPACE_COLUMNS) + "WHERE is_last_session IS 1 ORDER BY id DESC LIMIT ?1";
    std::vector<PDFWorkspace> workspaces = readWorkspaces(sql.constData(), true);
    for (PDFWorkspace& workspace : workspaces)
    {
        if (!workspace.documents.empty())
        {
            return workspace;
        }
    }
    return std::nullopt;
}

PDFDatabaseError PDFWorkspaceRepository::deleteLastSession()
{
    return saveLastSession(std::vector<PDFWorkspaceDocument>());
}

}   // namespace pdf
