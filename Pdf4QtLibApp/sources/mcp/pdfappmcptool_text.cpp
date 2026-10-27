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


#include "pdfappmcptool_text.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcptextservice.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"

#include "pdfdocument.h"
#include "pdfmcpjson.h"

namespace pdf
{

void PDFAppMcpTextTool::handle(const PDFAppMcpToolInvocation& invocation)
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
    const PDFInteger pageCount = document->getPageCount();
    const QString& action = invocation.actionName;
    PDFMcpJsonReader reader(invocation.actionArguments, action);

    PDFAppMcpTextRequest request;
    request.actionId = invocation.getActionId();
    request.header = PDFAppMcpDocumentHeader::create(document);

    QString error;
    if (action == QLatin1String("get_text"))
    {
        request.kind = PDFAppMcpTextJobKind::GetText;
        const QString pagesText = reader.getString(QLatin1StringView("pages"), 4096, false).value_or(QString());
        const QString pageLabel = reader.getString(QLatin1StringView("pageLabel"), 256, false, PDFMcpJson::SanitizeFlag::None).value_or(QString());
        if (!pagesText.isEmpty() && !pageLabel.isEmpty())
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Pass either pages or pageLabel, not both."));
            return;
        }

        if (!pageLabel.isEmpty())
        {
            PDFInteger pageIndex = -1;
            switch (PDFAppMcpReadSupport::resolvePage(invocation.call, document, std::nullopt, pageLabel, -1, &pageIndex, &error))
            {
                case PDFAppMcpPageResolution::Resolved:
                    request.pageIndices.push_back(pageIndex);
                    break;
                case PDFAppMcpPageResolution::InputRequested:
                    return;
                case PDFAppMcpPageResolution::Error:
                    host->finishWithError(invocation.call, invocation.getActionId(), document, error);
                    return;
            }
        }
        else if (pagesText.trimmed().isEmpty())
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document,
                                  QStringLiteral("Pass pages (such as \"1-3\", at most %1 pages per call) or pageLabel. The document has %2 pages.")
                                      .arg(PDFAppMcpToolSet::MAXIMUM_TEXT_PAGES).arg(pageCount));
            return;
        }
        else
        {
            std::optional<std::vector<PDFInteger>> pages = PDFAppMcpReadSupport::parsePages(pagesText, pageCount, PDFAppMcpToolSet::MAXIMUM_TEXT_PAGES, &error);
            if (!pages)
            {
                host->finishWithError(invocation.call, invocation.getActionId(), document, error);
                return;
            }
            request.pageIndices = std::move(*pages);
        }

        const QString geometry = reader.getString(QLatin1StringView("geometry"), 16, false).value_or(QStringLiteral("none"));
        request.geometry = geometry == QLatin1String("lines") ? PDFAppMcpTextGeometry::Lines :
                           geometry == QLatin1String("words") ? PDFAppMcpTextGeometry::Words : PDFAppMcpTextGeometry::None;
        request.maximumCharacters = reader.getInteger(QLatin1StringView("maxCharacters"), 1000, PDFAppMcpToolSet::MAXIMUM_TEXT_CHARACTERS, false).value_or(PDFAppMcpToolSet::DEFAULT_TEXT_CHARACTERS);
    }
    else if (action == QLatin1String("search"))
    {
        request.kind = PDFAppMcpTextJobKind::Search;
        request.query = reader.getString(QLatin1StringView("query"), PDFAppMcpToolSet::MAXIMUM_QUERY_LENGTH, true, PDFMcpJson::SanitizeFlag::None).value_or(QString());
        if (request.query.isEmpty())
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("query: pass 1 to 1024 characters to find."));
            return;
        }

        request.searchOptions.caseSensitive = reader.getBool(QLatin1StringView("matchCase"), false).value_or(false);
        request.searchOptions.wholeWords = reader.getBool(QLatin1StringView("wholeWords"), false).value_or(false);
        request.searchOptions.regularExpression = reader.getBool(QLatin1StringView("regularExpression"), false).value_or(false);
        request.searchOptions.wildcards = reader.getBool(QLatin1StringView("wildcards"), false).value_or(false);
        if (request.searchOptions.regularExpression && request.searchOptions.wildcards)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Pass either regularExpression or wildcards, not both."));
            return;
        }

        const QString pagesText = reader.getString(QLatin1StringView("pages"), 4096, false).value_or(QString());
        if (!pagesText.trimmed().isEmpty())
        {
            std::optional<std::vector<PDFInteger>> pages = PDFAppMcpReadSupport::parsePages(pagesText, pageCount, 0, &error);
            if (!pages)
            {
                host->finishWithError(invocation.call, invocation.getActionId(), document, error);
                return;
            }
            request.pageIndices = std::move(*pages);
        }
        request.maximumResults = int(reader.getInteger(QLatin1StringView("maxResults"), 1, PDFAppMcpToolSet::MAXIMUM_SEARCH_RESULTS, false).value_or(PDFAppMcpToolSet::DEFAULT_SEARCH_RESULTS));
    }
    else if (action == QLatin1String("get_text_in_area"))
    {
        request.kind = PDFAppMcpTextJobKind::TextInArea;
        PDFInteger pageIndex = -1;
        const std::optional<qint64> page = reader.getInteger(QLatin1StringView("page"), 1, 1000000000, true);
        const std::optional<QRectF> rect = reader.getRect(QLatin1StringView("rect"), true);
        if (!page || !rect)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Invalid arguments: %1").arg(reader.getErrorText()));
            return;
        }
        if (PDFAppMcpReadSupport::resolvePage(invocation.call, document, page, QString(), -1, &pageIndex, &error) != PDFAppMcpPageResolution::Resolved)
        {
            host->finishWithError(invocation.call, invocation.getActionId(), document, error);
            return;
        }
        request.pageIndices.push_back(pageIndex);
        request.area = *rect;
    }
    else
    {
        host->finishWithError(invocation.call, invocation.getActionId(), document, QStringLiteral("Unknown action %1.").arg(invocation.getActionId()));
        return;
    }

    PDFAppMcpTextService::start(host, invocation.call, document, std::move(request));
}

}   // namespace pdf
