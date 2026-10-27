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


#include "pdfappmcpdocumentresources.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcprenderer.h"
#include "pdfappmcpresources.h"
#include "pdfappmcptextservice.h"
#include "pdfappmcptool_application.h"
#include "pdfappmcptool_document.h"
#include "pdfappmcptoolset.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentregistry.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpcontroller.h"

#include "pdfcomponentpanels.h"
#include "pdfdocument.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfmcpserver.h"
#include "pdfmcpuritemplate.h"

#include <QTabBar>
#include <QTimer>

namespace pdf
{

/// The read of a resource after its approval
class PDFAppMcpResourceRead : public PDFAppMcpDeferredRead
{
public:
    virtual void proceed(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call) override
    {
        PDFAppMcpDocumentResources::performRead(host, call);
    }

    virtual void refuse(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision) override
    {
        Q_UNUSED(host);
        call->finish(PDFAppMcpDocumentResources::createError(*call, PDFAppMcpReadSupport::getReadRefusedText(decision)));
    }
};

/// Helpers of the document resources
class PDFAppMcpDocumentResourcesHelper
{
public:
    PDFAppMcpDocumentResourcesHelper() = delete;

    struct TemplateInfo
    {
        PDFAppMcpResourceKind kind;
        const char* uriTemplate;
        const char* name;
        const char* title;
        const char* description;
        const char* mimeType;
        const char* actionId;
    };

    static const std::vector<TemplateInfo>& getTemplates()
    {
        static const std::vector<TemplateInfo> templates =
        {
            { PDFAppMcpResourceKind::Info, "pdf4qt://documents/{documentId}/info", "document-info", "Document information",
              "Metadata, page count, form, encryption and counts of an open document (as document.get_info)", "application/json", "document.get_info" },
            { PDFAppMcpResourceKind::Pages, "pdf4qt://documents/{documentId}/pages", "document-pages", "Pages",
              "Sizes, boxes, rotation and labels of the pages (at most 2000; then use document.get_pages)", "application/json", "document.get_pages" },
            { PDFAppMcpResourceKind::Outline, "pdf4qt://documents/{documentId}/outline", "document-outline", "Outline",
              "Outline items with their ids (at most 500; as document.get_outline)", "application/json", "document.get_outline" },
            { PDFAppMcpResourceKind::Text, "pdf4qt://documents/{documentId}/text", "document-text", "Text of the document",
              "Plain text of all pages (at most 4 MiB; then use text.get_text)", "text/plain", "text.get_text" },
            { PDFAppMcpResourceKind::PageText, "pdf4qt://documents/{documentId}/pages/{page}/text", "page-text", "Text of a page",
              "Plain text of one page (physical page number from 1)", "text/plain", "text.get_text" },
            { PDFAppMcpResourceKind::PageImage, "pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}", "page-image", "Image of a page",
              "PNG image of one page at the resolution (dpi 18 to 600, at most 50 megapixels)", "image/png", "render.page" },
            { PDFAppMcpResourceKind::Annotations, "pdf4qt://documents/{documentId}/annotations", "document-annotations", "Annotations",
              "Annotations and comments of all pages (at most 1000; as document.get_annotations)", "application/json", "document.get_annotations" },
            { PDFAppMcpResourceKind::PageAnnotations, "pdf4qt://documents/{documentId}/pages/{page}/annotations", "page-annotations", "Annotations of a page",
              "Annotations and comments of one page", "application/json", "document.get_annotations" },
            { PDFAppMcpResourceKind::Form, "pdf4qt://documents/{documentId}/form", "document-form", "Form fields",
              "Form fields with their names, types and values (as document.get_form_fields)", "application/json", "document.get_form_fields" },
            { PDFAppMcpResourceKind::Attachment, "pdf4qt://documents/{documentId}/attachments/{index}", "document-attachment", "Attachment",
              "Content of an embedded file (text or blob, at most 8 MiB; nothing is opened or run)", "application/octet-stream", "document.read_attachment" }
        };
        return templates;
    }

    static const TemplateInfo* findTemplate(PDFAppMcpResourceKind kind)
    {
        for (const TemplateInfo& info : getTemplates())
        {
            if (info.kind == kind)
            {
                return &info;
            }
        }
        return nullptr;
    }

