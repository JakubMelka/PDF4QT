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

#include "pdfapptoolbeforeafteroverlay.h"

#include "pdfapppagerendercontext.h"

#include "pdfcatalog.h"
#include "pdfcomponentpainter.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfoperationcontrol.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfthreadannotations.h"
#include "pdfwidgetstyle.h"

#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QThreadPool>
#include <QtMath>

#include <atomic>
#include <cmath>
#include <exception>

namespace pdf
{

/// The way of a finished job back to the overlay: cut by the destructor of the overlay,
/// a job does not hold the overlay (a late result goes with the job)
class PDFAppToolBeforeAfterLink
{
public:
    explicit PDFAppToolBeforeAfterLink(PDFAppToolBeforeAfterOverlay* overlay) :
        m_overlay(overlay)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_overlay = nullptr;
    }

    void finished(const std::shared_ptr<PDFAppToolBeforeAfterJob>& job)
    {
        QMutexLocker locker(&m_mutex);
        if (m_overlay)
        {
            PDFThreadAnnotations::happensBefore(job.get());
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_overlay, &PDFAppToolBeforeAfterOverlay::onRenderFinished, Qt::QueuedConnection, job);
        }
    }

private:
    QMutex m_mutex;
    PDFAppToolBeforeAfterOverlay* m_overlay;
};

/// A page of a job
class PDFAppToolBeforeAfterJobPage
{
public:
    PDFInteger pageIndex = -1;
    QTransform pageToImage;
    QSize imageSize;
    QImage image;
};

/// One rendering of the shown pages of the result in the pool. Its members are written
/// before the start and read after the end (the hand-offs are annotated), except of the
/// cancellation.
class PDFAppToolBeforeAfterJob : public PDFOperationControl
{
public:
    std::shared_ptr<PDFAppToolBeforeAfterLink> link;
    PDFDocumentPointer document;
    PDFCMSSettings cmsSettings;
    PDFRenderer::Features features;
    QColor paperColor;
    std::vector<PDFAppToolBeforeAfterJobPage> pages;

    virtual bool isOperationCancelled() const override
    {
        return m_isCanceled.load(std::memory_order_relaxed);
    }

    void cancel()
    {
        m_isCanceled.store(true, std::memory_order_relaxed);
    }

    /// Runs in a worker
    void run()
    {
        try
        {
            PDFAppPageRenderContext context(document.data(), cmsSettings, features);
            for (PDFAppToolBeforeAfterJobPage& page : pages)
            {
                if (isOperationCancelled())
                {
                    return;
                }

                QImage image(page.imageSize, QImage::Format_ARGB32_Premultiplied);
                if (image.isNull())
                {
                    continue;
                }
                image.fill(paperColor);
                {
                    QPainter painter(&image);
                    context.draw(&painter, page.pageIndex, page.pageToImage, features.testFlag(PDFRenderer::DisplayAnnotations), this);
                }
                page.image = std::move(image);
            }
        }
        catch (const PDFException&)
        {
            // A damaged page of the result: the original stays shown
            pages.clear();
        }
        catch (const std::exception&)
        {
            // No memory for the images of a huge page: the same, the tab stays
            pages.clear();
        }
    }

private:
    std::atomic_bool m_isCanceled = false;
};

