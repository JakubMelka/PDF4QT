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

#include "pdfdocumenttoolrunner.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfexecutionpolicy.h"
#include "pdfoperationcontrol.h"
#include "pdfthreadannotations.h"

#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>

#include <atomic>
#include <iterator>

namespace pdf
{

/// Way from a worker back to the runner: nobody waits for the worker, so the runner may
/// be destroyed at any moment. The worker posts its queued calls under the mutex and
/// the destructor of the runner forgets it under the same mutex, while the object is
/// still whole (the pending calls of a destroyed object are removed by Qt).
class PDFDocumentToolRunnerLink
{
public:
    explicit PDFDocumentToolRunnerLink(PDFDocumentToolRunner* runner) :
        m_runner(runner)
    {

    }

    void forget()
    {
        QMutexLocker locker(&m_mutex);
        m_runner = nullptr;
    }

    void postProgress(const std::shared_ptr<PDFDocumentToolRunnerJob>& job, int percentage, const QString& stepText)
    {
        QMutexLocker locker(&m_mutex);
        if (m_runner)
        {
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_runner, &PDFDocumentToolRunner::onJobProgress, Qt::QueuedConnection, job, percentage, stepText);
        }
    }

    void postFinished(const std::shared_ptr<PDFDocumentToolRunnerJob>& job)
    {
        QMutexLocker locker(&m_mutex);
        if (m_runner)
        {
            PDFThreadAnnotations::happensBefore(job.get());
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_runner, &PDFDocumentToolRunner::onJobFinished, Qt::QueuedConnection, job);
        }
    }

private:
    QMutex m_mutex;
    PDFDocumentToolRunner* m_runner;
};

/// One computation in the pool. All members except of the cancellation and of the
/// progress are written before the start or read after the end (the hand-offs are
/// annotated).
class PDFDocumentToolRunnerJob : public PDFOperationControl,
                                 public PDFDocumentToolProgressListener,
                                 public std::enable_shared_from_this<PDFDocumentToolRunnerJob>
{
public:
    PDFDocumentToolRunnerJob(std::shared_ptr<PDFDocumentToolRunnerLink> link,
                             std::shared_ptr<const PDFDocumentTool> tool,
                             PDFDocumentToolContext context,
                             PDFAppDocumentSnapshot snapshot,
                             QVariantMap parameters,
                             bool isPreview) :
        m_link(std::move(link)),
        m_tool(std::move(tool)),
        m_context(std::move(context)),
        m_snapshot(std::move(snapshot)),
        m_parameters(std::move(parameters)),
        m_isPreview(isPreview)
    {

    }

    virtual bool isOperationCancelled() const override
    {
        return m_isCanceled.load(std::memory_order_relaxed);
    }

    virtual void onToolProgress(int percentage, const QString& stepText) override
    {
        // At most one queued call per a change: a tool may report thousands of steps
        if (m_lastPercentage != percentage || m_lastStepText != stepText)
        {
            m_lastPercentage = percentage;
            m_lastStepText = stepText;
            m_link->postProgress(shared_from_this(), percentage, stepText);
        }
    }

    void cancel() { m_isCanceled.store(true, std::memory_order_relaxed); }

    /// The computation (a thread of the pool)
    void execute()
    {
        // The tool converts its exceptions to a failed result; the adapter lives
        // on the stack of the worker
        PDFDocumentToolProgressAdapter progress(this);
        m_result = m_isPreview ? m_tool->runPreview(m_context, m_parameters, this, &progress)
                               : m_tool->run(m_context, m_parameters, this, &progress);
        m_link->postFinished(shared_from_this());
    }

    const PDFAppDocumentSnapshot& getSnapshot() const { return m_snapshot; }
    const QVariantMap& getParameters() const { return m_parameters; }
    const PDFDocumentToolResult& getResult() const { return m_result; }

private:
    std::shared_ptr<PDFDocumentToolRunnerLink> m_link;
    std::shared_ptr<const PDFDocumentTool> m_tool;
    PDFDocumentToolContext m_context;
    PDFAppDocumentSnapshot m_snapshot;
    QVariantMap m_parameters;
    bool m_isPreview;
    std::atomic<bool> m_isCanceled = false;
    int m_lastPercentage = -1;
    QString m_lastStepText;
    PDFDocumentToolResult m_result;
};

