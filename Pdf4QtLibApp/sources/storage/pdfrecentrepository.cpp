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

#include "pdfrecentrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QTimeZone>

#include <algorithm>
#include <iterator>
#include <limits>

namespace pdf
{

// The lists of the files have "LIMIT 10000" in their SQL
static_assert(PDFRecentRepository::MAXIMUM_FILE_ROWS == 10000);

/// Helpers of the repository of the recent files
class PDFRecentRepositoryHelper
{
public:
    PDFRecentRepositoryHelper() = delete;

    /// Columns of a file row, in the order of PDFRecentRepository::readFile. Text and
    /// blob columns are limited in SQL, so a hostile row never loads a huge value.
    static constexpr const char* FILE_COLUMNS =
        "SELECT id, path, CASE WHEN length(name) <= 4096 THEN name END, CASE WHEN length(folder) <= 32767 THEN folder END, "
        "last_opened, open_count, pinned, pin_order, page_count, file_size, file_mtime, last_page, reached_end, "
        "CASE WHEN typeof(view_state) = 'text' AND length(view_state) <= 65536 THEN view_state END, locked, "
        "typeof(thumbnail) = 'blob' AND length(thumbnail) BETWEEN 1 AND 2097152, thumbnail_mtime, thumbnail_state "
        "FROM recent_file WHERE typeof(path) = 'text' AND length(path) BETWEEN 1 AND 32767 ";

    static bool compareByName(const PDFRecentFile& left, const PDFRecentFile& right)
    {
        const int result = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        if (result != 0)
        {
            return result < 0;
        }
        return QString::compare(left.path, right.path, Qt::CaseInsensitive) < 0;
    }

    static bool compareByFolder(const PDFRecentFile& left, const PDFRecentFile& right)
    {
        const int result = QString::compare(left.folder, right.folder, Qt::CaseInsensitive);
        if (result != 0)
        {
            return result < 0;
        }
        return compareByName(left, right);
    }

    static bool compareFoldersByUse(const PDFRecentFolder& left, const PDFRecentFolder& right)
    {
        if (left.lastUsed != right.lastUsed)
        {
            return left.lastUsed > right.lastUsed;
        }
        return left.path < right.path;
    }

    /// Kind of a folder: a standard folder of the system, or Other
    static PDFRecentFolderKind getFolderKind(const QString& folderPath, const std::vector<PDFRecentFolder>& standardFolders)
    {
        for (const PDFRecentFolder& folder : standardFolders)
        {
            if (PDFRepositoryHelper::isSamePath(folder.path, folderPath))
            {
                return folder.kind;
            }
        }
        return PDFRecentFolderKind::Other;
    }

    /// Error of an operation on a file, which is not recorded
    static PDFDatabaseError notFound()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "The file is not in the recent files."));
    }

    static PDFDatabaseError invalidPath()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "The path is not valid."));
    }

    /// Path refused by the repository: invalid or too long
    static bool isUsablePath(const QString& path)
    {
        return PDFRepositoryHelper::isValidStoredPath(path);
    }

    /// Reads one row of pinned_location
    static bool readLocation(const PDFDatabaseStatement& statement, PDFPinnedLocation& location)
    {
        location.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, std::numeric_limits<qint64>::max());
        location.path = PDFRepositoryHelper::readText(statement, 1, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
        if (location.id <= 0 || !PDFRepositoryHelper::isValidStoredPath(location.path))
        {
            return false;
        }

        location.name = PDFRepositoryHelper::sanitizeStoredName(PDFRepositoryHelper::readText(statement, 2, PDFRepositoryHelper::MAXIMUM_NAME_LENGTH * 4));
        if (location.name.isEmpty())
        {
            location.name = PDFRepositoryHelper::getFileName(location.path);
        }
        location.kind = PDFPinnedLocationKind(PDFRepositoryHelper::readInteger(statement, 3, 0, 0, 2));
        location.position = PDFRepositoryHelper::readInteger(statement, 4, 0, std::numeric_limits<qint64>::min(), std::numeric_limits<qint64>::max());
        return true;
    }
};

PDFRecentReadingState PDFRecentFile::getReadingState() const
{
    if (reachedEnd)
    {
        return PDFRecentReadingState::Finished;
    }
    if (lastPage > 1)
    {
        return PDFRecentReadingState::InProgress;
    }
    return PDFRecentReadingState::NotStarted;
}

qreal PDFRecentFile::getProgress() const
{
    switch (getReadingState())
    {
        case PDFRecentReadingState::Finished:
            return 1.0;

        case PDFRecentReadingState::InProgress:
            if (pageCount > 0)
            {
                return qBound(0.0, qreal(qMin(lastPage, pageCount)) / qreal(pageCount), 1.0);
            }
            return 0.0;

        case PDFRecentReadingState::NotStarted:
            break;
    }
    return 0.0;
}

