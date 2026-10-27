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


#include "pdfappmcptool_view.h"
#include "pdfappmcpareahighlight.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcptoolset.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapproute.h"

#include "pdfaction.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfoutline.h"

namespace pdf
{

/// Helpers of the tool "view"
class PDFAppMcpViewToolHelper
{
public:
    PDFAppMcpViewToolHelper() = delete;

    static void finishView(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFMcpJsonWriter& writer)
    {
        // The view runs the code of the application (activation, navigation): a slot
        // may delete the host with its controller (UnitTestsAppMcpLifetime)
        if (!invocation.host)
        {
            invocation.call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
            return;
        }
        writer.endObject();
        invocation.host->recordActivity(invocation.call, invocation.getActionId(), document, QJsonObject(), PDFMcpActivityStatus::Ok);
        invocation.call->finish(PDFAppMcpReadSupport::createResult(invocation.actionName, writer.take(), false));
    }

    /// Switches to the tab of the document (the window is not raised)
    static void activate(PDFAppMcpHost* host, PDFAppDocument* document)
    {
        PDFAppMainWindow* window = host->getResolver().getWindow();
        PDFAppDocumentTab* tab = host->getResolver().findTab(document);
        if (window && tab && window->getCurrentTab() != tab)
        {
            window->activateTab(tab);
        }
    }

    /// A destination with its named destination resolved (nullopt = none)
    static std::optional<PDFDestination> resolveDestination(const PDFDocument* document, const PDFDestination& destination)
    {
        PDFDestination result = destination;
        if (result.getDestinationType() == DestinationType::Named)
        {
            const PDFDestination* target = document->getCatalog()->getNamedDestination(result.getName());
            if (!target)
            {
                return std::nullopt;
            }
            result = *target;
        }
        if (result.getDestinationType() == DestinationType::Invalid || result.getDestinationType() == DestinationType::Named)
        {
            return std::nullopt;
        }
        return result;
    }

    /// Page index of a resolved destination (-1 = none)
    static PDFInteger getDestinationPage(const PDFDocument* document, const PDFDestination& destination)
    {
        const PDFCatalog* catalog = document->getCatalog();
        if (destination.getPageReference().isValid())
        {
            const size_t index = catalog->getPageIndexFromPageReference(destination.getPageReference());
            return index == PDFCatalog::INVALID_PAGE_INDEX ? -1 : PDFInteger(index);
        }
        const PDFInteger pageIndex = destination.getPageIndex();
        return pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? pageIndex : -1;
    }

