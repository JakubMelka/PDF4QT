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


#include "pdfappmcptool_document.h"
#include "pdfappmcpjob.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcptoolset.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumenttab.h"

#include "pdfdocument.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"

#include <QStringDecoder>
#include <QTimer>

namespace pdf
{

/// Helpers of the tool "document"
class PDFAppMcpDocumentToolHelper
{
public:
    PDFAppMcpDocumentToolHelper() = delete;

    static void finishRead(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, const QByteArray& object, bool hasDocumentContent,
                           const std::set<int>& pages = std::set<int>())
    {
        invocation.host->recordActivity(invocation.call, invocation.getActionId(), document, QJsonObject(), PDFMcpActivityStatus::Ok, pages);
        invocation.call->finish(PDFAppMcpReadSupport::createResult(invocation.actionName, object, hasDocumentContent));
    }

    static void getPages(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document)
    {
        PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
        const QString pagesText = reader.getString(QLatin1StringView("pages"), 4096, false).value_or(QString());
        const PDFDocumentPointer pdfDocument = document->getDocument();
        const PDFInteger pageCount = PDFInteger(pdfDocument->getCatalog()->getPageCount());

        QString error;
        std::optional<std::vector<PDFInteger>> pages = PDFAppMcpReadSupport::parsePages(pagesText, pageCount, 0, &error);
        if (!pages)
        {
            invocation.host->finishWithError(invocation.call, invocation.getActionId(), document, error);
            return;
        }

        // At most 2000 pages per call, the rest by nextPage
        std::optional<PDFInteger> nextPage;
        if (pages->size() > size_t(PDFAppMcpToolSet::MAXIMUM_PAGES_PER_CALL))
        {
            nextPage = (*pages)[size_t(PDFAppMcpToolSet::MAXIMUM_PAGES_PER_CALL)] + 1;
            pages->resize(size_t(PDFAppMcpToolSet::MAXIMUM_PAGES_PER_CALL));
        }

        const QByteArray object = PDFAppMcpDocumentReader::getPages(PDFAppMcpDocumentHeader::create(document), pdfDocument.data(), *pages, nextPage);
        // The page labels are texts of the document (untrusted)
        finishRead(invocation, document, object, true);
    }

    static void getAnnotations(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document)
    {
        PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
        const QString pagesText = reader.getString(QLatin1StringView("pages"), 4096, false).value_or(QString());
        const PDFDocumentPointer pdfDocument = document->getDocument();
        const PDFInteger pageCount = PDFInteger(pdfDocument->getCatalog()->getPageCount());

        QString error;
        std::optional<std::vector<PDFInteger>> pages = PDFAppMcpReadSupport::parsePages(pagesText, pageCount, 0, &error);
        if (!pages)
        {
            invocation.host->finishWithError(invocation.call, invocation.getActionId(), document, error);
            return;
        }

        PDFAppMcpAnnotationQuery query;
        query.pageIndices = std::move(*pages);
        query.includeReplies = reader.getBool(QLatin1StringView("includeReplies"), false).value_or(true);
        query.maximumResults = int(reader.getInteger(QLatin1StringView("maxResults"), 1, PDFAppMcpToolSet::MAXIMUM_ANNOTATION_RESULTS, false).value_or(PDFAppMcpToolSet::DEFAULT_ANNOTATION_RESULTS));
        for (const QJsonValue& type : reader.getArray(QLatin1StringView("types"), 32, false).value_or(QJsonArray()))
        {
            query.types.insert(type.toString());
        }

        const QString cursor = reader.getString(QLatin1StringView("cursor"), 64, false).value_or(QString());
        if (!cursor.isEmpty())
        {
            const std::optional<qint64> offset = PDFAppMcpDocumentReader::parseCursor(cursor);
            if (!offset)
            {
                invocation.host->finishWithError(invocation.call, invocation.getActionId(), document,
                                                 QStringLiteral("cursor: the cursor is not valid. Pass nextCursor of the previous result unchanged, or omit it."));
                return;
            }
            query.offset = *offset;
        }

        const PDFAppMcpDocumentHeader header = PDFAppMcpDocumentHeader::create(document);
        if (query.pageIndices.size() > size_t(PDFAppMcpDocumentReader::ANNOTATION_WORKER_PAGE_LIMIT))
        {
            std::shared_ptr<PDFAppMcpDocumentJob> job = std::make_shared<PDFAppMcpDocumentJob>(invocation.call, document->getSnapshot(), PDFAppMcpDocumentJobKind::Annotations, header);
            job->setAnnotationQuery(std::move(query));
            invocation.host->startJob(std::move(job));
            return;
        }

        const QByteArray object = PDFAppMcpDocumentReader::getAnnotations(header, pdfDocument.data(), query);
        finishRead(invocation, document, object, true, PDFAppMcpReadSupport::getActivityPages(query.pageIndices));
    }

