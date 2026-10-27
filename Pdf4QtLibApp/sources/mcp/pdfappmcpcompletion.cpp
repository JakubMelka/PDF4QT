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


#include "pdfappmcpcompletion.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpcontent.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"

#include "pdfdocument.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfmcplimits.h"

#include <QDateTime>
#include <QSet>

namespace pdf
{

/// Helpers of the completion
class PDFAppMcpCompletionHelper
{
public:
    PDFAppMcpCompletionHelper() = delete;

    /// The document of a completion context ("document" of a prompt, or
    /// "documentId" of a template - an id or a title), else the active document
    static PDFAppDocument* findContextDocument(PDFAppMcpHost* host, const QJsonObject& context)
    {
        const PDFAppMcpDocumentResolver& resolver = host->getResolver();
        const QJsonObject arguments = context.value(QStringLiteral("arguments")).toObject();
        QString key = arguments.value(QStringLiteral("documentId")).toString();
        if (key.isEmpty())
        {
            key = arguments.value(QStringLiteral("document")).toString();
        }
        key = key.left(1024);

        if (!key.isEmpty())
        {
            if (PDFAppMcpDocumentResolver::isValidDocumentId(key))
            {
                const PDFAppMcpResolvedDocument resolved = resolver.resolve(key, true);
                if (resolved.isResolved())
                {
                    return resolved.document;
                }
            }
            for (PDFAppDocument* document : resolver.getDocuments())
            {
                if (document && document->isReady() && document->getTitle().compare(key, Qt::CaseInsensitive) == 0)
                {
                    return document;
                }
            }
            return nullptr;
        }

        const PDFAppMcpResolvedDocument resolved = resolver.resolve(QString(), true);
        return resolved.isResolved() ? resolved.document : nullptr;
    }

    /// Page numbers starting with the prefix (in their order, at most \p limit)
    static QStringList getPageNumbers(PDFInteger pageCount, const QString& prefix, int limit)
    {
        QStringList values;
        for (PDFInteger i = 1; i <= pageCount && values.size() < limit; ++i)
        {
            const QString number = QString::number(i);
            if (number.startsWith(prefix))
            {
                values << number;
            }
        }
        return values;
    }
};

PDFAppMcpCompletionLimiter::PDFAppMcpCompletionLimiter(QObject* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("mcpCompletionLimiter"));
}

PDFAppMcpCompletionLimiter::~PDFAppMcpCompletionLimiter() = default;

PDFAppMcpCompletionLimiter* PDFAppMcpCompletionLimiter::get(PDFAppMcpHost* host)
{
    if (!host)
    {
        return nullptr;
    }

    PDFAppMcpCompletionLimiter* limiter = host->findChild<PDFAppMcpCompletionLimiter*>(QStringLiteral("mcpCompletionLimiter"), Qt::FindDirectChildrenOnly);
    if (!limiter)
    {
        limiter = new PDFAppMcpCompletionLimiter(host);
    }
    return limiter;
}

qint64 PDFAppMcpCompletionLimiter::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

bool PDFAppMcpCompletionLimiter::allow(const QString& assistantKey, qint64 now, qint64* retryAfter)
{
    // Requests of the last second (bounded number of assistants)
    if (m_requests.size() > 1024)
    {
        m_requests.clear();
    }

    std::deque<qint64>& requests = m_requests[assistantKey];
    while (!requests.empty() && (requests.front() <= now - 1000 || requests.front() > now))
    {
        requests.pop_front();
    }

    if (int(requests.size()) >= PDFMcpLimits::MAXIMUM_COMPLETIONS_PER_SECOND)
    {
        *retryAfter = qMax<qint64>(1, requests.front() + 1000 - now);
        return false;
    }

    requests.push_back(now);
    return true;
}

PDFMcpCallResult PDFAppMcpCompletion::createResult(const QStringList& values, int total, bool hasMore)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.key(QLatin1StringView("completion"));
    writer.beginObject();
    writer.key(QLatin1StringView("values"));
    writer.writeStringArray(values.mid(0, MAXIMUM_VALUES));
    writer.writeMember(QLatin1StringView("total"), total);
    writer.writeMember(QLatin1StringView("hasMore"), hasMore);
    writer.endObject();
    writer.endObject();
    return PDFMcpCallResult::createComplete(writer.take());
}

QStringList PDFAppMcpCompletion::getPromptValues(PDFAppMcpHost* host, const PDFMcpCallRequest& request, const QString& promptName,
                                                 const QString& argumentName, const QJsonObject& context)
{
    using Helper = PDFAppMcpCompletionHelper;

    QStringList values;
    const bool isReadAllowed = PDFAppMcpReadSupport::isReadAllowed(host, request.client.assistantKey);
    if (argumentName == QLatin1String("document"))
    {
        // Titles of the documents only with the read permission ("Prevent
        // completion-based information disclosure")
        if (isReadAllowed)
        {
            for (const PDFAppDocument* document : host->getResolver().getDocuments())
            {
                if (document && document->isReady())
                {
                    values << PDFAppMcpReadSupport::sanitizeName(document->getTitle(), 256);
                }
            }
        }
        return values;
    }

    if (argumentName == QLatin1String("pages"))
    {
        PDFAppDocument* document = isReadAllowed ? Helper::findContextDocument(host, context) : nullptr;
        if (document)
        {
            const PDFDocumentPointer pdfDocument = document->getDocument();
            const PDFInteger pageCount = document->getPageCount();
            values << Helper::getPageNumbers(pageCount, QString(), 1000);

            // The labels once each: a set, not a search of the list (the GUI thread
            // answers up to 20 completions per second - review RC7-H)
            QSet<QString> seen(values.cbegin(), values.cend());
            for (PDFInteger i = 0; i < pageCount && i < 10000 && values.size() < 2000; ++i)
            {
                const QString label = PDFAppMcpReadSupport::sanitizeName(PDFAppMcpArguments::getPageLabel(pdfDocument.data(), i), 64);
                if (!seen.contains(label))
                {
                    seen.insert(label);
                    values << label;
                }
            }
        }
        return values;
    }

    return PDFAppMcpContent::getPromptArgumentValues(promptName, argumentName);
}