/// The function of a thread of the pool
class PDFDocumentToolRunnerTask
{
public:
    explicit PDFDocumentToolRunnerTask(std::shared_ptr<PDFDocumentToolRunnerJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()() const
    {
        m_job->execute();
    }

private:
    std::shared_ptr<PDFDocumentToolRunnerJob> m_job;
};

/// Helpers of the runner
class PDFDocumentToolRunnerHelper
{
public:
    PDFDocumentToolRunnerHelper() = delete;

    /// The result holds a document or a file (a large part of the memory)
    static bool holdsData(const PDFDocumentToolResult& result)
    {
        return !result.documents.empty() || !result.outputs.empty();
    }

    /// Refused commit with its reason
    static PDFDocumentToolRunner::CommitStatus refuse(QString* errorMessage, const QString& text)
    {
        if (errorMessage)
        {
            *errorMessage = text;
        }
        return PDFDocumentToolRunner::CommitStatus::Refused;
    }
};

const PDFDocumentToolOutputDocument* PDFDocumentToolRunner::findChangedDocument(const PDFDocumentToolResult& result)
{
    for (const PDFDocumentToolOutputDocument& output : result.documents)
    {
        if (output.kind == PDFDocumentToolOutputDocument::Kind::Changed && output.inputIndex == 0 && !output.document.isNull())
        {
            return &output;
        }
    }
    return nullptr;
}

PDFDocumentToolRunner::PDFDocumentToolRunner(PDFAppDocument* document, std::shared_ptr<const PDFDocumentTool> tool, QObject* parent) :
    BaseClass(parent),
    m_document(document),
    m_hasDocument(document != nullptr),
    m_tool(std::move(tool)),
    m_link(std::make_shared<PDFDocumentToolRunnerLink>(this))
{
    Q_ASSERT(m_tool);

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(PREVIEW_DELAY);
    connect(&m_previewTimer, &QTimer::timeout, this, &PDFDocumentToolRunner::onPreviewTimeout);

    if (m_document)
    {
        connect(m_document, &PDFAppDocument::documentChanged, this, &PDFDocumentToolRunner::onDocumentChanged);
        connect(m_document, &PDFAppDocument::stateChanged, this, &PDFDocumentToolRunner::onDocumentStateChanged);
        connect(m_document, &PDFAppDocument::aboutToBeDestroyed, this, &PDFDocumentToolRunner::onDocumentAboutToBeDestroyed);
    }
}

PDFDocumentToolRunner::~PDFDocumentToolRunner()
{
    // Nobody waits: the workers see the cancellation, their results are dropped
    m_link->forget();
    if (m_runJob)
    {
        m_runJob->cancel();
    }
    if (m_previewJob)
    {
        m_previewJob->cancel();
    }

    if (PDFAppDocumentOperation* operation = m_operation.data())
    {
        m_operation = nullptr;
        disconnect(operation, nullptr, this, nullptr);
        operation->finish();
    }
}

PDFDocumentToolContext PDFDocumentToolRunner::createContext(const PDFAppDocumentSnapshot& snapshot, const PDFAppToolRunEnvironment& environment)
{
    PDFDocumentToolContext context;
    if (snapshot.isValid())
    {
        context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(snapshot.document, snapshot.originalData, snapshot.filePath, snapshot.isModified));
    }

    context.documents.insert(context.documents.end(), environment.additionalDocuments.cbegin(), environment.additionalDocuments.cend());
    context.selectedPages = environment.selectedPages;
    context.currentPage = environment.currentPage;
    context.cms = environment.cms;
    context.authorName = environment.authorName;
    context.certificateStore = environment.certificateStore;
    context.temporaryFolder = environment.temporaryFolder;
    return context;
}

