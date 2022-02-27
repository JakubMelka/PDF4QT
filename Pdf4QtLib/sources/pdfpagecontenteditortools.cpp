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
#include "pdfpainterutils.h"

#include <QPen>
#include <QPainter>

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

QRectF PDFCreatePCElementTool::getRectangleFromPickTool(PDFPickTool* pickTool,
                                                        const QMatrix& pagePointToDevicePointMatrix)
{
    const std::vector<QPointF>& points = pickTool->getPickedPoints();
    if (points.empty())
    {
        return QRectF();
    }

    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(pickTool->getSnappedPoint());
    QPointF point = points.front();
    qreal xMin = qMin(point.x(), mousePoint.x());
    qreal xMax = qMax(point.x(), mousePoint.x());
    qreal yMin = qMin(point.y(), mousePoint.y());
    qreal yMax = qMax(point.y(), mousePoint.y());
    qreal width = xMax - xMin;
    qreal height = yMax - yMin;

    if (!qFuzzyIsNull(width) && !qFuzzyIsNull(height))
    {
        return QRectF(xMin, yMin, width, height);
    }

    return QRectF();
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

    QRectF rectangle = getRectangleFromPickTool(m_pickTool, pagePointToDevicePointMatrix);
    if (!rectangle.isValid())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(rectangle);

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

PDFCreatePCElementLineTool::PDFCreatePCElementLineTool(PDFDrawWidgetProxy* proxy,
                                                       PDFPageContentScene* scene,
                                                       QAction* action,
                                                       bool isHorizontal,
                                                       bool isVertical,
                                                       QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementLineTool::onPointPicked);

    QPen pen(Qt::SolidLine);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);

    PDFPageContentElementLine::LineGeometry geometry = PDFPageContentElementLine::LineGeometry::General;

    if (isHorizontal)
    {
        geometry = PDFPageContentElementLine::LineGeometry::Horizontal;
    }

    if (isVertical)
    {
        geometry = PDFPageContentElementLine::LineGeometry::Vertical;
    }

    m_element = new PDFPageContentElementLine();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));
    m_element->setGeometry(geometry);

    updateActions();
}

PDFCreatePCElementLineTool::~PDFCreatePCElementLineTool()
{
    delete m_element;
}

void PDFCreatePCElementLineTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QMatrix& pagePointToDevicePointMatrix,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

    if (pageIndex != m_pickTool->getPageIndex() || !m_startPoint)
    {
        return;
    }

    m_element->setPageIndex(pageIndex);

    QPointF startPoint = *m_startPoint;
    QPointF endPoint = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());
    QLineF line(startPoint, endPoint);

    if (!qFuzzyIsNull(line.length()))
    {
        m_element->setLine(line);
    }

    m_element->drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

void PDFCreatePCElementLineTool::clear()
{
    m_startPoint = std::nullopt;
}

void PDFCreatePCElementLineTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    if (!m_startPoint || m_element->getPageIndex() != pageIndex)
    {
        m_startPoint = pagePoint;
        m_element->setPageIndex(pageIndex);
        m_element->setLine(QLineF(pagePoint, pagePoint));
        return;
    }

    if (qFuzzyCompare(m_startPoint.value().x(), pagePoint.x()) &&
        qFuzzyCompare(m_startPoint.value().y(), pagePoint.y()))
    {
        // Jakub Melka: Point is same as the start point
        clear();
        return;
    }

    QLineF line = m_element->getLine();
    line.setP2(pagePoint);
    m_element->setLine(line);
    m_scene->addElement(m_element->clone());
    clear();

    setActive(false);
}

PDFCreatePCElementSvgTool::PDFCreatePCElementSvgTool(PDFDrawWidgetProxy* proxy,
                                                     PDFPageContentScene* scene,
                                                     QAction* action,
                                                     QByteArray content,
                                                     QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreatePCElementSvgTool::onRectanglePicked);

    m_element = new PDFPageContentSvgElement();
    m_element->setContent(content);

    updateActions();
}

PDFCreatePCElementSvgTool::~PDFCreatePCElementSvgTool()
{
    delete m_element;
}

void PDFCreatePCElementSvgTool::drawPage(QPainter* painter,
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

    QRectF rectangle = getRectangleFromPickTool(m_pickTool, pagePointToDevicePointMatrix);
    if (!rectangle.isValid())
    {
        return;
    }

    m_element->setPageIndex(pageIndex);
    m_element->setRectangle(rectangle);

    {
        PDFPainterStateGuard guard(painter);
        painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::DotLine);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rectangle);
    }

    m_element->drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

void PDFCreatePCElementSvgTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
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

PDFCreatePCElementDotTool::PDFCreatePCElementDotTool(PDFDrawWidgetProxy* proxy,
                                                     PDFPageContentScene* scene,
                                                     QAction* action,
                                                     QObject* parent) :
    BaseClass(proxy, scene, action, parent),
    m_pickTool(nullptr),
    m_element(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreatePCElementDotTool::onPointPicked);

    QPen pen(Qt::SolidLine);
    pen.setWidthF(5.0);
    pen.setCapStyle(Qt::RoundCap);

    m_element = new PDFPageContentElementDot();
    m_element->setBrush(Qt::NoBrush);
    m_element->setPen(std::move(pen));

    updateActions();
}

PDFCreatePCElementDotTool::~PDFCreatePCElementDotTool()
{
    delete m_element;
}

void PDFCreatePCElementDotTool::drawPage(QPainter* painter,
                                         PDFInteger pageIndex,
                                         const PDFPrecompiledPage* compiledPage,
                                         PDFTextLayoutGetter& layoutGetter,
                                         const QMatrix& pagePointToDevicePointMatrix,
                                         QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);

    QPointF point = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(m_element->getPen());
    painter->setBrush(m_element->getBrush());
    painter->drawPoint(point);
}

void PDFCreatePCElementDotTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    m_element->setPageIndex(pageIndex);
    m_element->setPoint(pagePoint);

    m_scene->addElement(m_element->clone());
    m_element->setPageIndex(-1);

    setActive(false);
}

}   // namespace pdf
