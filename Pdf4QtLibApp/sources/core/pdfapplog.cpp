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

#include "pdfapplog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>

#include <algorithm>
#include <atomic>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace pdf
{

/// State of the installed log (one per process)
class PDFAppLogState
{
public:
    QMutex mutex;
    bool isInstalled = false;
    PDFAppLogOptions options;
    /// Read without the mutex by a message of the log itself (the mutex is held then)
    std::atomic<QtMessageHandler> previousHandler = nullptr;
    QFile file;
    QString filePath;
    QDate fileDate;
    qint64 otherFilesSize = 0;
    bool isLimitReached = false;
    QDateTime testDateTime;
};

/// Helper functions of the log
class PDFAppLogHelper
{
public:
    PDFAppLogHelper() = delete;

    static PDFAppLogState& getState();
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);

    /// Opens the file of the day (mutex locked)
    static void openFile(PDFAppLogState& state, const QDate& date);

    /// Opens the file for appending. More instances write into the file of the day
    /// at once: every write must append atomically (O_APPEND, FILE_APPEND_DATA).
    static bool openForAppend(QFile& file, const QString& filePath);

    /// Text of the line of the log
    static QByteArray createLine(const QDateTime& time, const char* type, const char* category, const QString& message);

    /// Current time (mutex locked)
    static QDateTime getCurrentDateTime(const PDFAppLogState& state);

    static const char* getTypeName(QtMsgType type);

    /// Log file found by the cleanup
    struct LogFile
    {
        QString path;
        QDate date;
        qint64 size = 0;
    };

    /// Older files first
    static bool isOlder(const LogFile& left, const LogFile& right) { return left.date < right.date; }

    /// The handler is running in this thread (a message of the log itself)
    static thread_local bool s_isInHandler;
};

thread_local bool PDFAppLogHelper::s_isInHandler = false;

PDFAppLogState& PDFAppLogHelper::getState()
{
    static PDFAppLogState state;
    return state;
}

QDateTime PDFAppLogHelper::getCurrentDateTime(const PDFAppLogState& state)
{
    return state.testDateTime.isValid() ? state.testDateTime : QDateTime::currentDateTime();
}

const char* PDFAppLogHelper::getTypeName(QtMsgType type)
{
    switch (type)
    {
        case QtDebugMsg:
            return "debug";
        case QtInfoMsg:
            return "info";
        case QtWarningMsg:
            return "warning";
        case QtCriticalMsg:
            return "critical";
        case QtFatalMsg:
            return "fatal";
    }
    return "message";
}

QString PDFAppLog::getFileName(const QDate& date)
{
    return QStringLiteral("pdf4qt-%1.log").arg(date.toString(QStringLiteral("yyyy-MM-dd")));
}

QDate PDFAppLog::getFileDate(const QString& fileName)
{
    static const QRegularExpression expression(QStringLiteral("^pdf4qt-(\\d{4}-\\d{2}-\\d{2})\\.log$"));
    const QRegularExpressionMatch match = expression.match(fileName);
    if (!match.hasMatch())
    {
        return QDate();
    }
    return QDate::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd"));
}

QStringList PDFAppLog::cleanup(const QString& directory, const QDate& today, int retentionDays, qint64 maximumTotalSize)
{
    QStringList deletedFiles;
    if (directory.isEmpty() || !today.isValid())
    {
        return deletedFiles;
    }

    using LogFile = PDFAppLogHelper::LogFile;
    std::vector<LogFile> files;
    const QDate oldestKeptDate = today.addDays(-qMax(1, retentionDays) + 1);
    const QFileInfoList entries = QDir(directory).entryInfoList(QStringList() << QStringLiteral("pdf4qt-*.log"), QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo& entry : entries)
    {
        const QDate date = getFileDate(entry.fileName());
        if (!date.isValid())
        {
            continue;
        }

        if (date < oldestKeptDate)
        {
            if (QFile::remove(entry.absoluteFilePath()))
            {
                deletedFiles << entry.absoluteFilePath();
            }
            continue;
        }

        files.push_back({ entry.absoluteFilePath(), date, entry.size() });
    }

    // The oldest files are deleted first
    std::sort(files.begin(), files.end(), &PDFAppLogHelper::isOlder);

    qint64 totalSize = 0;
    for (const LogFile& file : files)
    {
        totalSize += file.size;
    }

    for (const LogFile& file : files)
    {
        if (totalSize <= maximumTotalSize)
        {
            break;
        }

        // The file of the day is deleted only, when it alone exceeds the limit
        if (file.date >= today && file.size <= maximumTotalSize)
        {
            continue;
        }

        if (QFile::remove(file.path))
        {
            deletedFiles << file.path;
            totalSize -= file.size;
        }
    }

    return deletedFiles;
}