/// The task of the pool: it holds the job, never the overlay
class PDFAppToolBeforeAfterTask
{
public:
    explicit PDFAppToolBeforeAfterTask(std::shared_ptr<PDFAppToolBeforeAfterJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()()
    {
        m_job->run();
        const std::shared_ptr<PDFAppToolBeforeAfterLink> link = m_job->link;
        link->finished(m_job);
    }

private:
    std::shared_ptr<PDFAppToolBeforeAfterJob> m_job;
};

PDFAppToolBeforeAfterOverlay::PDFAppToolBeforeAfterOverlay(PDFWidget* pdfWidget, QWidget* canvasArea) :
    BaseClass(canvasArea),
    m_pdfWidget(pdfWidget),
    m_proxy(pdfWidget ? pdfWidget->getDrawWidgetProxy() : nullptr),
    m_canvasArea(canvasArea),
    m_drawWidget(pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr),
    m_link(std::make_shared<PDFAppToolBeforeAfterLink>(this))
{
    setObjectName(QStringLiteral("toolBeforeAfterHandle"));
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::SplitHCursor);
    setFixedSize(HANDLE_SIZE + 2 * FOCUS_MARGIN, HANDLE_SIZE + 2 * FOCUS_MARGIN);
    setAccessibleName(tr("Divider between the original and the result"));
    hide();

    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(RENDER_DELAY);
    connect(&m_renderTimer, &QTimer::timeout, this, &PDFAppToolBeforeAfterOverlay::startRender);

    if (m_canvasArea)
    {
        m_canvasArea->installEventFilter(this);
    }
    if (m_proxy)
    {
        m_proxy->registerDrawInterface(this);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppToolBeforeAfterOverlay::updateCanvas);
    updatePlacement();
}

PDFAppToolBeforeAfterOverlay::~PDFAppToolBeforeAfterOverlay()
{
    // Nobody waits: the job goes on with its own data, its late result is dropped
    cancelRender();
    m_link->detach();
    if (m_proxy)
    {
        m_proxy->unregisterDrawInterface(this);
    }
    updateCanvas();
}

void PDFAppToolBeforeAfterOverlay::setBeforeText(const QString& text)
{
    if (m_beforeText != text)
    {
        m_beforeText = text;
        updateCanvas();
    }
}

void PDFAppToolBeforeAfterOverlay::setResult(const PDFDocumentPointer& document, const QString& afterText)
{
    const bool isSameDocument = m_document == document;
    m_afterText = afterText;
    m_document = afterText.isEmpty() ? PDFDocumentPointer() : document;
    if (!isSameDocument || afterText.isEmpty())
    {
        cancelRender();
        m_renderedPages.clear();
        m_requestedGeometry.clear();
        m_shownGeometry.clear();
        m_finishedRenderCount = 0;
    }
    setAccessibleDescription(afterText.isEmpty() ? m_beforeText : QStringLiteral("%1, %2").arg(m_beforeText, afterText));
    updatePlacement();
    updateCanvas();
}

void PDFAppToolBeforeAfterOverlay::setOverlayShown(bool isShown)
{
    // Also for the same value: the shell shows the widget of an overlay at its entry
    m_isOverlayShown = isShown;
    updatePlacement();
    updateCanvas();
}

int PDFAppToolBeforeAfterOverlay::getLinePosition() const
{
    const int width = m_canvasArea ? m_canvasArea->width() : 0;
    return qBound(HANDLE_SIZE / 2, int(std::lround(m_lineFraction * width)), qMax(HANDLE_SIZE / 2, width - HANDLE_SIZE / 2));
}

void PDFAppToolBeforeAfterOverlay::setLinePosition(int position)
{
    const int width = m_canvasArea ? m_canvasArea->width() : 0;
    if (width <= 0)
    {
        return;
    }

    m_lineFraction = qBound(0.0, double(position) / double(width), 1.0);
    updatePlacement();
    updateCanvas();
}

QRect PDFAppToolBeforeAfterOverlay::getHandleRect() const
{
    // split-handle 293:1347: 380 of the 842 of the canvas
    const int height = m_canvasArea ? m_canvasArea->height() : 0;
    return QRect(getLinePosition() - HANDLE_SIZE / 2, int(std::lround(double(height) * 380.0 / 842.0)), HANDLE_SIZE, HANDLE_SIZE);
}

QRect PDFAppToolBeforeAfterOverlay::getLineRect() const
{
    return QRect(getLinePosition() - LINE_WIDTH / 2, 0, LINE_WIDTH, m_canvasArea ? m_canvasArea->height() : 0);
}

