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

#include "pdfappthumbnailer.h"
#include "pdfappsysteminfo.h"
#include "pdfdatabase.h"
#include "pdfrepositoryhelper.h"

#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfdocumentreader.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QMutex>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QTimeZone>
#include <QWaitCondition>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <set>
#include <vector>

namespace pdf
{

/// State shared by the thumbnailer (GUI thread) and its worker thread. It is held
/// by both, so a worker abandoned by stop() (a hanging file system call) still has
/// valid state; after stop() it never touches the database again.
///
/// Two locks: \p mutex protects the queue and the flags and it is held only for a
/// moment (the GUI thread takes it at each request); \p databaseMutex protects the
/// database pointer and every use of the database by the worker - a write may wait
/// for the lock of another instance (busy timeout), only stop() waits for it.
/// Order of locking: databaseMutex first, never databaseMutex inside of mutex.
class PDFAppThumbnailerState
{
public:
    /// Protects the queue, the pending paths, the options and the flags
    QMutex mutex;
    QWaitCondition condition;

    /// Protects the database pointer and its use by the worker
    QMutex databaseMutex;
    PDFDatabase* database = nullptr;    ///< nullptr after stop() (databaseMutex)

    /// Path, whose rendering marker the worker has written and not removed yet
    /// (databaseMutex); stop() removes it - a stopped rendering is no crash
    QString renderingPath;

    /// Id of the thumbnailer in its rendering markers (constant)
    QString instance;

    PDFAppThumbnailerOptions options;
    std::deque<QString> queue;
    QSet<QString> pending;              ///< Queued or running paths
    bool hasDatabase = false;           ///< A database was given and stop() has not run (mutex)
    bool isStopped = false;

    /// Cancellation of the running request (read by the renderer in the worker)
    std::atomic_bool isCanceled = false;
};

/// Cancellation of the rendering of one document: stop of the thumbnailer or the
/// time budget. Read from the worker thread (and its parallel loops).
class PDFAppThumbnailerControl : public PDFOperationControl
{
public:
    PDFAppThumbnailerControl(const std::atomic_bool* isCanceled, int timeBudget) :
        m_isCanceled(isCanceled),
        m_timeBudget(timeBudget)
    {
        m_timer.start();
    }

    virtual bool isOperationCancelled() const override
    {
        return isStopped() || isOverBudget();
    }

    bool isStopped() const { return m_isCanceled && m_isCanceled->load(); }
    bool isOverBudget() const { return m_timeBudget > 0 && m_timer.hasExpired(m_timeBudget); }

private:
    const std::atomic_bool* m_isCanceled;
    int m_timeBudget;
    QElapsedTimer m_timer;
};

/// Helpers of the thumbnailer
class PDFAppThumbnailerHelper
{
public:
    PDFAppThumbnailerHelper() = delete;

    /// Password callback of the reader: no password is asked (the document gets
    /// the state PasswordProtected); a document with an empty user password opens
    static QString refusePassword(bool* ok)
    {
        if (ok)
        {
            *ok = false;
        }
        return QString();
    }

    static PDFAppThumbnailResult failed(qint64 fileModified, int pageCount = -1)
    {
        PDFAppThumbnailResult result;
        result.outcome = PDFAppThumbnailResult::Outcome::Done;
        result.state = PDFRecentThumbnailState::Failed;
        result.fileModified = fileModified;
        result.pageCount = pageCount;
        return result;
    }

    /// Keeps an abandoned worker thread: a QThread must not be destroyed while it
    /// runs (also not at the exit of the process), it is never deleted, but it
    /// stays reachable (one small object per hung file system call)
    static void abandonThread(QThread* thread)
    {
        static QMutex mutex;
        static std::vector<QThread*>* abandonedThreads = new std::vector<QThread*>();

        QMutexLocker locker(&mutex);
        abandonedThreads->push_back(thread);
    }

