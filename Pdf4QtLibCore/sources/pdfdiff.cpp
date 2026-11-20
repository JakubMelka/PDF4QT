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

#include "pdfdiff.h"
#include "pdfrenderer.h"
#include "pdfdocumenttextflow.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfalgorithmlcs.h"
#include "pdfpainter.h"
#include "pdfdocumenttooltablewriter.h"

#include <QtConcurrent/QtConcurrent>
#include <QXmlStreamWriter>

#include <numeric>

#include "pdfdbgheap.h"

namespace pdf
{

/// A page prepared for the comparison. The elements of the paths of its pieces are copied into a
/// plain array (\p elements, the piece i has the elements from elementOffsets[i] to
/// elementOffsets[i + 1]): the threads of the search of the moved pages read them at once, a
/// QPainterPath is never read by more threads (CLAUDE.md).
struct PDFDiffPageContext
{
    PDFInteger pageIndex = 0;
    std::array<uint8_t, 64> pageHash = { };
    PDFPrecompiledPage::GraphicPieceInfos graphicPieces;
    std::vector<QPainterPath::Element> elements;
    std::vector<size_t> elementOffsets;
    PDFDocumentTextFlow text;
};

class PDFDiffHelper
{
public:
    using GraphicPieceInfo = PDFPrecompiledPage::GraphicPieceInfo;
    using GraphicPieceInfos = PDFPrecompiledPage::GraphicPieceInfos;
    using PageSequence = PDFAlgorithmLongestCommonSubsequenceBase::Sequence;

    /// Most work (candidate pieces plus compared elements of their paths) of the pairing of the
    /// pieces of two pages within the tolerance (B194): the pieces, which are not paired within it,
    /// are differences - a hostile page of a hundred thousand pieces never makes it quadratic
    static constexpr qint64 MAXIMUM_PAIRING_WORK = qint64(4) * 1024 * 1024;

    /// Most work of the search of the moved pages over all unmatched pages (B194), divided equally
    /// among the unmatched pages of the original document (deterministic for any count of threads)
    static constexpr qint64 MAXIMUM_PAGE_MATCHING_WORK = qint64(256) * 1024 * 1024;

    /// Pieces of two pages, which are different: indices into their graphicPieces
    struct Differences
    {
        std::vector<size_t> left;
        std::vector<size_t> right;

        bool isEmpty() const { return left.empty() && right.empty(); }
    };

    /// A range of changed pages, whose text is compared: the joined text of its pages and its
    /// pages (a replaced pair, an added page with no left page, a removed page with no right page)
    struct TextFlowDifferences
    {
        PDFDocumentTextFlow leftTextFlow;
        PDFDocumentTextFlow rightTextFlow;
        std::vector<std::pair<const PDFDiffPageContext*, const PDFDiffPageContext*>> pages;
    };

    /// The text of one pair of pages of a range over the budget of the cells (R7-6)
    struct TextPairJob
    {
        const PDFDiffPageContext* left = nullptr;
        const PDFDiffPageContext* right = nullptr;
    };

    struct TextCompareItem
    {
        size_t index = 0;
        int charIndex = 0;
        int charCount = 0;
        bool left = false;
    };

    /// Copies the elements of the paths of the pieces (sorted already) into the plain array of the context
    static void preparePieceElements(PDFDiffPageContext& context);

    /// The pieces of the two pages, which are not in the other one: equal pieces by their hashes (as
    /// multisets), the rest paired within the tolerance \p epsilon (MAXIMUM_PAIRING_WORK); the text
    /// pieces are left out with \p isTextIgnored (the text is compared by its characters)
    static Differences calculateDifferences(const PDFDiffPageContext& left, const PDFDiffPageContext& right, PDFReal epsilon, bool isTextIgnored);

    /// The two pages have the same content within the tolerance; \p work is the remaining work of the
    /// caller, it is decreased (nothing left = not equal)
    static bool isEqualContent(const PDFDiffPageContext& left, const PDFDiffPageContext& right, PDFReal epsilon, qint64* work);

    /// Pairs the pieces of differences within the tolerance and removes the paired ones; with
    /// \p stopAtUnpaired it returns false at the first piece of the left page without a pair
    static bool pairWithTolerance(const PDFDiffPageContext& left, const PDFDiffPageContext& right, PDFReal epsilon,
                                  Differences* differences, qint64* work, bool stopAtUnpaired);

    static std::vector<size_t> getLeftUnmatched(const PageSequence& sequence);
    static std::vector<size_t> getRightUnmatched(const PageSequence& sequence);

    /// The sequence of the pages by their positions (the pages over the budget of the cells of the
    /// matching): equal hashes are a match, other pairs are a left and a right item (replaced)
    static PageSequence createPositionalSequence(const std::vector<PDFDiffPageContext>& left, const std::vector<PDFDiffPageContext>& right);

    static std::vector<TextCompareItem> prepareTextCompareItems(const PDFDocumentTextFlow& textFlow,
                                                                bool isWordsComparingMode,
                                                                bool isLeft);
    static void refineTextRectangles(PDFDiffResult::RectInfos& items);

    /// Compares two texts word by word (or by characters) and adds the text differences to the
    /// result (under \p mutex). Returns false and adds nothing, when the texts are over the budget
    /// of the cells (B191, R7-6) or the comparison was canceled.
    static bool compareTextFlows(const PDFDocumentTextFlow& leftTextFlow,
                                 const PDFDocumentTextFlow& rightTextFlow,
                                 bool isWordsComparingMode,
                                 const PDFOperationControl* control,
                                 QMutex* mutex,
                                 PDFDiffResult* result);

