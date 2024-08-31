//    Copyright (C) 2018-2023 Jakub Melka
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

#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfwidgettool.h"
#include "pdfannotation.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfblpainter.h"
#include "pdfpagecontentelements.h"

#include <QPainter>
#include <QGridLayout>
#include <QKeyEvent>
#include <QApplication>
#include <QPixmapCache>
#include <QColorSpace>

#include "pdfdbgheap.h"

namespace pdf
{

PDFWidget::PDFWidget(const PDFCMSManager* cmsManager, RendererEngine engine, QWidget* parent) :
    QWidget(parent),
    m_cmsManager(cmsManager),
    m_toolManager(nullptr),
    m_annotationManager(nullptr),
    m_formManager(nullptr),
    m_drawWidget(nullptr),
    m_horizontalScrollBar(nullptr),
    m_verticalScrollBar(nullptr),
    m_proxy(nullptr),
    m_rendererEngine(engine)
{
    m_drawWidget = new PDFDrawWidget(this, this);
    m_horizontalScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);

    QGridLayout* layout = new QGridLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_drawWidget->getWidget(), 0, 0);
    layout->addWidget(m_horizontalScrollBar, 1, 0);
    layout->addWidget(m_verticalScrollBar, 0, 1);
    layout->setContentsMargins(QMargins());

    setLayout(layout);
    setFocusProxy(m_drawWidget->getWidget());

    m_proxy = new PDFDrawWidgetProxy(this);
    m_proxy->init(this);
    m_proxy->updateRenderer(m_rendererEngine);
    connect(m_proxy, &PDFDrawWidgetProxy::renderingError, this, &PDFWidget::onRenderingError);
    connect(m_proxy, &PDFDrawWidgetProxy::repaintNeeded, m_drawWidget->getWidget(), QOverload<>::of(&QWidget::update));
    connect(m_proxy, &PDFDrawWidgetProxy::pageImageChanged, this, &PDFWidget::onPageImageChanged);
}

PDFWidget::~PDFWidget()
{

}

bool PDFWidget::focusNextPrevChild(bool next)
{
    if (m_formManager && m_formManager->focusNextPrevFormField(next))
    {
        return true;
    }

    return QWidget::focusNextPrevChild(next);
}

void PDFWidget::setDocument(const PDFModifiedDocument& document)
{
    m_proxy->setDocument(document);
    m_pageRenderingErrors.clear();
    m_drawWidget->getWidget()->update();
}

void PDFWidget::updateRenderer(RendererEngine engine)
{
    m_rendererEngine = engine;
    m_proxy->updateRenderer(m_rendererEngine);
}

void PDFWidget::updateCacheLimits(int compiledPageCacheLimit, int thumbnailsCacheLimit, int fontCacheLimit, int instancedFontCacheLimit)
{
    m_proxy->getCompiler()->setCacheLimit(compiledPageCacheLimit);
    QPixmapCache::setCacheLimit(qMax(thumbnailsCacheLimit, 16384));
    m_proxy->getFontCache()->setCacheLimits(fontCacheLimit, instancedFontCacheLimit);
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
    Q_EMIT pageRenderingErrorsChanged(pageIndex, errors.size());
}

void PDFWidget::onPageImageChanged(bool all, const std::vector<PDFInteger>& pages)
{
    if (all)
    {
        m_drawWidget->getWidget()->update();
    }
    else
    {
        std::vector<PDFInteger> currentPages = m_drawWidget->getCurrentPages();

        Q_ASSERT(std::is_sorted(pages.cbegin(), pages.cend()));
        for (PDFInteger pageIndex : currentPages)
        {
            if (std::binary_search(pages.cbegin(), pages.cend(), pageIndex))
            {
                m_drawWidget->getWidget()->update();
                return;
            }
        }
    }
}

void PDFWidget::onSceneActiveStateChanged(bool)
{
    Q_EMIT sceneActivityChanged();
}