    /// A page number from 1 of a URI variable ("12"), -1 for an invalid one
    static PDFInteger parsePage(const QString& text, PDFInteger pageCount)
    {
        if (text.isEmpty() || text.size() > 10 || text.startsWith(QLatin1Char('0')))
        {
            return -1;
        }
        for (const QChar c : text)
        {
            if (!c.isDigit() || c.unicode() > 127)
            {
                return -1;
            }
        }
        const qint64 page = text.toLongLong();
        return page >= 1 && page <= pageCount ? PDFInteger(page - 1) : -1;
    }

    /// A whole number of a URI variable in the range, -1 for an invalid one
    static qint64 parseNumber(const QString& text, qint64 minimum, qint64 maximum)
    {
        if (text.isEmpty() || text.size() > 10 || (text.size() > 1 && text.startsWith(QLatin1Char('0'))))
        {
            return -1;
        }
        for (const QChar c : text)
        {
            if (c.unicode() < u'0' || c.unicode() > u'9')
            {
                return -1;
            }
        }
        const qint64 value = text.toLongLong();
        return value >= minimum && value <= maximum ? value : -1;
    }

    static void recordRead(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpResourceKind kind, PDFAppDocument* document, const std::set<int>& pages = std::set<int>())
    {
        const TemplateInfo* info = findTemplate(kind);
        if (info)
        {
            host->recordActivity(call, QString::fromLatin1(info->actionId), document, QJsonObject(), PDFMcpActivityStatus::Ok, pages);
        }
    }
};

QString PDFAppMcpDocumentResources::getDocumentPrefix(const QString& documentId)
{
    return QStringLiteral("pdf4qt://documents/%1/").arg(documentId);
}

std::vector<PDFMcpResourceDefinition> PDFAppMcpDocumentResources::createListed(const PDFAppMcpHost* host)
{
    std::vector<PDFMcpResourceDefinition> resources;

    // The first catalog of a host starts the watcher of its documents (a child
    // object of the host; the list of the resources follows the documents)
    if (host && !host->isShutDown())
    {
        PDFAppMcpDocumentWatcher::ensure(const_cast<PDFAppMcpHost*>(host));
    }

    PDFMcpResourceDefinition state;
    state.uri = getStateUri();
    state.name = QStringLiteral("application-state");
    state.title = QStringLiteral("PDF4QT state");
    state.description = QStringLiteral("State of PDF4QT and the open documents (as application.get_state); subscribe to follow it");
    state.mimeType = QStringLiteral("application/json");
    resources.push_back(std::move(state));

    // The titles of the documents are listed only when Read documents is not denied
    if (!host || !host->getPermissions().isAvailable(PDFAppMcpPermissionKind::Read))
    {
        return resources;
    }

    for (const PDFAppDocument* document : host->getResolver().getDocuments())
    {
        if (!document || !document->isReady())
        {
            continue;
        }

        const QString title = PDFAppMcpReadSupport::sanitizeName(document->getTitle(), 128);
        const std::pair<const char*, const char*> parts[] = { { "info", "information" }, { "outline", "outline" }, { "annotations", "annotations" } };
        for (const auto& part : parts)
        {
            PDFMcpResourceDefinition resource;
            resource.uri = getDocumentPrefix(document->getId()) + QString::fromLatin1(part.first);
            resource.name = QStringLiteral("%1-%2").arg(QString::fromLatin1(part.first), document->getId());
            resource.title = title + QStringLiteral(" – ") + QString::fromLatin1(part.second);
            resource.description = QStringLiteral("The %1 of the open document (documentId %2)").arg(QString::fromLatin1(part.second), document->getId());
            resource.mimeType = QStringLiteral("application/json");
            resources.push_back(std::move(resource));
        }
    }
    return resources;
}

std::vector<PDFMcpResourceTemplateDefinition> PDFAppMcpDocumentResources::createTemplates()
{
    std::vector<PDFMcpResourceTemplateDefinition> templates;
    for (const PDFAppMcpDocumentResourcesHelper::TemplateInfo& info : PDFAppMcpDocumentResourcesHelper::getTemplates())
    {
        PDFMcpResourceTemplateDefinition definition;
        definition.uriTemplate = PDFMcpUriTemplate(QString::fromLatin1(info.uriTemplate));
        definition.name = QString::fromLatin1(info.name);
        definition.title = QString::fromLatin1(info.title);
        definition.description = QString::fromLatin1(info.description);
        definition.mimeType = QString::fromLatin1(info.mimeType);
        templates.push_back(std::move(definition));
    }
    return templates;
}

PDFAppMcpResourceUri PDFAppMcpDocumentResources::parseUri(const QString& uri)
{
    PDFAppMcpResourceUri result;
    if (uri == getStateUri())
    {
        result.kind = PDFAppMcpResourceKind::State;
        return result;
    }

    for (const PDFAppMcpDocumentResourcesHelper::TemplateInfo& info : PDFAppMcpDocumentResourcesHelper::getTemplates())
    {
        const std::optional<QMap<QString, QString>> values = PDFMcpUriTemplate(QString::fromLatin1(info.uriTemplate)).match(uri);
        if (values)
        {
            result.kind = info.kind;
            result.documentId = values->value(QStringLiteral("documentId"));
            result.page = values->value(QStringLiteral("page"));
            result.dpi = values->value(QStringLiteral("dpi"));
            result.index = values->value(QStringLiteral("index"));
            return result;
        }
    }
    return result;
}

PDFMcpCallResult PDFAppMcpDocumentResources::createTextResult(const QString& uri, const QString& mimeType, const QString& text)
{
    PDFMcpJsonWriter writer;
    writer.reserve(text.size() * 2 + uri.size() + 128);
    writer.beginObject();
    writer.key(QLatin1StringView("contents"));
    writer.beginArray();
    writer.beginObject();
    writer.writeMember(QLatin1StringView("uri"), QStringView(uri));
    writer.writeMember(QLatin1StringView("mimeType"), QStringView(mimeType));
    writer.writeMember(QLatin1StringView("text"), QStringView(text));
    writer.endObject();
    writer.endArray();
    writer.endObject();
    return PDFMcpCallResult::createComplete(writer.take());
}

PDFMcpCallResult PDFAppMcpDocumentResources::createBlobResult(const QString& uri, const QString& mimeType, QByteArrayView data)
{
    PDFMcpJsonWriter writer;
    writer.reserve(((data.size() + 2) / 3) * 4 + uri.size() + 128);
    writer.beginObject();
    writer.key(QLatin1StringView("contents"));
    writer.beginArray();
    writer.beginObject();
    writer.writeMember(QLatin1StringView("uri"), QStringView(uri));
    writer.writeMember(QLatin1StringView("mimeType"), QStringView(mimeType));
    writer.key(QLatin1StringView("blob"));
    writer.writeBase64(data);
    writer.endObject();
    writer.endArray();
    writer.endObject();
    return PDFMcpCallResult::createComplete(writer.take());
}

PDFMcpCallResult PDFAppMcpDocumentResources::createError(const PDFMcpCall& call, const QString& message)
{
    PDFMcpJsonWriter data;
    data.beginObject();
    data.writeMember(QLatin1StringView("uri"), PDFMcpJson::sanitizeString(call.getRequest().uri, PDFMcpJson::SanitizeFlag::RemoveBidi, 2048));
    data.endObject();
    return PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, message, data.take());
}

