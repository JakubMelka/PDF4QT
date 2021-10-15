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

#include "utils.h"
#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"

#include <QPainter>

namespace pdfdocdiff
{

void ComparedDocumentMapper::update(ComparedDocumentMapper::Mode mode,
                                    bool filterDifferences,
                                    const pdf::PDFDiffResult& diff,
                                    const pdf::PDFDocument* leftDocument,
                                    const pdf::PDFDocument* rightDocument,
                                    const pdf::PDFDocument* currentDocument)
{
    m_layout.clear();

    m_leftPageIndices.clear();
    m_rightPageIndices.clear();

    m_allLeft = false;
    m_allRight = false;

    if (!leftDocument || !rightDocument || !currentDocument)
    {
        return;
    }

    // Jakub Melka
    pdf::PDFDiffResult::PageSequence pageSequence = diff.getPageSequence();
    const bool isEmpty = pageSequence.empty();

    if (filterDifferences)
    {
        pdf::PDFDiffResult::PageSequence filteredPageSequence;

        std::vector<pdf::PDFInteger> leftPageIndices = diff.getChangedLeftPageIndices();
        std::vector<pdf::PDFInteger> rightPageIndices = diff.getChangedRightPageIndices();

        for (const pdf::PDFDiffResult::PageSequenceItem& item : pageSequence)
        {
            const bool isLeftModified = std::binary_search(leftPageIndices.cbegin(), leftPageIndices.cend(), item.leftPage);
            const bool isRightModified = std::binary_search(rightPageIndices.cbegin(), rightPageIndices.cend(), item.rightPage);

            if (isLeftModified || isRightModified)
            {
                filteredPageSequence.push_back(item);
            }
        }

        pageSequence = std::move(filteredPageSequence);
    }

    switch (mode)
    {
        case ComparedDocumentMapper::Mode::Left:
        {
            Q_ASSERT(leftDocument == currentDocument);

            m_allLeft = true;
            double yPos = 0.0;
            const pdf::PDFCatalog* catalog = leftDocument->getCatalog();

            if (isEmpty)
            {
                // Just copy all pages
                const size_t pageCount = catalog->getPageCount();
                for (size_t i = 0; i < pageCount; ++i)
                {
                    QSizeF pageSize = catalog->getPage(i)->getRotatedMediaBoxMM().size();
                    QRectF rect(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    m_layout.emplace_back(0, i, rect);
                    yPos += pageSize.height() + 5;
                }
            }
            else
            {
                for (const pdf::PDFDiffResult::PageSequenceItem& item : pageSequence)
                {
                    if (item.leftPage == -1)
                    {
                        continue;
                    }

                    QSizeF pageSize = catalog->getPage(item.leftPage)->getRotatedMediaBoxMM().size();
                    QRectF rect(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    m_layout.emplace_back(0, item.leftPage, rect);
                    yPos += pageSize.height() + 5;
                }
            }

            break;
        }

        case ComparedDocumentMapper::Mode::Right:
        {
            Q_ASSERT(rightDocument == currentDocument);

            m_allRight = true;
            double yPos = 0.0;
            const pdf::PDFCatalog* catalog = rightDocument->getCatalog();

            if (isEmpty)
            {
                // Just copy all pages
                const size_t pageCount = catalog->getPageCount();
                for (size_t i = 0; i < pageCount; ++i)
                {
                    QSizeF pageSize = catalog->getPage(i)->getRotatedMediaBoxMM().size();
                    QRectF rect(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    m_layout.emplace_back(0, i, rect);
                    yPos += pageSize.height() + 5;
                }
            }
            else
            {
                for (const pdf::PDFDiffResult::PageSequenceItem& item : pageSequence)
                {
                    if (item.rightPage == -1)
                    {
                        continue;
                    }

                    QSizeF pageSize = catalog->getPage(item.rightPage)->getRotatedMediaBoxMM().size();
                    QRectF rect(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    m_layout.emplace_back(0, item.rightPage, rect);
                    yPos += pageSize.height() + 5;
                }
            }

            break;
        }

        case ComparedDocumentMapper::Mode::Combined:
        case ComparedDocumentMapper::Mode::Overlay:
        {
            double yPos = 0.0;
            const pdf::PDFCatalog* catalog = currentDocument->getCatalog();
            pdf::PDFInteger offset = leftDocument->getCatalog()->getPageCount();

            for (const pdf::PDFDiffResult::PageSequenceItem& item : pageSequence)
            {
                double yAdvance = 0.0;

                if (item.leftPage != -1)
                {
                    QSizeF pageSize = catalog->getPage(item.leftPage)->getRotatedMediaBoxMM().size();
                    QRectF rect;
                    if (mode == ComparedDocumentMapper::Mode::Combined)
                    {
                        rect = QRectF(-pageSize.width() - 5, yPos, pageSize.width(), pageSize.height());
                    }
                    else
                    {
                        rect = QRectF(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    }
                    m_layout.emplace_back(0, item.leftPage, rect);
                    yAdvance = pageSize.height() + 5;
                    m_leftPageIndices[item.leftPage] = item.leftPage;
                }

                if (item.rightPage != -1)
                {
                    pdf::PDFInteger rightPageIndex = item.rightPage + offset;
                    QSizeF pageSize = catalog->getPage(rightPageIndex)->getRotatedMediaBoxMM().size();
                    QRectF rect;
                    if (mode == ComparedDocumentMapper::Mode::Combined)
                    {
                        rect = QRectF(5, yPos, pageSize.width(), pageSize.height());
                    }
                    else
                    {
                        rect = QRectF(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    }
                    m_layout.emplace_back(0, rightPageIndex, rect);
                    yAdvance = qMax(yAdvance, pageSize.height() + 5);
                    m_rightPageIndices[rightPageIndex] = item.rightPage;
                }

                yPos += yAdvance;
            }

            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

pdf::PDFInteger ComparedDocumentMapper::getLeftPageIndex(pdf::PDFInteger pageIndex) const
{
    if (m_allLeft)
    {
        return pageIndex;
    }

    auto it = m_leftPageIndices.find(pageIndex);
    if (it != m_leftPageIndices.cend())
    {
        return it->second;
    }

    return -1;
}

pdf::PDFInteger ComparedDocumentMapper::getRightPageIndex(pdf::PDFInteger pageIndex) const
{
    if (m_allRight)
    {
        return pageIndex;
    }

    auto it = m_rightPageIndices.find(pageIndex);
    if (it != m_rightPageIndices.cend())
    {
        return it->second;
    }

    return -1;
}

pdf::PDFInteger ComparedDocumentMapper::getPageIndexFromLeftPageIndex(pdf::PDFInteger leftPageIndex) const
{
    if (m_allLeft)
    {
        return leftPageIndex;
    }

    for (const auto& indexItem : m_leftPageIndices)
    {
        if (indexItem.second == leftPageIndex)
        {
            return indexItem.first;
        }
    }

    return -1;
}

pdf::PDFInteger ComparedDocumentMapper::getPageIndexFromRightPageIndex(pdf::PDFInteger rightPageIndex) const
{
    if (m_allRight)
    {
        return rightPageIndex;
    }

    for (const auto& indexItem : m_rightPageIndices)
    {
        if (indexItem.second == rightPageIndex)
        {
            return indexItem.first;
        }
    }

    return -1;
}

DifferencesDrawInterface::DifferencesDrawInterface(const Settings* settings,
                                                   const ComparedDocumentMapper* mapper,
                                                   const pdf::PDFDiffResult* diffResult) :
    m_settings(settings),
    m_mapper(mapper),
    m_diffResult(diffResult)
{

}

void DifferencesDrawInterface::drawPage(QPainter* painter,
                                        pdf::PDFInteger pageIndex,
                                        const pdf::PDFPrecompiledPage* compiledPage,
                                        pdf::PDFTextLayoutGetter& layoutGetter,
                                        const QMatrix& pagePointToDevicePointMatrix,
                                        QList<pdf::PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (!m_settings->displayDifferences)
    {
        return;
    }

    const size_t differencesCount = m_diffResult->getDifferencesCount();
    const pdf::PDFInteger leftPageIndex = m_mapper->getLeftPageIndex(pageIndex);
    const pdf::PDFInteger rightPageIndex = m_mapper->getRightPageIndex(pageIndex);

    if (leftPageIndex != -1)
    {
        for (size_t i = 0; i < differencesCount; ++i)
        {
            auto leftRectangles = m_diffResult->getLeftRectangles(i);
            for (auto it = leftRectangles.first; it != leftRectangles.second; ++it)
            {
                const auto& item = *it;
                if (item.first == leftPageIndex)
                {
                    QColor color = getColorForIndex(i);
                    drawRectangle(painter, pagePointToDevicePointMatrix, item.second, color);
                    drawMarker(painter, pagePointToDevicePointMatrix, item.second, color, true);
                }
            }
        }
    }

    if (rightPageIndex != -1)
    {
        for (size_t i = 0; i < differencesCount; ++i)
        {
            auto rightRectangles = m_diffResult->getRightRectangles(i);
            for (auto it = rightRectangles.first; it != rightRectangles.second; ++it)
            {
                const auto& item = *it;
                if (item.first == rightPageIndex)
                {
                    QColor color = getColorForIndex(i);
                    drawRectangle(painter, pagePointToDevicePointMatrix, item.second, color);
                    drawMarker(painter, pagePointToDevicePointMatrix, item.second, color, false);
                }
            }
        }
    }
}

QColor DifferencesDrawInterface::getColorForIndex(size_t index) const
{
    QColor color;

    const size_t resultIndex = index;

    if (m_diffResult->isReplaceDifference(resultIndex))
    {
        color = m_settings->colorReplaced;
    }
    else if (m_diffResult->isRemoveDifference(resultIndex))
    {
        color = m_settings->colorRemoved;
    }
    else if (m_diffResult->isAddDifference(resultIndex))
    {
        color = m_settings->colorAdded;
    }
    else if (m_diffResult->isPageMoveDifference(resultIndex))
    {
        color = m_settings->colorPageMove;
    }

    return color;
}

void DifferencesDrawInterface::drawPostRendering(QPainter* painter, QRect rect) const
{
    Q_UNUSED(painter);
    Q_UNUSED(rect);
}

void DifferencesDrawInterface::drawRectangle(QPainter* painter,
                                             const QMatrix& pagePointToDevicePointMatrix,
                                             const QRectF& rect,
                                             QColor color) const
{
    color.setAlphaF(0.5);

    QRectF resultRect = pagePointToDevicePointMatrix.mapRect(rect);
    painter->fillRect(resultRect, color);
}

void DifferencesDrawInterface::drawMarker(QPainter* painter,
                                          const QMatrix& pagePointToDevicePointMatrix,
                                          const QRectF& rect,
                                          QColor color,
                                          bool isLeft) const
{
    if (!m_settings->displayMarkers)
    {
        return;
    }

    pdf::PDFPainterStateGuard guard(painter);
    QRectF deviceRect = pagePointToDevicePointMatrix.mapRect(rect);

    QPointF snapPoint;
    QPointF markPoint;

    if (isLeft)
    {
        snapPoint.ry() = deviceRect.center().y();
        snapPoint.rx() = deviceRect.left();
        markPoint = snapPoint;
        markPoint.rx() = 0;
    }
    else
    {
        snapPoint.ry() = deviceRect.center().y();
        snapPoint.rx() = deviceRect.right();
        markPoint = snapPoint;
        markPoint.rx() = painter->device()->width();
    }

    const qreal lineWidthF = pdf::PDFWidgetUtils::scaleDPI_y(painter->device(), 2);

    QPen pen(Qt::DotLine);
    pen.setColor(color);
    pen.setWidthF(lineWidthF);

    painter->setBrush(Qt::NoBrush);
    painter->setPen(pen);
    painter->drawLine(snapPoint, markPoint);

    const qreal markSizeX = pdf::PDFWidgetUtils::scaleDPI_x(painter->device(), 10);
    const qreal markSizeY = pdf::PDFWidgetUtils::scaleDPI_y(painter->device(), 10);

    QPointF ptc = markPoint;
    QPointF ptTop = ptc;
    QPointF ptBottom = ptc;

    ptTop.ry() -= markSizeY;
    ptBottom.ry() += markSizeY;
    ptc.rx() += isLeft ? markSizeX : -markSizeX;

    std::array points = { ptTop, ptc, ptBottom };

    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(color));
    painter->drawConvexPolygon(points.data(), int(points.size()));
}

}   // namespace pdfdocdiff
