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

#include "pdfocrjobcontroller.h"
#include "pdfocrmodelmanager.h"
#include "pdfdocument.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfexception.h"

#include <QRunnable>
#include <QDateTime>
#include <QLockFile>
#include <QDirIterator>
#include <QRegularExpression>

namespace pdf
{

PDFOCRJobController::PDFOCRJobController(QObject* parent) :
    QObject(parent)
{
    qRegisterMetaType<pdf::PDFOCRPageResult>("pdf::PDFOCRPageResult");
    qRegisterMetaType<pdf::PDFOCRJobSummary>("pdf::PDFOCRJobSummary");

    m_threadPool.setMaxThreadCount(2);
    m_threadPool.setExpiryTimeout(30000);
}

PDFOCRJobController::~PDFOCRJobController()
{
    stop();
    waitForFinished();
}

void PDFOCRJobController::setEnvironment(const PDFDocument* document,
                                         const PDFFontCache* fontCache,
                                         const PDFCMS* cms,
                                         const PDFOptionalContentActivity* optionalContentActivity,
                                         const PDFMeshQualitySettings* meshQualitySettings,
                                         RendererEngine rendererEngine)
{
    Q_ASSERT(!isRunning());

    m_document = document;
    m_fontCache = fontCache;
    m_cms = cms;
    m_optionalContentActivity = optionalContentActivity;
    m_meshQualitySettings = meshQualitySettings;
    m_rendererEngine = rendererEngine;
}

bool PDFOCRJobController::start(PDFOCRJobDescription description, int* generation)
{
    if (isRunning() || !m_document || !m_fontCache || !m_cms || !m_meshQualitySettings)
    {
        return false;
    }

    auto job = std::make_shared<Job>();
    job->description = std::move(description);
    job->generation = ++m_generation;
    job->cancelToken = std::make_shared<PDFOCRCancelToken>();
    job->summary.generation = job->generation;
    job->summary.totalPages = int(job->description.pages.size());
    job->timer.start();

    for (size_t i = 0; i < job->description.pages.size(); ++i)
    {
        job->queue.push_back(i);
    }

    // Every worker owns an engine instance with loaded models (JOB-09), so no more
    // workers than pages are started.
    const int requestedWorkerCount = qBound(1, qMin(job->description.configuration.workerCount, int(qMin<size_t>(job->description.pages.size(), 64))), 64);
    int workerCount = requestedWorkerCount;

    // Memory of the models (JOB-10, R13): every worker loads its own copy of the model
    // set, so the model memory is reserved from the budget for the lifetime of the job
    // and the number of workers is reduced, when the models of all workers do not fit.
    // When even a single worker does not fit, the job fails with a critical error.
    const qint64 memoryBudget = qMax<qint64>(job->description.configuration.memoryBudget, qint64(64) << 20);
    const qint64 modelBytes = estimateModelBytes(job->description.models.dataPath);
    while (workerCount > 1 && modelBytes * workerCount > memoryBudget)
    {
        --workerCount;
    }

    if (modelBytes > memoryBudget)
    {
        job->criticalErrorReported = true;
        job->summary.criticalError = PDFOCRError::create(PDFOCRErrorCode::OutOfMemory,
                                                         PDFTranslationContext::tr("Language models need approximately %1 MB of memory, but the memory budget is %2 MB. Increase the memory budget or select fewer languages.").arg(modelBytes / (1024 * 1024) + 1).arg(memoryBudget / (1024 * 1024)),
                                                         PDFTranslationContext::tr("Initialization"));
        job->cancelToken->cancel();
        workerCount = 1;
    }

    job->summary.requestedWorkerCount = requestedWorkerCount;
    job->summary.workerCount = workerCount;
    job->summary.modelMemoryBytes = modelBytes;
    job->activeWorkers = workerCount;

    // A running job keeps its runtime model set alive (LANG-07): the housekeeping and
    // the cache cleanup of the model manager skip the sets in use.
    job->runtimeSetLease = PDFOCRModelManager::acquireRuntimeSetLease(job->description.models.dataPath);

    {
        QMutexLocker lock(&m_mutex);
        m_job = job;
        m_memoryBudget = memoryBudget;
        m_memoryReserved = job->criticalErrorReported ? 0 : modelBytes * workerCount;
        m_memoryUsed = m_memoryReserved;
    }

    m_stopping.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);

