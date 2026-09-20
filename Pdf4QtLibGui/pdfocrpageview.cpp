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

#include "pdfocrpageview.h"
#include "pdfocrpagepreparer.h"

#include <QPainter>
#include <QToolTip>
#include <QScrollBar>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

namespace pdfviewer
{

static constexpr double MINIMUM_ZOOM = 0.02;
static constexpr double MAXIMUM_ZOOM = 16.0;
static constexpr double HANDLE_SIZE = 9.0;

PDFOCRPageView::PDFOCRPageView(QWidget* parent) :
    BaseClass(parent)
{
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 200);
    qRegisterMetaType<pdf::PDFOCRQuad>("pdf::PDFOCRQuad");
}

void PDFOCRPageView::setImage(const QImage& image, const QTransform& pageToImage, const QString& caption)
{
    const bool sizeChanged = image.size() != m_image.size();
    m_image = image;
    m_pageToImage = pageToImage;
    m_caption = caption;
    m_message.clear();

    if (m_fitMode || sizeChanged)
    {
        if (m_fitMode)
        {
            zoomFit();
        }
        else
        {
            updateScrollBars();
        }
    }

    viewport()->update();
}

void PDFOCRPageView::clearImage(const QString& message)
{
    m_image = QImage();
    m_message = message;
    updateScrollBars();
    viewport()->update();
}

void PDFOCRPageView::setPageResult(const pdf::PDFOCRPageResult* result)
{
    if (result)
    {
        m_result = *result;
    }
    else
    {
        m_result.reset();
    }
    viewport()->update();
}

void PDFOCRPageView::setReviewThreshold(double threshold)
{
    m_threshold = threshold;
    viewport()->update();
}

void PDFOCRPageView::setOverlayVisible(bool visible)
{
    // Scroll position and zoom are not touched (UI-04)
    m_overlayVisible = visible;
    viewport()->update();
}

void PDFOCRPageView::setRegionsVisible(bool visible)
{
    m_regionsVisible = visible;
    viewport()->update();
}

void PDFOCRPageView::setSelectedWord(int wordId, bool ensureVisible)
{
    m_selectedWordId = wordId;

    if (ensureVisible && m_result && !m_image.isNull())
    {
        if (const pdf::PDFOCRWord* word = m_result->findWord(wordId))
        {
            const QRectF rect = getPageToView().map(word->quad.toPolygon()).boundingRect();
            const QRectF visible(QPointF(0, 0), QSizeF(viewport()->size()));
            if (!visible.adjusted(20, 20, -20, -20).contains(rect))
            {
                const QPointF center = rect.center();
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() + int(center.x() - visible.center().x()));
                verticalScrollBar()->setValue(verticalScrollBar()->value() + int(center.y() - visible.center().y()));
            }
        }
    }

    viewport()->update();
}

void PDFOCRPageView::setSelectedLine(int lineId)
{
    m_selectedLineId = lineId;
    viewport()->update();
}

void PDFOCRPageView::setSelectedRegion(int regionId)
{
    m_selectedRegionId = regionId;
    viewport()->update();
}

void PDFOCRPageView::setMode(Mode mode)
{
    m_mode = mode;
    m_dragging = false;
    m_activeHandle = Handle::None;

    switch (mode)
    {
        case Mode::Select:
            viewport()->setCursor(Qt::ArrowCursor);
            break;
        case Mode::EditWordGeometry:
            viewport()->setCursor(Qt::SizeAllCursor);
            break;
        default:
            viewport()->setCursor(Qt::CrossCursor);
            break;
    }

    viewport()->update();
}

void PDFOCRPageView::setZoom(double zoom)
{
    zoom = qBound(MINIMUM_ZOOM, zoom, MAXIMUM_ZOOM);
    if (qFuzzyCompare(zoom, m_zoom))
    {
        return;
    }

    // Keep the center of the view
    const QPointF center = viewToImage(QPointF(viewport()->width() * 0.5, viewport()->height() * 0.5));
    m_zoom = zoom;
    m_fitMode = false;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(center.x() * m_zoom - viewport()->width() * 0.5));
    verticalScrollBar()->setValue(int(center.y() * m_zoom - viewport()->height() * 0.5));
    viewport()->update();
    Q_EMIT zoomChanged(m_zoom);
}

void PDFOCRPageView::zoomIn()
{
    setZoom(m_zoom * 1.25);
}

void PDFOCRPageView::zoomOut()
{
    setZoom(m_zoom / 1.25);
}