int PDFAppToolBeforeAfterOverlay::getChipWidth(const QString& text) const
{
    return QFontMetrics(PDFThemeFonts::getFont(QFont::DemiBold, CHIP_FONT_SIZE)).horizontalAdvance(text) + 2 * CHIP_PADDING;
}

QRect PDFAppToolBeforeAfterOverlay::getBeforeChipRect() const
{
    // The chips reach to the edges of the handle; a chip near to the edge of the canvas
    // stays inside of it. Without a line the chip lies where the line would be.
    const int canvasWidth = m_canvasArea ? m_canvasArea->width() : 0;
    const int width = getChipWidth(m_beforeText);
    const int right = getLinePosition() - HANDLE_SIZE / 2;
    return QRect(qMax(0, qMin(right, canvasWidth) - width), CHIP_TOP, width, CHIP_HEIGHT);
}

QRect PDFAppToolBeforeAfterOverlay::getAfterChipRect() const
{
    const int canvasWidth = m_canvasArea ? m_canvasArea->width() : 0;
    const int width = getChipWidth(m_afterText);
    const int left = getLinePosition() + HANDLE_SIZE / 2;
    const QRect rect(qMax(0, qMin(left, canvasWidth - width)), CHIP_TOP, width, CHIP_HEIGHT);

    // A narrow canvas or a line at its edge: the chip goes below the other one, no frame
    // has both chips without their room
    if (!m_beforeText.isEmpty() && rect.adjusted(-CHIP_GAP, 0, 0, 0).intersects(getBeforeChipRect()))
    {
        return rect.translated(0, CHIP_HEIGHT + CHIP_GAP);
    }
    return rect;
}

std::vector<PDFInteger> PDFAppToolBeforeAfterOverlay::getRenderedPages() const
{
    std::vector<PDFInteger> pages;
    for (const RenderedPage& page : m_renderedPages)
    {
        pages.push_back(page.pageIndex);
    }
    return pages;
}

bool PDFAppToolBeforeAfterOverlay::isRenderPending() const
{
    return m_job != nullptr || m_renderTimer.isActive();
}

QPoint PDFAppToolBeforeAfterOverlay::getCanvasOffset() const
{
    return m_drawWidget && m_canvasArea ? m_drawWidget->mapFrom(m_canvasArea, QPoint(0, 0)) : QPoint(0, 0);
}

void PDFAppToolBeforeAfterOverlay::updatePlacement()
{
    if (hasResult() && m_isOverlayShown)
    {
        move(getHandleRect().topLeft() - QPoint(FOCUS_MARGIN, FOCUS_MARGIN));
        show();
    }
    else
    {
        hide();
    }
}

