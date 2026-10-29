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


#include "pdfappmcptool_application.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcprenderer.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfapptab.h"

#include "pdfdocument.h"
#include "pdfmcpjson.h"
#include "pdfmcpserver.h"
#include "pdfsecurityhandler.h"

#include <QDir>
#include <QFileInfo>

namespace pdf
{

/// The state after the read approval of the documents
class PDFAppMcpStateRead : public PDFAppMcpDeferredRead
{
public:
    explicit PDFAppMcpStateRead(bool isResource) : m_isResource(isResource) { }

    virtual void proceed(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call) override
    {
        PDFAppMcpApplicationTool::finishState(host, call, m_isResource, true, QString());
    }

    virtual void refuse(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision) override
    {
        // The state itself needs no permission: it is given without the documents
        PDFAppMcpApplicationTool::finishState(host, call, m_isResource, false, PDFAppMcpReadSupport::getReadRefusedText(decision));
    }

private:
    bool m_isResource = false;
};

/// Helpers of the tool "application"
class PDFAppMcpApplicationToolHelper
{
public:
    PDFAppMcpApplicationToolHelper() = delete;

    static QString getStateName(const PDFAppDocument* document)
    {
        switch (document->getState())
        {
            case PDFAppDocumentState::Loading:
                return QStringLiteral("loading");
            case PDFAppDocumentState::PasswordRequired:
                return QStringLiteral("password_required");
            case PDFAppDocumentState::Failed:
                return QStringLiteral("failed");
            case PDFAppDocumentState::Ready:
                break;
        }
        return QStringLiteral("ready");
    }

    /// The form of the document: SigFlags (signatures exist) and its fields
    static void getFormFlags(const PDFDocument* document, bool* isSigned, bool* hasForm)
    {
        *isSigned = false;
        *hasForm = false;
        const PDFDictionary* form = document ? document->getDictionaryFromObject(document->getCatalog()->getFormObject()) : nullptr;
        if (!form)
        {
            return;
        }

        PDFDocumentDataLoaderDecorator loader(document);
        *isSigned = (loader.readIntegerFromDictionary(form, "SigFlags", 0) & 1) != 0;
        const PDFObject& fields = document->getObject(form->get("Fields"));
        *hasForm = (fields.isArray() && fields.getArray()->getCount() > 0) || !document->getObject(form->get("XFA")).isNull();
    }