void PDFOCRPageView::zoomFit()
{
    if (m_image.isNull())
    {
        m_fitMode = true;
        return;
    }

    const QSizeF available = QSizeF(viewport()->size()) - QSizeF(8, 8);
    const double zoom = qMin(available.width() / m_image.width(), available.height() / m_image.height());
    m_zoom = qBound(MINIMUM_ZOOM, zoom, MAXIMUM_ZOOM);
    m_fitMode = true;
    updateScrollBars();
    viewport()->update();
    Q_EMIT zoomChanged(m_zoom);
}

void PDFOCRPageView::updateScrollBars()
{
    const QSize scaled = m_image.isNull() ? QSize() : QSize(int(std::ceil(m_image.width() * m_zoom)), int(std::ceil(m_image.height() * m_zoom)));
    const QSize view = viewport()->size();
    horizontalScrollBar()->setRange(0, qMax(0, scaled.width() - view.width()));
    verticalScrollBar()->setRange(0, qMax(0, scaled.height() - view.height()));
    horizontalScrollBar()->setPageStep(view.width());
    verticalScrollBar()->setPageStep(view.height());
    horizontalScrollBar()->setSingleStep(24);
    verticalScrollBar()->setSingleStep(24);
}

QTransform PDFOCRPageView::getImageToView() const
{
    const double scaledWidth = m_image.width() * m_zoom;
    const double scaledHeight = m_image.height() * m_zoom;

    // Image smaller than the viewport is centered
    const double offsetX = scaledWidth < viewport()->width() ? (viewport()->width() - scaledWidth) * 0.5 : -horizontalScrollBar()->value();
    const double offsetY = scaledHeight < viewport()->height() ? (viewport()->height() - scaledHeight) * 0.5 : -verticalScrollBar()->value();

    QTransform transform;
    transform.translate(offsetX, offsetY);
    transform.scale(m_zoom, m_zoom);
    return transform;
}

QTransform PDFOCRPageView::getPageToView() const
{
    return m_pageToImage * getImageToView();
}

QPointF PDFOCRPageView::viewToImage(const QPointF& point) const
{
    return getImageToView().inverted().map(point);
}

void PDFOCRPageView::drawWord(QPainter& painter, const pdf::PDFOCRWord& word, const QTransform& pageToView, bool selected) const
{
    if (!word.quad.isValid())
    {
        return;
    }

    const QPolygonF polygon = pageToView.map(word.quad.toPolygon());

    // The state is expressed by the color and by the line style and marks (UI-05)
    QColor color;
    Qt::PenStyle penStyle = Qt::SolidLine;
    bool cross = false;
    bool cornerMark = false;

    switch (word.reviewState)
    {
        case pdf::PDFOCRReviewState::Confirmed:
            color = QColor(0, 140, 60);
            break;

        case pdf::PDFOCRReviewState::Modified:
            color = QColor(30, 90, 220);
            cornerMark = true;
            break;

        case pdf::PDFOCRReviewState::Discarded:
            color = QColor(120, 120, 120);
            penStyle = Qt::DotLine;
            cross = true;
            break;

        case pdf::PDFOCRReviewState::Unreviewed:
            if (!word.confidence.isAvailable())
            {
                color = QColor(170, 0, 170);
                penStyle = Qt::DotLine;
            }
            else if (pdf::PDFOCRReview::requiresReview(word, m_threshold))
            {
                color = QColor(225, 110, 0);
                penStyle = Qt::DashLine;
            }
            else
            {
                color = QColor(0, 140, 60, 160);
            }
            break;
    }

    QColor fill = color;
    fill.setAlpha(selected ? 90 : 28);
    painter.setPen(QPen(color, selected ? 2.5 : 1.2, selected ? Qt::SolidLine : penStyle));
    painter.setBrush(fill);
    painter.drawPolygon(polygon);

    if (cross)
    {
        painter.drawLine(polygon[0], polygon[2]);
        painter.drawLine(polygon[1], polygon[3]);
    }

    if (cornerMark)
    {
        // Small triangle in the top-left corner marks a manual correction
        const QPointF origin = polygon[3];
        const QPointF toRight = (polygon[2] - polygon[3]);
        const QPointF toBottom = (polygon[0] - polygon[3]);
        const double size = qMin(8.0, qMin(QLineF(QPointF(), toRight).length(), QLineF(QPointF(), toBottom).length()) * 0.5);
        if (size > 1.0)
        {
            const QPointF right = origin + toRight / QLineF(QPointF(), toRight).length() * size;
            const QPointF bottom = origin + toBottom / QLineF(QPointF(), toBottom).length() * size;
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF() << origin << right << bottom);
        }
    }

    if (word.overlapsExcludedRegion || word.hasExtremeScaling)
    {
        // Exclamation mark: the word must be reviewed before it can be written
        painter.setPen(QPen(QColor(200, 0, 0), 2.0));
        const QPointF center = polygon.boundingRect().topRight();
        painter.drawLine(center + QPointF(3, 0), center + QPointF(3, 7));
        painter.drawPoint(center + QPointF(3, 10));
    }
}

