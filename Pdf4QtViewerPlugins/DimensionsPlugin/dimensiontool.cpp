//    Copyright (C) 2020-2021 Jakub Melka
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

#include "dimensiontool.h"
#include "pdfwidgetutils.h"
#include "pdfdrawwidget.h"

#include <QPainter>

DimensionTool::DimensionTool(Style style, pdf::PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_style(style),
    m_previewPointPixelSize(0),
    m_pickTool(nullptr)
{
    const bool isRectanglePicking = style == RectanglePerimeter || style == RectangleArea;
    const pdf::PDFPickTool::Mode pickingMode = isRectanglePicking ? pdf::PDFPickTool::Mode::Rectangles : pdf::PDFPickTool::Mode::Points;
    m_pickTool = new pdf::PDFPickTool(proxy, pickingMode, this);
    addTool(m_pickTool);
    connect(m_pickTool, &pdf::PDFPickTool::pointPicked, this, &DimensionTool::onPointPicked);
    connect(m_pickTool, &pdf::PDFPickTool::rectanglePicked, this, &DimensionTool::onRectanglePicked);
    m_previewPointPixelSize = pdf::PDFWidgetUtils::scaleDPI_x(proxy->getWidget(), 5);
}

void DimensionTool::drawPage(QPainter* painter,
                             pdf::PDFInteger pageIndex,
                             const pdf::PDFPrecompiledPage* compiledPage,
                             pdf::PDFTextLayoutGetter& layoutGetter,
                             const QMatrix& pagePointToDevicePointMatrix,
                             QList<pdf::PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (m_pickTool->getPageIndex() != pageIndex)
    {
        // Other page, nothing to draw
        return;
    }

    if (m_style == RectanglePerimeter || m_style == RectangleArea)
    {
        // Nothing to draw, picking tool is already drawing picked rectangle
        return;
    }

    painter->setPen(Qt::black);
    const std::vector<QPointF>& points = m_pickTool->getPickedPoints();
    for (size_t i = 1; i < points.size(); ++i)
    {
        painter->drawLine(pagePointToDevicePointMatrix.map(points[i - 1]), pagePointToDevicePointMatrix.map(points[i]));
    }

    if (!points.empty())
    {
        QMatrix inverted = pagePointToDevicePointMatrix.inverted();
        QPointF adjustedPoint = adjustPagePoint(inverted.map(m_pickTool->getSnappedPoint()));
        painter->drawLine(pagePointToDevicePointMatrix.map(points.back()), pagePointToDevicePointMatrix.map(adjustedPoint));
    }

    QPen pen = painter->pen();
    pen.setWidth(m_previewPointPixelSize);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);

    for (size_t i = 0; i < points.size(); ++i)
    {
        painter->drawPoint(pagePointToDevicePointMatrix.map(points[i]));
    }
}

void DimensionTool::onPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint)
{
    Q_UNUSED(pagePoint);

    if (Dimension::isComplete(getDimensionType(), m_pickTool->getPickedPoints()))
    {
        // Create a new dimension...
        std::vector<QPointF> points = m_pickTool->getPickedPoints();
        for (QPointF& point : points)
        {
            point = adjustPagePoint(point);
        }

        pdf::PDFReal measuredValue = getMeasuredValue(pageIndex, points);
        emit dimensionCreated(Dimension(getDimensionType(), pageIndex, measuredValue, qMove(points)));
        m_pickTool->resetTool();
    }

    if ((m_style == Perimeter || m_style == Area) && m_pickTool->getPickedPoints().size() == 1)
    {
        m_pickTool->setCustomSnapPoints(pageIndex, m_pickTool->getPickedPoints());
    }
}

void DimensionTool::onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    std::vector<QPointF> points = { pageRectangle.topLeft(), pageRectangle.topRight(), pageRectangle.bottomRight(), pageRectangle.bottomLeft(), pageRectangle.topLeft() };
    Q_ASSERT(Dimension::isComplete(getDimensionType(), points));
    pdf::PDFReal measuredValue = getMeasuredValue(pageIndex, points);
    emit dimensionCreated(Dimension(getDimensionType(), pageIndex, measuredValue, qMove(points)));
}

QPointF DimensionTool::adjustPagePoint(QPointF pagePoint) const
{
    switch (m_style)
    {
        case Style::LinearHorizontal:
        {
            const std::vector<QPointF>& pickedPoints = m_pickTool->getPickedPoints();
            if (!pickedPoints.empty())
            {
                const pdf::PDFPage* page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex());
                const bool rotated = page->getPageRotation() == pdf::PageRotation::Rotate90 || page->getPageRotation() == pdf::PageRotation::Rotate270;

                if (rotated)
                {
                    pagePoint.setX(pickedPoints.front().x());
                }
                else
                {
                    pagePoint.setY(pickedPoints.front().y());
                }
            }
            break;
        }

        case Style::LinearVertical:
        {
            const std::vector<QPointF>& pickedPoints = m_pickTool->getPickedPoints();
            if (!pickedPoints.empty())
            {
                const pdf::PDFPage* page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex());
                const bool rotated = page->getPageRotation() == pdf::PageRotation::Rotate90 || page->getPageRotation() == pdf::PageRotation::Rotate270;

                if (!rotated)
                {
                    pagePoint.setX(pickedPoints.front().x());
                }
                else
                {
                    pagePoint.setY(pickedPoints.front().y());
                }
            }
            break;
        }

        default:
            break;
    }

    return pagePoint;
}

