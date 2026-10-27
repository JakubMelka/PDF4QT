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


#include "pdfappmcptextservice.h"
#include "pdfappmcpjob.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcphost.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumenttab.h"

#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfmeshqualitysettings.h"
#include "pdfrenderer.h"
#include "pdfdocument.h"
#include "pdffont.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfoptionalcontent.h"
#include "pdftextlayoutgenerator.h"

#include <QLineF>
#include <QTimer>

namespace pdf
{

/// Helpers of the text service
class PDFAppMcpTextServiceHelper
{
public:
    PDFAppMcpTextServiceHelper() = delete;

    /// Rectangle of the bounding boxes of the characters [first, last)
    static QRectF getCharactersRect(const TextCharacters& characters, size_t first, size_t last)
    {
        QRectF rect;
        for (size_t i = first; i < last; ++i)
        {
            const QRectF box = characters[i].boundingBox.boundingRect();
            rect = rect.isNull() ? box : rect.united(box);
        }
        return rect;
    }

    /// The text of a line from its characters (the spaces of the gaps between
    /// the words added as PDFTextFlow does)
    static QString getLineText(const TextCharacters& characters)
    {
        QStringList words;
        for (const std::pair<size_t, size_t>& range : splitWords(characters))
        {
            words << getText(characters, range.first, range.second);
        }
        return words.join(QLatin1Char(' '));
    }

    /// Text of the characters [first, last)
    static QString getText(const TextCharacters& characters, size_t first, size_t last)
    {
        QString text;
        text.reserve(qsizetype(last - first));
        for (size_t i = first; i < last; ++i)
        {
            text += characters[i].character;
        }
        return text;
    }

    /// Ranges [first, last) of the words of a line: separated by white space
    /// characters, or by a gap larger than 1.2 advance of the previous character
    /// (the rule of PDFTextFlow - many documents have no space glyphs)
    static std::vector<std::pair<size_t, size_t>> splitWords(const TextCharacters& characters)
    {
        std::vector<std::pair<size_t, size_t>> ranges;
        size_t start = 0;
        for (size_t i = 0; i <= characters.size(); ++i)
        {
            const bool isEnd = i == characters.size();
            const bool isSpace = !isEnd && characters[i].character.isSpace();
            const bool isGap = !isEnd && !isSpace && i > start && !characters[i - 1].character.isSpace() &&
                               QLineF(characters[i - 1].position, characters[i].position).length() > characters[i - 1].advance * 1.2;
            if (!isEnd && !isSpace && !isGap)
            {
                continue;
            }

            if (i > start)
            {
                ranges.emplace_back(start, i);
            }
            start = isGap ? i : i + 1;
        }
        return ranges;
    }

    static void writeTextItems(PDFMcpJsonWriter& writer, QLatin1StringView key, const std::vector<std::pair<QString, QRectF>>& items)
    {
        writer.key(key);
        writer.beginArray();
        for (const auto& item : items)
        {
            writer.beginObject();
            writer.writeMember(QLatin1StringView("text"), PDFAppMcpReadSupport::sanitizeContent(item.first, 4096));
            PDFAppMcpReadSupport::writeRect(writer, QLatin1StringView("rect"), item.second);
            writer.endObject();
        }
        writer.endArray();
    }
};

PDFTextLayout PDFAppMcpTextService::createPageLayout(const PDFDocument* document, PDFInteger pageIndex)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(size_t(pageIndex)) : nullptr;
    if (!page)
    {
        return PDFTextLayout();
    }

    // Own caches of the worker (the caches of the widgets are never touched)
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity(document, OCUsage::View, nullptr);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    PDFCMSGeneric cms;

    PDFFontCacheShrinkGuard shrinkGuard(&fontCache);
    PDFMeshQualitySettings meshQualitySettings;
    PDFTextLayoutGenerator generator(PDFRenderer::getDefaultFeatures(), page, document, &fontCache, &iccProfileCache, &cms,
                                     &optionalContentActivity, QTransform(), meshQualitySettings);
    generator.processContents();
    return generator.createTextLayout();
}

