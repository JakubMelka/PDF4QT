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

#ifndef PDFRECENTASSEMBLYREPOSITORY_H
#define PDFRECENTASSEMBLYREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QString>

#include <vector>

namespace pdf
{

/// A recent assembly file (409:32023: the name, the folder, the date, the pages)
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentAssembly
{
public:
    QString path;           ///< Absolute path of the .pdf4qtassembly file (the key)
    QString name;           ///< Name of the assembly (the file name without the suffix)
    QString folder;         ///< Folder of the file
    qint64 lastOpened = 0;  ///< Time of the last opening or saving (ms since the epoch, UTC)
    int pageCount = -1;     ///< Pages of the result of the assembly, -1 = unknown
    bool pinned = false;    ///< Pinned (never pruned)
};

/// The recent assembly files of Manage Pages (plan of step 3b of phase 2, 4.6, 9.3; table
/// recent_assembly of migration 2; package M4): the menu of the assembly 409:32023, the empty
/// state 409:32739 and the page Open read it. The skeleton declares it, package M4 defines it.
///
/// Contract (as PDFRecentRepository): a value object, every read goes to the database (no
/// cache), every write is one short BEGIN IMMEDIATE transaction, whose commit wakes
/// PDFDatabaseNotifier (topic getTopic, the key is the path). Usable in any thread (each thread
/// has its own connection); functions never throw, errors of the database are returned; rows are
/// untrusted (another version, a damaged file): invalid rows are skipped, at most
/// MAXIMUM_ROWS are read. The non-pinned rows over MAXIMUM_ASSEMBLIES are pruned by a write.
class PDF4QTLIBAPPSHARED_EXPORT PDFRecentAssemblyRepository
{
public:
    /// Most non-pinned recent assemblies kept
    static constexpr int MAXIMUM_ASSEMBLIES = 20;

    /// Most rows, which a list reads
    static constexpr int MAXIMUM_ROWS = 1000;

    static QString getTopic() { return QStringLiteral("recent_assembly"); }

    explicit PDFRecentAssemblyRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// The recent assemblies, the pinned first, then by the time of the opening (newest first);
    /// at most \p limit (0 = MAXIMUM_ROWS)
    std::vector<PDFRecentAssembly> getAssemblies(int limit = 0) const;

    /// Records an opened or saved assembly file: the canonical absolute path, its name and folder,
    /// the time now, the pages of its result (-1 = unknown)
    PDFDatabaseError recordOpened(const QString& filePath, int pageCount);

    /// Removes the file from the list
    PDFDatabaseError remove(const QString& filePath);

    /// Pins or unpins the file
    PDFDatabaseError setPinned(const QString& filePath, bool pinned);

private:
    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFRECENTASSEMBLYREPOSITORY_H
