//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfdiff.h"
#include "pdfrenderer.h"
#include "pdfdocumenttextflow.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfconstants.h"
#include "pdfalgorithmlcs.h"

#include <QtConcurrent/QtConcurrent>

namespace pdf
{

class PDFDiffHelper
{
public:
    using GraphicPieceInfo = PDFPrecompiledPage::GraphicPieceInfo;
    using GraphicPieceInfos = PDFPrecompiledPage::GraphicPieceInfos;
    using PageSequence = PDFAlgorithmLongestCommonSubsequenceBase::Sequence;


    struct Differences
    {
        GraphicPieceInfos left;
        GraphicPieceInfos right;

        bool isEmpty() const { return left.empty() && right.empty(); }
    };

    static Differences calculateDifferences(const GraphicPieceInfos& left, const GraphicPieceInfos& right, PDFReal epsilon);
    static std::vector<size_t> getLeftUnmatched(const PageSequence& sequence);
    static std::vector<size_t> getRightUnmatched(const PageSequence& sequence);
    static void matchPage(PageSequence& sequence, size_t leftPage, size_t rightPage);
};

PDFDiff::PDFDiff(QObject* parent) :
    BaseClass(parent),
    m_progress(nullptr),
    m_leftDocument(nullptr),
    m_rightDocument(nullptr),
    m_options(Asynchronous | PC_Text | PC_VectorGraphics | PC_Images),
    m_epsilon(0.001),
    m_cancelled(false)
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

void PDFDiff::start()
{
    // Jakub Melka: First, we must ensure, that comparation
    // process is finished, otherwise we must wait for end.
    // Then, create a new future watcher.
    stop();

    m_cancelled = false;

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
        emit comparationFinished();
    }
}

