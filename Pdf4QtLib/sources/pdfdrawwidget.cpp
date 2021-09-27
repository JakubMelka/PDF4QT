//    Copyright (C) 2018-2021 Jakub Melka
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
#include "pdfform.h"

#include <QPainter>
#include <QGridLayout>
#include <QKeyEvent>
#include <QApplication>
#include <QPixmapCache>

namespace pdf
{

PDFWidget::PDFWidget(const PDFCMSManager* cmsManager, RendererEngine engine, int samplesCount, QWidget* parent) :
    QWidget(parent),
    m_cmsManager(cmsManager),
    m_toolManager(nullptr),
    m_annotationManager(nullptr),
    m_formManager(nullptr),
    m_drawWidget(nullptr),
    m_horizontalScrollBar(nullptr),
    m_verticalScrollBar(nullptr),
    m_proxy(nullptr)
{
    m_drawWidget = createDrawWidget(engine, samplesCount);
    m_horizontalScrollBar = new QScrollBar(Qt::Horizontal, this);
    m_verticalScrollBar = new QScrollBar(Qt::Vertical, this);

    QGridLayout* layout = new QGridLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_drawWidget->getWidget(), 0, 0);
    layout->addWidget(m_horizontalScrollBar, 1, 0);
    layout->addWidget(m_verticalScrollBar, 0, 1);
    layout->setMargin(0);

    setLayout(layout);
    setFocusProxy(m_drawWidget->getWidget());

    m_proxy = new PDFDrawWidgetProxy(this);
    m_proxy->init(this);
    connect(m_proxy, &PDFDrawWidgetProxy::renderingError, this, &PDFWidget::onRenderingError);
    connect(m_proxy, &PDFDrawWidgetProxy::repaintNeeded, m_drawWidget->getWidget(), QOverload<>::of(&QWidget::update));
    connect(m_proxy, &PDFDrawWidgetProxy::pageImageChanged, this, &PDFWidget::onPageImageChanged);
    updateRendererImpl();
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

void PDFWidget::updateRenderer(RendererEngine engine, int samplesCount)
{
    PDFOpenGLDrawWidget* openglDrawWidget = qobject_cast<PDFOpenGLDrawWidget*>(m_drawWidget->getWidget());
    PDFDrawWidget* softwareDrawWidget = qobject_cast<PDFDrawWidget*>(m_drawWidget->getWidget());

    // Do we need to change renderer?
    if ((openglDrawWidget && engine != RendererEngine::OpenGL) || (softwareDrawWidget && engine != RendererEngine::Software))
    {
        QGridLayout* layout = qobject_cast<QGridLayout*>(this->layout());
        layout->removeWidget(m_drawWidget->getWidget());
        delete m_drawWidget->getWidget();

        m_drawWidget = createDrawWidget(engine, samplesCount);
        layout->addWidget(m_drawWidget->getWidget(), 0, 0);
        setFocusProxy(m_drawWidget->getWidget());
        connect(m_proxy, &PDFDrawWidgetProxy::repaintNeeded, m_drawWidget->getWidget(), QOverload<>::of(&QWidget::update));
    }
    else if (openglDrawWidget)
    {
        // Just check the samples count
        QSurfaceFormat format = openglDrawWidget->format();
        if (format.samples() != samplesCount)
        {
            format.setSamples(samplesCount);
            openglDrawWidget->setFormat(format);
        }
    }
    updateRendererImpl();
}

void PDFWidget::updateCacheLimits(int compiledPageCacheLimit, int thumbnailsCacheLimit, int fontCacheLimit, int instancedFontCacheLimit)
{
    m_proxy->getCompiler()->setCacheLimit(compiledPageCacheLimit);
    QPixmapCache::setCacheLimit(thumbnailsCacheLimit);
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

void PDFWidget::updateRendererImpl()
{
    PDFOpenGLDrawWidget* openglDrawWidget = qobject_cast<PDFOpenGLDrawWidget*>(m_drawWidget->getWidget());
    m_proxy->updateRenderer(openglDrawWidget != nullptr, openglDrawWidget ? openglDrawWidget->format() : QSurfaceFormat::defaultFormat());
}

void PDFWidget::onRenderingError(PDFInteger pageIndex, const QList<PDFRenderError>& errors)
{
    // Empty list of error should not be reported!
    Q_ASSERT(!errors.empty());
    m_pageRenderingErrors[pageIndex] = errors;
    emit pageRenderingErrorsChanged(pageIndex, errors.size());
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

IDrawWidget* PDFWidget::createDrawWidget(RendererEngine rendererEngine, int samplesCount)
{
    switch (rendererEngine)
    {
        case RendererEngine::Software:
            return new PDFDrawWidget(this, this);

        case RendererEngine::OpenGL:
            return new PDFOpenGLDrawWidget(this, samplesCount, this);

        default:
            Q_ASSERT(false);
            break;
    }

    return nullptr;
}

void PDFWidget::removeInputInterface(IDrawWidgetInputInterface* inputInterface)
{
    auto it = std::find(m_inputInterfaces.begin(), m_inputInterfaces.end(), inputInterface);
    if (it != m_inputInterfaces.end())
    {
        m_inputInterfaces.erase(it);
    }
}

void PDFWidget::addInputInterface(IDrawWidgetInputInterface* inputInterface)
{
    if (inputInterface)
    {
        m_inputInterfaces.push_back(inputInterface);
        std::sort(m_inputInterfaces.begin(), m_inputInterfaces.end(), IDrawWidgetInputInterface::Comparator());
    }
}

PDFFormManager* PDFWidget::getFormManager() const
{
    return m_formManager;
}

void PDFWidget::setFormManager(PDFFormManager* formManager)
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

template<typename BaseWidget>
PDFDrawWidgetBase<BaseWidget>::PDFDrawWidgetBase(PDFWidget* widget, QWidget* parent) :
    BaseWidget(parent),
    m_widget(widget),
    m_mouseOperation(MouseOperation::None)
{
    this->setFocusPolicy(Qt::StrongFocus);
    this->setMouseTracking(true);
}

template<typename BaseWidget>
std::vector<PDFInteger> PDFDrawWidgetBase<BaseWidget>::getCurrentPages() const
{
    return this->m_widget->getDrawWidgetProxy()->getPagesIntersectingRect(this->rect());
}

template<typename BaseWidget>
QSize PDFDrawWidgetBase<BaseWidget>::minimumSizeHint() const
{
    return QSize(200, 200);
}

template<typename BaseWidget>
bool PDFDrawWidgetBase<BaseWidget>::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        return processEvent<QKeyEvent, &IDrawWidgetInputInterface::shortcutOverrideEvent>(static_cast<QKeyEvent*>(event));
    }

    return BaseWidget::event(event);
}

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::performMouseOperation(QPoint currentMousePosition)
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

template<typename BaseWidget>
template<typename Event, void (IDrawWidgetInputInterface::* Function)(QWidget*, Event*)>
bool PDFDrawWidgetBase<BaseWidget>::processEvent(Event* event)
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

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::keyPressEvent(QKeyEvent* event)
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

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::keyReleaseEvent(QKeyEvent* event)
{
    event->ignore();

    if (processEvent<QKeyEvent, &IDrawWidgetInputInterface::keyReleaseEvent>(event))
    {
        return;
    }

    event->accept();
}

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::mousePressEvent(QMouseEvent* event)
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

