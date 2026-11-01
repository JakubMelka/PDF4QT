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

#ifndef PDFAPPTOOLRESULTWRITER_H
#define PDFAPPTOOLRESULTWRITER_H

#include "pdfappdocument.h"
#include "pdfappdocumentsave.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>

#include <memory>

namespace pdf
{

class PDFDocumentToolCatalogEntry;
class PDFDocumentToolResult;
class PDFDocumentToolRunner;

/// What a tool writes (plan of the first batch, chapter 2.1, F6, F7): a copy of the
/// result, a new document or a file in a worker through PDFAppDocumentFileWriter, or the
/// result over the file of the document. Nobody waits for the worker: a deleted writer
/// delivers nothing (the started worker ends its write, as the saver).
///
/// One rule of a copy (F7, also for MCP - \p createCopyRequest): a tool of the impact
/// LostCopyOnly writes the whole file - no old revision, no /Prev -, a tool of the impact
/// IncrementalSave appends to the original data of a signed document. A display version of an
/// XFA form (isPagedXFA) is refused: its pages are not the pages of the file (F5).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolResultWriter : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Result of a write
    class Result
    {
    public:
        bool isWritten = false;         ///< The file has been written
        bool isTargetExisting = false;  ///< Nothing was written: the file exists and its replacement was not allowed
        bool isCommitRefused = false;   ///< Overwrite: the file has been written, but the document of the tab could not take the result (isWritten is false; the automatic refresh sees the new bytes)
        QString filePath;               ///< Written file (canonical), else the requested one
        QString errorMessage;           ///< Translated reason of a failure
        QByteArray data;                ///< Written bytes (the bytes of a document kept in the memory for a new tab)
    };

    explicit PDFAppToolResultWriter(QObject* parent);
    virtual ~PDFAppToolResultWriter() override;

    /// Request of a copy of the result of \p tool by the rule of a copy (thread safe)
    /// \param tool Entry of the catalog of the tool (nullptr: the whole write)
    /// \param snapshot Snapshot of the document, from which the result comes
    /// \param output The changed document of the result
    /// \param filePath Target file
    static PDFAppDocumentFileWriter::Request createCopyRequest(const PDFDocumentToolCatalogEntry* tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const QString& filePath);

    /// The write of a request in the worker, which never replaces a file without
    /// \p isReplacing (the check is in the worker: a folder may lie on another computer)
    static Result writeRequest(PDFAppDocumentFileWriter::Request request, bool isReplacing);

    /// The write of the bytes of a file in the worker, as \p writeRequest
    static Result writeFileData(QString filePath, QByteArray data, QString markSourceFilePath, bool isReplacing);

    /// The bytes of a new document in the worker (a new tab reads them)
    static Result writeToMemory(PDFDocumentPointer document);

    /// A write is running
    bool isWriting() const { return !m_watcher.isNull() || m_isOverwriting; }

    /// Writes a copy of \p output in a worker: \p finished comes from the event loop.
    /// \returns false (no signal), when a write runs or the document is a display version
    bool writeCopy(const PDFDocumentToolCatalogEntry* tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const QString& filePath, bool isReplacing);

    /// Writes a new document (Save as a file of a tool of the kind NewDocument)
    bool writeDocument(const PDFDocumentPointer& document, const QString& filePath, const QString& markSourceFilePath, bool isReplacing);

    /// Writes one file of the result (Save as a file of an export)
    bool writeFile(const QByteArray& data, const QString& filePath, const QString& markSourceFilePath, bool isReplacing);

    /// Writes a new document into the memory (Open in a new tab)
    bool writeDocumentToMemory(const PDFDocumentPointer& document);

    /// Overwrite the original file: the file first, then the commit of \p result by
    /// \p runner as one undo step and the saved state (F6) - a failed write leaves the
    /// document of the tab as it is. Silent: the errors come by \p finished. A file
    /// signed with a tool of the impact IncrementalSave gets an incremental update. What
    /// would refuse the commit (PDFDocumentToolRunner::getCommitRefusal) is told before
    /// the file changes; the operation of the runner keeps the document as it is during
    /// the write, so only a lock set by the application meanwhile gives isCommitRefused.
    /// \returns false (no signal), when a write runs, the document has no file or
    /// is a display version.
    bool overwrite(PDFDocumentToolRunner* runner, PDFAppDocumentSaver* saver, const PDFDocumentToolResult& result, const QString& undoDescription);

Q_SIGNALS:
    /// A write has ended. A slot may delete the writer.
    void finished(const pdf::PDFAppToolResultWriter::Result& result);

private:
    void start(QFuture<Result> future);
    void postFailure(const QString& filePath, const QString& errorMessage);
    void onWatcherFinished();
    void onSilentWriteFinished(const PDFAppDocumentFileWriter::Result& result);

    QPointer<QFutureWatcher<Result>> m_watcher;
    QPointer<PDFDocumentToolRunner> m_runner;
    QPointer<PDFAppDocumentSaver> m_saver;
    QString m_undoDescription;
    bool m_isOverwriting = false;
    std::shared_ptr<PDFDocumentToolResult> m_overwriteResult;
};

}   // namespace pdf

#endif // PDFAPPTOOLRESULTWRITER_H