    static void goTo(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab);
    static void showArea(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab);
    static void showSearch(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab);
    static void navigate(const PDFAppMcpToolInvocation& invocation);
    static void close(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab);
};

void PDFAppMcpViewToolHelper::goTo(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab)
{
    PDFAppMcpHost* host = invocation.host;
    PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
    const std::optional<qint64> page = reader.getInteger(QLatin1StringView("page"), 1, 1000000000, false);
    const QString pageLabel = reader.getString(QLatin1StringView("pageLabel"), 256, false, PDFMcpJson::SanitizeFlag::None).value_or(QString());
    const QString outlineItem = reader.getString(QLatin1StringView("outlineItem"), 256, false).value_or(QString());
    const QString namedDestination = reader.getString(QLatin1StringView("namedDestination"), 1024, false, PDFMcpJson::SanitizeFlag::None).value_or(QString());
    const QString fit = reader.getString(QLatin1StringView("fit"), 16, false).value_or(QString());
    const std::optional<double> zoom = reader.getNumber(QLatin1StringView("zoom"), 10.0, 6400.0, false);
    const bool isActivated = reader.getBool(QLatin1StringView("activate"), false).value_or(true);

    const int targetCount = (page ? 1 : 0) + (pageLabel.isEmpty() ? 0 : 1) + (outlineItem.isEmpty() ? 0 : 1) + (namedDestination.isEmpty() ? 0 : 1);
    if (targetCount != 1)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Pass exactly one of page, pageLabel, outlineItem or namedDestination."));
        return;
    }
    if (!fit.isEmpty() && zoom)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Pass either fit or zoom, not both."));
        return;
    }

    const PDFDocumentPointer pdfDocument = document->getDocument();
    PDFInteger pageIndex = -1;
    std::optional<PDFDestination> destination;
    QString error;
    if (page || !pageLabel.isEmpty())
    {
        switch (PDFAppMcpReadSupport::resolvePage(invocation.call, document, page, pageLabel, -1, &pageIndex, &error))
        {
            case PDFAppMcpPageResolution::Resolved:
                break;
            case PDFAppMcpPageResolution::InputRequested:
                return;
            case PDFAppMcpPageResolution::Error:
                host->finishWithError(invocation.call, invocation.getActionId(), document, error);
                return;
        }
    }
    else if (!outlineItem.isEmpty())
    {
        const PDFOutlineItem* item = PDFAppMcpDocumentReader::findOutlineItem(pdfDocument.data(), outlineItem);
        const PDFAction* action = item ? item->getAction() : nullptr;
        if (action)
        {
            for (const PDFAction* current : action->getActionList())
            {
                if (current && current->getType() == ActionType::GoTo && !destination)
                {
                    destination = resolveDestination(pdfDocument.data(), static_cast<const PDFActionGoTo*>(current)->getDestination());
                }
            }
        }
        if (!item)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("outlineItem: no outline item has the id \"%1\". The ids are listed by document.get_outline.")
                                                                                          .arg(PDFAppMcpReadSupport::sanitizeName(outlineItem, 64)));
            return;
        }
        if (!destination)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("outlineItem: the outline item has no destination in the document."));
            return;
        }
        pageIndex = getDestinationPage(pdfDocument.data(), *destination);
    }
    else
    {
        PDFDestination named = PDFDestination::createNamed(namedDestination.toUtf8());
        destination = resolveDestination(pdfDocument.data(), named);
        if (!destination)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("namedDestination: the document has no destination of this name."));
            return;
        }
        pageIndex = getDestinationPage(pdfDocument.data(), *destination);
    }

    if (pageIndex < 0)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("The destination does not lead to a page of the document."));
        return;
    }

    // The view may run code of the application (tools, panels): guard the tab
    QPointer<PDFAppDocumentTab> tabGuard(tab);
    QPointer<PDFAppDocument> documentGuard(document);
    if (isActivated)
    {
        activate(host, document);
    }

    PDFAppDocumentView* view = tabGuard ? tabGuard->getView() : nullptr;
    if (view && destination && view->getProxy())
    {
        view->getProxy()->goToDestination(*destination);
    }
    else if (view)
    {
        view->goToPage(pageIndex);
    }

    view = tabGuard ? tabGuard->getView() : nullptr;
    if (view && !fit.isEmpty())
    {
        view->executeCommand(fit == QLatin1String("width") ? PDFAppCommands::fitWidth() : PDFAppCommands::fitPage());
    }
    else if (view && zoom)
    {
        view->setZoom(*zoom / 100.0);
    }

    if (!documentGuard)
    {
        invocation.call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The document was closed in PDF4QT.")));
        return;
    }

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(documentGuard).write(writer);
    writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
    writer.writeMember(QLatin1StringView("label"), PDFAppMcpReadSupport::sanitizeName(PDFAppMcpArguments::getPageLabel(documentGuard->getDocument().data(), pageIndex), 256));
    finishView(invocation, documentGuard, writer);
}

void PDFAppMcpViewToolHelper::showArea(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab)
{
    PDFAppMcpHost* host = invocation.host;
    PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
    const std::optional<qint64> page = reader.getInteger(QLatin1StringView("page"), 1, 1000000000, true);
    const std::optional<QRectF> rect = reader.getRect(QLatin1StringView("rect"), true);
    const bool isActivated = reader.getBool(QLatin1StringView("activate"), false).value_or(true);
    if (!page || !rect)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Invalid arguments: %1").arg(reader.getErrorText()));
        return;
    }

    PDFInteger pageIndex = -1;
    QString error;
    if (PDFAppMcpReadSupport::resolvePage(invocation.call, document, page, QString(), -1, &pageIndex, &error) != PDFAppMcpPageResolution::Resolved)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, error);
        return;
    }

    QPointer<PDFAppDocumentTab> tabGuard(tab);
    QPointer<PDFAppDocument> documentGuard(document);
    if (isActivated)
    {
        activate(host, document);
    }

    PDFAppDocumentView* view = tabGuard ? tabGuard->getView() : nullptr;
    if (view && view->getProxy())
    {
        view->getProxy()->goToPageAndEnsureVisible(pageIndex, rect->normalized());
    }

    view = tabGuard ? tabGuard->getView() : nullptr;
    if (view)
    {
        PDFAppMcpAreaHighlight::show(view, pageIndex, *rect);
    }

    if (!documentGuard)
    {
        invocation.call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The document was closed in PDF4QT.")));
        return;
    }

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(documentGuard).write(writer);
    writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
    writer.writeMember(QLatin1StringView("label"), PDFAppMcpReadSupport::sanitizeName(PDFAppMcpArguments::getPageLabel(documentGuard->getDocument().data(), pageIndex), 256));
    finishView(invocation, documentGuard, writer);
}

