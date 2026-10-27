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


#include "pdfappmcptool_render.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcprenderer.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"

#include "pdfdocument.h"
#include "pdfmcpjson.h"

namespace pdf
{

void PDFAppMcpRenderTool::handle(const PDFAppMcpToolInvocation& invocation)
{
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
    const QString& action = invocation.actionName;
    PDFMcpJsonReader reader(invocation.actionArguments, action);

    PDFAppMcpRenderRequest request;
    request.actionId = invocation.getActionId();
    request.header = PDFAppMcpDocumentHeader::create(document);

    QString error;
    if (action == QLatin1String("page"))
    {
        // Without page and pageLabel the page shown in the tab is rendered
        PDFAppDocumentTab* tab = host->getResolver().findTab(document);
        const PDFInteger currentPage = tab && tab->getView() ? tab->getView()->getCurrentPageIndex() : 0;
        const std::optional<qint64> page = reader.getInteger(QLatin1StringView("page"), 1, 1000000000, false);
        const QString pageLabel = reader.getString(QLatin1StringView("pageLabel"), 256, false, PDFMcpJson::SanitizeFlag::None).value_or(QString());
        PDFInteger pageIndex = -1;
        switch (PDFAppMcpReadSupport::resolvePage(invocation.call, document, page, pageLabel, qMax<PDFInteger>(0, currentPage), &pageIndex, &error))
        {
            case PDFAppMcpPageResolution::Resolved:
                break;
            case PDFAppMcpPageResolution::InputRequested:
                return;
            case PDFAppMcpPageResolution::Error:
                host->finishWithError(invocation.call, invocation.getActionId(), document, error);
                return;
        }

        request.kind = PDFAppMcpRenderKind::Page;
        request.pageIndex = pageIndex;
        request.dpi = int(reader.getInteger(QLatin1StringView("dpi"), PDFAppMcpToolSet::MINIMUM_DPI, PDFAppMcpToolSet::MAXIMUM_DPI, false).value_or(PDFAppMcpToolSet::DEFAULT_DPI));
        if (reader.has(QLatin1StringView("area")))
        {
            const std::optional<QRectF> area = reader.getRect(QLatin1StringView("area"), true);
            if (!area || area->isEmpty())
            {
                host->finishWithError(invocation.call, invocation.getActionId(), document,
                                      QStringLiteral("area: pass a rectangle {left, bottom, right, top} of a non-zero size in PDF points."));
                return;
            }
            request.area = *area;
        }
        request.showAnnotations = reader.getString(QLatin1StringView("annotations"), 8, false).value_or(QStringLiteral("show")) != QLatin1String("hide");
        request.isJpeg = reader.getString(QLatin1StringView("format"), 8, false).value_or(QStringLiteral("png")) == QLatin1String("jpeg");
        request.jpegQuality = int(reader.getInteger(QLatin1StringView("jpegQuality"), 30, 95, false).value_or(85));

        // The limits are checked before a worker is used
        const PDFDocumentPointer pdfDocument = document->getDocument();
        if (!PDFAppMcpRenderer::getGeometry(pdfDocument->getCatalog()->getPage(size_t(pageIndex)), request.dpi, request.area, &error).isValid())
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, error);
            return;
        }
    }
    else if (action == QLatin1String("overview"))
    {
        const QString pagesText = reader.getString(QLatin1StringView("pages"), 4096, true).value_or(QString());
        if (pagesText.trimmed().isEmpty())
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("pages: pass at most 24 pages, such as \"1-12\"."));
            return;
        }

        std::optional<std::vector<PDFInteger>> pages = PDFAppMcpReadSupport::parsePages(pagesText, document->getPageCount(), PDFAppMcpToolSet::MAXIMUM_OVERVIEW_PAGES, &error);
        if (!pages)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, error);
            return;
        }

        request.kind = PDFAppMcpRenderKind::Overview;
        request.pageIndices = std::move(*pages);
        request.maximumCellSize = int(reader.getInteger(QLatin1StringView("maxSize"), 64, 256, false).value_or(192));
    }
    else
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Unknown action %1.").arg(invocation.getActionId()));
        return;
    }

    host->startJob(std::make_shared<PDFAppMcpRenderJob>(invocation.call, document->getSnapshot(), std::move(request)));
}

}   // namespace pdf