    static void readAttachment(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document)
    {
        PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
        const qint64 index = reader.getInteger(QLatin1StringView("index"), 0, 1000000, true).value_or(-1);
        const qint64 maximumBytes = reader.getInteger(QLatin1StringView("maxBytes"), 1, PDFAppMcpToolSet::MAXIMUM_ATTACHMENT_BYTES, false).value_or(PDFAppMcpToolSet::DEFAULT_ATTACHMENT_BYTES);
        std::shared_ptr<const std::vector<PDFAppAttachment>> attachments = PDFAppAttachments::getAttachments(document);
        if (!attachments || index < 0 || index >= qint64(attachments->size()))
        {
            const qint64 count = attachments ? qint64(attachments->size()) : 0;
            invocation.host->finishWithError(invocation.call, invocation.getActionId(), document,
                                             count > 0 ? QStringLiteral("index: the document has %1 attachments (indices 0 to %2, from document.get_attachments).").arg(count).arg(count - 1)
                                                       : QStringLiteral("index: the document has no attachments."));
            return;
        }

        const PDFAppAttachment& attachment = attachments->at(size_t(index));
        std::shared_ptr<PDFAppMcpDocumentJob> job = std::make_shared<PDFAppMcpDocumentJob>(invocation.call, document->getSnapshot(), PDFAppMcpDocumentJobKind::Attachment,
                                                                                            PDFAppMcpDocumentHeader::create(document));
        job->setAttachment(index, attachment.name, attachment.specification, maximumBytes);
        invocation.host->startJob(std::move(job));
    }

    static void getSignatures(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document)
    {
        PDFAppDocumentTab* tab = invocation.host->getResolver().findTab(document);
        PDFAppDocumentSearchCommandHandler* handler = tab ? PDFAppDocumentSearchCommandHandler::findHandler(tab) : nullptr;
        PDFAppDocumentSignatureVerifier* verifier = handler ? handler->getSignatureVerifier() : nullptr;
        if (verifier && verifier->isVerifying())
        {
            // The verification runs: the result is waited for (at most 10 s)
            new PDFAppMcpSignatureWaiter(invocation.host, invocation.call, document, verifier);
            return;
        }
        finishRead(invocation, document, PDFAppMcpDocumentReader::getSignatures(document, false), true);
    }
};

void PDFAppMcpDocumentTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    using Helper = PDFAppMcpDocumentToolHelper;

    PDFAppMcpHost* host = invocation.host;
    const PDFAppMcpResolvedDocument resolved = host->getResolver().resolveForCall(invocation.call, invocation.documentId, true);
    if (resolved.status == PDFAppMcpResolvedDocument::Status::InputRequested)
    {
        return;
    }
    if (!resolved.isResolved())
    {
        host->finishWithError(invocation.call, invocation.getActionId(), resolved.document, resolved.errorText);
        return;
    }

    PDFAppDocument* document = resolved.document;
    const PDFDocumentPointer pdfDocument = document->getDocument();
    const QString& action = invocation.actionName;
    PDFMcpJsonReader reader(invocation.actionArguments, action);

