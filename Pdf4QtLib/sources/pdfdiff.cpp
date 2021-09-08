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

#include <QtConcurrent/QtConcurrent>

namespace pdf
{

PDFDiff::PDFDiff(QObject* parent) :
    BaseClass(parent),
    m_progress(nullptr),
    m_leftDocument(nullptr),
    m_rightDocument(nullptr),
    m_options(Asynchronous),
    m_epsilon(0.0001),
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
    PDFPrecompiledPage::GraphicPieceInfos graphicPieces;
};

void PDFDiff::performSteps(const std::vector<PDFInteger>& leftPages, const std::vector<PDFInteger>& rightPages)
{
    std::vector<PDFDiffPageContext> leftPreparedPages;
    std::vector<PDFDiffPageContext> rightPreparedPages;

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

            PDFReal epsilon = calculateEpsilonForPage(m_leftDocument->getCatalog()->getPage(context.pageIndex));
            context.graphicPieces = compiledPage.calculateGraphicPieceInfos(epsilon);
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

            PDFReal epsilon = calculateEpsilonForPage(m_leftDocument->getCatalog()->getPage(context.pageIndex));
            context.graphicPieces = compiledPage.calculateGraphicPieceInfos(epsilon);
        };

        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, rightPreparedPages.begin(), rightPreparedPages.end(), fillPageContext);
        stepProgress();
    }

    // StepExtractTextLeftDocument
    if (!m_cancelled)
    {
        pdf::PDFDocumentTextFlowFactory factoryLeftDocumentTextFlow;
        factoryLeftDocumentTextFlow.setCalculateBoundingBoxes(true);
        PDFDocumentTextFlow leftTextFlow = factoryLeftDocumentTextFlow.create(m_leftDocument, leftPages, PDFDocumentTextFlowFactory::Algorithm::Auto);
        stepProgress();
    }

    // StepExtractTextRightDocument
    if (!m_cancelled)
    {
        pdf::PDFDocumentTextFlowFactory factoryRightDocumentTextFlow;
        factoryRightDocumentTextFlow.setCalculateBoundingBoxes(true);
        PDFDocumentTextFlow rightTextFlow = factoryRightDocumentTextFlow.create(m_rightDocument, rightPages, PDFDocumentTextFlowFactory::Algorithm::Auto);
        stepProgress();
    }

    // StepCompare
    if (!m_cancelled)
    {
        stepProgress();
    }
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

}   // namespace pdf