    /// The rectangle of the whole text of each page of a text flow (one per page)
    static PDFDiffResult::RectInfos getTextRectangles(const PDFDocumentTextFlow& textFlow);
};

PDFDiff::PDFDiff(QObject* parent) :
    BaseClass(parent),
    m_progress(nullptr),
    m_leftDocument(nullptr),
    m_rightDocument(nullptr),
    m_options(Asynchronous | PC_Text | PC_VectorGraphics | PC_Images | CompareWords),
    m_epsilon(0.001),
    m_textAnalysisAlgorithm(PDFDocumentTextFlowFactory::Algorithm::Layout)
{

}

PDFDiff::~PDFDiff()
{
    stop();
}

void PDFDiff::setLeftDocument(const PDFDocument* leftDocument)
{
    if (m_leftDocument != leftDocument)
    {
        stop();
        m_leftDocument = leftDocument;
    }
}

void PDFDiff::setRightDocument(const PDFDocument* rightDocument)
{
    if (m_rightDocument != rightDocument)
    {
        stop();
        m_rightDocument = rightDocument;
    }
}

void PDFDiff::setPagesForLeftDocument(PDFClosedIntervalSet pagesForLeftDocument)
{
    stop();
    m_pagesForLeftDocument = std::move(pagesForLeftDocument);
}

void PDFDiff::setPagesForRightDocument(PDFClosedIntervalSet pagesForRightDocument)
{
    stop();
    m_pagesForRightDocument = std::move(pagesForRightDocument);
}

PDFDiffResult PDFDiff::compare(const PDFDocument* leftDocument,
                               const PDFDocument* rightDocument,
                               const PDFClosedIntervalSet& leftPages,
                               const PDFClosedIntervalSet& rightPages,
                               Options options,
                               const PDFOperationControl* control,
                               PDFProgressSink* progress)
{
    // R7-15: the comparison in the thread of the caller; the engine object is local to this call (it
    // lives and dies in this thread, no signal of it is connected)
    PDFDiff diff(nullptr);
    diff.m_leftDocument = leftDocument;
    diff.m_rightDocument = rightDocument;
    diff.m_pagesForLeftDocument = leftPages;
    diff.m_pagesForRightDocument = rightPages;
    diff.m_options = options;
    diff.m_options.setFlag(Asynchronous, false);
    diff.m_progress = progress;
    diff.m_control = control;
    return diff.perform();
}

void PDFDiff::start()
{
    // Jakub Melka: First, we must ensure, that comparation
    // process is finished, otherwise we must wait for end.
    // Then, create a new future watcher.
    stop();

    m_cancelControl.canceled = false;
    m_control = &m_cancelControl;

    if (m_options.testFlag(Asynchronous))
    {
        m_futureWatcher = std::nullopt;
        m_futureWatcher.emplace();

        m_future = QtConcurrent::run(std::bind(&PDFDiff::perform, this));
        connect(&*m_futureWatcher, &QFutureWatcher<PDFDiffResult>::finished, this, &PDFDiff::onComparationPerformed);
        m_futureWatcher->setFuture(m_future);
    }
    else
    {
        // Just do comparation immediately
        m_result = perform();
        Q_EMIT comparationFinished();
    }
}

void PDFDiff::stop()
{
    if (m_futureWatcher && !m_futureWatcher->isFinished())
    {
        // Do stop only if process doesn't finished already.
        // If we are finished, we do not want to set canceled state.
        m_cancelControl.canceled = true;
        m_futureWatcher->waitForFinished();
    }
}

PDFDiffResult PDFDiff::perform()
{
    PDFDiffResult result;

    if (!m_leftDocument || !m_rightDocument)
    {
        result.setResult(tr("No document to be compared."));
        return result;
    }

    const PDFInteger leftDocumentPageCount = PDFInteger(m_leftDocument->getCatalog()->getPageCount());
    const PDFInteger rightDocumentPageCount = PDFInteger(m_rightDocument->getCatalog()->getPageCount());

    // B192: a document without pages is an error, never an access to its first page
    if (leftDocumentPageCount == 0)
    {
        result.setResult(tr("The original document has no pages."));
        return result;
    }
    if (rightDocumentPageCount == 0)
    {
        result.setResult(tr("The revised document has no pages."));
        return result;
    }

    if (m_pagesForLeftDocument.isEmpty() || m_pagesForRightDocument.isEmpty())
    {
        result.setResult(tr("No page to be compared."));
        return result;
    }

    // B174: the ranges are checked before they are unfolded - a range of billions of pages is never
    // allocated, a page outside of the document is an error
    auto isInDocument = [](const PDFClosedIntervalSet& pages, PDFInteger pageCount)
    {
        PDFClosedIntervalSet pagesInDocument = pages;
        pagesInDocument.intersect(0, pageCount - 1);
        return pagesInDocument == pages;
    };
    if (!isInDocument(m_pagesForLeftDocument, leftDocumentPageCount) || !isInDocument(m_pagesForRightDocument, rightDocumentPageCount))
    {
        result.setResult(tr("Invalid page range."));
        return result;
    }

    const std::vector<PDFInteger> leftPages = m_pagesForLeftDocument.unfold();
    const std::vector<PDFInteger> rightPages = m_pagesForRightDocument.unfold();

    if (m_progress)
    {
        ProgressStartupInfo info;
        info.showDialog = false;
        info.text = tr("Comparing documents.");
        m_progress->start(StepLast, std::move(info));
    }

    performSteps(leftPages, rightPages, result);

    if (m_progress)
    {
        m_progress->finish();
    }

    // B173: a canceled comparison is never a result without differences
    if (isCanceled())
    {
        PDFDiffResult canceledResult;
        canceledResult.setResult(tr("The comparison has been canceled."));
        return canceledResult;
    }

    return result;
}

void PDFDiff::stepProgress()
{
    if (m_progress)
    {
        m_progress->step();
    }
}

void PDFDiff::performPageMatching(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                                  const std::vector<PDFDiffPageContext>& rightPreparedPages,
                                  PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                                  std::map<size_t, size_t>& pageMatches)
{
    // Match pages. We will use following algorithm: exact solution can fail, because
    // we are using hashes and due to numerical instability, hashes can be different
    // even for exactly the same page. But if hashes are the same, the page must be the same.
    // So, we use longest common subsequence algorithm to detect same page ranges,
    // and then we match the rest. We assume the number of failing pages is relatively small.

    auto comparePages = [&](const PDFDiffPageContext& left, const PDFDiffPageContext& right)
    {
        if (left.pageHash == right.pageHash)
        {
            return true;
        }

        auto it = pageMatches.find(left.pageIndex);
        if (it != pageMatches.cend())
        {
            return it->second == static_cast<size_t>(right.pageIndex);
        }

        return false;
    };
    PDFAlgorithmLongestCommonSubsequence algorithm(leftPreparedPages.cbegin(), leftPreparedPages.cend(),
                                                   rightPreparedPages.cbegin(), rightPreparedPages.cend(),
                                                   comparePages);
    if (!algorithm.perform(m_control))
    {
        if (!algorithm.isOverBudget())
        {
            // Canceled
            return;
        }

        // B191: the pages, which differ, are too many for the matrix of the matching - the pages
        // are paired by their positions, the moved pages are not searched
        pageSequence = PDFDiffHelper::createPositionalSequence(leftPreparedPages, rightPreparedPages);
        PDFAlgorithmLongestCommonSubsequenceBase::markSequence(pageSequence, { }, { });
        return;
    }
    pageSequence = algorithm.getSequence();

    std::vector<size_t> leftUnmatched = PDFDiffHelper::getLeftUnmatched(pageSequence);
    std::vector<size_t> rightUnmatched = PDFDiffHelper::getRightUnmatched(pageSequence);

    // B195: the candidates of each unmatched left page are written by its own task into its own
    // vector, nothing shared is written by the threads
    const std::vector<std::vector<size_t>> candidates = findEqualPages(leftPreparedPages, rightPreparedPages, leftUnmatched, rightUnmatched);
    if (isCanceled())
    {
        return;
    }

    // B160: the moved items are positions in the prepared pages (the indices of the sequence),
    // not the page numbers of the documents - they differ for a selection of pages ("3-10")
    std::vector<size_t> leftPagesMoved;
    std::vector<size_t> rightPagesMoved;

    std::set<size_t> matchedRightPages;
    for (size_t i = 0; i < leftUnmatched.size(); ++i)
    {
        for (const size_t rightContextIndex : candidates[i])
        {
            if (!matchedRightPages.count(rightContextIndex))
            {
                matchedRightPages.insert(rightContextIndex);
                const PDFDiffPageContext& leftPageContext = leftPreparedPages[leftUnmatched[i]];
                const PDFDiffPageContext& rightPageContext = rightPreparedPages[rightContextIndex];

                leftPagesMoved.push_back(leftUnmatched[i]);
                rightPagesMoved.push_back(rightContextIndex);

                pageMatches[leftPageContext.pageIndex] = rightPageContext.pageIndex;

                // B172: a page is matched to one page only
                break;
            }
        }
    }

    if (!pageMatches.empty())
    {
        // The same lengths, the budget of the cells has been checked by the first run
        if (!algorithm.perform(m_control))
        {
            return;
        }
        pageSequence = algorithm.getSequence();
    }

    std::sort(leftPagesMoved.begin(), leftPagesMoved.end());
    std::sort(rightPagesMoved.begin(), rightPagesMoved.end());

    PDFAlgorithmLongestCommonSubsequenceBase::markSequence(pageSequence, leftPagesMoved, rightPagesMoved);
}

std::vector<std::vector<size_t>> PDFDiff::findEqualPages(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                                                         const std::vector<PDFDiffPageContext>& rightPreparedPages,
                                                         const std::vector<size_t>& leftUnmatched,
                                                         const std::vector<size_t>& rightUnmatched) const
{
    std::vector<std::vector<size_t>> candidates(leftUnmatched.size());
    if (leftUnmatched.empty() || rightUnmatched.empty())
    {
        return candidates;
    }

    // B194: the pages are compared only with the pages of the same count of pieces (a necessary
    // condition), each unmatched left page has an equal share of the budget of the work - the
    // result does not depend on the count of the threads
    std::map<size_t, std::vector<size_t>> rightByPieceCount;
    for (const size_t rightIndex : rightUnmatched)
    {
        rightByPieceCount[rightPreparedPages[rightIndex].graphicPieces.size()].push_back(rightIndex);
    }
    const qint64 workPerPage = qMax<qint64>(1, PDFDiffHelper::MAXIMUM_PAGE_MATCHING_WORK / qint64(leftUnmatched.size()));

    std::vector<size_t> tasks(leftUnmatched.size());
    std::iota(tasks.begin(), tasks.end(), size_t(0));

    auto matchLeftPage = [&, this](size_t task)
    {
        if (isCanceled())
        {
            return;
        }

        const PDFDiffPageContext& leftPageContext = leftPreparedPages[leftUnmatched[task]];
        auto it = rightByPieceCount.find(leftPageContext.graphicPieces.size());
        if (it == rightByPieceCount.cend())
        {
            return;
        }

        const PDFPage* page = m_leftDocument->getCatalog()->getPage(leftPageContext.pageIndex);
        const PDFReal epsilon = calculateEpsilonForPage(page);

        qint64 work = workPerPage;
        for (const size_t rightIndex : it->second)
        {
            if (work <= 0)
            {
                break;
            }

            if (PDFDiffHelper::isEqualContent(leftPageContext, rightPreparedPages[rightIndex], epsilon, &work))
            {
                // Jakub Melka: we have a match
                candidates[task].push_back(rightIndex);
            }
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, tasks.cbegin(), tasks.cend(), matchLeftPage);
    return candidates;
}

void PDFDiff::performSteps(const std::vector<PDFInteger>& leftPages,
                           const std::vector<PDFInteger>& rightPages,
                           PDFDiffResult& result)
{
    std::vector<PDFDiffPageContext> leftPreparedPages;
    std::vector<PDFDiffPageContext> rightPreparedPages;

    PDFDiffHelper::PageSequence pageSequence;
    std::map<size_t, size_t> pageMatches; // Indices are real page indices, not indices to page contexts

    auto createDiffPageContext = [](auto pageIndex)
    {
       PDFDiffPageContext context;
       context.pageIndex = pageIndex;
       return context;
    };
    std::transform(leftPages.cbegin(), leftPages.cend(), std::back_inserter(leftPreparedPages), createDiffPageContext);
    std::transform(rightPages.cbegin(), rightPages.cend(), std::back_inserter(rightPreparedPages), createDiffPageContext);

    // StepExtractContentLeftDocument, StepExtractContentRightDocument: the pages are compiled with
    // the cancellation of the run (B193: a page of a hostile content stream stops inside its operators)
    auto extractContent = [this](const PDFDocument* document, std::vector<PDFDiffPageContext>& preparedPages)
    {
        PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        PDFICCProfileCache iccProfileCache;
        PDFOptionalContentActivity optionalContentActivity(document, pdf::OCUsage::View, nullptr);
        fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity));
        iccProfileCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity));

        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(document);
        PDFCMSPointer cms = cmsManager.getCurrentCMS();

        auto fillPageContext = [&, this](PDFDiffPageContext& context)
        {
            if (isCanceled())
            {
                return;
            }

            PDFPrecompiledPage compiledPage;
            constexpr PDFRenderer::Features features = PDFRenderer::IgnoreOptionalContent;
            PDFRenderer renderer(document, &fontCache, &iccProfileCache, cms.data(), &optionalContentActivity, features, pdf::PDFMeshQualitySettings());
            renderer.setOperationControl(m_control);
            renderer.compile(&compiledPage, context.pageIndex);
            if (isCanceled())
            {
                return;
            }

            const PDFPage* page = document->getCatalog()->getPage(context.pageIndex);
            PDFReal epsilon = calculateEpsilonForPage(page);
            context.graphicPieces = compiledPage.calculateGraphicPieceInfos(page->getMediaBox(), epsilon);

            finalizeGraphicsPieces(context);
            PDFDiffHelper::preparePieceElements(context);
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, preparedPages.begin(), preparedPages.end(), fillPageContext);
        stepProgress();
    };

    if (!isCanceled())
    {
        extractContent(m_leftDocument, leftPreparedPages);
    }

    if (!isCanceled())
    {
        extractContent(m_rightDocument, rightPreparedPages);
    }

    // StepMatchPages
    if (!isCanceled())
    {
        performPageMatching(leftPreparedPages, rightPreparedPages, pageSequence, pageMatches);
        stepProgress();
    }

    // StepExtractTextLeftDocument
    if (!isCanceled())
    {
        pdf::PDFDocumentTextFlowFactory factoryLeftDocumentTextFlow;
        factoryLeftDocumentTextFlow.setCalculateBoundingBoxes(true);
        factoryLeftDocumentTextFlow.setOperationControl(m_control);
        PDFDocumentTextFlow leftTextFlow = factoryLeftDocumentTextFlow.create(m_leftDocument, leftPages, m_textAnalysisAlgorithm);
        std::map<PDFInteger, PDFDocumentTextFlow> splittedText = leftTextFlow.split(PDFDocumentTextFlow::Text);
        for (PDFDiffPageContext& leftContext : leftPreparedPages)
        {
            auto it = splittedText.find(leftContext.pageIndex);
            if (it != splittedText.cend())
            {
                leftContext.text = std::move(it->second);
                splittedText.erase(it);
            }
        }
        stepProgress();
    }

    // StepExtractTextRightDocument
    if (!isCanceled())
    {
        pdf::PDFDocumentTextFlowFactory factoryRightDocumentTextFlow;
        factoryRightDocumentTextFlow.setCalculateBoundingBoxes(true);
        factoryRightDocumentTextFlow.setOperationControl(m_control);
        PDFDocumentTextFlow rightTextFlow = factoryRightDocumentTextFlow.create(m_rightDocument, rightPages, m_textAnalysisAlgorithm);
        std::map<PDFInteger, PDFDocumentTextFlow> splittedText = rightTextFlow.split(PDFDocumentTextFlow::Text);
        for (PDFDiffPageContext& rightContext : rightPreparedPages)
        {
            auto it = splittedText.find(rightContext.pageIndex);
            if (it != splittedText.cend())
            {
                rightContext.text = std::move(it->second);
                splittedText.erase(it);
            }
        }
        stepProgress();
    }

    // StepCompare
    if (!isCanceled())
    {
        performCompare(leftPreparedPages, rightPreparedPages, pageSequence, pageMatches, result);
        stepProgress();
    }
}