void PDFAppMcpDocumentResources::read(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call)
{
    if (!host || !call)
    {
        return;
    }

    const PDFAppMcpResourceUri uri = parseUri(call->getRequest().uri);
    if (!uri.isValid())
    {
        call->finish(PDFAppMcpResources::createNotFound(*call));
        return;
    }

    // The state lists the documents only with the read permission (as get_state)
    if (uri.kind == PDFAppMcpResourceKind::State)
    {
        PDFAppMcpApplicationTool::readState(host, call, true);
        return;
    }

    // Deny is answered before the document is looked up: an open document and
    // an unknown id give the same answer (no disclosure of the open documents)
    const PDFAppMcpDocumentResourcesHelper::TemplateInfo* info = PDFAppMcpDocumentResourcesHelper::findTemplate(uri.kind);
    const QString actionId = info ? QString::fromLatin1(info->actionId) : QStringLiteral("document.get_info");
    if (!PDFAppMcpReadSupport::isReadAvailable(host, call->getRequest().client.assistantKey))
    {
        host->finishDenied(call, actionId, nullptr, createError(*call, getDeniedText()));
        return;
    }

    // A document, which is not open, is a resource, which does not exist
    const PDFAppMcpResolvedDocument resolved = host->getResolver().resolve(uri.documentId, true);
    if (resolved.status == PDFAppMcpResolvedDocument::Status::InvalidId || resolved.status == PDFAppMcpResolvedDocument::Status::NotFound)
    {
        call->finish(PDFAppMcpResources::createNotFound(*call));
        return;
    }
    if (!resolved.isResolved())
    {
        call->finish(createError(*call, resolved.errorText));
        return;
    }

    switch (PDFAppMcpReadSupport::checkRead(host, call, actionId, resolved.document, std::make_shared<PDFAppMcpResourceRead>()))
    {
        case PDFAppMcpReadAccess::Allowed:
            performRead(host, call);
            break;
        case PDFAppMcpReadAccess::Denied:
            host->finishDenied(call, actionId, resolved.document, createError(*call, getDeniedText()));
            break;
        case PDFAppMcpReadAccess::Asked:
            break;
    }
}

