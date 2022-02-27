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

#include "pdfpagecontentelements.h"
#include "pdfpainterutils.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSvgRenderer>

namespace pdf
{

PDFInteger PDFPageContentElement::getPageIndex() const
{
    return m_pageIndex;
}

void PDFPageContentElement::setPageIndex(PDFInteger newPageIndex)
{
    m_pageIndex = newPageIndex;
}

const QPen& PDFPageContentStyledElement::getPen() const
{
    return m_pen;
}

void PDFPageContentStyledElement::setPen(const QPen& newPen)
{
    m_pen = newPen;
}

const QBrush& PDFPageContentStyledElement::getBrush() const
{
    return m_brush;
}

void PDFPageContentStyledElement::setBrush(const QBrush& newBrush)
{
    m_brush = newBrush;
}

PDFPageContentElementRectangle* PDFPageContentElementRectangle::clone() const
{
    PDFPageContentElementRectangle* copy = new PDFPageContentElementRectangle();
    copy->setPageIndex(getPageIndex());
    copy->setPen(getPen());
    copy->setBrush(getBrush());
    copy->setRectangle(getRectangle());
    copy->setRounded(isRounded());
    return copy;
}

bool PDFPageContentElementRectangle::isRounded() const
{
    return m_rounded;
}

void PDFPageContentElementRectangle::setRounded(bool newRounded)
{
    m_rounded = newRounded;
}

const QRectF& PDFPageContentElementRectangle::getRectangle() const
{
    return m_rectangle;
}

void PDFPageContentElementRectangle::setRectangle(const QRectF& newRectangle)
{
    m_rectangle = newRectangle;
}

void PDFPageContentElementRectangle::drawPage(QPainter* painter,
                                              PDFInteger pageIndex,
                                              const PDFPrecompiledPage* compiledPage,
                                              PDFTextLayoutGetter& layoutGetter,
                                              const QMatrix& pagePointToDevicePointMatrix,
                                              QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (pageIndex != getPageIndex())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setPen(getPen());
    painter->setBrush(getBrush());
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF rect = getRectangle();
    if (isRounded())
    {
        qreal radius = qMin(rect.width(), rect.height()) * 0.25;
        painter->drawRoundedRect(rect, radius, radius, Qt::AbsoluteSize);
    }
    else
    {
        painter->drawRect(rect);
    }
}

PDFPageContentScene::PDFPageContentScene(QObject* parent) :
    QObject(parent),
    m_isActive(false)
{

}

PDFPageContentScene::~PDFPageContentScene()
{

}

void PDFPageContentScene::addElement(PDFPageContentElement* element)
{
    m_elements.emplace_back(element);
    emit sceneChanged();
}

void PDFPageContentScene::clear()
{
    if (!m_elements.empty())
    {
        m_elements.clear();
        emit sceneChanged();
    }
}

void PDFPageContentScene::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

void PDFPageContentScene::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();
}

QString PDFPageContentScene::getTooltip() const
{
    return QString();
}

const std::optional<QCursor>& PDFPageContentScene::getCursor() const
{
    return m_cursor;
}

int PDFPageContentScene::getInputPriority() const
{
    return ToolPriority + 1;
}

void PDFPageContentScene::drawPage(QPainter* painter,
                                   PDFInteger pageIndex,
                                   const PDFPrecompiledPage* compiledPage,
                                   PDFTextLayoutGetter& layoutGetter,
                                   const QMatrix& pagePointToDevicePointMatrix,
                                   QList<PDFRenderError>& errors) const
{
    if (!m_isActive)
    {
        return;
    }

    for (const auto& element : m_elements)
    {
        if (element->getPageIndex() != pageIndex)
        {
            continue;
        }

        element->drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
    }
}

bool PDFPageContentScene::isActive() const
{
    return m_isActive;
}

void PDFPageContentScene::setActive(bool newIsActive)
{
    if (m_isActive != newIsActive)
    {
        m_isActive = newIsActive;
        emit sceneChanged();
    }
}

PDFPageContentElementLine* PDFPageContentElementLine::clone() const
{
    PDFPageContentElementLine* copy = new PDFPageContentElementLine();
    copy->setPageIndex(getPageIndex());
    copy->setPen(getPen());
    copy->setBrush(getBrush());
    copy->setGeometry(getGeometry());
    copy->setLine(getLine());
    return copy;
}

void PDFPageContentElementLine::drawPage(QPainter* painter,
                                         PDFInteger pageIndex,
                                         const PDFPrecompiledPage* compiledPage,
                                         PDFTextLayoutGetter& layoutGetter,
                                         const QMatrix& pagePointToDevicePointMatrix,
                                         QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (pageIndex != getPageIndex())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setPen(getPen());
    painter->setBrush(getBrush());
    painter->setRenderHint(QPainter::Antialiasing);

    painter->drawLine(getLine());
}

PDFPageContentElementLine::LineGeometry PDFPageContentElementLine::getGeometry() const
{
    return m_geometry;
}

void PDFPageContentElementLine::setGeometry(LineGeometry newGeometry)
{
    m_geometry = newGeometry;
}

const QLineF& PDFPageContentElementLine::getLine() const
{
    return m_line;
}