void PDFDocumentToolRunner::setEnvironment(const PDFAppToolRunEnvironment& environment)
{
    m_environment = environment;
}

PDFAppDocumentSnapshot PDFDocumentToolRunner::createSnapshot() const
{
    return m_document ? m_document->getSnapshot() : PDFAppDocumentSnapshot();
}

std::shared_ptr<PDFDocumentToolRunnerJob> PDFDocumentToolRunner::startJob(const PDFAppDocumentSnapshot& snapshot, const QVariantMap& parameters, bool isPreview)
{
    std::shared_ptr<PDFDocumentToolRunnerJob> job = std::make_shared<PDFDocumentToolRunnerJob>(m_link, m_tool, createContext(snapshot, m_environment), snapshot, parameters, isPreview);
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFDocumentToolRunnerTask(job));
    return job;
}

bool PDFDocumentToolRunner::beginOperation()
{
    if (!m_document)
    {
        return true;
    }

    const PDFDocumentToolCatalogEntry* entry = m_tool->getInfo();
    QPointer<PDFDocumentToolRunner> guard(this);
    QPointer<PDFAppDocumentOperation> operation = m_document->beginOperation(PDFAppDocumentOperationKind::Tool, entry ? entry->getTitle() : m_tool->getId(), true);
    if (!guard)
    {
        // A slot of the announcement has deleted the runner: nobody else ends the operation
        if (operation)
        {
            operation->finish();
        }
        return false;
    }

    m_operation = operation;
    if (operation)
    {
        connect(operation.data(), &PDFAppDocumentOperation::cancelRequested, this, &PDFDocumentToolRunner::onOperationCancelRequested);
    }
    return true;
}

bool PDFDocumentToolRunner::endOperation()
{
    PDFAppDocumentOperation* operation = m_operation.data();
    if (!operation)
    {
        return true;
    }

    m_operation = nullptr;
    disconnect(operation, nullptr, this, nullptr);

    QPointer<PDFDocumentToolRunner> guard(this);
    operation->finish();
    return !guard.isNull();
}

bool PDFDocumentToolRunner::run(const QVariantMap& parameters)
{
    if (m_state != State::Idle)
    {
        return false;
    }

    if (m_hasDocument && (!m_document || !m_document->isReady() || m_document->isBlockedByOperation()))
    {
        return false;
    }

    m_runParameters = parameters;
    m_attempt = 0;
    startRun();
    return true;
}

bool PDFDocumentToolRunner::startRun()
{
    m_previewTimer.stop();
    ++m_attempt;

    PDFAppDocumentSnapshot snapshot = createSnapshot();
    const quint64 version = snapshot.version;
    const bool isPreviewFinal = m_tool->isPreviewFinal();

    // A result of a preview, which is the result itself: nothing is computed again
    const CachedPreview* cached = isPreviewFinal ? findCached(version, m_runParameters) : nullptr;
    std::shared_ptr<PDFDocumentToolRunnerJob> joinedJob;
    if (!cached && isPreviewFinal && m_previewJob && m_previewJob->getSnapshot().version == version && m_previewJob->getParameters() == m_runParameters)
    {
        joinedJob = std::move(m_previewJob);
        m_previewJob.reset();
    }
    else
    {
        cancelPreviewJob();
    }

    m_state = State::Running;
    m_resultSnapshot = snapshot;
    if (!joinedJob)
    {
        m_progress = 0;
        m_stepText.clear();
    }

    PDFDocumentToolResult cachedResult;
    if (cached)
    {
        cachedResult = cached->result;
    }

    const bool isCached = cached != nullptr;

    QPointer<PDFDocumentToolRunner> guard(this);
    bool isAlive = beginOperation();
    if (isAlive && m_operation && m_progress > 0)
    {
        m_operation->setProgress(m_progress);
        isAlive = !guard.isNull();
    }

    // A slot of the announcement has deleted the runner, stopped the run or lost the
    // document: the joined computation goes too, no operation stays behind
    if (!isAlive || m_state != State::Running)
    {
        if (joinedJob)
        {
            joinedJob->cancel();
        }
        return isAlive && endOperation();
    }

    if (!isCached)
    {
        m_runJob = joinedJob ? std::move(joinedJob) : startJob(snapshot, m_runParameters, false);
    }

    Q_EMIT runStarted();
    if (!guard)
    {
        return false;
    }

    // A slot may have stopped the run
    if (isCached && m_state == State::Running)
    {
        finishRun(cachedResult);
        return !guard.isNull();
    }
    return true;
}

