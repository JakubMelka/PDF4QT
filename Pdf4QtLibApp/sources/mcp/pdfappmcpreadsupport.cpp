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


#include "pdfappmcpreadsupport.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpelicitation.h"
#include "pdfappmcphost.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"

#include "pdfdocument.h"
#include "pdfmcpcatalog.h"
#include "pdfmcpjson.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

PDFAppMcpDeferredRead::~PDFAppMcpDeferredRead() = default;

/// Receiver of the read approval of a deferred read: the continuation proceeds
/// after Allow and refuses otherwise
class PDFAppMcpDeferredReadReceiver : public PDFAppMcpConfirmationReceiver
{
public:
    PDFAppMcpDeferredReadReceiver(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, std::shared_ptr<PDFAppMcpDeferredRead> continuation) :
        m_host(host),
        m_call(std::move(call)),
        m_continuation(std::move(continuation))
    {

    }

    virtual void confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision) override
    {
        Q_UNUSED(confirmationId);

        if (!m_host || !m_call || m_call->isFinished() || !m_continuation)
        {
            return;
        }

        // The continuation is released with the receiver, so it cannot outlive
        // the host by a cycle
        std::shared_ptr<PDFAppMcpDeferredRead> continuation = std::move(m_continuation);

        // The permission may have been set to Deny while the question waited (as
        // PDFAppMcpHost::continueToolCall): the approval does not read then
        if (decision == PDFAppMcpDecision::Allowed && !PDFAppMcpReadSupport::isReadAvailable(m_host.data(), m_call->getRequest().client.assistantKey))
        {
            decision = PDFAppMcpDecision::DeniedUser;
        }

        if (decision == PDFAppMcpDecision::Allowed && !m_call->isCanceled())
        {
            continuation->proceed(m_host, m_call);
        }
        else
        {
            continuation->refuse(m_host, m_call, decision);
        }
    }

private:
    QPointer<PDFAppMcpHost> m_host;
    std::shared_ptr<PDFMcpCall> m_call;
    std::shared_ptr<PDFAppMcpDeferredRead> m_continuation;
};

void PDFAppMcpReadSupport::writeDocumentHeader(PDFMcpJsonWriter& writer, const PDFAppDocument* document)
{
    if (!document)
    {
        return;
    }

    writer.writeMember(QLatin1StringView("documentId"), QStringView(document->getId()));
    writer.writeMember(QLatin1StringView("title"), sanitizeName(document->getTitle(), 256));
}

double PDFAppMcpReadSupport::round(double value)
{
    if (!std::isfinite(value))
    {
        return 0.0;
    }

    const double rounded = std::round(value * 1000.0) / 1000.0;
    return rounded == 0.0 ? 0.0 : rounded;
}

void PDFAppMcpReadSupport::writeRect(PDFMcpJsonWriter& writer, QLatin1StringView key, const QRectF& rect)
{
    const QRectF normalized = rect.normalized();
    writer.key(key);
    writer.beginObject();
    writer.writeMember(QLatin1StringView("left"), round(normalized.left()));
    writer.writeMember(QLatin1StringView("bottom"), round(normalized.top()));
    writer.writeMember(QLatin1StringView("right"), round(normalized.right()));
    writer.writeMember(QLatin1StringView("top"), round(normalized.bottom()));
    writer.endObject();
}

void PDFAppMcpReadSupport::writeQuads(PDFMcpJsonWriter& writer, QLatin1StringView key, const QPolygonF& quadrilaterals)
{
    writer.key(key);
    writer.beginArray();
    for (qsizetype i = 0; i + 3 < quadrilaterals.size(); i += 4)
    {
        writer.beginArray();
        for (qsizetype j = i; j < i + 4; ++j)
        {
            writer.writeNumber(round(quadrilaterals[j].x()));
            writer.writeNumber(round(quadrilaterals[j].y()));
        }
        writer.endArray();
    }
    writer.endArray();
}

