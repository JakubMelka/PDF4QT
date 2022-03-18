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
#include "pdfwidgetutils.h"
#include "pdfutils.h"

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

Qt::CursorShape PDFPageContentElement::getCursorShapeForManipulationMode(uint mode)
{
    switch (mode)
    {
        case None:
        case Pt1:
        case Pt2:
        case Translate:
            return Qt::ArrowCursor;

        case Top:
        case Bottom:
            return Qt::SizeVerCursor;

        case Left:
        case Right:
            return Qt::SizeHorCursor;

        case TopLeft:
        case BottomRight:
            return Qt::SizeBDiagCursor;

        case TopRight:
        case BottomLeft:
            return Qt::SizeFDiagCursor;

        default:
            Q_ASSERT(false);
            break;
    }

    return Qt::ArrowCursor;
}

uint PDFPageContentElement::getRectangleManipulationMode(const QRectF& rectangle,
                                                         const QPointF& point,
                                                         PDFReal snapPointDistanceThreshold) const
{
    if ((rectangle.topLeft() - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return TopLeft;
    }

    if ((rectangle.topRight() - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return TopRight;
    }

    if ((rectangle.bottomLeft() - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return BottomLeft;
    }

    if ((rectangle.bottomRight() - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return BottomRight;
    }

    if (rectangle.left() <= point.x() &&
        point.x() <= rectangle.right() &&
        (qAbs(rectangle.top() - point.y()) < snapPointDistanceThreshold))
    {
        return Top;
    }

    if (rectangle.left() <= point.x() &&
        point.x() <= rectangle.right() &&
        (qAbs(rectangle.bottom() - point.y()) < snapPointDistanceThreshold))
    {
        return Bottom;
    }

    if (rectangle.top() <= point.y() &&
        point.y() <= rectangle.bottom() &&
        (qAbs(rectangle.left() - point.x()) < snapPointDistanceThreshold))
    {
        return Left;
    }

    if (rectangle.top() <= point.y() &&
        point.y() <= rectangle.bottom() &&
        (qAbs(rectangle.right() - point.x()) < snapPointDistanceThreshold))
    {
        return Right;
    }

    if (rectangle.contains(point))
    {
        return Translate;
    }

    return None;
}

void PDFPageContentElement::performRectangleManipulation(QRectF& rectangle,
                                                         uint mode,
                                                         const QPointF& offset)
{
    switch (mode)
    {
        case None:
            break;

        case Translate:
            rectangle.translate(offset);
            break;

        case Top:
            rectangle.setTop(qMin(rectangle.bottom(), rectangle.top() + offset.y()));
            break;

        case Left:
            rectangle.setLeft(qMin(rectangle.right(), rectangle.left() + offset.x()));
            break;

        case Right:
            rectangle.setRight(qMax(rectangle.left(), rectangle.right() + offset.x()));
            break;

        case Bottom:
            rectangle.setBottom(qMax(rectangle.top(), rectangle.bottom() + offset.y()));
            break;

        case TopLeft:
            rectangle.setTop(qMin(rectangle.bottom(), rectangle.top() + offset.y()));
            rectangle.setLeft(qMin(rectangle.right(), rectangle.left() + offset.x()));
            break;

        case TopRight:
            rectangle.setTop(qMin(rectangle.bottom(), rectangle.top() + offset.y()));
            rectangle.setRight(qMax(rectangle.left(), rectangle.right() + offset.x()));
            break;

        case BottomLeft:
            rectangle.setBottom(qMax(rectangle.top(), rectangle.bottom() + offset.y()));
            rectangle.setLeft(qMin(rectangle.right(), rectangle.left() + offset.x()));
            break;

        case BottomRight:
            rectangle.setBottom(qMax(rectangle.top(), rectangle.bottom() + offset.y()));
            rectangle.setRight(qMax(rectangle.left(), rectangle.right() + offset.x()));
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFPageContentElement::performRectangleSetSize(QRectF& rectangle, QSizeF size)
{
    const qreal offset = rectangle.size().height() - size.height();
    rectangle.setSize(size);
    rectangle.translate(0, offset);
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

uint PDFPageContentElementRectangle::getManipulationMode(const QPointF& point,
                                                         PDFReal snapPointDistanceThreshold) const
{
    return getRectangleManipulationMode(getRectangle(), point, snapPointDistanceThreshold);
}

void PDFPageContentElementRectangle::performManipulation(uint mode, const QPointF& offset)
{
    performRectangleManipulation(m_rectangle, mode, offset);
}

QRectF PDFPageContentElementRectangle::getBoundingBox() const
{
    return getRectangle();
}

void PDFPageContentElementRectangle::setSize(QSizeF size)
{
    performRectangleSetSize(m_rectangle, size);
}

PDFPageContentScene::PDFPageContentScene(QObject* parent) :
    QObject(parent),
    m_firstFreeId(1),
    m_isActive(false),
    m_widget(nullptr),
    m_manipulator(this, nullptr)
{
    connect(&m_manipulator, &PDFPageContentElementManipulator::selectionChanged, this, &PDFPageContentScene::onSelectionChanged);
}

PDFPageContentScene::~PDFPageContentScene()
{

}

void PDFPageContentScene::addElement(PDFPageContentElement* element)
{
    element->setElementId(m_firstFreeId++);
    m_elements.emplace_back(element);
    emit sceneChanged(false);
}

void PDFPageContentScene::replaceElement(PDFPageContentElement* element)
{
    std::unique_ptr<PDFPageContentElement> elementPtr(element);

    for (size_t i = 0; i < m_elements.size(); ++i)
    {
        if (m_elements[i]->getElementId() == element->getElementId())
        {
            m_elements[i] = std::move(elementPtr);
            emit sceneChanged(false);
            break;
        }
    }
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
        m_manipulator.reset();
        m_elements.clear();
        emit sceneChanged(false);
    }
}

void PDFPageContentScene::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    constexpr QKeySequence::StandardKey acceptedKeys[] = { QKeySequence::Delete,
                                                           QKeySequence::SelectAll,
                                                           QKeySequence::Deselect,
                                                           QKeySequence::Cancel };

    if (std::any_of(std::begin(acceptedKeys), std::end(acceptedKeys), [event](QKeySequence::StandardKey standardKey) { return event == standardKey; }))
    {
        event->accept();
        return;
    }
}

void PDFPageContentScene::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    event->ignore();

    if (event == QKeySequence::Delete)
    {
        if (!m_manipulator.isSelectionEmpty())
        {
            m_manipulator.performDeleteSelection();
            event->accept();
        }
    }
    else if (event == QKeySequence::SelectAll)
    {
        if (!isEmpty())
        {
            m_manipulator.selectAll();
            event->accept();
        }
    }
    else if (event == QKeySequence::Deselect)
    {
        if (!m_manipulator.isSelectionEmpty())
        {
            m_manipulator.deselectAll();
            event->accept();
        }
    }
    else if (event == QKeySequence::Cancel)
    {
        if (m_manipulator.isManipulationInProgress())
        {
            m_manipulator.cancelManipulation();
            m_manipulator.deselectAll();
            event->accept();
        }
    }
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
                const bool isCtrl = keyboardModifiers.testFlag(Qt::ControlModifier);
                const bool isShift = keyboardModifiers.testFlag(Qt::ShiftModifier);

                if (isCtrl && !isShift)
                {
                    m_manipulator.select(info.hoveredElementIds);
                }
                else if (!isCtrl && isShift)
                {
                    m_manipulator.deselect(info.hoveredElementIds);
                }
                else if (!m_manipulator.isAllSelected(info.hoveredElementIds))
                {
                    m_manipulator.selectNew(info.hoveredElementIds);
                }
            }

            event->accept();
        }

        grabMouse(info, event);
    }
    else if (event->button() == Qt::LeftButton)
    {
        m_manipulator.deselectAll();
    }

    updateMouseCursor(info, getSnapPointDistanceThreshold());
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

            if (m_manipulator.isManipulationInProgress())
            {
                QPointF pagePoint;
                const PDFInteger pageIndex = m_widget->getDrawWidgetProxy()->getPageUnderPoint(event->pos(), &pagePoint);
                m_mouseGrabInfo.info.widgetMouseCurrentPos = event->pos();
                bool isCopyCreated = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
                m_manipulator.finishManipulation(pageIndex, m_mouseGrabInfo.info.pagePos, pagePoint, isCopyCreated);
            }
        }

        ungrabMouse(getMouseEventInfo(widget, event->pos()), event);
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    updateMouseCursor(info, getSnapPointDistanceThreshold());
}