void PDFDiff::stop()
{
    if (m_futureWatcher && !m_futureWatcher->isFinished())
    {
        // Do stop only if process doesn't finished already.
        // If we are finished, we do not want to set cancelled state.
        m_cancelled = true;
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

    if (m_pagesForLeftDocument.isEmpty() || m_pagesForRightDocument.isEmpty())
    {
        result.setResult(tr("No page to be compared."));
        return result;
    }

    auto leftPages = m_pagesForLeftDocument.unfold();
    auto rightPages = m_pagesForRightDocument.unfold();

    const size_t leftDocumentPageCount = m_leftDocument->getCatalog()->getPageCount();
    const size_t rightDocumentPageCount = m_rightDocument->getCatalog()->getPageCount();

    if (leftPages.front() < 0 ||
        leftPages.back() >= PDFInteger(leftDocumentPageCount) ||
        rightPages.front() < 0 ||
        rightPages.back() >= PDFInteger(rightDocumentPageCount))
    {
        result.setResult(tr("Invalid page range."));
        return result;
    }

    if (m_progress)
    {
        ProgressStartupInfo info;
        info.showDialog = false;
        info.text = tr("Comparing documents.");
        m_progress->start(StepLast, std::move(info));
    }

    performSteps(leftPages, rightPages);

    if (m_progress)
    {
        m_progress->finish();
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

struct PDFDiffPageContext
{
    PDFInteger pageIndex = 0;
    std::array<uint8_t, 64> pageHash = { };
    PDFPrecompiledPage::GraphicPieceInfos graphicPieces;
    PDFDocumentTextFlow text;
};

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
            return it->second == right.pageIndex;
        }

        return false;
    };
    PDFAlgorithmLongestCommonSubsequence algorithm(leftPreparedPages.cbegin(), leftPreparedPages.cend(),
                                                   rightPreparedPages.cbegin(), rightPreparedPages.cend(),
                                                   comparePages);
    algorithm.perform();
    pageSequence = algorithm.getSequence();

    std::vector<size_t> leftUnmatched = PDFDiffHelper::getLeftUnmatched(pageSequence);
    std::vector<size_t> rightUnmatched = PDFDiffHelper::getRightUnmatched(pageSequence);

    // We are matching left pages to the right ones
    std::map<size_t, std::vector<size_t>> matchedPages;

    for (const size_t index : leftUnmatched)
    {
        matchedPages[index] = std::vector<size_t>();
    }

    auto matchLeftPage = [&, this](size_t leftIndex)
    {
        const PDFDiffPageContext& leftPageContext = leftPreparedPages[leftIndex];

        auto page = m_leftDocument->getCatalog()->getPage(leftPageContext.pageIndex);
        PDFReal epsilon = calculateEpsilonForPage(page);

        for (const size_t rightIndex : rightUnmatched)
        {
            const PDFDiffPageContext& rightPageContext = rightPreparedPages[rightIndex];
            if (leftPageContext.graphicPieces.size() != rightPageContext.graphicPieces.size())
            {
                // Match cannot exist, graphic pieces have different size
                continue;
            }

            PDFDiffHelper::Differences differences = PDFDiffHelper::calculateDifferences(leftPageContext.graphicPieces, rightPageContext.graphicPieces, epsilon);

            if (differences.isEmpty())
            {
                // Jakub Melka: we have a match
                matchedPages[leftIndex].push_back(rightIndex);
            }
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, leftUnmatched.begin(), leftUnmatched.end(), matchLeftPage);

    std::vector<size_t> leftPagesMoved;
    std::vector<size_t> rightPagesMoved;

    std::set<size_t> matchedRightPages;
    for (const auto& matchedPage : matchedPages)
    {
        for (size_t rightContextIndex : matchedPage.second)
        {
            if (!matchedRightPages.count(rightContextIndex))
            {
                matchedRightPages.insert(rightContextIndex);
                const PDFDiffPageContext& leftPageContext = leftPreparedPages[matchedPage.first];
                const PDFDiffPageContext& rightPageContext = rightPreparedPages[rightContextIndex];

                leftPagesMoved.push_back(leftPageContext.pageIndex);
                rightPagesMoved.push_back(rightPageContext.pageIndex);

                pageMatches[leftPageContext.pageIndex] = rightPageContext.pageIndex;
            }
        }
    }

    if (!pageMatches.empty())
    {
        algorithm.perform();
        pageSequence = algorithm.getSequence();
    }

    std::sort(leftPagesMoved.begin(), leftPagesMoved.end());
    std::sort(rightPagesMoved.begin(), rightPagesMoved.end());

    PDFAlgorithmLongestCommonSubsequenceBase::markSequence(pageSequence, leftPagesMoved, rightPagesMoved);
}

void PDFDiff::performSteps(const std::vector<PDFInteger>& leftPages, const std::vector<PDFInteger>& rightPages)
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

    // StepExtractContentLeftDocument
    if (!m_cancelled)
    {
        PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        PDFOptionalContentActivity optionalContentActivity(m_leftDocument, pdf::OCUsage::View, nullptr);
        fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(m_leftDocument), &optionalContentActivity));

        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(m_leftDocument);
        PDFCMSPointer cms = cmsManager.getCurrentCMS();

        auto fillPageContext = [&, this](PDFDiffPageContext& context)
        {
            PDFPrecompiledPage compiledPage;
            constexpr PDFRenderer::Features features = PDFRenderer::IgnoreOptionalContent;
            PDFRenderer renderer(m_leftDocument, &fontCache, cms.data(), &optionalContentActivity, features, pdf::PDFMeshQualitySettings());
            renderer.compile(&compiledPage, context.pageIndex);

            auto page = m_leftDocument->getCatalog()->getPage(context.pageIndex);
            PDFReal epsilon = calculateEpsilonForPage(page);
            context.graphicPieces = compiledPage.calculateGraphicPieceInfos(page->getMediaBox(), epsilon);

            finalizeGraphicsPieces(context);
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, leftPreparedPages.begin(), leftPreparedPages.end(), fillPageContext);
        stepProgress();
    }

    // StepExtractContentRightDocument
    if (!m_cancelled)
    {
        PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        PDFOptionalContentActivity optionalContentActivity(m_rightDocument, pdf::OCUsage::View, nullptr);
        fontCache.setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(m_rightDocument), &optionalContentActivity));

        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(m_rightDocument);
        PDFCMSPointer cms = cmsManager.getCurrentCMS();

        auto fillPageContext = [&, this](PDFDiffPageContext& context)
        {
            PDFPrecompiledPage compiledPage;
            constexpr PDFRenderer::Features features = PDFRenderer::IgnoreOptionalContent;
            PDFRenderer renderer(m_rightDocument, &fontCache, cms.data(), &optionalContentActivity, features, pdf::PDFMeshQualitySettings());
            renderer.compile(&compiledPage, context.pageIndex);

            const PDFPage* page = m_rightDocument->getCatalog()->getPage(context.pageIndex);
            PDFReal epsilon = calculateEpsilonForPage(page);
            context.graphicPieces = compiledPage.calculateGraphicPieceInfos(page->getMediaBox(), epsilon);

            finalizeGraphicsPieces(context);
        };

        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, rightPreparedPages.begin(), rightPreparedPages.end(), fillPageContext);
        stepProgress();
    }

    // StepMatchPages
    if (!m_cancelled)
    {
        performPageMatching(leftPreparedPages, rightPreparedPages, pageSequence, pageMatches);
        stepProgress();
    }

    // StepExtractTextLeftDocument
    if (!m_cancelled)
    {
        pdf::PDFDocumentTextFlowFactory factoryLeftDocumentTextFlow;
        factoryLeftDocumentTextFlow.setCalculateBoundingBoxes(true);
        PDFDocumentTextFlow leftTextFlow = factoryLeftDocumentTextFlow.create(m_leftDocument, leftPages, PDFDocumentTextFlowFactory::Algorithm::Auto);
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
    if (!m_cancelled)
    {
        pdf::PDFDocumentTextFlowFactory factoryRightDocumentTextFlow;
        factoryRightDocumentTextFlow.setCalculateBoundingBoxes(true);
        PDFDocumentTextFlow rightTextFlow = factoryRightDocumentTextFlow.create(m_rightDocument, rightPages, PDFDocumentTextFlowFactory::Algorithm::Auto);
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
    if (!m_cancelled)
    {
        performCompare(leftPreparedPages, rightPreparedPages, pageSequence, pageMatches);
        stepProgress();
    }
}

void PDFDiff::performCompare(const std::vector<PDFDiffPageContext>& leftPreparedPages,
                             const std::vector<PDFDiffPageContext>& rightPreparedPages,
                             PDFAlgorithmLongestCommonSubsequenceBase::Sequence& pageSequence,
                             const std::map<size_t, size_t>& pageMatches)
{
    using AlgorithmLCS = PDFAlgorithmLongestCommonSubsequenceBase;

    auto modifiedRanges = AlgorithmLCS::getModifiedRanges(pageSequence);

    // First find all moved pages
    for (const AlgorithmLCS::SequenceItem& item : pageSequence)
    {
        if (item.isMovedLeft())
        {
            Q_ASSERT(pageMatches.contains(leftPreparedPages.at(item.index1).pageIndex));
            const PDFInteger leftIndex = leftPreparedPages[item.index1].pageIndex;
            const PDFInteger rightIndex = pageMatches.at(leftIndex);
            m_result.addPageMoved(leftIndex, rightIndex);
        }
        if (item.isMoved())
        {
            m_result.addPageMoved(leftPreparedPages[item.index1].pageIndex, rightPreparedPages[item.index2].pageIndex);
        }
    }

    for (const auto& range : modifiedRanges)
    {
        AlgorithmLCS::SequenceItemFlags flags = AlgorithmLCS::collectFlags(range);

        const bool isAdded = flags.testFlag(AlgorithmLCS::Added);
        const bool isRemoved = flags.testFlag(AlgorithmLCS::Removed);
    }
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

        hasher.addData(reinterpret_cast<const char*>(info.hash.data()), int(info.hash.size()));
    }

    QByteArray hash = hasher.result();
    Q_ASSERT(QCryptographicHash::hashLength(QCryptographicHash::Sha512) == 64);

    size_t size = qMin<size_t>(hash.length(), context.pageHash.size());
    std::copy(hash.data(), hash.data() + size, context.pageHash.data());
}