    if (generation)
    {
        *generation = job->generation;
    }

    m_threadPool.setMaxThreadCount(workerCount);
    for (int i = 0; i < workerCount; ++i)
    {
        m_threadPool.start([this, job]() { workerMain(job); });
    }

    return true;
}

void PDFOCRJobController::stop()
{
    std::shared_ptr<Job> job;
    {
        QMutexLocker lock(&m_mutex);
        job = m_job;
    }

    if (job && isRunning())
    {
        m_stopping.store(true, std::memory_order_release);
        job->cancelToken->cancel();
        m_memoryCondition.wakeAll();
    }
}

void PDFOCRJobController::waitForFinished()
{
    m_threadPool.waitForDone();
}

std::vector<PDFInteger> PDFOCRJobController::getPendingPages() const
{
    std::vector<PDFInteger> result;
    QMutexLocker lock(&m_mutex);
    if (m_job && isRunning())
    {
        for (PDFInteger pageIndex : m_job->inProgress)
        {
            result.push_back(pageIndex);
        }
        for (size_t index : m_job->queue)
        {
            result.push_back(m_job->description.pages[index].pageIndex);
        }
    }
    return result;
}

bool PDFOCRJobController::isPageInProgress(PDFInteger pageIndex) const
{
    QMutexLocker lock(&m_mutex);
    return m_job && isRunning() && m_job->inProgress.count(pageIndex) > 0;
}

bool PDFOCRJobController::isPagePending(PDFInteger pageIndex) const
{
    const std::vector<PDFInteger> pending = getPendingPages();
    return std::find(pending.begin(), pending.end(), pageIndex) != pending.end();
}

qint64 PDFOCRJobController::estimateModelBytes(const QString& dataPath)
{
    if (dataPath.isEmpty() || !QDir(dataPath).exists())
    {
        return 0;
    }

    qint64 bytes = 0;
    QDirIterator it(dataPath, QStringList() << QStringLiteral("*.traineddata"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        bytes += qMax<qint64>(0, it.fileInfo().size());
    }
    return bytes;
}

bool PDFOCRJobController::acquireMemory(Job& job, qint64 bytes, const PDFOperationControl* operationControl, PDFOCRError* error)
{
    Q_UNUSED(job);

    QMutexLocker lock(&m_mutex);

    // A single request larger than the budget left for the rasters is refused: the
    // budget is a hard limit, it is never bypassed and it is pointless to wait (R13).
    const qint64 availableForRasters = m_memoryBudget - m_memoryReserved;
    if (bytes > availableForRasters)
    {
        if (error)
        {
            *error = PDFOCRError::create(PDFOCRErrorCode::OutOfMemory,
                                         PDFTranslationContext::tr("Page needs approximately %1 MB of memory for the rasters, but only %2 MB of the memory budget of %3 MB are available. Use a lower resolution, recognize a smaller region of the page, or increase the memory budget.").arg(bytes / (1024 * 1024) + 1).arg(qMax<qint64>(0, availableForRasters) / (1024 * 1024)).arg(m_memoryBudget / (1024 * 1024)),
                                         PDFTranslationContext::tr("Rendering"));
        }
        return false;
    }

    while (m_memoryUsed + bytes > m_memoryBudget)
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            if (error)
            {
                *error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was stopped."), PDFTranslationContext::tr("Rendering"));
            }
            return false;
        }
        m_memoryCondition.wait(&m_mutex, 100);
    }

    m_memoryUsed += bytes;
    return true;
}

void PDFOCRJobController::releaseMemory(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    m_memoryUsed = qMax<qint64>(m_memoryReserved, m_memoryUsed - bytes);
    m_memoryCondition.wakeAll();
}