QString PDFAppMcpTextService::getLayoutText(const PDFTextLayout& layout, PDFInteger pageIndex)
{
    const PDFTextFlows flows = PDFTextFlow::createTextFlows(layout, PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen | PDFTextFlow::AddLineBreaks, pageIndex);
    QStringList texts;
    texts.reserve(qsizetype(flows.size()));
    for (const PDFTextFlow& flow : flows)
    {
        texts << flow.getText();
    }
    return texts.join(QLatin1Char('\n'));
}

std::vector<std::pair<QString, QRectF>> PDFAppMcpTextService::getWords(const PDFTextLayout& layout)
{
    using Helper = PDFAppMcpTextServiceHelper;

    std::vector<std::pair<QString, QRectF>> words;
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            const TextCharacters& characters = line.getCharacters();
            for (const std::pair<size_t, size_t>& range : Helper::splitWords(characters))
            {
                words.emplace_back(Helper::getText(characters, range.first, range.second), Helper::getCharactersRect(characters, range.first, range.second));
            }
        }
    }
    return words;
}

std::vector<std::pair<QString, QRectF>> PDFAppMcpTextService::getLines(const PDFTextLayout& layout)
{
    using Helper = PDFAppMcpTextServiceHelper;

    std::vector<std::pair<QString, QRectF>> lines;
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            const QString text = Helper::getLineText(line.getCharacters()).trimmed();
            if (!text.isEmpty())
            {
                lines.emplace_back(text, line.getBoundingBox().boundingRect());
            }
        }
    }
    return lines;
}

QString PDFAppMcpTextService::getAreaText(const PDFTextLayout& layout, const QRectF& area, std::vector<std::pair<QString, QRectF>>* words)
{
    using Helper = PDFAppMcpTextServiceHelper;

    QStringList lines;
    const QRectF normalizedArea = area.normalized();
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            QStringList lineWords;
            const TextCharacters& characters = line.getCharacters();
            for (const std::pair<size_t, size_t>& range : Helper::splitWords(characters))
            {
                const QRectF rect = Helper::getCharactersRect(characters, range.first, range.second);
                if (normalizedArea.contains(rect.center()))
                {
                    const QString word = Helper::getText(characters, range.first, range.second);
                    lineWords << word;
                    if (words)
                    {
                        words->emplace_back(word, rect);
                    }
                }
            }

            if (!lineWords.isEmpty())
            {
                lines << lineWords.join(QLatin1Char(' '));
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

PDFAppDocumentTextIndexer* PDFAppMcpTextService::findIndexer(PDFAppMcpHost* host, PDFAppDocument* document)
{
    PDFAppDocumentTab* tab = host ? host->getResolver().findTab(document) : nullptr;
    PDFAppDocumentSearchCommandHandler* handler = tab ? PDFAppDocumentSearchCommandHandler::findHandler(tab) : nullptr;
    return handler ? handler->getIndexer() : nullptr;
}

void PDFAppMcpTextService::start(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppDocument* document, PDFAppMcpTextRequest request)
{
    if (!host || !call || !document)
    {
        return;
    }

    std::shared_ptr<const PDFDocumentTextIndex> index = document->getTextIndex();
    const bool isFewPages = request.kind != PDFAppMcpTextJobKind::Search && !request.pageIndices.empty() &&
                            request.pageIndices.size() <= size_t(SINGLE_PAGE_LIMIT);

    // The index of the document, a single page layout, or the index built by the
    // indexer of the tab (a document without a tab builds it in the job)
    PDFAppDocumentTextIndexer* indexer = (index || isFewPages) ? nullptr : findIndexer(host, document);
    if (indexer)
    {
        PDFAppMcpTextIndexWaiter* waiter = new PDFAppMcpTextIndexWaiter(host, call, document, indexer, std::move(request));
        waiter->start();
        return;
    }

    host->startJob(std::make_shared<PDFAppMcpTextJob>(call, document->getSnapshot(), std::move(request), std::move(index)));
}

PDFAppMcpTextJob::PDFAppMcpTextJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot, PDFAppMcpTextRequest request,
                                   std::shared_ptr<const PDFDocumentTextIndex> index) :
    PDFAppMcpJob(std::move(call), std::move(snapshot)),
    m_request(std::move(request)),
    m_index(std::move(index))
{

}

PDFTextLayout PDFAppMcpTextJob::getLayout(PDFInteger pageIndex) const
{
    if (m_index && pageIndex >= 0 && pageIndex < m_index->getPageCount())
    {
        return m_index->getTextLayout(pageIndex);
    }
    return PDFAppMcpTextService::createPageLayout(getSnapshot().document.data(), pageIndex);
}

void PDFAppMcpTextJob::execute()
{
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!call || !getSnapshot().isValid())
    {
        return;
    }

    // A search or many pages without an index: the index is built here
    const bool isFewPages = m_request.kind != PDFAppMcpTextJobKind::Search && !m_request.pageIndices.empty() &&
                            m_request.pageIndices.size() <= size_t(PDFAppMcpTextService::SINGLE_PAGE_LIMIT);
    if (!m_index && !isFewPages)
    {
        call->reportProgress(0, 100, QStringLiteral("Indexing text 0 %"));
        m_builtIndex = PDFDocumentTextIndex::build(getSnapshot().document.data(), nullptr, call.get(), nullptr);
        m_index = m_builtIndex;
        if (!m_index)
        {
            return;
        }
    }

    switch (m_request.kind)
    {
        case PDFAppMcpTextJobKind::GetText:
            executeGetText();
            break;
        case PDFAppMcpTextJobKind::Search:
            executeSearch();
            break;
        case PDFAppMcpTextJobKind::TextInArea:
            executeTextInArea();
            break;
    }
}

