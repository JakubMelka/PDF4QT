// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolpagestage.h"

#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfwidgetstyle.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace pdf
{

PDFAppCropOverlay::PDFAppCropOverlay(PDFAppToolPageStage* stage) :
    BaseClass(stage),
    m_stage(stage)
{
    setObjectName(QStringLiteral("toolCropOverlay"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("Crop box"));
    if (stage)
    {
        stage->addOverlay(this);
    }
}

PDFAppCropOverlay::~PDFAppCropOverlay() = default;

void PDFAppCropOverlay::setBounds(const QRectF& bounds)
{
    m_bounds = bounds.normalized();
    m_rect = bound(m_rect);
    update();
}

void PDFAppCropOverlay::setRect(const QRectF& rect)
{
    m_rect = bound(rect.normalized());
    update();
}

void PDFAppCropOverlay::setMinimumSize(qreal minimumSize)
{
    m_minimumSize = qMax(qreal(0.0), minimumSize);
    m_rect = bound(m_rect);
    update();
}

void PDFAppCropOverlay::setInactive(bool isInactive)
{
    m_isInactive = isInactive;
    m_dragPart = Part::None;
    updateCursor(Part::None);
    update();
}

QRectF PDFAppCropOverlay::bound(const QRectF& rect) const
{
    if (m_bounds.isEmpty())
    {
        return rect;
    }

    // The size first (at least the minimum, at most the bounds), then the position
    const qreal width = qBound(qMin(m_minimumSize, m_bounds.width()), rect.width(), m_bounds.width());
    const qreal height = qBound(qMin(m_minimumSize, m_bounds.height()), rect.height(), m_bounds.height());
    const qreal left = qBound(m_bounds.left(), rect.left(), m_bounds.right() - width);
    const qreal top = qBound(m_bounds.top(), rect.top(), m_bounds.bottom() - height);
    return QRectF(left, top, width, height);
}

QRectF PDFAppCropOverlay::getBoxRect() const
{
    return m_stage ? m_stage->getPageToStageTransform().mapRect(m_rect) : QRectF();
}

QRectF PDFAppCropOverlay::getHandleRect(Part part) const
{
    const QRectF box = getBoxRect();
    QPointF center;
    switch (part)
    {
        case Part::TopLeft:
            center = box.topLeft();
            break;
        case Part::Top:
            center = QPointF(box.center().x(), box.top());
            break;
        case Part::TopRight:
            center = box.topRight();
            break;
        case Part::Right:
            center = QPointF(box.right(), box.center().y());
            break;
        case Part::BottomRight:
            center = box.bottomRight();
            break;
        case Part::Bottom:
            center = QPointF(box.center().x(), box.bottom());
            break;
        case Part::BottomLeft:
            center = box.bottomLeft();
            break;
        case Part::Left:
            center = QPointF(box.left(), box.center().y());
            break;
        default:
            return QRectF();
    }
    return QRectF(center.x() - HANDLE_SIZE * 0.5, center.y() - HANDLE_SIZE * 0.5, HANDLE_SIZE, HANDLE_SIZE);
}

PDFAppCropOverlay::Part PDFAppCropOverlay::getPartAt(const QPointF& position) const
{
    if (m_isInactive || !m_stage)
    {
        return Part::None;
    }

    // The handles have a larger area for the pointer than their picture
    for (const Part part : { Part::TopLeft, Part::TopRight, Part::BottomRight, Part::BottomLeft, Part::Top, Part::Right, Part::Bottom, Part::Left })
    {
        if (getHandleRect(part).adjusted(-3, -3, 3, 3).contains(position))
        {
            return part;
        }
    }
    return getBoxRect().contains(position) ? Part::Inside : Part::None;
}

QRectF PDFAppCropOverlay::getDraggedRect(Part part, const QRectF& start, const QPointF& delta) const
{
    QRectF rect = start;
    switch (part)
    {
        case Part::Inside:
            rect.translate(delta);
            return bound(rect);
        case Part::TopLeft:
            rect.setTopLeft(rect.topLeft() + delta);
            break;
        case Part::Top:
            rect.setTop(rect.top() + delta.y());
            break;
        case Part::TopRight:
            rect.setTopRight(rect.topRight() + delta);
            break;
        case Part::Right:
            rect.setRight(rect.right() + delta.x());
            break;
        case Part::BottomRight:
            rect.setBottomRight(rect.bottomRight() + delta);
            break;
        case Part::Bottom:
            rect.setBottom(rect.bottom() + delta.y());
            break;
        case Part::BottomLeft:
            rect.setBottomLeft(rect.bottomLeft() + delta);
            break;
        case Part::Left:
            rect.setLeft(rect.left() + delta.x());
            break;
        case Part::None:
            return start;
    }

    // A side, which is dragged, stays inside of the bounds; the other sides stay where they are
    if (!m_bounds.isEmpty())
    {
        rect.setLeft(qMax(rect.left(), m_bounds.left()));
        rect.setTop(qMax(rect.top(), m_bounds.top()));
        rect.setRight(qMin(rect.right(), m_bounds.right()));
        rect.setBottom(qMin(rect.bottom(), m_bounds.bottom()));
    }
    const bool movesLeft = part == Part::TopLeft || part == Part::Left || part == Part::BottomLeft;
    const bool movesTop = part == Part::TopLeft || part == Part::Top || part == Part::TopRight;
    if (rect.width() < m_minimumSize)
    {
        if (movesLeft)
        {
            rect.setLeft(rect.right() - m_minimumSize);
        }
        else
        {
            rect.setRight(rect.left() + m_minimumSize);
        }
    }
    if (rect.height() < m_minimumSize)
    {
        if (movesTop)
        {
            rect.setTop(rect.bottom() - m_minimumSize);
        }
        else
        {
            rect.setBottom(rect.top() + m_minimumSize);
        }
    }
    return bound(rect);
}

void PDFAppCropOverlay::updateCursor(Part part)
{
    switch (part)
    {
        case Part::TopLeft:
        case Part::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Part::TopRight:
        case Part::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Part::Top:
        case Part::Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Part::Left:
        case Part::Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case Part::Inside:
            setCursor(Qt::SizeAllCursor);
            break;
        case Part::None:
            unsetCursor();
            break;
    }
}

void PDFAppCropOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_stage || m_rect.isEmpty())
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = getBoxRect();

    if (m_isInactive)
    {
        // 405:32319: the crop, which will be removed, dashed and gray
        PDFPainterStateGuard guard(&painter);
        QPen pen(PDFStylePainter::color(PDFThemeColors::BorderStrong), BOX_WIDTH, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box);
        return;
    }

    // The area of the bounds outside of the box darkened (389:22382)
    const QRectF bounds = m_stage->getPageToStageTransform().mapRect(m_bounds.isEmpty() ? m_rect : m_bounds);
    QPainterPath dimmed;
    dimmed.addRect(bounds);
    QPainterPath inner;
    inner.addRect(box);
    dimmed = dimmed.subtracted(inner);
    painter.fillPath(dimmed, PDFStylePainter::color(PDFThemeColors::BgScrim));

    {
        PDFPainterStateGuard guard(&painter);
        painter.setPen(QPen(PDFStylePainter::color(PDFThemeColors::BgAccent), BOX_WIDTH));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box);
    }

    for (const Part part : { Part::TopLeft, Part::Top, Part::TopRight, Part::Right, Part::BottomRight, Part::Bottom, Part::BottomLeft, Part::Left })
    {
        const QRectF handle = getHandleRect(part);
        PDFStylePainter::fillRect(&painter, handle, PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(&painter, handle, PDFStylePainter::Radii(0), PDFThemeColors::BgAccent, 1.0);
    }

    // The focus ring around the box (after the keyboard only)
    if (hasFocus() && PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFStylePainter::strokeInside(&painter, box.adjusted(-4, -4, 4, 4), PDFStylePainter::Radii(2), PDFThemeColors::BorderFocus, 2.0);
    }
}

