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
#include <QPointer>
#include <QGridLayout>
#include <QKeyEvent>
#include <QApplication>
#include <QPixmapCache>
#include <QColorSpace>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

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

bool PDFWidget::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::ScreenChangeInternal:
        case QEvent::DevicePixelRatioChange:
        {
            // Conversion between the device space (millimeters) and the pixels depends
            // on the screen, on which the widget is displayed. We must recalculate the
            // draw space, otherwise the pages would keep the size (and the zoom modes
            // the zoom) calculated for the previous screen.
            if (m_proxy)
            {
                // A receiver of drawSpaceChanged of the proxy can delete the widget
                QPointer<PDFWidget> self(this);
                m_proxy->update();
                if (!self)
                {
                    return true;
                }
            }
            break;
        }

        default:
            break;
    }

    return BaseClass::event(event);
}

bool PDFWidget::focusNextPrevChild(bool next)
{
    // The move of the focus commits the edited form field: the application may
    // delete this widget from the change of the document
    QPointer<PDFWidget> self(this);
    if (m_formManager && m_formManager->focusNextPrevFormField(next))
    {
        return true;
    }
    if (!self)
    {
        return true;
    }

    return QWidget::focusNextPrevChild(next);
}

void PDFWidget::setDocument(const PDFModifiedDocument& document, std::vector<PDFSignatureVerificationResult> signatureVerificationResult)
{
    // A receiver of drawSpaceChanged of the proxy can delete the widget
    QPointer<PDFWidget> self(this);
    m_proxy->setDocument(document, std::move(signatureVerificationResult));
    if (!self)
    {
        return;
    }
    m_pageRenderingErrors.clear();
    m_drawWidget->getWidget()->update();
}

void PDFWidget::updateRenderer(RendererEngine engine)
{
    m_rendererEngine = engine;
    m_proxy->updateRenderer(m_rendererEngine);
}

void PDFWidget::updateCacheLimits(qsizetype compiledPageCacheLimit, int thumbnailsCacheLimit, int fontCacheLimit, int instancedFontCacheLimit)
{
    m_proxy->getCompiler()->setCacheLimit(compiledPageCacheLimit);
    QPixmapCache::setCacheLimit(qMax(thumbnailsCacheLimit, 16384));
    m_proxy->getFontCache()->setCacheLimits(fontCacheLimit, instancedFontCacheLimit);
}

void PDFWidget::setSmoothWheelScrolling(bool enabled)
{
    if (PDFDrawWidget* drawWidget = dynamic_cast<PDFDrawWidget*>(m_drawWidget))
    {
        drawWidget->setSmoothWheelScrolling(enabled);
    }
}

void PDFWidget::setWheelScrollSpeed(int horizontalPercent, int verticalPercent)
{
    if (PDFDrawWidget* drawWidget = dynamic_cast<PDFDrawWidget*>(m_drawWidget))
    {
        drawWidget->setWheelScrollSpeed(horizontalPercent, verticalPercent);
    }
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

    // The cursor follows a tool switched by the keyboard or a menu without a move of the mouse
    if (m_toolManager)
    {
        connect(m_toolManager, &PDFToolManager::activeToolChanged, this, &PDFWidget::onActiveToolChanged, Qt::UniqueConnection);
    }
}

void PDFWidget::onActiveToolChanged()
{
    // From the event loop: the tool is switched inside of a dispatch of the input,
    // or while the owner of the managers is changing them
    if (PDFDrawWidget* drawWidget = static_cast<PDFDrawWidget*>(m_drawWidget))
    {
        QMetaObject::invokeMethod(drawWidget, &PDFDrawWidget::updateCursor, Qt::QueuedConnection);
    }
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
    this->setAcceptDrops(true);

    // No tool yet: a drag pans the page (the cursor before the first move of the mouse)
    this->setCursor(QCursor(Qt::OpenHandCursor));

    QObject::connect(&m_autoScrollTimer, &QTimer::timeout, this, &PDFDrawWidget::onAutoScrollTimeout);
    QObject::connect(&m_wheelScrollTimer, &QTimer::timeout, this, &PDFDrawWidget::onWheelScrollTimeout);
}

void PDFDrawWidget::setSmoothWheelScrolling(bool enabled)
{
    m_smoothWheelScrolling = enabled;
    if (!m_smoothWheelScrolling)
    {
        m_wheelScrollTimer.stop();
        m_wheelScrollPendingOffset = QPointF(0.0, 0.0);
    }
}

