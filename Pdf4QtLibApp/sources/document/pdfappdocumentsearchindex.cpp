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

#include "pdfappdocumentsearchindex.h"
#include "pdfappdocument.h"

#include "pdfdocumenttextindex.h"

#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

PDFAppDocumentTextIndexer::PDFAppDocumentTextIndexer(PDFAppDocument* document, QObject* parent) :
    BaseClass(parent),
    m_document(document)
{
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(200);
    connect(m_progressTimer, &QTimer::timeout, this, &PDFAppDocumentTextIndexer::onProgressTimer);

    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppDocumentTextIndexer::onDocumentChanged);
    }
}

PDFAppDocumentTextIndexer::~PDFAppDocumentTextIndexer()
{
    // The worker keeps its snapshot and its job, it ends soon after the
    // cancellation; nobody waits for it (closing a tab never waits)
    stopJob();
}

std::shared_ptr<const PDFDocumentTextIndex> PDFAppDocumentTextIndexer::getIndex() const
{
    return m_document ? m_document->getTextIndex() : nullptr;
}

bool PDFAppDocumentTextIndexer::requestIndex()
{
    if (getIndex())
    {
        return true;
    }

    if (m_job || !m_document || !m_document->isReady())
    {
        return false;
    }

    const PDFAppDocumentSnapshot snapshot = m_document->getSnapshot();
    if (!snapshot.isValid())
    {
        return false;
    }

    m_job = std::make_shared<PDFAppTextIndexJob>();
    m_job->version = snapshot.version;
    m_job->pageCount = PDFInteger(snapshot.document->getCatalog()->getPageCount());
    m_lastProgress = -1;

    m_watcher = new Watcher(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &PDFAppDocumentTextIndexer::onBuildFinished);
    m_watcher->setFuture(QtConcurrent::run(&PDFAppDocumentTextIndexer::buildIndex, snapshot.document, m_job));

    // The progress of the tab; the operation does not block changes (a change of
    // the page contents only cancels the build)
    m_operation = m_document->beginOperation(PDFAppDocumentOperationKind::Index, tr("Indexing the text of the document"), false);
    if (m_operation)
    {
        connect(m_operation, &PDFAppDocumentOperation::cancelRequested, this, &PDFAppDocumentTextIndexer::onOperationCancelRequested);
    }
    m_progressTimer->start();

    QPointer<PDFAppDocumentTextIndexer> guard(this);
    Q_EMIT buildingChanged();
    if (guard)
    {
        Q_EMIT progressChanged(0);
    }
    return false;
}

int PDFAppDocumentTextIndexer::getProgress() const
{
    if (!m_job || m_job->pageCount <= 0)
    {
        return 0;
    }

    const PDFInteger processed = qBound(PDFInteger(0), m_job->processedPageCount.load(std::memory_order_relaxed), m_job->pageCount);
    return int((processed * 100) / m_job->pageCount);
}

void PDFAppDocumentTextIndexer::cancel()
{
    if (!m_job)
    {
        return;
    }

    stopJob();
    Q_EMIT buildingChanged();
}

void PDFAppDocumentTextIndexer::stopJob()
{
    if (m_job)
    {
        m_job->cancelFlag.cancel();
        m_job.reset();
    }

    // The result of the canceled build is not wanted any more; the watcher does
    // not wait for the worker
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }

    if (m_progressTimer)
    {
        m_progressTimer->stop();
    }

    QPointer<PDFAppDocumentOperation> operation = m_operation;
    m_operation = nullptr;
    if (operation)
    {
        operation->disconnect(this);
        operation->finish();
    }
}

std::shared_ptr<const PDFDocumentTextIndex> PDFAppDocumentTextIndexer::buildIndex(PDFDocumentPointer document, std::shared_ptr<PDFAppTextIndexJob> job)
{
    if (!document || !job)
    {
        return nullptr;
    }

    // The default optional content of the view: the activity of the document is a
    // QObject of the GUI thread, which the user may change during the build
    return PDFDocumentTextIndex::build(document.data(), nullptr, &job->cancelFlag, nullptr, &job->processedPageCount);
}

void PDFAppDocumentTextIndexer::onBuildFinished()
{
    std::shared_ptr<const PDFDocumentTextIndex> index;
    if (m_watcher)
    {
        index = m_watcher->result();
    }
    const quint64 version = m_job ? m_job->version : 0;
    const bool isFinished = m_job != nullptr;
    stopJob();

    // The document refuses an index of a version older than its page contents
    if (index && isFinished && m_document)
    {
        m_document->setTextIndex(index, version);
    }

    QPointer<PDFAppDocumentTextIndexer> guard(this);
    Q_EMIT buildingChanged();
    if (!guard)
    {
        return;
    }

    if (getIndex())
    {
        Q_EMIT indexReady();
    }
    else if (index)
    {
        // The page contents have changed at the end of the build: the requester
        // is still waiting, the current version is indexed
        requestIndex();
    }
}

void PDFAppDocumentTextIndexer::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // A build of an old version of the page contents is useless: the requester
    // still waits, so the new version is indexed instead
    if (m_job && (modified.hasReset() || modified.hasPageContentsChanged()))
    {
        stopJob();
        if (!requestIndex() && !m_job)
        {
            // The document cannot be indexed now (it is not ready)
            Q_EMIT buildingChanged();
        }
    }
}

void PDFAppDocumentTextIndexer::onProgressTimer()
{
    const int progress = getProgress();
    if (progress == m_lastProgress)
    {
        return;
    }

    m_lastProgress = progress;
    if (m_operation)
    {
        m_operation->setProgress(progress);
    }
    Q_EMIT progressChanged(progress);
}

void PDFAppDocumentTextIndexer::onOperationCancelRequested()
{
    cancel();
}

}   // namespace pdf