void PDFAppMcpTextJob::executeGetText()
{
    using Helper = PDFAppMcpTextServiceHelper;
    using Support = PDFAppMcpReadSupport;

    const std::shared_ptr<PDFMcpCall>& call = getCall();
    const PDFDocument* document = getSnapshot().document.data();
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());

    PDFMcpJsonWriter writer;
    writer.beginObject();
    m_request.header.write(writer);
    writer.key(QLatin1StringView("pages"));
    writer.beginArray();

    QString readable;
    QString resourceText;
    qint64 characters = 0;
    bool isTruncated = false;
    std::optional<PDFInteger> nextPage;
    const qint64 limit = m_request.maximumCharacters;

    for (size_t i = 0; i < m_request.pageIndices.size(); ++i)
    {
        if (call->isCanceled())
        {
            return;
        }

        const PDFInteger pageIndex = m_request.pageIndices[i];
        // The layout is decoded only for the geometry or without an index
        PDFTextLayout layout;
        if (!m_index || m_request.geometry != PDFAppMcpTextGeometry::None)
        {
            layout = getLayout(pageIndex);
        }
        QString text = m_index ? m_index->getPageText(pageIndex) : PDFAppMcpTextService::getLayoutText(layout, pageIndex);
        text = Support::sanitizeContent(text, -1);

        // Whole pages; a single page longer than the limit is cut
        if (characters + text.size() > limit)
        {
            if (characters > 0)
            {
                nextPage = pageIndex + 1;
                break;
            }
            text.truncate(qsizetype(limit));
            isTruncated = true;
        }
        characters += text.size();
        m_pages.insert(int(pageIndex + 1));

        const QString label = Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, pageIndex), 256);
        if (m_request.isResource)
        {
            if (!resourceText.isEmpty())
            {
                resourceText += QStringLiteral("\n\n");
            }
            resourceText += m_request.pageIndices.size() > 1 ? QStringLiteral("--- Page %1 (label %2) ---\n%3").arg(pageIndex + 1).arg(label, text) : text;
            continue;
        }

        writer.beginObject();
        writer.writeMember(QLatin1StringView("page"), qint64(pageIndex + 1));
        writer.writeMember(QLatin1StringView("label"), label);
        writer.writeMember(QLatin1StringView("text"), QStringView(text));
        if (m_request.geometry == PDFAppMcpTextGeometry::Lines)
        {
            Helper::writeTextItems(writer, QLatin1StringView("lines"), PDFAppMcpTextService::getLines(layout));
        }
        else if (m_request.geometry == PDFAppMcpTextGeometry::Words)
        {
            Helper::writeTextItems(writer, QLatin1StringView("words"), PDFAppMcpTextService::getWords(layout));
        }
        writer.endObject();

        readable += QStringLiteral("--- Page %1 (label %2) ---\n%3\n").arg(pageIndex + 1).arg(label, text);
        if (isTruncated)
        {
            if (pageIndex + 1 < pageCount)
            {
                nextPage = pageIndex + 2;
            }
            break;
        }
    }

    if (m_request.isResource)
    {
        if (isTruncated || nextPage)
        {
            const bool isModern = call->getRequest().client.era == PDFMcpEra::Modern;
            PDFMcpJsonWriter data;
            data.beginObject();
            data.writeMember(QLatin1StringView("uri"), QStringView(m_request.resourceUri));
            data.writeMember(QLatin1StringView("reason"), QLatin1StringView("too large"));
            data.endObject();
            call->finish(PDFMcpCallResult::createError(isModern ? PDFMcpErrorCodes::INVALID_PARAMS : PDFMcpErrorCodes::INTERNAL_ERROR,
                                                       QStringLiteral("The text of the document is too large for a resource; use text.get_text with page ranges."), data.take()));
            m_isFailed = true;
            return;
        }
        call->finish(PDFAppMcpDocumentResources::createTextResult(m_request.resourceUri, QStringLiteral("text/plain"), resourceText));
        return;
    }

    writer.endArray();
    writer.writeMember(QLatin1StringView("truncated"), isTruncated);
    if (nextPage)
    {
        writer.writeMember(QLatin1StringView("nextPage"), qint64(*nextPage));
        readable += isTruncated ? QStringLiteral("[The text was cut at maxCharacters; continue with page %1.]").arg(*nextPage)
                                : QStringLiteral("[More pages follow; continue with page %1.]").arg(*nextPage);
    }
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();

    call->finish(Support::createResult(QStringLiteral("get_text"), writer.take(), true, readable));
}