void PDFDrawWidget::setWheelScrollSpeed(int horizontalPercent, int verticalPercent)
{
    m_horizontalWheelScrollSpeed = qMax(PDFReal(0.01), PDFReal(horizontalPercent) / PDFReal(100.0));
    m_verticalWheelScrollSpeed = qMax(PDFReal(0.01), PDFReal(verticalPercent) / PDFReal(100.0));
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
        // Esc ends the own operation of the widget (the automatic scrolling) before
        // a shortcut of the window or a tool gets it
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (isMouseOperationActive() && keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier)
        {
            keyEvent->accept();
            return true;
        }

        return processEvent<QKeyEvent, &IDrawWidgetInputInterface::shortcutOverrideEvent>(keyEvent);
    }

    return BaseClass::event(event);
}

void PDFDrawWidget::focusOutEvent(QFocusEvent* event)
{
    // A popup, a dialog or another window takes the input - the release of the
    // button (the end of the automatic scrolling) would never come
    endMouseOperation();
    BaseClass::focusOutEvent(event);
}

void PDFDrawWidget::hideEvent(QHideEvent* event)
{
    endMouseOperation();
    BaseClass::hideEvent(event);
}

void PDFDrawWidget::endMouseOperation()
{
    if (m_mouseOperation == MouseOperation::None)
    {
        return;
    }

    m_mouseOperation = MouseOperation::None;
    m_autoScrollTimer.stop();
    m_autoScrollOffset = QPointF(0.0, 0.0);
    updateCursor();
}

void PDFDrawWidget::beginPageDrag(QPoint pressPosition)
{
    if (m_mouseOperation != MouseOperation::None)
    {
        return;
    }

    // The move, which has started the drag, scrolls the page from the press on
    // (mouseMoveEvent performs the operation, the content follows the pointer)
    m_mouseOperation = MouseOperation::Translate;
    m_lastMousePosition = pressPosition;
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

            // A receiver of drawSpaceChanged of the proxy can delete the widget
            QPointer<PDFDrawWidget> self(this);
            m_widget->getDrawWidgetProxy()->scrollByPixels(difference);
            if (!self)
            {
                return;
            }
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
    QPointer<PDFDrawWidget> self(this);
    for (IDrawWidgetInputInterface* inputInterface : m_widget->getInputInterfaces())
    {
        (inputInterface->*Function)(this, event);

        // A receiver of the input (a commit of the application) may delete the widget
        // with its tab synchronously - the event is consumed then
        if (!self)
        {
            return true;
        }

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

    // Esc ends the own operation of the widget first (the tools get the next Esc)
    if (isMouseOperationActive() && event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
    {
        endMouseOperation();
        event->accept();
        return;
    }

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
                // A receiver of drawSpaceChanged of the proxy can delete the widget
                QPointer<PDFDrawWidget> self(this);
                event->accept();
                m_widget->getDrawWidgetProxy()->performOperation(keyToOperation.second);
                if (!self)
                {
                    return;
                }
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

    // A press of any button ends the automatic scrolling (the press is not
    // a click of a tool, a link or an annotation)
    if (m_mouseOperation == MouseOperation::AutoScroll)
    {
        endMouseOperation();
        event->accept();
        return;
    }

    // The release of the drag of the page was lost (a popup, another window)
    if (m_mouseOperation == MouseOperation::Translate && event->button() == Qt::LeftButton)
    {
        m_mouseOperation = MouseOperation::None;
    }

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

    // The operation scrolls the view, a receiver of drawSpaceChanged of the proxy can
    // delete the widget
    QPointer<PDFDrawWidget> self(this);

    // The drag of the page ends by the release of its button, even when an input
    // interface accepts the release (its cursor comes back at once)
    if (m_mouseOperation == MouseOperation::Translate && event->button() == Qt::LeftButton)
    {
        performMouseOperation(event->pos());
        if (!self)
        {
            return;
        }
        m_mouseOperation = MouseOperation::None;
    }

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mouseReleaseEvent>(event))
    {
        return;
    }

    performMouseOperation(event->pos());
    if (!self)
    {
        return;
    }

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

    // The tool and the annotation manager share one hit test of the annotations and
    // one hover of the manager in the dispatch of the move
    PDFWidgetAnnotationManager::MouseMoveScope annotationScope(m_widget->getAnnotationManager());

    // The release of the drag of the page was lost (a popup, another window)
    if (m_mouseOperation == MouseOperation::Translate && !event->buttons().testFlag(Qt::LeftButton))
    {
        m_mouseOperation = MouseOperation::None;
    }

    // The operation scrolls the view, a receiver of drawSpaceChanged of the proxy can
    // delete the widget
    QPointer<PDFDrawWidget> self(this);

    // The own operation of the widget follows every move; the input interfaces still
    // get the move (their hover and cursor are current, when the operation ends)
    const bool isOwnOperation = isMouseOperationActive();
    if (isOwnOperation)
    {
        performMouseOperation(event->pos());
        if (!self)
        {
            return;
        }
    }

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mouseMoveEvent>(event))
    {
        return;
    }

    if (!isOwnOperation)
    {
        performMouseOperation(event->pos());
        if (!self)
        {
            return;
        }
    }
    updateCursor();
    event->accept();
}