    if (action == QLatin1String("get_info"))
    {
        Helper::finishRead(invocation, document, PDFAppMcpDocumentReader::getInfo(document), true);
    }
    else if (action == QLatin1String("get_pages"))
    {
        Helper::getPages(invocation, document);
    }
    else if (action == QLatin1String("get_outline"))
    {
        const int maximumDepth = int(reader.getInteger(QLatin1StringView("maxDepth"), 1, 256, false).value_or(8));
        const int maximumItems = int(reader.getInteger(QLatin1StringView("maxItems"), 1, 5000, false).value_or(500));
        Helper::finishRead(invocation, document, PDFAppMcpDocumentReader::getOutline(PDFAppMcpDocumentHeader::create(document), pdfDocument.data(), maximumDepth, maximumItems), true);
    }
    else if (action == QLatin1String("get_annotations"))
    {
        Helper::getAnnotations(invocation, document);
    }
    else if (action == QLatin1String("get_form_fields"))
    {
        QStringList names;
        for (const QJsonValue& name : reader.getArray(QLatin1StringView("names"), 500, false).value_or(QJsonArray()))
        {
            names << name.toString();
        }
        const bool includeValues = reader.getBool(QLatin1StringView("includeValues"), false).value_or(true);
        Helper::finishRead(invocation, document, PDFAppMcpDocumentReader::getFormFields(PDFAppMcpDocumentHeader::create(document), pdfDocument.data(), names, includeValues), true);
    }
    else if (action == QLatin1String("get_attachments"))
    {
        PDFMcpContentBuilder builder;
        const QByteArray object = PDFAppMcpDocumentReader::getAttachments(document);
        const QByteArray structured = PDFAppMcpReadSupport::createStructuredContent(action, object);
        builder.addText(PDFAppMcpToolSet::getUntrustedContentLine() + QLatin1Char('\n') + QString::fromUtf8(structured));

        // A resource link of every attachment (resources/read of …/attachments/{index})
        std::shared_ptr<const std::vector<PDFAppAttachment>> attachments = PDFAppAttachments::getAttachments(document);
        if (attachments)
        {
            for (size_t i = 0; i < attachments->size() && i < 1000; ++i)
            {
                const QString uri = PDFAppMcpDocumentResources::getDocumentPrefix(document->getId()) + QStringLiteral("attachments/%1").arg(i);
                builder.addResourceLink(uri, PDFAppMcpReadSupport::sanitizeName(attachments->at(i).name, 256), QString(), QString(), attachments->at(i).size);
            }
        }
        builder.setStructuredContent(structured);
        host->recordActivity(invocation.call, invocation.getActionId(), document, QJsonObject(), PDFMcpActivityStatus::Ok);
        invocation.call->finish(builder.createCallResult(false));
    }
    else if (action == QLatin1String("read_attachment"))
    {
        Helper::readAttachment(invocation, document);
    }
    else if (action == QLatin1String("get_signatures"))
    {
        Helper::getSignatures(invocation, document);
    }
    else if (action == QLatin1String("get_selection"))
    {
        PDFAppDocumentTab* tab = host->getResolver().findTab(document);
        Helper::finishRead(invocation, document, PDFAppMcpDocumentReader::getSelection(document, tab ? tab->getView() : nullptr), true);
    }
    else if (action == QLatin1String("get_layers"))
    {
        Helper::finishRead(invocation, document, PDFAppMcpDocumentReader::getLayers(document), true);
    }
    else if (action == QLatin1String("get_bookmarks"))
    {
        Helper::finishRead(invocation, document, PDFAppMcpDocumentReader::getBookmarks(host->getContext(), document), false);
    }
    else
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Unknown action %1.").arg(invocation.getActionId()));
    }
}

PDFAppMcpDocumentJob::PDFAppMcpDocumentJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpDocumentJobKind kind, PDFAppMcpDocumentHeader header) :
    PDFAppMcpJob(std::move(call), std::move(snapshot)),
    m_kind(kind),
    m_header(std::move(header))
{

}

void PDFAppMcpDocumentJob::setAttachment(qint64 index, QString name, PDFFileSpecification specification, qint64 maximumBytes)
{
    m_attachmentIndex = index;
    m_attachmentName = std::move(name);
    m_specification = std::move(specification);
    m_maximumBytes = maximumBytes;
}

bool PDFAppMcpDocumentJob::decodeText(const QByteArray& data, QString* text)
{
    if (data.contains('\0'))
    {
        return false;
    }

    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString decoded = decoder.decode(data);
    if (decoder.hasError())
    {
        return false;
    }

    // A byte order mark is not a part of the text
    if (decoded.startsWith(QChar(0xFEFF)))
    {
        decoded.remove(0, 1);
    }
    *text = std::move(decoded);
    return true;
}

