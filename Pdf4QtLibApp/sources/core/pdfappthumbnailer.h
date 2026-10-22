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

#ifndef PDFAPPTHUMBNAILER_H
#define PDFAPPTHUMBNAILER_H

#include "pdfappglobal.h"
#include "pdfrecentrepository.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

#include <memory>

namespace pdf
{

class PDFDatabase;
class PDFDocument;
class PDFOperationControl;
class PDFAppThumbnailerState;

/// Limits of the thumbnailer (injectable for the tests)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailerOptions
{
public:
    /// Larger files are not read (the whole file is read into memory and parsed);
    /// they get the state Failed
    qint64 maximumFileSize = qint64(256) * 1024 * 1024;

    /// Time budget of the rendering of page 1 in milliseconds; a slower page gets
    /// the state Failed (it is not tried again until the file changes)
    int timeBudget = 15000;

    /// Longest edge of the thumbnail in pixels (at most
    /// PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION)
    int maximumDimension = PDFRecentRepository::MAXIMUM_THUMBNAIL_DIMENSION;

    /// stop() waits at most this time for the worker (a file system call of a
    /// network path, which hangs, cannot be interrupted); then the worker thread is
    /// abandoned - it never touches the database again and ends by itself
    int stopWaitTime = 5000;

    /// Delay of the worker before each document in milliseconds (tests: a long
    /// running request, which is stopped)
    int testDelay = 0;

    /// Images of page 1 with more pixels (width x height of an image XObject, also
    /// of a soft mask and inside of forms and tiling patterns) are not decoded: the
    /// document gets the state Failed. The decoders of the core have no size limit,
    /// a huge scan would allocate gigabytes in the worker. Inline images are not
    /// checked (their data are a part of the content stream).
    qint64 maximumImagePixels = qint64(64) * 1024 * 1024;

    /// Crash-loop guard: a rendering marker (PDFThumbnailRenderingMarker) of another
    /// thumbnailer, whose process runs, is taken as a running rendering for this time
    /// in milliseconds; an older one, or one of an ended process, as a crash - the file
    /// gets the state Failed until it changes
    qint64 markerLifetime = qint64(5) * 60 * 1000;

    /// Tests: the process ends (std::_Exit with PDFAppThumbnailer::TEST_EXIT_CODE) right
    /// after the rendering marker is written - a document, which crashes the renderer
    bool testExitWhileRendering = false;
};

/// Result of PDFAppThumbnailer::createThumbnail
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailResult
{
public:
    enum class Outcome
    {
        Done,       ///< state, png and pageCount are valid and are stored
        Missing,    ///< The file does not exist (nothing is stored; Recent shows "No longer exists")
        Canceled    ///< Stopped, or rendered by another instance now (nothing is stored, the file is tried again next time)
    };

    Outcome outcome = Outcome::Missing;
    PDFRecentThumbnailState state = PDFRecentThumbnailState::Failed;
    QByteArray png;
    int pageCount = -1;
    qint64 fileModified = 0;    ///< Time of the modification of the file (ms since the epoch, UTC)
};

/// Worker thread of PDFAppThumbnailer (internal). Its own connection to the
/// database is closed, when the thread ends.
class PDFAppThumbnailerThread : public QThread
{
    Q_OBJECT

public:
    explicit PDFAppThumbnailerThread(std::shared_ptr<PDFAppThumbnailerState> state);
    virtual ~PDFAppThumbnailerThread() override;

Q_SIGNALS:
    /// Emitted in the worker thread, received queued in the GUI thread
    void itemFinished(const QString& path, int state, bool written);

protected:
    virtual void run() override;

private:
    /// What the worker does with a file, which needs a thumbnail
    enum class Claim
    {
        Render,     ///< The marker of this thumbnailer is written, the file is rendered
        Resolved,   ///< A crashed rendering left its marker: the file is Failed (result filled)
        Busy        ///< Another instance renders it now (or the database refused the marker)
    };

    void process(const QString& path);

    /// Crash-loop guard before a rendering (under the lock of the database)
    Claim claimRendering(const QString& path, qint64 fileModified, const PDFAppThumbnailerOptions& options, PDFAppThumbnailResult* result, bool* written);

    /// Markers of crashed renderings found at the start of the worker: their files become Failed
    void sweepMarkers(const PDFAppThumbnailerOptions& options);