    static PDFAppThumbnailResult canceled()
    {
        PDFAppThumbnailResult result;
        result.outcome = PDFAppThumbnailResult::Outcome::Canceled;
        return result;
    }

    /// PDFAppThumbnailer::createThumbnail without the handling of the exceptions
    static PDFAppThumbnailResult createThumbnail(const QString& path, const PDFAppThumbnailerOptions& options, const PDFOperationControl* control);

    /// The control is stopped (not over its time budget only)
    static bool isStopped(const PDFOperationControl* control)
    {
        const PDFAppThumbnailerControl* thumbnailControl = dynamic_cast<const PDFAppThumbnailerControl*>(control);
        return thumbnailControl ? thumbnailControl->isStopped() : PDFOperationControl::isOperationCancelled(control);
    }

    /// Size of the parts, in which a file is read (the cancellation is checked between them)
    static constexpr qint64 READ_CHUNK_SIZE = 1024 * 1024;

    enum class ReadResult
    {
        Done,
        Failed,     ///< Cannot be read, larger than the limit
        Canceled    ///< Stopped or over the time budget
    };

    /// Reads the file in parts of READ_CHUNK_SIZE; a slow disk or a network share
    /// does not block the stop and the time budget for the whole file
    static ReadResult readFile(const QString& path, qint64 maximumSize, const PDFOperationControl* control, QByteArray* data)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return ReadResult::Failed;
        }

        const qint64 size = file.size();
        if (size <= 0 || size > maximumSize)
        {
            return ReadResult::Failed;
        }

        data->clear();
        data->reserve(qsizetype(size));
        while (true)
        {
            if (PDFOperationControl::isOperationCancelled(control))
            {
                data->clear();
                return ReadResult::Canceled;
            }

            const QByteArray chunk = file.read(READ_CHUNK_SIZE);
            if (chunk.isEmpty())
            {
                break;
            }

            // The file may grow while it is read
            if (data->size() + chunk.size() > maximumSize)
            {
                data->clear();
                return ReadResult::Failed;
            }
            data->append(chunk);
        }

        if (file.error() != QFileDevice::NoError || data->isEmpty())
        {
            data->clear();
            return ReadResult::Failed;
        }
        return ReadResult::Done;
    }

    /// Most nested forms and patterns, which are searched for images
    static constexpr int MAXIMUM_RESOURCE_DEPTH = 16;

    /// Most XObjects and patterns, which are visited
    static constexpr int MAXIMUM_RESOURCE_OBJECTS = 10000;

    /// Largest image of the resources (recursively), see PDFAppThumbnailer::getLargestImagePixels
    static qint64 getLargestImagePixels(const PDFDocument* document, const PDFObject& resources, int depth, std::set<PDFObjectReference>* visited, int* objectCount);

    /// Pixels of an image stream (width x height; 0 for invalid values) and of its soft mask
    static qint64 getImagePixels(const PDFDocument* document, const PDFStream* stream, int depth);
};

PDFAppThumbnailerThread::PDFAppThumbnailerThread(std::shared_ptr<PDFAppThumbnailerState> state) :
    QThread(nullptr),
    m_state(std::move(state))
{
    setObjectName(QStringLiteral("PDF4QT thumbnails"));
}

PDFAppThumbnailerThread::~PDFAppThumbnailerThread()
{

}

void PDFAppThumbnailerThread::run()
{
    PDFAppThumbnailerOptions options;
    {
        QMutexLocker locker(&m_state->mutex);
        options = m_state->options;
    }

    // Renderings, which have crashed a previous run, before anything else
    sweepMarkers(options);

    while (true)
    {
        QString path;
        {
            QMutexLocker locker(&m_state->mutex);
            while (m_state->queue.empty() && !m_state->isStopped)
            {
                m_state->condition.wait(&m_state->mutex);
            }

            if (m_state->isStopped)
            {
                break;
            }

            path = m_state->queue.front();
            m_state->queue.pop_front();
        }

        process(path);
    }
}