void PDFAppMcpDocumentResources::performRead(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call)
{
    using Helper = PDFAppMcpDocumentResourcesHelper;

    if (!host || !call || call->isFinished())
    {
        return;
    }

    const QString& uriText = call->getRequest().uri;
    const PDFAppMcpResourceUri uri = parseUri(uriText);
    const PDFAppMcpResolvedDocument resolved = host->getResolver().resolve(uri.documentId, true);
    if (!resolved.isResolved())
    {
        call->finish(PDFAppMcpResources::createNotFound(*call));
        return;
    }

    PDFAppDocument* document = resolved.document;
    const PDFDocumentPointer pdfDocument = document->getDocument();
    const PDFInteger pageCount = PDFInteger(pdfDocument->getCatalog()->getPageCount());
    const PDFAppMcpDocumentHeader header = PDFAppMcpDocumentHeader::create(document);
    const QString json = QStringLiteral("application/json");

    switch (uri.kind)
    {
        case PDFAppMcpResourceKind::Info:
            Helper::recordRead(host, call, uri.kind, document);
            call->finish(createTextResult(uriText, json, QString::fromUtf8(PDFAppMcpDocumentReader::getInfo(document))));
            return;

        case PDFAppMcpResourceKind::Pages:
        {
            if (pageCount > MAXIMUM_RESOURCE_PAGES)
            {
                call->finish(createError(*call, QStringLiteral("The document has %1 pages; this resource lists at most %2. Use document.get_pages with page ranges.").arg(pageCount).arg(MAXIMUM_RESOURCE_PAGES)));
                return;
            }
            std::vector<PDFInteger> pages;
            for (PDFInteger i = 0; i < pageCount; ++i)
            {
                pages.push_back(i);
            }
            Helper::recordRead(host, call, uri.kind, document);
            call->finish(createTextResult(uriText, json, QString::fromUtf8(PDFAppMcpDocumentReader::getPages(header, pdfDocument.data(), pages, std::nullopt))));
            return;
        }

        case PDFAppMcpResourceKind::Outline:
            Helper::recordRead(host, call, uri.kind, document);
            call->finish(createTextResult(uriText, json, QString::fromUtf8(PDFAppMcpDocumentReader::getOutline(header, pdfDocument.data(), 8, 500))));
            return;

        case PDFAppMcpResourceKind::Form:
            Helper::recordRead(host, call, uri.kind, document);
            call->finish(createTextResult(uriText, json, QString::fromUtf8(PDFAppMcpDocumentReader::getFormFields(header, pdfDocument.data(), QStringList(), true))));
            return;

        case PDFAppMcpResourceKind::Annotations:
        case PDFAppMcpResourceKind::PageAnnotations:
        {
            PDFAppMcpAnnotationQuery query;
            query.maximumResults = MAXIMUM_RESOURCE_ANNOTATIONS;
            if (uri.kind == PDFAppMcpResourceKind::PageAnnotations)
            {
                const PDFInteger pageIndex = Helper::parsePage(uri.page, pageCount);
                if (pageIndex < 0)
                {
                    call->finish(PDFAppMcpResources::createNotFound(*call));
                    return;
                }
                query.pageIndices.push_back(pageIndex);
            }
            else
            {
                for (PDFInteger i = 0; i < pageCount; ++i)
                {
                    query.pageIndices.push_back(i);
                }
            }

            std::shared_ptr<PDFAppMcpDocumentJob> job = std::make_shared<PDFAppMcpDocumentJob>(call, document->getSnapshot(), PDFAppMcpDocumentJobKind::Annotations, header);
            job->setAnnotationQuery(std::move(query));
            job->setResource(uriText);
            host->startJob(std::move(job));
            return;
        }

        case PDFAppMcpResourceKind::Text:
        case PDFAppMcpResourceKind::PageText:
        {
            PDFAppMcpTextRequest request;
            request.kind = PDFAppMcpTextJobKind::GetText;
            request.actionId = QStringLiteral("text.get_text");
            request.header = header;
            request.isResource = true;
            request.resourceUri = uriText;
            request.maximumCharacters = PDFAppMcpTextService::MAXIMUM_RESOURCE_CHARACTERS;
            if (uri.kind == PDFAppMcpResourceKind::PageText)
            {
                const PDFInteger pageIndex = Helper::parsePage(uri.page, pageCount);
                if (pageIndex < 0)
                {
                    call->finish(PDFAppMcpResources::createNotFound(*call));
                    return;
                }
                request.pageIndices.push_back(pageIndex);
            }
            else
            {
                for (PDFInteger i = 0; i < pageCount; ++i)
                {
                    request.pageIndices.push_back(i);
                }
            }
            PDFAppMcpTextService::start(host, call, document, std::move(request));
            return;
        }

        case PDFAppMcpResourceKind::PageImage:
        {
            const PDFInteger pageIndex = Helper::parsePage(uri.page, pageCount);
            const qint64 dpi = Helper::parseNumber(uri.dpi, PDFAppMcpToolSet::MINIMUM_DPI, PDFAppMcpToolSet::MAXIMUM_DPI);
            if (pageIndex < 0 || dpi < 0)
            {
                call->finish(PDFAppMcpResources::createNotFound(*call));
                return;
            }

            PDFAppMcpRenderRequest request;
            request.kind = PDFAppMcpRenderKind::Page;
            request.actionId = QStringLiteral("render.page");
            request.header = header;
            request.pageIndex = pageIndex;
            request.dpi = int(dpi);
            request.isResource = true;
            request.resourceUri = uriText;
            host->startJob(std::make_shared<PDFAppMcpRenderJob>(call, document->getSnapshot(), std::move(request)));
            return;
        }

        case PDFAppMcpResourceKind::Attachment:
        {
            std::shared_ptr<const std::vector<PDFAppAttachment>> attachments = PDFAppAttachments::getAttachments(document);
            const qint64 index = Helper::parseNumber(uri.index, 0, 1000000);
            if (!attachments || index < 0 || index >= qint64(attachments->size()))
            {
                call->finish(PDFAppMcpResources::createNotFound(*call));
                return;
            }

            const PDFAppAttachment& attachment = attachments->at(size_t(index));
            std::shared_ptr<PDFAppMcpDocumentJob> job = std::make_shared<PDFAppMcpDocumentJob>(call, document->getSnapshot(), PDFAppMcpDocumentJobKind::Attachment, header);
            job->setAttachment(index, attachment.name, attachment.specification, PDFAppMcpToolSet::MAXIMUM_ATTACHMENT_BYTES);
            job->setResource(uriText);
            host->startJob(std::move(job));
            return;
        }

        case PDFAppMcpResourceKind::State:
        case PDFAppMcpResourceKind::Invalid:
            break;
    }

    call->finish(PDFAppMcpResources::createNotFound(*call));
}