void PDFDrawWidget::dragEnterEvent(QDragEnterEvent* event)
{
    event->ignore();

    PDFWidgetAnnotationManager* annotationManager = m_widget->getAnnotationManager();
    if (annotationManager && annotationManager->canAcceptAnnotationDrag(event->mimeData()))
    {
        const Qt::DropAction action = event->modifiers().testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction;
        event->setDropAction(action);
        event->accept();
    }
}

void PDFDrawWidget::dragMoveEvent(QDragMoveEvent* event)
{
    event->ignore();

    PDFWidgetAnnotationManager* annotationManager = m_widget->getAnnotationManager();
    if (annotationManager && annotationManager->canAcceptAnnotationDrag(event->mimeData()))
    {
        const Qt::DropAction action = event->modifiers().testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction;
        event->setDropAction(action);
        event->accept();

        // Snapping is disabled by Shift (Ctrl copies the annotations)
        annotationManager->updateAnnotationDropFeedback(event->mimeData(), event->position().toPoint(), !event->modifiers().testFlag(Qt::ShiftModifier));
    }
}

void PDFDrawWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    if (PDFWidgetAnnotationManager* annotationManager = m_widget->getAnnotationManager())
    {
        annotationManager->clearAnnotationDropFeedback();
    }

    event->accept();
}

void PDFDrawWidget::dropEvent(QDropEvent* event)
{
    event->ignore();

    PDFWidgetAnnotationManager* annotationManager = m_widget->getAnnotationManager();
    if (annotationManager && annotationManager->canAcceptAnnotationDrag(event->mimeData()))
    {
        const Qt::DropAction action = event->modifiers().testFlag(Qt::ControlModifier) ? Qt::CopyAction : Qt::MoveAction;
        if (annotationManager->handleAnnotationDrop(event->mimeData(), event->position().toPoint(), action, !event->modifiers().testFlag(Qt::ShiftModifier)))
        {
            event->setDropAction(action);
            event->accept();
        }
    }
}

