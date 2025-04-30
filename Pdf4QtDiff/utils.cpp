// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "utils.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"
#include "pdfdocumentbuilder.h"

#include <QPainter>

namespace pdfdiff
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
                    m_layout.emplace_back(0, i, -1, rect);
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
                    m_layout.emplace_back(0, item.leftPage, -1, rect);
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
                    m_layout.emplace_back(0, i, -1, rect);
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
                    m_layout.emplace_back(0, item.rightPage, -1, rect);
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
                    pdf::PDFInteger groupIndex = -1;
                    if (mode == ComparedDocumentMapper::Mode::Combined)
                    {
                        rect = QRectF(-pageSize.width() - 5, yPos, pageSize.width(), pageSize.height());
                    }
                    else
                    {
                        if (item.rightPage != -1)
                        {
                            groupIndex = 1;
                        }
                        rect = QRectF(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    }
                    m_layout.emplace_back(0, item.leftPage, groupIndex, rect);
                    yAdvance = pageSize.height() + 5;
                    m_leftPageIndices[item.leftPage] = item.leftPage;
                }

                if (item.rightPage != -1)
                {
                    pdf::PDFInteger rightPageIndex = item.rightPage + offset;
                    QSizeF pageSize = catalog->getPage(rightPageIndex)->getRotatedMediaBoxMM().size();
                    QRectF rect;
                    pdf::PDFInteger groupIndex = -1;
                    if (mode == ComparedDocumentMapper::Mode::Combined)
                    {
                        rect = QRectF(5, yPos, pageSize.width(), pageSize.height());
                    }
                    else
                    {
                        if (item.leftPage != -1)
                        {
                            groupIndex = 2;
                        }
                        rect = QRectF(-pageSize.width() * 0.5, yPos, pageSize.width(), pageSize.height());
                    }
                    m_layout.emplace_back(0, rightPageIndex, groupIndex, rect);
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
                                        const QTransform& pagePointToDevicePointMatrix,
                                        const pdf::PDFColorConvertor& convertor,
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

    std::optional<size_t> pageMoveIndex;

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
                    QColor color = convertor.convert(getColorForIndex(i), false, true);
                    drawRectangle(painter, pagePointToDevicePointMatrix, item.second, color);
                    drawMarker(painter, pagePointToDevicePointMatrix, item.second, color, true);
                }
            }

            if (m_diffResult->isPageMoveAddRemoveDifference(i) && m_diffResult->getLeftPage(i) == leftPageIndex)
            {
                pageMoveIndex = i;
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
                    QColor color = convertor.convert(getColorForIndex(i), false, true);
                    drawRectangle(painter, pagePointToDevicePointMatrix, item.second, color);
                    drawMarker(painter, pagePointToDevicePointMatrix, item.second, color, false);
                }
            }

            if (m_diffResult->isPageMoveAddRemoveDifference(i) && m_diffResult->getRightPage(i) == rightPageIndex)
            {
                pageMoveIndex = i;
            }
        }
    }

    if (pageMoveIndex)
    {
        QString text;

        switch (m_diffResult->getType(*pageMoveIndex))
        {
            case pdf::PDFDiffResult::Type::PageAdded:
                text = " + ";
                break;

            case pdf::PDFDiffResult::Type::PageRemoved:
                text = " - ";
                break;

            case pdf::PDFDiffResult::Type::PageMoved:
                text = QString("%1🠖%2").arg(m_diffResult->getLeftPage(*pageMoveIndex) + 1).arg(m_diffResult->getRightPage(*pageMoveIndex) + 1);
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        QColor color = convertor.convert(getColorForIndex(*pageMoveIndex), false, true);
        QPointF targetPoint = pagePointToDevicePointMatrix.map(QPointF(5, 5));
        pdf::PDFPainterHelper::drawBubble(painter, targetPoint.toPoint(), color, text, Qt::AlignRight | Qt::AlignTop);
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

void DifferencesDrawInterface::drawAnnotations(const pdf::PDFDocument* document,
                                               pdf::PDFDocumentBuilder* builder)
{
    pdf::PDFInteger pageCount = document->getCatalog()->getPageCount();

    QString title = pdf::PDFSysUtils::getUserName();
    QString subject = tr("Difference");

    for (pdf::PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        const size_t differencesCount = m_diffResult->getDifferencesCount();
        const pdf::PDFInteger leftPageIndex = m_mapper->getLeftPageIndex(pageIndex);
        const pdf::PDFInteger rightPageIndex = m_mapper->getRightPageIndex(pageIndex);

        const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
        pdf::PDFObjectReference reference = page->getPageReference();

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
                        const QRectF& rect = item.second;
                        QPolygonF polygon;
                        polygon << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
                        pdf::PDFObjectReference annotation = builder->createAnnotationPolygon(reference, polygon, 1.0, color, color, title, subject, m_diffResult->getMessage(i));
                        builder->setAnnotationOpacity(annotation, 0.3);
                        builder->updateAnnotationAppearanceStreams(annotation);
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
                        const QRectF& rect = item.second;
                        QPolygonF polygon;
                        polygon << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
                        pdf::PDFObjectReference annotation = builder->createAnnotationPolygon(reference, polygon, 1.0, color, color, title, subject, m_diffResult->getMessage(i));
                        builder->setAnnotationOpacity(annotation, 0.3);
                        builder->updateAnnotationAppearanceStreams(annotation);
                    }
                }
            }
        }
    }
}

void DifferencesDrawInterface::drawRectangle(QPainter* painter,
                                             const QTransform& pagePointToDevicePointMatrix,
                                             const QRectF& rect,
                                             QColor color) const
{
    color.setAlphaF(0.5);

    QRectF resultRect = pagePointToDevicePointMatrix.mapRect(rect);
    painter->fillRect(resultRect, color);
}

void DifferencesDrawInterface::drawMarker(QPainter* painter,
                                          const QTransform& pagePointToDevicePointMatrix,
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

}   // namespace pdfdiff
