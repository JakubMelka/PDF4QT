//    Copyright (C) 2018-2019 Jakub Melka
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

#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QPainter>
#include <QGridLayout>
#include <QKeyEvent>
#include <QApplication>

namespace pdf
{

PDFWidget::PDFWidget(QWidget* parent) :
    QWidget(parent),
    m_drawWidget(nullptr),
    m_horizontalScrollBar(nullptr),
    m_verticalScrollBar(nullptr),
    m_proxy(nullptr)
{
    m_drawWidget = new PDFDrawWidget(this, this);
    m_horizontalScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);

    QGridLayout* layout = new QGridLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_drawWidget, 0, 0);
    layout->addWidget(m_horizontalScrollBar, 1, 0);
    layout->addWidget(m_verticalScrollBar, 0, 1);
    layout->setMargin(0);

    setLayout(layout);
    setFocusProxy(m_drawWidget);

    m_proxy = new PDFDrawWidgetProxy(this);
    m_proxy->init(this);
    connect(m_proxy, &PDFDrawWidgetProxy::renderingError, this, &PDFWidget::onRenderingError);
}

PDFWidget::~PDFWidget()
{

}

void PDFWidget::setDocument(const PDFDocument* document)
{
    m_proxy->setDocument(document);
    m_pageRenderingErrors.clear();
}

int PDFWidget::getPageRenderingErrorCount() const
{
    int count = 0;
    for (const auto& item : m_pageRenderingErrors)
    {
        count += item.second.size();
    }
    return count;
}

void PDFWidget::onRenderingError(PDFInteger pageIndex, const QList<PDFRenderError>& errors)
{
    // Empty list of error should not be reported!
    Q_ASSERT(!errors.empty());
    m_pageRenderingErrors[pageIndex] = errors;
    emit pageRenderingErrorsChanged(pageIndex, errors.size());
}

PDFDrawWidget::PDFDrawWidget(PDFWidget* widget, QWidget* parent) :
    QWidget(parent),
    m_widget(widget),
    m_mouseOperation(MouseOperation::None)
{
    setFocusPolicy(Qt::StrongFocus);
}

PDFDrawWidget::~PDFDrawWidget()
{

}

std::vector<PDFInteger> PDFDrawWidget::getCurrentPages() const
{
    return m_widget->getDrawWidgetProxy()->getPagesIntersectingRect(this->rect());
}

QSize PDFDrawWidget::minimumSizeHint() const
{
    return QSize(200, 200);
}

void PDFDrawWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    m_widget->getDrawWidgetProxy()->draw(&painter, this->rect());
}

void PDFDrawWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    m_widget->getDrawWidgetProxy()->update();
}

void PDFDrawWidget::keyPressEvent(QKeyEvent* event)
{
    QScrollBar* verticalScrollbar = m_widget->getVerticalScrollbar();

    // Vertical navigation
    if (verticalScrollbar->isVisible())
    {
        constexpr std::pair<QKeySequence::StandardKey, PDFDrawWidgetProxy::Operation> keyToOperations[] =
        {
            { QKeySequence::MoveToStartOfDocument, PDFDrawWidgetProxy::NavigateDocumentStart },
            { QKeySequence::MoveToEndOfDocument, PDFDrawWidgetProxy::NavigateDocumentEnd },
            { QKeySequence::MoveToNextPage, PDFDrawWidgetProxy::NavigateNextPage },
            { QKeySequence::MoveToPreviousPage, PDFDrawWidgetProxy::NavigatePreviousPage },
            { QKeySequence::MoveToNextLine, PDFDrawWidgetProxy::NavigateNextStep },
            { QKeySequence::MoveToPreviousLine, PDFDrawWidgetProxy::NavigatePreviousStep }
        };

        for (const std::pair<QKeySequence::StandardKey, PDFDrawWidgetProxy::Operation>& keyToOperation : keyToOperations)
        {
            if (event->matches(keyToOperation.first))
            {
                m_widget->getDrawWidgetProxy()->performOperation(keyToOperation.second);
            }
        }
    }

    event->accept();
}

void PDFDrawWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_mouseOperation = MouseOperation::Translate;
        m_lastMousePosition = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }

    event->accept();
}

