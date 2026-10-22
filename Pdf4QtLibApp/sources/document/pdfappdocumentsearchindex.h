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

#ifndef PDFAPPDOCUMENTSEARCHINDEX_H
#define PDFAPPDOCUMENTSEARCHINDEX_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdfoperationcontrol.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <memory>

class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentOperation;
class PDFDocumentTextIndex;
class PDFModifiedDocument;

/// Cancellation flag of a job of a worker thread (thread safe). The job holds it
/// by a shared pointer, so the owner can go away (a closed tab) without waiting.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCancelFlag : public PDFOperationControl
{
public:
    PDFAppCancelFlag() = default;

    virtual bool isOperationCancelled() const override { return m_isCanceled.load(std::memory_order_acquire); }

    void cancel() { m_isCanceled.store(true, std::memory_order_release); }

private:
    std::atomic_bool m_isCanceled = false;
};

/// State of one build of the text index shared by the worker and the indexer
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTextIndexJob
{
public:
    PDFAppCancelFlag cancelFlag;
    std::atomic<PDFInteger> processedPageCount = 0;
    PDFInteger pageCount = 0;
    quint64 version = 0;
};

/// Builds the text index of the document of a tab (PDFDocumentTextIndex) as the
/// Editor does (newapp/step3_large_documents.md): lazily, at the first request
/// (search, speech), in a worker thread from a snapshot of the document, all pages
/// in parallel into the compact storage - but cancellable: a change of the page
/// contents, the destruction of the indexer (a closed tab) and Stop of the
/// operation cancel the build, nobody waits for it. The index belongs to the
/// document (PDFAppDocument::setTextIndex), which keeps it over changes without new
/// page contents; a changed version is indexed again at the next request.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentTextIndexer : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using Watcher = QFutureWatcher<std::shared_ptr<const PDFDocumentTextIndex>>;

    explicit PDFAppDocumentTextIndexer(PDFAppDocument* document, QObject* parent);
    virtual ~PDFAppDocumentTextIndexer() override;

    /// Index of the current version (nullptr = not built yet)
    std::shared_ptr<const PDFDocumentTextIndex> getIndex() const;

    /// Requests the index: starts the build, when there is none and none is
    /// running. \returns true, when the index is ready already.
    bool requestIndex();

    /// The index is being built
    bool isBuilding() const { return m_job != nullptr; }

    /// Progress of the build 0 - 100 (0 when nothing is built)
    int getProgress() const;

    /// Cancels the running build (the index is built again at the next request)
    void cancel();

    /// Builds the index of the document (the function of the worker thread)
    static std::shared_ptr<const PDFDocumentTextIndex> buildIndex(PDFDocumentPointer document, std::shared_ptr<PDFAppTextIndexJob> job);

Q_SIGNALS:
    /// The index of the current version is ready (getIndex)
    void indexReady();

    /// The build has started, has ended or has been canceled
    void buildingChanged();

    /// The progress of the build has changed (getProgress)
    void progressChanged(int progress);

private:
    void onBuildFinished();
    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    void onProgressTimer();
    void onOperationCancelRequested();
    void stopJob();

    QPointer<PDFAppDocument> m_document;
    QPointer<Watcher> m_watcher;
    QPointer<QTimer> m_progressTimer;
    QPointer<PDFAppDocumentOperation> m_operation;
    std::shared_ptr<PDFAppTextIndexJob> m_job;
    int m_lastProgress = -1;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSEARCHINDEX_H
