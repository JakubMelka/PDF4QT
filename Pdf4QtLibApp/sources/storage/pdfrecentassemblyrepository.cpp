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

#include "pdfrecentassemblyrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrecentrepository.h"
#include "pdfrepositoryhelper.h"

#include "pdfdocumentassembly.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>

#include <limits>

namespace pdf
{

// getAssemblies has the limit of MAXIMUM_ROWS in its binding, the prune the limit of
// MAXIMUM_ASSEMBLIES; the page count of a row is the page count of an assembly
static_assert(PDFRecentAssemblyRepository::MAXIMUM_ROWS == 1000);
static_assert(PDFRecentAssemblyRepository::MAXIMUM_ASSEMBLIES == 20);

/// Helpers of the repository of the recent assemblies
class PDFRecentAssemblyRepositoryHelper
{
public:
    PDFRecentAssemblyRepositoryHelper() = delete;

    /// Columns of a row in the order of readAssembly. Text columns are limited in SQL, so a
    /// hostile row (another version, a damaged file) never loads a huge value.
    static constexpr const char* COLUMNS =
        "SELECT path, CASE WHEN length(name) <= 4096 THEN name END, CASE WHEN length(folder) <= 32767 THEN folder END, "
        "last_opened, page_count, pinned FROM recent_assembly WHERE typeof(path) = 'text' AND length(path) BETWEEN 1 AND 32767 ";

    /// Most pages of an assembly (PDFDocumentAssembly::MAXIMUM_PAGE_COUNT)
    static constexpr qint64 MAXIMUM_PAGE_COUNT = PDFDocumentAssembly::MAXIMUM_PAGE_COUNT;

    static PDFDatabaseError invalidPath()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentAssemblyRepository", "The path is not valid."));
    }