QString PDFAppMcpReadSupport::sanitizeContent(const QString& text, qsizetype maximumLength)
{
    return PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::KeepNewlines, maximumLength);
}

QString PDFAppMcpReadSupport::sanitizeName(const QString& text, qsizetype maximumLength)
{
    return PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlags(PDFMcpJson::SanitizeFlag::RemoveBidi) | PDFMcpJson::SanitizeFlag::Trim, maximumLength);
}

QString PDFAppMcpReadSupport::formatDateTime(const QDateTime& dateTime)
{
    if (!dateTime.isValid())
    {
        return QString();
    }
    return dateTime.toString(Qt::ISODate);
}

QByteArray PDFAppMcpReadSupport::createStructuredContent(const QString& actionName, const QByteArray& actionObject)
{
    PDFMcpJsonWriter writer;
    writer.reserve(actionObject.size() + actionName.size() * 2 + 32);
    writer.beginObject();
    writer.writeMember(QLatin1StringView("action"), QStringView(actionName));
    writer.key(actionName);
    writer.writeRaw(actionObject);
    writer.endObject();
    return writer.take();
}

PDFMcpCallResult PDFAppMcpReadSupport::createResult(const QString& actionName, const QByteArray& actionObject, bool hasDocumentContent, const QString& readableText)
{
    QByteArray structured = createStructuredContent(actionName, actionObject);

    QString text = readableText.isEmpty() ? QString::fromUtf8(structured) : readableText;
    if (hasDocumentContent)
    {
        text.prepend(PDFAppMcpToolSet::getUntrustedContentLine() + QLatin1Char('\n'));
    }

    PDFMcpContentBuilder builder;
    builder.reserve(structured.size() + text.size() * 2 + 64);
    builder.addText(text);
    builder.setStructuredContent(std::move(structured));
    return builder.createCallResult(false);
}

bool PDFAppMcpReadSupport::isReadAllowed(const PDFAppMcpHost* host, const QString& assistantKey)
{
    if (!host)
    {
        return false;
    }

    const bool hasOpenDocuments = !host->getResolver().getDocuments().empty();
    return host->getPolicy().evaluate(PDFAppMcpPermissionKind::Read, assistantKey, hasOpenDocuments) == PDFAppMcpPolicyResult::Allowed;
}

bool PDFAppMcpReadSupport::isReadAvailable(const PDFAppMcpHost* host, const QString& assistantKey)
{
    if (!host)
    {
        return false;
    }

    const bool hasOpenDocuments = !host->getResolver().getDocuments().empty();
    return host->getPolicy().evaluate(PDFAppMcpPermissionKind::Read, assistantKey, hasOpenDocuments) != PDFAppMcpPolicyResult::Denied;
}

PDFMcpActivityStatus PDFAppMcpReadSupport::getActivityStatus(const PDFMcpCall& call, bool isError)
{
    if (call.isCanceled())
    {
        return PDFMcpActivityStatus::Canceled;
    }
    return isError ? PDFMcpActivityStatus::Failed : PDFMcpActivityStatus::Ok;
}