void PDFWidget::removeInputInterface(IDrawWidgetInputInterface* inputInterface)
{
    auto it = std::find(m_inputInterfaces.begin(), m_inputInterfaces.end(), inputInterface);
    if (it != m_inputInterfaces.end())
    {
        m_inputInterfaces.erase(it);
    }

    PDFPageContentScene* scene = dynamic_cast<PDFPageContentScene*>(inputInterface);
    if (scene)
    {
        auto itScene = std::find(m_scenes.begin(), m_scenes.end(), inputInterface);
        if (itScene != m_scenes.end())
        {
            m_scenes.erase(itScene);
            disconnect(scene, &PDFPageContentScene::sceneActiveStateChanged, this, &PDFWidget::onSceneActiveStateChanged);
        }
    }
}

void PDFWidget::addInputInterface(IDrawWidgetInputInterface* inputInterface)
{
    if (inputInterface)
    {
        m_inputInterfaces.push_back(inputInterface);
        std::sort(m_inputInterfaces.begin(), m_inputInterfaces.end(), IDrawWidgetInputInterface::Comparator());

        PDFPageContentScene* scene = dynamic_cast<PDFPageContentScene*>(inputInterface);
        if (scene)
        {
            m_scenes.push_back(scene);
            connect(scene, &PDFPageContentScene::sceneActiveStateChanged, this, &PDFWidget::onSceneActiveStateChanged);
        }
    }
}

bool PDFWidget::isAnySceneActive(PDFPageContentScene* sceneToSkip) const
{
    for (PDFPageContentScene* scene : m_scenes)
    {
        if (scene->isActive() && scene != sceneToSkip)
        {
            return true;
        }
    }

    return false;
}

PDFWidgetFormManager* PDFWidget::getFormManager() const
{
    return m_formManager;
}

void PDFWidget::setFormManager(PDFWidgetFormManager* formManager)
{
    removeInputInterface(m_formManager);
    m_formManager = formManager;
    addInputInterface(m_formManager);
}

void PDFWidget::setToolManager(PDFToolManager* toolManager)
{
    removeInputInterface(m_toolManager);
    m_toolManager = toolManager;
    addInputInterface(m_toolManager);
}

void PDFWidget::setAnnotationManager(PDFWidgetAnnotationManager* annotationManager)
{
    removeInputInterface(m_annotationManager);
    m_annotationManager = annotationManager;
    addInputInterface(m_annotationManager);
}

PDFDrawWidget::PDFDrawWidget(PDFWidget* widget, QWidget* parent) :
    BaseClass(parent),
    m_widget(widget),
    m_mouseOperation(MouseOperation::None)
{
    this->setFocusPolicy(Qt::StrongFocus);
    this->setMouseTracking(true);

    QObject::connect(&m_autoScrollTimer, &QTimer::timeout, this, &PDFDrawWidget::onAutoScrollTimeout);
}

std::vector<PDFInteger> PDFDrawWidget::getCurrentPages() const
{
    return this->m_widget->getDrawWidgetProxy()->getPagesIntersectingRect(this->rect());
}

QSize PDFDrawWidget::minimumSizeHint() const
{
    return QSize(200, 200);
}

bool PDFDrawWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        return processEvent<QKeyEvent, &IDrawWidgetInputInterface::shortcutOverrideEvent>(static_cast<QKeyEvent*>(event));
    }

    return BaseClass::event(event);
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

        case MouseOperation::AutoScroll:
        {
            m_lastMousePosition = currentMousePosition;
            onAutoScrollTimeout();
            break;
        }

        default:
            Q_ASSERT(false);
    }
}

template<typename Event, void (IDrawWidgetInputInterface::* Function)(QWidget*, Event*)>
bool PDFDrawWidget::processEvent(Event* event)
{
    QString tooltip;
    for (IDrawWidgetInputInterface* inputInterface : m_widget->getInputInterfaces())
    {
        (inputInterface->*Function)(this, event);

        // Update tooltip
        if (tooltip.isEmpty())
        {
            tooltip = inputInterface->getTooltip();
        }

        // If event is accepted, then update cursor/tooltip and return
        if (event->isAccepted())
        {
            this->setToolTip(tooltip);
            this->updateCursor();
            return true;
        }
    }
    this->setToolTip(tooltip);

    return false;
}