void PDFDocumentToolRunner::stop()
{
    if (m_state != State::Running)
    {
        return;
    }

    if (m_runJob)
    {
        m_runJob->cancel();
        m_runJob.reset();
    }
    finishRun(PDFDocumentToolResult::createStopped());
}

void PDFDocumentToolRunner::onOperationCancelRequested()
{
    // Stop and Close of the dialog close-running of the tab
    if (m_state == State::Running)
    {
        stop();
    }
    else if (m_state == State::Waiting)
    {
        endDecision();
    }
}

void PDFDocumentToolRunner::finishRun(const PDFDocumentToolResult& result)
{
    m_runJob.reset();

    QPointer<PDFDocumentToolRunner> guard(this);
    if (result.isSuccess())
    {
        // Nothing runs, the operation keeps the document as it is, while the user decides
        m_state = State::Waiting;
        if (m_operation)
        {
            m_operation->setWaiting();
            if (!guard)
            {
                return;
            }
        }
    }
    else
    {
        m_state = State::Idle;
        if (!endOperation())
        {
            return;
        }
    }

    Q_EMIT runFinished(result);
    if (guard && m_state == State::Idle)
    {
        resumeDeferredPreview();
    }
}

QString PDFDocumentToolRunner::getCommitRefusal() const
{
    const PDFAppDocument* document = m_document.data();
    if (m_state != State::Waiting || !document || !document->isReady())
    {
        return tr("The document is not available any more.");
    }

    if (document->isPagedXFA())
    {
        return PDFAppDocument::getPagedXFARefusalText();
    }

    if (document->isLocked())
    {
        return tr("The document is locked. Unlock it or save the result as a copy.");
    }

    for (const PDFAppDocumentOperation* operation : document->getOperations())
    {
        if (operation != m_operation.data() && operation->isBlockingChanges())
        {
            return tr("The document is busy with another operation. Try it again when it ends.");
        }
    }
    return QString();
}

PDFDocumentToolRunner::CommitStatus PDFDocumentToolRunner::commitResult(const PDFDocumentToolResult& result, const QString& description, QString* errorMessage)
{
    using Helper = PDFDocumentToolRunnerHelper;

    const QString refusal = getCommitRefusal();
    if (!refusal.isEmpty())
    {
        return Helper::refuse(errorMessage, refusal);
    }

    const PDFDocumentToolOutputDocument* output = findChangedDocument(result);
    if (!output)
    {
        return Helper::refuse(errorMessage, tr("The result has no document."));
    }

    PDFAppDocument* document = m_document.data();
    QPointer<PDFDocumentToolRunner> guard(this);
    if (document->getVersion() != m_resultSnapshot.version)
    {
        // The document has changed under the result: the tool runs again on the current
        // version and the user decides over its new facts (never silently, F2)
        if (m_attempt >= MAXIMUM_ATTEMPTS)
        {
            m_state = State::Idle;
            if (!endOperation() || !guard)
            {
                return CommitStatus::Refused;
            }
            return Helper::refuse(errorMessage, tr("The document keeps changing. Run the tool again."));
        }

        if (!endOperation())
        {
            return CommitStatus::Refused;
        }
        m_state = State::Idle;
        startRun();
        return CommitStatus::Rerun;
    }

    const PDFDocumentPointer outputDocument = output->document;
    const PDFModifiedDocument::ModificationFlags flags = output->flags;
    const quint64 version = m_resultSnapshot.version;

    // The operation ends just before the commit (F2): a slot of the announcement may
    // delete the runner, the document is checked again
    m_state = State::Idle;
    if (!endOperation())
    {
        return CommitStatus::Refused;
    }

    document = m_document.data();
    if (!document)
    {
        return Helper::refuse(errorMessage, tr("The document is not available any more."));
    }

    const PDFModifiedDocument modified(outputDocument, document->getOptionalContentActivity(), flags);
    const PDFDocumentToolCatalogEntry* entry = m_tool->getInfo();
    const bool isWholeFileRewrite = entry && entry->getSignatureImpact() == PDFDocumentToolSignatureImpact::LostCopyOnly;
    if (!document->commitToolResult(modified, description, version, PDFAppChangeSource::Tool, isWholeFileRewrite))
    {
        return guard ? Helper::refuse(errorMessage, tr("The document is busy with another operation. Run the tool again when it ends.")) : CommitStatus::Refused;
    }
    return CommitStatus::Committed;
}

