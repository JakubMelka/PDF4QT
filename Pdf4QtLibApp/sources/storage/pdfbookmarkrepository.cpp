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

#include "pdfbookmarkrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace pdf
{

/// Helpers of the repository of the bookmarks
class PDFBookmarkRepositoryHelper
{
public:
    PDFBookmarkRepositoryHelper() = delete;

    static QString getFormatName() { return QStringLiteral("pdf4qt-bookmarks"); }
    static constexpr int FORMAT_VERSION = 1;

    static PDFDatabaseError invalidFile()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFBookmarkRepository", "The document has no file."));
    }

    static PDFDatabaseError invalidPage()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFBookmarkRepository", "The page is not valid."));
    }

    static PDFDatabaseError tooMany()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFBookmarkRepository", "The document has too many bookmarks."));
    }

    /// Comparator of the bookmarks by their pages
    static bool isPageBefore(const PDFBookmark& left, const PDFBookmark& right)
    {
        return left.page < right.page;
    }

    /// Writes the positions 1..n in the order of the ids
    static PDFDatabaseError writeOrder(PDFDatabaseConnection* connection, const std::vector<qint64>& ids)
    {
        PDFDatabaseStatement statement(connection, "UPDATE bookmark SET position = ?2 WHERE id = ?1 AND position IS NOT ?2");
        for (size_t i = 0; i < ids.size(); ++i)
        {
            statement.bindInt64(1, ids[i]);
            statement.bindInt64(2, qint64(i) + 1);
            if (!statement.execute())
            {
                return statement.getError();
            }
        }
        return PDFDatabaseError();
    }
};

PDFBookmarkRepository::PDFBookmarkRepository(PDFDatabase* database) :
    m_database(database)
{

}

PDFDatabaseConnection* PDFBookmarkRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

bool PDFBookmarkRepository::isValidPath(const QString& filePath)
{
    return PDFRepositoryHelper::isValidStoredPath(filePath);
}

QString PDFBookmarkRepository::normalizeTitle(const QString& title)
{
    QString result;
    result.reserve(qMin<qsizetype>(title.size(), MAXIMUM_TITLE_LENGTH));
    for (const QChar character : title)
    {
        result.append((character.category() == QChar::Other_Control || character == QChar::LineSeparator || character == QChar::ParagraphSeparator) ? QChar(u' ') : character);
        if (result.size() >= MAXIMUM_TITLE_LENGTH + 1)
        {
            break;
        }
    }

    result = result.trimmed();
    if (result.size() > MAXIMUM_TITLE_LENGTH)
    {
        result.truncate(MAXIMUM_TITLE_LENGTH);
        // A cut surrogate pair is not kept
        if (!result.isEmpty() && result.back().isHighSurrogate())
        {
            result.chop(1);
        }
    }
    return result;
}

std::vector<PDFBookmark> PDFBookmarkRepository::getBookmarks(const QString& filePath) const
{
    std::vector<PDFBookmark> bookmarks;
    if (!isValidPath(filePath))
    {
        return bookmarks;
    }

    PDFDatabaseStatement statement(getConnection(), "SELECT id, page, CASE WHEN typeof(title) = 'text' AND length(title) <= 4096 THEN title END FROM bookmark "
                                                    "WHERE file_path = ?1 AND auto IS NOT 1 "
                                                    "ORDER BY CASE WHEN typeof(position) = 'integer' THEN position END IS NULL, position, id LIMIT ?2");
    statement.bindText(1, filePath);
    statement.bindInt64(2, MAXIMUM_BOOKMARKS);
    while (statement.next())
    {
        PDFBookmark bookmark;
        bookmark.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, std::numeric_limits<qint64>::max());
        bookmark.page = PDFRepositoryHelper::readInteger(statement, 1, -1, 0, MAXIMUM_PAGE);
        if (bookmark.id <= 0 || bookmark.page < 0)
        {
            continue;
        }

        bookmark.filePath = filePath;
        bookmark.title = normalizeTitle(PDFRepositoryHelper::readText(statement, 2, 4096));
        bookmarks.push_back(std::move(bookmark));
    }
    return bookmarks;
}