PDFAppMcpReadAccess PDFAppMcpReadSupport::checkRead(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, const QString& actionId,
                                                    PDFAppDocument* document, std::shared_ptr<PDFAppMcpDeferredRead> continuation)
{
    if (!host || !call)
    {
        return PDFAppMcpReadAccess::Denied;
    }

    const QString& assistantKey = call->getRequest().client.assistantKey;
    const bool hasOpenDocuments = !host->getResolver().getDocuments().empty();
    switch (host->getPolicy().evaluate(PDFAppMcpPermissionKind::Read, assistantKey, hasOpenDocuments))
    {
        case PDFAppMcpPolicyResult::Allowed:
            return PDFAppMcpReadAccess::Allowed;
        case PDFAppMcpPolicyResult::Denied:
            return PDFAppMcpReadAccess::Denied;
        case PDFAppMcpPolicyResult::Ask:
            break;
    }

    std::shared_ptr<PDFAppMcpDeferredReadReceiver> receiver = std::make_shared<PDFAppMcpDeferredReadReceiver>(host, call, std::move(continuation));
    std::shared_ptr<PDFAppMcpConfirmation> confirmation = std::make_shared<PDFAppMcpConfirmation>();
    confirmation->kind = PDFAppMcpConfirmationKind::ReadDocuments;
    confirmation->permissions = PDFAppMcpPermissionKind::Read;
    confirmation->call = call;
    confirmation->document = document ? document : host->getResolver().getActiveDocument();
    confirmation->actionId = actionId;
    confirmation->receiver = receiver;
    confirmation->receiverHolder = receiver;
    host->requestConfirmation(confirmation);
    return PDFAppMcpReadAccess::Asked;
}

QString PDFAppMcpReadSupport::getReadRefusedText(PDFAppMcpDecision decision)
{
    switch (decision)
    {
        case PDFAppMcpDecision::DeniedTimeout:
            return QStringLiteral("The user did not answer the request to read the documents within 2 minutes. Ask the user before trying again.");
        case PDFAppMcpDecision::Unavailable:
            return QStringLiteral("PDF4QT cannot ask the user to approve reading the documents now. Ask the user to set Read documents to Allow on the AI page of PDF4QT.");
        case PDFAppMcpDecision::DeniedStopped:
            return QStringLiteral("The MCP server of PDF4QT is stopping.");
        case PDFAppMcpDecision::Canceled:
        case PDFAppMcpDecision::DeniedDisconnected:
            return QStringLiteral("The request was canceled.");
        default:
            break;
    }
    return QStringLiteral("The user denied reading the open documents in PDF4QT. Ask the user before trying again.");
}

std::optional<std::vector<PDFInteger>> PDFAppMcpReadSupport::parsePages(const QString& text, PDFInteger pageCount, int maximumPages, QString* errorMessage)
{
    QString error;
    std::optional<std::vector<PDFInteger>> pages = PDFAppMcpArguments::parsePages(text, pageCount, maximumPages, &error);
    if (!pages && errorMessage)
    {
        *errorMessage = QStringLiteral("pages: %1").arg(error);
    }
    return pages;
}

