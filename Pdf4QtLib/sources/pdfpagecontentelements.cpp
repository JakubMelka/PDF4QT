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
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSvgRenderer>
#include <QApplication>

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

PDFInteger PDFPageContentElement::getElementId() const
{
    return m_elementId;
}

void PDFPageContentElement::setElementId(PDFInteger newElementId)
{
    m_elementId = newElementId;
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
    copy->setElementId(getElementId());
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
    m_firstFreeId(1),
    m_isActive(false),
    m_manipulator(this, nullptr)
{

}

PDFPageContentScene::~PDFPageContentScene()
{

}

void PDFPageContentScene::addElement(PDFPageContentElement* element)
{
    element->setElementId(m_firstFreeId++);
    m_elements.emplace_back(element);
    emit sceneChanged();
}

PDFPageContentElement* PDFPageContentScene::getElementById(PDFInteger id) const
{
    auto it = std::find_if(m_elements.cbegin(), m_elements.cend(), [id](const auto& element) { return element->getElementId() == id; });
    if (it != m_elements.cend())
    {
        return it->get();
    }

    return nullptr;
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
    if (!isActive())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid() || isMouseGrabbed())
    {
        // We to handle selecting/deselecting the active
        // item. After that, accept the item.
        if (info.isValid() && event->button() == Qt::LeftButton)
        {
            info.widgetMouseStartPos = event->pos();
            info.timer.start();

            if (!m_manipulator.isManipulationInProgress())
            {
                Qt::KeyboardModifiers keyboardModifiers = QApplication::keyboardModifiers();
                const bool isCtrl = keyboardModifiers.testFlag(Qt::CTRL);
                const bool isShift = keyboardModifiers.testFlag(Qt::SHIFT);

                if (isCtrl && !isShift)
                {
                    m_manipulator.select(info.hoveredElementIds);
                }
                else if (!isCtrl && isShift)
                {
                    m_manipulator.deselect(info.hoveredElementIds);
                }
                else
                {
                    m_manipulator.selectNew(info.hoveredElementIds);
                }
            }

            event->accept();
        }

        grabMouse(info, event);
    }
}

void PDFPageContentScene::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (!isActive())
    {
        return;
    }

    // If mouse is grabbed, then event is accepted always (because
    // we get Press event, when we grabbed the mouse, then we will
    // wait for corresponding release event while all mouse move events
    // will be accepted, even if editor doesn't accept them.
    if (isMouseGrabbed())
    {
        event->accept();
    }
}

void PDFPageContentScene::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (!isActive())
    {
        return;
    }

    if (isMouseGrabbed())
    {
        if (event->button() == Qt::LeftButton)
        {
            event->accept();
            m_manipulator.finishManipulation();
        }

        ungrabMouse(info, event);
    }
}

void PDFPageContentScene::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    if (!isActive())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {


    }

    // If mouse is grabbed, then event is accepted always (because
    // we get Press event, when we grabbed the mouse, then we will
    // wait for corresponding release event while all mouse move events
    // will be accepted, even if editor doesn't accept them.
    if (isMouseGrabbed())
    {
        event->accept();
    }
}

void PDFPageContentScene::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);

    if (!isActive())
    {
        return;
    }

    // We will accept mouse wheel events, if we are grabbing the mouse.
    // We do not want to zoom in/zoom out while grabbing.
    if (isMouseGrabbed())
    {
        event->accept();
    }
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

PDFPageContentScene::MouseEventInfo PDFPageContentScene::getMouseEventInfo(QWidget* widget, QPoint point)
{
    MouseEventInfo result;

    Q_ASSERT(isActive());

    if (isMouseGrabbed())
    {
        result = m_mouseGrabInfo.info;
        result.widgetMouseCurrentPos = point;
        return result;
    }

    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {

    }

    return result;
}

void PDFPageContentScene::grabMouse(const MouseEventInfo& info, QMouseEvent* event)
{
    Q_ASSERT(isActive());

    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // Double clicks doesn't grab the mouse
        return;
    }

    Q_ASSERT(event->type() == QEvent::MouseButtonPress);

    if (isMouseGrabbed())
    {
        // If mouse is already grabbed, then when new mouse button is pressed,
        // we just increase nesting level and accept the mouse event. We are
        // accepting all mouse events, if mouse is grabbed.
        ++m_mouseGrabInfo.mouseGrabNesting;
        event->accept();
    }
    else if (event->isAccepted())
    {
        // Event is accepted and we are not grabbing the mouse. We must start
        // grabbing the mouse.
        Q_ASSERT(m_mouseGrabInfo.mouseGrabNesting == 0);
        ++m_mouseGrabInfo.mouseGrabNesting;
        m_mouseGrabInfo.info = info;
    }
}

