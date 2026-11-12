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

#include "pdfapptoolresultwriter.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfdocumenttool.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfform.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

// The limit of a result of a tool and of a written file is the same limit of a QByteArray
static_assert(PDFDocumentTool::MAXIMUM_RESULT_SIZE == PDFAppDocumentFileWriter::MAXIMUM_FILE_SIZE);

/// Helpers of the writer
class PDFAppToolResultWriterHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolResultWriter)

public:
    PDFAppToolResultWriterHelper() = delete;

    /// Result of a write of the file writer
    static PDFAppToolResultWriter::Result convert(const PDFAppDocumentFileWriter::Result& result)
    {
        PDFAppToolResultWriter::Result converted;
        converted.isWritten = result.isSaved;
        converted.filePath = result.isSaved ? result.filePath : result.requestedFilePath;
        converted.errorMessage = result.errorMessage;
        return converted;
    }

    /// A file exists (also a link, whose target is missing): a copy never replaces it
    /// without the consent of the user. A path of a dangling link is refused later
    /// by the system.
    static bool isTargetExisting(const QString& filePath)
    {
        const QFileInfo info(filePath);
        return info.exists() || info.isSymLink();
    }
};

PDFAppToolResultWriter::PDFAppToolResultWriter(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolResultWriter::~PDFAppToolResultWriter()
{
    // The worker keeps its data and ends by itself; nothing is delivered
    if (m_watcher)
    {
        m_watcher->disconnect(this);
    }
}

PDFAppDocumentFileWriter::Request PDFAppToolResultWriter::createCopyRequest(const PDFDocumentToolOutputTraits& tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const QString& filePath, const PDFDocumentToolResult* result)
{
    PDFAppDocumentFileWriter::Request request;
    request.document = output;
    request.filePath = filePath;
    request.version = snapshot.version;
    request.markSourceFilePath = snapshot.filePath;

    // The signed revision stays in the file only by an incremental update; a tool, which
    // rewrites the file, writes it whole: no old revision is left in the file (O31). A
    // result without any signed field (the signed page was removed) has nothing to keep,
    // a version an earlier tool has rewritten and a damaged file are no update of the original bytes.
    // A certified document changed by a page tool is written whole (Q12, R10 of step 3).
    // The impact of the output of the run (RB3 a of step 3b: Manage Pages This document)
    if (tool.signatureImpact == PDFDocumentToolSignatureImpact::IncrementalSave && !isRewrittenWhole(tool, result) &&
        !snapshot.originalData.isEmpty() && !snapshot.isWholeFileRewritten && !snapshot.isSignedRevisionGone &&
        snapshot.restoration == PDFAppDocumentRestoration::None && PDFForm::getSignatureCount(output.data()) > 0)
    {
        request.originalData = snapshot.originalData;
        request.isIncremental = true;
    }
    return request;
}

PDFAppDocumentFileWriter::Request PDFAppToolResultWriter::createOverwriteRequest(const PDFDocumentToolOutputTraits& tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const PDFAppDocument* document, const PDFDocumentToolResult* result)
{
    PDFAppDocumentFileWriter::Request request = createCopyRequest(tool, snapshot, output, document ? document->getFilePath() : QString(), result);
    request.markSourceFilePath = document ? document->getOriginFilePath() : QString();
    return request;
}

PDFAppToolResultWriter::DocumentRefusal PDFAppToolResultWriter::getDocumentRefusal(const PDFDocumentToolOutputTraits& tool, const PDFAppDocument* document)
{
    if (!tool.entry || !document)
    {
        return DocumentRefusal::None;
    }

    // The lock of a file comes only when the document is shown (T3). A tool available for a
    // locked document opens over it; a change of the locked document by the output of a run
    // (Manage Pages This document, plan of step 3b, 4.5) is refused where it is decided - the
    // result dialog (no Keep, no Overwrite), the commit of the runner and MCP (keep, overwrite).
    if (document->isLocked() && tool.entry->getLockedAccess() == PDFDocumentToolLockedAccess::Unavailable)
    {
        return DocumentRefusal::Locked;
    }

    // A display version of an XFA form cannot be changed, an export of its text can run (F5);
    // the kind of the output of the run (RB3 a of step 3b)
    if (document->isPagedXFA() && tool.resultKind == PDFDocumentToolResultKind::ChangeDocument)
    {
        return DocumentRefusal::DisplayVersion;
    }
    return DocumentRefusal::None;
}

bool PDFAppToolResultWriter::isCopyOnly(const PDFDocumentToolOutputTraits& tool, bool isSigned, bool hasFile, bool isOnlyCopyWritable, const PDFDocumentToolResult* result)
{
    // Q12: a certified document changed by a page tool is only a copy, also without a file
    return isCertificationRemoved(tool, result) || (hasFile && (isOnlyCopyWritable || isSignatureLost(tool, isSigned, isOnlyCopyWritable, result)));
}

bool PDFAppToolResultWriter::isSignatureLost(const PDFDocumentToolOutputTraits& tool, bool isSigned, bool isOnlyCopyWritable, const PDFDocumentToolResult* result)
{
    return isSigned && (isOnlyCopyWritable || isRewrittenWhole(tool, result));
}

bool PDFAppToolResultWriter::isRewrittenWhole(const PDFDocumentToolOutputTraits& tool, const PDFDocumentToolResult* result)
{
    return tool.signatureImpact == PDFDocumentToolSignatureImpact::LostCopyOnly || isCertificationRemoved(tool, result);
}

bool PDFAppToolResultWriter::isCertificationRemoved(const PDFDocumentToolOutputTraits& tool, const PDFDocumentToolResult* result)
{
    if (!result)
    {
        return false;
    }

    // "lost-copy-only" in the data of a tool of the impact LostCopyOnly (Compress) is its
    // catalog impact (finishWholeFileResult), not a certification
    const QString impact = result->data.value(QStringLiteral("signatureImpact")).toString();
    const bool isRewritingTool = tool.signatureImpact == PDFDocumentToolSignatureImpact::LostCopyOnly;
    return result->data.value(QStringLiteral("certificationRemoved")).toBool() || impact == QLatin1String("certified-copy-only") ||
           (impact == QLatin1String("lost-copy-only") && !isRewritingTool);
}

PDFAppToolResultWriter::Result PDFAppToolResultWriter::writeRequest(PDFAppDocumentFileWriter::Request request, bool isReplacing)
{
    if (!isReplacing && PDFAppToolResultWriterHelper::isTargetExisting(request.filePath))
    {
        Result result;
        result.isTargetExisting = true;
        result.filePath = request.filePath;
        return result;
    }

    return PDFAppToolResultWriterHelper::convert(PDFAppDocumentFileWriter::write(std::move(request)));
}

PDFAppToolResultWriter::Result PDFAppToolResultWriter::writeFileData(QString filePath, QByteArray data, QString markSourceFilePath, bool isReplacing)
{
    if (!isReplacing && PDFAppToolResultWriterHelper::isTargetExisting(filePath))
    {
        Result result;
        result.isTargetExisting = true;
        result.filePath = filePath;
        return result;
    }

    return PDFAppToolResultWriterHelper::convert(PDFAppDocumentFileWriter::writeData(std::move(filePath), std::move(data), std::move(markSourceFilePath)));
}

PDFAppToolResultWriter::Result PDFAppToolResultWriter::writeToMemory(PDFDocumentPointer document)
{
    Result result;
    if (document.isNull())
    {
        result.errorMessage = PDFAppToolResultWriterHelper::tr("There is no document to save.");
        return result;
    }

    try
    {
        QBuffer buffer(&result.data);
        if (!buffer.open(QIODevice::WriteOnly))
        {
            result.errorMessage = PDFAppToolResultWriterHelper::tr("The document cannot be written into the memory.");
            return result;
        }

        PDFDocumentWriter writer(nullptr);
        const PDFOperationResult writeResult = writer.write(&buffer, document.data());
        buffer.close();
        if (!writeResult)
        {
            result.errorMessage = writeResult.getErrorMessage();
            result.data.clear();
            return result;
        }
        result.isWritten = true;
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        result.data.clear();
    }
    return result;
}

void PDFAppToolResultWriter::start(QFuture<Result> future)
{
    QFutureWatcher<Result>* watcher = new QFutureWatcher<Result>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<Result>::finished, this, &PDFAppToolResultWriter::onWatcherFinished);
    watcher->setFuture(future);
}

