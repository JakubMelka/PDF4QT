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

#include "pdfdocumenttextindex.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdftextlayoutgenerator.h"
#include "pdfutils.h"

#include <QRegularExpression>
#include <QThread>

#include <algorithm>

#include "pdfdbgheap.h"

namespace pdf
{

/// Builds the text layouts of the pages of PDFDocumentTextIndex (the functor of
/// PDFExecutionPolicy::execute, one call per page, called from several threads)
class PDFDocumentTextIndexBuilder
{
public:
    PDFDocumentTextIndexBuilder(PDFDocumentTextIndex* index,
                                const PDFDocument* document,
                                const PDFFontCache* fontCache,
                                const PDFICCProfileCache* iccProfileCache,
                                const PDFCMS* cms,
                                const PDFOptionalContentActivity* optionalContentActivity,
                                const PDFOperationControl* operationControl,
                                PDFProgress* progress,
                                std::atomic<PDFInteger>* processedPageCount) :
        m_index(index),
        m_document(document),
        m_fontCache(fontCache),
        m_iccProfileCache(iccProfileCache),
        m_cms(cms),
        m_optionalContentActivity(optionalContentActivity),
        m_operationControl(operationControl),
        m_progress(progress),
        m_processedPageCount(processedPageCount)
    {

    }