void PDFAppCropOverlay::mousePressEvent(QMouseEvent* event)
{
    const Part part = event->button() == Qt::LeftButton ? getPartAt(event->position()) : Part::None;
    if (part == Part::None || !m_stage)
    {
        event->ignore();
        return;
    }

    setFocus(Qt::MouseFocusReason);
    m_dragPart = part;
    m_dragStart = m_stage->getPageToStageTransform().inverted().map(event->position());
    m_dragStartRect = m_rect;
    event->accept();
}

void PDFAppCropOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragPart == Part::None || !m_stage)
    {
        updateCursor(getPartAt(event->position()));
        event->ignore();
        return;
    }

    const QPointF position = m_stage->getPageToStageTransform().inverted().map(event->position());
    const QRectF rect = getDraggedRect(m_dragPart, m_dragStartRect, position - m_dragStart);
    event->accept();
    if (rect != m_rect)
    {
        m_rect = rect;
        update();

        // The last statement: a slot may delete the overlay
        Q_EMIT rectChanged(m_rect);
    }
}

void PDFAppCropOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragPart == Part::None || event->button() != Qt::LeftButton)
    {
        event->ignore();
        return;
    }

    const bool isChanged = m_rect != m_dragStartRect;
    m_dragPart = Part::None;
    event->accept();
    if (isChanged)
    {
        // One undo step of the tool; the last statement
        Q_EMIT rectChosen(m_rect);
    }
}

void PDFAppCropOverlay::keyPressEvent(QKeyEvent* event)
{
    if (m_isInactive)
    {
        BaseClass::keyPressEvent(event);
        return;
    }

    // The arrows in the picture of the page (not mirrored); Shift changes the size
    const qreal step = event->modifiers().testFlag(Qt::ControlModifier) ? 10.0 : 1.0;
    QPointF delta;
    switch (event->key())
    {
        case Qt::Key_Left:
            delta = QPointF(-step, 0.0);
            break;
        case Qt::Key_Right:
            delta = QPointF(step, 0.0);
            break;
        case Qt::Key_Up:
            delta = QPointF(0.0, -step);
            break;
        case Qt::Key_Down:
            delta = QPointF(0.0, step);
            break;
        default:
            BaseClass::keyPressEvent(event);
            return;
    }

    event->accept();
    const QRectF rect = getDraggedRect(event->modifiers().testFlag(Qt::ShiftModifier) ? Part::BottomRight : Part::Inside, m_rect, delta);
    if (rect != m_rect)
    {
        m_rect = rect;
        update();

        // One undo step of the tool; the last statement
        Q_EMIT rectChosen(m_rect);
    }
}

void PDFAppCropOverlay::focusInEvent(QFocusEvent* event)
{
    BaseClass::focusInEvent(event);
    update();
}

void PDFAppCropOverlay::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);
    update();
}

}   // namespace pdf

#include "moc_pdfapptoolcropoverlay.cpp"