void PDFAppMcpDocumentJob::execute()
{
    if (!getCall() || !getSnapshot().isValid())
    {
        return;
    }

    switch (m_kind)
    {
        case PDFAppMcpDocumentJobKind::Annotations:
            executeAnnotations();
            break;
        case PDFAppMcpDocumentJobKind::Attachment:
            executeAttachment();
            break;
    }
}

void PDFAppMcpDocumentJob::executeAnnotations()
{
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    const QByteArray object = PDFAppMcpDocumentReader::getAnnotations(m_header, getSnapshot().document.data(), m_query);
    if (call->isCanceled())
    {
        return;
    }

    if (!m_resourceUri.isEmpty())
    {
        call->finish(PDFAppMcpDocumentResources::createTextResult(m_resourceUri, QStringLiteral("application/json"), QString::fromUtf8(object)));
        return;
    }
    call->finish(PDFAppMcpReadSupport::createResult(QStringLiteral("get_annotations"), object, true));
}

void PDFAppMcpDocumentJob::executeAttachment()
{
    using Support = PDFAppMcpReadSupport;

    const std::shared_ptr<PDFMcpCall>& call = getCall();

    // Decoded only - nothing is opened, run or written (O18)
    QString error;
    QByteArray data = PDFAppAttachments::decode(getSnapshot().document, m_specification, &error);
    if (call->isCanceled())
    {
        return;
    }
    if (data.isNull() && !error.isEmpty())
    {
        m_isFailed = true;
        if (!m_resourceUri.isEmpty())
        {
            call->finish(PDFAppMcpDocumentResources::createError(*call, QStringLiteral("The attachment cannot be read: %1").arg(Support::sanitizeName(error, 256))));
            return;
        }
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The attachment cannot be read: %1").arg(Support::sanitizeName(error, 256))));
        return;
    }

    const qint64 size = data.size();
    const bool isTruncated = size > m_maximumBytes;
    if (isTruncated)
    {
        data.truncate(qsizetype(m_maximumBytes));

        // A cut UTF-8 sequence at the end is dropped (up to 3 bytes)
        for (int i = 0; i < 3 && !data.isEmpty() && (uchar(data.back()) & 0xC0) == 0x80; ++i)
        {
            data.chop(1);
        }
        if (!data.isEmpty() && (uchar(data.back()) & 0xC0) == 0xC0)
        {
            data.chop(1);
        }
    }

    const PDFEmbeddedFile* file = m_specification.getPlatformFile();
    QString mimeType = file ? Support::sanitizeName(QString::fromLatin1(file->getSubtype()), 128) : QString();
    QString text;
    const bool isText = decodeText(data, &text);
    if (mimeType.isEmpty())
    {
        mimeType = isText ? QStringLiteral("text/plain") : QStringLiteral("application/octet-stream");
    }

    const QString uri = PDFAppMcpDocumentResources::getDocumentPrefix(m_header.documentId) + QStringLiteral("attachments/%1").arg(m_attachmentIndex);
    if (!m_resourceUri.isEmpty())
    {
        call->finish(isText ? PDFAppMcpDocumentResources::createTextResult(m_resourceUri, mimeType, Support::sanitizeContent(text, -1))
                            : PDFAppMcpDocumentResources::createBlobResult(m_resourceUri, mimeType, data));
        return;
    }

    PDFMcpJsonWriter writer;
    writer.beginObject();
    m_header.write(writer);
    writer.writeMember(QLatin1StringView("index"), m_attachmentIndex);
    writer.writeMember(QLatin1StringView("name"), Support::sanitizeName(m_attachmentName, 256));
    writer.writeMember(QLatin1StringView("mimeType"), mimeType);
    writer.writeMember(QLatin1StringView("size"), size);
    writer.writeMember(QLatin1StringView("truncated"), isTruncated);
    writer.writeMember(QLatin1StringView("encoding"), isText ? QLatin1StringView("text") : QLatin1StringView("blob"));
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    const QByteArray structured = Support::createStructuredContent(QStringLiteral("read_attachment"), writer.take());

    // The readable text block shows a text attachment; the exact content is the embedded resource
    PDFMcpContentBuilder builder;
    builder.reserve(data.size() * 2 + structured.size() + 1024);
    // The name and the type are data of the document: never a part of a format string
    const QString details = QStringLiteral(" bytes%1).").arg(isTruncated ? QStringLiteral(", truncated to %1 bytes").arg(data.size()) : QString());
    const QString summary = PDFAppMcpToolSet::getUntrustedContentLine() + QStringLiteral("\nAttachment %1 “").arg(m_attachmentIndex) +
                            Support::sanitizeName(m_attachmentName, 256) + QStringLiteral("” (") + mimeType + QStringLiteral(", ") + QString::number(size) + details;
    if (isText)
    {
        builder.addText(summary + QStringLiteral(" Its text:\n") + Support::sanitizeContent(text, -1));
        builder.addEmbeddedText(uri, mimeType, Support::sanitizeContent(text, -1));
    }
    else
    {
        builder.addText(summary + QStringLiteral(" It is binary data, returned as an embedded blob."));
        builder.addEmbeddedBlob(uri, mimeType, data);
    }
    builder.setStructuredContent(structured);
    call->finish(builder.createCallResult(false));
}