void PDFPageContentScene::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (!isActive())
    {
        return;
    }

    const PDFReal threshold = getSnapPointDistanceThreshold();

    QPointF pagePoint;
    const PDFInteger pageIndex = m_widget->getDrawWidgetProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (m_mouseGrabInfo.info.isValid() &&
        event->buttons().testFlag(Qt::LeftButton) &&
        m_mouseGrabInfo.info.pageIndex == pageIndex &&
        m_manipulator.isManipulationAllowed(pageIndex))
    {
        // Jakub Melka: Start grab?
        m_mouseGrabInfo.info.widgetMouseCurrentPos = event->pos();

        if (!m_manipulator.isManipulationInProgress())
        {
            QPoint vector = m_mouseGrabInfo.info.widgetMouseCurrentPos - m_mouseGrabInfo.info.widgetMouseStartPos;
            if (vector.manhattanLength() > QApplication::startDragDistance() ||
                    m_mouseGrabInfo.info.timer.hasExpired(QApplication::startDragTime()))
            {
                m_manipulator.startManipulation(pageIndex, m_mouseGrabInfo.info.pagePos, pagePoint, threshold);
            }
        }
        else
        {
            m_manipulator.updateManipulation(pageIndex, m_mouseGrabInfo.info.pagePos, pagePoint);
        }
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    updateMouseCursor(info, threshold);

    // If mouse is grabbed, then event is accepted always (because
    // we get Press event, when we grabbed the mouse, then we will
    // wait for corresponding release event while all mouse move events
    // will be accepted, even if editor doesn't accept them.
    if (isMouseGrabbed())
    {
        event->accept();
    }

    if (m_manipulator.isManipulationInProgress())
    {
        emit sceneChanged(true);
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

    m_manipulator.drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
}

PDFPageContentScene::MouseEventInfo PDFPageContentScene::getMouseEventInfo(QWidget* widget, QPoint point)
{
    MouseEventInfo result;

    Q_UNUSED(widget);
    Q_ASSERT(isActive());

    if (isMouseGrabbed())
    {
        result = m_mouseGrabInfo.info;
        result.widgetMouseCurrentPos = point;
        return result;
    }

    result.widgetMouseStartPos = point;
    result.widgetMouseCurrentPos = point;
    result.timer = m_mouseGrabInfo.info.timer;
    result.pageIndex = m_widget->getDrawWidgetProxy()->getPageUnderPoint(point, &result.pagePos);

    const PDFReal threshold = getSnapPointDistanceThreshold();
    for (const auto& elementItem : m_elements)
    {
        PDFPageContentElement* element = elementItem.get();

        if (element->getPageIndex() != result.pageIndex)
        {
            // Different page
            continue;
        }

        if (element->getManipulationMode(result.pagePos, threshold) != 0)
        {
            result.hoveredElementIds.insert(element->getElementId());
        }
    }

    return result;
}

PDFReal PDFPageContentScene::getSnapPointDistanceThreshold() const
{
    const PDFReal snapPointDistanceThresholdPixels = PDFWidgetUtils::scaleDPI_x(m_widget, 6.0);
    const PDFReal snapPointDistanceThreshold = m_widget->getDrawWidgetProxy()->transformPixelToDeviceSpace(snapPointDistanceThresholdPixels);
    return snapPointDistanceThreshold;
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

void PDFPageContentScene::updateMouseCursor(const MouseEventInfo& info, PDFReal snapPointDistanceThreshold)
{
    std::optional<Qt::CursorShape> cursorShapeValue;

    for (const PDFInteger id : info.hoveredElementIds)
    {
        PDFPageContentElement* element = getElementById(id);
        uint manipulationMode = element->getManipulationMode(info.pagePos, snapPointDistanceThreshold);

        if (manipulationMode > 0)
        {
            Qt::CursorShape cursorShape = PDFPageContentElement::getCursorShapeForManipulationMode(manipulationMode);

            if (!cursorShapeValue)
            {
                cursorShapeValue = cursorShape;
            }
            else if (cursorShapeValue.value() != cursorShape)
            {
                cursorShapeValue = Qt::ArrowCursor;
                break;
            }
        }
    }

    if (cursorShapeValue && cursorShapeValue.value() == Qt::ArrowCursor &&
        m_manipulator.isManipulationInProgress())
    {
        const bool isCopy = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
        cursorShapeValue = isCopy ? Qt::DragCopyCursor : Qt::DragMoveCursor;
    }

    // Update cursor shape
    if (!cursorShapeValue)
    {
        m_cursor = std::nullopt;
    }
    else if (!m_cursor.has_value() || m_cursor.value().shape() != cursorShapeValue.value())
    {
        m_cursor = QCursor(cursorShapeValue.value());
    }
}

void PDFPageContentScene::onSelectionChanged()
{
    emit sceneChanged(true);
}

PDFWidget* PDFPageContentScene::widget() const
{
    return m_widget;
}

void PDFPageContentScene::setWidget(PDFWidget* newWidget)
{
    m_widget = newWidget;
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

        emit sceneChanged(false);
    }
}

std::set<PDFInteger> PDFPageContentScene::getElementIds() const
{
    std::set<PDFInteger> result;

    for (const auto& element : m_elements)
    {
        result.insert(element->getElementId());
    }

    return result;
}

void PDFPageContentScene::removeElementsById(const std::vector<PDFInteger>& selection)
{
    const size_t oldSize = m_elements.size();
    m_elements.erase(std::remove_if(m_elements.begin(), m_elements.end(), [&selection](const auto& element){ return std::find(selection.cbegin(), selection.cend(), element->getElementId()) != selection.cend(); }), m_elements.end());
    const size_t newSize = m_elements.size();

    if (newSize < oldSize)
    {
        emit sceneChanged(false);
    }
}

void PDFPageContentScene::performOperation(int operation)
{
    m_manipulator.performOperation(static_cast<PDFPageContentElementManipulator::Operation>(operation));
}

const PDFDocument* PDFPageContentScene::getDocument() const
{
    if (m_widget)
    {
        return m_widget->getDrawWidgetProxy()->getDocument();
    }

    return nullptr;
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

uint PDFPageContentElementLine::getManipulationMode(const QPointF& point,
                                                    PDFReal snapPointDistanceThreshold) const
{
    if ((m_line.p1() - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return Pt1;
    }

    if ((m_line.p2() - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return Pt2;
    }

    QPointF vl = m_line.p2() - m_line.p1();
    QPointF vp = point - m_line.p1();

    const qreal lengthSquared = QPointF::dotProduct(vl, vl);

    if (qFuzzyIsNull(lengthSquared))
    {
        return None;
    }

    const qreal t = QPointF::dotProduct(vl, vp) / lengthSquared;
    if (t >= 0.0 && t <= 1.0)
    {
        QPointF projectedPoint = m_line.p1() + t * vl;
        if ((point - projectedPoint).manhattanLength() < snapPointDistanceThreshold)
        {
            return Translate;
        }
    }

    return None;
}

void PDFPageContentElementLine::performManipulation(uint mode, const QPointF& offset)
{
    switch (mode)
    {
        case None:
            break;

        case Pt1:
            m_line.setP1(m_line.p1() + offset);
            break;

        case Pt2:
            m_line.setP2(m_line.p2() + offset);
            break;

        case Translate:
            m_line.translate(offset);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

QRectF PDFPageContentElementLine::getBoundingBox() const
{
    if (!qFuzzyIsNull(m_line.length()))
    {
        const qreal xMin = qMin(m_line.p1().x(), m_line.p2().x());
        const qreal xMax = qMax(m_line.p1().x(), m_line.p2().x());
        const qreal yMin = qMin(m_line.p1().y(), m_line.p2().y());
        const qreal yMax = qMax(m_line.p1().y(), m_line.p2().y());
        return QRectF(xMin, yMin, xMax - xMin, yMax - yMin);
    }

    return QRectF();
}

void PDFPageContentElementLine::setSize(QSizeF size)
{
    QPointF p1 = m_line.p1();
    QPointF p2 = m_line.p2();

    if (p1.x() < p2.x())
    {
        p2.setX(p1.x() + size.width());
    }
    else
    {
        p1.setX(p2.x() + size.width());
    }

    if (p1.y() < p2.y())
    {
        p1.setY(p2.y() - size.height());
    }
    else
    {
        p2.setY(p1.y() - size.height());
    }

    m_line.setPoints(p1, p2);
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

uint PDFPageContentSvgElement::getManipulationMode(const QPointF& point, PDFReal snapPointDistanceThreshold) const
{
    return getRectangleManipulationMode(getRectangle(), point, snapPointDistanceThreshold);
}

void PDFPageContentSvgElement::performManipulation(uint mode, const QPointF& offset)
{
    performRectangleManipulation(m_rectangle, mode, offset);
}

QRectF PDFPageContentSvgElement::getBoundingBox() const
{
    return getRectangle();
}

void PDFPageContentSvgElement::setSize(QSizeF size)
{
    performRectangleSetSize(m_rectangle, size);
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

uint PDFPageContentElementDot::getManipulationMode(const QPointF& point,
                                                   PDFReal snapPointDistanceThreshold) const
{
    if ((m_point - point).manhattanLength() < snapPointDistanceThreshold)
    {
        return Translate;
    }

    return None;
}

void PDFPageContentElementDot::performManipulation(uint mode, const QPointF& offset)
{
    switch (mode)
    {
        case None:
            break;

        case Translate:
            m_point += offset;
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

QRectF PDFPageContentElementDot::getBoundingBox() const
{
    return QRectF(m_point, QSizeF(0.001, 0.001));
}

void PDFPageContentElementDot::setSize(QSizeF size)
{
    Q_UNUSED(size);
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

uint PDFPageContentElementFreehandCurve::getManipulationMode(const QPointF& point, PDFReal snapPointDistanceThreshold) const
{
    Q_UNUSED(snapPointDistanceThreshold);

    if (m_curve.isEmpty())
    {
        return None;
    }

    if (m_curve.controlPointRect().contains(point))
    {
        return Translate;
    }

    return None;
}

void PDFPageContentElementFreehandCurve::performManipulation(uint mode, const QPointF& offset)
{
    switch (mode)
    {
        case None:
            break;

        case Translate:
            m_curve.translate(offset);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

QRectF PDFPageContentElementFreehandCurve::getBoundingBox() const
{
    return m_curve.controlPointRect();
}

void PDFPageContentElementFreehandCurve::setSize(QSizeF size)
{
    Q_UNUSED(size);
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

bool PDFPageContentElementManipulator::isSelected(PDFInteger id) const
{
    return std::find(m_selection.cbegin(), m_selection.cend(), id) != m_selection.cend();
}

bool PDFPageContentElementManipulator::isAllSelected(const std::set<PDFInteger>& elementIds) const
{
    return std::all_of(elementIds.cbegin(), elementIds.cend(), [this](PDFInteger id) { return isSelected(id); });
}

void PDFPageContentElementManipulator::reset()
{
    cancelManipulation();
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
                m_selection.push_back(id);
            }
        }

        if (modes.testFlag(Deselect))
        {
            if (isSelected(id))
            {
                modified = true;
                eraseSelectedElementById(id);
            }
        }

        if (modes.testFlag(Toggle))
        {
            if (isSelected(id))
            {
                eraseSelectedElementById(id);
            }
            else
            {
                m_selection.push_back(id);
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
                    m_selection.push_back(id);
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
                    eraseSelectedElementById(id);
                }
            }
        }

        if (modes.testFlag(Toggle))
        {
            for (auto id : ids)
            {
                if (isSelected(id))
                {
                    eraseSelectedElementById(id);
                }
                else
                {
                    m_selection.push_back(id);
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
    update(id, SelectionModes(Select | Clear));
}

void PDFPageContentElementManipulator::selectNew(const std::set<PDFInteger>& ids)
{
    update(ids, SelectionModes(Select | Clear));
}

void PDFPageContentElementManipulator::deselect(PDFInteger id)
{
    update(id, Deselect);
}

void PDFPageContentElementManipulator::deselect(const std::set<PDFInteger>& ids)
{
    update(ids, Deselect);
}

void PDFPageContentElementManipulator::selectAll()
{
    std::set<PDFInteger> ids = m_scene->getElementIds();
    update(ids, Select);
}

void PDFPageContentElementManipulator::deselectAll()
{
    update(-1, Clear);
}

bool PDFPageContentElementManipulator::isManipulationAllowed(PDFInteger pageIndex) const
{
    for (const PDFInteger id : m_selection)
    {
        if (const PDFPageContentElement* element = m_scene->getElementById(id))
        {
            if (element->getPageIndex() == pageIndex)
            {
                return true;
            }
        }
    }

    return false;
}

void PDFPageContentElementManipulator::performOperation(Operation operation)
{
    if (isSelectionEmpty())
    {
        // Jakub Melka: nothing selected
        return;
    }

    QRectF representativeRect = getSelectionBoundingRect();
    std::vector<PDFPageContentElement*> manipulatedElements;
    for (const PDFInteger id : m_selection)
    {
        const PDFPageContentElement* element = m_scene->getElementById(id);
        manipulatedElements.push_back(element->clone());
    }

    switch (operation)
    {
        case Operation::AlignTop:
        {
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                const qreal offset = representativeRect.bottom() - boundingBox.bottom();
                element->performManipulation(PDFPageContentElement::Translate, QPointF(0.0, offset));
            }
            break;
        }

        case Operation::AlignCenterVertically:
        {
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                const qreal offset = representativeRect.center().y() - boundingBox.center().y();
                element->performManipulation(PDFPageContentElement::Translate, QPointF(0.0, offset));
            }
            break;
        }

        case Operation::AlignBottom:
        {
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                const qreal offset = representativeRect.top() - boundingBox.top();
                element->performManipulation(PDFPageContentElement::Translate, QPointF(0.0, offset));
            }
            break;
        }

        case Operation::AlignLeft:
        {
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                const qreal offset = representativeRect.left() - boundingBox.left();
                element->performManipulation(PDFPageContentElement::Translate, QPointF(offset, 0.0));
            }
            break;
        }

        case Operation::AlignCenterHorizontally:
        {
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                const qreal offset = representativeRect.center().x() - boundingBox.center().x();
                element->performManipulation(PDFPageContentElement::Translate, QPointF(offset, 0.0));
            }
            break;
        }

        case Operation::AlignRight:
        {
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                const qreal offset = representativeRect.right() - boundingBox.right();
                element->performManipulation(PDFPageContentElement::Translate, QPointF(offset, 0.0));
            }
            break;
        }

        case Operation::SetSameHeight:
        {
            QSizeF size = manipulatedElements.front()->getBoundingBox().size();

            for (PDFPageContentElement* element : manipulatedElements)
            {
                size.setWidth(element->getBoundingBox().width());
                element->setSize(size);
            }
            break;
        }

        case Operation::SetSameWidth:
        {
            QSizeF size = manipulatedElements.front()->getBoundingBox().size();

            for (PDFPageContentElement* element : manipulatedElements)
            {
                size.setHeight(element->getBoundingBox().height());
                element->setSize(size);
            }
            break;
        }

        case Operation::SetSameSize:
        {
            QSizeF size = manipulatedElements.front()->getBoundingBox().size();

            for (PDFPageContentElement* element : manipulatedElements)
            {
                element->setSize(size);
            }
            break;
        }

        case Operation::CenterHorizontally:
        {
            const PDFInteger pageIndex = manipulatedElements.front()->getPageIndex();
            QRectF pageMediaBox = getPageMediaBox(pageIndex);
            const qreal offset = pageMediaBox.center().x() - representativeRect.center().x();

            for (PDFPageContentElement* element : manipulatedElements)
            {
                element->performManipulation(PDFPageContentElement::Translate, QPointF(offset, 0.0));
            }
            break;
        }

        case Operation::CenterVertically:
        {
            const PDFInteger pageIndex = manipulatedElements.front()->getPageIndex();
            QRectF pageMediaBox = getPageMediaBox(pageIndex);
            const qreal offset = pageMediaBox.center().y() - representativeRect.center().y();

            for (PDFPageContentElement* element : manipulatedElements)
            {
                element->performManipulation(PDFPageContentElement::Translate, QPointF(0.0, offset));
            }
            break;
        }

        case Operation::CenterHorAndVert:
        {
            const PDFInteger pageIndex = manipulatedElements.front()->getPageIndex();
            QRectF pageMediaBox = getPageMediaBox(pageIndex);
            const qreal offsetX = pageMediaBox.center().x() - representativeRect.center().x();
            const qreal offsetY = pageMediaBox.center().y() - representativeRect.center().y();
            QPointF offset(offsetX, offsetY);

            for (PDFPageContentElement* element : manipulatedElements)
            {
                element->performManipulation(PDFPageContentElement::Translate, offset);
            }
            break;
        }

        case Operation::LayoutVertically:
        {
            auto comparator = [](PDFPageContentElement* left, PDFPageContentElement* right)
            {
                QRectF r1 = left->getBoundingBox();
                QRectF r2 = right->getBoundingBox();

                return r1.y() > r2.y();
            };
            std::stable_sort(manipulatedElements.begin(), manipulatedElements.end(), comparator);

            qreal yTop = representativeRect.bottom();
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                QPointF offset(0.0, yTop - boundingBox.bottom());
                element->performManipulation(PDFPageContentElement::Translate, offset);
                yTop -= boundingBox.height();
            }

            break;
        }

        case Operation::LayoutHorizontally:
        {
            auto comparator = [](PDFPageContentElement* left, PDFPageContentElement* right)
            {
                QRectF r1 = left->getBoundingBox();
                QRectF r2 = right->getBoundingBox();

                return r1.x() < r2.x();
            };
            std::stable_sort(manipulatedElements.begin(), manipulatedElements.end(), comparator);

            qreal x = representativeRect.left();
            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();
                QPointF offset(x - boundingBox.left(), 0.0);
                element->performManipulation(PDFPageContentElement::Translate, offset);
                x += boundingBox.width();
            }

            break;
        }

        case Operation::LayoutForm:
        {
            std::vector<std::pair<PDFPageContentElement*, PDFPageContentElement*>> formLayout;

            // Divide elements to left/right side
            std::vector<PDFPageContentElement*> elementsLeft;
            std::vector<PDFPageContentElement*> elementsRight;

            for (PDFPageContentElement* element : manipulatedElements)
            {
                QRectF boundingBox = element->getBoundingBox();

                const qreal distanceLeft = boundingBox.left() - representativeRect.left();
                const qreal distanceRight = representativeRect.right() - boundingBox.right();

                if (distanceLeft < distanceRight)
                {
                    elementsLeft.push_back(element);
                }
                else
                {
                    elementsRight.push_back(element);
                }
            }

            // Create pairs of left/right elements
            while (!elementsLeft.empty())
            {
                if (!elementsLeft.empty() && !elementsRight.empty())
                {
                    PDFPageContentElement* elementRight = nullptr;
                    PDFPageContentElement* elementLeft = elementsLeft.back();
                    QRectF leftBoundingBox = elementLeft->getBoundingBox();
                    elementsLeft.pop_back();
                    qreal overlap = 0.0;

                    // Find matching element on the right
                    for (PDFPageContentElement* elementRightCurrent : elementsRight)
                    {
                        QRectF rightBoundingBox = elementRightCurrent->getBoundingBox();
                        if (isRectangleVerticallyOverlapped(leftBoundingBox, rightBoundingBox))
                        {
                            QRectF unitedRect = leftBoundingBox.united(rightBoundingBox);
                            qreal currentOverlap = leftBoundingBox.height() + rightBoundingBox.height() - unitedRect.height();

                            if (currentOverlap > overlap)
                            {
                                overlap = currentOverlap;
                                elementRight = elementRightCurrent;
                            }
                        }
                    }

                    if (elementRight)
                    {
                        auto it = std::find(elementsRight.begin(), elementsRight.end(), elementRight);
                        elementsRight.erase(it);
                    }

                    formLayout.emplace_back(elementLeft, elementRight);
                    continue;
                }
            }

            for (PDFPageContentElement* rightElement : elementsRight)
            {
                formLayout.emplace_back(nullptr, rightElement);
            }

            // Sort elements vertically
            auto comparator = [](const auto& left, const auto& right)
            {
                const PDFPageContentElement* l1 = left.first ? left.first : left.second;
                const PDFPageContentElement* l2 = left.second ? left.second : left.first;

                const PDFPageContentElement* r1 = right.first ? right.first : right.second;
                const PDFPageContentElement* r2 = right.second ? right.second : right.first;

                QRectF lbb1 = l1->getBoundingBox();
                QRectF lbb2 = l2->getBoundingBox();
                QRectF rbb1 = r1->getBoundingBox();
                QRectF rbb2 = r2->getBoundingBox();

                const qreal ly = (lbb1.center().y() + lbb2.center().y()) * 0.5;
                const qreal ry = (rbb1.center().y() + rbb2.center().y()) * 0.5;

                return ly > ry;
            };
            std::stable_sort(formLayout.begin(), formLayout.end(), comparator);

            // Calculate width
            qreal leftWidth = 0.0;
            qreal rightWidth = 0.0;

            for (const auto& row : formLayout)
            {
                PDFPageContentElement* elementLeft = row.first;
                PDFPageContentElement* elementRight = row.second;

                if (elementLeft)
                {
                    qreal width = elementLeft->getBoundingBox().width();
                    leftWidth = qMax(leftWidth, width);
                }

                if (elementRight)
                {
                    qreal width = elementRight->getBoundingBox().width();
                    rightWidth = qMax(rightWidth, width);
                }
            }

            // Now, perform layout
            qreal yTop = representativeRect.bottom();
            qreal xLeft = representativeRect.left();
            qreal xRight = representativeRect.right() - rightWidth;

            for (const auto& row : formLayout)
            {
                PDFPageContentElement* elementLeft = row.first;
                PDFPageContentElement* elementRight = row.second;

                qreal yHeight = 0.0;

                if (elementLeft)
                {
                    QRectF boundingBoxLeft = elementLeft->getBoundingBox();
                    QPointF offsetLeft(xLeft - boundingBoxLeft.x(), yTop - boundingBoxLeft.bottom());
                    elementLeft->performManipulation(PDFPageContentElement::Translate, offsetLeft);
                    yHeight = qMax(yHeight, boundingBoxLeft.height());
                }

                if (elementRight)
                {
                    QRectF boundingBoxRight = elementRight->getBoundingBox();
                    QPointF offsetRight(xRight - boundingBoxRight.x(), yTop - boundingBoxRight.bottom());
                    elementRight->performManipulation(PDFPageContentElement::Translate, offsetRight);
                    yHeight = qMax(yHeight, boundingBoxRight.height());
                }

                yTop -= yHeight;
            }

            break;
        }

        case Operation::LayoutGrid:
            break;
    }

    for (PDFPageContentElement* element : manipulatedElements)
    {
        m_scene->replaceElement(element);
    }
}

void PDFPageContentElementManipulator::performDeleteSelection()
{
    cancelManipulation();
    m_scene->removeElementsById(m_selection);
    deselectAll();
}

void PDFPageContentElementManipulator::startManipulation(PDFInteger pageIndex,
                                                         const QPointF& startPoint,
                                                         const QPointF& currentPoint,
                                                         PDFReal snapPointDistanceThreshold)
{
    Q_ASSERT(!isManipulationInProgress());

    // Collect elements to be manipulated
    for (const PDFInteger id : m_selection)
    {
        const PDFPageContentElement* element = m_scene->getElementById(id);

        if (!element || element->getPageIndex() != pageIndex)
        {
            continue;
        }

        const uint manipulationMode = element->getManipulationMode(startPoint, snapPointDistanceThreshold);
        if (manipulationMode)
        {
            // Jakub Melka: yes, we can manipulate this element
            m_manipulatedElements.emplace_back(element->clone());
            m_manipulationModes[id] = manipulationMode;
        }
    }

    if (!m_manipulatedElements.empty())
    {
        m_isManipulationInProgress = true;
        m_lastUpdatedPoint = startPoint;
        updateManipulation(pageIndex, startPoint, currentPoint);
        emit stateChanged();
    }
}

void PDFPageContentElementManipulator::updateManipulation(PDFInteger pageIndex,
                                                          const QPointF& startPoint,
                                                          const QPointF& currentPoint)
{
    Q_UNUSED(startPoint);

    QPointF offset = currentPoint - m_lastUpdatedPoint;

    for (const auto& element : m_manipulatedElements)
    {
        if (element->getPageIndex() == pageIndex)
        {
            element->performManipulation(m_manipulationModes[element->getElementId()], offset);
        }
    }

    m_lastUpdatedPoint = currentPoint;
    emit stateChanged();
}

void PDFPageContentElementManipulator::finishManipulation(PDFInteger pageIndex,
                                                          const QPointF& startPoint,
                                                          const QPointF& currentPoint,
                                                          bool createCopy)
{
    Q_ASSERT(isManipulationInProgress());
    updateManipulation(pageIndex, startPoint, currentPoint);

    if (createCopy)
    {
        for (const auto& element : m_manipulatedElements)
        {
            m_scene->addElement(element->clone());
        }
    }
    else
    {
        for (const auto& element : m_manipulatedElements)
        {
            m_scene->replaceElement(element->clone());
        }
    }

    cancelManipulation();
}

void PDFPageContentElementManipulator::cancelManipulation()
{
    if (isManipulationInProgress())
    {
        m_isManipulationInProgress = false;
        m_manipulatedElements.clear();
        m_manipulationModes.clear();
        emit stateChanged();
    }

    Q_ASSERT(!m_isManipulationInProgress);
    Q_ASSERT(m_manipulatedElements.empty());
    Q_ASSERT(m_manipulationModes.empty());
}

void PDFPageContentElementManipulator::drawPage(QPainter* painter,
                                                PDFInteger pageIndex,
                                                const PDFPrecompiledPage* compiledPage,
                                                PDFTextLayoutGetter& layoutGetter,
                                                const QMatrix& pagePointToDevicePointMatrix,
                                                QList<PDFRenderError>& errors) const
{
    // Draw selection
    if (!isSelectionEmpty())
    {
        QPainterPath selectionPath;
        for (const PDFInteger id : m_selection)
        {
            if (PDFPageContentElement* element = m_scene->getElementById(id))
            {
                QPainterPath tempPath;
                tempPath.addRect(element->getBoundingBox());
                selectionPath = selectionPath.united(tempPath);
            }
        }

        if (!selectionPath.isEmpty())
        {
            PDFPainterStateGuard guard(painter);
            QPen pen(Qt::SolidLine);
            pen.setWidthF(2.0);
            pen.setColor(QColor::fromRgbF(0.8, 0.8, 0.1, 0.7));
            QBrush brush(Qt::SolidPattern);
            brush.setColor(QColor::fromRgbF(1.0, 1.0, 0.0, 0.2));

            painter->setPen(std::move(pen));
            painter->setBrush(std::move(brush));

            selectionPath = pagePointToDevicePointMatrix.map(selectionPath);
            painter->drawPath(selectionPath);
        }
    }

    // Draw dragged items
    if (isManipulationInProgress())
    {
        PDFPainterStateGuard guard(painter);
        painter->setOpacity(0.2);

        for (const auto& manipulatedElement : m_manipulatedElements)
        {
            manipulatedElement->drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, errors);
        }
    }
}

QRectF PDFPageContentElementManipulator::getSelectionBoundingRect() const
{
    QRectF rect;

    for (const PDFInteger elementId : m_selection)
    {
        if (const PDFPageContentElement* element = m_scene->getElementById(elementId))
        {
            rect = rect.united(element->getBoundingBox());
        }
    }

    return rect;
}

QRectF PDFPageContentElementManipulator::getPageMediaBox(PDFInteger pageIndex) const
{
    if (pageIndex < 0)
    {
        return QRectF();
    }

    if (const PDFDocument* document = m_scene->getDocument())
    {
        size_t pageCount = document->getCatalog()->getPageCount();
        if (size_t(pageIndex) < pageCount)
        {
            const PDFPage* page = document->getCatalog()->getPage(pageIndex);
            return page->getMediaBox();
        }
    }

    return QRectF();
}

void PDFPageContentElementManipulator::eraseSelectedElementById(PDFInteger id)
{
    auto it = std::find(m_selection.begin(), m_selection.end(), id);
    if (it != m_selection.end())
    {
        m_selection.erase(it);
    }
}

}   // namespace pdf