void PDFAppThumbnailerThread::process(const QString& path)
{
    PDFAppThumbnailerOptions options;
    qint64 storedModified = -1;
    PDFRecentThumbnailState storedState = PDFRecentThumbnailState::Pending;
    bool isRecorded = false;

    {
        QMutexLocker locker(&m_state->mutex);
        if (m_state->isStopped)
        {
            return;
        }
        options = m_state->options;
    }

    // 1. The stored state (under the lock: stop() cannot close the database meanwhile)
    {
        QMutexLocker locker(&m_state->databaseMutex);
        if (!m_state->database)
        {
            return;
        }

        PDFRecentRepository repository(m_state->database, nullptr);
        if (std::optional<PDFRecentFile> file = repository.getFile(path))
        {
            isRecorded = true;
            storedModified = file->thumbnailModified;
            storedState = file->thumbnailState;
        }
    }

    PDFAppThumbnailResult result;
    bool isGenerated = false;
    bool written = false;
    if (isRecorded)
    {
        if (options.testDelay > 0)
        {
            QThread::msleep(options.testDelay);
        }

        // 2. The file (a network path can block here)
        const QFileInfo info(path);
        const QDateTime modified = info.exists() ? info.lastModified(QTimeZone::UTC) : QDateTime();
        const qint64 fileModified = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;

        if (!info.exists() || !info.isFile())
        {
            result.outcome = PDFAppThumbnailResult::Outcome::Missing;
        }
        else if (storedState != PDFRecentThumbnailState::Pending && storedModified == fileModified)
        {
            // Up to date, also a document without a thumbnail (no retry loop)
            result.outcome = PDFAppThumbnailResult::Outcome::Done;
            result.state = storedState;
            result.fileModified = fileModified;
        }
        else
        {
            switch (claimRendering(path, fileModified, options, &result, &written))
            {
                case Claim::Render:
                {
                    if (options.testExitWhileRendering)
                    {
                        // Tests: the process dies inside of the rendering (a crash of the renderer)
                        std::_Exit(PDFAppThumbnailer::TEST_EXIT_CODE);
                    }

                    PDFAppThumbnailerControl control(&m_state->isCanceled, options.timeBudget);
                    result = PDFAppThumbnailer::createThumbnail(path, options, &control);
                    isGenerated = true;
                    break;
                }

                case Claim::Resolved:
                    break;

                case Claim::Busy:
                    result.outcome = PDFAppThumbnailResult::Outcome::Canceled;
                    break;
            }
        }
    }

    // 3. The result and the removal of the marker (under the lock of the database:
    //    nothing is written after stop(), which removes the marker itself; the write
    //    may wait for another instance, the requests of the GUI thread do not)
    bool isDatabaseClosed = false;
    {
        QMutexLocker locker(&m_state->databaseMutex);
        isDatabaseClosed = !m_state->database;
        if (!isDatabaseClosed && isGenerated)
        {
            PDFRecentRepository repository(m_state->database, nullptr);
            if (result.outcome == PDFAppThumbnailResult::Outcome::Done)
            {
                const PDFDatabaseError error = repository.setThumbnail(path, result.png, result.fileModified, result.state, result.pageCount);
                if (error.isError())
                {
                    qWarning("Thumbnail of %s cannot be stored: %s", qPrintable(QDir::toNativeSeparators(path)), qPrintable(error.toString()));
                }
                written = !error.isError();
            }

            // The rendering has ended (also a canceled one): its marker is no crash. A
            // marker, which cannot be removed now (busy), is removed by the next request
            // of the file (it is the own one) or it ends as Failed after the process ends.
            const PDFDatabaseError error = repository.endRendering(path, m_state->instance);
            if (error.isError())
            {
                qWarning("The rendering marker of %s cannot be removed: %s", qPrintable(QDir::toNativeSeparators(path)), qPrintable(error.toString()));
            }
            else
            {
                m_state->renderingPath.clear();
            }
        }
    }

    {
        QMutexLocker locker(&m_state->mutex);
        m_state->pending.remove(path);
        if (m_state->isStopped || isDatabaseClosed)
        {
            return;
        }
    }

    Q_EMIT itemFinished(path, int(result.outcome == PDFAppThumbnailResult::Outcome::Done ? result.state : PDFRecentThumbnailState::Pending), written);
}