void PDFAppMcpViewToolHelper::showSearch(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab)
{
    PDFAppMcpHost* host = invocation.host;
    PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
    const QString query = reader.getString(QLatin1StringView("query"), PDFAppMcpToolSet::MAXIMUM_QUERY_LENGTH, true, PDFMcpJson::SanitizeFlag::RemoveBidi).value_or(QString()).simplified();
    if (query.isEmpty())
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("query: pass 1 to 1024 characters to find."));
        return;
    }

    PDFDocumentTextSearchOptions options;
    options.caseSensitive = reader.getBool(QLatin1StringView("matchCase"), false).value_or(false);
    options.wholeWords = reader.getBool(QLatin1StringView("wholeWords"), false).value_or(false);
    options.regularExpression = reader.getBool(QLatin1StringView("regularExpression"), false).value_or(false);
    options.wildcards = reader.getBool(QLatin1StringView("wildcards"), false).value_or(false);
    if (options.regularExpression && options.wildcards)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Pass either regularExpression or wildcards, not both."));
        return;
    }

    PDFAppDocumentSearchCommandHandler* handler = tab ? PDFAppDocumentSearchCommandHandler::findHandler(tab) : nullptr;
    PDFAppDocumentSearchController* controller = handler ? handler->getSearchController() : nullptr;
    if (!handler || !controller)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("The search is not available for this document."));
        return;
    }

    // The search panel of the tab shows the query and highlights the results
    QPointer<PDFAppDocument> documentGuard(document);
    QPointer<PDFAppDocumentSearchCommandHandler> handlerGuard(handler);
    activate(host, document);
    if (handlerGuard && handlerGuard->getSearchController())
    {
        handlerGuard->getSearchController()->setOptions(options);
    }
    if (handlerGuard)
    {
        handlerGuard->startSearch(query);
    }

    if (!documentGuard)
    {
        invocation.call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The document was closed in PDF4QT.")));
        return;
    }

    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(documentGuard).write(writer);
    writer.writeMember(QLatin1StringView("query"), PDFAppMcpReadSupport::sanitizeName(query));
    finishView(invocation, documentGuard, writer);
}

void PDFAppMcpViewToolHelper::navigate(const PDFAppMcpToolInvocation& invocation)
{
    PDFAppMcpHost* host = invocation.host;
    PDFMcpJsonReader reader(invocation.actionArguments, invocation.actionName);
    const QString text = reader.getString(QLatin1StringView("route"), PDFAppMcpToolSet::MAXIMUM_ROUTE_LENGTH, true, PDFMcpJson::SanitizeFlag::None).value_or(QString());

    // Only routes of the application: no address, no file, no scheme
    const PDFAppRoute route = PDFAppRoute::parse(text);
    if (text.isEmpty() || !text.startsWith(QLatin1Char('/')) || text.startsWith(QStringLiteral("//")) || !route.isValid())
    {
        host->finishWithError(invocation.call, invocation.getActionId(), nullptr,
                              QStringLiteral("route: pass a route of PDF4QT, such as /help/annotations or /tools (web addresses and files are refused)."));
        return;
    }

    PDFAppMainWindow* window = host->getResolver().getWindow();
    PDFAppNavigator* navigator = window ? window->getNavigator() : nullptr;
    if (!navigator)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), nullptr, QStringLiteral("PDF4QT has no window now."));
        return;
    }

    navigator->navigate(route, PDFAppNavigationMode::Push, window->getCurrentTab());

    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("route"), route.toString());
    finishView(invocation, nullptr, writer);
}