void PDFAppMcpTextJob::executeSearch()
{
    using Support = PDFAppMcpReadSupport;

    const std::shared_ptr<PDFMcpCall>& call = getCall();
    const PDFDocument* document = getSnapshot().document.data();
    if (!m_index)
    {
        return;
    }

    // With a page filter, the results of the other pages are found and skipped
    const std::set<PDFInteger> filter(m_request.pageIndices.cbegin(), m_request.pageIndices.cend());
    const size_t maximumResults = filter.empty() ? size_t(m_request.maximumResults) : size_t(100000);
    const PDFDocumentTextSearchResult result = PDFDocumentTextSearch::find(*m_index, m_request.query, m_request.searchOptions, maximumResults + (filter.empty() ? 1 : 0), call.get());
    if (call->isCanceled() || result.isCanceled)
    {
        return;
    }

    if (!result.error.isEmpty())
    {
        m_isFailed = true;
        call->finish(PDFMcpCallResult::createToolError(QStringLiteral("query: the regular expression is not valid: %1. Fix the query or pass regularExpression false.")
                                                       .arg(Support::sanitizeName(result.error, 512))));
        return;
    }

    PDFMcpJsonWriter writer;
    writer.beginObject();
    m_request.header.write(writer);
    writer.key(QLatin1StringView("matches"));
    writer.beginArray();

    QString readable;
    int count = 0;
    bool isTruncated = result.isTruncated;
    for (const PDFDocumentTextSearchMatch& match : result.results)
    {
        if (!filter.empty() && !filter.count(match.pageIndex))
        {
            continue;
        }
        if (count >= m_request.maximumResults)
        {
            isTruncated = true;
            break;
        }
        ++count;
        m_pages.insert(int(match.pageIndex + 1));

        const QString label = Support::sanitizeName(PDFAppMcpArguments::getPageLabel(document, match.pageIndex), 256);
        const QString context = Support::sanitizeContent(match.context, 4096);
        writer.beginObject();
        writer.writeMember(QLatin1StringView("page"), qint64(match.pageIndex + 1));
        writer.writeMember(QLatin1StringView("label"), label);
        writer.writeMember(QLatin1StringView("text"), Support::sanitizeContent(match.matched, 4096));
        writer.writeMember(QLatin1StringView("context"), context);
        writer.writeMember(QLatin1StringView("contextStart"), qint64(match.contextMatchStart));
        Support::writeQuads(writer, QLatin1StringView("quads"), match.quadrilaterals);
        Support::writeRect(writer, QLatin1StringView("rect"), match.boundingRect);
        writer.endObject();

        readable += QStringLiteral("Page %1 (label %2): %3\n").arg(match.pageIndex + 1).arg(label, context.simplified());
    }

    writer.endArray();
    writer.writeMember(QLatin1StringView("truncated"), isTruncated);
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    QByteArray object = writer.take();

    // The readable text block lists the matches; the quads are in structuredContent
    QString text = QStringLiteral("%1 matches of \"%2\"%3.\n").arg(count).arg(Support::sanitizeName(m_request.query, 256), isTruncated ? QStringLiteral(" (more exist; narrow the query or the pages)") : QString());
    call->finish(Support::createResult(QStringLiteral("search"), object, true, text + readable +
                                       QStringLiteral("The quads of the matches are in structuredContent (pass them to annotations.add).")));
}