PDFAppMcpDocumentWatcher::PDFAppMcpDocumentWatcher(PDFAppMcpHost* host) :
    BaseClass(host),
    m_host(host)
{
    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(50);
    connect(m_rebuildTimer, &QTimer::timeout, this, &PDFAppMcpDocumentWatcher::onRebuildTimer);

    PDFAppContext* context = host ? host->getContext() : nullptr;
    PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr;
    if (registry)
    {
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppMcpDocumentWatcher::onDocumentAdded);
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppMcpDocumentWatcher::onDocumentRemoved);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppMcpDocumentWatcher::onRegistryDocumentChanged);
        for (PDFAppDocument* document : registry->getDocuments())
        {
            connectDocument(document);
        }
    }
    connectWindow();

    for (const PDFMcpResourceDefinition& resource : PDFAppMcpDocumentResources::createListed(host))
    {
        m_listedKeys.insert(resource.uri + QLatin1Char('\n') + resource.title);
    }
}

PDFAppMcpDocumentWatcher::~PDFAppMcpDocumentWatcher() = default;

PDFAppMcpDocumentWatcher* PDFAppMcpDocumentWatcher::ensure(PDFAppMcpHost* host)
{
    if (!host)
    {
        return nullptr;
    }

    PDFAppMcpDocumentWatcher* watcher = find(host);
    return watcher ? watcher : new PDFAppMcpDocumentWatcher(host);
}