PDFAppThumbnailerThread::Claim PDFAppThumbnailerThread::claimRendering(const QString& path, qint64 fileModified, const PDFAppThumbnailerOptions& options,
                                                                       PDFAppThumbnailResult* result, bool* written)
{
    QMutexLocker locker(&m_state->databaseMutex);
    if (!m_state->database)
    {
        return Claim::Busy;
    }

    PDFRecentRepository repository(m_state->database, nullptr);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (const std::optional<PDFThumbnailRenderingMarker> marker = repository.getRenderingMarker(path))
    {
        const bool isOwn = marker->instance == m_state->instance;
        if (!isOwn && PDFAppThumbnailer::isMarkerAlive(*marker, now, options.markerLifetime))
        {
            // Another instance renders the file now; it stores the result
            return Claim::Busy;
        }

        if (!isOwn && marker->fileModified == fileModified)
        {
            // The rendering of this version of the file has crashed: no retry until it changes
            bool isFailed = false;
            if (repository.failRendering(*marker, &isFailed).isError())
            {
                return Claim::Busy;
            }

            const std::optional<PDFRecentFile> file = repository.getFile(path);
            result->outcome = PDFAppThumbnailResult::Outcome::Done;
            result->state = file ? file->thumbnailState : PDFRecentThumbnailState::Failed;
            result->fileModified = fileModified;
            *written = isFailed;
            return Claim::Resolved;
        }

        // The own marker, which could not be removed before, or a crash of an older
        // version of the file (it has changed since): the file is rendered again
        if (repository.endRendering(path, marker->instance).isError())
        {
            return Claim::Busy;
        }
    }

    PDFThumbnailRenderingMarker marker;
    marker.path = path;
    marker.fileModified = fileModified;
    marker.instance = m_state->instance;
    marker.processId = QCoreApplication::applicationPid();
    marker.started = now;

    // A file is never rendered without its marker (a database refusing the write
    // would otherwise let a crashing document crash every start)
    bool isWritten = false;
    if (repository.beginRendering(marker, &isWritten).isError() || !isWritten)
    {
        return Claim::Busy;
    }

    m_state->renderingPath = path;
    return Claim::Render;
}

void PDFAppThumbnailerThread::sweepMarkers(const PDFAppThumbnailerOptions& options)
{
    QMutexLocker locker(&m_state->databaseMutex);
    if (!m_state->database)
    {
        return;
    }

    // The markers of the renderings of ended processes (or too old ones): their
    // files become Failed (the notification of recent_file updates the pages)
    PDFRecentRepository repository(m_state->database, nullptr);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const PDFThumbnailRenderingMarker& marker : repository.getRenderingMarkers())
    {
        if (marker.instance == m_state->instance || PDFAppThumbnailer::isMarkerAlive(marker, now, options.markerLifetime))
        {
            continue;
        }

        const PDFDatabaseError error = repository.failRendering(marker);
        if (error.isError())
        {
            qWarning("The rendering marker of %s cannot be resolved: %s", qPrintable(QDir::toNativeSeparators(marker.path)), qPrintable(error.toString()));
        }
    }
}

PDFAppThumbnailer::PDFAppThumbnailer(PDFDatabase* database, QObject* parent) :
    QObject(parent),
    m_state(std::make_shared<PDFAppThumbnailerState>())
{
    qRegisterMetaType<pdf::PDFRecentThumbnailState>();
    m_state->database = database;
    m_state->hasDatabase = database != nullptr;
    m_state->instance = QString::number(QRandomGenerator::global()->generate64(), 16);
}

PDFAppThumbnailer::~PDFAppThumbnailer()
{
    stop();
}