QImage PDFRecentThumbnail::toImage() const
{
    if (png.isEmpty() || png.size() > PDFRecentRepository::MAXIMUM_THUMBNAIL_BYTES)
    {
        return QImage();
    }

    QBuffer buffer;
    buffer.setData(png);
    if (!buffer.open(QIODevice::ReadOnly))
    {
        return QImage();
    }

    QImageReader reader(&buffer, "png");
    const QSize size = reader.size();
    constexpr int limit = PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION;
    if (!size.isValid() || size.width() > limit || size.height() > limit)
    {
        return QImage();
    }

    QImage image;
    if (!reader.read(&image))
    {
        return QImage();
    }
    return image;
}

PDFRecentRepository::PDFRecentRepository(PDFDatabase* database, const PDFSettingRegistry* registry) :
    m_database(database),
    m_registry(registry)
{

}

qint64 PDFRecentRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

int PDFRecentRepository::getMaximumFiles() const
{
    if (!m_registry)
    {
        return 10;
    }

    const qint64 value = PDFSettingsRepository(m_database, m_registry).getInteger(PDFSettingKeys::recentMaxFiles());
    return int(std::clamp<qint64>(value, 1, 1000));
}

PDFDatabaseConnection* PDFRecentRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

bool PDFRecentRepository::readFile(const PDFDatabaseStatement& statement, PDFRecentFile& file)
{
    constexpr qint64 maximumInteger = std::numeric_limits<qint64>::max();

    file = PDFRecentFile();
    file.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, maximumInteger);
    file.path = PDFRepositoryHelper::readText(statement, 1, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
    if (file.id <= 0 || !PDFRepositoryHelper::isValidStoredPath(file.path))
    {
        return false;
    }

    file.name = PDFRepositoryHelper::sanitizeStoredName(PDFRepositoryHelper::readText(statement, 2, 4096));
    if (file.name.isEmpty())
    {
        file.name = PDFRepositoryHelper::getFileName(file.path);
    }

    file.folder = PDFRepositoryHelper::readText(statement, 3, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
    if (!PDFRepositoryHelper::isValidStoredPath(file.folder))
    {
        file.folder = PDFRepositoryHelper::getFolder(file.path);
    }

    file.lastOpened = PDFRepositoryHelper::readInteger(statement, 4, 0, 0, maximumInteger);
    file.openCount = PDFRepositoryHelper::readInteger(statement, 5, 0, 0, maximumInteger);
    file.pinned = PDFRepositoryHelper::readInteger(statement, 6, 0, 0, 1) != 0;
    file.pinOrder = file.pinned ? PDFRepositoryHelper::readInteger(statement, 7, 0, 0, maximumInteger) : 0;
    file.pageCount = int(PDFRepositoryHelper::readInteger(statement, 8, -1, 0, MAXIMUM_PAGE));
    file.fileSize = PDFRepositoryHelper::readInteger(statement, 9, -1, 0, maximumInteger);
    file.fileModified = PDFRepositoryHelper::readInteger(statement, 10, 0, 0, maximumInteger);
    file.lastPage = int(PDFRepositoryHelper::readInteger(statement, 11, 0, 0, MAXIMUM_PAGE));
    if (file.pageCount > 0 && file.lastPage > file.pageCount)
    {
        file.lastPage = file.pageCount;
    }
    file.reachedEnd = PDFRepositoryHelper::readInteger(statement, 12, 0, 0, 1) != 0;
    file.viewState = PDFRepositoryHelper::readText(statement, 13, MAXIMUM_VIEW_STATE_LENGTH);
    file.locked = PDFRepositoryHelper::readInteger(statement, 14, 0, 0, 1) != 0;
    file.hasThumbnail = PDFRepositoryHelper::readInteger(statement, 15, 0, 0, 1) != 0;
    file.thumbnailModified = PDFRepositoryHelper::readInteger(statement, 16, 0, 0, maximumInteger);
    file.thumbnailState = PDFRecentThumbnailState(PDFRepositoryHelper::readInteger(statement, 17, 0, 0, 3));
    return true;
}

std::vector<PDFRecentFile> PDFRecentRepository::readFiles(const char* sql) const
{
    std::vector<PDFRecentFile> files;
    PDFDatabaseStatement statement(getConnection(), sql);
    PDFRecentFile file;
    while (statement.next())
    {
        if (readFile(statement, file))
        {
            files.push_back(file);
        }
    }
    return files;
}

PDFDatabaseError PDFRecentRepository::pruneFiles(PDFDatabaseConnection* connection) const
{
    // Pinned files are never deleted (newapp_storage.md 4)
    PDFDatabaseStatement statement(connection, "DELETE FROM recent_file WHERE pinned IS NOT 1 AND id NOT IN "
                                               "(SELECT id FROM recent_file WHERE pinned IS NOT 1 ORDER BY last_opened DESC, id DESC LIMIT ?1)");
    statement.bindInt64(1, getMaximumFiles());
    statement.execute();
    return statement.getError();
}

PDFDatabaseError PDFRecentRepository::pruneFolders(PDFDatabaseConnection* connection) const
{
    PDFDatabaseStatement statement(connection, "DELETE FROM recent_folder WHERE kind IS NOT 1 AND kind IS NOT 2 AND kind IS NOT 3 AND path NOT IN "
                                               "(SELECT path FROM recent_folder WHERE kind IS NOT 1 AND kind IS NOT 2 AND kind IS NOT 3 ORDER BY last_used DESC LIMIT ?1)");
    statement.bindInt64(1, MAXIMUM_RECENT_FOLDERS);
    statement.execute();
    return statement.getError();
}

PDFDatabaseError PDFRecentRepository::recordFolderImpl(PDFDatabaseConnection* connection, const QString& folderPath) const
{
    const PDFRecentFolderKind kind = PDFRecentRepositoryHelper::getFolderKind(folderPath, getStandardFolders());
    PDFDatabaseStatement statement(connection, "INSERT INTO recent_folder(path, last_used, kind) VALUES (?1, ?2, ?3) "
                                               "ON CONFLICT(path) DO UPDATE SET last_used = excluded.last_used, kind = excluded.kind");
    statement.bindText(1, folderPath);
    statement.bindInt64(2, getCurrentTime());
    statement.bindInt64(3, int(kind));
    if (!statement.execute())
    {
        return statement.getError();
    }
    return pruneFolders(connection);
}

PDFDatabaseError PDFRecentRepository::recordOpened(const QString& filePath, QString* storedPath)
{
    if (storedPath)
    {
        storedPath->clear();
    }

    const QString path = PDFRepositoryHelper::normalizePath(filePath);
    if (path.isEmpty())
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    const QFileInfo info(path);
    const QString folder = PDFRepositoryHelper::getFolder(path);
    const qint64 fileSize = info.exists() ? info.size() : -1;
    const QDateTime modified = info.exists() ? info.lastModified(QTimeZone::UTC) : QDateTime();
    const qint64 fileModified = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // A hostile open_count (text, negative, huge) restarts the counter
    PDFDatabaseStatement statement(connection,
        "INSERT INTO recent_file(path, name, folder, last_opened, open_count, file_size, file_mtime) VALUES (?1, ?2, ?3, ?4, 1, ?5, ?6) "
        "ON CONFLICT(path) DO UPDATE SET name = excluded.name, folder = excluded.folder, last_opened = excluded.last_opened, "
        "open_count = CASE WHEN typeof(recent_file.open_count) = 'integer' AND recent_file.open_count BETWEEN 0 AND 1000000000 THEN recent_file.open_count + 1 ELSE 1 END, "
        "file_size = excluded.file_size, file_mtime = excluded.file_mtime");
    statement.bindText(1, path);
    statement.bindText(2, PDFRepositoryHelper::getFileName(path));
    statement.bindText(3, folder);
    statement.bindInt64(4, getCurrentTime());
    if (fileSize >= 0)
    {
        statement.bindInt64(5, fileSize);
    }
    else
    {
        statement.bindNull(5);
    }
    if (fileModified > 0)
    {
        statement.bindInt64(6, fileModified);
    }
    else
    {
        statement.bindNull(6);
    }

    if (!statement.execute())
    {
        return statement.getError();
    }

    PDFDatabaseError error = recordFolderImpl(connection, folder);
    if (error.isError())
    {
        return error;
    }

    error = pruneFiles(connection);
    if (error.isError())
    {
        return error;
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    if (storedPath)
    {
        *storedPath = path;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFRecentRepository::updateFile(const char* sql, const QString& path, const QVariant& first, const QVariant& second, const QVariant& third)
{
    if (!PDFRecentRepositoryHelper::isUsablePath(path))
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement exists(connection, "SELECT 1 FROM recent_file WHERE path = ?1");
    exists.bindText(1, path);
    const bool found = exists.next();
    exists.reset();
    if (exists.getError().isError())
    {
        return exists.getError();
    }
    if (!found)
    {
        return PDFRecentRepositoryHelper::notFound();
    }

    // ?1 = path, ?2 .. ?4 = values; the statements change the row only when a value
    // differs (no change notification without a change)
    PDFDatabaseStatement statement(connection, sql);
    statement.bindText(1, path);
    statement.bindVariant(2, first);
    if (second.isValid())
    {
        statement.bindVariant(3, second);
    }
    if (third.isValid())
    {
        statement.bindVariant(4, third);
    }
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

PDFDatabaseError PDFRecentRepository::removeFile(const QString& path)
{
    if (!PDFRecentRepositoryHelper::isUsablePath(path))
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM recent_file WHERE path = ?1");
    statement.bindText(1, path);
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

PDFDatabaseError PDFRecentRepository::setPinned(const QString& path, bool pinned)
{
    if (!PDFRecentRepositoryHelper::isUsablePath(path))
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement current(connection, "SELECT pinned IS 1 FROM recent_file WHERE path = ?1");
    current.bindText(1, path);
    if (!current.next())
    {
        const PDFDatabaseError error = current.getError();
        return error.isError() ? error : PDFRecentRepositoryHelper::notFound();
    }
    const bool isPinned = current.getBool(0);
    current.reset();

    if (isPinned == pinned)
    {
        return PDFDatabaseError();
    }

    if (pinned)
    {
        PDFDatabaseStatement statement(connection, "UPDATE recent_file SET pinned = 1, "
                                                   "pin_order = (SELECT COALESCE(MAX(CASE WHEN typeof(pin_order) = 'integer' AND pin_order BETWEEN 0 AND 1000000000 THEN pin_order END), 0) + 1 FROM recent_file WHERE pinned IS 1) "
                                                   "WHERE path = ?1");
        statement.bindText(1, path);
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    else
    {
        PDFDatabaseStatement statement(connection, "UPDATE recent_file SET pinned = 0, pin_order = NULL WHERE path = ?1");
        statement.bindText(1, path);
        if (!statement.execute())
        {
            return statement.getError();
        }

        // The unpinned file is subject to the limit of the recent files again
        const PDFDatabaseError error = pruneFiles(connection);
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

PDFDatabaseError PDFRecentRepository::setPinnedOrder(const QStringList& paths)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // Current order (inside of the transaction - another instance cannot change it now)
    QStringList current;
    {
        PDFDatabaseStatement statement(connection, "SELECT path FROM recent_file WHERE pinned IS 1 AND typeof(path) = 'text' "
                                                   "ORDER BY CASE WHEN typeof(pin_order) = 'integer' THEN pin_order END IS NULL, pin_order, last_opened DESC, id");
        while (statement.next())
        {
            current << statement.getText(0);
        }
        if (statement.getError().isError())
        {
            return statement.getError();
        }
    }

    QStringList order;
    for (const QString& path : paths)
    {
        if (current.contains(path) && !order.contains(path))
        {
            order << path;
        }
    }
    for (const QString& path : std::as_const(current))
    {
        if (!order.contains(path))
        {
            order << path;
        }
    }

    PDFDatabaseStatement statement(connection, "UPDATE recent_file SET pin_order = ?2 WHERE path = ?1 AND pin_order IS NOT ?2");
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

PDFDatabaseError PDFRecentRepository::setReadingState(const QString& path, int lastPage, int pageCount, bool reachedEnd)
{
    if (lastPage < 1 || lastPage > MAXIMUM_PAGE || pageCount < 0 || pageCount > MAXIMUM_PAGE || (pageCount > 0 && lastPage > pageCount))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "Invalid page of the document."));
    }

    // Finished stays Finished (reached_end is sticky)
    return updateFile("UPDATE recent_file SET last_page = ?2, page_count = CASE WHEN ?3 > 0 THEN ?3 ELSE page_count END, "
                      "reached_end = CASE WHEN reached_end IS 1 OR ?4 = 1 THEN 1 ELSE 0 END "
                      "WHERE path = ?1 AND (last_page IS NOT ?2 OR (?3 > 0 AND page_count IS NOT ?3) OR (?4 = 1 AND reached_end IS NOT 1))",
                      path, QVariant(qint64(lastPage)), QVariant(qint64(pageCount)), QVariant(qint64(reachedEnd ? 1 : 0)));
}

PDFDatabaseError PDFRecentRepository::setViewState(const QString& path, const QString& viewState)
{
    if (viewState.size() > MAXIMUM_VIEW_STATE_LENGTH)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "The view state is too long."));
    }

    return updateFile("UPDATE recent_file SET view_state = ?2 WHERE path = ?1 AND view_state IS NOT ?2", path, QVariant(viewState));
}

PDFDatabaseError PDFRecentRepository::setLocked(const QString& path, bool locked)
{
    return updateFile("UPDATE recent_file SET locked = ?2 WHERE path = ?1 AND locked IS NOT ?2", path, QVariant(qint64(locked ? 1 : 0)));
}

std::optional<PDFRecentFile> PDFRecentRepository::getFile(const QString& path) const
{
    if (!PDFRecentRepositoryHelper::isUsablePath(path))
    {
        return std::nullopt;
    }

    const QByteArray sql = QByteArray(PDFRecentRepositoryHelper::FILE_COLUMNS) + "AND path = ?1";
    PDFDatabaseStatement statement(getConnection(), sql.constData());
    statement.bindText(1, path);

    std::optional<PDFRecentFile> result;
    if (statement.next())
    {
        PDFRecentFile file;
        if (readFile(statement, file))
        {
            result = file;
        }
        statement.reset();
    }
    return result;
}

std::vector<PDFRecentFile> PDFRecentRepository::getFiles(const PDFRecentQuery& query) const
{
    const QByteArray sql = QByteArray(PDFRecentRepositoryHelper::FILE_COLUMNS) +
                           (query.pinnedOnly ? "AND pinned IS 1 " : "") +
                           "ORDER BY last_opened DESC, id DESC LIMIT 10000";
    std::vector<PDFRecentFile> files = readFiles(sql.constData());

    if (!query.text.trimmed().isEmpty())
    {
        std::vector<PDFRecentFile> filtered;
        for (PDFRecentFile& file : files)
        {
            if (PDFRepositoryHelper::containsText(file.name, query.text) ||
                PDFRepositoryHelper::containsText(file.path, query.text) ||
                PDFRepositoryHelper::containsText(QDir::toNativeSeparators(file.path), query.text))
            {
                filtered.push_back(std::move(file));
            }
        }
        files = std::move(filtered);
    }

    switch (query.sort)
    {
        case PDFRecentSort::LastOpened:
            break;

        case PDFRecentSort::Name:
            std::stable_sort(files.begin(), files.end(), PDFRecentRepositoryHelper::compareByName);
            break;

        case PDFRecentSort::Folder:
            std::stable_sort(files.begin(), files.end(), PDFRecentRepositoryHelper::compareByFolder);
            break;
    }

    if (query.limit >= 0 && files.size() > size_t(query.limit))
    {
        files.resize(size_t(query.limit));
    }
    return files;
}

std::vector<PDFRecentFile> PDFRecentRepository::getPinnedFiles() const
{
    const QByteArray sql = QByteArray(PDFRecentRepositoryHelper::FILE_COLUMNS) +
                           "AND pinned IS 1 ORDER BY CASE WHEN typeof(pin_order) = 'integer' THEN pin_order END IS NULL, pin_order, last_opened DESC, id LIMIT 10000";
    return readFiles(sql.constData());
}

std::vector<PDFRecentFile> PDFRecentRepository::getContinueReading(int limit) const
{
    std::vector<PDFRecentFile> result;
    if (limit <= 0)
    {
        return result;
    }

    const QByteArray sql = QByteArray(PDFRecentRepositoryHelper::FILE_COLUMNS) +
                           "AND typeof(last_page) = 'integer' AND last_page > 1 AND reached_end IS NOT 1 ORDER BY last_opened DESC, id DESC LIMIT 10000";
    for (const PDFRecentFile& file : readFiles(sql.constData()))
    {
        if (file.getReadingState() == PDFRecentReadingState::InProgress)
        {
            result.push_back(file);
            if (int(result.size()) >= limit)
            {
                break;
            }
        }
    }
    return result;
}

int PDFRecentRepository::getFileCount() const
{
    return int(getFiles().size());
}

int PDFRecentRepository::getPinnedCount() const
{
    return int(getPinnedFiles().size());
}

PDFRecentThumbnail PDFRecentRepository::getThumbnail(const QString& path) const
{
    PDFRecentThumbnail thumbnail;
    if (!PDFRecentRepositoryHelper::isUsablePath(path))
    {
        return thumbnail;
    }

    PDFDatabaseStatement statement(getConnection(), "SELECT CASE WHEN typeof(thumbnail) = 'blob' AND length(thumbnail) <= 2097152 THEN thumbnail END, "
                                                    "thumbnail_mtime, thumbnail_state FROM recent_file WHERE path = ?1");
    statement.bindText(1, path);
    if (statement.next())
    {
        if (!statement.isNull(0))
        {
            thumbnail.png = statement.getBlob(0);
        }
        thumbnail.fileModified = PDFRepositoryHelper::readInteger(statement, 1, 0, 0, std::numeric_limits<qint64>::max());
        thumbnail.state = PDFRecentThumbnailState(PDFRepositoryHelper::readInteger(statement, 2, 0, 0, 3));
        statement.reset();
    }

    // A picture without the state Ready is not shown
    if (thumbnail.state != PDFRecentThumbnailState::Ready)
    {
        thumbnail.png.clear();
    }
    return thumbnail;
}

PDFDatabaseError PDFRecentRepository::setThumbnail(const QString& path, const QByteArray& png, qint64 fileModified, PDFRecentThumbnailState state, int pageCount)
{
    if (png.size() > MAXIMUM_THUMBNAIL_BYTES || int(state) < 0 || int(state) > 3 || pageCount > MAXIMUM_PAGE || fileModified < 0)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "Invalid thumbnail."));
    }

    if (!PDFRecentRepositoryHelper::isUsablePath(path))
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // The row may have been removed meanwhile (Clear history): nothing is inserted
    PDFDatabaseStatement statement(connection,
        "UPDATE recent_file SET thumbnail = ?2, thumbnail_mtime = ?3, thumbnail_state = ?4, "
        "page_count = CASE WHEN ?5 >= 0 THEN ?5 ELSE page_count END "
        "WHERE path = ?1 AND (thumbnail IS NOT ?2 OR thumbnail_mtime IS NOT ?3 OR thumbnail_state IS NOT ?4 OR (?5 >= 0 AND page_count IS NOT ?5))");
    statement.bindText(1, path);
    if (png.isEmpty())
    {
        statement.bindNull(2);
    }
    else
    {
        statement.bindBlob(2, png);
    }
    statement.bindInt64(3, fileModified);
    statement.bindInt64(4, int(state));
    statement.bindInt64(5, pageCount);
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

std::vector<PDFRecentFolder> PDFRecentRepository::getStandardFolders()
{
    static const std::pair<QStandardPaths::StandardLocation, PDFRecentFolderKind> locations[] =
    {
        { QStandardPaths::DocumentsLocation, PDFRecentFolderKind::Documents },
        { QStandardPaths::DownloadLocation, PDFRecentFolderKind::Downloads },
        { QStandardPaths::DesktopLocation, PDFRecentFolderKind::Desktop },
    };

    std::vector<PDFRecentFolder> folders;
    for (const auto& location : locations)
    {
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(QStandardPaths::writableLocation(location.first)));
        if (!PDFRepositoryHelper::isValidStoredPath(path))
        {
            continue;
        }

        bool isDuplicate = false;
        for (const PDFRecentFolder& folder : folders)
        {
            isDuplicate = isDuplicate || PDFRepositoryHelper::isSamePath(folder.path, path);
        }

        // The home folder is not a standard folder (Linux without XDG user folders)
        if (isDuplicate || PDFRepositoryHelper::isSamePath(path, QDir::homePath()))
        {
            continue;
        }

        PDFRecentFolder folder;
        folder.path = path;
        folder.name = QStandardPaths::displayName(location.first);
        if (folder.name.isEmpty())
        {
            folder.name = PDFRepositoryHelper::getFileName(path);
        }
        folder.kind = location.second;
        folders.push_back(folder);
    }
    return folders;
}