PDFAppMcpDocumentWatcher* PDFAppMcpDocumentWatcher::find(const PDFAppMcpHost* host)
{
    return host ? host->findChild<PDFAppMcpDocumentWatcher*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

void PDFAppMcpDocumentWatcher::connectDocument(PDFAppDocument* document)
{
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppMcpDocumentWatcher::onDocumentContentChanged, Qt::UniqueConnection);
    }
}

void PDFAppMcpDocumentWatcher::connectWindow()
{
    if (m_tabBar || !m_host)
    {
        return;
    }

    PDFAppMainWindow* window = m_host->getResolver().getWindow();
    QTabBar* tabBar = window ? window->getTabBar() : nullptr;
    if (tabBar)
    {
        m_tabBar = tabBar;
        connect(tabBar, &QTabBar::currentChanged, this, &PDFAppMcpDocumentWatcher::onCurrentTabChanged);
    }
}

void PDFAppMcpDocumentWatcher::onDocumentAdded(PDFAppDocument* document)
{
    connectDocument(document);
    connectWindow();
    scheduleRebuild();
    notifyUpdated(PDFAppMcpDocumentResources::getStateUri());
}

void PDFAppMcpDocumentWatcher::onDocumentRemoved(const QString& id)
{
    // Its subscribed resources end (updated) and the list changes
    notifyUpdated(PDFAppMcpDocumentResources::getDocumentPrefix(id));
    notifyUpdated(PDFAppMcpDocumentResources::getStateUri());
    scheduleRebuild();
}

void PDFAppMcpDocumentWatcher::onRegistryDocumentChanged(PDFAppDocument* document)
{
    // The state, the lock, the file or the title has changed
    connectWindow();
    scheduleRebuild();
    notifyUpdated(PDFAppMcpDocumentResources::getStateUri());
    if (document)
    {
        notifyUpdated(PDFAppMcpDocumentResources::getDocumentPrefix(document->getId()));
    }
}

void PDFAppMcpDocumentWatcher::onDocumentContentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);

    // A new version (commit, undo, redo, reload) updates every resource of the document
    PDFAppDocument* document = qobject_cast<PDFAppDocument*>(sender());
    if (document)
    {
        notifyUpdated(PDFAppMcpDocumentResources::getDocumentPrefix(document->getId()));
        notifyUpdated(PDFAppMcpDocumentResources::getStateUri());
    }
}

void PDFAppMcpDocumentWatcher::onCurrentTabChanged(int index)
{
    Q_UNUSED(index);
    notifyUpdated(PDFAppMcpDocumentResources::getStateUri());
}

QString PDFAppMcpDocumentResources::getDeniedText()
{
    return QStringLiteral("The user does not allow reading documents in PDF4QT (Read documents is set to Deny on the AI page).");
}

bool PDFAppMcpDocumentWatcher::isRebuildPending() const
{
    return m_rebuildTimer && m_rebuildTimer->isActive();
}

void PDFAppMcpDocumentWatcher::scheduleRebuild()
{
    if (m_rebuildTimer)
    {
        m_rebuildTimer->start();
    }
}

void PDFAppMcpDocumentWatcher::onRebuildTimer()
{
    if (!m_host || m_host->isShutDown())
    {
        return;
    }

    // A new catalog only when the listed resources differ (list_changed)
    QSet<QString> keys;
    for (const PDFMcpResourceDefinition& resource : PDFAppMcpDocumentResources::createListed(m_host))
    {
        keys.insert(resource.uri + QLatin1Char('\n') + resource.title);
    }
    if (keys == m_listedKeys)
    {
        return;
    }

    m_listedKeys = keys;
    ++m_rebuildCount;
    m_host->rebuildCatalog();
}

void PDFAppMcpDocumentWatcher::notifyUpdated(const QString& uri)
{
    PDFAppContext* context = m_host ? m_host->getContext() : nullptr;
    PDFAppMcpController* controller = context ? context->getMcpController() : nullptr;
    PDFMcpServer* server = controller ? controller->getServer() : nullptr;
    if (server)
    {
        server->notifyResourceUpdated(uri);
    }
}

}   // namespace pdf