QRectF PDFOCRPageView::getEditedRectangleInView() const
{
    if (!m_result)
    {
        return QRectF();
    }

    if (m_mode == Mode::EditWordGeometry)
    {
        if (const pdf::PDFOCRWord* word = m_result->findWord(m_selectedWordId))
        {
            return getPageToView().map(word->quad.toPolygon()).boundingRect();
        }
        return QRectF();
    }

    if (m_mode == Mode::Select && m_regionsVisible)
    {
        if (const pdf::PDFOCRRegion* region = m_result->findRegion(m_selectedRegionId))
        {
            return getPageToView().mapRect(region->rect);
        }
    }

    return QRectF();
}

PDFOCRPageView::Handle PDFOCRPageView::getHandleAt(const QPointF& viewPoint, const QRectF& rect) const
{
    if (rect.isNull())
    {
        return Handle::None;
    }

    auto hit = [&viewPoint](const QPointF& corner)
    {
        return QRectF(corner - QPointF(HANDLE_SIZE, HANDLE_SIZE), QSizeF(2 * HANDLE_SIZE, 2 * HANDLE_SIZE)).contains(viewPoint);
    };

    if (hit(rect.topLeft()))
    {
        return Handle::TopLeft;
    }
    if (hit(rect.topRight()))
    {
        return Handle::TopRight;
    }
    if (hit(rect.bottomLeft()))
    {
        return Handle::BottomLeft;
    }
    if (hit(rect.bottomRight()))
    {
        return Handle::BottomRight;
    }
    if (rect.contains(viewPoint))
    {
        return Handle::Move;
    }
    return Handle::None;
}

QRectF PDFOCRPageView::applyHandle(const QRectF& rectangle, Handle handle, const QPointF& delta) const
{
    QRectF result = rectangle;
    switch (handle)
    {
        case Handle::Move:
            result.translate(delta);
            break;
        case Handle::TopLeft:
            result.setTopLeft(rectangle.topLeft() + delta);
            break;
        case Handle::TopRight:
            result.setTopRight(rectangle.topRight() + delta);
            break;
        case Handle::BottomLeft:
            result.setBottomLeft(rectangle.bottomLeft() + delta);
            break;
        case Handle::BottomRight:
            result.setBottomRight(rectangle.bottomRight() + delta);
            break;
        case Handle::None:
            break;
    }
    return result.normalized();
}

const pdf::PDFOCRWord* PDFOCRPageView::getWordAt(const QPointF& viewPoint) const
{
    if (!m_result || !m_overlayVisible)
    {
        return nullptr;
    }

    const QTransform pageToView = getPageToView();
    const pdf::PDFOCRWord* best = nullptr;
    double bestArea = std::numeric_limits<double>::max();
    for (const pdf::PDFOCRWord* word : m_result->getWords())
    {
        const QPolygonF polygon = pageToView.map(word->quad.toPolygon());
        if (polygon.containsPoint(viewPoint, Qt::OddEvenFill))
        {
            const QRectF rect = polygon.boundingRect();
            const double area = rect.width() * rect.height();
            if (area < bestArea)
            {
                bestArea = area;
                best = word;
            }
        }
    }
    return best;
}

const pdf::PDFOCRRegion* PDFOCRPageView::getRegionAt(const QPointF& viewPoint) const
{
    if (!m_result || !m_regionsVisible)
    {
        return nullptr;
    }

    const QTransform pageToView = getPageToView();
    const pdf::PDFOCRRegion* best = nullptr;
    double bestArea = std::numeric_limits<double>::max();
    for (const pdf::PDFOCRRegion& region : m_result->regions)
    {
        const QRectF rect = pageToView.mapRect(region.rect);
        if (rect.contains(viewPoint) && rect.width() * rect.height() < bestArea)
        {
            bestArea = rect.width() * rect.height();
            best = &region;
        }
    }
    return best;
}

