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

#ifndef PDFOCRJOBCONTROLLER_H
#define PDFOCRJOBCONTROLLER_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"
#include "pdfocrengine.h"
#include "pdfocrconfiguration.h"
#include "pdfocrpagepreparer.h"
#include "pdfoperationcontrol.h"

#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QSemaphore>
#include <QWaitCondition>

#include <set>
#include <atomic>
#include <memory>
#include <deque>

#include <QElapsedTimer>

namespace pdf
{
class PDFCMS;
class PDFDocument;
class PDFFontCache;
class PDFOptionalContentActivity;
struct PDFMeshQualitySettings;

/// Cancellation token shared by the job and the workers (JOB-05)
class PDF4QTLIBCORESHARED_EXPORT PDFOCRCancelToken : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_cancelled.load(std::memory_order_acquire); }
    void cancel() { m_cancelled.store(true, std::memory_order_release); }

private:
    std::atomic<bool> m_cancelled = { false };
};

/// Task of one page
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRPageTask
{
    PDFInteger pageIndex = -1;

    /// Effective configuration of the page (PAGE-06)
    PDFOCRConfiguration configuration;

    /// Regions of the page
    std::vector<PDFOCRRegion> regions;

    /// Rectangles which must be masked (annotations, unapplied redactions), page space
    std::vector<QRectF> maskedRectangles;

    /// Page analysis (already performed)
    PDFOCRPageAnalysis analysis;

    /// Page label
    QString pageLabel;

    /// Page fingerprint
    QByteArray pageFingerprint;

    /// Blank page detection is disabled for this page (IMAGE-07)
    bool skipBlankDetection = false;

    /// Recognition generation (result generation)
    int generation = 0;
};

/// Description of the job (JOB-02)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRJobDescription
{
    QString jobId;
    QByteArray documentFingerprint;
    PDFOCRConfiguration configuration;
    PDFOCRResolvedModelSet models;
    std::vector<PDFOCRPageTask> pages;
    qint64 maximumRasterPixels = PDFOCRPagePreparer::DefaultMaximumPixels;
};

/// Summary of the finished job (JOB-04, JOB-07)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRJobSummary
{
    int generation = 0;
    int totalPages = 0;
    int donePages = 0;
    int noTextPages = 0;
    int errorPages = 0;
    int cancelledPages = 0;
    int skippedPages = 0;
    qint64 elapsedMilliseconds = 0;
    bool cancelled = false;

    /// Critical error, which stopped the job (initialization, models, ...)
    PDFOCRError criticalError;

    bool isPartial() const { return errorPages > 0 || cancelledPages > 0; }
};

/// Controller of the OCR jobs (chapter 11 of the OCR specification). Runs the
/// pipeline (rasterization, preprocessing, recognition, conversion) on worker
/// threads and delivers immutable results via queued signals. The document is
/// never modified from the worker threads.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRJobController : public QObject
{
    Q_OBJECT

public:
    explicit PDFOCRJobController(QObject* parent);
    virtual ~PDFOCRJobController() override;

    /// Sets the rendering environment (must be valid during the job)
    void setEnvironment(const PDFDocument* document,
                        const PDFFontCache* fontCache,
                        const PDFCMS* cms,
                        const PDFOptionalContentActivity* optionalContentActivity,
                        const PDFMeshQualitySettings* meshQualitySettings,
                        RendererEngine rendererEngine);

    /// Starts the job. Returns false, if a job is already running. Generation
    /// of the job is returned via the reference.
    bool start(PDFOCRJobDescription description, int* generation);

    /// Requests the stop of the job (JOB-05). Returns immediately.
    void stop();

    /// Waits until all workers finish (used before the destruction)
    void waitForFinished();

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    bool isStopping() const { return m_stopping.load(std::memory_order_acquire); }
    int getGeneration() const { return m_generation; }

    /// Returns page indices, which are still queued or being processed
    std::vector<PDFInteger> getPendingPages() const;

    /// Returns true, if page is currently being processed
    bool isPageInProgress(PDFInteger pageIndex) const;

    /// Returns true, if page is queued or being processed
    bool isPagePending(PDFInteger pageIndex) const;

signals:
    /// Page entered a new state (Preparing, Recognizing)
    void pageStateChanged(int generation, qint64 pageIndex, int state, QString phase);

    /// Progress of the page (percent, -1 = indeterminate)
    void pageProgress(int generation, qint64 pageIndex, int percent);

    /// Page was finished (in any final state)
    void pageFinished(int generation, pdf::PDFOCRPageResult result);

    /// Job progress (finished pages / total)
    void jobProgress(int generation, int finished, int total);

    /// Job finished
    void jobFinished(int generation, pdf::PDFOCRJobSummary summary);

private:
    struct Job
    {
        PDFOCRJobDescription description;
        int generation = 0;
        std::shared_ptr<PDFOCRCancelToken> cancelToken;
        std::deque<size_t> queue;
        std::set<PDFInteger> inProgress;
        PDFOCRJobSummary summary;
        int finishedPages = 0;
        int activeWorkers = 0;
        QElapsedTimer timer;
        bool criticalErrorReported = false;
    };

    void workerMain(std::shared_ptr<Job> job);
    PDFOCRPageResult processPage(Job& job, const PDFOCRPageTask& task, PDFOCREngine* engine, const PDFOperationControl* operationControl);
    void finishPage(Job& job, PDFOCRPageResult result);
    void finishJob(Job& job);
    bool acquireMemory(Job& job, qint64 bytes, const PDFOperationControl* operationControl);
    void releaseMemory(qint64 bytes);

    const PDFDocument* m_document = nullptr;
    const PDFFontCache* m_fontCache = nullptr;
    const PDFCMS* m_cms = nullptr;
    const PDFOptionalContentActivity* m_optionalContentActivity = nullptr;
    const PDFMeshQualitySettings* m_meshQualitySettings = nullptr;
    RendererEngine m_rendererEngine = RendererEngine::QPainter;

    QThreadPool m_threadPool;
    mutable QMutex m_mutex;
    QWaitCondition m_memoryCondition;
    std::shared_ptr<Job> m_job;
    std::atomic<bool> m_running = { false };
    std::atomic<bool> m_stopping = { false };
    int m_generation = 0;
    qint64 m_memoryUsed = 0;
    qint64 m_memoryBudget = qint64(1) << 30;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFOCRPageResult)
Q_DECLARE_METATYPE(pdf::PDFOCRJobSummary)

#endif // PDFOCRJOBCONTROLLER_H
