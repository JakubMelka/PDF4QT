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


#include "pdfappmcppromptruntime.h"
#include "pdfappmcpcontent.h"
#include "pdfappmcpelicitation.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappdocument.h"

#include "pdfmcpcatalog.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"

namespace pdf
{

/// The prompt after the read approval of the document
class PDFAppMcpPromptRead : public PDFAppMcpDeferredRead
{
public:
    virtual void proceed(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call) override
    {
        PDFAppMcpPromptRuntime::finishPrompt(host, call, true);
    }

    virtual void refuse(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision) override
    {
        // The prompt is given without naming the document
        Q_UNUSED(decision);
        PDFAppMcpPromptRuntime::finishPrompt(host, call, false);
    }
};

/// Helpers of prompts/get
class PDFAppMcpPromptRuntimeHelper
{
public:
    PDFAppMcpPromptRuntimeHelper() = delete;

    static PDFMcpCallResult createError(const QString& message)
    {
        return PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, message);
    }

    static bool hasArgument(const PDFMcpPromptDefinition* definition, const QString& name)
    {
        for (const PDFMcpPromptArgument& argument : definition->arguments)
        {
            if (argument.name == name)
            {
                return true;
            }
        }
        return false;
    }
};

PDFMcpCallResult PDFAppMcpPromptRuntime::createResult(const QString& description, const QString& text, const QString& workflowUri, const QString& workflowText)
{
    PDFMcpJsonWriter writer;
    writer.reserve((text.size() + workflowText.size()) * 2 + 1024);
    writer.beginObject();
    if (!description.isEmpty())
    {
        writer.writeMember(QLatin1StringView("description"), QStringView(description));
    }
    writer.key(QLatin1StringView("messages"));
    writer.beginArray();

    writer.beginObject();
    writer.writeMember(QLatin1StringView("role"), QLatin1StringView("user"));
    writer.key(QLatin1StringView("content"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("type"), QLatin1StringView("text"));
    writer.writeMember(QLatin1StringView("text"), QStringView(text));
    writer.endObject();
    writer.endObject();

    if (!workflowUri.isEmpty() && !workflowText.isEmpty())
    {
        writer.beginObject();
        writer.writeMember(QLatin1StringView("role"), QLatin1StringView("user"));
        writer.key(QLatin1StringView("content"));
        writer.beginObject();
        writer.writeMember(QLatin1StringView("type"), QLatin1StringView("resource"));
        writer.key(QLatin1StringView("resource"));
        writer.beginObject();
        writer.writeMember(QLatin1StringView("uri"), QStringView(workflowUri));
        writer.writeMember(QLatin1StringView("mimeType"), QLatin1StringView("text/markdown"));
        writer.writeMember(QLatin1StringView("text"), QStringView(workflowText));
        writer.endObject();
        writer.endObject();
        writer.endObject();
    }

    writer.endArray();
    writer.endObject();
    return PDFMcpCallResult::createComplete(writer.take());
}

void PDFAppMcpPromptRuntime::getPrompt(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call)
{
    using Helper = PDFAppMcpPromptRuntimeHelper;

    if (!host || !call)
    {
        return;
    }

    const PDFMcpCallRequest& request = call->getRequest();
    std::shared_ptr<const PDFMcpCatalog> catalog = host->getCatalog();
    const PDFMcpPromptDefinition* definition = catalog ? catalog->findPrompt(request.name) : nullptr;
    if (!definition)
    {
        call->finish(Helper::createError(QStringLiteral("Unknown prompt: %1").arg(PDFAppMcpReadSupport::sanitizeName(request.name, 64))));
        return;
    }

    // The arguments are strings of a bounded length; the required ones are given
    for (auto it = request.arguments.constBegin(); it != request.arguments.constEnd(); ++it)
    {
        if (!it.value().isString() || it.value().toString().size() > MAXIMUM_ARGUMENT_LENGTH)
        {
            call->finish(Helper::createError(QStringLiteral("Invalid argument %1: the arguments of a prompt are strings of at most %2 characters.")
                                                 .arg(PDFAppMcpReadSupport::sanitizeName(it.key(), 64), QString::number(MAXIMUM_ARGUMENT_LENGTH))));
            return;
        }
    }
    for (const PDFMcpPromptArgument& argument : definition->arguments)
    {
        if (argument.required && request.arguments.value(argument.name).toString().trimmed().isEmpty())
        {
            call->finish(Helper::createError(QStringLiteral("Missing required argument: %1").arg(argument.name)));
            return;
        }
    }

    // Naming a document needs the read permission (as the tools)
    if (!Helper::hasArgument(definition, QStringLiteral("document")) || host->getResolver().getDocuments().empty())
    {
        finishPrompt(host, call, false);
        return;
    }

    switch (PDFAppMcpReadSupport::checkRead(host, call, QStringLiteral("document.get_info"), nullptr, std::make_shared<PDFAppMcpPromptRead>()))
    {
        case PDFAppMcpReadAccess::Allowed:
            finishPrompt(host, call, true);
            break;
        case PDFAppMcpReadAccess::Denied:
            finishPrompt(host, call, false);
            break;
        case PDFAppMcpReadAccess::Asked:
            break;
    }
}

void PDFAppMcpPromptRuntime::finishPrompt(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, bool isDocumentAllowed)
{
    using Helper = PDFAppMcpPromptRuntimeHelper;

    if (!host || !call || call->isFinished())
    {
        return;
    }

    const PDFMcpCallRequest& request = call->getRequest();
    std::shared_ptr<const PDFMcpCatalog> catalog = host->getCatalog();
    const PDFMcpPromptDefinition* definition = catalog ? catalog->findPrompt(request.name) : nullptr;
    if (!definition)
    {
        call->finish(Helper::createError(QStringLiteral("Unknown prompt: %1").arg(PDFAppMcpReadSupport::sanitizeName(request.name, 64))));
        return;
    }

    QMap<QString, QString> arguments;
    for (const PDFMcpPromptArgument& argument : definition->arguments)
    {
        const QString value = request.arguments.value(argument.name).toString();
        if (!value.isEmpty())
        {
            arguments.insert(argument.name, value);
        }
    }

    if (isDocumentAllowed && Helper::hasArgument(definition, QStringLiteral("document")))
    {
        const PDFAppMcpDocumentResolver& resolver = host->getResolver();
        const QString documentText = arguments.value(QStringLiteral("document")).trimmed();
        if (documentText.isEmpty())
        {
            // The active document tab, the only document, or the choice of the client;
            // without a choice the workflow lets the model ask
            const PDFAppMcpResolvedDocument resolved = resolver.resolveForCall(call, QString(), false);
            if (resolved.status == PDFAppMcpResolvedDocument::Status::InputRequested)
            {
                return;
            }
            if (resolved.document && resolved.status != PDFAppMcpResolvedDocument::Status::Ambiguous)
            {
                arguments.insert(QStringLiteral("documentId"), resolved.document->getId());
            }
        }
        else
        {
            // An id, or a title of one or more open documents
            std::vector<QPointer<PDFAppDocument>> candidates;
            if (PDFAppMcpDocumentResolver::isValidDocumentId(documentText))
            {
                const PDFAppMcpResolvedDocument resolved = resolver.resolve(documentText, false);
                if (resolved.document)
                {
                    candidates.push_back(resolved.document);
                }
            }
            if (candidates.empty())
            {
                for (PDFAppDocument* document : resolver.getDocuments())
                {
                    if (document && document->getTitle().compare(documentText, Qt::CaseInsensitive) == 0)
                    {
                        candidates.push_back(document);
                    }
                }
            }

            if (candidates.empty())
            {
                call->finish(Helper::createError(QStringLiteral("document: no open document has the id or the title \"%1\". Call application.get_state for the open documents.")
                                                     .arg(PDFAppMcpReadSupport::sanitizeName(documentText, 128))));
                return;
            }

            QPointer<PDFAppDocument> chosen = candidates.size() == 1 ? candidates.front() : QPointer<PDFAppDocument>();
            if (!chosen)
            {
                // Several documents have the title: the choice of the client
                const PDFAppMcpElicitationAnswer answer = PDFAppMcpElicitation::getAnswer(request, PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"));
                if (answer.isAccepted())
                {
                    for (const QPointer<PDFAppDocument>& candidate : candidates)
                    {
                        if (candidate && candidate->getId() == answer.value.toString())
                        {
                            chosen = candidate;
                        }
                    }
                }

                if (!chosen)
                {
                    const bool isDeclined = answer.status == PDFAppMcpElicitationAnswer::Status::Declined || answer.status == PDFAppMcpElicitationAnswer::Status::Canceled;
                    if (!isDeclined && resolver.askForDocument(call, candidates, QStringLiteral("Several open documents have this title. Which document should the assistant use?")))
                    {
                        return;
                    }
                    call->finish(Helper::createError(resolver.createAmbiguousText(candidates)));
                    return;
                }
            }
            arguments.insert(QStringLiteral("documentId"), chosen->getId());
        }
    }

    const QString text = PDFAppMcpContent::createPromptText(request.name, arguments);
    call->finish(createResult(definition->description, text, PDFAppMcpContent::getWorkflowUri(request.name), PDFAppMcpContent::getWorkflowText(request.name)));
}

}   // namespace pdf
