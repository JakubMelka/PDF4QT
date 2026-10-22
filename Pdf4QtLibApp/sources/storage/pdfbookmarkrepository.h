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

#ifndef PDFBOOKMARKREPOSITORY_H
#define PDFBOOKMARKREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QByteArray>
#include <QPointer>
#include <QString>

#include <vector>

namespace pdf
{

/// Personal bookmark of a page of a file (table bookmark, newapp_storage.md 2)
class PDF4QTLIBAPPSHARED_EXPORT PDFBookmark
{
public:
    qint64 id = 0;          ///< Row id
    QString filePath;       ///< Canonical path of the file
    qint64 page = 0;        ///< Page index from 0
    QString title;          ///< Title given by the user (may be empty)
};

/// Bookmark of an exported or imported file (page from 0)
class PDF4QTLIBAPPSHARED_EXPORT PDFBookmarkEntry
{
public:
    qint64 page = 0;
    QString title;
};

/// Personal bookmarks ("My Bookmarks", 195:3445) by the canonical path of the
/// file. Only the bookmarks of the user are stored (auto = 0); the bookmarks
/// generated from the outline (bookmarks.autoGenerate) are made by the panel at
/// run time, rows with auto = 1 written by another version are skipped. A value
/// object: reads go to the database, writes are short BEGIN IMMEDIATE
/// transactions, notified with the topic getTopic (key = file path). Rows are
/// untrusted input: wrong types, negative or huge pages, too long titles and
/// invalid paths are skipped or bounded. Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFBookmarkRepository
{
public:
    static QString getTopic() { return QStringLiteral("bookmark"); }

    /// Longest title (a longer one is shortened)
    static constexpr int MAXIMUM_TITLE_LENGTH = 256;

    /// Most bookmarks of a file (more are not read, an addition is refused)
    static constexpr int MAXIMUM_BOOKMARKS = 10000;

    /// Largest page index stored
    static constexpr qint64 MAXIMUM_PAGE = 100000000;

    /// Largest file of an import
    static constexpr qint64 MAXIMUM_IMPORT_SIZE = 4 * 1024 * 1024;

    explicit PDFBookmarkRepository(PDFDatabase* database);

    /// Bookmarks of the file in their order (position, then id)
    std::vector<PDFBookmark> getBookmarks(const QString& filePath) const;

    /// Adds a bookmark of the page; it is placed before the first bookmark of a
    /// later page (the list stays in the order of the pages, unless the user has
    /// reordered it). \p id receives the row id.
    PDFDatabaseError addBookmark(const QString& filePath, qint64 page, const QString& title, qint64* id = nullptr);

    /// Renames the bookmark
    PDFDatabaseError renameBookmark(qint64 id, const QString& title);

    /// Removes the bookmark
    PDFDatabaseError removeBookmark(qint64 id);

    /// Sets the order of the bookmarks of the file: the listed ids first, the
    /// other bookmarks of the file after them in their order
    PDFDatabaseError setOrder(const QString& filePath, const std::vector<qint64>& ids);

    /// Adds the entries (an entry with the page and the title of an existing
    /// bookmark is skipped, the count of the file is bounded by MAXIMUM_BOOKMARKS);
    /// \p added receives the number of the added bookmarks
    PDFDatabaseError importBookmarks(const QString& filePath, const std::vector<PDFBookmarkEntry>& entries, int* added = nullptr);

    /// JSON of the bookmarks: {"format": "pdf4qt-bookmarks", "version": 1,
    /// "bookmarks": [{"page": 1, "title": "..."}]} (pages from 1)
    static QByteArray toJson(const std::vector<PDFBookmark>& bookmarks);

    /// Parses the JSON of toJson (untrusted: at most MAXIMUM_IMPORT_SIZE bytes and
    /// MAXIMUM_BOOKMARKS entries, invalid entries are skipped, pages from 1 up to
    /// \p pageCount). \returns false with the translated error for a file, which
    /// is not such a JSON.
    static bool fromJson(const QByteArray& data, qint64 pageCount, std::vector<PDFBookmarkEntry>* entries, QString* error);

    /// Title of the user: control characters replaced by spaces, the ends trimmed,
    /// shortened to MAXIMUM_TITLE_LENGTH
    static QString normalizeTitle(const QString& title);

private:
    PDFDatabaseConnection* getConnection() const;
    static bool isValidPath(const QString& filePath);

    QPointer<PDFDatabase> m_database;
};

}   // namespace pdf

#endif // PDFBOOKMARKREPOSITORY_H