void PDFAppThumbnailer::setOptions(const PDFAppThumbnailerOptions& options)
{
    QMutexLocker locker(&m_state->mutex);
    m_state->options = options;
    m_state->options.maximumDimension = qBound(1, options.maximumDimension, PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION);
    m_state->options.stopWaitTime = qMax(MINIMUM_STOP_WAIT_TIME, options.stopWaitTime);
    m_state->options.maximumImagePixels = qMax(qint64(1), options.maximumImagePixels);
    m_state->options.markerLifetime = qMax(qint64(0), options.markerLifetime);
}

PDFAppThumbnailerOptions PDFAppThumbnailer::getOptions() const
{
    QMutexLocker locker(&m_state->mutex);
    return m_state->options;
}

void PDFAppThumbnailer::requestThumbnail(const QString& path)
{
    if (!PDFRepositoryHelper::isValidStoredPath(path))
    {
        return;
    }

    {
        QMutexLocker locker(&m_state->mutex);
        // Never the lock of the database: the worker may wait there for another instance
        if (m_state->isStopped || !m_state->hasDatabase || m_state->pending.contains(path) || m_state->pending.size() >= MAXIMUM_QUEUE_LENGTH)
        {
            return;
        }

        m_state->pending.insert(path);
        m_state->queue.push_back(path);
        m_state->condition.wakeOne();
    }

    startThread();
}

void PDFAppThumbnailer::requestThumbnails(const QStringList& paths)
{
    for (const QString& path : paths)
    {
        requestThumbnail(path);
    }
}

void PDFAppThumbnailer::startThread()
{
    if (m_thread)
    {
        return;
    }

    m_thread = new PDFAppThumbnailerThread(m_state);
    connect(m_thread, &PDFAppThumbnailerThread::itemFinished, this, &PDFAppThumbnailer::onItemFinished, Qt::QueuedConnection);
    m_thread->start(QThread::LowPriority);
}

void PDFAppThumbnailer::stop()
{
    int stopWaitTime = 0;
    {
        QMutexLocker locker(&m_state->mutex);
        m_state->isStopped = true;
        m_state->hasDatabase = false;
        m_state->queue.clear();
        m_state->pending.clear();
        m_state->isCanceled.store(true);
        m_state->condition.wakeAll();
        stopWaitTime = m_state->options.stopWaitTime;
    }

    {
        // Waits for a running use of the database by the worker (at most its busy
        // timeout); after that the worker never touches the database again. The
        // marker of a running rendering is removed here: a stopped rendering (also
        // an abandoned one, which hangs) is no crash of the document.
        QMutexLocker locker(&m_state->databaseMutex);
        if (m_state->database && !m_state->renderingPath.isEmpty())
        {
            PDFRecentRepository repository(m_state->database, nullptr);
            const PDFDatabaseError error = repository.endRendering(m_state->renderingPath, m_state->instance);
            if (error.isError())
            {
                qWarning("The rendering marker of %s cannot be removed: %s", qPrintable(QDir::toNativeSeparators(m_state->renderingPath)), qPrintable(error.toString()));
            }
        }
        m_state->renderingPath.clear();
        m_state->database = nullptr;
    }

    if (!m_thread)
    {
        return;
    }

    PDFAppThumbnailerThread* thread = m_thread;
    m_thread = nullptr;
    thread->disconnect(this);

    if (thread->wait(QDeadlineTimer(stopWaitTime)))
    {
        delete thread;
    }
    else
    {
        // A file system call of the worker hangs (a network path). The worker holds
        // the shared state, it never uses the database again and it ends by itself;
        // the thread object is left to it (a QThread must not be destroyed while it runs).
        qWarning("The thumbnail worker did not stop in %d ms, it is abandoned.", stopWaitTime);
        PDFAppThumbnailerHelper::abandonThread(thread);
    }
}

bool PDFAppThumbnailer::isIdle() const
{
    QMutexLocker locker(&m_state->mutex);
    return m_state->pending.isEmpty();
}