void PDFDrawWidget::updateCursor()
{
    std::optional<QCursor> cursor;

    // The own operation of the widget wins: an input interface (the text selection
    // tool, the annotation manager) keeps the cursor of its last hover, which does
    // not say, what the mouse does now
    switch (m_mouseOperation)
    {
        case MouseOperation::None:
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

    if (!cursor)
    {
        for (IDrawWidgetInputInterface* inputInterface : m_widget->getInputInterfaces())
        {
            cursor = inputInterface->getCursor();

            if (cursor)
            {
                // We have found cursor
                break;
            }
        }
    }

    if (!cursor)
    {
        // No tool and nothing under the cursor: a drag pans the page
        cursor = QCursor(Qt::OpenHandCursor);
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

void PDFDrawWidget::onWheelScrollTimeout()
{
    if (m_wheelScrollPendingOffset.isNull())
    {
        m_wheelScrollTimer.stop();
        return;
    }

    QPointF stepOffset = m_wheelScrollPendingOffset * 0.35;
    int stepX = qRound(stepOffset.x());
    int stepY = qRound(stepOffset.y());

    if (stepX == 0 && !qFuzzyIsNull(m_wheelScrollPendingOffset.x()))
    {
        stepX = (m_wheelScrollPendingOffset.x() > 0.0) ? 1 : -1;
    }
    if (stepY == 0 && !qFuzzyIsNull(m_wheelScrollPendingOffset.y()))
    {
        stepY = (m_wheelScrollPendingOffset.y() > 0.0) ? 1 : -1;
    }

    // A receiver of drawSpaceChanged of the proxy can delete the widget
    QPointer<PDFDrawWidget> self(this);
    PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    const QPoint scrollStep(stepX, stepY);
    const QPoint appliedOffset = proxy->scrollByPixels(scrollStep);
    if (!self)
    {
        return;
    }

    if (appliedOffset.y() == 0 && scrollStep.y() != 0 && proxy->isBlockMode())
    {
        // We must move to another block (we are in block mode)
        const bool up = scrollStep.y() > 0;

        QScrollBar* verticalScrollbar = m_widget->getVerticalScrollbar();
        const int newValue = verticalScrollbar->value() + (up ? -1 : 1);

        if (newValue >= verticalScrollbar->minimum() && newValue <= verticalScrollbar->maximum())
        {
            verticalScrollbar->setValue(newValue);
            if (!self)
            {
                return;
            }
            proxy->scrollByPixels(QPoint(0, up ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max()));
            if (!self)
            {
                return;
            }
        }
    }

    if (appliedOffset.x() == 0 && scrollStep.x() != 0)
    {
        m_wheelScrollPendingOffset.setX(0.0);
    }
    else
    {
        m_wheelScrollPendingOffset.setX(m_wheelScrollPendingOffset.x() - appliedOffset.x());
    }

    if (appliedOffset.y() == 0 && scrollStep.y() != 0)
    {
        m_wheelScrollPendingOffset.setY(0.0);
    }
    else
    {
        m_wheelScrollPendingOffset.setY(m_wheelScrollPendingOffset.y() - appliedOffset.y());
    }

    if (qAbs(m_wheelScrollPendingOffset.x()) < 0.5)
    {
        m_wheelScrollPendingOffset.setX(0.0);
    }
    if (qAbs(m_wheelScrollPendingOffset.y()) < 0.5)
    {
        m_wheelScrollPendingOffset.setY(0.0);
    }

    if (m_wheelScrollPendingOffset.isNull())
    {
        m_wheelScrollTimer.stop();
    }
}

void PDFDrawWidget::wheelEvent(QWheelEvent* event)
{
    event->ignore();

    if (processEvent<QWheelEvent, &IDrawWidgetInputInterface::wheelEvent>(event))
    {
        return;
    }

    Qt::KeyboardModifiers keyboardModifiers = QApplication::keyboardModifiers();
    const bool shiftModifier = keyboardModifiers.testFlag(Qt::ShiftModifier);

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
        if (!scrollByPixels.isNull() && shiftModifier)
        {
            if (scrollByPixels.x() == 0)
            {
                scrollByPixels.setX(scrollByPixels.y());
            }
            scrollByPixels.setY(0);
        }

        if (scrollByPixels.isNull())
        {
            QPoint angleDelta = event->angleDelta();
            if (shiftModifier)
            {
                if (angleDelta.x() == 0)
                {
                    angleDelta.setX(angleDelta.y());
                }
                angleDelta.setY(0);
            }

            int stepVertical = 0;
            int stepHorizontal = 0;

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

                stepVertical = shiftModifier ? qMax(boundingBox.height(), 1) : qMax(boundingBox.height() / 10, 1);
                stepHorizontal = shiftModifier ? qMax(boundingBox.width(), 1) : qMax(boundingBox.width() / 10, 1);
            }
            else
            {
                stepVertical = shiftModifier ? m_widget->getVerticalScrollbar()->pageStep() : m_widget->getVerticalScrollbar()->singleStep();
                stepHorizontal = shiftModifier ? m_widget->getHorizontalScrollbar()->pageStep() : m_widget->getHorizontalScrollbar()->singleStep();
            }

            const int scrollVertical = stepVertical * static_cast<PDFReal>(angleDelta.y()) / static_cast<PDFReal>(QWheelEvent::DefaultDeltasPerStep);
            const int scrollHorizontal = stepHorizontal * static_cast<PDFReal>(angleDelta.x()) / static_cast<PDFReal>(QWheelEvent::DefaultDeltasPerStep);

            scrollByPixels = QPoint(scrollHorizontal, scrollVertical);
        }

        scrollByPixels.setX(qRound(static_cast<PDFReal>(scrollByPixels.x()) * m_horizontalWheelScrollSpeed));
        scrollByPixels.setY(qRound(static_cast<PDFReal>(scrollByPixels.y()) * m_verticalWheelScrollSpeed));

        if (m_smoothWheelScrolling)
        {
            m_wheelScrollPendingOffset += QPointF(scrollByPixels);
            if (!m_wheelScrollTimer.isActive())
            {
                m_wheelScrollTimer.setInterval(10);
                m_wheelScrollTimer.start();
            }
            onWheelScrollTimeout();
        }
        else
        {
            // A receiver of drawSpaceChanged of the proxy can delete the widget
            QPointer<PDFDrawWidget> self(this);
            QPoint offset = proxy->scrollByPixels(scrollByPixels);
            if (!self)
            {
                event->accept();
                return;
            }

            if (offset.y() == 0 && scrollByPixels.y() != 0 && proxy->isBlockMode())
            {
                // We must move to another block (we are in block mode)
                bool up = scrollByPixels.y() > 0;

                QScrollBar* verticalScrollbar = m_widget->getVerticalScrollbar();
                const int newValue = verticalScrollbar->value() + (up ? -1 : 1);

                if (newValue >= verticalScrollbar->minimum() && newValue <= verticalScrollbar->maximum())
                {
                    verticalScrollbar->setValue(newValue);
                    if (self)
                    {
                        proxy->scrollByPixels(QPoint(0, up ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max()));
                    }
                }
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