void PDFPageContentScene::ungrabMouse(const MouseEventInfo& info, QMouseEvent* event)
{
    Q_UNUSED(info);
    Q_ASSERT(isActive());
    Q_ASSERT(event->type() == QEvent::MouseButtonRelease);

    if (isMouseGrabbed())
    {
        // Mouse is being grabbed, decrease nesting level. We must also accept
        // mouse release event, because mouse is being grabbed.
        --m_mouseGrabInfo.mouseGrabNesting;
        event->accept();

        if (!isMouseGrabbed())
        {
            m_mouseGrabInfo.info = MouseEventInfo();
        }
    }

    Q_ASSERT(m_mouseGrabInfo.mouseGrabNesting >= 0);
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

        if (!newIsActive)
        {
            m_mouseGrabInfo = MouseGrabInfo();
            m_manipulator.reset();
        }

        emit sceneChanged();
    }
}

void PDFPageContentScene::removeElementsById(const std::set<PDFInteger>& selection)
{
    const size_t oldSize = m_elements.size();
    m_elements.erase(std::remove_if(m_elements.begin(), m_elements.end(), [&selection](const auto& element){ return selection.count(element->getElementId()); }), m_elements.end());
    const size_t newSize = m_elements.size();

    if (newSize < oldSize)
    {
        emit sceneChanged();
    }
}

PDFPageContentElementLine* PDFPageContentElementLine::clone() const
{
    PDFPageContentElementLine* copy = new PDFPageContentElementLine();
    copy->setElementId(getElementId());
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
    copy->setElementId(getElementId());
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
    copy->setElementId(getElementId());
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
    copy->setElementId(getElementId());
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

PDFPageContentElementManipulator::PDFPageContentElementManipulator(PDFPageContentScene* scene, QObject* parent) :
    QObject(parent),
    m_scene(scene),
    m_isManipulationInProgress(false)
{

}

void PDFPageContentElementManipulator::reset()
{
    stopManipulation();
    deselectAll();
}

void PDFPageContentElementManipulator::update(PDFInteger id, SelectionModes modes)
{
    bool modified = false;

    if (modes.testFlag(Clear))
    {
        modified = !m_selection.empty();
        m_selection.clear();
    }

    // Is id valid?
    if (id > 0)
    {
        if (modes.testFlag(Select))
        {
            if (!isSelected(id))
            {
                modified = true;
                m_selection.insert(id);
            }
        }

        if (modes.testFlag(Deselect))
        {
            if (isSelected(id))
            {
                modified = true;
                m_selection.erase(id);
            }
        }

        if (modes.testFlag(Toggle))
        {
            if (isSelected(id))
            {
                m_selection.erase(id);
            }
            else
            {
                m_selection.insert(id);
            }

            // When toggle is performed, selection is always changed
            modified = true;
        }
    }

    if (modified)
    {
        emit selectionChanged();
    }
}

void PDFPageContentElementManipulator::update(const std::set<PDFInteger>& ids, SelectionModes modes)
{
    bool modified = false;

    if (modes.testFlag(Clear))
    {
        modified = !m_selection.empty();
        m_selection.clear();
    }

    // Is id valid?
    if (!ids.empty())
    {
        if (modes.testFlag(Select))
        {
            for (auto id : ids)
            {
                if (!isSelected(id))
                {
                    modified = true;
                    m_selection.insert(id);
                }
            }
        }

        if (modes.testFlag(Deselect))
        {
            for (auto id : ids)
            {
                if (isSelected(id))
                {
                    modified = true;
                    m_selection.erase(id);
                }
            }
        }

        if (modes.testFlag(Toggle))
        {
            for (auto id : ids)
            {
                if (isSelected(id))
                {
                    m_selection.erase(id);
                }
                else
                {
                    m_selection.insert(id);
                }

                // When toggle is performed, selection is always changed
                modified = true;
            }
        }
    }

    if (modified)
    {
        emit selectionChanged();
    }
}

void PDFPageContentElementManipulator::select(PDFInteger id)
{
    update(id, Select);
}

void PDFPageContentElementManipulator::select(const std::set<PDFInteger>& ids)
{
    update(ids, Select);
}

void PDFPageContentElementManipulator::selectNew(PDFInteger id)
{
    update(id, Select | Clear);
}

void PDFPageContentElementManipulator::selectNew(const std::set<PDFInteger>& ids)
{
    update(ids, Select | Clear);
}

void PDFPageContentElementManipulator::deselect(PDFInteger id)
{
    update(id, Deselect);
}

void PDFPageContentElementManipulator::deselect(const std::set<PDFInteger>& ids)
{
    update(ids, Deselect);
}

void PDFPageContentElementManipulator::deselectAll()
{
    update(-1, Clear);
}

void PDFPageContentElementManipulator::manipulateDeleteSelection()
{
    stopManipulation();
    m_scene->removeElementsById(m_selection);
    deselectAll();
}

void PDFPageContentElementManipulator::stopManipulation()
{
    if (m_isManipulationInProgress)
    {
        m_isManipulationInProgress = false;
        m_manipulatedElements.clear();
        m_manipulationModes.clear();
        emit stateChanged();
    }
}

}   // namespace pdf