PDFAppMcpPageResolution PDFAppMcpReadSupport::resolvePage(const std::shared_ptr<PDFMcpCall>& call, const PDFAppDocument* document,
                                                          std::optional<qint64> page, const QString& pageLabel, PDFInteger defaultPageIndex,
                                                          PDFInteger* pageIndex, QString* errorMessage)
{
    const PDFDocument* pdfDocument = document ? document->getDocument().data() : nullptr;
    const PDFInteger pageCount = pdfDocument ? PDFInteger(pdfDocument->getCatalog()->getPageCount()) : 0;
    if (!pdfDocument || pageCount <= 0)
    {
        *errorMessage = QStringLiteral("The document has no pages.");
        return PDFAppMcpPageResolution::Error;
    }

    if (page && !pageLabel.isEmpty())
    {
        *errorMessage = QStringLiteral("Pass either page or pageLabel, not both.");
        return PDFAppMcpPageResolution::Error;
    }

    if (page)
    {
        if (*page < 1 || *page > pageCount)
        {
            *errorMessage = QStringLiteral("page: page %1 does not exist; the document has %2 pages (numbered from 1).").arg(*page).arg(pageCount);
            return PDFAppMcpPageResolution::Error;
        }
        *pageIndex = PDFInteger(*page - 1);
        return PDFAppMcpPageResolution::Resolved;
    }

    if (pageLabel.isEmpty())
    {
        if (defaultPageIndex >= 0 && defaultPageIndex < pageCount)
        {
            *pageIndex = defaultPageIndex;
            return PDFAppMcpPageResolution::Resolved;
        }
        *errorMessage = QStringLiteral("Pass page (a physical page number from 1) or pageLabel.");
        return PDFAppMcpPageResolution::Error;
    }

    const QString label = sanitizeName(pageLabel, 256);
    const std::vector<PDFInteger> matches = PDFAppMcpArguments::findPagesByLabel(pdfDocument, pageLabel);
    if (matches.empty())
    {
        *errorMessage = QStringLiteral("pageLabel: no page has the label \"%1\". The labels are listed by document.get_pages; or pass page (a physical page number from 1).").arg(label);
        return PDFAppMcpPageResolution::Error;
    }

    if (matches.size() == 1)
    {
        *pageIndex = matches.front();
        return PDFAppMcpPageResolution::Resolved;
    }

    // Several pages have the label: the page chosen in the client (plan 2.8)
    QStringList candidates;
    for (PDFInteger match : matches)
    {
        if (candidates.size() < 50)
        {
            candidates << QString::number(match + 1);
        }
    }
    const QString fallback = QStringLiteral("The label \"%1\" belongs to several pages (physical pages %2). Pass page instead of pageLabel.").arg(label, candidates.join(QStringLiteral(", ")));

    if (call)
    {
        const PDFMcpCallRequest& request = call->getRequest();
        const PDFAppMcpElicitationAnswer answer = PDFAppMcpElicitation::getAnswer(request, PDFAppMcpElicitation::getPageKey(), QStringLiteral("page"));
        switch (answer.status)
        {
            case PDFAppMcpElicitationAnswer::Status::Accepted:
            {
                bool ok = false;
                const PDFInteger chosen = answer.value.toString().toLongLong(&ok) - 1;
                if (ok && std::find(matches.cbegin(), matches.cend(), chosen) != matches.cend())
                {
                    *pageIndex = chosen;
                    return PDFAppMcpPageResolution::Resolved;
                }
                break;
            }

            case PDFAppMcpElicitationAnswer::Status::Declined:
            case PDFAppMcpElicitationAnswer::Status::Canceled:
                *errorMessage = PDFAppMcpElicitation::getDeclinedText(QStringLiteral("a page"), fallback);
                return PDFAppMcpPageResolution::Error;

            case PDFAppMcpElicitationAnswer::Status::None:
            case PDFAppMcpElicitationAnswer::Status::Invalid:
                break;
        }

        if (PDFAppMcpElicitation::isSupported(request) && matches.size() <= size_t(PDFAppMcpElicitation::MAXIMUM_OPTIONS))
        {
            std::vector<PDFAppMcpElicitationOption> options;
            for (PDFInteger match : matches)
            {
                PDFAppMcpElicitationOption option;
                option.value = QString::number(match + 1);
                option.title = QStringLiteral("Page %1 (label “%2”)").arg(match + 1).arg(label);
                options.push_back(std::move(option));
            }

            PDFMcpInputRequests requests = PDFAppMcpElicitation::createRequests(request);
            PDFAppMcpElicitation::addChoice(request, requests, PDFAppMcpElicitation::getPageKey(),
                                            QStringLiteral("Several pages of %1 have the label “%2”. Which page do you mean?").arg(sanitizeName(document->getTitle(), 128), label),
                                            QStringLiteral("page"), QStringLiteral("Page"), options);
            call->requestInput(std::move(requests));
            return PDFAppMcpPageResolution::InputRequested;
        }
    }

    *errorMessage = fallback;
    return PDFAppMcpPageResolution::Error;
}

std::set<int> PDFAppMcpReadSupport::getActivityPages(const std::vector<PDFInteger>& pageIndices)
{
    std::set<int> pages;
    for (PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex >= 0 && pageIndex < std::numeric_limits<int>::max())
        {
            pages.insert(int(pageIndex + 1));
        }
    }
    return pages;
}

}   // namespace pdf