void PDFOCRJobController::workerMain(std::shared_ptr<Job> job)
{
    const PDFOperationControl* operationControl = job->cancelToken.get();

    // Engine is a foreign code (OPS-05): an exception thrown by the engine must
    // end as an error of the page or of the job, never as a terminated application.
    auto createCrashError = [](const QString& details, const QString& step)
    {
        return PDFOCRError::create(PDFOCRErrorCode::WorkerCrashed, PDFTranslationContext::tr("OCR engine failed unexpectedly."), step, details);
    };

    std::unique_ptr<PDFOCREngine> engine;

    auto createEngine = [&]() -> PDFOCRError
    {
        try
        {
            engine = PDFOCREngineRegistry::getInstance()->createEngine(job->description.configuration.engineId);

            if (!engine)
            {
                return PDFOCRError::create(PDFOCRErrorCode::InitializationFailed,
                                           PDFTranslationContext::tr("OCR engine '%1' is not available.").arg(job->description.configuration.engineId),
                                           PDFTranslationContext::tr("Initialization"));
            }

            PDFOCRError error = engine->validateConfiguration(job->description.configuration, job->description.models);
            if (!error)
            {
                error = engine->prepare(job->description.configuration, job->description.models);
            }
            return error;
        }
        catch (const PDFException& exception)
        {
            return createCrashError(exception.getMessage(), PDFTranslationContext::tr("Initialization"));
        }
        catch (const std::exception& exception)
        {
            return createCrashError(QString::fromLocal8Bit(exception.what()), PDFTranslationContext::tr("Initialization"));
        }
        catch (...)
        {
            return createCrashError(QString(), PDFTranslationContext::tr("Initialization"));
        }
    };

    auto destroyEngine = [&]()
    {
        try
        {
            if (engine)
            {
                engine->release();
            }
        }
        catch (...)
        {
            // Engine is in an unknown state, nothing else can be done
        }
        engine.reset();
    };

    bool alreadyFailed = false;
    {
        QMutexLocker lock(&m_mutex);
        alreadyFailed = job->criticalErrorReported;
    }

    // The engine is not created, when the job already failed at its start (models
    // do not fit into the memory budget); the pages are finished with the error below.
    PDFOCRError criticalError = alreadyFailed ? PDFOCRError::none() : createEngine();

    if (criticalError)
    {
        // Critical error stops the scheduling (JOB-07)
        QMutexLocker lock(&m_mutex);
        if (!job->criticalErrorReported)
        {
            job->criticalErrorReported = true;
            job->summary.criticalError = criticalError;
        }
        job->cancelToken->cancel();
    }

    while (true)
    {
        size_t taskIndex = 0;
        {
            QMutexLocker lock(&m_mutex);
            if (job->queue.empty())
            {
                break;
            }

            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                // Remaining pages are finished as cancelled / error
                while (!job->queue.empty())
                {
                    const PDFOCRPageTask& task = job->description.pages[job->queue.front()];
                    job->queue.pop_front();

                    PDFOCRPageResult result;
                    result.pageIndex = task.pageIndex;
                    result.pageLabel = task.pageLabel;
                    result.pageFingerprint = task.pageFingerprint;
                    result.analysis = task.analysis;
                    result.regions = task.regions;
                    result.generation = task.generation;
                    if (job->summary.criticalError)
                    {
                        result.state = PDFOCRPageState::Error;
                        result.error = job->summary.criticalError;
                    }
                    else
                    {
                        result.state = PDFOCRPageState::Cancelled;
                        result.error = PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was stopped."), PDFTranslationContext::tr("Scheduling"));
                    }

                    lock.unlock();
                    finishPage(*job, std::move(result));
                    lock.relock();
                }
                break;
            }

            taskIndex = job->queue.front();
            job->queue.pop_front();
            job->inProgress.insert(job->description.pages[taskIndex].pageIndex);
        }

        const PDFOCRPageTask& task = job->description.pages[taskIndex];
        PDFOCRPageResult result;
        std::optional<QString> crashDetails;

        try
        {
            result = processPage(*job, task, engine.get(), operationControl);
        }
        catch (const PDFException& exception)
        {
            crashDetails = exception.getMessage();
        }
        catch (const std::exception& exception)
        {
            crashDetails = QString::fromLocal8Bit(exception.what());
        }
        catch (...)
        {
            crashDetails = QString();
        }

        if (crashDetails)
        {
            result = PDFOCRPageResult();
            result.pageIndex = task.pageIndex;
            result.pageLabel = task.pageLabel;
            result.pageFingerprint = task.pageFingerprint;
            result.analysis = task.analysis;
            result.regions = task.regions;
            result.generation = task.generation;
            result.state = PDFOCRPageState::Error;
            result.error = createCrashError(*crashDetails, PDFTranslationContext::tr("Recognition"));

            // State of the engine instance is unknown after the exception, the
            // remaining pages of this worker get a new instance.
            destroyEngine();
            const PDFOCRError engineError = createEngine();
            if (engineError)
            {
                destroyEngine();
            }
        }

        finishPage(*job, std::move(result));
    }

    destroyEngine();

    bool last = false;
    {
        QMutexLocker lock(&m_mutex);
        --job->activeWorkers;
        last = job->activeWorkers == 0;
    }

    if (last)
    {
        finishJob(*job);
    }
}

