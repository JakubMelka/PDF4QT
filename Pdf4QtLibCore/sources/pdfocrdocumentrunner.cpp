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

#include "pdfocrdocumentrunner.h"
#include "pdfocrsession.h"
#include "pdfocrmodelmanager.h"
#include "pdfocrengine.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrexport.h"
#include "pdfdocumentreader.h"
#include "pdfexecutionpolicy.h"
#include "pdfoptionalcontent.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfconstants.h"

#include <QDir>
#include <QMutex>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>
#include <QEventLoop>

#include <algorithm>
#include <numeric>
#include <set>

namespace pdf
{

namespace
{

/// Disables the shrinking of the font cache for the time of the run (the pages are
/// rendered concurrently) and enables it again, whatever happens
class PDFFontCacheShrinkGuard
{
public:
    explicit PDFFontCacheShrinkGuard(PDFFontCache* fontCache) :
        m_fontCache(fontCache)
    {
        m_fontCache->setCacheShrinkEnabled(nullptr, false);
    }

    ~PDFFontCacheShrinkGuard()
    {
        m_fontCache->setCacheShrinkEnabled(nullptr, true);
    }

private:
    PDFFontCache* m_fontCache;
};

}   // namespace

int PDFOCRDocumentRunner::Result::getFailedPageCount() const
{
    return int(std::count_if(records.cbegin(), records.cend(), [](const PageRecord& record)
    {
        return record.state == PDFOCRPageState::Error || record.state == PDFOCRPageState::Cancelled;
    }));
}

int PDFOCRDocumentRunner::Result::getResultPageCount() const
{
    return int(std::count_if(records.cbegin(), records.cend(), [](const PageRecord& record)
    {
        return record.state == PDFOCRPageState::Done || record.state == PDFOCRPageState::NoText;
    }));
}

PDFOCRConfidenceStatistics PDFOCRDocumentRunner::Result::getStatistics() const
{
    PDFOCRConfidenceStatistics statistics;
    for (const PageRecord& record : records)
    {
        if (record.state == PDFOCRPageState::Done || record.state == PDFOCRPageState::NoText)
        {
            statistics.merge(record.statistics);
        }
    }
    return statistics;
}

PDFOCRDocumentRunner::Result PDFOCRDocumentRunner::run(const PDFDocument* document,
                                                       const Settings& settings,
                                                       const ProgressCallback& progress,
                                                       const PDFOperationControl* operationControl)
{
    Result result;
    result.session = std::make_shared<PDFOCRSession>(nullptr);

    auto fail = [&result](PDFOCRErrorCode code, const QString& message)
    {
        result.errorCode = code;
        result.errorMessage = message;
        return result;
    };

    if (!document || !document->getCatalog())
    {
        return fail(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("No document to recognize."));
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());

    // Selected pages
    std::vector<PDFInteger> pages = settings.pages;
    if (pages.empty())
    {
        pages.resize(size_t(pageCount));
        std::iota(pages.begin(), pages.end(), PDFInteger(0));
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    if (!pages.empty() && (pages.front() < 0 || pages.back() >= pageCount))
    {
        return fail(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Invalid page selection, the document has %n page(s).", nullptr, int(pageCount)));
    }
    result.pages = pages;

    // Validation of the configuration (REC-03)
    const PDFOCRConfiguration& configuration = settings.configuration;
    QStringList errors = configuration.validate();
    std::shared_ptr<PDFOCREngineFactory> factory = PDFOCREngineRegistry::getInstance()->getFactory(configuration.engineId);
    PDFOCREngineCapabilities capabilities;
    QString unavailableReason;
    if (!factory || !factory->isAvailable(&unavailableReason))
    {
        errors << PDFTranslationContext::tr("OCR engine '%1' is not available. %2").arg(configuration.engineId, unavailableReason);
    }
    else
    {
        capabilities = factory->getCapabilities();
        QStringList parameterErrors;
        PDFOCRConfiguration::validateEngineParameters(configuration.engineParameters, capabilities.parameters, &parameterErrors);
        errors << parameterErrors;
    }

    if (!errors.isEmpty())
    {
        return fail(PDFOCRErrorCode::InvalidConfiguration, errors.join(QChar('\n')));
    }

    if (factory->usesManagedModels() && !settings.modelManager)
    {
        return fail(PDFOCRErrorCode::MissingModel, PDFTranslationContext::tr("The language models cannot be resolved, no model manager was given."));
    }

    // Rendering environment of the analysis and of the recognition
    PDFOptionalContentActivity optionalContentActivity(const_cast<PDFDocument*>(document), OCUsage::View, nullptr);
    PDFCMSGeneric genericCms;
    const PDFCMS* cms = settings.cms ? settings.cms : &genericCms;
    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    PDFFontCacheShrinkGuard fontCacheGuard(&fontCache);

    // Session: identity of the document and the configuration
    const PDFOCRApplyProcessor::Context applyContext = PDFOCRApplyProcessor::createContext(document, settings.fileName);
    PDFOCRDocumentIdentity identity;
    identity.fileName = QFileInfo(settings.fileName).fileName();
    identity.sourceHash = document->getSourceDataHash();
    identity.pageCount = pageCount;
    identity.isEncrypted = applyContext.isEncrypted;

    PDFOCRSession* session = result.session.get();
    session->setDocument(document, identity);
    session->setConfiguration(configuration);

    auto isCancelled = [operationControl]() { return PDFOperationControl::isOperationCancelled(operationControl); };
    auto cancel = [&result]()
    {
        result.cancelled = true;
        return result;
    };

    // Fingerprints of the pages are computed in parallel, only once per page
    QMutex mutex;
    auto ensureFingerprints = [&](const std::vector<PDFInteger>& fingerprintPages)
    {
        std::vector<PDFInteger> missing;
        std::copy_if(fingerprintPages.cbegin(), fingerprintPages.cend(), std::back_inserter(missing), [&result](PDFInteger page) { return !result.fingerprints.count(page); });

        auto compute = [&](PDFInteger page)
        {
            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                return;
            }

            QByteArray fingerprint;
            try
            {
                fingerprint = PDFOCRPagePreparer::computePageFingerprint(document, page);
            }
            catch (const PDFException&)
            {
                // Page without a fingerprint never matches a project and is refused by the writer
            }

            QMutexLocker lock(&mutex);
            result.fingerprints[page] = fingerprint;
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, missing.cbegin(), missing.cend(), compute);
    };

    // Results of the project (EXPORT-04): only the pages with the same content are used,
    // the configuration of the run is given by the settings
    std::set<PDFInteger> projectPages;
    if (settings.project)
    {
        const PDFOCRProject& project = *settings.project;

        std::vector<PDFInteger> pagesOfProject;
        for (const auto& item : project.pages)
        {
            if (item.first >= 0 && item.first < pageCount)
            {
                pagesOfProject.push_back(item.first);
            }
        }
        ensureFingerprints(pagesOfProject);
        if (isCancelled())
        {
            return cancel();
        }

        const PDFOCRProjectSerializer::MatchResult match = PDFOCRProjectSerializer::match(project, identity, [&result](PDFInteger page)
        {
            auto it = result.fingerprints.find(page);
            return it != result.fingerprints.end() ? it->second : QByteArray();
        });

        session->loadProject(project, match.matchingPages);
        session->setConfiguration(configuration);

        std::vector<PDFInteger> changedPages;
        std::copy_if(match.changedPages.cbegin(), match.changedPages.cend(), std::back_inserter(changedPages), [&pages](PDFInteger page) { return std::binary_search(pages.cbegin(), pages.cend(), page); });
        if (!changedPages.empty())
        {
            result.warnings << PDFTranslationContext::tr("Pages %1 of the project have a different content in the document; their results are not used and the pages are recognized again.").arg(PDFOCRPageSelection::describe(changedPages));
        }
        if (!match.missingPages.empty())
        {
            result.warnings << PDFTranslationContext::tr("The project contains %n page(s), which are not in the document.", nullptr, int(match.missingPages.size()));
        }

        for (PDFInteger page : pages)
        {
            const PDFOCRPageResult* pageResult = session->getPage(page);
            if (pageResult && pageResult->hasResult())
            {
                projectPages.insert(page);
            }
        }
    }

    // Pages, which are recognized now
    std::vector<PDFInteger> candidates;
    std::vector<std::pair<PDFInteger, QString>> pagesToSkip;
    for (PDFInteger page : pages)
    {
        if (projectPages.count(page))
        {
            continue;
        }

        // Manual corrections are never overwritten silently (EDIT-07)
        if (session->hasManualCorrections(page))
        {
            pagesToSkip.emplace_back(page, PDFTranslationContext::tr("The outdated result of the page in the project contains manual corrections. Open the project in the editor to recognize the page again."));
            continue;
        }
        candidates.push_back(page);
    }

    // Analysis of the pages (INPUT-01..03), in parallel
    {
        auto analyze = [&](PDFInteger page)
        {
            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                return;
            }

            PDFOCRPagePreparer preparer(document, &fontCache, cms, &optionalContentActivity, meshQualitySettings, settings.rendererEngine);
            PDFOCRPageAnalysis analysis = preparer.analyze(page, operationControl);

            QMutexLocker lock(&mutex);
            result.analysis[page] = std::move(analysis);
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, candidates.cbegin(), candidates.cend(), analyze);

        if (progress)
        {
            progress(0, 0, nullptr);
        }
    }

    if (isCancelled())
    {
        return cancel();
    }

    // Policy of the existing text (INPUT-04), decisions of the dialog given by the settings
    std::vector<PDFInteger> pagesToRecognize;
    std::set<PDFInteger> reviewOnlyPages;
    std::set<PDFInteger> maskedPages;
    QStringList regionConflicts;

    for (PDFInteger page : candidates)
    {
        const PDFOCRPageAnalysis& analysis = result.analysis[page];
        const PDFOCRPageResult* pageResult = session->getPage(page);
        bool hasInclusiveRegions = false;
        if (pageResult)
        {
            // Overlapping inclusive regions with a different configuration (REGION-02)
            for (size_t i = 0; i < pageResult->regions.size(); ++i)
            {
                const PDFOCRRegion& first = pageResult->regions[i];
                hasInclusiveRegions = hasInclusiveRegions || first.type == PDFOCRRegionType::Recognize;
                for (size_t j = i + 1; j < pageResult->regions.size(); ++j)
                {
                    const PDFOCRRegion& second = pageResult->regions[j];
                    if (first.type == PDFOCRRegionType::Recognize && second.type == PDFOCRRegionType::Recognize &&
                        first.rect.intersects(second.rect) && !(first.configuration == second.configuration))
                    {
                        regionConflicts << PDFTranslationContext::tr("Page %1: overlapping regions with different settings.").arg(page + 1);
                    }
                }
            }
        }

        QString reason;
        switch (PDFOCRPagePreparer::evaluateExistingTextPolicy(analysis, configuration.existingTextPolicy, hasInclusiveRegions, &reason, pageResult ? &pageResult->regions : nullptr))
        {
            case PDFOCRPagePreparer::PolicyDecision::Recognize:
                pagesToRecognize.push_back(page);
                break;

            case PDFOCRPagePreparer::PolicyDecision::Skip:
                pagesToSkip.emplace_back(page, reason);
                break;

            case PDFOCRPagePreparer::PolicyDecision::NeedsDecision:
            {
                switch (settings.decisionPolicy)
                {
                    case DecisionPolicy::Skip:
                        pagesToSkip.emplace_back(page, PDFTranslationContext::tr("Page requires a manual decision (existing text). %1").arg(reason));
                        break;

                    case DecisionPolicy::ReviewOnly:
                        pagesToRecognize.push_back(page);
                        reviewOnlyPages.insert(page);
                        break;

                    case DecisionPolicy::MaskExistingText:
                    {
                        // Only a scan with a small invisible or foreign text can be masked (R04),
                        // a region over the existing text is never masked (R05)
                        const bool hasCollision = pageResult && !PDFOCRPagePreparer::getRegionsCollidingWithText(analysis, pageResult->regions).empty();
                        if (!hasCollision && analysis.contentClass == PDFOCRPageContentClass::Mixed && !analysis.hasVisibleText && !analysis.textRectangles.empty())
                        {
                            pagesToRecognize.push_back(page);
                            maskedPages.insert(page);
                        }
                        else
                        {
                            pagesToSkip.emplace_back(page, PDFTranslationContext::tr("Page requires a manual decision, its existing text cannot be masked. %1").arg(reason));
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

    if (!regionConflicts.isEmpty())
    {
        return fail(PDFOCRErrorCode::InvalidConfiguration, PDFTranslationContext::tr("Resolve the overlapping regions of the project first:\n%1").arg(regionConflicts.join(QChar('\n'))));
    }

    if (configuration.existingTextPolicy == PDFOCRExistingTextPolicy::ReviewOnly)
    {
        reviewOnlyPages.insert(pagesToRecognize.begin(), pagesToRecognize.end());
    }
    else
    {
        for (PDFInteger page : pagesToRecognize)
        {
            const PDFOCRPageResult* existing = session->getPage(page);
            if (!existing || !existing->reviewOnly || reviewOnlyPages.count(page) || maskedPages.count(page))
            {
                continue;
            }

            // A page recognized under a writing policy is no more review only,
            // otherwise the flag of the previous result is inherited
            if (PDFOCRPagePreparer::evaluateExistingTextPolicy(result.analysis[page], configuration.existingTextPolicy, true, nullptr) != PDFOCRPagePreparer::PolicyDecision::Recognize ||
                result.analysis[page].hasUsableVisibleText())
            {
                reviewOnlyPages.insert(page);
            }
        }
    }

    // The resolution of a page too large for the memory limit or for the image size
    // limit of the engine is reduced only with an explicit consent (IMAGE-01, ARCH-02)
    std::vector<std::pair<PDFInteger, QString>> pagesTooLarge;
    {
        const int maximumDimension = capabilities.maximumImageSize.isEmpty() ? 0 : qMin(capabilities.maximumImageSize.width(), capabilities.maximumImageSize.height());
        QStringList reducedPages;
        for (PDFInteger page : pagesToRecognize)
        {
            const double requestedDpi = session->getEffectiveConfiguration(page).dpi;
            const double limitedDpi = PDFOCRPagePreparer::getLimitedDpi(document->getCatalog()->getPage(size_t(page)), requestedDpi, PDFOCRPagePreparer::DefaultMaximumPixels, maximumDimension);
            if (limitedDpi + 0.5 < requestedDpi)
            {
                const QString text = PDFTranslationContext::tr("Page %1: %2 DPI instead of %3 DPI").arg(page + 1).arg(qRound(limitedDpi)).arg(qRound(requestedDpi));
                if (settings.allowReducedResolution)
                {
                    reducedPages << text;
                }
                else
                {
                    pagesTooLarge.emplace_back(page, PDFTranslationContext::tr("The page is too large for the requested resolution (%1). Select a lower resolution, or allow the reduced resolution.").arg(text));
                }
            }
        }

        if (!reducedPages.isEmpty())
        {
            result.warnings << PDFTranslationContext::tr("The resolution of %n page(s) was reduced: %1", nullptr, int(reducedPages.size())).arg(reducedPages.join(QStringLiteral(", ")));
        }

        for (const auto& item : pagesTooLarge)
        {
            std::erase(pagesToRecognize, item.first);
        }
    }

    // Skipped pages and the pages, which are too large, are marked in the session;
    // existing results are not destroyed by the skipping
    auto markPage = [&](PDFInteger page, PDFOCRPageState state, const QString& reason)
    {
        const PDFOCRPageResult* existing = session->getPage(page);
        if (existing && existing->hasResult())
        {
            return;
        }

        PDFOCRPageResult pageResult;
        pageResult.pageIndex = page;
        pageResult.pageLabel = PDFOCRPagePreparer::getPageLabel(document, page);
        pageResult.state = state;
        if (existing)
        {
            pageResult.regions = existing->regions;
        }
        if (state == PDFOCRPageState::Skipped)
        {
            pageResult.skipReason = reason;
            pageResult.error = PDFOCRError::create(PDFOCRErrorCode::None, reason, PDFTranslationContext::tr("Existing text policy"));
        }
        else
        {
            pageResult.error = PDFOCRError::create(PDFOCRErrorCode::ImageTooLarge, reason);
        }
        auto analysisIt = result.analysis.find(page);
        if (analysisIt != result.analysis.end())
        {
            pageResult.analysis = analysisIt->second;
        }
        session->setPageResult(std::move(pageResult));
    };

    for (const auto& item : pagesToSkip)
    {
        markPage(item.first, PDFOCRPageState::Skipped, item.second);
    }
    for (const auto& item : pagesTooLarge)
    {
        markPage(item.first, PDFOCRPageState::Error, item.second);
    }

    if (!pagesToRecognize.empty())
    {
        // All languages of the run (configuration, page exceptions and regions) are resolved into a single model set
        QStringList languages = configuration.languages;
        auto addLanguages = [&languages](const QStringList& addedLanguages)
        {
            for (const QString& language : addedLanguages)
            {
                if (!languages.contains(language))
                {
                    languages << language;
                }
            }
        };
        for (PDFInteger page : pagesToRecognize)
        {
            addLanguages(session->getEffectiveConfiguration(page).languages);
            if (const PDFOCRPageResult* pageResult = session->getPage(page))
            {
                for (const PDFOCRRegion& region : pageResult->regions)
                {
                    addLanguages(region.configuration.languages);
                }
            }
        }

        PDFOCRResolvedModelSet models;
        if (factory->usesManagedModels())
        {
            // The models are never downloaded here, a missing model is an error
            PDFOCRError modelError;
            models = settings.modelManager->resolveModelSet(configuration.engineId, languages, configuration.profile, &modelError);
            if (modelError)
            {
                return fail(modelError.code, modelError.message);
            }
        }
        else
        {
            models.dataPath = QStringLiteral("-");
            models.languages = languages;
            models.profile = configuration.profile;
        }

        ensureFingerprints(pagesToRecognize);
        if (isCancelled())
        {
            return cancel();
        }

        // Job description, the same as in the dialog
        PDFOCRJobDescription description;
        description.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        description.documentFingerprint = identity.fingerprint;
        description.configuration = configuration;
        description.models = models;

        for (PDFInteger page : pagesToRecognize)
        {
            PDFOCRPageTask task;
            task.pageIndex = page;
            task.configuration = session->getEffectiveConfiguration(page);
            task.pageLabel = PDFOCRPagePreparer::getPageLabel(document, page);
            task.pageFingerprint = result.fingerprints[page];
            task.analysis = result.analysis[page];
            task.maskedRectangles = task.analysis.annotationRectangles;
            task.maskedRectangles.insert(task.maskedRectangles.end(), task.analysis.redactionRectangles.begin(), task.analysis.redactionRectangles.end());

            if (maskedPages.count(page))
            {
                // The existing text is not recognized again (R04)
                task.maskedRectangles.insert(task.maskedRectangles.end(), task.analysis.textRectangles.begin(), task.analysis.textRectangles.end());
            }

            if (const PDFOCRPageResult* pageResult = session->getPage(page))
            {
                task.regions = pageResult->regions;
                task.generation = pageResult->generation + 1;
                task.skipBlankDetection = pageResult->blankDetectionOverridden;
            }
            else
            {
                task.generation = 1;
            }

            description.pages.push_back(std::move(task));
        }

        // The controller delivers the results by queued signals into this thread
        PDFOCRJobController controller(nullptr);
        controller.setEnvironment(document, &fontCache, cms, &optionalContentActivity, &meshQualitySettings, settings.rendererEngine);

        QEventLoop eventLoop;
        QObject receiver;
        int generation = 0;
        int finishedPages = 0;
        bool jobFinished = false;
        const int totalPages = int(description.pages.size());

        QObject::connect(&controller, &PDFOCRJobController::pageFinished, &receiver, [&](int pageGeneration, PDFOCRPageResult pageResult)
        {
            if (pageGeneration != generation)
            {
                return;
            }

            const PDFInteger page = pageResult.pageIndex;

            // Permission to write the result is a serialized property of the result (INPUT-04, R03)
            pageResult.reviewOnly = reviewOnlyPages.count(page) > 0;
            session->setPageResult(std::move(pageResult));
            ++finishedPages;

            if (progress)
            {
                progress(finishedPages, totalPages, session->getPage(page));
            }
        }, Qt::QueuedConnection);

        QObject::connect(&controller, &PDFOCRJobController::jobFinished, &receiver, [&](int jobGeneration, PDFOCRJobSummary summary)
        {
            if (jobGeneration != generation)
            {
                return;
            }

            result.summary = std::move(summary);
            jobFinished = true;
            eventLoop.quit();
        }, Qt::QueuedConnection);

        // The cancellation of the caller stops the job cooperatively (JOB-05)
        QTimer cancellationTimer;
        QObject::connect(&cancellationTimer, &QTimer::timeout, &receiver, [&]()
        {
            if (PDFOperationControl::isOperationCancelled(operationControl) && controller.isRunning() && !controller.isStopping())
            {
                controller.stop();
            }
        });
        cancellationTimer.start(50);

        if (!controller.start(std::move(description), &generation))
        {
            return fail(PDFOCRErrorCode::InitializationFailed, PDFTranslationContext::tr("Recognition cannot be started."));
        }

        if (!jobFinished)
        {
            eventLoop.exec();
        }
        cancellationTimer.stop();
        controller.waitForFinished();

        if (result.summary.criticalError)
        {
            result.errorCode = result.summary.criticalError.code;
            result.errorMessage = result.summary.criticalError.step.isEmpty() ? result.summary.criticalError.message
                                                                               : PDFTranslationContext::tr("Recognition failed in step '%1': %2").arg(result.summary.criticalError.step, result.summary.criticalError.message);
        }
        result.cancelled = result.summary.cancelled || isCancelled();
    }

    // Records of the selected pages
    const PDFOCRReviewCriteria criteria = session->getReviewCriteria();
    for (PDFInteger page : pages)
    {
        PageRecord record;
        record.pageIndex = page;
        record.masked = maskedPages.count(page) > 0;

        const PDFOCRPageResult* pageResult = session->getPage(page);
        if (pageResult)
        {
            record.pageLabel = pageResult->pageLabel;
            record.state = pageResult->state;
            record.reviewOnly = pageResult->reviewOnly;
            record.elapsedMilliseconds = pageResult->elapsedMilliseconds;
            record.statistics = PDFOCRConfidenceStatistics::compute(*pageResult, criteria);

            if (pageResult->state == PDFOCRPageState::Skipped)
            {
                record.message = pageResult->skipReason;
            }
            else if (pageResult->state == PDFOCRPageState::NoText)
            {
                record.message = pageResult->skipReason;
            }
            else if (pageResult->error)
            {
                record.message = pageResult->error.message;
            }
        }
        else
        {
            record.pageLabel = PDFOCRPagePreparer::getPageLabel(document, page);
        }

        if (projectPages.count(page))
        {
            record.source = PageSource::Project;
        }
        else if (std::find(pagesToRecognize.cbegin(), pagesToRecognize.cend(), page) != pagesToRecognize.cend())
        {
            record.source = PageSource::Recognized;
        }
        else
        {
            record.source = PageSource::Skipped;
        }

        result.records.push_back(std::move(record));
    }

    return result;
}

PDFOCRApplyProcessor::Result PDFOCRDocumentRunner::writeCopy(const PDFDocument* document,
                                                             const Result& result,
                                                             const WriteOptions& options,
                                                             PDFOCRApplyProcessor::Plan* plan,
                                                             const PDFOperationControl* operationControl)
{
    PDFOCRApplyProcessor::Result applyResult;

    auto fail = [&applyResult](const QString& message)
    {
        applyResult.errorMessage = message;
        return applyResult;
    };

    if (!document || !result.session)
    {
        return fail(PDFTranslationContext::tr("No document to write."));
    }

    const PDFOCRApplyProcessor::Context context = PDFOCRApplyProcessor::createContext(document, options.sourceFileName);

    // Permissions of the document and the certification (PDF-12), conformance declaration (PDF-15)
    const QString permissionError = PDFOCRApplyProcessor::checkPermissions(context, PDFOCRApplyProcessor::OutputMode::CreateCopy);
    if (!permissionError.isEmpty())
    {
        return fail(permissionError);
    }

    const QStringList compressionErrors = options.compression.validate();
    if (!compressionErrors.isEmpty())
    {
        return fail(compressionErrors.join(QChar('\n')));
    }

    PDFOCRApplyProcessor::Request request;
    request.outputMode = PDFOCRApplyProcessor::OutputMode::CreateCopy;
    request.reviewCriteria = result.session->getReviewCriteria();
    request.writerOptions = options.writerOptions;
    request.compression = options.compression;
    request.memoryBudget = options.memoryBudget;

    for (PDFInteger page : result.pages)
    {
        const PDFOCRPageResult* pageResult = result.session->getPage(page);
        if (!pageResult || !pageResult->hasResult())
        {
            continue;
        }

        request.results.push_back(*pageResult);

        auto analysisIt = result.analysis.find(page);
        if (analysisIt != result.analysis.end())
        {
            request.analysis[page] = analysisIt->second;
        }

        auto fingerprintIt = result.fingerprints.find(page);
        if (fingerprintIt != result.fingerprints.end() && !fingerprintIt->second.isEmpty())
        {
            request.fingerprints[page] = fingerprintIt->second;
        }
    }

    PDFOCRApplyProcessor::Plan localPlan = PDFOCRApplyProcessor::createPlan(context, request);
    if (plan)
    {
        *plan = localPlan;
    }

    if (!localPlan.hasRequests())
    {
        QString message = PDFTranslationContext::tr("There is no result, which can be written into the PDF.");
        if (!localPlan.excluded.isEmpty())
        {
            message += QChar('\n') + localPlan.excluded.join(QChar('\n'));
        }
        return fail(message);
    }

    const QString copyError = PDFOCRApplyProcessor::setCopyFileName(localPlan, context, options.outputFileName);
    if (!copyError.isEmpty())
    {
        return fail(copyError);
    }

    if (plan)
    {
        *plan = localPlan;
    }

    return PDFOCRApplyProcessor::execute(context, localPlan, operationControl);
}

PDFOCRDocumentRunner::FileResult PDFOCRDocumentRunner::processFile(const FileTask& task,
                                                                  const Settings& settings,
                                                                  const ProgressCallback& progress,
                                                                  const PDFOperationControl* operationControl)
{
    FileResult fileResult;
    QElapsedTimer timer;
    timer.start();

    auto finish = [&](FileResult::Status status, FileResult::Stage stage, const QString& message)
    {
        fileResult.status = status;
        fileResult.stage = stage;
        fileResult.message = message;
        fileResult.elapsedMilliseconds = timer.elapsed();

        // The session must not refer to the local document, which is destroyed now
        if (PDFOCRSession* session = fileResult.result.session.get())
        {
            session->setDocument(nullptr, session->getDocumentIdentity());
        }
        return std::move(fileResult);
    };

    // Document (a password is tried only once)
    bool isFirstPasswordAttempt = true;
    auto passwordCallback = [&task, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt && !task.password.isEmpty();
        isFirstPasswordAttempt = false;
        return task.password;
    };
    PDFDocumentReader reader(nullptr, passwordCallback, true, false);
    const PDFDocument document = reader.readFromFile(task.inputFile);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        const QString reason = reader.getReadingResult() == PDFDocumentReader::Result::Cancelled ? PDFTranslationContext::tr("The document is protected by a password.") : reader.getErrorMessage();
        return finish(FileResult::Status::Failed, FileResult::Stage::Reading, PDFTranslationContext::tr("The document '%1' cannot be read. %2").arg(QDir::toNativeSeparators(task.inputFile), reason));
    }
    fileResult.pageCount = int(document.getCatalog()->getPageCount());

    // The permissions are checked before the recognition (PDF-12, PDF-15)
    const bool writesDocument = !task.outputFile.isEmpty();
    if (writesDocument)
    {
        const PDFOCRApplyProcessor::Context context = PDFOCRApplyProcessor::createContext(&document, task.inputFile);
        const QString permissionError = PDFOCRApplyProcessor::checkPermissions(context, PDFOCRApplyProcessor::OutputMode::CreateCopy);
        if (!permissionError.isEmpty())
        {
            return finish(FileResult::Status::Skipped, FileResult::Stage::Permissions, permissionError);
        }
    }

    // Project
    Settings fileSettings = settings;
    if (!task.projectFile.isEmpty())
    {
        PDFOCRProject project;
        QString errorMessage;
        if (!PDFOCRProjectSerializer::load(task.projectFile, project, &errorMessage))
        {
            return finish(FileResult::Status::Failed, FileResult::Stage::Project, PDFTranslationContext::tr("The project cannot be read. %1").arg(errorMessage));
        }
        fileSettings.project = std::move(project);
    }

    // Configuration and pages
    if (task.configure)
    {
        const QString errorMessage = task.configure(fileSettings.project ? &*fileSettings.project : nullptr, &fileSettings.configuration);
        if (!errorMessage.isEmpty())
        {
            return finish(FileResult::Status::Failed, FileResult::Stage::Configuration, errorMessage);
        }
    }

    if (fileSettings.configuration.compression.isEnabled() && !writesDocument)
    {
        return finish(FileResult::Status::Failed, FileResult::Stage::Configuration, PDFTranslationContext::tr("The images are compressed only when the document is written."));
    }

    fileSettings.pages.clear();
    if (task.selectPages)
    {
        const QString errorMessage = task.selectPages(PDFInteger(fileResult.pageCount), &fileSettings.pages);
        if (!errorMessage.isEmpty())
        {
            return finish(FileResult::Status::Failed, FileResult::Stage::Configuration, errorMessage);
        }
    }
    else
    {
        fileSettings.pages.resize(size_t(fileResult.pageCount));
        std::iota(fileSettings.pages.begin(), fileSettings.pages.end(), PDFInteger(0));
    }

    if (fileSettings.pages.empty())
    {
        return finish(FileResult::Status::Skipped, FileResult::Stage::NothingToWrite, PDFTranslationContext::tr("The document has no page to recognize."));
    }

    fileSettings.fileName = task.inputFile;

    // Recognition
    fileResult.result = run(&document, fileSettings, progress, operationControl);
    Result& result = fileResult.result;
    fileResult.warnings = result.warnings;

    if (!result.errorMessage.isEmpty())
    {
        return finish(FileResult::Status::Failed, FileResult::Stage::Recognition, result.errorMessage);
    }
    if (result.cancelled)
    {
        return finish(FileResult::Status::Failed, FileResult::Stage::Recognition, PDFTranslationContext::tr("The recognition was cancelled, nothing was written."));
    }

    // A failed page means an incomplete result, which is written only on request
    const int failedPages = result.getFailedPageCount();
    if (failedPages > 0 && !task.allowPageErrors)
    {
        return finish(FileResult::Status::Failed, FileResult::Stage::PageErrors, PDFTranslationContext::tr("Recognition of %n page(s) failed, nothing was written.", nullptr, failedPages));
    }

    // Nothing was recognized (the pages have a text, or they were skipped): no output
    // is written, not even an empty export
    if (result.getResultPageCount() == 0)
    {
        QStringList reasons;
        for (const PageRecord& record : result.records)
        {
            if (!record.message.isEmpty() && !reasons.contains(record.message))
            {
                reasons << record.message;
            }
        }
        QString message = PDFTranslationContext::tr("No page has a recognized text, nothing was written.");
        if (!reasons.isEmpty())
        {
            message += QChar(' ') + reasons.join(QChar(' '));
        }
        return finish(FileResult::Status::Skipped, FileResult::Stage::NothingToWrite, message);
    }

    FileResult::Status status = FileResult::Status::Success;
    FileResult::Stage stage = FileResult::Stage::None;
    QString statusMessage;

    // Document with the text layer
    if (writesDocument)
    {
        WriteOptions writeOptions;
        writeOptions.sourceFileName = task.inputFile;
        writeOptions.outputFileName = task.outputFile;
        writeOptions.writerOptions.keepReviewData = task.keepReviewData || fileSettings.configuration.keepReviewDataInDocument;
        writeOptions.writerOptions.onlyReviewed = task.onlyReviewed;
        writeOptions.compression = fileSettings.configuration.compression;
        writeOptions.memoryBudget = fileSettings.configuration.memoryBudget;

        fileResult.writeResult = writeCopy(&document, result, writeOptions, &fileResult.plan, operationControl);
        const PDFOCRApplyProcessor::Result& writeResult = fileResult.writeResult;
        if (!writeResult.isSuccess())
        {
            if (!fileResult.plan.hasRequests())
            {
                // Nothing to write: the results are for the review only, or without text;
                // the exports are still written
                status = FileResult::Status::Skipped;
                stage = FileResult::Stage::NothingToWrite;
                statusMessage = writeResult.errorMessage;
            }
            else
            {
                return finish(FileResult::Status::Failed, FileResult::Stage::Writing, PDFTranslationContext::tr("The document was not written. %1").arg(writeResult.errorMessage));
            }
        }
        else
        {
            fileResult.messages << PDFTranslationContext::tr("Text layer was written on %n page(s) into '%1'.", nullptr, int(writeResult.report.writtenPages.size() + writeResult.report.unchangedPages.size()))
                                   .arg(QDir::toNativeSeparators(task.outputFile));
            if (writeOptions.compression.isEnabled())
            {
                fileResult.messages << writeResult.compressionReport.getSummary();
            }
            if (writeResult.conformanceRemoved)
            {
                fileResult.messages << PDFTranslationContext::tr("The PDF/A or PDF/UA declaration was removed from the copy, its conformance was not verified.");
            }
            if (!fileResult.plan.excluded.isEmpty())
            {
                fileResult.messages << PDFTranslationContext::tr("Pages, which were not written:") << fileResult.plan.excluded;
            }
        }
    }

    // Exports
    const std::vector<const PDFOCRPageResult*> exportResults = getExportResults(result);
    if (!task.exportText.isEmpty())
    {
        PDFOCRTextExporter::Options exportOptions;
        exportOptions.onlyReviewed = task.onlyReviewed;
        PDFOCRTextExporter::Report exportReport;
        const QString text = PDFOCRTextExporter::exportText(exportResults, exportOptions, &exportReport);
        QString errorMessage;
        if (!PDFOCRTextExporter::writeTextFile(task.exportText, text, &errorMessage))
        {
            return finish(FileResult::Status::Failed, FileResult::Stage::Writing, errorMessage);
        }
        fileResult.messages << PDFTranslationContext::tr("Text of %n page(s) was exported into '%1'.", nullptr, int(exportReport.exportedPages.size())).arg(QDir::toNativeSeparators(task.exportText));
    }

    const std::pair<PDFOCRStructuredExporter::Format, QString> structuredExports[] =
    {
        { PDFOCRStructuredExporter::Format::Hocr, task.exportHocr },
        { PDFOCRStructuredExporter::Format::Alto, task.exportAlto },
        { PDFOCRStructuredExporter::Format::Tsv, task.exportTsv }
    };
    for (const auto& item : structuredExports)
    {
        if (item.second.isEmpty())
        {
            continue;
        }

        PDFOCRStructuredExporter::Options exportOptions;
        exportOptions.format = item.first;
        exportOptions.dpi = task.exportDpi;
        exportOptions.onlyReviewed = task.onlyReviewed;
        exportOptions.title = QFileInfo(task.inputFile).completeBaseName();
        PDFOCRTextExporter::Report exportReport;
        const QByteArray data = PDFOCRStructuredExporter::exportPages(&document, exportResults, exportOptions, &exportReport);
        QString errorMessage;
        if (!PDFOCRStructuredExporter::writeFile(item.second, data, &errorMessage))
        {
            return finish(FileResult::Status::Failed, FileResult::Stage::Writing, errorMessage);
        }
        fileResult.messages << PDFTranslationContext::tr("%1 of %n page(s) was exported into '%2'.", nullptr, int(exportReport.exportedPages.size()))
                               .arg(PDFOCRStructuredExporter::getFormatName(item.first), QDir::toNativeSeparators(item.second));
    }

    // Project of the editor, it describes the source document
    if (!task.saveProjectFile.isEmpty())
    {
        PDFOCRSession* session = result.session.get();
        PDFOCRDocumentIdentity identity = session->getDocumentIdentity();
        identity.fingerprint = PDFOCRPagePreparer::computeDocumentFingerprint(&document);
        session->setDocument(&document, identity);

        QString errorMessage;
        if (!PDFOCRProjectSerializer::save(session->createProject(result.pages), task.saveProjectFile, &errorMessage))
        {
            return finish(FileResult::Status::Failed, FileResult::Stage::Writing, PDFTranslationContext::tr("The project was not saved. %1").arg(errorMessage));
        }
        fileResult.messages << PDFTranslationContext::tr("The project was saved into '%1'.").arg(QDir::toNativeSeparators(task.saveProjectFile));
    }

    if (status == FileResult::Status::Success && failedPages > 0)
    {
        statusMessage = PDFTranslationContext::tr("Recognition of %n page(s) failed.", nullptr, failedPages);
    }

    return finish(status, stage, statusMessage);
}

std::vector<const PDFOCRPageResult*> PDFOCRDocumentRunner::getExportResults(const Result& result)
{
    std::vector<const PDFOCRPageResult*> results;
    if (result.session)
    {
        results = result.session->getResults(result.pages);
    }
    return results;
}

QString PDFOCRDocumentRunner::getPageStateName(PDFOCRPageState state)
{
    switch (state)
    {
        case PDFOCRPageState::Pending:
            return PDFTranslationContext::tr("Not recognized");
        case PDFOCRPageState::Preparing:
            return PDFTranslationContext::tr("Preparing");
        case PDFOCRPageState::Recognizing:
            return PDFTranslationContext::tr("Recognizing");
        case PDFOCRPageState::Done:
            return PDFTranslationContext::tr("Recognized");
        case PDFOCRPageState::NoText:
            return PDFTranslationContext::tr("No text");
        case PDFOCRPageState::Skipped:
            return PDFTranslationContext::tr("Skipped");
        case PDFOCRPageState::Error:
            return PDFTranslationContext::tr("Error");
        case PDFOCRPageState::Cancelled:
            return PDFTranslationContext::tr("Cancelled");
        case PDFOCRPageState::Stale:
            return PDFTranslationContext::tr("Outdated");
    }

    return QString();
}

}   // namespace pdf