void PDFAppLogHelper::openFile(PDFAppLogState& state, const QDate& date)
{
    state.file.close();
    state.filePath.clear();
    state.fileDate = date;
    state.isLimitReached = false;
    state.otherFilesSize = 0;

    const QString directory = state.options.directory;
    // A new folder is accessible only by the user (Unix)
    if (directory.isEmpty() || !QDir().mkpath(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
    {
        return;
    }

    PDFAppLog::cleanup(directory, date, state.options.retentionDays, state.options.maximumTotalSize);

    const QString fileName = QDir(directory).filePath(PDFAppLog::getFileName(date));
    const QFileInfoList entries = QDir(directory).entryInfoList(QStringList() << QStringLiteral("pdf4qt-*.log"), QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo& entry : entries)
    {
        if (PDFAppLog::getFileDate(entry.fileName()).isValid() && entry.absoluteFilePath() != QFileInfo(fileName).absoluteFilePath())
        {
            state.otherFilesSize += entry.size();
        }
    }

    if (openForAppend(state.file, fileName))
    {
        state.filePath = fileName;
    }
    else
    {
        state.filePath.clear();
    }
}

bool PDFAppLogHelper::openForAppend(QFile& file, const QString& filePath)
{
#if defined(Q_OS_WIN)
    // QFile opens with a write access and seeks to the end once, so the writes of
    // two processes would overwrite each other; FILE_APPEND_DATA always appends
    HANDLE handle = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(filePath).utf16()), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_APPEND | _O_WRONLY | _O_BINARY);
    if (descriptor < 0)
    {
        CloseHandle(handle);
        return false;
    }

    if (!file.open(descriptor, QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle))
    {
        _close(descriptor);
        return false;
    }
    return true;
#else
    file.setFileName(filePath);
    return file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
}

QByteArray PDFAppLogHelper::createLine(const QDateTime& time, const char* type, const char* category, const QString& message)
{
    // More instances append to the file of the day: whole lines with the process id
    // (newapplication.md, Logs)
    QByteArray line = time.toString(Qt::ISODateWithMs).toUtf8();
    line += " [";
    line += QByteArray::number(QCoreApplication::applicationPid());
    line += "] [";
    line += type;
    line += "] ";
    if (category && qstrcmp(category, "default") != 0)
    {
        line += category;
        line += ": ";
    }

    // A message is untrusted text (paths, texts of other processes): its next lines
    // are indented, so they never look like a line of their own, and other control
    // characters are replaced
    QString text = message;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    for (QChar& character : text)
    {
        if (character == QLatin1Char('\r'))
        {
            character = QLatin1Char('\n');
        }
        else if (character.unicode() < 0x20 && character != QLatin1Char('\n') && character != QLatin1Char('\t'))
        {
            character = QChar(0xFFFD);
        }
    }
    text.replace(QLatin1String("\n"), QLatin1String("\n    "));

#if defined(Q_OS_WIN)
    text.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    line += text.toUtf8();
    line += "\r\n";
#else
    line += text.toUtf8();
    line += '\n';
#endif
    return line;
}