void PDFOCRJobController::finishPage(Job& job, PDFOCRPageResult result)
{
    int finished = 0;
    int total = 0;
    {
        QMutexLocker lock(&m_mutex);
        job.inProgress.erase(result.pageIndex);
        ++job.finishedPages;
        finished = job.finishedPages;
        total = job.summary.totalPages;

        switch (result.state)
        {
            case PDFOCRPageState::Done:
                ++job.summary.donePages;
                break;
            case PDFOCRPageState::NoText:
                ++job.summary.noTextPages;
                break;
            case PDFOCRPageState::Error:
                ++job.summary.errorPages;
                break;
            case PDFOCRPageState::Cancelled:
                ++job.summary.cancelledPages;
                break;
            case PDFOCRPageState::Skipped:
                ++job.summary.skippedPages;
                break;
            default:
                break;
        }
    }

    Q_EMIT pageFinished(job.generation, std::move(result));
    Q_EMIT jobProgress(job.generation, finished, total);
}

void PDFOCRJobController::finishJob(Job& job)
{
    PDFOCRJobSummary summary;
    {
        QMutexLocker lock(&m_mutex);
        job.summary.elapsedMilliseconds = job.timer.elapsed();
        job.summary.cancelled = job.cancelToken->isOperationCancelled() && !job.summary.criticalError;
        summary = job.summary;
        m_memoryUsed = 0;
        m_memoryReserved = 0;

        // The runtime set can be removed by the housekeeping from now on (LANG-07)
        job.runtimeSetLease.reset();
    }

    m_running.store(false, std::memory_order_release);
    m_stopping.store(false, std::memory_order_release);
    Q_EMIT jobFinished(job.generation, summary);
}