int PDFAppThumbnailer::getPendingCount() const
{
    QMutexLocker locker(&m_state->mutex);
    return int(m_state->pending.size());
}

QString PDFAppThumbnailer::getInstanceId() const
{
    return m_state->instance;
}

bool PDFAppThumbnailer::isMarkerAlive(const PDFThumbnailRenderingMarker& marker, qint64 now, qint64 lifetime)
{
    // A process, which has ended, has crashed inside of the rendering (a normal end
    // removes the marker - stop() included). A running process may be another
    // instance rendering the file, or a reused process id: the time limit decides.
    if (marker.processId <= 0 || !PDFAppProcessInfo::isProcessRunning(marker.processId))
    {
        return false;
    }

    const qint64 age = now - marker.started;
    return age < lifetime && age > -lifetime;
}

void PDFAppThumbnailer::onItemFinished(const QString& path, int state, bool written)
{
    const PDFRecentThumbnailState thumbnailState = PDFRecentThumbnailState(qBound(0, state, 3));
    Q_EMIT thumbnailFinished(path, thumbnailState, written);
}

PDFAppThumbnailResult PDFAppThumbnailer::createThumbnail(const QString& path, const PDFAppThumbnailerOptions& options, const PDFOperationControl* control)
{
    // A document is untrusted: an exception of the library (a damaged document,
    // memory exhausted by a huge image) must not end the worker thread - an
    // exception leaving QThread::run terminates the process, and the next start
    // would request the same thumbnail again. The document gets the state Failed.
    try
    {
        return PDFAppThumbnailerHelper::createThumbnail(path, options, control);
    }
    catch (const PDFException& exception)
    {
        qWarning("The thumbnail of %s cannot be created: %s", qPrintable(QDir::toNativeSeparators(path)), qPrintable(exception.getMessage()));
    }
    catch (const std::exception& exception)
    {
        qWarning("The thumbnail of %s cannot be created: %s", qPrintable(QDir::toNativeSeparators(path)), exception.what());
    }

    const QDateTime modified = QFileInfo(path).lastModified(QTimeZone::UTC);
    return PDFAppThumbnailerHelper::failed(modified.isValid() ? modified.toMSecsSinceEpoch() : 0);
}