    /// The route without the data of the documents: no query (the search text of
    /// the user, the page) and no document id - the state without the read
    /// permission names no document (review RC7-H)
    static QString getRouteWithoutDocuments(const QString& route)
    {
        QString path = route.section(QLatin1Char('?'), 0, 0).section(QLatin1Char('#'), 0, 0);
        const QString documentPrefix = QStringLiteral("/document/");
        if (path.startsWith(documentPrefix))
        {
            path = documentPrefix.chopped(1);
        }
        return path;
    }
};

QByteArray PDFAppMcpApplicationTool::createState(PDFAppMcpHost* host, const PDFMcpClientContext& client, bool includeDocuments, const QString& note)
{
    using Helper = PDFAppMcpApplicationToolHelper;
    using Support = PDFAppMcpReadSupport;

    const PDFAppMcpPermissions& permissions = host->getPermissions();
    const PDFAppMcpDocumentResolver& resolver = host->getResolver();
    const PDFAppDocument* activeDocument = resolver.getActiveDocument();
    PDFAppMainWindow* window = resolver.getWindow();

    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("version"), QString::fromLatin1(PDFMcpServer::getDefaultVersion()));
    writer.writeMember(QLatin1StringView("era"), client.protocolVersion);
    writer.key(QLatin1StringView("permissions"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("read"), PDFAppMcpPermissions::getName(permissions.read));
    writer.writeMember(QLatin1StringView("edit"), PDFAppMcpPermissions::getName(permissions.edit));
    writer.writeMember(QLatin1StringView("files"), PDFAppMcpPermissions::getName(permissions.files));
    writer.endObject();
    writer.writeMember(QLatin1StringView("readApproved"), permissions.read == PDFAppMcpPermission::Allow || host->getPolicy().isReadApproved(client.assistantKey));
    if (activeDocument && includeDocuments)
    {
        writer.writeMember(QLatin1StringView("activeDocumentId"), QStringView(activeDocument->getId()));
    }

    QString activeRoute;
    const PDFAppTab* currentTab = window ? window->getCurrentTab() : nullptr;
    const PDFAppHistoryEntry* entry = currentTab ? currentTab->getHistory().getCurrentEntry() : nullptr;
    if (entry)
    {
        activeRoute = entry->route.toString();
    }
    if (!includeDocuments)
    {
        activeRoute = Helper::getRouteWithoutDocuments(activeRoute);
    }
    writer.writeMember(QLatin1StringView("activeRoute"), Support::sanitizeName(activeRoute, PDFAppMcpToolSet::MAXIMUM_ROUTE_LENGTH));

    if (includeDocuments)
    {
        const bool isPathShown = permissions.isAvailable(PDFAppMcpPermissionKind::Files);
        writer.key(QLatin1StringView("documents"));
        writer.beginArray();
        for (const PDFAppDocument* document : resolver.getDocuments())
        {
            if (!document)
            {
                continue;
            }

            const PDFDocumentPointer pdfDocument = document->getDocument();
            bool isSigned = false;
            bool hasForm = false;
            Helper::getFormFlags(pdfDocument.data(), &isSigned, &hasForm);
            const PDFSecurityHandler* handler = pdfDocument ? pdfDocument->getStorage().getSecurityHandler() : nullptr;
            const bool isEncrypted = document->isPasswordProtected() || (handler && handler->getMode() != EncryptionMode::None);
            PDFAppDocumentTab* tab = resolver.findTab(document);
            const PDFAppDocumentView* view = tab ? tab->getView() : nullptr;

            writer.beginObject();
            writer.writeMember(QLatin1StringView("documentId"), QStringView(document->getId()));
            writer.writeMember(QLatin1StringView("title"), Support::sanitizeName(document->getTitle(), 256));
            writer.writeMember(QLatin1StringView("fileName"), document->getFilePath().isEmpty() ? QString() : Support::sanitizeName(QFileInfo(document->getFilePath()).fileName(), 256));
            if (isPathShown && !document->getFilePath().isEmpty())
            {
                writer.writeMember(QLatin1StringView("path"), Support::sanitizeName(QDir::toNativeSeparators(document->getFilePath()), PDFAppMcpToolSet::MAXIMUM_PATH_LENGTH));
            }
            writer.writeMember(QLatin1StringView("pageCount"), qint64(document->isReady() ? document->getPageCount() : 0));
            writer.writeMember(QLatin1StringView("currentPage"), qint64(view && document->isReady() ? view->getDisplayedPageIndex() + 1 : 0));
            writer.writeMember(QLatin1StringView("isActive"), document == activeDocument);
            writer.writeMember(QLatin1StringView("isLocked"), document->isLocked());
            writer.writeMember(QLatin1StringView("isModified"), document->isModified());
            writer.writeMember(QLatin1StringView("isSigned"), isSigned);
            writer.writeMember(QLatin1StringView("hasForm"), hasForm);
            writer.writeMember(QLatin1StringView("isEncrypted"), isEncrypted);
            writer.writeMember(QLatin1StringView("state"), Helper::getStateName(document));
            writer.endObject();
        }
        writer.endArray();
    }

    writer.key(QLatin1StringView("limits"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("maxRenderDpi"), PDFAppMcpToolSet::MAXIMUM_DPI);
    writer.writeMember(QLatin1StringView("maxRenderMegapixels"), PDFAppMcpToolSet::MAXIMUM_MEGAPIXELS);
    writer.writeMember(QLatin1StringView("maxTextPages"), PDFAppMcpToolSet::MAXIMUM_TEXT_PAGES);
    writer.writeMember(QLatin1StringView("maxTextCharacters"), PDFAppMcpToolSet::MAXIMUM_TEXT_CHARACTERS);
    writer.writeMember(QLatin1StringView("maxSearchResults"), PDFAppMcpToolSet::MAXIMUM_SEARCH_RESULTS);
    writer.writeMember(QLatin1StringView("maxAnnotationItems"), PDFAppMcpToolSet::MAXIMUM_ANNOTATION_ITEMS);
    writer.writeMember(QLatin1StringView("maxAttachmentBytes"), PDFAppMcpToolSet::MAXIMUM_ATTACHMENT_BYTES);
    writer.writeMember(QLatin1StringView("confirmationTimeoutSeconds"), 120);
    writer.endObject();
    writer.writeMember(QLatin1StringView("docs"), QLatin1StringView("pdf4qt://docs/index.md"));
    if (!note.isEmpty())
    {
        writer.writeMember(QLatin1StringView("note"), QStringView(note));
    }
    writer.endObject();
    return writer.take();
}

void PDFAppMcpApplicationTool::finishState(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, bool isResource, bool includeDocuments, const QString& note)
{
    if (!host || !call || call->isFinished())
    {
        return;
    }

    const QByteArray state = createState(host, call->getRequest().client, includeDocuments, note);
    if (isResource)
    {
        call->finish(PDFAppMcpDocumentResources::createTextResult(call->getRequest().uri, QStringLiteral("application/json"), QString::fromUtf8(state)));
        return;
    }
    call->finish(PDFAppMcpReadSupport::createResult(QStringLiteral("get_state"), state, includeDocuments));
}

void PDFAppMcpApplicationTool::readState(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, bool isResource)
{
    if (!host || !call)
    {
        return;
    }

    switch (PDFAppMcpReadSupport::checkRead(host, call, QStringLiteral("application.get_state"), nullptr, std::make_shared<PDFAppMcpStateRead>(isResource)))
    {
        case PDFAppMcpReadAccess::Allowed:
            finishState(host, call, isResource, true, QString());
            break;
        case PDFAppMcpReadAccess::Denied:
            finishState(host, call, isResource, false,
                        QStringLiteral("The documents are not listed: Read documents is set to Deny in PDF4QT. Ask the user to change it on the AI page."));
            break;
        case PDFAppMcpReadAccess::Asked:
            break;
    }
}

void PDFAppMcpApplicationTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    if (invocation.actionName != QLatin1String("get_state"))
    {
        invocation.host->finishWithError(invocation.call, invocation.getActionId(), nullptr, QStringLiteral("Unknown action %1.").arg(invocation.getActionId()));
        return;
    }
    readState(invocation.host, invocation.call, false);
}

}   // namespace pdf