void PDFDiff::performCompare(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                             const std::vector<PDFDiffPageContext>& rightPreparedPages,
                             PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                             const std::map<size_t, size_t>& pageMatches,
                             PDFDiffResult& result)
{
    using AlgorithmLCS = PDFAlgorithmLongestCommonSubsequenceBase;

    auto modifiedRanges = AlgorithmLCS::getModifiedRanges(pageSequence);
    PDFDiffResult::PageSequence resultPageSequence;
    resultPageSequence.reserve(pageSequence.size());

    // First find all moved pages. A moved page is a page out of the order of the other document
    // (MovedLeft, found by the matching); a matched page, whose number only shifts by the pages
    // added or removed before it (Moved), is no difference of its own - else every page after an
    // inserted page would be reported as moved.
    for (const AlgorithmLCS::SequenceItem& item : pageSequence)
    {
        if (item.isMovedLeft())
        {
            const PDFInteger leftIndex = leftPreparedPages[item.index1].pageIndex;
            auto itMatch = pageMatches.find(size_t(leftIndex));
            if (itMatch != pageMatches.cend())
            {
                result.addPageMoved(leftIndex, PDFInteger(itMatch->second));
            }
        }

        PDFDiffResult::PageSequenceItem pageSequenceItem;
        if (item.isLeftValid())
        {
            const PDFInteger leftIndex = leftPreparedPages[item.index1].pageIndex;
            pageSequenceItem.leftPage = leftIndex;
        }
        if (item.isRightValid())
        {
            const PDFInteger rightIndex = rightPreparedPages[item.index2].pageIndex;
            pageSequenceItem.rightPage = rightIndex;
        }
        resultPageSequence.emplace_back(pageSequenceItem);
    }
    result.setPageSequence(std::move(resultPageSequence));

    std::vector<PDFDiffHelper::TextFlowDifferences> textFlowDifferences;

    for (const auto& range : modifiedRanges)
    {
        AlgorithmLCS::SequenceItemFlags flags = AlgorithmLCS::collectFlags(range);

        const bool isAdded = flags.testFlag(AlgorithmLCS::Added);
        const bool isRemoved = flags.testFlag(AlgorithmLCS::Removed);
        const bool isReplaced = flags.testFlag(AlgorithmLCS::Replaced);

        Q_ASSERT(isAdded || isRemoved || isReplaced);

        // There are two cases. Some page content was replaced, or either
        // page range was added, or page range was removed.
        if (isReplaced)
        {
            PDFDiffHelper::TextFlowDifferences textFlowDifference;
            PDFDocumentTextFlow& leftTextFlow = textFlowDifference.leftTextFlow;
            PDFDocumentTextFlow& rightTextFlow = textFlowDifference.rightTextFlow;

            const bool isTextComparedAsVectorGraphics = m_options.testFlag(CompareTextsAsVector);

            for (auto it = range.first; it != range.second; ++it)
            {
                if (isCanceled())
                {
                    return;
                }

                const AlgorithmLCS::SequenceItem& item = *it;
                if (item.isReplaced() && item.isMatch())
                {
                    const PDFDiffPageContext& leftPageContext = leftPreparedPages[item.index1];
                    const PDFDiffPageContext& rightPageContext = rightPreparedPages[item.index2];

                    if (!isTextComparedAsVectorGraphics)
                    {
                        leftTextFlow.append(leftPageContext.text);
                        rightTextFlow.append(rightPageContext.text);
                        textFlowDifference.pages.emplace_back(&leftPageContext, &rightPageContext);
                    }

                    auto pageLeft = m_leftDocument->getCatalog()->getPage(leftPageContext.pageIndex);
                    auto pageRight = m_rightDocument->getCatalog()->getPage(rightPageContext.pageIndex);
                    PDFReal epsilon = (calculateEpsilonForPage(pageLeft) + calculateEpsilonForPage(pageRight)) * 0.5;

                    PDFDiffHelper::Differences differences = PDFDiffHelper::calculateDifferences(leftPageContext, rightPageContext, epsilon, !isTextComparedAsVectorGraphics);

                    for (const size_t pieceIndex : differences.left)
                    {
                        const PDFDiffHelper::GraphicPieceInfo& info = leftPageContext.graphicPieces[pieceIndex];
                        switch (info.type)
                        {
                            case PDFDiffHelper::GraphicPieceInfo::Type::Text:
                                if (isTextComparedAsVectorGraphics)
                                {
                                    result.addRemovedTextCharContent(leftPageContext.pageIndex, info.boundingRect);
                                }
                                break;

                            case PDFDiffHelper::GraphicPieceInfo::Type::VectorGraphics:
                                result.addRemovedVectorGraphicContent(leftPageContext.pageIndex, info.boundingRect);
                                break;

                            case PDFDiffHelper::GraphicPieceInfo::Type::Image:
                                result.addRemovedImageContent(leftPageContext.pageIndex, info.boundingRect);
                                break;

                            case PDFDiffHelper::GraphicPieceInfo::Type::Shading:
                                result.addRemovedShadingContent(leftPageContext.pageIndex, info.boundingRect);
                                break;

                            default:
                                Q_ASSERT(false);
                                break;
                        }
                    }

                    for (const size_t pieceIndex : differences.right)
                    {
                        const PDFDiffHelper::GraphicPieceInfo& info = rightPageContext.graphicPieces[pieceIndex];
                        switch (info.type)
                        {
                            case PDFDiffHelper::GraphicPieceInfo::Type::Text:
                                if (isTextComparedAsVectorGraphics)
                                {
                                    result.addAddedTextCharContent(rightPageContext.pageIndex, info.boundingRect);
                                }
                                break;

                            case PDFDiffHelper::GraphicPieceInfo::Type::VectorGraphics:
                                result.addAddedVectorGraphicContent(rightPageContext.pageIndex, info.boundingRect);
                                break;

                            case PDFDiffHelper::GraphicPieceInfo::Type::Image:
                                result.addAddedImageContent(rightPageContext.pageIndex, info.boundingRect);
                                break;

                            case PDFDiffHelper::GraphicPieceInfo::Type::Shading:
                                result.addAddedShadingContent(rightPageContext.pageIndex, info.boundingRect);
                                break;

                            default:
                                Q_ASSERT(false);
                                break;
                        }
                    }
                }

                if (item.isAdded())
                {
                    const PDFDiffPageContext& rightPageContext = rightPreparedPages[item.index2];

                    if (!isTextComparedAsVectorGraphics)
                    {
                        rightTextFlow.append(rightPageContext.text);
                        textFlowDifference.pages.emplace_back(nullptr, &rightPageContext);
                    }

                    result.addPageAdded(rightPageContext.pageIndex);
                }
                if (item.isRemoved())
                {
                    const PDFDiffPageContext& leftPageContext = leftPreparedPages[item.index1];

                    if (!isTextComparedAsVectorGraphics)
                    {
                        leftTextFlow.append(leftPageContext.text);
                        textFlowDifference.pages.emplace_back(&leftPageContext, nullptr);
                    }

                    result.addPageRemoved(leftPageContext.pageIndex);
                }
            }

            // The same text gives no difference
            if (leftTextFlow.getText() != rightTextFlow.getText())
            {
                textFlowDifferences.emplace_back(std::move(textFlowDifference));
            }
        }
        else
        {
            for (auto it = range.first; it != range.second; ++it)
            {
                const AlgorithmLCS::SequenceItem& item = *it;
                Q_ASSERT(item.isAdded() || item.isRemoved());

                if (item.isAdded())
                {
                    result.addPageAdded(rightPreparedPages[item.index2].pageIndex);
                }
                if (item.isRemoved())
                {
                    result.addPageRemoved(leftPreparedPages[item.index1].pageIndex);
                }
            }
        }
    }

    QMutex mutex;
    const bool isWordsComparingMode = m_options.testFlag(CompareWords);

    // Jakub Melka: try to compare text differences. B191, R7-6: the joined text of a range of changed
    // pages is compared within the budget of the cells (the text, which flows over the pages, is no
    // difference on both of them); a range over the budget is compared by its pairs of pages, each
    // pair over the budget is one difference "too large to compare word by word". The threads write
    // only their own item of a vector (B195), the result under the mutex.
    std::vector<char> isRangeOverBudget(textFlowDifferences.size(), 0);
    std::vector<size_t> rangeIndices(textFlowDifferences.size());
    std::iota(rangeIndices.begin(), rangeIndices.end(), size_t(0));

    auto compareRange = [&, this](size_t rangeIndex)
    {
        if (isCanceled())
        {
            return;
        }

        const PDFDiffHelper::TextFlowDifferences& context = textFlowDifferences[rangeIndex];
        if (!PDFDiffHelper::compareTextFlows(context.leftTextFlow, context.rightTextFlow, isWordsComparingMode, m_control, &mutex, &result) && !isCanceled())
        {
            isRangeOverBudget[rangeIndex] = 1;
        }
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, rangeIndices.cbegin(), rangeIndices.cend(), compareRange);

    std::vector<PDFDiffHelper::TextPairJob> pairJobs;
    for (size_t i = 0; i < textFlowDifferences.size(); ++i)
    {
        if (isRangeOverBudget[i])
        {
            for (const auto& pages : textFlowDifferences[i].pages)
            {
                pairJobs.push_back({ pages.first, pages.second });
            }
        }
    }

    auto comparePair = [&, this](const PDFDiffHelper::TextPairJob& job)
    {
        if (isCanceled())
        {
            return;
        }

        const PDFDocumentTextFlow emptyTextFlow;
        const PDFDocumentTextFlow& leftTextFlow = job.left ? job.left->text : emptyTextFlow;
        const PDFDocumentTextFlow& rightTextFlow = job.right ? job.right->text : emptyTextFlow;
        if (leftTextFlow.getText() == rightTextFlow.getText())
        {
            return;
        }

        if (!PDFDiffHelper::compareTextFlows(leftTextFlow, rightTextFlow, isWordsComparingMode, m_control, &mutex, &result) && !isCanceled() && job.left && job.right)
        {
            // Only a pair of two texts can be over the budget (one text alone needs no matrix)
            PDFDiffResult::RectInfos leftRectInfos = PDFDiffHelper::getTextRectangles(leftTextFlow);
            PDFDiffResult::RectInfos rightRectInfos = PDFDiffHelper::getTextRectangles(rightTextFlow);

            QMutexLocker locker(&mutex);
            result.addTextTooLarge(job.left->pageIndex, job.right->pageIndex, leftRectInfos, rightRectInfos);
        }
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pairJobs.cbegin(), pairJobs.cend(), comparePair);

    // Jakub Melka: sort results
    result.finalize();
}

