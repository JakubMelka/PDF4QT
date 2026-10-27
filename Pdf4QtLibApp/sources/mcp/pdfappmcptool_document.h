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


#ifndef PDFAPPMCPTOOL_DOCUMENT_H
#define PDFAPPMCPTOOL_DOCUMENT_H

#include "pdfappglobal.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcpjob.h"
#include "pdfappmcptoolhandler.h"

#include "pdffile.h"

#include <QObject>
#include <QPointer>

class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentSignatureVerifier;

/// Handler of the tool "document" (document.*, plan of step 7, 3.2; package B):
/// the structure of an open document - information, pages, outline,
/// annotations, form fields, attachments and their contents, signatures, the
/// selection of the user, layers and bookmarks. Reads of the GUI thread are
/// fast; the annotations of more than 50 pages and the contents of an
/// attachment are read in a worker.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("document"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// The longest wait for a running verification of the signatures
    static constexpr int SIGNATURE_WAIT_TIME = 10000;
};

/// What a document job reads in a worker
enum class PDFAppMcpDocumentJobKind
{
    Annotations,    ///< document.get_annotations / …/annotations
    Attachment      ///< document.read_attachment / …/attachments/{index}
};

/// A document job: the annotations of many pages or the decoded content of an
/// attachment (nothing is opened or run, O18), for a tool or a resource
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentJob : public PDFAppMcpJob
{
public:
    PDFAppMcpDocumentJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpDocumentJobKind kind, PDFAppMcpDocumentHeader header);

    /// Annotations
    void setAnnotationQuery(PDFAppMcpAnnotationQuery query) { m_query = std::move(query); }

    /// Attachment: its index, name and file specification, the most bytes returned
    void setAttachment(qint64 index, QString name, PDFFileSpecification specification, qint64 maximumBytes);

    /// The result is a resource of the URI (resources/read), not a tool result
    void setResource(QString uri) { m_resourceUri = std::move(uri); }

    virtual void execute() override;
    virtual void finish(PDFAppMcpHost* host) override;

    /// The data are a text (valid UTF-8 without NUL); \p text gets it
    static bool decodeText(const QByteArray& data, QString* text);

private:
    void executeAnnotations();
    void executeAttachment();

    PDFAppMcpDocumentJobKind m_kind;
    PDFAppMcpDocumentHeader m_header;
    PDFAppMcpAnnotationQuery m_query;
    qint64 m_attachmentIndex = 0;
    QString m_attachmentName;
    PDFFileSpecification m_specification;
    qint64 m_maximumBytes = 0;
    QString m_resourceUri;
    bool m_isFailed = false;
};

/// Waits at most PDFAppMcpDocumentTool::SIGNATURE_WAIT_TIME for the running
/// verification of the signatures of a tab (document.get_signatures), then
/// answers with the results (pending, when it still runs)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpSignatureWaiter : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    PDFAppMcpSignatureWaiter(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, PDFAppDocument* document, PDFAppDocumentSignatureVerifier* verifier);
    virtual ~PDFAppMcpSignatureWaiter() override;

    /// Shortens the wait (tests)
    void setWaitTime(int milliseconds);

private:
    void onVerificationChanged();
    void onTimeout();
    void answer();

    QPointer<PDFAppMcpHost> m_host;
    std::shared_ptr<PDFMcpCall> m_call;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentSignatureVerifier> m_verifier;
    QPointer<QTimer> m_timer;
    bool m_isDone = false;
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_DOCUMENT_H