void PDFDocumentToolRunner::endDecision()
{
    if (m_state != State::Waiting)
    {
        return;
    }

    m_state = State::Idle;
    if (endOperation())
    {
        resumeDeferredPreview();
    }
}

bool PDFDocumentToolRunner::resumeDeferredPreview()
{
    if (!m_isPreviewDeferred || m_state != State::Idle)
    {
        return true;
    }

    // The slot of a preview found in the cache may delete the runner
    m_isPreviewDeferred = false;
    QPointer<PDFDocumentToolRunner> guard(this);
    const QVariantMap parameters = m_previewParameters;
    requestPreview(parameters);
    return !guard.isNull();
}

void PDFDocumentToolRunner::requestPreview(const QVariantMap& parameters)
{
    if (!m_tool->hasPreview())
    {
        return;
    }

    m_isPreviewRequested = true;
    m_previewParameters = parameters;

    if (m_state != State::Idle)
    {
        // The document is held by the run: the preview is computed after it
        m_isPreviewDeferred = true;
        return;
    }

    if (m_document && !m_document->isReady())
    {
        return;
    }

    // The running computation of other parameters is of no use
    if (m_previewJob && (m_previewJob->getParameters() != parameters || m_previewJob->getSnapshot().version != createSnapshot().version))
    {
        cancelPreviewJob();
    }

    const CachedPreview* cached = findCached(createSnapshot().version, parameters);
    if (cached)
    {
        m_previewTimer.stop();
        cancelPreviewJob();
        const PDFDocumentToolResult result = cached->result;
        Q_EMIT previewFinished(result);
        return;
    }

    if (!m_previewJob)
    {
        m_previewTimer.start();
    }
}

void PDFDocumentToolRunner::cancelPreview()
{
    m_isPreviewRequested = false;
    m_isPreviewDeferred = false;
    m_previewParameters.clear();
    m_previewTimer.stop();
    cancelPreviewJob();
}

bool PDFDocumentToolRunner::isPreviewPending() const
{
    return m_previewTimer.isActive() || m_previewJob != nullptr;
}

const PDFDocumentToolResult* PDFDocumentToolRunner::getPreview() const
{
    if (!m_isPreviewRequested)
    {
        return nullptr;
    }

    const CachedPreview* cached = findCached(createSnapshot().version, m_previewParameters);
    return cached ? &cached->result : nullptr;
}

void PDFDocumentToolRunner::cancelPreviewJob()
{
    if (m_previewJob)
    {
        m_previewJob->cancel();
        m_previewJob.reset();
    }
}

void PDFDocumentToolRunner::onPreviewTimeout()
{
    if (!m_isPreviewRequested || m_state != State::Idle || (m_document && !m_document->isReady()))
    {
        return;
    }

    m_progress = 0;
    m_stepText.clear();
    m_previewJob = startJob(createSnapshot(), m_previewParameters, true);
    Q_EMIT previewStarted();
}

void PDFDocumentToolRunner::setProgress(int percentage, const QString& stepText)
{
    m_progress = percentage;
    m_stepText = stepText;
}

