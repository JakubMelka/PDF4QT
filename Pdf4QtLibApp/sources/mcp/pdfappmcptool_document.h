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

    /// The lists of the images of list_images (shared with its jobs, RS4APP)
    const std::shared_ptr<PDFAppMcpImageListCache>& getImageListCache() const { return m_imageListCache; }

private:
    std::shared_ptr<PDFAppMcpImageListCache> m_imageListCache = std::make_shared<PDFAppMcpImageListCache>();
};

/// What a document job reads in a worker
enum class PDFAppMcpDocumentJobKind
{
    Annotations,    ///< document.get_annotations / …/annotations
    Attachment,     ///< document.read_attachment / …/attachments/{index}
    Images          ///< document.list_images (every page is walked: always a worker)
};

/// A document job: the annotations of many pages or the decoded content of an
/// attachment (nothing is opened or run, O18), for a tool or a resource
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentJob : public PDFAppMcpJob
{
public:
    PDFAppMcpDocumentJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpDocumentJobKind kind, PDFAppMcpDocumentHeader header);

    /// Annotations
    void setAnnotationQuery(PDFAppMcpAnnotationQuery query) { m_query = std::move(query); }

    /// Images; the list of the version of the document is taken from \p cache, or collected and
    /// put there (the cursors of one listing walk the pages once)
    void setImageQuery(PDFAppMcpImageQuery query, std::shared_ptr<PDFAppMcpImageListCache> cache)
    {
        m_imageQuery = std::move(query);
        m_imageListCache = std::move(cache);
    }

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
    void executeImages();

    /// The action of the job ("document.get_annotations")
    QString getActionId() const;

    PDFAppMcpDocumentJobKind m_kind;
    PDFAppMcpDocumentHeader m_header;
    PDFAppMcpAnnotationQuery m_query;
    PDFAppMcpImageQuery m_imageQuery;
    std::shared_ptr<PDFAppMcpImageListCache> m_imageListCache;
    qint64 m_attachmentIndex = 0;
    QString m_attachmentName;
    PDFFileSpecification m_specification;
    qint64 m_maximumBytes = 0;
    QString m_resourceUri;
    bool m_isFailed = false;
};

/// Waits at most PDFAppMcpDocumentTool::SIGNATURE_WAIT_TIME for the running
/// verification of the signatures of a tab (document.get_signatures), then
/// answers with the results (pending, when it still runs); a canceled call
/// (PDFAppMcpHost::callCancelReceived) is answered at once
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
    void onCallCanceled(const std::shared_ptr<pdf::PDFMcpCall>& call);
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