void PDFOCRPageView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().color(QPalette::Mid));

    if (m_image.isNull())
    {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(viewport()->rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    const QTransform imageToView = getImageToView();
    const QRectF target = imageToView.mapRect(QRectF(QPointF(0, 0), QSizeF(m_image.size())));

    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
    painter.drawImage(target, m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QTransform pageToView = getPageToView();

    if (m_result)
    {
        // Regions
        if (m_regionsVisible)
        {
            for (const pdf::PDFOCRRegion& region : m_result->regions)
            {
                const QRectF rect = pageToView.mapRect(region.rect);
                const bool selected = region.id == m_selectedRegionId;
                const bool exclude = region.type == pdf::PDFOCRRegionType::Exclude;
                const QColor color = exclude ? QColor(200, 30, 30) : QColor(20, 110, 200);
                QColor fill = color;
                fill.setAlpha(selected ? 50 : 22);
                painter.setPen(QPen(color, selected ? 2.5 : 1.5, exclude ? Qt::DashDotLine : Qt::SolidLine));
                painter.setBrush(QBrush(fill, exclude ? Qt::BDiagPattern : Qt::SolidPattern));
                painter.drawRect(rect);

                QString caption = region.name.isEmpty() ? (exclude ? tr("Excluded %1").arg(region.order) : tr("Region %1").arg(region.order)) : region.name;
                if (!region.configuration.isEmpty())
                {
                    caption += QStringLiteral(" *");
                }
                painter.setPen(color);
                painter.drawText(rect.adjusted(3, 2, -3, -2), Qt::AlignLeft | Qt::AlignTop, caption);
            }
        }

        // Words
        if (m_overlayVisible)
        {
            for (const pdf::PDFOCRBlock& block : m_result->blocks)
            {
                for (const pdf::PDFOCRLine& line : block.lines)
                {
                    if (line.id == m_selectedLineId && line.quad.isValid())
                    {
                        painter.setPen(QPen(QColor(30, 90, 220), 1.0, Qt::DashLine));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawPolygon(pageToView.map(line.quad.toPolygon()));
                    }

                    for (const pdf::PDFOCRWord& word : line.words)
                    {
                        drawWord(painter, word, pageToView, word.id == m_selectedWordId);
                    }
                }
            }
        }
    }

    // Edited rectangle with the handles
    QRectF edited = getEditedRectangleInView();
    if (m_dragging && m_activeHandle != Handle::None)
    {
        edited = applyHandle(m_dragOriginalRectangle, m_activeHandle, m_dragCurrent - m_dragStart);
    }

    if (!edited.isNull())
    {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(edited);
        painter.setBrush(palette().color(QPalette::Highlight));
        for (const QPointF& corner : { edited.topLeft(), edited.topRight(), edited.bottomLeft(), edited.bottomRight() })
        {
            painter.drawRect(QRectF(corner - QPointF(3.5, 3.5), QSizeF(7, 7)));
        }
    }

    // Rubber band of the drawn rectangle
    if (m_dragging && m_activeHandle == Handle::None && m_mode != Mode::Select && m_mode != Mode::EditWordGeometry)
    {
        const QRectF rubberBand = QRectF(m_dragStart, m_dragCurrent).normalized();
        const QColor color = m_mode == Mode::DrawExcludeRegion ? QColor(200, 30, 30) : QColor(20, 110, 200);
        QColor fill = color;
        fill.setAlpha(40);
        painter.setPen(QPen(color, 1.5, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(rubberBand);
    }

    if (!m_caption.isEmpty())
    {
        const QFontMetrics metrics(font());
        const QRect captionRect(4, 4, metrics.horizontalAdvance(m_caption) + 12, metrics.height() + 6);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 150));
        painter.drawRoundedRect(captionRect, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(captionRect, Qt::AlignCenter, m_caption);
    }
}

void PDFOCRPageView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);

    if (m_fitMode)
    {
        zoomFit();
    }
    else
    {
        updateScrollBars();
    }
}

void PDFOCRPageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_image.isNull())
    {
        BaseClass::mousePressEvent(event);
        return;
    }

    setFocus();
    const QPointF point = event->position();
    m_dragStart = point;
    m_dragCurrent = point;

    if (m_mode == Mode::Select || m_mode == Mode::EditWordGeometry)
    {
        const QRectF edited = getEditedRectangleInView();
        const Handle handle = getHandleAt(point, edited);

        if (handle != Handle::None && (m_mode == Mode::EditWordGeometry || handle != Handle::Move || !getWordAt(point)))
        {
            m_dragging = true;
            m_activeHandle = handle;
            m_dragOriginalRectangle = edited;
            return;
        }

        if (m_mode == Mode::Select)
        {
            if (const pdf::PDFOCRWord* word = getWordAt(point))
            {
                Q_EMIT wordClicked(word->id);
                return;
            }

            const pdf::PDFOCRRegion* region = getRegionAt(point);
            m_selectedRegionId = region ? region->id : 0;
            Q_EMIT regionClicked(m_selectedRegionId);
            viewport()->update();
        }
        return;
    }

    // Drawing mode
    m_dragging = true;
    m_activeHandle = Handle::None;
}