void PDFAppMcpViewToolHelper::close(const PDFAppMcpToolInvocation& invocation, PDFAppDocument* document, PDFAppDocumentTab* tab)
{
    PDFAppMcpHost* host = invocation.host;
    if (document->isModified() || document->hasPendingEdits())
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document,
                              QStringLiteral("The document has unsaved changes. Save it (files.save) or ask the user to close it."));
        return;
    }

    for (const PDFAppDocumentOperation* operation : document->getOperations())
    {
        if (operation && !operation->isFinished() && operation->getKind() != PDFAppDocumentOperationKind::Index && operation->getKind() != PDFAppDocumentOperationKind::Signatures &&
            operation->getKind() != PDFAppDocumentOperationKind::Fonts)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document,
                                  QStringLiteral("PDF4QT is working with the document (saving, printing or running a tool). Try again when it has finished."), PDFMcpActivityStatus::DeniedBusy);
            return;
        }
    }

    const QString& assistantKey = invocation.call->getRequest().client.assistantKey;
    for (const std::shared_ptr<PDFAppMcpConfirmation>& confirmation : host->getPendingConfirmations())
    {
        if (confirmation && confirmation->document == document && confirmation->assistantKey != assistantKey)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document,
                                  QStringLiteral("Another assistant waits for the confirmation of the user in this document. Try again later."), PDFMcpActivityStatus::DeniedBusy);
            return;
        }
    }

    PDFAppMainWindow* window = host->getResolver().getWindow();
    if (!window || !tab)
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("The document has no tab in PDF4QT."));
        return;
    }

    // The activity is written before the tab (and its document) goes
    PDFMcpJsonWriter writer;
    writer.beginObject();
    PDFAppMcpDocumentHeader::create(document).write(writer);
    host->recordActivity(invocation.call, invocation.getActionId(), document, QJsonObject(), PDFMcpActivityStatus::Ok);

    std::shared_ptr<PDFMcpCall> call = invocation.call;
    QPointer<PDFAppDocumentTab> tabGuard(tab);
    const bool isClosed = window->closeTab(tab);
    writer.writeMember(QLatin1StringView("closed"), isClosed || !tabGuard);
    writer.endObject();
    call->finish(PDFAppMcpReadSupport::createResult(QStringLiteral("close"), writer.take(), false));
}

void PDFAppMcpViewTool::handle(const PDFAppMcpToolInvocation& invocation)
{
    using Helper = PDFAppMcpViewToolHelper;

    PDFAppMcpHost* host = invocation.host;
    const QString& action = invocation.actionName;
    if (action == QLatin1String("navigate"))
    {
        Helper::navigate(invocation);
        return;
    }

    // activate and close name their document
    if ((action == QLatin1String("activate") || action == QLatin1String("close")) && invocation.documentId.isEmpty())
    {
        host->finishWithError(invocation.call, invocation.getActionId(), nullptr, QStringLiteral("Pass documentId (from application.get_state) for view.%1.").arg(action));
        return;
    }

    const PDFAppMcpResolvedDocument resolved = host->getResolver().resolveForCall(invocation.call, invocation.documentId, action != QLatin1String("close"));
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
    PDFAppDocumentTab* tab = host->getResolver().findTab(document);
    if (action == QLatin1String("activate"))
    {
        QPointer<PDFAppDocument> documentGuard(document);
        Helper::activate(host, document);
        if (!documentGuard)
        {
            invocation.call->finish(PDFMcpCallResult::createToolError(QStringLiteral("The document was closed in PDF4QT.")));
            return;
        }

        PDFMcpJsonWriter writer;
        writer.beginObject();
        PDFAppMcpDocumentHeader::create(documentGuard).write(writer);
        const PDFAppDocument* active = host->getResolver().getActiveDocument();
        if (active)
        {
            writer.writeMember(QLatin1StringView("activeDocumentId"), QStringView(active->getId()));
        }
        Helper::finishView(invocation, documentGuard, writer);
    }
    else if (action == QLatin1String("go_to"))
    {
        Helper::goTo(invocation, document, tab);
    }
    else if (action == QLatin1String("show_area"))
    {
        Helper::showArea(invocation, document, tab);
    }
    else if (action == QLatin1String("show_search"))
    {
        Helper::showSearch(invocation, document, tab);
    }
    else if (action == QLatin1String("close"))
    {
        Helper::close(invocation, document, tab);
    }
    else
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Unknown action %1.").arg(invocation.getActionId()));
    }
}

}   // namespace pdf
