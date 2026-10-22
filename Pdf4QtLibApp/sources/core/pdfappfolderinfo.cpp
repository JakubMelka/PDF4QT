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

#include "pdfappfolderinfo.h"
#include "pdfrepositoryhelper.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutex>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <memory>
#include <vector>

namespace pdf
{

/// State shared by the service and its workers. The workers deliver their result
/// only while the service lives (receiver), under the mutex, so a result is never
/// posted to a destroyed service; a worker abandoned at the destruction holds it.
class PDFAppFolderInfoShared
{
public:
    QMutex mutex;
    PDFAppFolderInfoService* receiver = nullptr;
    std::atomic_int maximumEntries = PDFAppFolderInfoService::DEFAULT_MAXIMUM_ENTRIES;
    std::atomic_int testDelay = 0;
    std::atomic_int runningCount = 0;   ///< Workers computing a request now (tests)
};

/// One request computed in the pool
class PDFAppFolderInfoTask : public QRunnable
{
public:
    PDFAppFolderInfoTask(std::shared_ptr<PDFAppFolderInfoShared> shared, QString path, bool countPdfFiles) :
        m_shared(std::move(shared)),
        m_path(std::move(path)),
        m_countPdfFiles(countPdfFiles)
    {
        setAutoDelete(true);
    }

    virtual void run() override
    {
        m_shared->runningCount.fetch_add(1);

        const int delay = m_shared->testDelay.load();
        if (delay > 0)
        {
            QThread::msleep(delay);
        }

        const PDFAppFolderInfo info = PDFAppFolderInfoService::computeInfo(m_path, m_countPdfFiles, m_shared->maximumEntries.load());

        {
            QMutexLocker locker(&m_shared->mutex);
            if (m_shared->receiver)
            {
                // Posted event: the service cannot be destroyed while the mutex is held,
                // a destroyed receiver drops its posted events
                QMetaObject::invokeMethod(m_shared->receiver, &PDFAppFolderInfoService::onResult, Qt::QueuedConnection, info, m_countPdfFiles);
            }
        }

        m_shared->runningCount.fetch_sub(1);
    }

private:
    std::shared_ptr<PDFAppFolderInfoShared> m_shared;
    QString m_path;
    bool m_countPdfFiles;
};

/// Helpers of the service
class PDFAppFolderInfoHelper
{
public:
    PDFAppFolderInfoHelper() = delete;

    static QString getKey(const QString& path, bool countPdfFiles)
    {
        return (countPdfFiles ? QStringLiteral("1:") : QStringLiteral("0:")) + path;
    }

    /// Keeps an abandoned pool (its workers hang in the file system): it cannot be
    /// destroyed while they run (even at the exit of the process, the destructor
    /// would wait), so it is never deleted, but it stays reachable
    static void abandonPool(QThreadPool* pool)
    {
        // The list itself is never destroyed either (no destructor at the exit)
        static QMutex mutex;
        static std::vector<QThreadPool*>* abandonedPools = new std::vector<QThreadPool*>();

        QMutexLocker locker(&mutex);
        abandonedPools->push_back(pool);
    }
};

PDFAppFolderInfoService::PDFAppFolderInfoService(QObject* parent) :
    QObject(parent),
    m_shared(std::make_shared<PDFAppFolderInfoShared>()),
    m_pool(new QThreadPool())
{
    qRegisterMetaType<pdf::PDFAppFolderInfo>();
    m_shared->receiver = this;

    // More threads: a request of a network path, which blocks, does not stop the others
    m_pool->setMaxThreadCount(4);
    m_pool->setObjectName(QStringLiteral("PDF4QT folder information"));
}

PDFAppFolderInfoService::~PDFAppFolderInfoService()
{
    {
        QMutexLocker locker(&m_shared->mutex);
        m_shared->receiver = nullptr;
    }

    m_pool->clear();
    if (m_pool->waitForDone(QDeadlineTimer(m_stopWaitTime)))
    {
        delete m_pool;
    }
    else
    {
        // A worker hangs in the file system (a network path). The pool cannot be
        // destroyed while it runs (its destructor would wait), it is abandoned; its
        // workers hold only the shared state and drop their results.
        qWarning("The folder information workers did not stop in %d ms, they are abandoned.", m_stopWaitTime);
        PDFAppFolderInfoHelper::abandonPool(m_pool);
    }
    m_pool = nullptr;
}

bool PDFAppFolderInfoService::requestFolderInfo(const QString& path)
{
    return request(path, true);
}

bool PDFAppFolderInfoService::requestPathInfo(const QString& path)
{
    return request(path, false);
}

bool PDFAppFolderInfoService::request(const QString& path, bool countPdfFiles)
{
    if (!PDFRepositoryHelper::isValidStoredPath(QDir::fromNativeSeparators(path)))
    {
        return false;
    }

    // A request of a running path is merged with it (its result is delivered)
    const QString key = PDFAppFolderInfoHelper::getKey(path, countPdfFiles);
    if (m_pending.contains(key))
    {
        return true;
    }

    if (isFull())
    {
        return false;
    }

    m_pending.insert(key);
    m_pool->start(new PDFAppFolderInfoTask(m_shared, path, countPdfFiles));
    return true;
}

void PDFAppFolderInfoService::setMaximumEntries(int maximumEntries)
{
    m_shared->maximumEntries.store(qMax(1, maximumEntries));
}

int PDFAppFolderInfoService::getMaximumEntries() const
{
    return m_shared->maximumEntries.load();
}

void PDFAppFolderInfoService::setTestDelay(int delay)
{
    m_shared->testDelay.store(qMax(0, delay));
}

int PDFAppFolderInfoService::getRunningCount() const
{
    return m_shared->runningCount.load();
}

void PDFAppFolderInfoService::onResult(const PDFAppFolderInfo& info, bool countPdfFiles)
{
    m_pending.remove(PDFAppFolderInfoHelper::getKey(info.path, countPdfFiles));
    Q_EMIT infoReady(info);
}

PDFAppFolderInfo PDFAppFolderInfoService::computeInfo(const QString& path, bool countPdfFiles, int maximumEntries)
{
    PDFAppFolderInfo info;
    info.path = path;

    const QString filePath = QDir::fromNativeSeparators(path);
    if (!PDFRepositoryHelper::isValidStoredPath(filePath))
    {
        return info;
    }

    const QFileInfo fileInfo(filePath);
    info.exists = fileInfo.exists();
    info.isDirectory = info.exists && fileInfo.isDir();
    info.isFile = info.exists && fileInfo.isFile();

    if (!countPdfFiles || !info.isDirectory)
    {
        return info;
    }

    // Every entry counts towards the limit (also folders and other files), so a
    // huge folder is never enumerated completely
    int count = 0;
    int entries = 0;
    const int limit = qMax(1, maximumEntries);
    QDirIterator iterator(filePath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (iterator.hasNext())
    {
        if (entries >= limit)
        {
            info.isCountLimited = true;
            break;
        }

        iterator.next();
        ++entries;

        const QFileInfo entry = iterator.fileInfo();
        if (entry.isFile() && entry.fileName().endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        {
            ++count;
        }
    }

    info.pdfCount = count;
    return info;
}

}   // namespace pdf