std::vector<PDFRecentFolder> PDFRecentRepository::getUsedFolders() const
{
    std::vector<PDFRecentFolder> folders;
    const std::vector<PDFRecentFolder> standardFolders = getStandardFolders();

    PDFDatabaseStatement statement(getConnection(), "SELECT path, last_used FROM recent_folder "
                                                    "WHERE typeof(path) = 'text' AND length(path) BETWEEN 1 AND 32767 ORDER BY last_used DESC LIMIT 1000");
    while (statement.next())
    {
        PDFRecentFolder folder;
        folder.path = PDFRepositoryHelper::readText(statement, 0, PDFRepositoryHelper::MAXIMUM_PATH_LENGTH);
        if (!PDFRepositoryHelper::isValidStoredPath(folder.path))
        {
            continue;
        }

        folder.lastUsed = PDFRepositoryHelper::readInteger(statement, 1, 0, 0, std::numeric_limits<qint64>::max());

        // The kind is computed again: the standard folders of another computer or
        // user (a copied database) are ordinary folders here
        folder.kind = PDFRecentRepositoryHelper::getFolderKind(folder.path, standardFolders);
        folder.name = PDFRepositoryHelper::getFileName(folder.path);
        for (const PDFRecentFolder& standardFolder : standardFolders)
        {
            if (standardFolder.kind == folder.kind && folder.isStandard())
            {
                folder.name = standardFolder.name;
            }
        }
        folders.push_back(folder);
    }

    std::stable_sort(folders.begin(), folders.end(), PDFRecentRepositoryHelper::compareFoldersByUse);
    return folders;
}