bool PDFDiffHelper::compareTextFlows(const PDFDocumentTextFlow& leftTextFlow,
                                     const PDFDocumentTextFlow& rightTextFlow,
                                     bool isWordsComparingMode,
                                     const PDFOperationControl* control,
                                     QMutex* mutex,
                                     PDFDiffResult* result)
{
    // The two text flows; an item of the comparison says which one it belongs to
    struct TextFlows
    {
        const PDFDocumentTextFlow& leftTextFlow;
        const PDFDocumentTextFlow& rightTextFlow;
    };
    const TextFlows context = { leftTextFlow, rightTextFlow };

    std::vector<TextCompareItem> leftItems = prepareTextCompareItems(context.leftTextFlow, isWordsComparingMode, true);
    std::vector<TextCompareItem> rightItems = prepareTextCompareItems(context.rightTextFlow, isWordsComparingMode, false);

    auto compareCharacters = [&](const TextCompareItem& a, const TextCompareItem& b)
    {
        const auto& aItem = a.left ? context.leftTextFlow : context.rightTextFlow;
        const auto& bItem = b.left ? context.leftTextFlow : context.rightTextFlow;

        QStringView aText(aItem.getItem(a.index)->text);
        aText = aText.mid(a.charIndex, a.charCount);

        QStringView bText(bItem.getItem(b.index)->text);
        bText = bText.mid(b.charIndex, b.charCount);

        return aText == bText;
    };
    PDFAlgorithmLongestCommonSubsequence algorithm(leftItems.cbegin(), leftItems.cend(),
                                                   rightItems.cbegin(), rightItems.cend(),
                                                   compareCharacters);

    // B191, R7-6: over the budget of the cells (or canceled) nothing is added
    if (!algorithm.perform(control))
    {
        return false;
    }

    PDFAlgorithmLongestCommonSubsequenceBase::Sequence sequence = algorithm.getSequence();
    PDFAlgorithmLongestCommonSubsequenceBase::markSequence(sequence, { }, { });
    PDFAlgorithmLongestCommonSubsequenceBase::SequenceItemRanges modifiedRanges = PDFAlgorithmLongestCommonSubsequenceBase::getModifiedRanges(sequence);

    // Merge modified sequences separated by just space
    if (!isWordsComparingMode && !modifiedRanges.empty())
    {
        auto itPrev = sequence.end();
        for (const auto& range : modifiedRanges)
        {
            if (itPrev != sequence.end())
            {
                auto itNext = range.first;

                bool isReplaced = true;
                for (auto it = itPrev; it != itNext && isReplaced; ++it)
                {
                    const PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem& item = *it;

                    // If we doesn't have a match, then it is not a whitespace
                    if (!item.isMatch())
                    {
                        isReplaced = false;
                        break;
                    }

                    const TextCompareItem& compareItem = leftItems[item.index1];
                    const auto& flowItem = compareItem.left ? context.leftTextFlow : context.rightTextFlow;
                    QChar character = flowItem.getItem(compareItem.index)->text.at(compareItem.charIndex);

                    isReplaced = !character.isSpace();
                }

                if (isReplaced)
                {
                    for (auto it = itPrev; it != itNext; ++it)
                    {
                        PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem& item = *it;
                        item.markReplaced();
                    }
                }
            }

            itPrev = range.second;
        }

        modifiedRanges = PDFAlgorithmLongestCommonSubsequenceBase::getModifiedRanges(sequence);
    }

    for (const auto& range : modifiedRanges)
    {
        auto it = range.first;
        auto itEnd = range.second;

        QStringList leftStrings;
        QStringList rightStrings;

        PDFDiffResult::RectInfos leftRectInfos;
        PDFDiffResult::RectInfos rightRectInfos;

        PDFInteger pageIndex1 = -1;
        PDFInteger pageIndex2 = -1;

        for (; it != itEnd; ++it)
        {
            const PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem& item = *it;

            if (item.isLeftValid())
            {
                const TextCompareItem& textCompareItem = leftItems[item.index1];
                const auto& textFlow = textCompareItem.left ? context.leftTextFlow : context.rightTextFlow;
                const PDFDocumentTextFlow::Item* textItem = textFlow.getItem(textCompareItem.index);

                QStringView text(textItem->text);
                text = text.mid(textCompareItem.charIndex, textCompareItem.charCount);

                leftStrings << text.toString();

                if (pageIndex1 == -1)
                {
                    pageIndex1 = textItem->pageIndex;
                }

                if (static_cast<std::size_t>(textCompareItem.charIndex) + textCompareItem.charCount <= textItem->characterBoundingRects.size())
                {
                    const size_t startIndex = textCompareItem.charIndex;
                    const size_t endIndex = startIndex + textCompareItem.charCount;

                    for (size_t i = startIndex; i < endIndex; ++i)
                    {
                        leftRectInfos.emplace_back(textItem->pageIndex, textItem->characterBoundingRects[i]);
                    }
                }
            }

            if (item.isRightValid())
            {
                const TextCompareItem& textCompareItem = rightItems[item.index2];
                const auto& textFlow = textCompareItem.left ? context.leftTextFlow : context.rightTextFlow;
                const PDFDocumentTextFlow::Item* textItem = textFlow.getItem(textCompareItem.index);

                QStringView text(textItem->text);
                text = text.mid(textCompareItem.charIndex, textCompareItem.charCount);

                rightStrings << text.toString();

                if (pageIndex2 == -1)
                {
                    pageIndex2 = textItem->pageIndex;
                }

                if (static_cast<std::size_t>(textCompareItem.charIndex) + textCompareItem.charCount <= textItem->characterBoundingRects.size())
                {
                    const size_t startIndex = textCompareItem.charIndex;
                    const size_t endIndex = startIndex + textCompareItem.charCount;

                    for (size_t i = startIndex; i < endIndex; ++i)
                    {
                        rightRectInfos.emplace_back(textItem->pageIndex, textItem->characterBoundingRects[i]);
                    }
                }
            }
        }

        QString leftString;
        QString rightString;

        if (isWordsComparingMode)
        {
            leftString = leftStrings.join(QChar::Space);
            rightString = rightStrings.join(QChar::Space);
        }
        else
        {
            leftString = leftStrings.join(QString());
            rightString = rightStrings.join(QString());
        }

        refineTextRectangles(leftRectInfos);
        refineTextRectangles(rightRectInfos);

        QMutexLocker locker(mutex);
        if (!leftString.isEmpty() && !rightString.isEmpty())
        {
            result->addTextReplaced(pageIndex1, pageIndex2, leftString, rightString, leftRectInfos, rightRectInfos);
        }
        else
        {
            if (!leftString.isEmpty())
            {
                result->addTextRemoved(pageIndex1, leftString, leftRectInfos);
            }

            if (!rightString.isEmpty())
            {
                result->addTextAdded(pageIndex2, rightString, rightRectInfos);
            }
        }
    }

    return true;
}