void PDFPageContentElementLine::setLine(const QLineF& newLine)
{
    m_line = newLine;

    if (m_geometry == LineGeometry::Horizontal)
    {
        m_line.setP2(QPointF(newLine.p2().x(), newLine.p1().y()));
    }

    if (m_geometry == LineGeometry::Vertical)
    {
        m_line.setP2(QPointF(newLine.p1().x(), newLine.p2().y()));
    }
}

PDFPageContentSvgElement::PDFPageContentSvgElement() :
    m_renderer(std::make_unique<QSvgRenderer>())
{

}

PDFPageContentSvgElement::~PDFPageContentSvgElement()
{

}

PDFPageContentSvgElement* PDFPageContentSvgElement::clone() const
{
    PDFPageContentSvgElement* copy = new PDFPageContentSvgElement();
    copy->setPageIndex(getPageIndex());
    copy->setRectangle(getRectangle());
    copy->setContent(getContent());
    return copy;
}

void PDFPageContentSvgElement::drawPage(QPainter* painter,
                                        PDFInteger pageIndex,
                                        const PDFPrecompiledPage* compiledPage,
                                        PDFTextLayoutGetter& layoutGetter,
                                        const QMatrix& pagePointToDevicePointMatrix,
                                        QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (pageIndex != getPageIndex() || !getRectangle().isValid())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF viewBox = m_renderer->viewBoxF();
    if (!viewBox.isValid())
    {
        return;
    }

    QRectF renderBox = getRectangle();
    QSizeF viewBoxSize = viewBox.size();
    QSizeF renderBoxSize = viewBoxSize.scaled(renderBox.size(), Qt::KeepAspectRatio);
    QRectF targetRenderBox = QRectF(QPointF(), renderBoxSize);
    targetRenderBox.moveCenter(renderBox.center());

    painter->translate(targetRenderBox.bottomLeft());
    painter->scale(1.0, -1.0);
    targetRenderBox.moveTopLeft(QPointF(0, 0));

    m_renderer->render(painter, targetRenderBox);
}

const QByteArray& PDFPageContentSvgElement::getContent() const
{
    return m_content;
}

void PDFPageContentSvgElement::setContent(const QByteArray& newContent)
{
    if (m_content != newContent)
    {
        m_content = newContent;
        m_renderer->load(m_content);
    }
}

const QRectF& PDFPageContentSvgElement::getRectangle() const
{
    return m_rectangle;
}

void PDFPageContentSvgElement::setRectangle(const QRectF& newRectangle)
{
    m_rectangle = newRectangle;
}

PDFPageContentElementDot* PDFPageContentElementDot::clone() const
{
    PDFPageContentElementDot* copy = new PDFPageContentElementDot();
    copy->setPageIndex(getPageIndex());
    copy->setPen(getPen());
    copy->setBrush(getBrush());
    copy->setPoint(getPoint());
    return copy;
}

void PDFPageContentElementDot::drawPage(QPainter* painter,
                                        PDFInteger pageIndex,
                                        const PDFPrecompiledPage* compiledPage,
                                        PDFTextLayoutGetter& layoutGetter,
                                        const QMatrix& pagePointToDevicePointMatrix,
                                        QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (pageIndex != getPageIndex())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(getPen());
    painter->setBrush(getBrush());
    painter->drawPoint(m_point);
}

QPointF PDFPageContentElementDot::getPoint() const
{
    return m_point;
}

void PDFPageContentElementDot::setPoint(QPointF newPoint)
{
    m_point = newPoint;
}

PDFPageContentElementFreehandCurve* PDFPageContentElementFreehandCurve::clone() const
{
    PDFPageContentElementFreehandCurve* copy = new PDFPageContentElementFreehandCurve();
    copy->setPageIndex(getPageIndex());
    copy->setPen(getPen());
    copy->setBrush(getBrush());
    copy->setCurve(getCurve());
    return copy;
}

void PDFPageContentElementFreehandCurve::drawPage(QPainter* painter,
                                                  PDFInteger pageIndex,
                                                  const PDFPrecompiledPage* compiledPage,
                                                  PDFTextLayoutGetter& layoutGetter,
                                                  const QMatrix& pagePointToDevicePointMatrix,
                                                  QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (pageIndex != getPageIndex())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setWorldMatrix(pagePointToDevicePointMatrix, true);
    painter->setPen(getPen());
    painter->setBrush(getBrush());
    painter->setRenderHint(QPainter::Antialiasing);

    painter->drawPath(getCurve());
}

QPainterPath PDFPageContentElementFreehandCurve::getCurve() const
{
    return m_curve;
}

void PDFPageContentElementFreehandCurve::setCurve(QPainterPath newCurve)
{
    m_curve = newCurve;
}

void PDFPageContentElementFreehandCurve::addStartPoint(const QPointF& point)
{
    m_curve.moveTo(point);
}

void PDFPageContentElementFreehandCurve::addPoint(const QPointF& point)
{
    m_curve.lineTo(point);
}

void PDFPageContentElementFreehandCurve::clear()
{
    setPageIndex(-1);
    m_curve = QPainterPath();
}

}   // namespace pdf