void PDFDocumentToolRunner::onJobProgress(const std::shared_ptr<PDFDocumentToolRunnerJob>& job, int percentage, const QString& stepText)
{
    if (job == m_runJob)
    {
        setProgress(percentage, stepText);

        QPointer<PDFDocumentToolRunner> guard(this);
        if (m_operation)
        {
            m_operation->setProgress(percentage);
        }
        if (guard)
        {
            Q_EMIT runProgress(percentage, stepText);
        }
    }
    else if (job == m_previewJob)
    {
        setProgress(percentage, stepText);
        Q_EMIT previewProgress(percentage, stepText);
    }
}

void PDFDocumentToolRunner::onJobFinished(const std::shared_ptr<PDFDocumentToolRunnerJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    if (job == m_runJob)
    {
        const PDFDocumentToolResult result = job->getResult();
        if (m_tool->isPreviewFinal() && (result.isSuccess() || result.status == PDFDocumentToolResultStatus::NothingToDo))
        {
            addToCache(job->getSnapshot().version, job->getParameters(), result);
        }
        finishRun(result);
    }
    else if (job == m_previewJob)
    {
        m_previewJob.reset();

        const PDFDocumentToolResult& result = job->getResult();
        if (result.status == PDFDocumentToolResultStatus::Stopped)
        {
            return;
        }

        if (result.isSuccess() || result.status == PDFDocumentToolResultStatus::NothingToDo)
        {
            addToCache(job->getSnapshot().version, job->getParameters(), result);
        }
        Q_EMIT previewFinished(result);
    }
}

void PDFDocumentToolRunner::addToCache(quint64 version, const QVariantMap& parameters, const PDFDocumentToolResult& result)
{
    // At most one result holds data (the memory, plan chapter 3)
    if (PDFDocumentToolRunnerHelper::holdsData(result))
    {
        for (auto it = m_previewCache.begin(); it != m_previewCache.end();)
        {
            it = PDFDocumentToolRunnerHelper::holdsData(it->result) ? m_previewCache.erase(it) : std::next(it);
        }
    }

    if (m_previewCache.size() >= size_t(MAXIMUM_CACHED_PREVIEWS))
    {
        m_previewCache.erase(m_previewCache.begin());
    }

    CachedPreview item;
    item.version = version;
    item.parameters = parameters;
    item.result = result;
    m_previewCache.push_back(std::move(item));
}

const PDFDocumentToolRunner::CachedPreview* PDFDocumentToolRunner::findCached(quint64 version, const QVariantMap& parameters) const
{
    for (const CachedPreview& item : m_previewCache)
    {
        if (item.version == version && item.parameters == parameters)
        {
            return &item;
        }
    }
    return nullptr;
}

void PDFDocumentToolRunner::onDocumentChanged()
{
    // A new version: nothing of the previews is valid (F2 of the plan)
    const bool hadPreview = !m_previewCache.empty() || m_previewJob != nullptr;
    m_previewCache.clear();
    cancelPreviewJob();
    m_previewTimer.stop();

    QPointer<PDFDocumentToolRunner> guard(this);
    if (hadPreview)
    {
        Q_EMIT previewDropped();
        if (!guard)
        {
            return;
        }
    }

    if (m_isPreviewRequested && m_tool->hasPreview() && m_state == State::Idle && m_document && m_document->isReady())
    {
        m_previewTimer.start();
    }
}

void PDFDocumentToolRunner::onDocumentStateChanged()
{
    if (m_document && !m_document->isReady())
    {
        loseDocument();
    }
}

void PDFDocumentToolRunner::onDocumentAboutToBeDestroyed()
{
    loseDocument();
}

void PDFDocumentToolRunner::loseDocument()
{
    m_previewTimer.stop();
    cancelPreviewJob();
    m_previewCache.clear();
    if (m_runJob)
    {
        m_runJob->cancel();
        m_runJob.reset();
    }

    m_state = State::Idle;
    QPointer<PDFDocumentToolRunner> guard(this);
    if (!endOperation() || !guard)
    {
        return;
    }
    Q_EMIT documentLost();
}

}   // namespace pdf

#include "moc_pdfdocumenttoolrunner.cpp"