void PDFOCRPageView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging)
    {
        m_dragCurrent = event->position();
        viewport()->update();
        return;
    }

    if (m_mode == Mode::Select || m_mode == Mode::EditWordGeometry)
    {
        switch (getHandleAt(event->position(), getEditedRectangleInView()))
        {
            case Handle::TopLeft:
            case Handle::BottomRight:
                viewport()->setCursor(Qt::SizeFDiagCursor);
                break;
            case Handle::TopRight:
            case Handle::BottomLeft:
                viewport()->setCursor(Qt::SizeBDiagCursor);
                break;
            case Handle::Move:
                viewport()->setCursor(m_mode == Mode::EditWordGeometry ? Qt::SizeAllCursor : Qt::ArrowCursor);
                break;
            case Handle::None:
                viewport()->setCursor(Qt::ArrowCursor);
                break;
        }
    }

    BaseClass::mouseMoveEvent(event);
}

void PDFOCRPageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
    {
        BaseClass::mouseReleaseEvent(event);
        return;
    }

    m_dragging = false;
    m_dragCurrent = event->position();

    const QTransform viewToPage = getPageToView().inverted();
    const QTransform imageToPage = m_pageToImage.inverted();

    if (m_activeHandle != Handle::None)
    {
        const QRectF viewRectangle = applyHandle(m_dragOriginalRectangle, m_activeHandle, m_dragCurrent - m_dragStart);
        m_activeHandle = Handle::None;

        if (viewRectangle.width() >= 3.0 && viewRectangle.height() >= 3.0 && (m_dragCurrent - m_dragStart).manhattanLength() >= 2.0)
        {
            if (m_mode == Mode::EditWordGeometry)
            {
                const QRectF imageRectangle = getImageToView().inverted().mapRect(viewRectangle);
                Q_EMIT wordQuadChanged(m_selectedWordId, pdf::PDFOCRPagePreparer::imageRectToPageQuad(imageRectangle, imageToPage));
            }
            else
            {
                Q_EMIT regionGeometryChanged(m_selectedRegionId, viewToPage.mapRect(viewRectangle));
            }
        }

        viewport()->update();
        return;
    }

    const QRectF viewRectangle = QRectF(m_dragStart, m_dragCurrent).normalized();
    if (viewRectangle.width() >= 4.0 && viewRectangle.height() >= 4.0)
    {
        const QRectF imageRectangle = getImageToView().inverted().mapRect(viewRectangle);
        Q_EMIT rectangleDrawn(int(m_mode), viewToPage.mapRect(viewRectangle), pdf::PDFOCRPagePreparer::imageRectToPageQuad(imageRectangle, imageToPage));
    }

    Q_EMIT modeFinished();
    viewport()->update();
}

void PDFOCRPageView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        if (event->angleDelta().y() > 0)
        {
            zoomIn();
        }
        else if (event->angleDelta().y() < 0)
        {
            zoomOut();
        }
        event->accept();
        return;
    }

    BaseClass::wheelEvent(event);
}

void PDFOCRPageView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomIn();
            event->accept();
            return;

        case Qt::Key_Minus:
            zoomOut();
            event->accept();
            return;

        case Qt::Key_0:
            zoomFit();
            event->accept();
            return;

        case Qt::Key_Escape:
            if (m_mode != Mode::Select)
            {
                setMode(Mode::Select);
                Q_EMIT modeFinished();
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    BaseClass::keyPressEvent(event);
}

bool PDFOCRPageView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(event);
        if (const pdf::PDFOCRWord* word = getWordAt(helpEvent->pos()))
        {
            QString confidence = tr("confidence not available");
            if (word->confidence.isAvailable())
            {
                confidence = tr("confidence %1/100").arg(qRound(word->confidence.normalized.value()));
            }
            QToolTip::showText(helpEvent->globalPos(), QStringLiteral("%1\n%2").arg(word->text, confidence), viewport());
        }
        else
        {
            QToolTip::hideText();
        }
        return true;
    }

    return BaseClass::viewportEvent(event);
}

}   // namespace pdfviewer