PDFDiffResult::RectInfos PDFDiffHelper::getTextRectangles(const PDFDocumentTextFlow& textFlow)
{
    std::map<PDFInteger, QRectF> rectangles;
    for (const PDFDocumentTextFlow::Item& item : textFlow.getItems())
    {
        if (item.isText() && item.boundingRect.isValid())
        {
            QRectF& rectangle = rectangles[item.pageIndex];
            rectangle = rectangle.united(item.boundingRect);
        }
    }

    return PDFDiffResult::RectInfos(rectangles.cbegin(), rectangles.cend());
}

void PDFDiff::finalizeGraphicsPieces(PDFDiffPageContext& context)
{
    std::sort(context.graphicPieces.begin(), context.graphicPieces.end());

    // Compute page hash using active settings
    QCryptographicHash hasher(QCryptographicHash::Sha512);
    hasher.reset();

    for (const PDFPrecompiledPage::GraphicPieceInfo& info : context.graphicPieces)
    {
        if (info.isText() && !m_options.testFlag(PC_Text))
        {
            continue;
        }
        if (info.isVectorGraphics() && !m_options.testFlag(PC_VectorGraphics))
        {
            continue;
        }
        if (info.isImage() && !m_options.testFlag(PC_Images))
        {
            continue;
        }
        if (info.isShading() && !m_options.testFlag(PC_Mesh))
        {
            continue;
        }

        QByteArrayView view(reinterpret_cast<const char*>(info.hash.data()), info.hash.size());
        hasher.addData(view);
    }

    QByteArray hash = hasher.result();
    Q_ASSERT(QCryptographicHash::hashLength(QCryptographicHash::Sha512) == 64);

    size_t size = qMin<size_t>(hash.length(), context.pageHash.size());
    std::copy(hash.data(), hash.data() + size, context.pageHash.data());
}

void PDFDiff::onComparationPerformed()
{
    m_cancelControl.canceled = false;
    m_result = m_future.result();
    Q_EMIT comparationFinished();
}

PDFReal PDFDiff::calculateEpsilonForPage(const PDFPage* page) const
{
    Q_ASSERT(page);

    QRectF mediaBox = page->getMediaBox();

    PDFReal width = mediaBox.width();
    PDFReal height = mediaBox.height();
    PDFReal factor = qMax(width, height);

    return factor * m_epsilon;
}

PDFDocumentTextFlowFactory::Algorithm PDFDiff::getTextAnalysisAlgorithm() const
{
    return m_textAnalysisAlgorithm;
}

void PDFDiff::setTextAnalysisAlgorithm(PDFDocumentTextFlowFactory::Algorithm textAnalysisAlgorithm)
{
    m_textAnalysisAlgorithm = textAnalysisAlgorithm;
}

PDFDiffResult::PDFDiffResult() :
    m_result(true)
{

}