    updateCursor();
    event->accept();
}

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->ignore();

    if (processEvent<QMouseEvent, &IDrawWidgetInputInterface::mouseDoubleClickEvent>(event))
    {
        return;
    }
}

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::mouseReleaseEvent(QMouseEvent* event)
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
            m_mouseOperation = MouseOperation::None;
            break;
        }

        default:
            Q_ASSERT(false);
    }

    updateCursor();
    event->accept();
}

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::mouseMoveEvent(QMouseEvent* event)
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

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::updateCursor()
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

template<typename BaseWidget>
void PDFDrawWidgetBase<BaseWidget>::wheelEvent(QWheelEvent* event)
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

PDFOpenGLDrawWidget::PDFOpenGLDrawWidget(PDFWidget* widget, int samplesCount, QWidget* parent) :
    BaseClass(widget, parent)
{
    QSurfaceFormat format = this->format();
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(samplesCount);
    format.setColorSpace(QSurfaceFormat::sRGBColorSpace);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);
}

PDFOpenGLDrawWidget::~PDFOpenGLDrawWidget()
{

}

void PDFOpenGLDrawWidget::resizeGL(int w, int h)
{
    QOpenGLWidget::resizeGL(w, h);

    getPDFWidget()->getDrawWidgetProxy()->update();
}

void PDFOpenGLDrawWidget::initializeGL()
{
    QOpenGLWidget::initializeGL();
}

void PDFOpenGLDrawWidget::paintGL()
{
    QPainter painter(this);
    getPDFWidget()->getDrawWidgetProxy()->draw(&painter, this->rect());
}

PDFDrawWidget::PDFDrawWidget(PDFWidget* widget, QWidget* parent) :
    BaseClass(widget, parent)
{

}

PDFDrawWidget::~PDFDrawWidget()
{

}

void PDFDrawWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    getPDFWidget()->getDrawWidgetProxy()->draw(&painter, this->rect());
}

void PDFDrawWidget::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);

    getPDFWidget()->getDrawWidgetProxy()->update();
}

template class PDFDrawWidgetBase<QOpenGLWidget>;
template class PDFDrawWidgetBase<QWidget>;

}   // namespace pdf