PDFAppThumbnailResult PDFAppThumbnailerHelper::createThumbnail(const QString& path, const PDFAppThumbnailerOptions& options, const PDFOperationControl* control)
{
    PDFAppThumbnailResult result;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        result.outcome = PDFAppThumbnailResult::Outcome::Missing;
        return result;
    }

    const QDateTime modified = info.lastModified(QTimeZone::UTC);
    const qint64 fileModified = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;

    if (info.size() > options.maximumFileSize || info.size() <= 0)
    {
        return PDFAppThumbnailerHelper::failed(fileModified);
    }

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFAppThumbnailerHelper::canceled();
    }

    // The file is read in parts (canceled between them), then parsed from the memory
    QByteArray data;
    switch (readFile(path, options.maximumFileSize, control, &data))
    {
        case ReadResult::Done:
            break;

        case ReadResult::Failed:
            return PDFAppThumbnailerHelper::failed(fileModified);

        case ReadResult::Canceled:
            // The time budget of a slow reading is a failure, a stop is not
            return isStopped(control) ? PDFAppThumbnailerHelper::canceled() : PDFAppThumbnailerHelper::failed(fileModified);
    }

    // The parsing cannot be canceled (the core has no cancellation of PDFDocumentReader)
    PDFDocumentReader reader(nullptr, &PDFAppThumbnailerHelper::refusePassword, true, false);
    const PDFDocument document = reader.readFromBuffer(data);
    data = QByteArray();

    if (isStopped(control))
    {
        return PDFAppThumbnailerHelper::canceled();
    }

    switch (reader.getReadingResult())
    {
        case PDFDocumentReader::Result::OK:
            break;

        case PDFDocumentReader::Result::Cancelled:
        {
            // The only cancellation of the reader is the refused password
            result.outcome = PDFAppThumbnailResult::Outcome::Done;
            result.state = PDFRecentThumbnailState::PasswordProtected;
            result.fileModified = fileModified;
            return result;
        }

        case PDFDocumentReader::Result::Failed:
            return PDFAppThumbnailerHelper::failed(fileModified);
    }

    const PDFCatalog* catalog = document.getCatalog();
    const size_t pageCount = catalog ? catalog->getPageCount() : 0;
    const int storedPageCount = int(qMin<size_t>(pageCount, size_t(PDFRecentRepository::MAXIMUM_PAGE)));
    if (pageCount == 0)
    {
        return PDFAppThumbnailerHelper::failed(fileModified, 0);
    }

    // The time budget covers the parsing too: a document, which needs it whole for
    // the parsing, is not rendered
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFAppThumbnailerHelper::failed(fileModified, storedPageCount);
    }

    // A huge image is not decoded (the decoders of the core have no size limit)
    if (PDFAppThumbnailer::getLargestImagePixels(&document) > options.maximumImagePixels)
    {
        return PDFAppThumbnailerHelper::failed(fileModified, storedPageCount);
    }

    const QImage image = PDFAppThumbnailer::renderFirstPage(&document, qBound(1, options.maximumDimension, PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION), control);
    if (image.isNull())
    {
        if (isStopped(control))
        {
            return PDFAppThumbnailerHelper::canceled();
        }
        return PDFAppThumbnailerHelper::failed(fileModified, storedPageCount);
    }

    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, "png");
        if (!writer.write(image))
        {
            return PDFAppThumbnailerHelper::failed(fileModified, storedPageCount);
        }
    }

    if (png.isEmpty() || png.size() > PDFRecentRepository::MAXIMUM_THUMBNAIL_BYTES)
    {
        return PDFAppThumbnailerHelper::failed(fileModified, storedPageCount);
    }

    result.outcome = PDFAppThumbnailResult::Outcome::Done;
    result.state = PDFRecentThumbnailState::Ready;
    result.png = png;
    result.pageCount = storedPageCount;
    result.fileModified = fileModified;
    return result;
}

QImage PDFAppThumbnailer::renderFirstPage(const PDFDocument* document, int maximumDimension, const PDFOperationControl* control)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || catalog->getPageCount() == 0 || maximumDimension <= 0)
    {
        return QImage();
    }

    const PDFPage* page = catalog->getPage(0);
    if (!page)
    {
        return QImage();
    }

    // Size of the picture: the rotated crop box scaled to the longest edge
    const QSizeF pageSize = page->getRotatedCropBox().size();
    if (!std::isfinite(pageSize.width()) || !std::isfinite(pageSize.height()) || pageSize.width() <= 0.0 || pageSize.height() <= 0.0)
    {
        return QImage();
    }

    const double scale = double(maximumDimension) / qMax(pageSize.width(), pageSize.height());
    const QSize imageSize(qBound(1, int(std::lround(pageSize.width() * scale)), maximumDimension),
                          qBound(1, int(std::lround(pageSize.height() * scale)), maximumDimension));

    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity(document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity));
    iccProfileCache.setDocument(document);

    // Colors without the color management system (no state of LittleCMS in the worker;
    // the alternate color spaces are good enough for a thumbnail)
    const PDFCMSGeneric cms;
    constexpr PDFRenderer::Features features = PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing |
                                                                      PDFRenderer::SmoothImages | PDFRenderer::ClipToCropBox);
    PDFRenderer renderer(document, &fontCache, &iccProfileCache, &cms, &optionalContentActivity, features, PDFMeshQualitySettings());
    renderer.setOperationControl(control);

    PDFPrecompiledPage compiledPage;
    renderer.compile(&compiledPage, 0);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return QImage();
    }

    QImage image(imageSize, QImage::Format_RGB32);
    if (image.isNull())
    {
        return QImage();
    }
    image.fill(Qt::white);

    const QRectF rotatedCropBox = page->getRotatedBox(page->getCropBox(), page->getPageRotation());
    const QTransform pageToImage = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedCropBox, QRectF(QPointF(0, 0), QSizeF(imageSize)), page->getPageRotation());

    QPainter painter(&image);
    compiledPage.draw(&painter, page->getCropBox(), pageToImage, features, 1.0);
    painter.end();

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return QImage();
    }
    return image;
}

