//    Copyright (C) 2022 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfpagecontenteditortools.h"
#include "pdfpagecontentelements.h"

#include <QPen>

namespace pdf
{

PDFCreatePCElementTool::PDFCreatePCElementTool(PDFDrawWidgetProxy* proxy,
                                               PDFPageContentScene* scene,
                                               QAction* action,
                                               QObject* parent) :
    PDFWidgetTool(proxy, action, parent),
    m_scene(scene)
{

}

PDFCreatePCElementRectangleTool::PDFCreatePCElementRectangleTool(PDFDrawWidgetProxy* proxy,
                                                                 PDFPageContentScene* scene,
                                                                 QAction* action,
                                                                 bool isRounded,
                                                                 QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreatePCElementRectangleTool::onRectanglePicked);

    QPen pen(Qt::SolidLine);
    pen.setWidthF(1.0);

    m_element = new PDFPageContentElementRectangle();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));
    m_element->setRounded(isRounded);

    updateActions();
}

PDFCreatePCElementRectangleTool::~PDFCreatePCElementRectangleTool()
{
    delete m_element;
}

void PDFCreatePCElementRectangleTool::drawPage(QPainter* painter,
                                               PDFInteger pageIndex,
                                               const PDFPrecompiledPage* compiledPage,
                                               PDFTextLayoutGetter& layoutGetter,
                                               const QMatrix& pagePointToDevicePointMatrix,
                                               QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

    if (pageIndex != m_pickTool->getPageIndex())
    {
        return;
    }

    const std::vector<QPointF>& points = m_pickTool->getPickedPoints();
    if (points.empty())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);

    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());
    QPointF point = points.front();
    qreal xMin = qMin(point.x(), mousePoint.x());
    qreal xMax = qMax(point.x(), mousePoint.x());
    qreal yMin = qMin(point.y(), mousePoint.y());
    qreal yMax = qMax(point.y(), mousePoint.y());
    qreal width = xMax - xMin;
    qreal height = yMax - yMin;

    if (!qFuzzyIsNull(width) && !qFuzzyIsNull(height))
    {
        QRectF rect(xMin, yMin, width, height);
        m_element->setRectangle(rect);
    }

    m_element->drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

void PDFCreatePCElementRectangleTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(pageRectangle);
    m_scene->addElement(m_element->clone());

    setActive(false);
}

}   // namespace pdf