void PDFDrawWidget::keyPressEvent(QKeyEvent* event)
{
    event->ignore();

    if (processEvent<QKeyEvent, &IDrawWidgetInputInterface::keyPressEvent>(event))
    {
        return;
    }

    // Vertical navigation
    QScrollBar* verticalScrollbar = m_widget->getVerticalScrollbar();
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
                event->accept();
            }
        }
    }

    updateCursor();
}

void PDFDrawWidget::keyReleaseEvent(QKeyEvent* event)
{
    event->ignore();

    if (processEvent<QKeyEvent, &IDrawWidgetInputInterface::keyReleaseEvent>(event))
    {
        return;
    }

    event->accept();
}

void PDFDrawWidget::mousePressEvent(QMouseEvent* event)
{
    event->ignore();

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mousePressEvent>(event))
    {
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        m_mouseOperation = MouseOperation::Translate;
        m_lastMousePosition = event->pos();
    }

    if (event->button() == Qt::MiddleButton)
    {
        if (m_mouseOperation == MouseOperation::AutoScroll)
        {
            m_mouseOperation = MouseOperation::None;
            m_autoScrollTimer.stop();
            m_autoScrollLastElapsedTimer.restart();
            m_autoScrollOffset = QPointF(0.0, 0.0);
        }
        else
        {
            m_mouseOperation = MouseOperation::AutoScroll;
            m_autoScrollMousePosition = event->pos();
            m_autoScrollLastElapsedTimer.restart();
            m_autoScrollOffset = QPointF(0.0, 0.0);
            m_lastMousePosition = event->pos();
            m_autoScrollTimer.setInterval(10);
            m_autoScrollTimer.start();
        }
    }

    updateCursor();
    event->accept();
}

void PDFDrawWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->ignore();

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mouseDoubleClickEvent>(event))
    {
        return;
    }
}

void PDFDrawWidget::mouseReleaseEvent(QMouseEvent* event)
{
    event->ignore();

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mouseReleaseEvent>(event))
    {
        return;
    }

    performMouseOperation(event->pos());

    switch (m_mouseOperation)
    {
        case MouseOperation::None:
            break;

        case MouseOperation::Translate:
        {
            if (event->button() != Qt::MiddleButton)
            {
                m_mouseOperation = MouseOperation::None;
            }
            break;
        }

        case MouseOperation::AutoScroll:
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    updateCursor();
    event->accept();
}

void PDFDrawWidget::mouseMoveEvent(QMouseEvent* event)
{
    event->ignore();

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mouseMoveEvent>(event))
    {
        return;
    }

    performMouseOperation(event->pos());
    updateCursor();
    event->accept();
}