QStringList PDFAppMcpCompletion::getTemplateValues(PDFAppMcpHost* host, const PDFMcpCallRequest& request, const QString& uriTemplate,
                                                   const QString& variable, const QString& prefix, const QJsonObject& context)
{
    using Helper = PDFAppMcpCompletionHelper;

    QStringList values;
    const bool isReadAllowed = PDFAppMcpReadSupport::isReadAllowed(host, request.client.assistantKey);
    const bool isDocumentTemplate = uriTemplate.startsWith(QLatin1String("pdf4qt://documents/"));

    if (variable == QLatin1String("documentId"))
    {
        if (isReadAllowed)
        {
            for (const PDFAppDocument* document : host->getResolver().getDocuments())
            {
                if (document && document->isReady())
                {
                    values << document->getId();
                }
            }
        }
    }
    else if (variable == QLatin1String("page") && isDocumentTemplate)
    {
        PDFAppDocument* document = isReadAllowed ? Helper::findContextDocument(host, context) : nullptr;
        if (document)
        {
            values = Helper::getPageNumbers(document->getPageCount(), prefix, MAXIMUM_VALUES + 1);
        }
    }
    else if (variable == QLatin1String("dpi"))
    {
        values = QStringList({ QStringLiteral("72"), QStringLiteral("96"), QStringLiteral("150"), QStringLiteral("200"), QStringLiteral("300"), QStringLiteral("600") });
    }
    else if (variable == QLatin1String("index") && isDocumentTemplate)
    {
        PDFAppDocument* document = isReadAllowed ? Helper::findContextDocument(host, context) : nullptr;
        std::shared_ptr<const std::vector<PDFAppAttachment>> attachments = document ? PDFAppAttachments::getAttachments(document) : nullptr;
        for (size_t i = 0; attachments && i < attachments->size() && i < 1000; ++i)
        {
            values << QString::number(i);
        }
    }
    else if (variable == QLatin1String("topic") || variable == QLatin1String("tool"))
    {
        // Names of the public files of the server (no document data)
        const QString uriPrefix = variable == QLatin1String("topic") ? QStringLiteral("pdf4qt://help/") : QStringLiteral("pdf4qt://docs/tools/");
        for (const PDFMcpResourceDefinition& resource : PDFAppMcpContent::getStaticResources())
        {
            if (resource.uri.startsWith(uriPrefix) && resource.uri.endsWith(QLatin1String(".md")))
            {
                values << resource.uri.mid(uriPrefix.size(), resource.uri.size() - uriPrefix.size() - 3);
            }
        }
    }
    return values;
}

void PDFAppMcpCompletion::complete(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call)
{
    if (!host || !call)
    {
        return;
    }

    const PDFMcpCallRequest& request = call->getRequest();

    // At most 20 requests per second of an assistant
    PDFAppMcpCompletionLimiter* limiter = PDFAppMcpCompletionLimiter::get(host);
    qint64 retryAfter = 0;
    if (limiter && !limiter->allow(request.client.assistantKey, limiter->getCurrentTime(), &retryAfter))
    {
        PDFMcpJsonWriter data;
        data.beginObject();
        data.writeMember(QLatin1StringView("retryAfterMs"), retryAfter);
        data.endObject();
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::RATE_LIMITED, QStringLiteral("Too many completion requests; retry later."), data.take()));
        return;
    }

    PDFMcpJsonReader reader(request.arguments, QStringLiteral("params"));
    PDFMcpJsonReader reference = reader.child(QLatin1StringView("ref"), true);
    PDFMcpJsonReader argument = reader.child(QLatin1StringView("argument"), true);
    const QString type = reference.getString(QLatin1StringView("type"), 64, true).value_or(QString());
    const QString name = argument.getString(QLatin1StringView("name"), 256, true).value_or(QString());
    const QString value = argument.getString(QLatin1StringView("value"), 4096, false).value_or(QString());
    const QJsonObject context = request.arguments.value(QStringLiteral("context")).toObject();
    if (reader.hasErrors() || name.isEmpty())
    {
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Invalid completion request: %1").arg(reader.getErrorText())));
        return;
    }

    QStringList candidates;
    if (type == QLatin1String("ref/prompt"))
    {
        const QString promptName = reference.getString(QLatin1StringView("name"), 256, true).value_or(QString());
        std::shared_ptr<const PDFMcpCatalog> catalog = host->getCatalog();
        if (!catalog || !catalog->findPrompt(promptName))
        {
            call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown prompt: %1").arg(PDFAppMcpReadSupport::sanitizeName(promptName, 64))));
            return;
        }
        candidates = getPromptValues(host, request, promptName, name, context);
    }
    else if (type == QLatin1String("ref/resource"))
    {
        const QString uri = reference.getString(QLatin1StringView("uri"), 2048, true).value_or(QString());
        candidates = getTemplateValues(host, request, uri, name, value, context);
    }
    else
    {
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown reference type of the completion")));
        return;
    }

    QStringList values;
    int total = 0;
    for (const QString& candidate : candidates)
    {
        if (candidate.startsWith(value, Qt::CaseInsensitive))
        {
            ++total;
            if (values.size() < MAXIMUM_VALUES)
            {
                values << candidate;
            }
        }
    }
    call->finish(createResult(values, total, total > values.size()));
}

}   // namespace pdf