    std::shared_ptr<PDFAppThumbnailerState> m_state;
};

/// Thumbnails of page 1 and page counts of the recent files (newapp_storage.md 4:
/// PNG of at most 320 px). Documents are read and rendered in one worker thread
/// with its own connection to the database (PDFDocumentReader, PDFRenderer and a
/// QPainter on a QImage - no widgets). The result is written into recent_file
/// (thumbnail, thumbnail_mtime, thumbnail_state, page_count); the pages read it
/// from the database after the notification of PDFDatabaseNotifier (topic
/// recent_file, key = path) - nothing is cached.
///
/// - Requests are deduplicated; a file, whose thumbnail_mtime equals the time of
///   its modification and whose state is not Pending, is skipped (also a password
///   protected or damaged one: no retry loop until the file changes).
/// - Encrypted documents, which need a password, get the state PasswordProtected;
///   damaged, empty (no pages), too large or too slow ones Failed; a missing file
///   is not written at all.
/// - stop() (and the destructor) cancels the rendering and waits for the worker;
///   the owner (PDFAppContext) stops it before the database is closed.
/// - In the portable mode the database is in memory, nothing is written to the disk.
///
/// GUI thread only (the worker is internal).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThumbnailer : public QObject
{
    Q_OBJECT

public:
    /// Longest queue of requests (further requests are ignored until it shrinks)
    static constexpr int MAXIMUM_QUEUE_LENGTH = 1000;

    /// Shortest wait of stop() for the worker (an idle worker needs a moment to wake up)
    static constexpr int MINIMUM_STOP_WAIT_TIME = 100;

    /// Exit code of a process ended by PDFAppThumbnailerOptions::testExitWhileRendering
    static constexpr int TEST_EXIT_CODE = 86;

    explicit PDFAppThumbnailer(PDFDatabase* database, QObject* parent = nullptr);
    virtual ~PDFAppThumbnailer() override;

    /// Options (tests); call before the first request
    void setOptions(const PDFAppThumbnailerOptions& options);
    PDFAppThumbnailerOptions getOptions() const;

    /// Requests the thumbnail of a recent file (path as stored in recent_file).
    /// Invalid paths, duplicates of pending requests and requests after stop() are ignored.
    void requestThumbnail(const QString& path);

    /// Requests thumbnails of more files (in this order)
    void requestThumbnails(const QStringList& paths);

    /// Stops the worker: the pending requests are dropped, the rendering is canceled,
    /// the worker thread is waited for (PDFAppThumbnailerOptions::stopWaitTime).
    /// Further requests are ignored.
    void stop();

    /// Nothing is pending or running
    bool isIdle() const;

    /// Number of the pending and running requests
    int getPendingCount() const;

    /// Id of this thumbnailer in its rendering markers (random)
    QString getInstanceId() const;

    /// The marker of another thumbnailer belongs to a running rendering: its process
    /// runs and it is younger than \p lifetime (also not more than \p lifetime in the
    /// future - a clock set back). Otherwise its rendering has crashed (or hangs).
    static bool isMarkerAlive(const PDFThumbnailRenderingMarker& marker, qint64 now, qint64 lifetime);

    /// Makes the thumbnail of a file (any thread, no database): reads the file (in
    /// parts, canceled between them), parses the document, renders page 1 into a PNG
    /// of at most options.maximumDimension pixels. The time budget of \p control
    /// covers the whole work: the reading and the rendering are canceled, the parsing
    /// of the core cannot be (PDFDocumentReader has no cancellation) - a document
    /// over the budget after the parsing is Failed without being rendered.
    /// \param control Cancellation (stop, time budget), may be nullptr
    static PDFAppThumbnailResult createThumbnail(const QString& path, const PDFAppThumbnailerOptions& options, const PDFOperationControl* control);

    /// Renders page 1 of a document (any thread), null image on an error or a cancellation
    static QImage renderFirstPage(const PDFDocument* document, int maximumDimension, const PDFOperationControl* control);

    /// Largest image of page 1 in pixels (image XObjects of the resources, their soft
    /// masks, forms and tiling patterns, recursively; 0 = none)
    static qint64 getLargestImagePixels(const PDFDocument* document);

Q_SIGNALS:
    /// A request was processed (GUI thread, queued). \p written = the database was
    /// changed (the page gets also the notification of the database). For tests
    /// and diagnostics - pages use the notification.
    void thumbnailFinished(const QString& path, pdf::PDFRecentThumbnailState state, bool written);

private:
    void onItemFinished(const QString& path, int state, bool written);
    void startThread();

    std::shared_ptr<PDFAppThumbnailerState> m_state;
    PDFAppThumbnailerThread* m_thread = nullptr;
};

}   // namespace pdf

#endif // PDFAPPTHUMBNAILER_H
