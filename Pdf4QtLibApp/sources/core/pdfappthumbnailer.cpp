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
#include <QFileInfo>
#include <QImageWriter>
#include <QMutex>
#include <QPainter>
#include <QSet>
#include <QTimeZone>
#include <QWaitCondition>

#include <atomic>
#include <cmath>
#include <deque>
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
            PDFAppThumbnailerControl control(&m_state->isCanceled, options.timeBudget);
            result = PDFAppThumbnailer::createThumbnail(path, options, &control);
            isGenerated = true;
        }
    }

    // 3. The result (under the lock of the database: nothing is written after stop();
    //    the write may wait for another instance, the requests of the GUI thread do not)
    bool written = false;
    bool isDatabaseClosed = false;
    {
        QMutexLocker locker(&m_state->databaseMutex);
        isDatabaseClosed = !m_state->database;
        if (!isDatabaseClosed && isGenerated && result.outcome == PDFAppThumbnailResult::Outcome::Done)
        {
            PDFRecentRepository repository(m_state->database, nullptr);
            const PDFDatabaseError error = repository.setThumbnail(path, result.png, result.fileModified, result.state, result.pageCount);
            if (error.isError())
            {
                qWarning("Thumbnail of %s cannot be stored: %s", qPrintable(QDir::toNativeSeparators(path)), qPrintable(error.toString()));
            }
            written = !error.isError();
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

PDFAppThumbnailer::PDFAppThumbnailer(PDFDatabase* database, QObject* parent) :
    QObject(parent),
    m_state(std::make_shared<PDFAppThumbnailerState>())
{
    qRegisterMetaType<pdf::PDFRecentThumbnailState>();
    m_state->database = database;
    m_state->hasDatabase = database != nullptr;
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
        // timeout); after that the worker never touches the database again
        QMutexLocker locker(&m_state->databaseMutex);
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

    PDFDocumentReader reader(nullptr, &PDFAppThumbnailerHelper::refusePassword, true, false);
    const PDFDocument document = reader.readFromFile(path);

    if (PDFOperationControl::isOperationCancelled(control))
    {
        // The time budget of a slow reading is a failure, a stop is not
        const PDFAppThumbnailerControl* thumbnailControl = dynamic_cast<const PDFAppThumbnailerControl*>(control);
        if (!thumbnailControl || thumbnailControl->isStopped())
        {
            return PDFAppThumbnailerHelper::canceled();
        }
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

    const QImage image = PDFAppThumbnailer::renderFirstPage(&document, qBound(1, options.maximumDimension, PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION), control);
    if (image.isNull())
    {
        const PDFAppThumbnailerControl* thumbnailControl = dynamic_cast<const PDFAppThumbnailerControl*>(control);
        if (thumbnailControl && thumbnailControl->isStopped())
        {
            return PDFAppThumbnailerHelper::canceled();
        }
        if (!thumbnailControl && PDFOperationControl::isOperationCancelled(control))
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

}   // namespace pdf