bool PDFAppToolResultWriter::writeCopy(const PDFDocumentToolOutputTraits& tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const QString& filePath, const QString& markSourceFilePath, bool isReplacing, const PDFDocumentToolResult* result)
{
    // A display version of an XFA form is never written as a changed copy (F5)
    if (isWriting() || snapshot.isPagedXFA || output.isNull())
    {
        return false;
    }

    PDFAppDocumentFileWriter::Request request = createCopyRequest(tool, snapshot, output, filePath, result);
    request.markSourceFilePath = markSourceFilePath;
    start(QtConcurrent::run(&PDFAppToolResultWriter::writeRequest, std::move(request), isReplacing));
    return true;
}

bool PDFAppToolResultWriter::writeDocument(const PDFDocumentPointer& document, const QString& filePath, const QString& markSourceFilePath, bool isReplacing)
{
    if (isWriting() || document.isNull())
    {
        return false;
    }

    PDFAppDocumentFileWriter::Request request;
    request.document = document;
    request.filePath = filePath;
    request.markSourceFilePath = markSourceFilePath;
    start(QtConcurrent::run(&PDFAppToolResultWriter::writeRequest, std::move(request), isReplacing));
    return true;
}

bool PDFAppToolResultWriter::writeFile(const QByteArray& data, const QString& filePath, const QString& markSourceFilePath, bool isReplacing)
{
    if (isWriting())
    {
        return false;
    }

    start(QtConcurrent::run(&PDFAppToolResultWriter::writeFileData, filePath, data, markSourceFilePath, isReplacing));
    return true;
}