void PDFDrawWidget::mouseReleaseEvent(QMouseEvent* event)
{
    performMouseOperation(event->pos());

    switch (m_mouseOperation)
    {
        case MouseOperation::None:
            break;

        case MouseOperation::Translate:
        {
            m_mouseOperation = MouseOperation::None;
            unsetCursor();
            break;
        }

        default:
            Q_ASSERT(false);
    }

    event->accept();
}

void PDFDrawWidget::mouseMoveEvent(QMouseEvent* event)
{
    performMouseOperation(event->pos());
    event->accept();
}

void PDFDrawWidget::wheelEvent(QWheelEvent* event)
{
    Qt::KeyboardModifiers keyboardModifiers = QApplication::keyboardModifiers();

    PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    if (keyboardModifiers.testFlag(Qt::ControlModifier))
    {
        // Zoom in/Zoom out
        const int angleDeltaY = event->angleDelta().y();
        const PDFReal zoom = m_widget->getDrawWidgetProxy()->getZoom();
        const PDFReal zoomStep = std::pow(PDFDrawWidgetProxy::ZOOM_STEP, static_cast<PDFReal>(angleDeltaY) / static_cast<PDFReal>(QWheelEvent::DefaultDeltasPerStep));
        const PDFReal newZoom = zoom * zoomStep;
        proxy->zoom(newZoom);
    }
    else
    {
        // Move Up/Down. Angle is negative, if wheel is scrolled down. First we try to scroll by pixel delta.
        // Otherwise we compute scroll using angle.
        QPoint scrollByPixels = event->pixelDelta();
        if (scrollByPixels.isNull())
        {
            const QPoint angleDelta = event->angleDelta();
            const bool shiftModifier = keyboardModifiers.testFlag(Qt::ShiftModifier);
            int stepVertical = 0;
            int stepHorizontal = shiftModifier ? m_widget->getHorizontalScrollbar()->pageStep() : m_widget->getHorizontalScrollbar()->singleStep();

            if (proxy->isBlockMode())
            {
                // In block mode, we must calculate pixel offsets differently - scrollbars corresponds to indices of blocks,
                // not to the pixels.
                QRect boundingBox = proxy->getPagesIntersectingRectBoundingBox(this->rect());

                if (boundingBox.isEmpty())
                {
                    // This occurs, when we have not opened a document
                    boundingBox = this->rect();
                }

                stepVertical = shiftModifier ? boundingBox.height() : boundingBox.height() / 10;
            }
            else
            {
                stepVertical = shiftModifier ? m_widget->getVerticalScrollbar()->pageStep() : m_widget->getVerticalScrollbar()->singleStep();
            }

            const int scrollVertical = stepVertical * static_cast<PDFReal>(angleDelta.y()) / static_cast<PDFReal>(QWheelEvent::DefaultDeltasPerStep);
            const int scrollHorizontal = stepHorizontal * static_cast<PDFReal>(angleDelta.x()) / static_cast<PDFReal>(QWheelEvent::DefaultDeltasPerStep);

            scrollByPixels = QPoint(scrollHorizontal, scrollVertical);
        }

        QPoint offset = proxy->scrollByPixels(scrollByPixels);

        if (offset.y() == 0 && scrollByPixels.y() != 0 && proxy->isBlockMode())
        {
            // We must move to another block (we are in block mode)
            bool up = scrollByPixels.y() > 0;

            m_widget->getVerticalScrollbar()->setValue(m_widget->getVerticalScrollbar()->value() + (up ? -1 : 1));
            proxy->scrollByPixels(QPoint(0, up ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max()));
        }
    }

    event->accept();
}

void PDFDrawWidget::performMouseOperation(QPoint currentMousePosition)
{
    switch (m_mouseOperation)
    {
        case MouseOperation::None:
            // No operation performed
            break;

        case MouseOperation::Translate:
        {
            QPoint difference = currentMousePosition - m_lastMousePosition;
            m_widget->getDrawWidgetProxy()->scrollByPixels(difference);
            m_lastMousePosition = currentMousePosition;
            break;
        }

        default:
            Q_ASSERT(false);
    }
}

}   // namespace pdf