PDFDatabaseError PDFBookmarkRepository::addBookmark(const QString& filePath, qint64 page, const QString& title, qint64* id)
{
    if (id)
    {
        *id = 0;
    }
    if (!isValidPath(filePath))
    {
        return PDFBookmarkRepositoryHelper::invalidFile();
    }
    if (page < 0 || page > MAXIMUM_PAGE)
    {
        return PDFBookmarkRepositoryHelper::invalidPage();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const std::vector<PDFBookmark> bookmarks = getBookmarks(filePath);
    if (bookmarks.size() >= size_t(MAXIMUM_BOOKMARKS))
    {
        return PDFBookmarkRepositoryHelper::tooMany();
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO bookmark(file_path, page, title, auto, position) VALUES (?1, ?2, ?3, 0, 0)");
    statement.bindText(1, filePath);
    statement.bindInt64(2, page);
    statement.bindText(3, normalizeTitle(title));
    if (!statement.execute())
    {
        return statement.getError();
    }

    const qint64 newId = connection->getLastInsertRowId();

    // The new bookmark goes before the first bookmark of a later page
    std::vector<qint64> order;
    order.reserve(bookmarks.size() + 1);
    bool isPlaced = false;
    for (const PDFBookmark& bookmark : bookmarks)
    {
        if (!isPlaced && bookmark.page > page)
        {
            order.push_back(newId);
            isPlaced = true;
        }
        order.push_back(bookmark.id);
    }
    if (!isPlaced)
    {
        order.push_back(newId);
    }

    PDFDatabaseError error = PDFBookmarkRepositoryHelper::writeOrder(connection, order);
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
        *id = newId;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFBookmarkRepository::renameBookmark(qint64 id, const QString& title)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "UPDATE bookmark SET title = ?2 WHERE id = ?1 AND title IS NOT ?2");
    statement.bindInt64(1, id);
    statement.bindText(2, normalizeTitle(title));
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

PDFDatabaseError PDFBookmarkRepository::removeBookmark(qint64 id)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM bookmark WHERE id = ?1");
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

PDFDatabaseError PDFBookmarkRepository::setOrder(const QString& filePath, const std::vector<qint64>& ids)
{
    if (!isValidPath(filePath))
    {
        return PDFBookmarkRepositoryHelper::invalidFile();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    const std::vector<PDFBookmark> bookmarks = getBookmarks(filePath);
    std::set<qint64> existing;
    for (const PDFBookmark& bookmark : bookmarks)
    {
        existing.insert(bookmark.id);
    }

    // Only the ids of the file, each once, then the remaining ones in their order
    std::vector<qint64> order;
    std::set<qint64> used;
    for (const qint64 id : ids)
    {
        if (existing.count(id) && used.insert(id).second)
        {
            order.push_back(id);
        }
    }
    for (const PDFBookmark& bookmark : bookmarks)
    {
        if (used.insert(bookmark.id).second)
        {
            order.push_back(bookmark.id);
        }
    }

    PDFDatabaseError error = PDFBookmarkRepositoryHelper::writeOrder(connection, order);
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

PDFDatabaseError PDFBookmarkRepository::importBookmarks(const QString& filePath, const std::vector<PDFBookmarkEntry>& entries, int* added)
{
    if (added)
    {
        *added = 0;
    }
    if (!isValidPath(filePath))
    {
        return PDFBookmarkRepositoryHelper::invalidFile();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    std::vector<PDFBookmark> bookmarks = getBookmarks(filePath);
    std::set<std::pair<qint64, QString>> existing;
    for (const PDFBookmark& bookmark : bookmarks)
    {
        existing.insert(std::make_pair(bookmark.page, bookmark.title));
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO bookmark(file_path, page, title, auto, position) VALUES (?1, ?2, ?3, 0, 0)");
    int count = 0;
    for (const PDFBookmarkEntry& entry : entries)
    {
        if (bookmarks.size() >= size_t(MAXIMUM_BOOKMARKS))
        {
            break;
        }

        const QString title = normalizeTitle(entry.title);
        if (entry.page < 0 || entry.page > MAXIMUM_PAGE || !existing.insert(std::make_pair(entry.page, title)).second)
        {
            continue;
        }

        statement.bindText(1, filePath);
        statement.bindInt64(2, entry.page);
        statement.bindText(3, title);
        if (!statement.execute())
        {
            return statement.getError();
        }

        PDFBookmark bookmark;
        bookmark.id = connection->getLastInsertRowId();
        bookmark.page = entry.page;
        bookmarks.push_back(bookmark);
        ++count;
    }

    // The imported bookmarks are sorted into the list by their pages
    std::vector<PDFBookmark> sorted = bookmarks;
    std::stable_sort(sorted.begin(), sorted.end(), &PDFBookmarkRepositoryHelper::isPageBefore);
    std::vector<qint64> order;
    order.reserve(sorted.size());
    for (const PDFBookmark& bookmark : sorted)
    {
        order.push_back(bookmark.id);
    }

    if (count > 0)
    {
        PDFDatabaseError error = PDFBookmarkRepositoryHelper::writeOrder(connection, order);
        if (error.isError())
        {
            return error;
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    if (added)
    {
        *added = count;
    }
    return PDFDatabaseError();
}

QByteArray PDFBookmarkRepository::toJson(const std::vector<PDFBookmark>& bookmarks)
{
    QJsonArray array;
    for (const PDFBookmark& bookmark : bookmarks)
    {
        QJsonObject object;
        object.insert(QStringLiteral("page"), bookmark.page + 1);
        object.insert(QStringLiteral("title"), bookmark.title);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), PDFBookmarkRepositoryHelper::getFormatName());
    root.insert(QStringLiteral("version"), PDFBookmarkRepositoryHelper::FORMAT_VERSION);
    root.insert(QStringLiteral("bookmarks"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool PDFBookmarkRepository::fromJson(const QByteArray& data, qint64 pageCount, std::vector<PDFBookmarkEntry>* entries, QString* error)
{
    entries->clear();
    const QString notBookmarks = QCoreApplication::translate("pdf::PDFBookmarkRepository", "The file is not a file of exported bookmarks.");
    if (data.size() > MAXIMUM_IMPORT_SIZE)
    {
        *error = QCoreApplication::translate("pdf::PDFBookmarkRepository", "The file is too large.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        *error = notBookmarks;
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue array = root.value(QStringLiteral("bookmarks"));
    if (root.value(QStringLiteral("format")).toString() != PDFBookmarkRepositoryHelper::getFormatName() || !array.isArray())
    {
        *error = notBookmarks;
        return false;
    }

    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (version < 1 || version > PDFBookmarkRepositoryHelper::FORMAT_VERSION)
    {
        *error = QCoreApplication::translate("pdf::PDFBookmarkRepository", "The file was exported by a newer version of PDF4QT.");
        return false;
    }

    for (const QJsonValue& value : array.toArray())
    {
        if (entries->size() >= size_t(MAXIMUM_BOOKMARKS))
        {
            break;
        }

        const QJsonObject object = value.toObject();
        const QJsonValue pageValue = object.value(QStringLiteral("page"));
        if (!pageValue.isDouble())
        {
            continue;
        }

        const double page = pageValue.toDouble();
        if (!(page >= 1.0) || page > double(qMin(pageCount, MAXIMUM_PAGE)) || page != std::floor(page))
        {
            continue;
        }

        PDFBookmarkEntry entry;
        entry.page = qint64(page) - 1;
        entry.title = normalizeTitle(object.value(QStringLiteral("title")).toString());
        entries->push_back(std::move(entry));
    }

    error->clear();
    return true;
}

}   // namespace pdf