    void operator()(PDFInteger pageIndex) const
    {
        if (!PDFOperationControl::isOperationCancelled(m_operationControl))
        {
            const PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
            if (page)
            {
                PDFMeshQualitySettings meshQualitySettings;
                PDFTextLayoutGenerator generator(PDFRenderer::getDefaultFeatures(), page, m_document, m_fontCache, m_iccProfileCache, m_cms,
                                                 m_optionalContentActivity, QTransform(), meshQualitySettings);
                generator.processContents();
                m_index->m_storage.setTextLayout(pageIndex, generator.createTextLayout());
            }
        }

        if (m_progress)
        {
            m_progress->step();
        }

        if (m_processedPageCount)
        {
            m_processedPageCount->fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    PDFDocumentTextIndex* m_index;
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFICCProfileCache* m_iccProfileCache;
    const PDFCMS* m_cms;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFOperationControl* m_operationControl;
    PDFProgress* m_progress;
    std::atomic<PDFInteger>* m_processedPageCount;
};

/// Gives the text layout of one page to PDFTextLayoutCache (the geometry of the
/// matches of the page is computed from the layout, which the search has decoded)
class PDFDocumentTextSearchLayoutProvider
{
public:
    explicit PDFDocumentTextSearchLayoutProvider(const PDFTextLayout* layout) :
        m_layout(layout)
    {

    }

    PDFTextLayout operator()(PDFInteger pageIndex) const
    {
        Q_UNUSED(pageIndex);
        return *m_layout;
    }

private:
    const PDFTextLayout* m_layout;
};

/// Searches one page of the index (the functor of PDFExecutionPolicy::execute,
/// one call per page of a chunk, called from several threads). Each call writes
/// only the results of its own page.
class PDFDocumentTextSearchPageFunctor
{
public:
    PDFDocumentTextSearchPageFunctor(const PDFDocumentTextIndex* index,
                                     const QString* query,
                                     const QString* pattern,
                                     QRegularExpression::PatternOptions patternOptions,
                                     Qt::CaseSensitivity caseSensitivity,
                                     size_t maximumResults,
                                     PDFInteger firstPage,
                                     std::vector<PDFDocumentTextSearchMatches>* pageResults,
                                     const PDFOperationControl* operationControl) :
        m_index(index),
        m_query(query),
        m_pattern(pattern),
        m_patternOptions(patternOptions),
        m_caseSensitivity(caseSensitivity),
        m_maximumResults(maximumResults),
        m_firstPage(firstPage),
        m_pageResults(pageResults),
        m_operationControl(operationControl)
    {

    }

    void operator()(PDFInteger pageIndex) const
    {
        if (PDFOperationControl::isOperationCancelled(m_operationControl))
        {
            return;
        }

        const PDFTextLayout layout = m_index->getStorage().getTextLayout(pageIndex);
        if (layout.getTextBlocks().empty())
        {
            return;
        }

        // Each page has its own expression: the matching is not shared by the threads
        QRegularExpression expression;
        if (!m_pattern->isEmpty())
        {
            expression = QRegularExpression(*m_pattern, m_patternOptions);
        }

        const PDFTextFlow::FlowFlags flowFlags = PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen;
        const PDFTextFlows flows = PDFTextFlow::createTextFlows(layout, flowFlags, pageIndex);

        PDFFindResults findResults;
        for (const PDFTextFlow& flow : flows)
        {
            if (PDFOperationControl::isOperationCancelled(m_operationControl))
            {
                return;
            }

            PDFFindResults flowResults = m_pattern->isEmpty() ? flow.find(*m_query, m_caseSensitivity) : flow.find(expression);
            findResults.insert(findResults.end(), std::make_move_iterator(flowResults.begin()), std::make_move_iterator(flowResults.end()));

            // A page cannot give more results than the whole search
            if (findResults.size() > m_maximumResults)
            {
                break;
            }
        }
        std::sort(findResults.begin(), findResults.end());
        if (findResults.size() > m_maximumResults)
        {
            findResults.resize(m_maximumResults);
        }

        PDFTextLayoutCache layoutCache{PDFDocumentTextSearchLayoutProvider(&layout)};
        PDFDocumentTextSearchMatches& matches = (*m_pageResults)[size_t(pageIndex - m_firstPage)];
        matches.reserve(findResults.size());
        for (PDFFindResult& findResult : findResults)
        {
            PDFDocumentTextSearchMatch match;
            match.pageIndex = pageIndex;
            match.matched = std::move(findResult.matched);
            match.context = std::move(findResult.context);
            match.contextMatchStart = findResult.contextMatchStart;
            match.contextMatchLength = findResult.contextMatchLength;
            match.textSelectionItems = std::move(findResult.textSelectionItems);

            PDFTextSelection selection;
            selection.addItems(match.textSelectionItems, QColor());
            selection.build();
            PDFTextSelectionPainter painter(&selection);
            PDFTextLayoutGetter getter(&layoutCache, pageIndex);
            painter.prepareGeometry(pageIndex, getter, QTransform(), &match.quadrilaterals);
            match.boundingRect = match.quadrilaterals.boundingRect();
            matches.push_back(std::move(match));
        }
    }

private:
    const PDFDocumentTextIndex* m_index;
    const QString* m_query;
    const QString* m_pattern;
    QRegularExpression::PatternOptions m_patternOptions;
    Qt::CaseSensitivity m_caseSensitivity;
    size_t m_maximumResults;
    PDFInteger m_firstPage;
    std::vector<PDFDocumentTextSearchMatches>* m_pageResults;
    const PDFOperationControl* m_operationControl;
};

std::shared_ptr<const PDFDocumentTextIndex> PDFDocumentTextIndex::build(const PDFDocument* document,
                                                                        const PDFOptionalContentActivity* optionalContentActivity,
                                                                        const PDFOperationControl* operationControl,
                                                                        PDFProgress* progress,
                                                                        std::atomic<PDFInteger>* processedPageCount)
{
    if (!document)
    {
        return nullptr;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());

    // The optional content of the view, when the caller has none (the activity is
    // only read by the generators)
    std::unique_ptr<PDFOptionalContentActivity> ownActivity;
    if (!optionalContentActivity)
    {
        ownActivity = std::make_unique<PDFOptionalContentActivity>(document, OCUsage::View, nullptr);
        optionalContentActivity = ownActivity.get();
    }

    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), const_cast<PDFOptionalContentActivity*>(optionalContentActivity));
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    PDFCMSGeneric cms;

    std::shared_ptr<PDFDocumentTextIndex> index = std::make_shared<PDFDocumentTextIndex>();
    index->m_storage = PDFTextLayoutStorage(pageCount);

    if (progress)
    {
        ProgressStartupInfo info;
        info.showDialog = false;
        info.text = PDFTranslationContext::tr("Indexing the text of the document...");
        progress->start(size_t(pageCount), std::move(info));
    }

    {
        PDFFontCacheShrinkGuard shrinkGuard(&fontCache);
        PDFDocumentTextIndexBuilder builder(index.get(), document, &fontCache, &iccProfileCache, &cms, optionalContentActivity, operationControl, progress, processedPageCount);
        PDFIntegerRange<PDFInteger> pageRange(0, pageCount);
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), builder);
    }

    if (progress)
    {
        progress->finish();
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        return nullptr;
    }

    return index;
}

PDFTextLayout PDFDocumentTextIndex::getTextLayout(PDFInteger pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= getPageCount())
    {
        return PDFTextLayout();
    }
    return m_storage.getTextLayout(pageIndex);
}

