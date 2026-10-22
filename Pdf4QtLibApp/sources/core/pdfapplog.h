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

#ifndef PDFAPPLOG_H
#define PDFAPPLOG_H

#include "pdfappglobal.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace pdf
{

/// Parameters of the log
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLogOptions
{
public:
    /// Folder of the log files (PDFAppPaths::getLogDirectory)
    QString directory;

    /// Portable mode: nothing is written to the disk, messages go only to the
    /// previous handler (stderr)
    bool portable = false;

    /// Files of the last days are kept (newapplication.md 3: 7 days)
    int retentionDays = 7;

    /// Maximal size of all log files together (10 MB)
    qint64 maximumTotalSize = 10 * 1024 * 1024;
};

/// Log of the application (newapplication.md 3, Prostředí aplikace): a message
/// handler (qInstallMessageHandler) writes every message into
/// <directory>/pdf4qt-<yyyy-MM-dd>.log and passes it to the previous handler.
/// It is thread safe; a new file is started at midnight. Files older than the
/// retention are deleted at the start and at the change of the day, the oldest
/// files are deleted, when the total size exceeds the limit, and the writing stops
/// for the rest of the day, when the file of the day alone reaches it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLog
{
public:
    PDFAppLog() = delete;

    /// Installs the message handler. \returns true, if messages are written to a file.
    static bool install(const PDFAppLogOptions& options);

    /// Restores the previous message handler and closes the file
    static void uninstall();

    /// The handler is installed
    static bool isInstalled();

    /// Folder of the log files (About shows it), empty in the portable mode and
    /// when no log is installed (also after uninstall)
    static QString getLogDirectory();

    /// The file, into which the messages are written now (empty = none)
    static QString getCurrentLogFile();

    /// Name of the log file of the day
    static QString getFileName(const QDate& date);

    /// Date of the log file name (invalid date for another file)
    static QDate getFileDate(const QString& fileName);

    /// Deletes the log files of \p directory older than the retention and the oldest
    /// ones above the size limit (never the file of \p today, unless it alone is
    /// larger than the limit). \returns the deleted files.
    static QStringList cleanup(const QString& directory, const QDate& today, int retentionDays, qint64 maximumTotalSize);

    /// Writes the buffered data to the disk
    static void flush();

    /// Time used by the log instead of the time of the system (tests of the
    /// rotation); an invalid time restores the time of the system
    static void setCurrentDateTimeForTesting(const QDateTime& dateTime);
};

}   // namespace pdf

#endif // PDFAPPLOG_H
