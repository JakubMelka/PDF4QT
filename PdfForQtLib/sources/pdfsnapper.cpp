//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfsnapper.h"
#include "pdfcompiler.h"
#include "pdfwidgetutils.h"

#include <QPainter>

namespace pdf
{

void PDFSnapInfo::addPageMediaBox(const QRectF& mediaBox)
{
    QPointF tl = mediaBox.topLeft();
    QPointF tr = mediaBox.topRight();
    QPointF bl = mediaBox.bottomLeft();
    QPointF br = mediaBox.bottomRight();
    QPointF center = mediaBox.center();

    m_snapPoints.insert(m_snapPoints.cend(), {
                            SnapPoint(SnapType::PageCorner, tl ),
                            SnapPoint(SnapType::PageCorner, tr ),
                            SnapPoint(SnapType::PageCorner, bl ),
                            SnapPoint(SnapType::PageCorner, br ),
                            SnapPoint(SnapType::PageCenter, center)
                        });

    addLine(tl, tr);
    addLine(tr, br);
    addLine(br, bl);
    addLine(tl, bl);
}

void PDFSnapInfo::addImage(const std::array<QPointF, 5>& points)
{
    m_snapPoints.insert(m_snapPoints.cend(), {
                            SnapPoint(SnapType::ImageCorner, points[0]),
                            SnapPoint(SnapType::ImageCorner, points[1]),
                            SnapPoint(SnapType::ImageCorner, points[2]),
                            SnapPoint(SnapType::ImageCorner, points[3]),
                            SnapPoint(SnapType::ImageCenter, points[4])
                        });

    for (size_t i = 0; i < 4; ++i)
    {
        addLine(points[i], points[(i + 1) % 4]);
    }
}

void PDFSnapInfo::addLine(const QPointF& start, const QPointF& end)
{
    QLineF line(start, end);
    m_snapPoints.emplace_back(SnapType::LineCenter, line.center());
    m_snapLines.emplace_back(line);
}

PDFSnapper::PDFSnapper()
{

}

void PDFSnapper::drawSnapPoints(QPainter* painter, PDFInteger pageIndex, const PDFPrecompiledPage* compiledPage, const QMatrix& pagePointToDevicePointMatrix) const
{
    if (m_currentPage != -1 && m_currentPage != pageIndex)
    {
        // We are drawing only snap points, which are on current page
        return;
    }

    Q_ASSERT(painter);
    Q_ASSERT(compiledPage);

    const PDFSnapInfo* snapInfo = (m_currentPage != pageIndex) ? compiledPage->getSnapInfo() : &m_currentPageSnapInfo;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen pen = painter->pen();
    pen.setCapStyle(Qt::RoundCap);
    pen.setWidth(PDFWidgetUtils::scaleDPI_x(painter->device(), 10));

    for (const PDFSnapInfo::SnapPoint& snapPoint : snapInfo->getSnapPoints())
    {
        QColor color = pen.color();
        QColor newColor = color;
        switch (snapPoint.type)
        {
            case SnapType::PageCorner:
                newColor = Qt::blue;
                break;

            case SnapType::GeneratedLineProjection:
                newColor = Qt::green;
                break;

            default:
                newColor = Qt::red;
                break;
        }

        if (color != newColor)
        {
            pen.setColor(newColor);
            painter->setPen(pen);
        }

        QPoint point = pagePointToDevicePointMatrix.map(snapPoint.point).toPoint();
        painter->drawPoint(point);
    }

    painter->restore();
}

}   // namespace pdf