QString PDFDocumentTextIndex::getPageText(PDFInteger pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= getPageCount())
    {
        return QString();
    }

    const PDFTextFlows flows = PDFTextFlow::createTextFlows(m_storage.getTextLayout(pageIndex),
                                                            PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen | PDFTextFlow::AddLineBreaks,
                                                            pageIndex);
    QStringList texts;
    texts.reserve(qsizetype(flows.size()));
    for (const PDFTextFlow& flow : flows)
    {
        texts << flow.getText();
    }
    return texts.join(QLatin1Char('\n'));
}

QString PDFDocumentTextSearch::createPattern(const QString& query, const PDFDocumentTextSearchOptions& options)
{
    if (!options.regularExpression && !options.wholeWords && !options.wildcards)
    {
        return QString();
    }

    QString pattern;
    if (options.regularExpression)
    {
        pattern = query;
    }
    else if (options.wildcards)
    {
        // * = characters of a word, ? = one character of a word, the rest is literal
        pattern.reserve(query.size() * 2);
        for (const QChar character : query)
        {
            if (character == QLatin1Char('*'))
            {
                pattern += QStringLiteral("\\w*");
            }
            else if (character == QLatin1Char('?'))
            {
                pattern += QStringLiteral("\\w");
            }
            else
            {
                pattern += QRegularExpression::escape(QString(character));
            }
        }
    }
    else
    {
        pattern = QRegularExpression::escape(query);
    }

    if (options.wholeWords)
    {
        // Not \b: a query, which starts or ends with a character outside of a word,
        // would never match a whole word
        pattern = QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(pattern);
    }

    return pattern;
}

PDFDocumentTextSearchResult PDFDocumentTextSearch::find(const PDFDocumentTextIndex& index,
                                                        const QString& query,
                                                        const PDFDocumentTextSearchOptions& options,
                                                        size_t maximumResults,
                                                        const PDFOperationControl* operationControl)
{
    PDFDocumentTextSearchResult result;
    if (maximumResults == 0)
    {
        maximumResults = DEFAULT_MAXIMUM_RESULTS;
    }

    if (query.isEmpty())
    {
        return result;
    }

    if (query.size() > MAXIMUM_QUERY_LENGTH)
    {
        result.error = PDFTranslationContext::tr("The search text is too long.");
        return result;
    }

    // Whole words, wildcards and regular expressions are searched as a regular
    // expression, which is checked first
    const QString pattern = createPattern(query, options);
    QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
    {
        patternOptions |= QRegularExpression::CaseInsensitiveOption;
    }

    if (!pattern.isEmpty())
    {
        const QRegularExpression expression(pattern, patternOptions);
        if (!expression.isValid())
        {
            result.error = PDFTranslationContext::tr("The regular expression is not valid: %1.").arg(expression.errorString());
            return result;
        }
    }

    const Qt::CaseSensitivity caseSensitivity = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const PDFInteger pageCount = index.getPageCount();

    // The pages are searched in chunks: the pages of a chunk in parallel, the
    // chunks one after another, so the search stops soon after the limit
    const PDFInteger chunkSize = qMax(PDFInteger(16), PDFInteger(QThread::idealThreadCount()) * 4);
    for (PDFInteger firstPage = 0; firstPage < pageCount && !result.isTruncated; firstPage += chunkSize)
    {
        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            break;
        }

        const PDFInteger lastPage = qMin(pageCount, firstPage + chunkSize);
        std::vector<PDFDocumentTextSearchMatches> pageResults(size_t(lastPage - firstPage));
        PDFDocumentTextSearchPageFunctor functor(&index, &query, &pattern, patternOptions, caseSensitivity, maximumResults + 1, firstPage, &pageResults, operationControl);
        PDFIntegerRange<PDFInteger> pageRange(firstPage, lastPage);
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), functor);

        for (PDFDocumentTextSearchMatches& matches : pageResults)
        {
            for (PDFDocumentTextSearchMatch& match : matches)
            {
                if (result.results.size() >= maximumResults)
                {
                    result.isTruncated = true;
                    break;
                }
                result.results.push_back(std::move(match));
            }

            if (result.isTruncated)
            {
                break;
            }
        }
    }

    if (PDFOperationControl::isOperationCancelled(operationControl))
    {
        result.isCanceled = true;
        result.isTruncated = false;
        result.results.clear();
    }

    return result;
}

}   // namespace pdf
