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

#include "pdfappaudiobookoverlay.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfappworkspace.h"
#include "pdfcomponentpanels.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"

#include "pdfpainterutils.h"
#include "pdfrenderer.h"
#include "pdfthememanager.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace pdf
{

PDFAppAudioBookOverlay::PDFAppAudioBookOverlay(QWidget* picture) :
    BaseClass(picture)
{
    setObjectName(QStringLiteral("audioBookOverlay"));
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
    if (picture)
    {
        // The overlay covers the whole picture, also after its resize
        PDFDeferredDeletion::installEventFilter(picture, this);
        setGeometry(picture->rect());
    }
}

PDFAppAudioBookOverlay::~PDFAppAudioBookOverlay() = default;

void PDFAppAudioBookOverlay::setPageGeometry(const QRectF& mediaBox, PageRotation rotation)
{
    m_mediaBox = mediaBox;
    m_rotation = rotation;
    update();
}

void PDFAppAudioBookOverlay::setItems(std::vector<Item> items)
{
    m_items = std::move(items);
    update();
}

void PDFAppAudioBookOverlay::setRectangleMode(bool isRectangleMode)
{
    m_isRectangleMode = isRectangleMode;
    m_isDragging = false;
    setCursor(isRectangleMode ? Qt::CrossCursor : Qt::ArrowCursor);
    if (isRectangleMode)
    {
        setFocus(Qt::OtherFocusReason);
    }
    update();
}

bool PDFAppAudioBookOverlay::cancelRectangleMode()
{
    if (!m_isRectangleMode)
    {
        return false;
    }

    setRectangleMode(false);
    Q_EMIT rectangleCanceled();
    return true;
}

void PDFAppAudioBookOverlay::cancelToolDrag()
{
    // The rectangle disappears, the mode stays (the next Esc ends it); the release selects nothing
    if (!isToolDragActive())
    {
        return;
    }
    m_isDragging = false;
    update();
}

QRect PDFAppAudioBookOverlay::getPaperRect() const
{
    const PDFAppToolPagePicture* picture = qobject_cast<const PDFAppToolPagePicture*>(parentWidget());
    return picture ? picture->getPaperRect() : QRect();
}

QTransform PDFAppAudioBookOverlay::getPageToWidgetTransform() const
{
    const QRect paperRect = getPaperRect();
    if (paperRect.isEmpty() || !m_mediaBox.isValid())
    {
        return QTransform();
    }
    return PDFRenderer::createMediaBoxToDevicePointMatrix(m_mediaBox, QRectF(paperRect), m_rotation);
}

qint64 PDFAppAudioBookOverlay::getItemAt(const QPointF& point) const
{
    const QTransform matrix = getPageToWidgetTransform();
    if (getPaperRect().isEmpty())
    {
        return -1;
    }

    qint64 result = -1;
    qreal smallestArea = 0.0;
    for (const Item& item : m_items)
    {
        const QRectF rect = matrix.mapRect(item.rect);
        if (rect.contains(point))
        {
            const qreal area = rect.width() * rect.height();
            if (result < 0 || area < smallestArea)
            {
                result = qint64(item.index);
                smallestArea = area;
            }
        }
    }
    return result;
}

QColor PDFAppAudioBookOverlay::getStateColor(State state)
{
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    switch (state)
    {
        case State::Read:
            return manager->getColor(PDFThemeColors::AccentGreenIcon);
        case State::Selected:
            return manager->getColor(PDFThemeColors::AccentBlueIcon);
        case State::Modified:
            return manager->getColor(PDFThemeColors::AccentAmberIcon);
        case State::Skipped:
            return manager->getColor(PDFThemeColors::AccentRedIcon);
    }
    return manager->getColor(PDFThemeColors::AccentGreenIcon);
}

void PDFAppAudioBookOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const QTransform matrix = getPageToWidgetTransform();
    if (getPaperRect().isEmpty())
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 341:9564 ...: the rectangle of an item, radius 2, the border 1.5 and the fill of its color
    for (const Item& item : m_items)
    {
        const QRectF rect = matrix.mapRect(item.rect).adjusted(-2.0, -2.0, 2.0, 2.0);
        if (rect.isEmpty())
        {
            continue;
        }

        QColor color = getStateColor(item.state);
        QColor fill = color;
        fill.setAlphaF(0.16f);
        painter.setPen(QPen(color, item.state == State::Selected ? 2.0 : 1.25));
        painter.setBrush(fill);
        painter.drawRoundedRect(rect, 2.0, 2.0);
    }

    // 360:13257: the dashed rectangle of the selection
    if (m_isRectangleMode && m_isDragging)
    {
        PDFPainterStateGuard guard(&painter);
        QPen pen(getStateColor(State::Selected), 1.5, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(m_dragStart, m_dragEnd).normalized());
    }
}

bool PDFAppAudioBookOverlay::event(QEvent* event)
{
    // Esc of the mode of the rectangle goes to the shortcut of the tool: the shell cancels a rectangle
    // being drawn first (cancelToolDrag), then asks the GUI (PDFAppToolGUIAudioBook::handleEscape ends
    // the mode), the next Esc leaves the tool
    return BaseClass::event(event);
}

void PDFAppAudioBookOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        BaseClass::mousePressEvent(event);
        return;
    }

    event->accept();
    if (m_isRectangleMode)
    {
        m_isDragging = true;
        m_dragStart = event->position();
        m_dragEnd = m_dragStart;
        update();
    }
}

void PDFAppAudioBookOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isRectangleMode && m_isDragging)
    {
        m_dragEnd = event->position();
        update();
        event->accept();
        return;
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFAppAudioBookOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        BaseClass::mouseReleaseEvent(event);
        return;
    }

    event->accept();
    if (m_isRectangleMode)
    {
        if (!m_isDragging)
        {
            return;
        }

        // The rectangle in the coordinates of the page; the mode ends, the signal is last
        m_dragEnd = event->position();
        const QRectF widgetRectangle = QRectF(m_dragStart, m_dragEnd).normalized();
        bool isInvertible = false;
        const QTransform inverted = getPageToWidgetTransform().inverted(&isInvertible);
        setRectangleMode(false);
        if (isInvertible && !widgetRectangle.isEmpty())
        {
            Q_EMIT rectangleSelected(inverted.mapRect(widgetRectangle));
        }
        else
        {
            Q_EMIT rectangleCanceled();
        }
        return;
    }

    const bool isAdding = event->modifiers().testFlag(Qt::ControlModifier);
    Q_EMIT itemClicked(getItemAt(event->position()), isAdding);
}

void PDFAppAudioBookOverlay::keyPressEvent(QKeyEvent* event)
{
    // Esc of the mode: the shortcut of the tool (see event), cancelRectangleMode
    BaseClass::keyPressEvent(event);
}

bool PDFAppAudioBookOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
    {
        setGeometry(parentWidget()->rect());
    }
    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf

#include "moc_pdfappaudiobookoverlay.cpp"
