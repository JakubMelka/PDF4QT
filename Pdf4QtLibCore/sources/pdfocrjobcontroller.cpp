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
#include "pdfdocument.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfexception.h"

#include <QRunnable>
#include <QDateTime>

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
    const int workerCount = qBound(1, qMin(job->description.configuration.workerCount, int(qMin<size_t>(job->description.pages.size(), 64))), 64);
    job->activeWorkers = workerCount;

    {
        QMutexLocker lock(&m_mutex);
        m_job = job;
        m_memoryBudget = qMax<qint64>(job->description.configuration.memoryBudget, qint64(64) << 20);
        m_memoryUsed = 0;
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

bool PDFOCRJobController::acquireMemory(Job& job, qint64 bytes, const PDFOperationControl* operationControl)
{
    Q_UNUSED(job);

    QMutexLocker lock(&m_mutex);
    while (m_memoryUsed > 0 && m_memoryUsed + bytes > m_memoryBudget)
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
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
    m_memoryUsed = qMax<qint64>(0, m_memoryUsed - bytes);
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

    PDFOCRError criticalError = createEngine();

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

    if (!engine)
    {
        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InitializationFailed, PDFTranslationContext::tr("OCR engine is not available."), PDFTranslationContext::tr("Initialization")));
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        return finishWithError(cancelledError(PDFTranslationContext::tr("Scheduling")));
    }

    // 1. Preparing: rasterization
    Q_EMIT pageStateChanged(job.generation, task.pageIndex, int(PDFOCRPageState::Preparing), PDFTranslationContext::tr("Rendering"));

    const PDFCatalog* catalog = m_document->getCatalog();
    if (task.pageIndex < 0 || size_t(task.pageIndex) >= catalog->getPageCount())
    {
        return finishWithError(PDFOCRError::create(PDFOCRErrorCode::RasterizationFailed, PDFTranslationContext::tr("Page %1 does not exist.").arg(task.pageIndex + 1), PDFTranslationContext::tr("Rendering")));
    }

    const PDFPage* page = catalog->getPage(task.pageIndex);
    const double limitedDpi = PDFOCRPagePreparer::getLimitedDpi(page, configuration.dpi, job.description.maximumRasterPixels);
    const qint64 rasterBytes = PDFOCRPagePreparer::estimateRasterBytes(page, limitedDpi) * 3;

    if (!acquireMemory(job, rasterBytes, operationControl))
    {
        return finishWithError(cancelledError(PDFTranslationContext::tr("Rendering")));
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

    PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(task.pageIndex, configuration.dpi, task.maskedRectangles, job.description.maximumRasterPixels, operationControl);
    if (raster.error)
    {
        return finishWithError(raster.error);
    }

    if (!qFuzzyCompare(raster.geometry.dpi, raster.geometry.requestedDpi))
    {
        raster.geometry.pipeline << QStringLiteral("dpi-limited(requested=%1,used=%2)").arg(raster.geometry.requestedDpi).arg(raster.geometry.dpi);
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

    // 3. Orientation detection
    std::optional<PDFOCROrientation> orientation;
    if (configuration.preprocessing.autoOrientation || configuration.preprocessing.deskew)
    {
        if (engine->getCapabilities().supportsOrientationDetection && job.description.models.hasOrientationData)
        {
            Q_EMIT pageStateChanged(job.generation, task.pageIndex, int(PDFOCRPageState::Preparing), PDFTranslationContext::tr("Orientation detection"));
            PDFOCRError orientationError;
            orientation = engine->detectOrientation(raster.image, raster.geometry.dpi, operationControl, &orientationError);
            if (orientation)
            {
                result.orientation = orientation;
                raster.geometry.pipeline << QStringLiteral("orientation(rotation=%1,confidence=%2)").arg(orientation->rotation).arg(orientation->confidence.value_or(-1.0), 0, 'f', 1);
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

        // Effective configuration of the region (PAGE-06)
        PDFOCRConfiguration effectiveConfiguration = configuration;
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

            if (region->configuration.rotation >= 0 && region->configuration.rotation != configuration.preprocessing.rotation)
            {
                result.geometry.pipeline << QStringLiteral("region(%1,rotation-override-ignored)").arg(regionId);
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
        input.image = engineImage;
        input.dpi = result.geometry.dpi;
        input.region = rect;
        input.configuration = effectiveConfiguration;
        input.models = job.description.models;

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

        // Output must belong to the input image (ARCH-04)
        if (!output.imageSize.isEmpty() && output.imageSize != engineImage.size())
        {
            return finishWithError(PDFOCRError::create(PDFOCRErrorCode::InvalidEngineOutput,
                                                       PDFTranslationContext::tr("Engine returned coordinates of a different image (%1 x %2 instead of %3 x %4).").arg(output.imageSize.width()).arg(output.imageSize.height()).arg(engineImage.width()).arg(engineImage.height()),
                                                       PDFTranslationContext::tr("Recognition")));
        }

        // Validate the output (OPS-05, AT-24)
        for (const PDFOCRRawBlock& block : output.blocks)
        {
            for (const PDFOCRRawLine& line : block.lines)
            {
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

        PDFOCRPagePreparer::appendOutput(result, output, result.geometry, regionId, excludedRectangles);
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
    result.elapsedMilliseconds = timer.elapsed();
    return result;
}

}   // namespace pdf