PDFOCRPageResult PDFOCRJobController::processPage(Job& job, const PDFOCRPageTask& task, PDFOCREngine* engine, const PDFOperationControl* operationControl)
{
    PDFOCRPageResult result;
    result.pageIndex = task.pageIndex;
    result.pageLabel = task.pageLabel;
    result.pageFingerprint = task.pageFingerprint;
    result.analysis = task.analysis;
    result.regions = task.regions;
    result.generation = task.generation;
    result.recognitionTime = QDateTime::currentDateTime();
    result.assignIdentifiers();

    const PDFOCRConfiguration& configuration = task.configuration;
    result.provenance.engineId = configuration.engineId;
    result.provenance.engineVersion = engine ? engine->getVersion() : QString();
    result.provenance.modelIds = job.description.models.modelIds;
    result.provenance.modelSetHash = job.description.models.hash;
    result.provenance.parameters = configuration.toParameterMap();

    QElapsedTimer timer;
    timer.start();

    auto finishWithError = [&](PDFOCRError error)
    {
        result.state = error.code == PDFOCRErrorCode::Cancelled ? PDFOCRPageState::Cancelled : PDFOCRPageState::Error;
        result.error = std::move(error);
        result.elapsedMilliseconds = timer.elapsed();
        return result;
    };

    auto cancelledError = [](const QString& step)
    {
        return PDFOCRError::create(PDFOCRErrorCode::Cancelled, PDFTranslationContext::tr("Recognition was stopped."), step);
    };

    // One shared deadline of the page for all phases (JOB-06, R12): orientation
    // detection, preprocessing and every recognized rectangle consume the same budget.
    const qint64 pageTimeoutMilliseconds = configuration.pageTimeoutSeconds > 0 ? qint64(configuration.pageTimeoutSeconds) * 1000 : -1;
    auto getRemainingMilliseconds = [&]() -> qint64
    {
        if (pageTimeoutMilliseconds < 0)
        {
            return -1;
        }
        return qMax<qint64>(0, pageTimeoutMilliseconds - timer.elapsed());
    };

    auto timeoutError = [&](const QString& step)
    {
        return PDFOCRError::create(PDFOCRErrorCode::Timeout,
                                   PDFTranslationContext::tr("Recognition of the page exceeded the time limit of %1 s.").arg(configuration.pageTimeoutSeconds),
                                   step);
    };

    auto isDeadlineExceeded = [&]()
    {
        return pageTimeoutMilliseconds >= 0 && timer.elapsed() >= pageTimeoutMilliseconds;
    };

    if (!engine)
    {
        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InitializationFailed, PDFTranslationContext::tr("OCR engine is not available."), PDFTranslationContext::tr("Initialization")));
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        return finishWithError(cancelledError(PDFTranslationContext::tr("Scheduling")));
    }

    const PDFOCREngineCapabilities capabilities = engine->getCapabilities();

    // Dimension limit of the engine (ARCH-02, R13): a long narrow page can pass the
    // pixel count limit and still exceed the maximal width or height of the engine.
    const int maximumDimension = capabilities.maximumImageSize.isEmpty() ? 0 : qMin(capabilities.maximumImageSize.width(), capabilities.maximumImageSize.height());

    // 1. Preparing: rasterization
    Q_EMIT pageStateChanged(job.generation, task.pageIndex, int(PDFOCRPageState::Preparing), PDFTranslationContext::tr("Rendering"));

    const PDFCatalog* catalog = m_document->getCatalog();
    if (task.pageIndex < 0 || size_t(task.pageIndex) >= catalog->getPageCount())
    {
        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::RasterizationFailed, PDFTranslationContext::tr("Page %1 does not exist.").arg(task.pageIndex + 1), PDFTranslationContext::tr("Rendering")));
    }

    const PDFPage* page = catalog->getPage(task.pageIndex);
    const double limitedDpi = PDFOCRPagePreparer::getLimitedDpi(page, configuration.dpi, job.description.maximumRasterPixels, maximumDimension);
    const qint64 rasterBytes = PDFOCRPagePreparer::estimateRasterBytes(page, limitedDpi) * 3;

    PDFOCRError memoryError;
    if (!acquireMemory(job, rasterBytes, operationControl, &memoryError))
    {
        return finishWithError(memoryError ? memoryError : cancelledError(PDFTranslationContext::tr("Rendering")));
    }

    auto memoryGuard = qScopeGuard([this, rasterBytes]() { releaseMemory(rasterBytes); });

    PDFOCRPagePreparer preparer(m_document, m_fontCache, m_cms, m_optionalContentActivity, *m_meshQualitySettings, m_rendererEngine);

    // Excluded rectangles: masked rectangles + excluded regions
    std::vector<QRectF> excludedRectangles = task.maskedRectangles;
    for (const PDFOCRRegion& region : task.regions)
    {
        if (region.type == PDFOCRRegionType::Exclude)
        {
            excludedRectangles.push_back(region.rect);
        }
    }

    PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(task.pageIndex, configuration.dpi, task.maskedRectangles, job.description.maximumRasterPixels, operationControl, maximumDimension);
    if (raster.error)
    {
        return finishWithError(raster.error);
    }

    if (!qFuzzyCompare(raster.geometry.dpi, raster.geometry.requestedDpi))
    {
        raster.geometry.pipeline << QStringLiteral("dpi-limited(requested=%1,used=%2)").arg(raster.geometry.requestedDpi).arg(raster.geometry.dpi);
    }

    if (job.summary.workerCount > 0 && job.summary.workerCount < job.summary.requestedWorkerCount)
    {
        raster.geometry.pipeline << QStringLiteral("workers-limited(requested=%1,used=%2,model-memory=%3MB)").arg(job.summary.requestedWorkerCount).arg(job.summary.workerCount).arg(job.summary.modelMemoryBytes / (1024 * 1024) + 1);
    }

    // 2. Blank page detection (IMAGE-07)
    if (configuration.detectBlankPages && !task.skipBlankDetection)
    {
        double inkRatio = 0.0;
        if (PDFOCRPagePreparer::isBlankImage(raster.image, &inkRatio, operationControl))
        {
            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                return finishWithError(cancelledError(PDFTranslationContext::tr("Blank page detection")));
            }

            result.geometry = raster.geometry;
            result.geometry.pipeline << QStringLiteral("blank(ink=%1)").arg(inkRatio, 0, 'f', 6);
            result.state = PDFOCRPageState::NoText;
            result.skipReason = PDFTranslationContext::tr("Page appears to be blank (ink ratio %1 %). Blank page detection can be disabled.").arg(inkRatio * 100.0, 0, 'f', 3);
            result.elapsedMilliseconds = timer.elapsed();
            return result;
        }
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        return finishWithError(cancelledError(PDFTranslationContext::tr("Rendering")));
    }

    if (isDeadlineExceeded())
    {
        return finishWithError(timeoutError(PDFTranslationContext::tr("Rendering")));
    }

    // 3. Orientation detection
    std::optional<PDFOCROrientation> orientation;
    if (configuration.preprocessing.autoOrientation || configuration.preprocessing.deskew)
    {
        if (capabilities.supportsOrientationDetection && job.description.models.hasOrientationData)
        {
            Q_EMIT pageStateChanged(job.generation, task.pageIndex, int(PDFOCRPageState::Preparing), PDFTranslationContext::tr("Orientation detection"));
            PDFOCRError orientationError;
            orientation = engine->detectOrientation(raster.image, raster.geometry.dpi, operationControl, &orientationError, getRemainingMilliseconds());
            if (orientation)
            {
                result.orientation = orientation;
                raster.geometry.pipeline << QStringLiteral("orientation(rotation=%1,confidence=%2)").arg(orientation->rotation).arg(orientation->confidence.value_or(-1.0), 0, 'f', 1);
            }
            else if (orientationError.code == PDFOCRErrorCode::Timeout)
            {
                return finishWithError(orientationError);
            }
            else
            {
                raster.geometry.pipeline << QStringLiteral("orientation(not-detected)");
            }
        }
        else if (configuration.preprocessing.autoOrientation)
        {
            raster.geometry.pipeline << QStringLiteral("orientation(unavailable)");
        }
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        return finishWithError(cancelledError(PDFTranslationContext::tr("Orientation detection")));
    }

    if (isDeadlineExceeded())
    {
        return finishWithError(timeoutError(PDFTranslationContext::tr("Orientation detection")));
    }

    // 4. Preprocessing
    Q_EMIT pageStateChanged(job.generation, task.pageIndex, int(PDFOCRPageState::Preparing), PDFTranslationContext::tr("Preprocessing"));
    PDFOCRPagePreparer::PreprocessResult preprocessed = PDFOCRPagePreparer::preprocess(raster.image, raster.geometry, configuration.preprocessing, orientation, operationControl);
    if (preprocessed.error)
    {
        return finishWithError(preprocessed.error);
    }

    raster.image = QImage();
    result.geometry = preprocessed.geometry;

    if (!result.geometry.isInvertible())
    {
        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::IrreversibleTransformation, PDFTranslationContext::tr("Transformation of the page raster is not invertible."), PDFTranslationContext::tr("Preprocessing")));
    }

    if (isDeadlineExceeded())
    {
        return finishWithError(timeoutError(PDFTranslationContext::tr("Preprocessing")));
    }

    // Rotation applied to the whole page by the preprocessing (manual or detected),
    // needed for the rotation override of the regions (REGION-02)
    int appliedPageRotation = 0;
    {
        static const QRegularExpression rotateExpression(QStringLiteral("^rotate\\((\\d+)\\)$"));
        for (const QString& step : result.geometry.pipeline)
        {
            const QRegularExpressionMatch match = rotateExpression.match(step);
            if (match.hasMatch())
            {
                appliedPageRotation = match.captured(1).toInt();
            }
        }
    }

    // 5. Region masks
    const QTransform pageToEngine = result.geometry.getPageToEngine();
    QImage engineImage = PDFOCRPagePreparer::maskRegions(preprocessed.image, pageToEngine, task.regions);
    preprocessed.image = QImage();

    const std::vector<std::pair<int, QRect>> rectangles = PDFOCRPagePreparer::getRecognitionRectangles(engineImage.size(), pageToEngine, task.regions);
    if (rectangles.empty())
    {
        result.state = PDFOCRPageState::NoText;
        result.skipReason = PDFTranslationContext::tr("No region to recognize.");
        result.elapsedMilliseconds = timer.elapsed();
        return result;
    }

    // 6. Recognition
    Q_EMIT pageStateChanged(job.generation, task.pageIndex, int(PDFOCRPageState::Recognizing), PDFTranslationContext::tr("Recognition"));

    PDFOCRConfiguration preparedConfiguration = job.description.configuration;

    // Engine prepared for an exception of a page or region must be returned to the
    // configuration of the job on every path (also on errors and cancellation),
    // otherwise the next page would be silently recognized with wrong languages.
    auto restoreGuard = qScopeGuard([&]()
    {
        if (preparedConfiguration.languages != job.description.configuration.languages || preparedConfiguration.layout != job.description.configuration.layout)
        {
            try
            {
                engine->prepare(job.description.configuration, job.description.models);
            }
            catch (...)
            {
                // Reported by the next page, which fails in the recognition
            }
        }
    });

    size_t rectangleIndex = 0;
    for (const auto& item : rectangles)
    {
        const int regionId = item.first;
        const QRect& rect = item.second;

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            return finishWithError(cancelledError(PDFTranslationContext::tr("Recognition")));
        }

        if (isDeadlineExceeded())
        {
            return finishWithError(timeoutError(PDFTranslationContext::tr("Recognition")));
        }

        // Effective configuration of the region (PAGE-06)
        PDFOCRConfiguration effectiveConfiguration = configuration;
        QImage inputImage = engineImage;
        QRect inputRegion = rect;
        QTransform outputToEngine;

        if (const PDFOCRRegion* region = result.findRegion(regionId))
        {
            effectiveConfiguration = PDFOCRConfigurationResolver::resolve(configuration, nullptr, &region->configuration);

            // Region languages must be a subset of the job languages (models are resolved for the job)
            bool languagesAvailable = true;
            for (const QString& language : effectiveConfiguration.languages)
            {
                if (!job.description.models.languages.contains(language))
                {
                    languagesAvailable = false;
                    break;
                }
            }

            if (!languagesAvailable)
            {
                effectiveConfiguration.languages = configuration.languages;
                result.geometry.pipeline << QStringLiteral("region(%1,languages-not-resolved)").arg(regionId);
            }

            // Rotation override of the region (REGION-02): the region is cut out of the
            // engine image, rotated by the difference to the rotation of the page and
            // recognized as an own image; the output is mapped back through outputToEngine.
            if (region->configuration.rotation >= 0)
            {
                const int overrideRotation = ((region->configuration.rotation % 360) + 360) % 360;
                const int delta = ((overrideRotation - appliedPageRotation) % 360 + 360) % 360;

                if (delta % 90 != 0)
                {
                    result.geometry.pipeline << QStringLiteral("region(%1,rotation-override-invalid=%2)").arg(regionId).arg(region->configuration.rotation);
                }
                else if (delta != 0)
                {
                    const QRect cropRect = rect.intersected(engineImage.rect());
                    const QImage crop = engineImage.copy(cropRect);

                    QTransform rotation;
                    rotation.rotate(delta);

                    // QImage::transformed uses the true matrix (the rotation followed by the
                    // translation of the bounding rectangle into the origin), so the inverse
                    // of the true matrix maps the rotated crop back into the crop, and the
                    // translation by the crop origin maps the crop into the engine image.
                    const QTransform trueMatrix = QImage::trueMatrix(rotation, crop.width(), crop.height());
                    inputImage = crop.transformed(rotation, Qt::FastTransformation);
                    inputRegion = inputImage.rect();
                    outputToEngine = trueMatrix.inverted() * QTransform::fromTranslate(cropRect.left(), cropRect.top());
                    result.geometry.pipeline << QStringLiteral("region(%1,rotation=%2,applied=%3)").arg(regionId).arg(overrideRotation).arg(delta);
                }
                else
                {
                    result.geometry.pipeline << QStringLiteral("region(%1,rotation=%2)").arg(regionId).arg(overrideRotation);
                }
            }
        }

        // Re-prepare the engine, if configuration of the region differs
        if (effectiveConfiguration.languages != preparedConfiguration.languages ||
            effectiveConfiguration.layout != preparedConfiguration.layout)
        {
            const PDFOCRError prepareError = engine->prepare(effectiveConfiguration, job.description.models);
            if (prepareError)
            {
                return finishWithError(prepareError);
            }
            preparedConfiguration = effectiveConfiguration;
        }

        PDFOCRRecognitionInput input;
        input.image = inputImage;
        input.dpi = result.geometry.dpi;
        input.region = inputRegion;
        input.configuration = effectiveConfiguration;
        input.models = job.description.models;
        input.remainingMilliseconds = getRemainingMilliseconds();

        const size_t currentRectangle = rectangleIndex;
        const size_t rectangleCount = rectangles.size();
        auto progressCallback = [this, &job, &task, currentRectangle, rectangleCount](int percent)
        {
            int total = -1;
            if (percent >= 0)
            {
                total = int((100 * currentRectangle + qBound(0, percent, 100)) / rectangleCount);
            }
            Q_EMIT pageProgress(job.generation, task.pageIndex, total);
        };

        PDFOCRRecognitionOutput output = engine->recognize(input, operationControl, progressCallback);

        if (output.cancelled)
        {
            return finishWithError(cancelledError(PDFTranslationContext::tr("Recognition")));
        }

        if (output.error)
        {
            return finishWithError(output.error);
        }

        // The deadline of the page is a hard limit also for an engine, which does not
        // honour it itself (JOB-06)
        if (isDeadlineExceeded())
        {
            return finishWithError(timeoutError(PDFTranslationContext::tr("Recognition")));
        }

        // Output must belong to the input image (ARCH-04)
        if (!output.imageSize.isEmpty() && output.imageSize != inputImage.size())
        {
            return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput,
                                                       PDFTranslationContext::tr("Engine returned coordinates of a different image (%1 x %2 instead of %3 x %4).").arg(output.imageSize.width()).arg(output.imageSize.height()).arg(inputImage.width()).arg(inputImage.height()),
                                                       PDFTranslationContext::tr("Recognition")));
        }

        // Validate the output (OPS-05, AT-24): hard limits of the element counts and of the geometry
        constexpr size_t MaximumBlocksPerPage = 10000;
        constexpr size_t MaximumWordsPerPage = 100000;
        if (output.blocks.size() > MaximumBlocksPerPage)
        {
            return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput, PDFTranslationContext::tr("Engine returned too many blocks (%1, at most %2 are allowed).").arg(output.blocks.size()).arg(MaximumBlocksPerPage), PDFTranslationContext::tr("Recognition")));
        }

        size_t wordCount = 0;
        for (const PDFOCRRawBlock& block : output.blocks)
        {
            for (const PDFOCRRawLine& line : block.lines)
            {
                wordCount += line.words.empty() ? 1 : line.words.size();
                if (wordCount > MaximumWordsPerPage)
                {
                    return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput, PDFTranslationContext::tr("Engine returned too many words (more than %1).").arg(MaximumWordsPerPage), PDFTranslationContext::tr("Recognition")));
                }

                for (const PDFOCRRawWord& word : line.words)
                {
                    const qreal values[] = { word.rect.left(), word.rect.top(), word.rect.width(), word.rect.height() };
                    for (qreal value : values)
                    {
                        if (!std::isfinite(value) || std::abs(value) > 1.0e7)
                        {
                            return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput, PDFTranslationContext::tr("Engine returned invalid geometry."), PDFTranslationContext::tr("Recognition")));
                        }
                    }

                    if (word.text.size() > 4096)
                    {
                        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput, PDFTranslationContext::tr("Engine returned too long text."), PDFTranslationContext::tr("Recognition")));
                    }
                }
            }
        }

        if (output.orientation && !result.orientation)
        {
            result.orientation = output.orientation;
        }

        PDFOCRPagePreparer::appendOutput(result, output, result.geometry, regionId, excludedRectangles, outputToEngine);
        ++rectangleIndex;
    }

    const QStringList validationErrors = PDFOCRValidator::validate(result);
    if (!validationErrors.isEmpty())
    {
        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput, validationErrors.front(), PDFTranslationContext::tr("Recognition"), validationErrors.join(QChar('\n'))));
    }

    result.state = result.hasUsableText() ? PDFOCRPageState::Done : PDFOCRPageState::NoText;
    if (result.state == PDFOCRPageState::NoText)
    {
        result.skipReason = PDFTranslationContext::tr("No text was found on the page.");
    }

    // The raw recognition is kept next to the editable blocks (DATA-02)
    result.originalBlocks = result.blocks;
    result.elapsedMilliseconds = timer.elapsed();
    return result;
}

}   // namespace pdf