bool PDFAppToolResultWriter::writeDocumentToMemory(const PDFDocumentPointer& document)
{
    if (isWriting() || document.isNull())
    {
        return false;
    }

    start(QtConcurrent::run(&PDFAppToolResultWriter::writeToMemory, document));
    return true;
}

void PDFAppToolResultWriter::onWatcherFinished()
{
    QFutureWatcher<Result>* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const Result result = watcher->future().result();
    m_watcher = nullptr;
    watcher->deleteLater();
    Q_EMIT finished(result);
}

bool PDFAppToolResultWriter::overwrite(PDFDocumentToolRunner* runner, PDFAppDocumentSaver* saver, const PDFDocumentToolResult& result, const QString& undoDescription)
{
    PDFAppDocument* document = runner ? runner->getDocument() : nullptr;
    const PDFDocumentToolOutputDocument* output = PDFDocumentToolRunner::findChangedDocument(result);
    if (isWriting() || !document || !saver || !output || document->getFilePath().isEmpty() || document->isPagedXFA())
    {
        return false;
    }

    // The file comes first and nothing takes it back (F6): what would refuse the commit
    // is told before the file changes
    const QString refusal = runner->getCommitRefusal();
    if (!refusal.isEmpty())
    {
        postFailure(document->getFilePath(), refusal);
        return true;
    }

    const PDFAppDocumentSnapshot& snapshot = runner->getResultSnapshot();
    PDFAppDocumentFileWriter::Request request = createOverwriteRequest(runner->getOutputTraits(), snapshot, output->document, document, &result);

    m_runner = runner;
    m_saver = saver;
    m_undoDescription = undoDescription;
    m_overwriteResult = std::make_shared<PDFDocumentToolResult>(result);
    connect(saver, &PDFAppDocumentSaver::silentWriteFinished, this, &PDFAppToolResultWriter::onSilentWriteFinished, Qt::UniqueConnection);

    QString error;
    m_isOverwriting = true;
    if (!saver->writeSilently(std::move(request), &error))
    {
        m_isOverwriting = false;
        disconnect(saver, nullptr, this, nullptr);
        postFailure(document->getFilePath(), error);
    }
    return true;
}

void PDFAppToolResultWriter::postFailure(const QString& filePath, const QString& errorMessage)
{
    // The reason of a write, which did not start, comes as the others do
    Result failure;
    failure.filePath = filePath;
    failure.errorMessage = errorMessage;
    QMetaObject::invokeMethod(this, &PDFAppToolResultWriter::finished, Qt::QueuedConnection, failure);
}

void PDFAppToolResultWriter::onSilentWriteFinished(const PDFAppDocumentFileWriter::Result& result)
{
    m_isOverwriting = false;
    if (m_saver)
    {
        disconnect(m_saver.data(), nullptr, this, nullptr);
    }

    Result finishedResult = PDFAppToolResultWriterHelper::convert(result);
    QPointer<PDFAppToolResultWriter> guard(this);
    if (result.isSaved)
    {
        // The file has the bytes of the result: the commit makes them the document. A
        // slot of the commit may delete the runner, the document stays saved
        PDFDocumentToolRunner* runner = m_runner.data();
        QPointer<PDFAppDocument> document = runner ? runner->getDocument() : nullptr;
        QString commitError;
        const PDFDocumentToolRunner::CommitStatus status = runner ? runner->commitResult(*m_overwriteResult, m_undoDescription, &commitError) : PDFDocumentToolRunner::CommitStatus::Refused;
        if (!guard)
        {
            return;
        }

        if (status == PDFDocumentToolRunner::CommitStatus::Committed)
        {
            if (m_saver && document)
            {
                m_saver->completeSilentWrite(document->getVersion(), result);
                if (!guard)
                {
                    return;
                }
            }
        }
        else
        {
            // Told before the write (getCommitRefusal), the operation keeps the version
            // meanwhile: only a lock set by the application reaches this. The tab keeps
            // its document, the automatic refresh sees a file of other bytes and asks.
            finishedResult.isWritten = false;
            finishedResult.isCommitRefused = true;
            finishedResult.errorMessage = commitError.isEmpty() ? tr("The document has changed meanwhile.") : commitError;
        }
    }

    Q_EMIT finished(finishedResult);
}

}   // namespace pdf

#include "moc_pdfapptoolresultwriter.cpp"
