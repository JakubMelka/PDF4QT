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

#include "pdfdocumenttool.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>

#include <memory>

namespace pdf
{

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
///
/// The rules read the values of the output of the run (PDFDocumentToolOutputTraits; plan of step
/// 3b of phase 2, RB3 a): PDFDocumentTool::getOutputTraits with the parameters of the run, or a
/// catalog entry (its row - the implicit conversion) where no run is known.
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

    /// Why a tool cannot work on a document (one rule of the shell and of MCP, F5, T3)
    enum class DocumentRefusal
    {
        None,
        Locked,         ///< The document is locked, the tool is not available for a locked document
        DisplayVersion  ///< A display version of an XFA form, the tool changes the document
    };

    explicit PDFAppToolResultWriter(QObject* parent);
    virtual ~PDFAppToolResultWriter() override;

    /// Why the tool cannot work on the document (its texts are of the caller)
    static DocumentRefusal getDocumentRefusal(const PDFDocumentToolOutputTraits& tool, const PDFAppDocument* document);

    /// O31: the tool rewrites the file, so its result of a signed document can only be a
    /// copy (Keep and Overwrite are refused). Without a file nothing is signed by this
    /// rule (F5). One rule of the result dialog and of MCP.
    /// Q12, R10 of step 3: the result of a certified document, whose certification the tool
    /// has removed, is only a copy, also without a file.
    /// \param isOnlyCopyWritable PDFAppDocumentFileWriter::isOnlyCopyWritable of the document
    /// \param result The result (optional): its data may make it a whole-file rewrite (isRewrittenWhole)
    static bool isCopyOnly(const PDFDocumentToolOutputTraits& tool, bool isSigned, bool hasFile, bool isOnlyCopyWritable, const PDFDocumentToolResult* result = nullptr);

    /// The tool rewrites the file, so the signatures of a signed document do not survive a
    /// save of its result, also without a file, where the result can be kept (the warning of
    /// the result dialog; owner 2026-11-02)
    /// \param isOnlyCopyWritable PDFAppDocumentFileWriter::isOnlyCopyWritable of the document
    /// \param result The result (optional): its data may make it a whole-file rewrite (isRewrittenWhole)
    static bool isSignatureLost(const PDFDocumentToolOutputTraits& tool, bool isSigned, bool isOnlyCopyWritable, const PDFDocumentToolResult* result = nullptr);

    /// The result is written whole, never as an incremental update: the tool is of the impact
    /// LostCopyOnly, or (Q12, R10 of step 3) a page tool of the impact IncrementalSave has
    /// changed a certified document (DocMDP) - an incremental update would mark the certification
    /// as broken (isCertificationRemoved)
    /// \param tool Entry of the catalog of the tool (may be null)
    /// \param result The result (may be null: the catalog decides)
    static bool isRewrittenWhole(const PDFDocumentToolOutputTraits& tool, const PDFDocumentToolResult* result);

    /// The result comes from a certified document, whose pages the tool has changed (Q12, R10):
    /// the data "signatureImpact" "lost-copy-only" of a tool, which does not rewrite the file by
    /// its catalog, "certificationRemoved" (PDFDocMDPPermissions::addCertificationRemoved), or
    /// "certified-copy-only" of Crop, Resize and Boxes (the same meaning)
    /// \param tool Entry of the catalog of the tool (may be null)
    /// \param result The result (may be null: false)
    static bool isCertificationRemoved(const PDFDocumentToolOutputTraits& tool, const PDFDocumentToolResult* result);

    /// Request of a copy of the result of \p tool by the rule of a copy (thread safe)
    /// \param tool Entry of the catalog of the tool (nullptr: the whole write)
    /// \param snapshot Snapshot of the document, from which the result comes
    /// \param output The changed document of the result
    /// \param filePath Target file
    /// \param result The result of the output (optional, isRewrittenWhole: a certified document is written whole)
    static PDFAppDocumentFileWriter::Request createCopyRequest(const PDFDocumentToolOutputTraits& tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const QString& filePath, const PDFDocumentToolResult* result = nullptr);

    /// Request of a result over the file of its document (F6): the rule of a copy, the
    /// Mark of the Web of the origin of the document
    static PDFAppDocumentFileWriter::Request createOverwriteRequest(const PDFDocumentToolOutputTraits& tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const PDFAppDocument* document, const PDFDocumentToolResult* result = nullptr);

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
    bool writeCopy(const PDFDocumentToolOutputTraits& tool, const PDFAppDocumentSnapshot& snapshot, const PDFDocumentPointer& output, const QString& filePath, const QString& markSourceFilePath, bool isReplacing, const PDFDocumentToolResult* result = nullptr);

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