void PDFAppLogHelper::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    PDFAppLogState& state = getState();
    QtMessageHandler previousHandler = nullptr;

    if (!s_isInHandler)
    {
        s_isInHandler = true;
        QMutexLocker locker(&state.mutex);
        previousHandler = state.previousHandler.load();

        if (state.isInstalled && !state.options.portable)
        {
            const QDateTime now = getCurrentDateTime(state);
            if (state.fileDate != now.date())
            {
                openFile(state, now.date());
            }

            if (state.file.isOpen() && !state.isLimitReached)
            {
                const QByteArray line = createLine(now, getTypeName(type), context.category, message);

                // Room is kept for the line, which says that the limit is reached
                const QByteArray lastLine = createLine(now, "warning", nullptr, QStringLiteral("The size limit of the logs has been reached, logging stops for today."));
                const qint64 limit = state.options.maximumTotalSize;
                if (state.otherFilesSize + state.file.size() + line.size() + lastLine.size() > limit)
                {
                    state.isLimitReached = true;
                    if (state.otherFilesSize + state.file.size() + lastLine.size() <= limit)
                    {
                        state.file.write(lastLine);
                    }
                }
                else
                {
                    state.file.write(line);
                }
            }
        }

        s_isInHandler = false;
    }
    else
    {
        // A message of the log itself (for example an error of the file): this thread
        // holds the non-recursive mutex already, the message goes only to the
        // previous handler
        previousHandler = state.previousHandler.load();
    }

    if (previousHandler)
    {
        previousHandler(type, context, message);
    }
}

bool PDFAppLog::install(const PDFAppLogOptions& options)
{
    uninstall();

    PDFAppLogState& state = PDFAppLogHelper::getState();
    {
        QMutexLocker locker(&state.mutex);
        state.options = options;
        state.options.retentionDays = qBound(1, state.options.retentionDays, 3650);
        state.options.maximumTotalSize = qMax(qint64(1024), state.options.maximumTotalSize);
        state.isInstalled = true;
        state.fileDate = QDate();
        state.isLimitReached = false;

        if (!state.options.portable)
        {
            PDFAppLogHelper::openFile(state, PDFAppLogHelper::getCurrentDateTime(state).date());
        }
    }

    QtMessageHandler previousHandler = qInstallMessageHandler(&PDFAppLogHelper::messageHandler);
    {
        QMutexLocker locker(&state.mutex);
        state.previousHandler = previousHandler;
        return state.file.isOpen();
    }
}

void PDFAppLog::uninstall()
{
    PDFAppLogState& state = PDFAppLogHelper::getState();
    QtMessageHandler previousHandler = nullptr;
    {
        QMutexLocker locker(&state.mutex);
        if (!state.isInstalled)
        {
            return;
        }

        previousHandler = state.previousHandler.load();
        state.isInstalled = false;
        state.options = PDFAppLogOptions();     // No folder of a removed log (About)
        state.file.close();
        state.file.setFileName(QString());
        state.filePath.clear();
        state.fileDate = QDate();
    }

    qInstallMessageHandler(previousHandler);

    QMutexLocker locker(&state.mutex);
    state.previousHandler = nullptr;
}

bool PDFAppLog::isInstalled()
{
    PDFAppLogState& state = PDFAppLogHelper::getState();
    QMutexLocker locker(&state.mutex);
    return state.isInstalled;
}

QString PDFAppLog::getLogDirectory()
{
    PDFAppLogState& state = PDFAppLogHelper::getState();
    QMutexLocker locker(&state.mutex);
    return state.isInstalled && !state.options.portable ? state.options.directory : QString();
}

QString PDFAppLog::getCurrentLogFile()
{
    PDFAppLogState& state = PDFAppLogHelper::getState();
    QMutexLocker locker(&state.mutex);
    return state.file.isOpen() ? state.filePath : QString();
}

void PDFAppLog::flush()
{
    PDFAppLogState& state = PDFAppLogHelper::getState();
    QMutexLocker locker(&state.mutex);
    if (state.file.isOpen())
    {
        state.file.flush();
    }
}

void PDFAppLog::setCurrentDateTimeForTesting(const QDateTime& dateTime)
{
    PDFAppLogState& state = PDFAppLogHelper::getState();
    QMutexLocker locker(&state.mutex);
    state.testDateTime = dateTime;
}

}   // namespace pdf