void PDFAppToolBeforeAfterOverlay::updateCanvas()
{
    update();
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

void PDFAppToolBeforeAfterOverlay::cancelRender()
{
    m_renderTimer.stop();
    if (m_job)
    {
        m_job->cancel();
        m_job.reset();
    }
}

void PDFAppToolBeforeAfterOverlay::startRender()
{
    cancelRender();

    const PDFDocument* document = m_document.data();
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || !m_proxy || !m_drawWidget || !m_isOverlayShown)
    {
        return;
    }

    // The visible part of each shown page: moving the line needs no new rendering. A
    // zoom of a page far over the canvas stays bounded by the area of the canvas.
    struct VisiblePage
    {
        PDFInteger pageIndex = -1;
        QTransform matrix;
        QRectF visibleRect;
    };

    std::vector<VisiblePage> visiblePages;
    double area = 0.0;
    const QRectF widgetRect(m_drawWidget->rect());
    for (const PageGeometry& geometry : m_shownGeometry)
    {
        // A page without a counterpart in the result stays the original
        if (geometry.pageIndex < 0 || size_t(geometry.pageIndex) >= catalog->getPageCount())
        {
            continue;
        }

        const PDFPage* page = catalog->getPage(size_t(geometry.pageIndex));

        const QRectF visibleRect = geometry.matrix.mapRect(page->getCropBox()).intersected(widgetRect);
        if (visibleRect.isEmpty())
        {
            continue;
        }

        visiblePages.push_back({ geometry.pageIndex, geometry.matrix, visibleRect });
        area += visibleRect.width() * visibleRect.height();
    }

    // The scale of the pixels of the screen, smaller only over the limit of the memory
    double scale = m_drawWidget->devicePixelRatioF();
    if (area * scale * scale > double(MAXIMUM_PIXELS))
    {
        scale = std::sqrt(double(MAXIMUM_PIXELS) / area);
    }

    std::shared_ptr<PDFAppToolBeforeAfterJob> job = std::make_shared<PDFAppToolBeforeAfterJob>();
    job->link = m_link;
    job->document = m_document;
    job->cmsSettings = m_proxy->getCMSManager()->getSettings();
    job->features = m_proxy->getFeatures();
    job->paperColor = m_proxy->getPaperColor();
    for (const VisiblePage& visiblePage : visiblePages)
    {
        QTransform imageMatrix;
        imageMatrix.scale(scale, scale);
        imageMatrix.translate(-visiblePage.visibleRect.left(), -visiblePage.visibleRect.top());

        PDFAppToolBeforeAfterJobPage page;
        page.pageIndex = visiblePage.pageIndex;
        page.pageToImage = visiblePage.matrix * imageMatrix;
        page.imageSize = QSize(qMax(1, qCeil(visiblePage.visibleRect.width() * scale)), qMax(1, qCeil(visiblePage.visibleRect.height() * scale)));
        job->pages.push_back(std::move(page));
    }

    m_requestedGeometry = m_shownGeometry;
    m_job = job;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppToolBeforeAfterTask(std::move(job)));
}

void PDFAppToolBeforeAfterOverlay::onRenderFinished(const std::shared_ptr<PDFAppToolBeforeAfterJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    // A job of an older result or of an older view
    if (job != m_job || job->isOperationCancelled())
    {
        return;
    }
    m_job.reset();

    m_renderedPages.clear();
    for (PDFAppToolBeforeAfterJobPage& page : job->pages)
    {
        if (!page.image.isNull())
        {
            m_renderedPages.push_back({ page.pageIndex, std::move(page.image), page.pageToImage.inverted() });
        }
    }
    ++m_finishedRenderCount;
    updateCanvas();
}

void PDFAppToolBeforeAfterOverlay::drawPage(QPainter* painter,
                                            pdf::PDFInteger pageIndex,
                                            const PDFPrecompiledPage* compiledPage,
                                            PDFTextLayoutGetter& layoutGetter,
                                            const QTransform& pagePointToDevicePointMatrix,
                                            const PDFColorConvertor& convertor,
                                            QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);

    if (!hasResult() || !m_isOverlayShown || !m_document)
    {
        return;
    }

    // A second drawing of the pages at another scale (the magnifier) is no view to render
    if (painter->worldTransform().isIdentity())
    {
        m_paintedGeometry.push_back({ pageIndex, pagePointToDevicePointMatrix });
    }

    for (const RenderedPage& page : m_renderedPages)
    {
        if (page.pageIndex == pageIndex)
        {
            // Inside of the page, so another draw interface draws over it (the frame of
            // a selection, which is drawn after all the pages)
            PDFPainterStateGuard guard(painter);
            const int lineX = getLinePosition() + getCanvasOffset().x();
            const QRectF area = painter->worldTransform().inverted().mapRect(QRectF(painter->viewport()));
            painter->setClipRect(QRectF(lineX, area.top(), qMax(0.0, area.right() - lineX), area.height()), Qt::IntersectClip);
            painter->setWorldTransform(page.imageToPage * pagePointToDevicePointMatrix);
            painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter->drawImage(QPointF(0.0, 0.0), page.image);
            break;
        }
    }
}