void PDFAppMcpDocumentJob::finish(PDFAppMcpHost* host)
{
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!host || !call)
    {
        return;
    }

    if (call->isCanceled() && !call->isFinished())
    {
        call->finish(PDFAppMcpJobRunnable::createFailure(*call, QStringLiteral("The request was canceled.")));
    }

    const std::optional<PDFMcpCallResult> result = call->getResult();
    const bool isError = m_isFailed || !result || result->kind == PDFMcpCallResult::Kind::Error || result->isError;
    const QString action = m_kind == PDFAppMcpDocumentJobKind::Annotations ? QStringLiteral("document.get_annotations") : QStringLiteral("document.read_attachment");
    PDFAppDocument* document = host->getResolver().resolve(m_header.documentId, false).document;
    const std::set<int> pages = m_kind == PDFAppMcpDocumentJobKind::Annotations ? PDFAppMcpReadSupport::getActivityPages(m_query.pageIndices) : std::set<int>();
    host->recordActivity(call, action, document, QJsonObject(), PDFAppMcpReadSupport::getActivityStatus(*call, isError), pages);
}

PDFAppMcpSignatureWaiter::PDFAppMcpSignatureWaiter(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, PDFAppDocument* document, PDFAppDocumentSignatureVerifier* verifier) :
    BaseClass(host),
    m_host(host),
    m_call(std::move(call)),
    m_document(document),
    m_verifier(verifier)
{
    if (m_verifier)
    {
        connect(m_verifier, &PDFAppDocumentSignatureVerifier::verificationChanged, this, &PDFAppMcpSignatureWaiter::onVerificationChanged);
        connect(m_verifier, &QObject::destroyed, this, &PDFAppMcpSignatureWaiter::onTimeout);
    }

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(PDFAppMcpDocumentTool::SIGNATURE_WAIT_TIME);
    connect(m_timer, &QTimer::timeout, this, &PDFAppMcpSignatureWaiter::onTimeout);
    m_timer->start();
}

PDFAppMcpSignatureWaiter::~PDFAppMcpSignatureWaiter()
{
    if (!m_isDone && m_call && !m_call->isFinished())
    {
        m_call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
    }
}

void PDFAppMcpSignatureWaiter::setWaitTime(int milliseconds)
{
    if (m_timer)
    {
        m_timer->start(milliseconds);
    }
}

void PDFAppMcpSignatureWaiter::onVerificationChanged()
{
    if (!m_verifier || !m_verifier->isVerifying())
    {
        answer();
    }
}

void PDFAppMcpSignatureWaiter::onTimeout()
{
    answer();
}

void PDFAppMcpSignatureWaiter::answer()
{
    if (m_isDone)
    {
        return;
    }
    m_isDone = true;

    if (m_call && !m_call->isFinished() && !m_call->isCanceled())
    {
        if (!m_document || !m_host)
        {
            m_call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The document was closed in PDF4QT.")));
        }
        else
        {
            const bool isVerifying = m_verifier && m_verifier->isVerifying();
            m_host->recordActivity(m_call, QStringLiteral("document.get_signatures"), m_document, QJsonObject(), PDFMcpActivityStatus::Ok);
            m_call->finish(PDFAppMcpReadSupport::createResult(QStringLiteral("get_signatures"), PDFAppMcpDocumentReader::getSignatures(m_document, isVerifying), true));
        }
    }
    deleteLater();
}

}   // namespace pdf