qint64 PDFAppThumbnailer::getLargestImagePixels(const PDFDocument* document)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const PDFPage* page = catalog && catalog->getPageCount() > 0 ? catalog->getPage(0) : nullptr;
    if (!page)
    {
        return 0;
    }

    std::set<PDFObjectReference> visited;
    int objectCount = 0;
    return PDFAppThumbnailerHelper::getLargestImagePixels(document, page->getResources(), 0, &visited, &objectCount);
}

qint64 PDFAppThumbnailerHelper::getImagePixels(const PDFDocument* document, const PDFStream* stream, int depth)
{
    const PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
    if (!dictionary || depth > MAXIMUM_RESOURCE_DEPTH)
    {
        return 0;
    }

    // Values of a hostile document: negative or huge sizes are clamped (the product
    // of two 2^31 values fits into qint64)
    const PDFObject& widthObject = document->getObject(dictionary->get("Width"));
    const PDFObject& heightObject = document->getObject(dictionary->get("Height"));
    const qint64 width = widthObject.isInt() ? qBound(qint64(0), qint64(widthObject.getInteger()), qint64(std::numeric_limits<int>::max())) : 0;
    const qint64 height = heightObject.isInt() ? qBound(qint64(0), qint64(heightObject.getInteger()), qint64(std::numeric_limits<int>::max())) : 0;
    qint64 pixels = width * height;

    // The soft mask is decoded too
    const PDFObject& softMask = document->getObject(dictionary->get("SMask"));
    if (softMask.isStream())
    {
        pixels = qMax(pixels, getImagePixels(document, softMask.getStream(), depth + 1));
    }
    return pixels;
}

qint64 PDFAppThumbnailerHelper::getLargestImagePixels(const PDFDocument* document, const PDFObject& resourcesObject, int depth, std::set<PDFObjectReference>* visited, int* objectCount)
{
    const PDFObject& resources = document->getObject(resourcesObject);
    if (!resources.isDictionary() || depth > MAXIMUM_RESOURCE_DEPTH)
    {
        return 0;
    }

    qint64 largest = 0;
    for (const char* category : { "XObject", "Pattern" })
    {
        const PDFObject& objects = document->getObject(resources.getDictionary()->get(category));
        if (!objects.isDictionary())
        {
            continue;
        }

        const PDFDictionary* dictionary = objects.getDictionary();
        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            if (++(*objectCount) > MAXIMUM_RESOURCE_OBJECTS)
            {
                return largest;
            }

            // A shared form or a cycle of forms is visited once
            const PDFObject& value = dictionary->getValue(i);
            if (value.isReference())
            {
                if (visited->count(value.getReference()) > 0)
                {
                    continue;
                }
                visited->insert(value.getReference());
            }

            const PDFObject& object = document->getObject(value);
            if (!object.isStream())
            {
                continue;
            }

            const PDFStream* stream = object.getStream();
            const PDFDictionary* streamDictionary = stream->getDictionary();
            const PDFObject& subtype = document->getObject(streamDictionary->get("Subtype"));
            if (subtype.isName() && subtype.getString() == "Image")
            {
                largest = qMax(largest, getImagePixels(document, stream, depth));
            }
            else
            {
                // A form XObject or a tiling pattern: its own resources
                largest = qMax(largest, getLargestImagePixels(document, streamDictionary->get("Resources"), depth + 1, visited, objectCount));
            }
        }
    }
    return largest;
}

}   // namespace pdf