void PDFAppMcpTextJob::executeTextInArea()
{
    using Helper = PDFAppMcpTextServiceHelper;
    using Support = PDFAppMcpReadSupport;

    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (m_request.pageIndices.size() != 1)
    {
        return;
    }

    const PDFInteger pageIndex = m_request.pageIndices.front();
    const PDFTextLayout layout = getLayout(pageIndex);
    std::vector<std::pair<QString, QRectF>> words;
    const QString text = Support::sanitizeContent(PDFAppMcpTextService::getAreaText(layout, m_request.area, &words), -1);
    m_pages.insert(int(pageIndex + 1));

    PDFMcpJsonWriter writer;
    writer.beginObject();
    m_request.header.write(writer);
    writer.writeMember(QLatin1StringView("text"), QStringView(text));
    Helper::writeTextItems(writer, QLatin1StringView("words"), words);
    writer.writeMember(QLatin1StringView("origin"), QLatin1StringView("document"));
    writer.endObject();
    call->finish(Support::createResult(QStringLiteral("get_text_in_area"), writer.take(), true, text));
}

void PDFAppMcpTextJob::finish(PDFAppMcpHost* host)
{
    const std::shared_ptr<PDFMcpCall>& call = getCall();
    if (!host || !call)
    {
        return;
    }

    // The index built by the job belongs to the document of its version
    PDFAppDocument* document = host->getResolver().resolve(m_request.header.documentId, false).document;
    if (m_builtIndex && document)
    {
        document->setTextIndex(m_builtIndex, getSnapshot().version);
    }

    if (call->isCanceled() && !call->isFinished())
    {
        call->finish(PDFAppMcpJobRunnable::createFailure(*call, QStringLiteral("The request was canceled.")));
    }

    const std::optional<PDFMcpCallResult> result = call->getResult();
    const bool isError = m_isFailed || !result || result->kind == PDFMcpCallResult::Kind::Error || result->isError;
    host->recordActivity(call, m_request.actionId, document, QJsonObject(), PDFAppMcpReadSupport::getActivityStatus(*call, isError), m_pages);
}

PDFAppMcpTextIndexWaiter::PDFAppMcpTextIndexWaiter(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, PDFAppDocument* document,
                                                   PDFAppDocumentTextIndexer* indexer, PDFAppMcpTextRequest request) :
    BaseClass(host),
    m_host(host),
    m_call(std::move(call)),
    m_document(document),
    m_indexer(indexer),
    m_request(std::move(request))
{
    if (m_indexer)
    {
        connect(m_indexer, &PDFAppDocumentTextIndexer::indexReady, this, &PDFAppMcpTextIndexWaiter::onIndexReady);
        connect(m_indexer, &PDFAppDocumentTextIndexer::buildingChanged, this, &PDFAppMcpTextIndexWaiter::onBuildingChanged);
        connect(m_indexer, &PDFAppDocumentTextIndexer::progressChanged, this, &PDFAppMcpTextIndexWaiter::onProgressChanged);
    }

    // The call may be canceled, the document or the indexer may go away
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(250);
    connect(m_checkTimer, &QTimer::timeout, this, &PDFAppMcpTextIndexWaiter::onCheckTimer);
    m_checkTimer->start();
}