void PDFDiffResult::addPageMoved(PDFInteger pageIndex1, PDFInteger pageIndex2)
{
    Difference difference;

    difference.type = Type::PageMoved;
    difference.pageIndex1 = pageIndex1;
    difference.pageIndex2 = pageIndex2;

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addPageAdded(PDFInteger pageIndex)
{
    Difference difference;

    difference.type = Type::PageAdded;
    difference.pageIndex2 = pageIndex;

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addPageRemoved(PDFInteger pageIndex)
{
    Difference difference;

    difference.type = Type::PageRemoved;
    difference.pageIndex1 = pageIndex;

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addLeftItem(Type type, PDFInteger pageIndex, QRectF rect)
{
    Difference difference;

    difference.type = type;
    difference.pageIndex1 = pageIndex;
    addRectLeft(difference, rect);

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addRightItem(Type type, PDFInteger pageIndex, QRectF rect)
{
    Difference difference;

    difference.type = type;
    difference.pageIndex2 = pageIndex;
    addRectRight(difference, rect);

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addRemovedTextCharContent(PDFInteger pageIndex, QRectF rect)
{
    addLeftItem(Type::RemovedTextCharContent, pageIndex, rect);
}

void PDFDiffResult::addRemovedVectorGraphicContent(PDFInteger pageIndex, QRectF rect)
{
    addLeftItem(Type::RemovedVectorGraphicContent, pageIndex, rect);
}

void PDFDiffResult::addRemovedImageContent(PDFInteger pageIndex, QRectF rect)
{
    addLeftItem(Type::RemovedImageContent, pageIndex, rect);
}

void PDFDiffResult::addRemovedShadingContent(PDFInteger pageIndex, QRectF rect)
{
    addLeftItem(Type::RemovedShadingContent, pageIndex, rect);
}

void PDFDiffResult::addAddedTextCharContent(PDFInteger pageIndex, QRectF rect)
{
    addRightItem(Type::AddedTextCharContent, pageIndex, rect);
}

void PDFDiffResult::addAddedVectorGraphicContent(PDFInteger pageIndex, QRectF rect)
{
    addRightItem(Type::AddedVectorGraphicContent, pageIndex, rect);
}

void PDFDiffResult::addAddedImageContent(PDFInteger pageIndex, QRectF rect)
{
    addRightItem(Type::AddedImageContent, pageIndex, rect);
}

void PDFDiffResult::addAddedShadingContent(PDFInteger pageIndex, QRectF rect)
{
    addRightItem(Type::AddedShadingContent, pageIndex, rect);
}

void PDFDiffResult::addTextAdded(PDFInteger pageIndex,
                                 QString text,
                                 const RectInfos& rectInfos)
{
    Difference difference;

    difference.type = Type::TextAdded;
    difference.pageIndex2 = pageIndex;
    difference.textAddedIndex = m_strings.size();
    m_strings << text;
    difference.rightRectIndex = m_rects.size();
    difference.rightRectCount = rectInfos.size();
    m_rects.insert(m_rects.end(), rectInfos.cbegin(), rectInfos.cend());

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addTextRemoved(PDFInteger pageIndex,
                                   QString text,
                                   const RectInfos& rectInfos)
{
    Difference difference;

    difference.type = Type::TextRemoved;
    difference.pageIndex1 = pageIndex;
    difference.textRemovedIndex = m_strings.size();
    m_strings << text;
    difference.leftRectIndex = m_rects.size();
    difference.leftRectCount = rectInfos.size();
    m_rects.insert(m_rects.end(), rectInfos.cbegin(), rectInfos.cend());

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addTextReplaced(PDFInteger pageIndex1,
                                    PDFInteger pageIndex2,
                                    QString textRemoved,
                                    QString textAdded,
                                    const RectInfos& rectInfos1,
                                    const RectInfos& rectInfos2)
{
    Difference difference;

    difference.type = Type::TextReplaced;
    difference.pageIndex1 = pageIndex1;
    difference.pageIndex2 = pageIndex2;
    difference.textRemovedIndex = m_strings.size();
    m_strings << textRemoved;
    difference.textAddedIndex = m_strings.size();
    m_strings << textAdded;

    difference.leftRectIndex = m_rects.size();
    difference.leftRectCount = rectInfos1.size();
    m_rects.insert(m_rects.end(), rectInfos1.cbegin(), rectInfos1.cend());

    difference.rightRectIndex = m_rects.size();
    difference.rightRectCount = rectInfos2.size();
    m_rects.insert(m_rects.end(), rectInfos2.cbegin(), rectInfos2.cend());

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::addTextTooLarge(PDFInteger pageIndex1,
                                    PDFInteger pageIndex2,
                                    const RectInfos& rectInfos1,
                                    const RectInfos& rectInfos2)
{
    Difference difference;

    difference.type = Type::TextReplaced;
    difference.pageIndex1 = pageIndex1;
    difference.pageIndex2 = pageIndex2;
    difference.isTextTooLarge = true;

    difference.leftRectIndex = m_rects.size();
    difference.leftRectCount = rectInfos1.size();
    m_rects.insert(m_rects.end(), rectInfos1.cbegin(), rectInfos1.cend());

    difference.rightRectIndex = m_rects.size();
    difference.rightRectCount = rectInfos2.size();
    m_rects.insert(m_rects.end(), rectInfos2.cbegin(), rectInfos2.cend());

    m_differences.emplace_back(std::move(difference));
}

void PDFDiffResult::saveToStream(QXmlStreamWriter* stream) const
{
    stream->setAutoFormatting(true);
    stream->setAutoFormattingIndent(2);
    stream->writeStartDocument();
    stream->writeNamespace("https://github.com/JakubMelka/PDF4QT", "pdf4qt");
    stream->writeStartElement("difference-report");

    // Jakub Melka: write all differences
    stream->writeStartElement("differences");
    for (const Difference& difference : m_differences)
    {
        stream->writeStartElement("difference");

        QString type;
        switch (difference.type)
        {
            case Type::PageMoved:
                type = "page-moved";
                break;

            case Type::PageAdded:
                type = "page-added";
                break;

            case Type::PageRemoved:
                type = "page-removed";
                break;

            case Type::RemovedTextCharContent:
                type = "removed-text-char";
                break;

            case Type::RemovedVectorGraphicContent:
                type = "removed-vector-graphics";
                break;

            case Type::RemovedImageContent:
                type = "removed-image";
                break;

            case Type::RemovedShadingContent:
                type = "removed-shading";
                break;

            case Type::AddedTextCharContent:
                type = "added-text-char";
                break;

            case Type::AddedVectorGraphicContent:
                type = "added-vector-graphics";
                break;

            case Type::AddedImageContent:
                type = "added-image";
                break;

            case Type::AddedShadingContent:
                type = "added-shading";
                break;

            case Type::TextAdded:
                type = "text-added";
                break;

            case Type::TextRemoved:
                type = "text-removed";
                break;

            case Type::TextReplaced:
                type = "text-replaced";
                break;

            default:
                Q_ASSERT(false);
                break;
        }
        stream->writeAttribute("type", type);

        if (difference.pageIndex1 != -1)
        {
            stream->writeAttribute("left", QString::number(difference.pageIndex1 + 1));
        }

        if (difference.pageIndex2 != -1)
        {
            stream->writeAttribute("right", QString::number(difference.pageIndex2 + 1));
        }

        if (difference.isTextTooLarge)
        {
            stream->writeAttribute("text-too-large", "true");
        }

        // The text of a document may hold characters, which XML 1.0 does not allow (a control character, a lone
        // surrogate): they are replaced by U+FFFD, the file is always well-formed (the rule of the table writer)
        if (difference.textAddedIndex != -1)
        {
            stream->writeTextElement("text-added", PDFDocumentToolTableWriter::toXmlText(m_strings[difference.textAddedIndex]));
        }

        if (difference.textRemovedIndex != -1)
        {
            stream->writeTextElement("text-removed", PDFDocumentToolTableWriter::toXmlText(m_strings[difference.textRemovedIndex]));
        }

        // Step 7c of phase 2: the rectangles of the difference in points of their pages (C locale);
        // a reader of the old form skips the new element
        auto writeRectangles = [stream, this](size_t index, size_t count, const char* side)
        {
            for (size_t i = index; i < index + count && i < m_rects.size(); ++i)
            {
                const QRectF& rect = m_rects[i].second;
                stream->writeStartElement("rect");
                stream->writeAttribute("side", QString::fromLatin1(side));
                stream->writeAttribute("page", QString::number(m_rects[i].first + 1));
                stream->writeAttribute("x", QString::number(rect.x(), 'g', 10));
                stream->writeAttribute("y", QString::number(rect.y(), 'g', 10));
                stream->writeAttribute("width", QString::number(rect.width(), 'g', 10));
                stream->writeAttribute("height", QString::number(rect.height(), 'g', 10));
                stream->writeEndElement();
            }
        };
        writeRectangles(difference.leftRectIndex, difference.leftRectCount, "left");
        writeRectangles(difference.rightRectIndex, difference.rightRectCount, "right");

        stream->writeEndElement();
    }
    stream->writeEndElement();

    stream->writeStartElement("page-sequence");
    for (const PageSequenceItem& item : m_pageSequence)
    {
        stream->writeStartElement("item");

        QString left = item.leftPage != -1 ? QString::number(item.leftPage + 1) : QString("none");
        QString right = item.rightPage != -1 ? QString::number(item.rightPage + 1) : QString("none");

        stream->writeAttribute("left", left);
        stream->writeAttribute("right", right);

        stream->writeEndElement();
    }
    stream->writeEndElement();

    stream->writeEndElement();
    stream->writeEndDocument();
}

void PDFDiffResult::finalize()
{
    auto predicate = [](const Difference& l, const Difference& r)
    {
        return qMax(l.pageIndex1, l.pageIndex2) < qMax(r.pageIndex1, r.pageIndex2);
    };

    std::stable_sort(m_differences.begin(), m_differences.end(), predicate);
    updateTypeFlags();
}

void PDFDiffResult::updateTypeFlags()
{
    m_typeFlags = 0;
    for (const Difference& difference : m_differences)
    {
        m_typeFlags |= static_cast<uint32_t>(difference.type);
    }
}

uint32_t PDFDiffResult::getTypeFlags(size_t index) const
{
    if (index >= m_differences.size())
    {
        return 0;
    }

    return uint32_t(m_differences[index].type);
}

QString PDFDiffResult::getMessage(size_t index) const
{
    if (index >= m_differences.size())
    {
        return QString();
    }

    const Difference& difference = m_differences[index];
    if (difference.isTextTooLarge || (difference.type != Type::TextAdded && difference.type != Type::TextRemoved && difference.type != Type::TextReplaced))
    {
        // The same text with or without the text of the documents
        return getDescription(index);
    }

    switch (difference.type)
    {
        case Type::TextAdded:
            return PDFDiff::tr("Text '%1' has been added to page %2.").arg(m_strings[difference.textAddedIndex]).arg(difference.pageIndex2 + 1);

        case Type::TextRemoved:
            return PDFDiff::tr("Text '%1' has been removed from page %2.").arg(m_strings[difference.textRemovedIndex]).arg(difference.pageIndex1 + 1);

        case Type::TextReplaced:
            return PDFDiff::tr("Text '%1' on page %2 has been replaced by text '%3' on page %4.").arg(m_strings[difference.textRemovedIndex]).arg(difference.pageIndex1 + 1).arg(m_strings[difference.textAddedIndex]).arg(difference.pageIndex2 + 1);

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PDFDiffResult::getDescription(size_t index) const
{
    if (index >= m_differences.size())
    {
        return QString();
    }

    const Difference& difference = m_differences[index];
    if (difference.isTextTooLarge)
    {
        // B191, R7-6: the text of the pair of pages was not compared word by word
        return PDFDiff::tr("Text of page %1 differs from the text of page %2 (too large to compare word by word).").arg(difference.pageIndex1 + 1).arg(difference.pageIndex2 + 1);
    }

    switch (difference.type)
    {
        case Type::PageMoved:
            return PDFDiff::tr("Page no. %1 was moved to a page no. %2.").arg(difference.pageIndex1 + 1).arg(difference.pageIndex2 + 1);

        case Type::PageAdded:
            return PDFDiff::tr("Page no. %1 was added.").arg(difference.pageIndex2 + 1);

        case Type::PageRemoved:
            return PDFDiff::tr("Page no. %1 was removed.").arg(difference.pageIndex1 + 1);

        case Type::RemovedTextCharContent:
            return PDFDiff::tr("Removed text character from page %1.").arg(difference.pageIndex1 + 1);

        case Type::RemovedVectorGraphicContent:
            return PDFDiff::tr("Removed vector graphics from page %1.").arg(difference.pageIndex1 + 1);

        case Type::RemovedImageContent:
            return PDFDiff::tr("Removed image from page %1.").arg(difference.pageIndex1 + 1);

        case Type::RemovedShadingContent:
            return PDFDiff::tr("Removed shading from page %1.").arg(difference.pageIndex1 + 1);

        case Type::AddedTextCharContent:
            return PDFDiff::tr("Added text character to page %1.").arg(difference.pageIndex2 + 1);

        case Type::AddedVectorGraphicContent:
            return PDFDiff::tr("Added vector graphics to page %1.").arg(difference.pageIndex2 + 1);

        case Type::AddedImageContent:
            return PDFDiff::tr("Added image to page %1.").arg(difference.pageIndex2 + 1);

        case Type::AddedShadingContent:
            return PDFDiff::tr("Added shading to page %1.").arg(difference.pageIndex2 + 1);

        case Type::TextAdded:
            return PDFDiff::tr("Text has been added to page %1.").arg(difference.pageIndex2 + 1);

        case Type::TextRemoved:
            return PDFDiff::tr("Text has been removed from page %1.").arg(difference.pageIndex1 + 1);

        case Type::TextReplaced:
            return PDFDiff::tr("Text on page %1 has been replaced by text on page %2.").arg(difference.pageIndex1 + 1).arg(difference.pageIndex2 + 1);

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

bool PDFDiffResult::isTextTooLarge(size_t index) const
{
    return index < m_differences.size() && m_differences[index].isTextTooLarge;
}

QString PDFDiffResult::getRemovedText(size_t index) const
{
    if (index >= m_differences.size() || m_differences[index].textRemovedIndex < 0)
    {
        return QString();
    }

    return m_strings.value(m_differences[index].textRemovedIndex);
}

QString PDFDiffResult::getAddedText(size_t index) const
{
    if (index >= m_differences.size() || m_differences[index].textAddedIndex < 0)
    {
        return QString();
    }

    return m_strings.value(m_differences[index].textAddedIndex);
}

std::vector<PDFInteger> PDFDiffResult::getTextTooLargeLeftPageIndices() const
{
    std::set<PDFInteger> pageIndices;
    for (const Difference& difference : m_differences)
    {
        if (difference.isTextTooLarge && difference.pageIndex1 >= 0)
        {
            pageIndices.insert(difference.pageIndex1);
        }
    }

    return std::vector<PDFInteger>(pageIndices.cbegin(), pageIndices.cend());
}

PDFInteger PDFDiffResult::getLeftPage(size_t index) const
{
    if (index >= m_differences.size())
    {
        return -1;
    }

    return m_differences[index].pageIndex1;
}

PDFInteger PDFDiffResult::getRightPage(size_t index) const
{
    if (index >= m_differences.size())
    {
        return -1;
    }

    return m_differences[index].pageIndex2;
}

PDFDiffResult::Type PDFDiffResult::getType(size_t index) const
{
    if (index >= m_differences.size())
    {
        return Type::Invalid;
    }

    return m_differences[index].type;
}

QString PDFDiffResult::getTypeDescription(size_t index) const
{
    return getTypeDescription(getType(index));
}

QString PDFDiffResult::getTypeDescription(Type type)
{
    switch (type)
    {
        case Type::Invalid:
            return PDFDiff::tr("Invalid");
        case Type::PageMoved:
            return PDFDiff::tr("Page moved");
        case Type::PageAdded:
            return PDFDiff::tr("Page added");
        case Type::PageRemoved:
            return PDFDiff::tr("Page removed");
        case Type::RemovedTextCharContent:
            return PDFDiff::tr("Removed text character");
        case Type::RemovedVectorGraphicContent:
            return PDFDiff::tr("Removed vector graphics");
        case Type::RemovedImageContent:
            return PDFDiff::tr("Removed image");
        case Type::RemovedShadingContent:
            return PDFDiff::tr("Removed shading");
        case Type::AddedTextCharContent:
            return PDFDiff::tr("Added text character");
        case Type::AddedVectorGraphicContent:
            return PDFDiff::tr("Added vector graphics");
        case Type::AddedImageContent:
            return PDFDiff::tr("Added image");
        case Type::AddedShadingContent:
            return PDFDiff::tr("Added shading");
        case Type::TextAdded:
            return PDFDiff::tr("Text added");
        case Type::TextRemoved:
            return PDFDiff::tr("Text removed");
        case Type::TextReplaced:
            return PDFDiff::tr("Text replaced");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

std::pair<PDFDiffResult::RectInfosIt, PDFDiffResult::RectInfosIt> PDFDiffResult::getLeftRectangles(size_t index) const
{
    if (index >= m_differences.size())
    {
        return std::make_pair(m_rects.cend(), m_rects.cend());
    }

    const Difference& difference = m_differences[index];
    if (difference.leftRectCount > 0)
    {
        auto it = std::next(m_rects.cbegin(), difference.leftRectIndex);
        auto itEnd = std::next(it, difference.leftRectCount);
        return std::make_pair(it, itEnd);
    }

    return std::make_pair(m_rects.cend(), m_rects.cend());
}

std::pair<PDFDiffResult::RectInfosIt, PDFDiffResult::RectInfosIt> PDFDiffResult::getRightRectangles(size_t index) const
{
    if (index >= m_differences.size())
    {
        return std::make_pair(m_rects.cend(), m_rects.cend());
    }

    const Difference& difference = m_differences[index];
    if (difference.rightRectCount > 0)
    {
        auto it = std::next(m_rects.cbegin(), difference.rightRectIndex);
        auto itEnd = std::next(it, difference.rightRectCount);
        return std::make_pair(it, itEnd);
    }

    return std::make_pair(m_rects.cend(), m_rects.cend());
}

bool PDFDiffResult::isPageMoveAddRemoveDifference(size_t index) const
{
    return getTypeFlags(index) & FLAGS_TYPE_PAGE_MOVE_ADD_REMOVE;
}

bool PDFDiffResult::isPageMoveDifference(size_t index) const
{
    return getTypeFlags(index) & FLAGS_TYPE_PAGE_MOVE;
}

bool PDFDiffResult::isAddDifference(size_t index) const
{
    return getTypeFlags(index) & FLAGS_TYPE_ADD;
}

bool PDFDiffResult::isRemoveDifference(size_t index) const
{
    return getTypeFlags(index) & FLAGS_TYPE_REMOVE;
}

bool PDFDiffResult::isReplaceDifference(size_t index) const
{
    return getTypeFlags(index) & FLAGS_TYPE_REPLACE;
}

std::vector<PDFInteger> PDFDiffResult::getChangedLeftPageIndices() const
{
    std::set<PDFInteger> changedPageIndices;

    for (size_t i = 0; i < m_differences.size(); ++i)
    {
        changedPageIndices.insert(getLeftPage(i));
    }
    changedPageIndices.erase(-1);

    return std::vector<PDFInteger>(changedPageIndices.cbegin(), changedPageIndices.cend());
}

std::vector<PDFInteger> PDFDiffResult::getChangedRightPageIndices() const
{
    std::set<PDFInteger> changedPageIndices;

    for (size_t i = 0; i < m_differences.size(); ++i)
    {
        changedPageIndices.insert(getRightPage(i));
    }
    changedPageIndices.erase(-1);

    return std::vector<PDFInteger>(changedPageIndices.cbegin(), changedPageIndices.cend());
}

PDFDiffResult PDFDiffResult::filter(bool filterPageMoveDifferences,
                                    bool filterTextDifferences,
                                    bool filterVectorGraphicsDifferences,
                                    bool filterImageDifferences,
                                    bool filterShadingDifferences) const
{
    PDFDiffResult filteredResult = *this;

    uint32_t typeFlags = 0;

    if (filterPageMoveDifferences)
    {
        typeFlags |= FLAGS_PAGE_MOVE;
    }

    if (filterTextDifferences)
    {
        typeFlags |= FLAGS_TEXT;
    }

    if (filterVectorGraphicsDifferences)
    {
        typeFlags |= FLAGS_VECTOR_GRAPHICS;
    }

    if (filterImageDifferences)
    {
        typeFlags |= FLAGS_IMAGE;
    }

    if (filterShadingDifferences)
    {
        typeFlags |= FLAGS_SHADING;
    }

    auto remove = [typeFlags](const Difference& difference)
    {
        return (uint32_t(difference.type) & typeFlags) == 0;
    };
    filteredResult.m_differences.erase(std::remove_if(filteredResult.m_differences.begin(), filteredResult.m_differences.end(), remove), filteredResult.m_differences.end());
    filteredResult.updateTypeFlags();

    return filteredResult;
}

void PDFDiffResult::addRectLeft(Difference& difference, QRectF rect)
{
    difference.leftRectIndex = m_rects.size();
    difference.leftRectCount = 1;
    m_rects.emplace_back(difference.pageIndex1, rect);
}

void PDFDiffResult::addRectRight(Difference& difference, QRectF rect)
{
    difference.rightRectIndex = m_rects.size();
    difference.rightRectCount = 1;
    m_rects.emplace_back(difference.pageIndex2, rect);
}

const PDFDiffResult::PageSequence& PDFDiffResult::getPageSequence() const
{
    return m_pageSequence;
}

void PDFDiffResult::setPageSequence(PageSequence pageSequence)
{
    m_pageSequence = pageSequence;
}

void PDFDiffResult::saveToXML(QIODevice* device) const
{
    QXmlStreamWriter stream(device);
    saveToStream(&stream);
}

void PDFDiffResult::saveToXML(QByteArray* byteArray) const
{
    QXmlStreamWriter stream(byteArray);
    saveToStream(&stream);
}

void PDFDiffResult::saveToXML(QString* string) const
{
    QXmlStreamWriter stream(string);
    saveToStream(&stream);
}

void PDFDiffHelper::preparePieceElements(PDFDiffPageContext& context)
{
    context.elements.clear();
    context.elementOffsets.clear();
    context.elementOffsets.reserve(context.graphicPieces.size() + 1);
    context.elementOffsets.push_back(0);

    for (GraphicPieceInfo& info : context.graphicPieces)
    {
        const int elementCount = info.pagePath.elementCount();
        for (int i = 0; i < elementCount; ++i)
        {
            context.elements.push_back(info.pagePath.elementAt(i));
        }
        context.elementOffsets.push_back(context.elements.size());

        // The path is never read again (the comparison reads the elements): the pages of both documents are
        // held at once, a path kept beside its copy would double their memory
        info.pagePath = QPainterPath();
    }
}

PDFDiffHelper::Differences PDFDiffHelper::calculateDifferences(const PDFDiffPageContext& left,
                                                               const PDFDiffPageContext& right,
                                                               PDFReal epsilon,
                                                               bool isTextIgnored)
{
    Differences differences;

    const GraphicPieceInfos& leftPieces = left.graphicPieces;
    const GraphicPieceInfos& rightPieces = right.graphicPieces;

    Q_ASSERT(std::is_sorted(leftPieces.cbegin(), leftPieces.cend()));
    Q_ASSERT(std::is_sorted(rightPieces.cbegin(), rightPieces.cend()));

    // The pieces equal by their hashes, as multisets (B194: one walk over the two sorted lists);
    // the text compared by its characters is not paired here (it would take the budget of the
    // pairing from the other pieces)
    size_t leftIndex = 0;
    size_t rightIndex = 0;
    while (leftIndex < leftPieces.size() || rightIndex < rightPieces.size())
    {
        if (rightIndex == rightPieces.size() || (leftIndex < leftPieces.size() && leftPieces[leftIndex] < rightPieces[rightIndex]))
        {
            if (!isTextIgnored || !leftPieces[leftIndex].isText())
            {
                differences.left.push_back(leftIndex);
            }
            ++leftIndex;
        }
        else if (leftIndex == leftPieces.size() || rightPieces[rightIndex] < leftPieces[leftIndex])
        {
            if (!isTextIgnored || !rightPieces[rightIndex].isText())
            {
                differences.right.push_back(rightIndex);
            }
            ++rightIndex;
        }
        else
        {
            ++leftIndex;
            ++rightIndex;
        }
    }

    // If exact match fails, then try to use match with epsilon
    qint64 work = MAXIMUM_PAIRING_WORK;
    pairWithTolerance(left, right, epsilon, &differences, &work, false);
    return differences;
}

bool PDFDiffHelper::isEqualContent(const PDFDiffPageContext& left, const PDFDiffPageContext& right, PDFReal epsilon, qint64* work)
{
    if (left.graphicPieces.size() != right.graphicPieces.size())
    {
        return false;
    }

    // Every comparison costs work, also of two pages without pieces (blank pages)
    *work -= qint64(left.graphicPieces.size() + right.graphicPieces.size()) + 1;

    Differences differences;
    size_t leftIndex = 0;
    size_t rightIndex = 0;
    while (leftIndex < left.graphicPieces.size() || rightIndex < right.graphicPieces.size())
    {
        if (rightIndex == right.graphicPieces.size() || (leftIndex < left.graphicPieces.size() && left.graphicPieces[leftIndex] < right.graphicPieces[rightIndex]))
        {
            differences.left.push_back(leftIndex++);
        }
        else if (leftIndex == left.graphicPieces.size() || right.graphicPieces[rightIndex] < left.graphicPieces[leftIndex])
        {
            differences.right.push_back(rightIndex++);
        }
        else
        {
            ++leftIndex;
            ++rightIndex;
        }
    }

    // The counts of the pieces are equal, so are the counts of their differences: each one must be paired
    if (differences.isEmpty())
    {
        return true;
    }

    return *work > 0 && pairWithTolerance(left, right, epsilon, &differences, work, true) && differences.isEmpty();
}

bool PDFDiffHelper::pairWithTolerance(const PDFDiffPageContext& left,
                                      const PDFDiffPageContext& right,
                                      PDFReal epsilon,
                                      Differences* differences,
                                      qint64* work,
                                      bool stopAtUnpaired)
{
    if (differences->left.empty() || differences->right.empty())
    {
        return differences->left.empty();
    }

    const PDFReal epsilonSquared = epsilon * epsilon;

    // B194: a piece is compared only with the pieces of its type and of its count of the elements
    std::map<std::pair<int, size_t>, std::vector<size_t>> rightGroups;
    for (size_t i = 0; i < differences->right.size(); ++i)
    {
        const size_t pieceIndex = differences->right[i];
        const size_t elementCount = right.elementOffsets[pieceIndex + 1] - right.elementOffsets[pieceIndex];
        rightGroups[std::make_pair(int(right.graphicPieces[pieceIndex].type), elementCount)].push_back(i);
    }

    std::vector<char> isRightPaired(differences->right.size(), 0);
    std::vector<char> isLeftPaired(differences->left.size(), 0);

    for (size_t i = 0; i < differences->left.size() && *work > 0; ++i)
    {
        const size_t leftPieceIndex = differences->left[i];
        const GraphicPieceInfo& leftInfo = left.graphicPieces[leftPieceIndex];
        const size_t leftElementOffset = left.elementOffsets[leftPieceIndex];
        const size_t elementCount = left.elementOffsets[leftPieceIndex + 1] - leftElementOffset;
        const QRectF leftRect = leftInfo.boundingRect.adjusted(-epsilon, -epsilon, epsilon, epsilon);

        auto it = rightGroups.find(std::make_pair(int(leftInfo.type), elementCount));
        if (it != rightGroups.cend())
        {
            for (const size_t j : it->second)
            {
                if (*work <= 0)
                {
                    break;
                }

                --(*work);
                if (isRightPaired[j])
                {
                    continue;
                }

                // Heuristically compare these items (a line has an empty rectangle, so the
                // rectangles are compared with the tolerance)
                const size_t rightPieceIndex = differences->right[j];
                const GraphicPieceInfo& rightInfo = right.graphicPieces[rightPieceIndex];
                if (!leftRect.intersects(rightInfo.boundingRect.adjusted(-epsilon, -epsilon, epsilon, epsilon)))
                {
                    continue;
                }

                bool hasMatch = (leftInfo.type != GraphicPieceInfo::Type::Image) || (leftInfo.imageHash == rightInfo.imageHash);
                const size_t rightElementOffset = right.elementOffsets[rightPieceIndex];
                for (size_t k = 0; k < elementCount && hasMatch; ++k)
                {
                    const QPainterPath::Element& leftElement = left.elements[leftElementOffset + k];
                    const QPainterPath::Element& rightElement = right.elements[rightElementOffset + k];

                    PDFReal diffX = leftElement.x - rightElement.x;
                    PDFReal diffY = leftElement.y - rightElement.y;
                    PDFReal squaredDistance = diffX * diffX + diffY * diffY;

                    hasMatch = (leftElement.type == rightElement.type) &&
                               (squaredDistance < epsilonSquared);
                }
                *work -= qint64(elementCount);

                if (hasMatch)
                {
                    // B172: a piece is paired with one piece only
                    isRightPaired[j] = 1;
                    isLeftPaired[i] = 1;
                    break;
                }
            }
        }

        if (!isLeftPaired[i] && stopAtUnpaired)
        {
            return false;
        }
    }

    std::vector<size_t> unpairedLeft;
    std::vector<size_t> unpairedRight;
    for (size_t i = 0; i < differences->left.size(); ++i)
    {
        if (!isLeftPaired[i])
        {
            unpairedLeft.push_back(differences->left[i]);
        }
    }
    for (size_t j = 0; j < differences->right.size(); ++j)
    {
        if (!isRightPaired[j])
        {
            unpairedRight.push_back(differences->right[j]);
        }
    }
    differences->left = std::move(unpairedLeft);
    differences->right = std::move(unpairedRight);
    return differences->left.empty();
}

PDFDiffHelper::PageSequence PDFDiffHelper::createPositionalSequence(const std::vector<PDFDiffPageContext>& left, const std::vector<PDFDiffPageContext>& right)
{
    PageSequence sequence;
    const size_t commonCount = qMin(left.size(), right.size());
    for (size_t i = 0; i < commonCount; ++i)
    {
        if (left[i].pageHash == right[i].pageHash)
        {
            PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem item;
            item.index1 = i;
            item.index2 = i;
            sequence.push_back(item);
        }
        else
        {
            // A left and a right item next to each other: markSequence makes them a replaced pair
            PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem leftItem;
            leftItem.index1 = i;
            sequence.push_back(leftItem);

            PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem rightItem;
            rightItem.index2 = i;
            sequence.push_back(rightItem);
        }
    }

    for (size_t i = commonCount; i < left.size(); ++i)
    {
        PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem item;
        item.index1 = i;
        sequence.push_back(item);
    }

    for (size_t i = commonCount; i < right.size(); ++i)
    {
        PDFAlgorithmLongestCommonSubsequenceBase::SequenceItem item;
        item.index2 = i;
        sequence.push_back(item);
    }

    return sequence;
}

std::vector<size_t> PDFDiffHelper::getLeftUnmatched(const PageSequence& sequence)
{
    std::vector<size_t> result;

    for (const auto& item : sequence)
    {
        if (item.isLeft())
        {
            result.push_back(item.index1);
        }
    }

    return result;
}

std::vector<size_t> PDFDiffHelper::getRightUnmatched(const PageSequence& sequence)
{
    std::vector<size_t> result;

    for (const auto& item : sequence)
    {
        if (item.isRight())
        {
            result.push_back(item.index2);
        }
    }

    return result;
}

std::vector<PDFDiffHelper::TextCompareItem> PDFDiffHelper::prepareTextCompareItems(const PDFDocumentTextFlow& textFlow,
                                                                                   bool isWordsComparingMode,
                                                                                   bool isLeft)
{
    std::vector<TextCompareItem> items;

    const size_t leftCount = textFlow.getSize();
    for (size_t i = 0; i < leftCount; ++i)
    {
        PDFDiffHelper::TextCompareItem item;
        item.index = i;
        item.left = isLeft;
        item.charCount = 0;

        const PDFDocumentTextFlow::Item* textFlowItem = textFlow.getItem(i);
        for (int j = 0; j < textFlowItem->text.size(); ++j)
        {
            if (isWordsComparingMode)
            {
                if (textFlowItem->text[j].isSpace())
                {
                    // Flush buffer
                    if (item.charCount > 0)
                    {
                        items.push_back(item);
                        item.charCount = 0;
                    }
                }
                else
                {
                    if (item.charCount == 0)
                    {
                        item.charIndex = j;
                    }
                    ++item.charCount;
                }
            }
            else
            {
                item.charIndex = j;
                item.charCount = 1;
                items.push_back(item);
            }
        }

        if (isWordsComparingMode && item.charCount > 0)
        {
            items.push_back(item);
            item.charCount = 0;
        }
    }

    return items;
}

void PDFDiffHelper::refineTextRectangles(PDFDiffResult::RectInfos& items)
{
    PDFDiffResult::RectInfos refinedItems;

    auto it = items.cbegin();
    auto itEnd = items.cend();
    while (it != itEnd)
    {
        // Jakub Melka: find range which can be merged into one
        // rectangle (it must be on a single page and rectangles must go
        // in right direction).

        auto itNext = std::next(it);
        while (itNext != itEnd)
        {
            const std::pair<PDFInteger, QRectF>& currentItem = *std::prev(itNext);
            const std::pair<PDFInteger, QRectF>& nextItem = *itNext;
            if (nextItem.first != currentItem.first)
            {
                // Page index has changed...
                break;
            }

            const QRectF& left = currentItem.second;
            const QRectF& right = nextItem.second;

            if (left.center().x() >= right.center().x())
            {
                break;
            }

            ++itNext;
        }

        // Merge range [it, itNext) into one new sequence
        QRectF unifiedRect;
        for (auto cit = it; cit != itNext; ++cit)
        {
            unifiedRect = unifiedRect.united((*cit).second);
        }
        refinedItems.emplace_back((*it).first, unifiedRect);

        it = itNext;
    }

    items = std::move(refinedItems);
}

PDFDiffResultNavigator::PDFDiffResultNavigator(QObject* parent) :
    QObject(parent),
    m_diffResult(nullptr),
    m_currentIndex(0)
{

}

PDFDiffResultNavigator::~PDFDiffResultNavigator()
{

}

void PDFDiffResultNavigator::setResult(const PDFDiffResult* diffResult)
{
    if (m_diffResult != diffResult)
    {
        // B175: the selection of an earlier result is never an index past the end of this one
        m_diffResult = diffResult;
        const size_t limit = getLimit();
        m_currentIndex = (limit > 0) ? qMin(m_currentIndex, limit - 1) : 0;
        Q_EMIT selectionChanged(m_currentIndex);
    }
}

bool PDFDiffResultNavigator::isSelected() const
{
    const size_t limit = getLimit();
    return m_currentIndex < limit;
}

bool PDFDiffResultNavigator::canGoNext() const
{
    const size_t limit = getLimit();
    return limit > 0 && m_currentIndex + 1 < limit;
}

bool PDFDiffResultNavigator::canGoPrevious() const
{
    const size_t limit = getLimit();
    return limit > 0 && m_currentIndex > 0;
}

void PDFDiffResultNavigator::goNext()
{
    if (!canGoNext())
    {
        return;
    }

    ++m_currentIndex;
    Q_EMIT selectionChanged(m_currentIndex);
}

void PDFDiffResultNavigator::goPrevious()
{
    if (!canGoPrevious())
    {
        return;
    }

    const size_t limit = getLimit();
    if (m_currentIndex >= limit)
    {
        m_currentIndex = limit - 1;
    }
    else
    {
        --m_currentIndex;
    }
    Q_EMIT selectionChanged(m_currentIndex);
}

void PDFDiffResultNavigator::update()
{
    const size_t limit = getLimit();
    if (limit > 0 && m_currentIndex >= limit)
    {
        m_currentIndex = limit - 1;
        Q_EMIT selectionChanged(m_currentIndex);
    }
}

void PDFDiffResultNavigator::select(size_t currentIndex)
{
    if (currentIndex < getLimit() && m_currentIndex != currentIndex)
    {
        m_currentIndex = currentIndex;
        Q_EMIT selectionChanged(m_currentIndex);
    }
}

}   // namespace pdf

#include "moc_pdfdiff.cpp"