void PDFDiff::onComparationPerformed()
{
    m_cancelled = false;
    m_result = m_future.result();
    emit comparationFinished();
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
    difference.message = PDFDiff::tr("Page no. %1 from old document has been moved to a new document at page no. %2.").arg(pageIndex1 + 1).arg(pageIndex2 + 1);

    m_differences.emplace_back(std::move(difference));
}

PDFDiffHelper::Differences PDFDiffHelper::calculateDifferences(const GraphicPieceInfos& left,
                                                               const GraphicPieceInfos& right,
                                                               PDFReal epsilon)
{
    Differences differences;

    Q_ASSERT(std::is_sorted(left.cbegin(), left.cend()));
    Q_ASSERT(std::is_sorted(right.cbegin(), right.cend()));

    for (const GraphicPieceInfo& info : left)
    {
        if (!std::binary_search(right.cbegin(), right.cend(), info))
        {
            differences.left.push_back(info);
        }
    }

    for (const GraphicPieceInfo& info : right)
    {
        if (!std::binary_search(left.cbegin(), left.cend(), info))
        {
            differences.right.push_back(info);
        }
    }

    const PDFReal epsilonSquared = epsilon * epsilon;

    // If exact match fails, then try to use match with epsilon. For each
    // item in left, we try to find matching item in right.
    for (auto it = differences.left.begin(); it != differences.left.end();)
    {
        bool hasMatch = false;

        const GraphicPieceInfo& leftInfo = *it;
        for (auto it2 = differences.right.begin(); it2 != differences.right.end();)
        {
            // Heuristically compare these items

            const GraphicPieceInfo& rightInfo = *it2;
            if (leftInfo.type != rightInfo.type || !leftInfo.boundingRect.intersects(rightInfo.boundingRect))
            {
                ++it2;
                continue;
            }

            const int elementCountPath1 = leftInfo.pagePath.elementCount();
            const int elementCountPath2 = rightInfo.pagePath.elementCount();

            if (elementCountPath1 != elementCountPath2)
            {
                ++it2;
                continue;
            }

            hasMatch = (leftInfo.type != GraphicPieceInfo::Type::Image) || (leftInfo.imageHash == rightInfo.imageHash);
            const int elementCount = leftInfo.pagePath.elementCount();
            for (int i = 0; i < elementCount && hasMatch; ++i)
            {
                QPainterPath::Element leftElement = leftInfo.pagePath.elementAt(i);
                QPainterPath::Element rightElement = rightInfo.pagePath.elementAt(i);

                PDFReal diffX = leftElement.x - rightElement.x;
                PDFReal diffY = leftElement.y - rightElement.y;
                PDFReal squaredDistance = diffX * diffX + diffY * diffY;

                hasMatch = (leftElement.type == rightElement.type) &&
                           (squaredDistance < epsilonSquared);
            }

            if (hasMatch)
            {
                it2 = differences.right.erase(it2);
            }
            else
            {
                ++it2;
            }
        }

        if (hasMatch)
        {
            it = differences.left.erase(it);
        }
        else
        {
            ++it;
        }
    }

    return differences;
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

void PDFDiffHelper::matchPage(PageSequence& sequence,
                              size_t leftPage,
                              size_t rightPage)
{
    for (auto it = sequence.begin(); it != sequence.end();)
    {
        auto& item = *it;

        if (item.isLeft() && item.index1 == leftPage)
        {
            item.index2 = rightPage;
        }

        if (item.isRight() && item.index2 == rightPage)
        {
            it = sequence.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}   // namespace pdf