PDFAppMcpTextIndexWaiter::~PDFAppMcpTextIndexWaiter()
{
    // A waiter destroyed with its host (shutdown) ends its call
    if (!m_isDone && m_call && !m_call->isFinished())
    {
        m_call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
    }
}

void PDFAppMcpTextIndexWaiter::start()
{
    if (!m_indexer)
    {
        fail(QStringLiteral("The document was closed in PDF4QT."));
        return;
    }

    ++m_requestCount;
    QPointer<PDFAppMcpTextIndexWaiter> self(this);
    const bool isReady = m_indexer->requestIndex();
    if (!self)
    {
        return;
    }

    if (isReady)
    {
        onIndexReady();
    }
    else
    {
        onProgressChanged(m_indexer ? m_indexer->getProgress() : 0);
    }
}

void PDFAppMcpTextIndexWaiter::onIndexReady()
{
    if (m_isDone)
    {
        return;
    }

    std::shared_ptr<const PDFDocumentTextIndex> index = m_indexer ? m_indexer->getIndex() : nullptr;
    if (!index && m_document)
    {
        index = m_document->getTextIndex();
    }
    if (!index)
    {
        return;
    }
    startJob(std::move(index));
}

void PDFAppMcpTextIndexWaiter::onBuildingChanged()
{
    if (m_isDone || !m_indexer || m_indexer->isBuilding())
    {
        return;
    }

    if (m_indexer->getIndex())
    {
        onIndexReady();
        return;
    }

    // The build was canceled (a change of the page contents, Stop of the user):
    // it is requested once more
    if (m_requestCount < 3)
    {
        start();
        return;
    }
    fail(QStringLiteral("The text index of the document could not be built (the build was stopped). Try again."));
}

void PDFAppMcpTextIndexWaiter::onProgressChanged(int progress)
{
    if (m_isDone || !m_call || progress <= m_lastProgress)
    {
        return;
    }

    m_lastProgress = progress;
    m_call->reportProgress(qBound(0, progress, 99), 100, QStringLiteral("Indexing text %1 %").arg(qBound(0, progress, 100)));
}

void PDFAppMcpTextIndexWaiter::onCheckTimer()
{
    if (m_isDone)
    {
        return;
    }

    if (!m_call || m_call->isFinished())
    {
        m_isDone = true;
        deleteLater();
        return;
    }

    // A call canceled while the index is built ends here (not a job - nobody
    // else answers it; every cancellation has its activity row, review RC7-H)
    if (m_call->isCanceled())
    {
        m_isDone = true;
        if (m_host)
        {
            m_host->recordActivity(m_call, m_request.actionId, m_document, QJsonObject(), PDFMcpActivityStatus::Canceled);
        }
        m_call->finish(PDFAppMcpJobRunnable::createFailure(*m_call, QStringLiteral("The request was canceled.")));
        deleteLater();
        return;
    }

    if (!m_document || !m_indexer || !m_host)
    {
        fail(QStringLiteral("The document was closed in PDF4QT."));
    }
}

void PDFAppMcpTextIndexWaiter::startJob(std::shared_ptr<const PDFDocumentTextIndex> index)
{
    if (m_isDone)
    {
        return;
    }

    m_isDone = true;
    if (m_host && m_document && m_call && !m_call->isFinished())
    {
        m_host->startJob(std::make_shared<PDFAppMcpTextJob>(m_call, m_document->getSnapshot(), m_request, std::move(index)));
    }
    deleteLater();
}

void PDFAppMcpTextIndexWaiter::fail(const QString& text)
{
    if (m_isDone)
    {
        return;
    }

    m_isDone = true;
    if (m_host && m_call)
    {
        m_host->finishWithError(m_call, m_request.actionId, m_document, text);
    }
    else if (m_call)
    {
        m_call->finish(PDFMcpCallResult::createToolError(text));
    }
    deleteLater();
}

}   // namespace pdf