    static PDFDatabaseError notFound()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentAssemblyRepository", "The file is not in the recent assemblies."));
    }

    static PDFDatabaseError invalidPageCount()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentAssemblyRepository", "The page count is not valid."));
    }

    /// The name of an assembly of its path: the file name without the suffix
    /// ("Tender_2026.pdf4qtassembly" - "Tender_2026"); the file name, when nothing remains
    static QString getName(const QString& path)
    {
        const QString fileName = PDFRepositoryHelper::getFileName(path);
        const QString name = QFileInfo(fileName).completeBaseName();
        return name.isEmpty() ? fileName : name;
    }

    /// Reads one row; false for a row, which is not usable (an invalid path)
    static bool readAssembly(const PDFDatabaseStatement& statement, PDFRecentAssembly& assembly)
    {
        constexpr qint64 maximumInteger = std::numeric_limits<qint64>::max();

        assembly = PDFRecentAssembly();
        assembly.path = PDFRepositoryHelper::readText(statement, 0, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
        if (!PDFRepositoryHelper::isValidStoredPath(assembly.path))
        {
            return false;
        }

        assembly.name = PDFRepositoryHelper::sanitizeStoredName(PDFRepositoryHelper::readText(statement, 1, 4096));
        if (assembly.name.isEmpty())
        {
            assembly.name = PDFRepositoryHelper::sanitizeStoredName(getName(assembly.path));
        }

        assembly.folder = PDFRepositoryHelper::readText(statement, 2, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
        if (!PDFRepositoryHelper::isValidStoredPath(assembly.folder))
        {
            assembly.folder = PDFRepositoryHelper::getFolder(assembly.path);
        }

        assembly.lastOpened = PDFRepositoryHelper::readInteger(statement, 3, 0, 0, maximumInteger);
        assembly.pageCount = int(PDFRepositoryHelper::readInteger(statement, 4, -1, 0, MAXIMUM_PAGE_COUNT));
        assembly.pinned = PDFRepositoryHelper::readInteger(statement, 5, 0, 0, 1) != 0;
        return true;
    }

    /// Deletes the non-pinned rows over MAXIMUM_ASSEMBLIES (the oldest first)
    static PDFDatabaseError prune(PDFDatabaseConnection* connection)
    {
        PDFDatabaseStatement statement(connection, "DELETE FROM recent_assembly WHERE pinned IS NOT 1 AND path NOT IN "
                                                   "(SELECT path FROM recent_assembly WHERE pinned IS NOT 1 ORDER BY last_opened DESC, path LIMIT ?1)");
        statement.bindInt64(1, PDFRecentAssemblyRepository::MAXIMUM_ASSEMBLIES);
        statement.execute();
        return statement.getError();
    }
};

PDFRecentAssemblyRepository::PDFRecentAssemblyRepository(PDFDatabase* database) :
    m_database(database)
{

}

std::vector<PDFRecentAssembly> PDFRecentAssemblyRepository::getAssemblies(int limit) const
{
    std::vector<PDFRecentAssembly> assemblies;
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    if (!connection)
    {
        return assemblies;
    }

    // The pinned ones first (a pinned value of another type is not pinned), then the newest;
    // the rows, which are skipped, still count to the limit of the rows read
    const int rowLimit = (limit <= 0 || limit > MAXIMUM_ROWS) ? MAXIMUM_ROWS : limit;
    QByteArray sql(PDFRecentAssemblyRepositoryHelper::COLUMNS);
    sql += "ORDER BY pinned IS 1 DESC, CASE WHEN typeof(last_opened) = 'integer' THEN last_opened ELSE 0 END DESC, path LIMIT ?1";

    PDFDatabaseStatement statement(connection, sql.constData());
    statement.bindInt64(1, rowLimit);
    PDFRecentAssembly assembly;
    while (statement.next())
    {
        if (PDFRecentAssemblyRepositoryHelper::readAssembly(statement, assembly))
        {
            assemblies.push_back(assembly);
        }
    }
    return assemblies;
}

PDFDatabaseError PDFRecentAssemblyRepository::recordOpened(const QString& filePath, int pageCount)
{
    // The canonical path of the file (it exists: it was opened or saved now)
    const QString path = PDFRepositoryHelper::normalizePath(filePath);
    if (path.isEmpty() || !PDFRepositoryHelper::isValidStoredPath(path))
    {
        return PDFRecentAssemblyRepositoryHelper::invalidPath();
    }
    if (pageCount < -1 || pageCount > PDFRecentAssemblyRepositoryHelper::MAXIMUM_PAGE_COUNT)
    {
        return PDFRecentAssemblyRepositoryHelper::invalidPageCount();
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    const QString folder = PDFRepositoryHelper::getFolder(path);
    {
        PDFDatabaseTransaction transaction(connection);
        if (!transaction.isActive())
        {
            return transaction.getError();
        }

        // An unknown page count (-1: the file is opened, its assembly is not read yet) keeps the
        // count of the last reading; a known one replaces it
        PDFDatabaseStatement statement(connection,
            "INSERT INTO recent_assembly(path, name, folder, last_opened, page_count, pinned) VALUES (?1, ?2, ?3, ?4, ?5, 0) "
            "ON CONFLICT(path) DO UPDATE SET name = excluded.name, folder = excluded.folder, last_opened = excluded.last_opened, "
            "page_count = CASE WHEN excluded.page_count IS NULL THEN recent_assembly.page_count ELSE excluded.page_count END");
        statement.bindText(1, path);
        statement.bindText(2, PDFRecentAssemblyRepositoryHelper::getName(path));
        statement.bindText(3, folder);
        statement.bindInt64(4, m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch());
        if (pageCount >= 0)
        {
            statement.bindInt64(5, pageCount);
        }
        else
        {
            statement.bindNull(5);
        }
        if (!statement.execute())
        {
            return statement.getError();
        }

        const PDFDatabaseError error = PDFRecentAssemblyRepositoryHelper::prune(connection);
        if (error.isError())
        {
            return error;
        }

        if (!transaction.commit())
        {
            return transaction.getError();
        }
    }

    // The folder is a recent folder of the application (RB12 of step 3b: the table
    // recent_folder, as the recent folders of PageMaster were); its own short transaction
    PDFRecentRepository recent(m_database, nullptr);
    if (m_currentTime > 0)
    {
        recent.setCurrentTime(m_currentTime);
    }
    return recent.recordFolder(folder);
}

PDFDatabaseError PDFRecentAssemblyRepository::remove(const QString& filePath)
{
    if (!PDFRepositoryHelper::isValidStoredPath(filePath))
    {
        return PDFRecentAssemblyRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM recent_assembly WHERE path = ?1");
    statement.bindText(1, filePath);
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

PDFDatabaseError PDFRecentAssemblyRepository::setPinned(const QString& filePath, bool pinned)
{
    if (!PDFRepositoryHelper::isValidStoredPath(filePath))
    {
        return PDFRecentAssemblyRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // The row must exist; an unchanged value writes nothing (no notification without a change)
    PDFDatabaseStatement current(connection, "SELECT pinned IS 1 FROM recent_assembly WHERE path = ?1");
    current.bindText(1, filePath);
    if (!current.next())
    {
        const PDFDatabaseError error = current.getError();
        return error.isError() ? error : PDFRecentAssemblyRepositoryHelper::notFound();
    }
    const bool isPinned = current.getBool(0);
    current.reset();
    if (isPinned == pinned)
    {
        return PDFDatabaseError();
    }

    PDFDatabaseStatement statement(connection, "UPDATE recent_assembly SET pinned = ?2 WHERE path = ?1");
    statement.bindText(1, filePath);
    statement.bindInt64(2, pinned ? 1 : 0);
    if (!statement.execute())
    {
        return statement.getError();
    }

    // An unpinned assembly is subject to the limit of the recent assemblies again
    if (!pinned)
    {
        const PDFDatabaseError error = PDFRecentAssemblyRepositoryHelper::prune(connection);
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

}   // namespace pdf