void PDFAppToolBeforeAfterOverlay::drawChip(QPainter* painter, const QRect& rect, const QString& text) const
{
    PDFStylePainter::fill(painter, QRectF(rect), PDFStylePainter::Radii(qreal(CHIP_HEIGHT) / 2.0), PDFThemeColors::IconStrong);
    painter->setFont(PDFThemeFonts::getFont(QFont::DemiBold, CHIP_FONT_SIZE));
    painter->setPen(PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgSurface));
    painter->drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, text);
}

void PDFAppToolBeforeAfterOverlay::drawPostRendering(QPainter* painter, QRect rect) const
{
    Q_UNUSED(rect);

    // What this paint has shown: new pages or a new zoom ask for a rendering after the rest
    if (hasResult() && m_isOverlayShown && m_document)
    {
        if (m_paintedGeometry != m_shownGeometry)
        {
            m_shownGeometry = m_paintedGeometry;
            if (m_shownGeometry != m_requestedGeometry)
            {
                m_renderTimer.start();
            }
            else
            {
                m_renderTimer.stop();
            }
        }
    }
    m_paintedGeometry.clear();

    if (!m_isOverlayShown)
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(getCanvasOffset());

    if (hasResult())
    {
        painter->fillRect(getLineRect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgAccent));
        drawChip(painter, getAfterChipRect(), m_afterText);
    }
    if (!m_beforeText.isEmpty())
    {
        drawChip(painter, getBeforeChipRect(), m_beforeText);
    }
}

bool PDFAppToolBeforeAfterOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvasArea && event->type() == QEvent::Resize)
    {
        updatePlacement();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolBeforeAfterOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // split-handle 293:1347: the circle bg/accent with the icon columns on the accent
    QPainter painter(this);
    const QRectF shape(FOCUS_MARGIN, FOCUS_MARGIN, HANDLE_SIZE, HANDLE_SIZE);
    const PDFStylePainter::Radii radii(qreal(HANDLE_SIZE) / 2.0);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(&painter, shape, radii, PDFThemeColors::BgAccent);
    const qreal iconOffset = (HANDLE_SIZE - HANDLE_ICON) / 2.0;
    PDFStylePainter::icon(&painter, QRectF(shape.left() + iconOffset, shape.top() + iconOffset, HANDLE_ICON, HANDLE_ICON), QStringLiteral("columns"), PDFThemeColors::IconOnAccent);
    if (hasFocus() && PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFComponentPainter::paintFocusRingOutside(&painter, shape, radii);
    }
}

void PDFAppToolBeforeAfterOverlay::keyPressEvent(QKeyEvent* event)
{
    // Never mirrored: the original is always at the left of the line
    const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 1 : KEY_STEP;
    const int width = m_canvasArea ? m_canvasArea->width() : 0;
    switch (event->key())
    {
        case Qt::Key_Left:
            setLinePosition(getLinePosition() - step);
            break;
        case Qt::Key_Right:
            setLinePosition(getLinePosition() + step);
            break;
        case Qt::Key_Home:
            setLinePosition(0);
            break;
        case Qt::Key_End:
            setLinePosition(width);
            break;
        default:
            BaseClass::keyPressEvent(event);
            return;
    }
    event->accept();
}

void PDFAppToolBeforeAfterOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isDragging = true;
        event->accept();
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppToolBeforeAfterOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging && event->buttons().testFlag(Qt::LeftButton))
    {
        setLinePosition(mapToParent(event->position().toPoint()).x());
        event->accept();
        return;
    }
    m_isDragging = false;
    BaseClass::mouseMoveEvent(event);
}

void PDFAppToolBeforeAfterOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    m_isDragging = false;
    BaseClass::mouseReleaseEvent(event);
}

void PDFAppToolBeforeAfterOverlay::focusInEvent(QFocusEvent* event)
{
    BaseClass::focusInEvent(event);
    update();
}

void PDFAppToolBeforeAfterOverlay::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);
    update();
}

}   // namespace pdf

#include "moc_pdfapptoolbeforeafteroverlay.cpp"