std::vector<PDFRecentFolder> PDFRecentRepository::getFolders() const
{
    std::vector<PDFRecentFolder> folders = getStandardFolders();
    const std::vector<PDFRecentFolder> usedFolders = getUsedFolders();

    // Time of use of the standard folders
    for (PDFRecentFolder& folder : folders)
    {
        for (const PDFRecentFolder& usedFolder : usedFolders)
        {
            if (usedFolder.kind == folder.kind)
            {
                folder.lastUsed = qMax(folder.lastUsed, usedFolder.lastUsed);
            }
        }
    }

    int otherCount = 0;
    for (const PDFRecentFolder& usedFolder : usedFolders)
    {
        if (!usedFolder.isStandard() && otherCount < MAXIMUM_RECENT_FOLDERS)
        {
            folders.push_back(usedFolder);
            ++otherCount;
        }
    }
    return folders;
}

PDFDatabaseError PDFRecentRepository::recordFolder(const QString& folderPath)
{
    const QString path = PDFRepositoryHelper::normalizePath(folderPath);
    if (path.isEmpty())
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const PDFDatabaseError error = recordFolderImpl(connection, path);
    if (error.isError())
    {
        return error;
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFRecentRepository::removeFolder(const QString& folderPath)
{
    if (!PDFRecentRepositoryHelper::isUsablePath(folderPath))
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM recent_folder WHERE path = ?1");
    statement.bindText(1, folderPath);
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

std::vector<PDFPinnedLocation> PDFRecentRepository::getPinnedLocations() const
{
    std::vector<PDFPinnedLocation> locations;
    PDFDatabaseStatement statement(getConnection(),
        "SELECT id, CASE WHEN typeof(path) = 'text' AND length(path) <= 32767 THEN path END, CASE WHEN typeof(name) = 'text' AND length(name) <= 1024 THEN name END, kind, position "
        "FROM pinned_location ORDER BY CASE WHEN typeof(position) = 'integer' THEN position END IS NULL, position, id LIMIT 1000");
    while (statement.next())
    {
        PDFPinnedLocation location;
        if (PDFRecentRepositoryHelper::readLocation(statement, location))
        {
            locations.push_back(location);
        }
    }
    return locations;
}

bool PDFRecentRepository::hasPinnedLocation(const QString& folderPath) const
{
    for (const PDFPinnedLocation& location : getPinnedLocations())
    {
        if (PDFRepositoryHelper::isSamePath(location.path, folderPath))
        {
            return true;
        }
    }
    return false;
}

PDFDatabaseError PDFRecentRepository::addPinnedLocation(const QString& folderPath, const QString& name, qint64* id, bool* isDuplicate)
{
    if (id)
    {
        *id = 0;
    }
    if (isDuplicate)
    {
        *isDuplicate = false;
    }

    const QString path = PDFRepositoryHelper::normalizePath(folderPath);
    if (path.isEmpty())
    {
        return PDFRecentRepositoryHelper::invalidPath();
    }

    QString locationName = name.isEmpty() ? PDFRepositoryHelper::getFileName(path) : PDFRepositoryHelper::normalizeName(name);
    if (locationName.isEmpty())
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "The name is not valid."));
    }
    locationName = PDFRepositoryHelper::sanitizeStoredName(locationName);

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // Duplicate check inside of the write transaction (two instances adding the same folder)
    if (hasPinnedLocation(path))
    {
        if (isDuplicate)
        {
            *isDuplicate = true;
        }
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "The location is already pinned."));
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO pinned_location(path, name, kind, position) VALUES (?1, ?2, ?3, "
                                               "(SELECT COALESCE(MAX(CASE WHEN typeof(position) = 'integer' AND position BETWEEN 0 AND 1000000000 THEN position END), 0) + 1 FROM pinned_location))");
    statement.bindText(1, path);
    statement.bindText(2, locationName);
    statement.bindInt64(3, int(detectLocationKind(path)));
    if (!statement.execute())
    {
        return statement.getError();
    }
    const qint64 newId = connection->getLastInsertRowId();

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    if (id)
    {
        *id = newId;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFRecentRepository::renamePinnedLocation(qint64 id, const QString& name)
{
    const QString locationName = PDFRepositoryHelper::normalizeName(name);
    if (locationName.isEmpty())
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFRecentRepository", "The name is not valid."));
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "UPDATE pinned_location SET name = ?2 WHERE id = ?1 AND name IS NOT ?2");
    statement.bindInt64(1, id);
    statement.bindText(2, locationName);
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

PDFDatabaseError PDFRecentRepository::removePinnedLocation(qint64 id)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM pinned_location WHERE id = ?1");
    statement.bindInt64(1, id);
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

PDFDatabaseError PDFRecentRepository::setPinnedLocationOrder(const QList<qint64>& ids)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    QList<qint64> current;
    for (const PDFPinnedLocation& location : getPinnedLocations())
    {
        current << location.id;
    }

    QList<qint64> order;
    for (qint64 id : ids)
    {
        if (current.contains(id) && !order.contains(id))
        {
            order << id;
        }
    }
    for (qint64 id : std::as_const(current))
    {
        if (!order.contains(id))
        {
            order << id;
        }
    }

    PDFDatabaseStatement statement(connection, "UPDATE pinned_location SET position = ?2 WHERE id = ?1 AND position IS NOT ?2");
    for (int i = 0; i < order.size(); ++i)
    {
        statement.bindInt64(1, order[i]);
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

PDFPinnedLocationKind PDFRecentRepository::detectLocationKind(const QString& folderPath)
{
    // Backslashes are separators on every system here (a path of Windows in a database
    // used on Linux is classified the same way)
    QString path = folderPath;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // UNC paths, URLs of network file systems and the mounts of GVFS (GNOME)
    const bool isGvfsMount = path.startsWith(QLatin1String("/run/user/")) && path.contains(QLatin1String("/gvfs/"));
    if (path.startsWith(QLatin1String("//")) ||
        path.startsWith(QLatin1String("smb:"), Qt::CaseInsensitive) ||
        path.startsWith(QLatin1String("nfs:"), Qt::CaseInsensitive) ||
        isGvfsMount)
    {
        return PDFPinnedLocationKind::Network;
    }

    static const char* const cloudFolders[] =
    {
        "OneDrive", "Dropbox", "Google Drive", "My Drive", "iCloud Drive", "iCloudDrive", "Nextcloud", "ownCloud", "pCloud Drive", "MEGA", "Box", "Box Sync"
    };

    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        for (const char* cloudFolder : cloudFolders)
        {
            const QLatin1String name(cloudFolder);
            // "OneDrive", "OneDrive - Company", "Dropbox (Personal)"
            if (part.compare(name, Qt::CaseInsensitive) == 0 ||
                part.startsWith(name + QLatin1String(" - "), Qt::CaseInsensitive) ||
                part.startsWith(name + QLatin1String(" ("), Qt::CaseInsensitive))
            {
                return PDFPinnedLocationKind::Cloud;
            }
        }

        // macOS File Provider folders (~/Library/CloudStorage/...)
        if (part == QLatin1String("CloudStorage"))
        {
            return PDFPinnedLocationKind::Cloud;
        }
    }

    return PDFPinnedLocationKind::Local;
}

QString PDFRecentRepository::getLocationKindText(PDFPinnedLocationKind kind)
{
    switch (kind)
    {
        case PDFPinnedLocationKind::Local:
            return QCoreApplication::translate("pdf::PDFRecentRepository", "This PC");
        case PDFPinnedLocationKind::Cloud:
            return QCoreApplication::translate("pdf::PDFRecentRepository", "Personal cloud");
        case PDFPinnedLocationKind::Network:
            return QCoreApplication::translate("pdf::PDFRecentRepository", "Network");
    }
    return QString();
}

PDFRecentClearSummary PDFRecentRepository::getClearHistorySummary(bool includePinned) const
{
    PDFRecentClearSummary summary;
    const std::vector<PDFRecentFile> files = getFiles();
    for (const PDFRecentFile& file : files)
    {
        if (file.pinned)
        {
            ++summary.pinnedFiles;
        }
        if (!file.pinned || includePinned)
        {
            ++summary.removedFiles;
        }
    }

    summary.removedFolders = int(getUsedFolders().size());
    summary.hasContinueReading = !getContinueReading(1).empty();

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseStatement workspaces(connection, "SELECT COUNT(*) FROM workspace WHERE is_last_session IS NOT 1 AND opened IS NOT NULL");
    if (workspaces.next())
    {
        summary.removedWorkspaces = int(std::clamp<qint64>(workspaces.getInt64(0), 0, std::numeric_limits<int>::max()));
        workspaces.reset();
    }

    PDFDatabaseStatement lastSession(connection, "SELECT COUNT(*) FROM workspace WHERE is_last_session IS 1");
    if (lastSession.next())
    {
        summary.hasLastSession = lastSession.getInt64(0) > 0;
        lastSession.reset();
    }

    return summary;
}

PDFDatabaseError PDFRecentRepository::clearHistory(bool includePinned)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    static const char* const statements[] =
    {
        // Files (the pinned ones only with the option)
        "DELETE FROM recent_file WHERE pinned IS NOT 1 OR ?1 = 1",

        // Kept pinned files forget where the reading stopped (Continue where you left off)
        "UPDATE recent_file SET last_page = NULL, reached_end = 0 WHERE last_page IS NOT NULL OR reached_end IS NOT 0",

        // Folders opened from
        "DELETE FROM recent_folder",

        // Saved workspaces leave Recent, they stay in Open › Workspaces
        "UPDATE workspace SET opened = NULL WHERE is_last_session IS NOT 1 AND opened IS NOT NULL",

        // Restore last session
        "DELETE FROM workspace WHERE is_last_session IS 1",
    };

    for (size_t i = 0; i < std::size(statements); ++i)
    {
        PDFDatabaseStatement statement(connection, statements[i]);
        if (i == 0)
        {
            statement.bindInt64(1, includePinned ? 1 : 0);
        }
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

QString PDFRecentRepository::getSortId(PDFRecentSort sort)
{
    switch (sort)
    {
        case PDFRecentSort::LastOpened:
            return QStringLiteral("last-opened");
        case PDFRecentSort::Name:
            return QStringLiteral("name");
        case PDFRecentSort::Folder:
            return QStringLiteral("folder");
    }
    return QString();
}

std::optional<PDFRecentSort> PDFRecentRepository::parseSort(QStringView id)
{
    for (PDFRecentSort sort : { PDFRecentSort::LastOpened, PDFRecentSort::Name, PDFRecentSort::Folder })
    {
        if (id == getSortId(sort))
        {
            return sort;
        }
    }
    return std::nullopt;
}

}   // namespace pdf