void PDFDrawWidget::updateCursor()
{
    std::optional<QCursor> cursor;

    for (IDrawWidgetInputInterface* inputInterface : m_widget->getInputInterfaces())
    {
        cursor = inputInterface->getCursor();

        if (cursor)
        {
            // We have found cursor
            break;
        }
    }

    if (!cursor)
    {
        switch (m_mouseOperation)
        {
            case MouseOperation::None:
                cursor = QCursor(Qt::OpenHandCursor);
                break;

            case MouseOperation::Translate:
                cursor = QCursor(Qt::ClosedHandCursor);
                break;

            case MouseOperation::AutoScroll:
                cursor = QCursor(Qt::SizeAllCursor);
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }

    if (cursor)
    {
        this->setCursor(*cursor);
    }
    else
    {
        this->unsetCursor();
    }
}

void PDFDrawWidget::onAutoScrollTimeout()
{
    if (m_mouseOperation != MouseOperation::AutoScroll)
    {
        return;
    }

    QPointF offset = m_autoScrollMousePosition - m_lastMousePosition;
    QPointF scrollOffset = m_autoScrollOffset;

    qreal secondsElapsed = qreal(m_autoScrollLastElapsedTimer.nsecsElapsed()) * 0.000000001;
    m_autoScrollLastElapsedTimer.restart();
    scrollOffset += offset * secondsElapsed;

    int scrollX = qFloor(scrollOffset.x());
    int scrollY = qFloor(scrollOffset.y());

    scrollOffset -= QPointF(scrollX, scrollY);
    m_autoScrollOffset = scrollOffset;

    PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    proxy->scrollByPixels(QPoint(scrollX, scrollY));
}

void PDFDrawWidget::wheelEvent(QWheelEvent* event)
{
    event->ignore();

    if (processEvent<QWheelEvent, &IDrawWidgetInputInterface::wheelEvent>(event))
    {
        return;
    }

    Qt::KeyboardModifiers keyboardModifiers = QApplication::keyboardModifiers();

    PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    if (keyboardModifiers.testFlag(Qt::ControlModifier))
    {
        // Zoom in/Zoom out
        const int angleDeltaY = event->angleDelta().y();
        const PDFReal zoom = m_widget->getDrawWidgetProxy()->getZoom();
        const PDFReal zoomStep = std::pow(PDFDrawWidgetProxy::ZOOM_STEP, static_cast<PDFReal>(angleDeltaY) / static_cast<PDFReal>(QWheelEvent::DefaultDeltasPerStep));
        const PDFReal newZoom = zoom * zoomStep;
        proxy->zoom(newZoom, event->position());
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

            QScrollBar* verticalScrollbar = m_widget->getVerticalScrollbar();
            const int newValue = verticalScrollbar->value() + (up ? -1 : 1);

            if (newValue >= verticalScrollbar->minimum() && newValue <= verticalScrollbar->maximum())
            {
                verticalScrollbar->setValue(newValue);
                proxy->scrollByPixels(QPoint(0, up ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max()));
            }
        }
    }

    event->accept();
}

void PDFDrawWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    RendererEngine rendererEngine = getPDFWidget()->getDrawWidgetProxy()->getRendererEngine();
    switch (rendererEngine)
    {
        case RendererEngine::Blend2D_MultiThread:
        case RendererEngine::Blend2D_SingleThread:
        {
            QRect rect = this->rect();

            qreal devicePixelRatio = devicePixelRatioF();
            m_blend2DframeBuffer.setDevicePixelRatio(devicePixelRatio);

            qreal dpmX = logicalDpiX() / 0.0254;
            qreal dpmY = logicalDpiY() / 0.0254;
            m_blend2DframeBuffer.setDotsPerMeterX(qCeil(dpmX));
            m_blend2DframeBuffer.setDotsPerMeterY(qCeil(dpmY));

            QSize requiredSize = rect.size() * devicePixelRatio;
            if (m_blend2DframeBuffer.size() != requiredSize)
            {
                m_blend2DframeBuffer = QImage(requiredSize, QImage::Format_ARGB32_Premultiplied);
            }

            const bool multithreaded = rendererEngine == RendererEngine::Blend2D_MultiThread;
            PDFBLPaintDevice blPaintDevice(m_blend2DframeBuffer, multithreaded);
            QPainter blPainter;

            if (blPainter.begin(&blPaintDevice))
            {
                getPDFWidget()->getDrawWidgetProxy()->draw(&blPainter, rect);
                blPainter.end();
            }

            QPainter painter(this);
            painter.drawImage(QPoint(0, 0), m_blend2DframeBuffer);
            break;
        }

        case RendererEngine::QPainter:
        {
            QPainter painter(this);
            getPDFWidget()->getDrawWidgetProxy()->draw(&painter, this->rect());
            m_blend2DframeBuffer = QImage();
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFDrawWidget::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);

    getPDFWidget()->getDrawWidgetProxy()->update();
}

}   // namespace pdf