Dimension::Type DimensionTool::getDimensionType() const
{
    switch (m_style)
    {
        case DimensionTool::LinearHorizontal:
        case DimensionTool::LinearVertical:
        case DimensionTool::Linear:
            return Dimension::Type::Linear;

        case DimensionTool::Perimeter:
        case DimensionTool::RectanglePerimeter:
            return Dimension::Type::Perimeter;

        case DimensionTool::Area:
        case DimensionTool::RectangleArea:
            return Dimension::Type::Area;

        case DimensionTool::Angle:
            return Dimension::Type::Angular;
    }

    Q_ASSERT(false);
    return Dimension::Type::Linear;
}

pdf::PDFReal DimensionTool::getMeasuredValue(pdf::PDFInteger pageIndex, const std::vector<QPointF>& pickedPoints) const
{
    const pdf::PDFPage* page = getDocument()->getCatalog()->getPage(pageIndex);
    Q_ASSERT(page);

    switch (getDimensionType())
    {
        case Dimension::Linear:
        case Dimension::Perimeter:
        {
            pdf::PDFReal length = 0.0;

            for (size_t i = 1; i < pickedPoints.size(); ++i)
            {
                QPointF vector = pickedPoints[i] - pickedPoints[i - 1];
                length += qSqrt(QPointF::dotProduct(vector, vector));
            }

            return length * page->getUserUnit();
        }

        case Dimension::Area:
        {
            pdf::PDFReal area = 0.0;

            // We calculate the area using standard formula for polygons.
            // We determine integral over perimeter (for each edge of the polygon).
            for (size_t i = 1; i < pickedPoints.size(); ++i)
            {
                const QPointF& p1 = pickedPoints[i - 1];
                const QPointF& p2 = pickedPoints[i];

                area += p1.x() * p2.y() - p1.y() * p2.x();
            }

            area = qAbs(area) * 0.5;
            return area * page->getUserUnit() * page->getUserUnit();
        }

        case Dimension::Angular:
        {
            Q_ASSERT(pickedPoints.size() == 3);
            QLineF line1(pickedPoints[1], pickedPoints.front());
            QLineF line2(pickedPoints[1], pickedPoints.back());
            return line1.angleTo(line2);
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return 0.0;
}

bool Dimension::isComplete(Type type, const std::vector<QPointF>& polygon)
{
    switch (type)
    {
        case Dimension::Linear:
            return polygon.size() == 2;

        case Dimension::Perimeter:
        case Dimension::Area:
            return polygon.size() > 2 && polygon.front() == polygon.back();

        case Dimension::Angular:
            return polygon.size() == 3;

        default:
            Q_ASSERT(false);
            break;
    }

    return false;
}

DimensionUnits DimensionUnit::getLengthUnits()
{
    DimensionUnits units;

    units.emplace_back(1.0, DimensionTool::tr("pt"));
    units.emplace_back(pdf::PDF_POINT_TO_INCH, DimensionTool::tr("in"));
    units.emplace_back(pdf::PDF_POINT_TO_MM, DimensionTool::tr("mm"));
    units.emplace_back(pdf::PDF_POINT_TO_MM * 0.1, DimensionTool::tr("cm"));

    return units;
}

DimensionUnits DimensionUnit::getAreaUnits()
{
    DimensionUnits units;

    units.emplace_back(1.0, DimensionTool::tr("sq. pt"));
    units.emplace_back(pdf::PDF_POINT_TO_INCH * pdf::PDF_POINT_TO_INCH, DimensionTool::tr("sq. in"));
    units.emplace_back(pdf::PDF_POINT_TO_MM * pdf::PDF_POINT_TO_MM, DimensionTool::tr("sq. mm"));
    units.emplace_back(pdf::PDF_POINT_TO_MM * 0.1 * pdf::PDF_POINT_TO_MM * 0.1, DimensionTool::tr("sq. cm"));

    return units;
}

DimensionUnits DimensionUnit::getAngleUnits()
{
    DimensionUnits units;

    units.emplace_back(1.0, DimensionTool::tr("°"));
    units.emplace_back(qDegreesToRadians(1.0), DimensionTool::tr("rad"));

    return units;
}
