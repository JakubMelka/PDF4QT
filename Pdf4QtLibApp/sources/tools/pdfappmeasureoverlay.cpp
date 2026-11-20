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

#include "pdfappmeasureoverlay.h"
#include "pdfapplocale.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumenttool_measure.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfpainterutils.h"
#include "pdfthememanager.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetsnapshot.h"
#include "pdfwidgetutils.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <cmath>

namespace pdf
{

// ------------------------------------------------------------------ PDFAppMeasureText

PDFMeasureKind PDFAppMeasureText::getKind(PDFAppMeasureDrawKind drawKind)
{
    switch (drawKind)
    {
        case PDFAppMeasureDrawKind::Perimeter:
        case PDFAppMeasureDrawKind::RectanglePerimeter:
            return PDFMeasureKind::Perimeter;
        case PDFAppMeasureDrawKind::Area:
        case PDFAppMeasureDrawKind::RectangleArea:
            return PDFMeasureKind::Area;
        case PDFAppMeasureDrawKind::Angle:
            return PDFMeasureKind::Angle;
        case PDFAppMeasureDrawKind::Horizontal:
        case PDFAppMeasureDrawKind::Vertical:
        case PDFAppMeasureDrawKind::Linear:
        case PDFAppMeasureDrawKind::Calibrate:
            break;
    }
    return PDFMeasureKind::Distance;
}

QString PDFAppMeasureText::getDrawKindId(PDFAppMeasureDrawKind drawKind)
{
    switch (drawKind)
    {
        case PDFAppMeasureDrawKind::Horizontal:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_HORIZONTAL);
        case PDFAppMeasureDrawKind::Vertical:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_VERTICAL);
        case PDFAppMeasureDrawKind::Linear:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_LINEAR);
        case PDFAppMeasureDrawKind::Perimeter:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_PERIMETER);
        case PDFAppMeasureDrawKind::RectanglePerimeter:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_RECTANGLE_PERIMETER);
        case PDFAppMeasureDrawKind::Area:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_AREA);
        case PDFAppMeasureDrawKind::RectangleArea:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_RECTANGLE_AREA);
        case PDFAppMeasureDrawKind::Angle:
            return QString::fromLatin1(PDFDocumentToolMeasure::DRAW_ANGLE);
        case PDFAppMeasureDrawKind::Calibrate:
            break;
    }
    return QString();
}

PDFAppMeasureDrawKind PDFAppMeasureText::getDrawKind(const QString& id)
{
    for (const PDFAppMeasureDrawKind kind : { PDFAppMeasureDrawKind::Horizontal, PDFAppMeasureDrawKind::Vertical, PDFAppMeasureDrawKind::Linear, PDFAppMeasureDrawKind::Perimeter,
                                              PDFAppMeasureDrawKind::RectanglePerimeter, PDFAppMeasureDrawKind::Area, PDFAppMeasureDrawKind::RectangleArea, PDFAppMeasureDrawKind::Angle })
    {
        if (getDrawKindId(kind) == id)
        {
            return kind;
        }
    }
    return PDFAppMeasureDrawKind::Linear;
}

QString PDFAppMeasureText::getDrawKindTitle(PDFAppMeasureDrawKind drawKind)
{
    switch (drawKind)
    {
        case PDFAppMeasureDrawKind::Horizontal:
            return tr("Horizontal");
        case PDFAppMeasureDrawKind::Vertical:
            return tr("Vertical");
        case PDFAppMeasureDrawKind::Linear:
            return tr("Linear");
        case PDFAppMeasureDrawKind::Perimeter:
            return tr("Perimeter");
        case PDFAppMeasureDrawKind::RectanglePerimeter:
            return tr("Rectangle Perimeter");
        case PDFAppMeasureDrawKind::Area:
            return tr("Area");
        case PDFAppMeasureDrawKind::RectangleArea:
            return tr("Rectangle Area");
        case PDFAppMeasureDrawKind::Angle:
            return tr("Angle");
        case PDFAppMeasureDrawKind::Calibrate:
            return tr("Calibrate");
    }
    return QString();
}

QString PDFAppMeasureText::getDrawKindIconName(PDFAppMeasureDrawKind drawKind)
{
    // The icons of the kinds of 471:6294
    switch (drawKind)
    {
        case PDFAppMeasureDrawKind::Horizontal:
            return QStringLiteral("measure-h");
        case PDFAppMeasureDrawKind::Vertical:
            return QStringLiteral("measure-v");
        case PDFAppMeasureDrawKind::Linear:
            return QStringLiteral("measure-line");
        case PDFAppMeasureDrawKind::Perimeter:
            return QStringLiteral("perimeter");
        case PDFAppMeasureDrawKind::RectanglePerimeter:
            return QStringLiteral("rect-perimeter");
        case PDFAppMeasureDrawKind::Area:
            return QStringLiteral("area");
        case PDFAppMeasureDrawKind::RectangleArea:
            return QStringLiteral("rect-area");
        case PDFAppMeasureDrawKind::Angle:
            return QStringLiteral("angle");
        case PDFAppMeasureDrawKind::Calibrate:
            return QStringLiteral("calibrate");
    }
    return QString();
}

QString PDFAppMeasureText::getKindIconName(PDFMeasureKind kind)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
            return getDrawKindIconName(PDFAppMeasureDrawKind::Linear);
        case PDFMeasureKind::Perimeter:
            return getDrawKindIconName(PDFAppMeasureDrawKind::Perimeter);
        case PDFMeasureKind::Area:
            return getDrawKindIconName(PDFAppMeasureDrawKind::Area);
        case PDFMeasureKind::Angle:
            return getDrawKindIconName(PDFAppMeasureDrawKind::Angle);
    }
    return QString();
}

QString PDFAppMeasureText::formatValue(PDFReal value)
{
    if (!std::isfinite(value))
    {
        return QString();
    }
    return PDFAppLocale::getUiLocale().toString(value, 'f', PDFMeasureGeometry::VALUE_DECIMALS);
}

QString PDFAppMeasureText::formatTrimmed(PDFReal value, int decimals)
{
    if (!std::isfinite(value))
    {
        return QString();
    }

    // The value of the C locale without its trailing zeros, written by the locale of the user interface
    const QString cText = PDFMeasureUnits::formatNumber(value, decimals);
    const qsizetype dot = cText.indexOf(QLatin1Char('.'));
    return PDFAppLocale::getUiLocale().toString(cText.toDouble(), 'f', dot < 0 ? 0 : int(cText.size() - dot - 1));
}

QString PDFAppMeasureText::formatScaleNumber(PDFReal value)
{
    return formatTrimmed(value, PDFMeasureScale::VALUE_DECIMALS);
}

QString PDFAppMeasureText::formatRatio(const PDFMeasureScale& scale)
{
    if (!scale.isValid())
    {
        return QString();
    }
    return tr("%1 %2 = %3 %4", "a scale: 1 mm = 50 mm").arg(formatScaleNumber(scale.getPaperValue()),
                                                            PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, scale.getPaperUnitId()).getSymbol(),
                                                            formatScaleNumber(scale.getRealValue()),
                                                            PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, scale.getRealUnitId()).getSymbol());
}

QString PDFAppMeasureText::formatProportion(const PDFMeasureScale& scale)
{
    const std::pair<PDFReal, PDFReal> proportion = scale.getProportion();
    if (proportion.first <= 0.0)
    {
        return QString();
    }

    // "1:50" (the rows of the list 471:5975), the numbers of the locale with at most 2 places
    return tr("%1:%2", "a proportion of a scale: 1:50").arg(formatTrimmed(proportion.first, PDFMeasureScale::PROPORTION_DECIMALS),
                                                            formatTrimmed(proportion.second, PDFMeasureScale::PROPORTION_DECIMALS));
}

PDFReal PDFAppMeasureText::getUserUnit(const PDFDocument* document, PDFInteger pageIndex)
{
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return 1.0;
    }
    return catalog->getPage(size_t(pageIndex))->getEffectiveUserUnit();
}

PDFMeasureUnit PDFAppMeasureText::getUnit(PDFMeasureKind kind, const PDFMeasureDisplayUnits& units)
{
    const PDFMeasureQuantity quantity = PDFMeasureGeometry::getQuantity(kind);
    switch (quantity)
    {
        case PDFMeasureQuantity::Length:
            return PDFMeasureUnits::findUnitOrDefault(quantity, units.length);
        case PDFMeasureQuantity::Area:
            return PDFMeasureUnits::findUnitOrDefault(quantity, units.area);
        case PDFMeasureQuantity::Angle:
            return PDFMeasureUnits::findUnitOrDefault(quantity, units.angle);
    }
    return PDFMeasureUnit();
}

PDFReal PDFAppMeasureText::getValue(const PDFDocument* document, PDFInteger pageIndex, PDFMeasureKind kind, const std::vector<QPointF>& points, const PDFMeasureScale& scale,
                                    const PDFMeasureDisplayUnits& units, PDFMeasureScale* effectiveScale, QString* viewportName)
{
    // The one rule of the core: a viewport of the first point gives the scale (12.9.2), else the scale chosen
    PDFMeasureItem item;
    item.pageIndex = pageIndex;
    item.kind = kind;
    item.points = points;
    item.scale = scale;
    const PDFMeasureScale usedScale = PDFDocumentToolMeasure::getEffectiveScale(document, item, viewportName);
    if (effectiveScale)
    {
        *effectiveScale = usedScale;
    }
    return PDFMeasureGeometry::convertValue(kind, PDFMeasureGeometry::getValue(kind, points, getUserUnit(document, pageIndex)), usedScale, getUnit(kind, units));
}

QString PDFAppMeasureText::getText(const PDFDocument* document, PDFInteger pageIndex, PDFMeasureKind kind, const std::vector<QPointF>& points, const PDFMeasureScale& scale,
                                   const PDFMeasureDisplayUnits& units, PDFMeasureScale* effectiveScale)
{
    const PDFReal value = getValue(document, pageIndex, kind, points, scale, units, effectiveScale, nullptr);
    return PDFMeasureGeometry::getValueText(kind, formatValue(value), getUnit(kind, units).getSymbol());
}

// ------------------------------------------------------------------ PDFAppMeasureOverlay

/// The values of the drawing of the overlay
class PDFAppMeasureOverlayHelper
{
public:
    PDFAppMeasureOverlayHelper() = delete;

    /// Width of the lines of the measurements and of the outline of the selected one (device pixels)
    static constexpr qreal LINE_WIDTH = 1.5;
    static constexpr qreal SELECTED_WIDTH = 4.0;

    /// Half of the length of the ticks at the ends of a distance and the radius of the arc of an angle
    static constexpr qreal TICK_SIZE = 5.0;
    static constexpr qreal ARC_RADIUS = 18.0;

    /// Padding of the box of a label
    static constexpr qreal LABEL_PADDING = 3.0;

    /// The gap of a label beside its line (471:6681: a horizontal line 2 below its label, a vertical one 8 left of
    /// it; 471:5731: the line being drawn 6 above its label) and of the live label of a shape above its first edge (8)
    static constexpr qreal LABEL_GAP_ABOVE = 2.0;
    static constexpr qreal LABEL_GAP_RIGHT = 8.0;
    static constexpr qreal LABEL_GAP_BELOW = 6.0;
    static constexpr qreal SHAPE_LABEL_GAP = 8.0;

    /// The marks of the snapping (471:5731: the snapped point a square of 10; 471:6681: the first point of a shape,
    /// which closes it, a ring of 14)
    static constexpr qreal SNAP_MARK_SIZE = 10.0;
    static constexpr qreal CLOSE_MARK_SIZE = 14.0;

    /// A line is horizontal / vertical on the screen (a horizontal or vertical measurement, also on a rotated page)
    static constexpr qreal AXIS_TOLERANCE = 0.5;

    /// Most points of a shape (PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT)
    static constexpr int MAXIMUM_POINTS = PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT;

    /// The centroid of the corners (a closed shape repeats its first point)
    static QPointF getCenter(const std::vector<QPointF>& points)
    {
        const size_t count = (points.size() > 1 && points.front() == points.back()) ? points.size() - 1 : points.size();
        QPointF center(0.0, 0.0);
        for (size_t i = 0; i < count; ++i)
        {
            center += points[i];
        }
        return count > 0 ? center / qreal(count) : center;
    }
};

PDFAppMeasureOverlay::PDFAppMeasureOverlay(PDFWidget* pdfWidget, QWidget* canvasArea) :
    BaseClass(canvasArea),
    m_pdfWidget(pdfWidget),
    m_canvasArea(canvasArea),
    m_proxy(pdfWidget ? pdfWidget->getDrawWidgetProxy() : nullptr),
    m_drawWidget(pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr)
{
    setObjectName(QStringLiteral("measureOverlay"));

    // No area of its own: the mouse goes through to the canvas (the PDF widget gives its input to the
    // input interface below)
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(0, 0);

    if (m_drawWidget)
    {
        m_snapper.setSnapPointPixelSize(PDFWidgetUtils::scaleDPI_x(m_drawWidget.data(), 10));
        m_snapper.setSnapPointTolerance(m_snapper.getSnapPointPixelSize());
    }
    if (m_proxy)
    {
        m_proxy->registerDrawInterface(this);
        connect(m_proxy.data(), &PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFAppMeasureOverlay::buildSnapData);
        connect(m_proxy.data(), &PDFDrawWidgetProxy::pageImageChanged, this, &PDFAppMeasureOverlay::buildSnapData);
    }
    if (m_pdfWidget)
    {
        m_pdfWidget->addInputInterface(this);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppMeasureOverlay::updateCanvas);
    m_cursor = QCursor(Qt::CrossCursor);

    // 471:5490 callout-blue over the canvas: it takes no mouse (the clicks go to the page under it) and no focus
    if (canvasArea)
    {
        PDFAppToolCallout* callout = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString(), canvasArea);
        callout->setObjectName(QStringLiteral("measureCanvasCallout"));
        callout->setCompactStyle();
        callout->setAttribute(Qt::WA_TransparentForMouseEvents);
        callout->setFocusPolicy(Qt::NoFocus);
        callout->hide();
        m_callout = callout;
        canvasArea->installEventFilter(this);
    }
}

PDFAppMeasureOverlay::~PDFAppMeasureOverlay()
{
    if (m_canvasArea)
    {
        m_canvasArea->removeEventFilter(this);
    }
    // The callout is a child of the canvas area, which outlives the tool
    delete m_callout.data();

    if (m_pdfWidget)
    {
        m_pdfWidget->removeInputInterface(this);
    }
    if (m_proxy)
    {
        m_proxy->unregisterDrawInterface(this);
    }
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

void PDFAppMeasureOverlay::setInput(std::optional<PDFAppMeasureDrawKind> input)
{
    if (m_input == input)
    {
        return;
    }

    m_input = input;
    m_points.clear();
    m_page = -1;
    m_cursor = input ? std::optional<QCursor>(QCursor(Qt::CrossCursor)) : std::nullopt;
    buildSnapData();
    updateCanvas();
    updateCallout();
}

void PDFAppMeasureOverlay::setCallout(const QString& title, const QString& text)
{
    m_calloutTitle = title;
    if (m_callout)
    {
        m_callout->setTexts(title, text);
    }
    updateCallout();
}

void PDFAppMeasureOverlay::updateCallout()
{
    PDFAppToolCallout* callout = m_callout.data();
    const QWidget* canvasArea = m_canvasArea.data();
    if (!callout || !canvasArea)
    {
        return;
    }

    // 471:5490 at 20, 20 of the canvas, as wide as the canvas less its margins; 471:5731 has none after the first point
    // (nor while a finished measurement waits for its emission - the receiver sets the callout then)
    const bool isShown = !m_calloutTitle.isEmpty() && m_points.empty() && !m_isPickQueued;
    if (isShown)
    {
        const int width = qMax(0, canvasArea->width() - 2 * CALLOUT_MARGIN);
        const int height = callout->hasHeightForWidth() ? callout->heightForWidth(width) : callout->sizeHint().height();
        callout->setGeometry(CALLOUT_MARGIN, CALLOUT_MARGIN, width, qMax(height, callout->minimumSizeHint().height()));
        callout->raise();
    }
    callout->setVisible(isShown);
}

bool PDFAppMeasureOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvasArea && event->type() == QEvent::Resize)
    {
        updateCallout();
    }
    return BaseClass::eventFilter(watched, event);
}

QPointF PDFAppMeasureOverlay::clampToPage(PDFInteger pageIndex, const QPointF& pagePoint) const
{
    const PDFDocument* document = m_proxy ? m_proxy->getDocument() : nullptr;
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return pagePoint;
    }

    // Review RS7CCORE L4: the measurement lies on its page (ISO 32000-2, 14.11.2: the MediaBox is the physical medium)
    const QRectF mediaBox = catalog->getPage(size_t(pageIndex))->getMediaBox().normalized();
    if (!mediaBox.isValid())
    {
        return pagePoint;
    }
    return QPointF(qBound(mediaBox.left(), pagePoint.x(), mediaBox.right()), qBound(mediaBox.top(), pagePoint.y(), mediaBox.bottom()));
}

void PDFAppMeasureOverlay::setMeasureSettings(const PDFMeasureScale& scale, const PDFMeasureDisplayUnits& units)
{
    m_scale = scale.isValid() ? scale : PDFMeasureScale::createIdentity();
    m_units = units;
    updateCanvas();
}

void PDFAppMeasureOverlay::setShapes(std::vector<PDFAppMeasureShape> shapes)
{
    m_shapes = std::move(shapes);
    updateCanvas();
}

void PDFAppMeasureOverlay::setShown(bool isShown)
{
    m_isShown = isShown;
    updateCanvas();
}

void PDFAppMeasureOverlay::setCrossShown(bool isCrossShown)
{
    m_isCrossShown = isCrossShown;
    updateCanvas();
}

void PDFAppMeasureOverlay::setLabelStyle(const PDFAppMeasureLabelStyle& style)
{
    m_style = style;
    updateCanvas();
}

QString PDFAppMeasureOverlay::getPreviewText() const
{
    if (!m_input || m_page < 0 || !m_proxy)
    {
        return QString();
    }

    const std::vector<QPointF> points = getPreviewPoints();
    const PDFMeasureKind kind = PDFAppMeasureText::getKind(*m_input);
    const PDFDocument* document = m_proxy->getDocument();

    if (*m_input == PDFAppMeasureDrawKind::Calibrate)
    {
        // 471:7363 "Picked distance: 224.0 mm on the paper"
        if (points.size() < 2)
        {
            return QString();
        }
        const PDFReal length = PDFMeasureGeometry::getValue(PDFMeasureKind::Distance, points, PDFAppMeasureText::getUserUnit(document, m_page));
        return tr("Picked distance: %1 mm on the paper").arg(PDFAppLocale::getUiLocale().toString(length * PDF_POINT_TO_MM, 'f', 1));
    }

    switch (kind)
    {
        case PDFMeasureKind::Distance:
            return points.size() >= 2 ? PDFAppMeasureText::getText(document, m_page, kind, { points.front(), points.back() }, m_scale, m_units, nullptr) : QString();

        case PDFMeasureKind::Perimeter:
        {
            if (const std::optional<std::vector<QPointF>> normalized = PDFMeasureGeometry::normalizePoints(kind, points, PDFAppMeasureOverlayHelper::MAXIMUM_POINTS))
            {
                return PDFAppMeasureText::getText(document, m_page, kind, *normalized, m_scale, m_units, nullptr);
            }
            return points.size() >= 2 ? PDFAppMeasureText::getText(document, m_page, PDFMeasureKind::Distance, points, m_scale, m_units, nullptr) : QString();
        }

        case PDFMeasureKind::Area:
        {
            const std::optional<std::vector<QPointF>> normalized = PDFMeasureGeometry::normalizePoints(kind, points, PDFAppMeasureOverlayHelper::MAXIMUM_POINTS);
            return normalized ? PDFAppMeasureText::getText(document, m_page, kind, *normalized, m_scale, m_units, nullptr) : QString();
        }

        case PDFMeasureKind::Angle:
        {
            const std::optional<std::vector<QPointF>> normalized = PDFMeasureGeometry::normalizePoints(kind, points, PDFAppMeasureOverlayHelper::MAXIMUM_POINTS);
            return normalized ? PDFAppMeasureText::getText(document, m_page, kind, *normalized, m_scale, m_units, nullptr) : QString();
        }
    }
    return QString();
}

QString PDFAppMeasureOverlay::getPreviewLabelText() const
{
    const QString value = getPreviewText();
    if (value.isEmpty() || !m_input || (*m_input != PDFAppMeasureDrawKind::Perimeter && *m_input != PDFAppMeasureDrawKind::Area))
    {
        return value;
    }

    // 471:6681 "Click the first point to close · A = 22.25 sq. m": a shape of at least 3 corners can be closed
    const PDFMeasureKind kind = PDFAppMeasureText::getKind(*m_input);
    if (!PDFMeasureGeometry::normalizePoints(kind, getPreviewPoints(), PDFAppMeasureOverlayHelper::MAXIMUM_POINTS))
    {
        return value;
    }
    return tr("Click the first point to close · %1").arg(value);
}

QPointF PDFAppMeasureOverlay::adjustPoint(PDFInteger pageIndex, const QPointF& pagePoint) const
{
    if (!m_input || m_points.empty() || (*m_input != PDFAppMeasureDrawKind::Horizontal && *m_input != PDFAppMeasureDrawKind::Vertical))
    {
        return pagePoint;
    }

    const PDFDocument* document = m_proxy ? m_proxy->getDocument() : nullptr;
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return pagePoint;
    }

    // R7-12 (a): inside a viewport the axes of its /BBox (the axes of the page - a normalized rectangle of the
    // default user space), outside of it the axes as the page is shown (/Rotate 90 or 270 swaps them)
    const PDFPage* page = catalog->getPage(size_t(pageIndex));
    const QPointF first = m_points.front();
    const std::vector<PDFViewport> viewports = PDFViewport::parseViewports(&document->getStorage(), page->getViewports(&document->getStorage()));
    const bool isInViewport = PDFMeasureGeometry::findScaleViewport(viewports, first, page->getEffectiveUserUnit(), nullptr) != nullptr;
    const bool isRotated = !isInViewport && (page->getPageRotation() == PageRotation::Rotate90 || page->getPageRotation() == PageRotation::Rotate270);
    const bool isHorizontal = (*m_input == PDFAppMeasureDrawKind::Horizontal) != isRotated;

    QPointF result = pagePoint;
    if (isHorizontal)
    {
        result.setY(first.y());
    }
    else
    {
        result.setX(first.x());
    }
    return result;
}

std::vector<QPointF> PDFAppMeasureOverlay::getPreviewPoints() const
{
    std::vector<QPointF> points = m_points;
    if (!m_input || m_page < 0 || !m_hasMousePosition)
    {
        return points;
    }

    const std::optional<QPointF> cursorPoint = mapToPage(m_page, m_snapper.getSnappedPoint());
    if (!cursorPoint)
    {
        return points;
    }

    const QPointF point = adjustPoint(m_page, clampToPage(m_page, *cursorPoint));
    if ((*m_input == PDFAppMeasureDrawKind::RectanglePerimeter || *m_input == PDFAppMeasureDrawKind::RectangleArea) && points.size() == 1)
    {
        // Two opposite corners make the rectangle
        const QRectF rectangle = QRectF(points.front(), point).normalized();
        return { rectangle.topLeft(), rectangle.topRight(), rectangle.bottomRight(), rectangle.bottomLeft(), rectangle.topLeft() };
    }
    points.push_back(point);
    return points;
}

bool PDFAppMeasureOverlay::isComplete(const std::vector<QPointF>& points) const
{
    if (!m_input)
    {
        return false;
    }

    switch (*m_input)
    {
        case PDFAppMeasureDrawKind::RectanglePerimeter:
        case PDFAppMeasureDrawKind::RectangleArea:
            return points.size() == 5;
        case PDFAppMeasureDrawKind::Calibrate:
            return points.size() == 2;
        default:
            return PDFMeasureGeometry::isComplete(PDFAppMeasureText::getKind(*m_input), points);
    }
}

void PDFAppMeasureOverlay::addPoint(PDFInteger pageIndex, const QPointF& pagePoint)
{
    if (!m_input || pageIndex < 0 || !std::isfinite(pagePoint.x()) || !std::isfinite(pagePoint.y()))
    {
        return;
    }

    QPointF point = pagePoint;
    if (m_points.empty())
    {
        // The measurement lies on the page of its first point (12.9.2 NOTE 1, FM-9 cancelled: each
        // measurement on one page, the measurements on any pages)
        m_page = pageIndex;
        point = clampToPage(m_page, point);
    }
    else
    {
        // A click beyond the edge of the page is a point on its edge (the clamp first: the aligned coordinate of a
        // horizontal / vertical drawing is the one of the first point, which lies on the page)
        point = adjustPoint(m_page, clampToPage(m_page, point));
        if (point == m_points.back())
        {
            // A second click on the same point (a double click) adds nothing
            return;
        }
    }

    const bool isRectangle = *m_input == PDFAppMeasureDrawKind::RectanglePerimeter || *m_input == PDFAppMeasureDrawKind::RectangleArea;
    if (isRectangle && m_points.size() == 1)
    {
        const QRectF rectangle = QRectF(m_points.front(), point).normalized();
        m_points = { rectangle.topLeft(), rectangle.topRight(), rectangle.bottomRight(), rectangle.bottomLeft(), rectangle.topLeft() };
    }
    else
    {
        if (int(m_points.size()) > PDFAppMeasureOverlayHelper::MAXIMUM_POINTS)
        {
            return;
        }
        m_points.push_back(point);
    }

    // A shape is closed by its first point: the snapper offers it
    if ((*m_input == PDFAppMeasureDrawKind::Perimeter || *m_input == PDFAppMeasureDrawKind::Area) && !m_points.empty())
    {
        m_snapper.setCustomSnapPoints({ m_points.front() });
    }

    if (isComplete(m_points))
    {
        Pick pick;
        pick.pageIndex = m_page;
        pick.isCalibration = *m_input == PDFAppMeasureDrawKind::Calibrate;
        pick.kind = PDFAppMeasureText::getKind(*m_input);
        const std::optional<std::vector<QPointF>> normalized = PDFMeasureGeometry::normalizePoints(pick.kind, m_points, PDFAppMeasureOverlayHelper::MAXIMUM_POINTS);
        if (normalized || pick.isCalibration)
        {
            pick.isValid = true;
            pick.points = normalized ? *normalized : m_points;
        }

        m_points.clear();
        m_page = -1;
        m_snapper.setCustomSnapPoints({});
        m_snapper.clearReferencePoint();

        if (pick.isValid)
        {
            // The measurement is made from the event loop, never inside of the dispatch of the input
            m_pick = std::move(pick);
            if (!m_isPickQueued)
            {
                m_isPickQueued = true;
                QMetaObject::invokeMethod(this, &PDFAppMeasureOverlay::emitPick, Qt::QueuedConnection);
            }
        }
    }
    else
    {
        m_snapper.setReferencePoint(m_page, m_points.back());
    }
    updateCanvas();
    updateCallout();
}

void PDFAppMeasureOverlay::restart()
{
    const bool hadPoints = !m_points.empty();
    m_points.clear();
    m_page = -1;
    m_snapper.setCustomSnapPoints({});
    m_snapper.clearReferencePoint();
    updateCanvas();
    updateCallout();
    if (hadPoints)
    {
        // The last call (queued by the receiver)
        Q_EMIT drawingChanged();
    }
}

void PDFAppMeasureOverlay::emitPick()
{
    m_isPickQueued = false;
    Pick pick = std::move(m_pick);
    m_pick = Pick();
    if (!pick.isValid)
    {
        updateCallout();
        return;
    }

    if (pick.isCalibration)
    {
        const PDFDocument* document = m_proxy ? m_proxy->getDocument() : nullptr;
        const PDFReal length = PDFMeasureGeometry::getValue(PDFMeasureKind::Distance, pick.points, PDFAppMeasureText::getUserUnit(document, pick.pageIndex));
        Q_EMIT calibrationPicked(pick.pageIndex, length);
        return;
    }
    Q_EMIT measurementPicked(pick.pageIndex, pick.kind, std::move(pick.points));
}

void PDFAppMeasureOverlay::buildSnapData()
{
    if (!m_input || !m_proxy)
    {
        m_snapper.clear();
        return;
    }

    // The corners and edges of the drawing (the content of the pages) and the points of the annotations
    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    m_snapper.buildSnapPoints(snapshot);
    if (PDFWidgetAnnotationManager* annotationManager = m_proxy->getAnnotationManager())
    {
        for (const PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
        {
            m_snapper.addSnapInfo(item.pageIndex, item.pageToDeviceMatrix, annotationManager->getSnapInfo(item.pageIndex));
        }
    }
    if (m_hasMousePosition)
    {
        m_snapper.updateSnappedPoint(m_mousePosition);
    }
}

std::optional<QPointF> PDFAppMeasureOverlay::mapToPage(PDFInteger pageIndex, const QPointF& position) const
{
    if (!m_proxy)
    {
        return std::nullopt;
    }

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
    if (!item || !item->pageToDeviceMatrix.isInvertible())
    {
        return std::nullopt;
    }
    return item->pageToDeviceMatrix.inverted().map(position);
}

void PDFAppMeasureOverlay::drawPage(QPainter* painter,
                                    pdf::PDFInteger pageIndex,
                                    const PDFPrecompiledPage* compiledPage,
                                    PDFTextLayoutGetter& layoutGetter,
                                    const QTransform& pagePointToDevicePointMatrix,
                                    const PDFColorConvertor& convertor,
                                    QList<PDFRenderError>& errors) const
{
    // Everything is drawn over the annotations of the view (drawPostRendering)
    Q_UNUSED(painter);
    Q_UNUSED(pageIndex);
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(pagePointToDevicePointMatrix);
    Q_UNUSED(convertor);
    Q_UNUSED(errors);
}

void PDFAppMeasureOverlay::drawPostRendering(QPainter* painter, QRect rect) const
{
    if (!m_proxy)
    {
        return;
    }

    const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& item : snapshot.items)
    {
        PDFPainterStateGuard guard(painter);
        painter->setClipRect(item.rect, Qt::IntersectClip);
        drawPageGraphics(painter, item.pageIndex, item.pageToDeviceMatrix);
    }

    if (m_input)
    {
        const PDFThemeManager* manager = PDFThemeManager::getInstance();
        PDFPainterStateGuard guard(painter);
        drawSnapMarks(painter);

        if (m_isCrossShown && m_hasMousePosition)
        {
            // The big cross (C, 471:5731): through the snapped point over the whole canvas
            const QPointF point = m_snapper.getSnappedPoint();
            painter->setPen(QPen(manager->getColor(PDFThemeColors::TextPrimary), 1.0));
            painter->drawLine(QPointF(rect.left(), point.y()), QPointF(rect.right(), point.y()));
            painter->drawLine(QPointF(point.x(), rect.top()), QPointF(point.x(), rect.bottom()));
        }
    }
}

std::vector<QRectF> PDFAppMeasureOverlay::getSnapMarks() const
{
    using H = PDFAppMeasureOverlayHelper;

    std::vector<QRectF> marks;
    if (!m_input)
    {
        return marks;
    }

    // 471:6681 first-point: the first corner of a shape, which closes it (a ring)
    const bool isShape = *m_input == PDFAppMeasureDrawKind::Perimeter || *m_input == PDFAppMeasureDrawKind::Area;
    if (isShape && m_points.size() >= 2 && m_proxy)
    {
        const PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
        if (const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(m_page))
        {
            QRectF ring(0.0, 0.0, H::CLOSE_MARK_SIZE, H::CLOSE_MARK_SIZE);
            ring.moveCenter(item->pageToDeviceMatrix.map(m_points.front()));
            marks.push_back(ring);
        }
    }

    // 471:5731 snap-point: the point, which the cursor snaps to (a square)
    if (m_hasMousePosition && m_snapper.isSnapped())
    {
        QRectF square(0.0, 0.0, H::SNAP_MARK_SIZE, H::SNAP_MARK_SIZE);
        square.moveCenter(m_snapper.getSnappedPoint());
        marks.push_back(square);
    }
    return marks;
}

void PDFAppMeasureOverlay::drawSnapMarks(QPainter* painter) const
{
    using H = PDFAppMeasureOverlayHelper;

    // Review RS7CFIGMA L2: only the snapped point is marked (471:5731 snap-point), never every snap point of the page
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(manager->getColor(PDFThemeColors::BorderFocus), 1.5));
    for (const QRectF& mark : getSnapMarks())
    {
        if (qFuzzyCompare(mark.width(), H::CLOSE_MARK_SIZE))
        {
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(mark);
        }
        else
        {
            painter->setBrush(manager->getColor(PDFThemeColors::BgSurface));
            painter->drawRect(mark);
        }
    }
}

void PDFAppMeasureOverlay::drawPageGraphics(QPainter* painter, PDFInteger pageIndex, const QTransform& matrix) const
{
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    for (const PDFAppMeasureShape& shape : m_shapes)
    {
        if (shape.pageIndex != pageIndex || (shape.isAnnotation && !shape.isSelected) || (!m_isShown && !shape.isSelected && !shape.isAnnotation))
        {
            continue;
        }
        drawShape(painter, shape, matrix, false);
    }

    // The measurement being drawn (471:5731: the accent line and the value in an accent box)
    if (m_input && m_page == pageIndex && !m_points.empty())
    {
        PDFAppMeasureShape preview;
        preview.pageIndex = pageIndex;
        preview.kind = PDFAppMeasureText::getKind(*m_input);
        preview.points = getPreviewPoints();
        preview.text = getPreviewLabelText();
        drawShape(painter, preview, matrix, true);
    }
}

void PDFAppMeasureOverlay::drawShape(QPainter* painter, const PDFAppMeasureShape& shape, const QTransform& matrix, bool isPreview) const
{
    using H = PDFAppMeasureOverlayHelper;

    if (shape.points.empty())
    {
        return;
    }

    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    QPolygonF polygon;
    for (const QPointF& point : shape.points)
    {
        polygon << matrix.map(point);
    }

    PDFPainterStateGuard guard(painter);
    if (shape.isSelected)
    {
        // FM-28: the selected row of the list is outlined on the page (border/focus)
        QPen outline(manager->getColor(PDFThemeColors::BorderFocus), H::SELECTED_WIDTH);
        outline.setJoinStyle(Qt::RoundJoin);
        outline.setCapStyle(Qt::RoundCap);
        painter->setPen(outline);
        painter->setBrush(Qt::NoBrush);
        if (shape.kind == PDFMeasureKind::Area)
        {
            painter->drawPolygon(polygon);
        }
        else
        {
            painter->drawPolyline(polygon);
        }
        if (shape.isAnnotation)
        {
            // The annotation itself is drawn by the view
            return;
        }
    }

    const QColor lineColor = isPreview ? manager->getColor(PDFThemeColors::BorderFocus) : m_style.textColor;
    QPen pen(lineColor, H::LINE_WIDTH);
    if (isPreview || shape.kind == PDFMeasureKind::Perimeter)
    {
        pen.setStyle(Qt::DashLine);
    }
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const qreal pixelsPerPoint = std::hypot(matrix.m11(), matrix.m12());
    switch (shape.kind)
    {
        case PDFMeasureKind::Distance:
        {
            painter->drawPolyline(polygon);
            if (polygon.size() >= 2)
            {
                const QLineF line(polygon.front(), polygon.back());
                if (!line.isNull())
                {
                    const QLineF normal = line.normalVector().unitVector();
                    const QPointF tick = (normal.p2() - normal.p1()) * H::TICK_SIZE;
                    painter->setPen(QPen(lineColor, H::LINE_WIDTH));
                    painter->drawLine(line.p1() - tick, line.p1() + tick);
                    painter->drawLine(line.p2() - tick, line.p2() + tick);
                }
            }
            break;
        }

        case PDFMeasureKind::Perimeter:
            painter->drawPolyline(polygon);
            break;

        case PDFMeasureKind::Area:
            if (polygon.size() >= 3)
            {
                painter->setBrush(isPreview ? manager->getColor(PDFThemeColors::BgAccentSubtle) : m_style.backgroundColor);
                painter->drawPolygon(polygon, Qt::OddEvenFill);
            }
            else
            {
                painter->drawPolyline(polygon);
            }
            break;

        case PDFMeasureKind::Angle:
        {
            painter->drawPolyline(polygon);
            if (polygon.size() == 3)
            {
                const QLineF arm1(polygon[1], polygon[0]);
                const QLineF arm2(polygon[1], polygon[2]);
                qreal sweep = arm1.angleTo(arm2);
                if (sweep > 180.0)
                {
                    sweep -= 360.0;
                }
                const qreal radius = qMin(H::ARC_RADIUS, qMin(arm1.length(), arm2.length()));
                const QRectF arc(polygon[1].x() - radius, polygon[1].y() - radius, 2.0 * radius, 2.0 * radius);
                painter->drawArc(arc, qRound(arm1.angle() * 16.0), qRound(sweep * 16.0));
            }
            break;
        }
    }

    // The ends of the points of a drawing (471:5731)
    if (isPreview)
    {
        painter->setPen(QPen(lineColor, 6.0, Qt::SolidLine, Qt::RoundCap));
        for (const QPointF& point : polygon)
        {
            painter->drawPoint(point);
        }
    }

    if (!shape.text.isEmpty())
    {
        drawLabel(painter, shape.text, getLabelRect(shape, matrix, isPreview).center(), pixelsPerPoint, isPreview);
    }
}

QRectF PDFAppMeasureOverlay::getLabelRect(const PDFAppMeasureShape& shape, const QTransform& matrix, bool isPreview) const
{
    using H = PDFAppMeasureOverlayHelper;

    QPolygonF polygon;
    for (const QPointF& point : shape.points)
    {
        polygon << matrix.map(point);
    }
    if (polygon.isEmpty() || shape.text.isEmpty())
    {
        return QRectF();
    }

    // A shape has its label on the centroid of its corners (471:5975)
    const qreal pixelsPerPoint = std::hypot(matrix.m11(), matrix.m12());
    QPointF position = H::getCenter(std::vector<QPointF>(polygon.cbegin(), polygon.cend()));
    QRectF box = getLabelBox(shape.text, position, pixelsPerPoint);
    const bool isShapeDrawing = isPreview && m_input && (*m_input == PDFAppMeasureDrawKind::Perimeter || *m_input == PDFAppMeasureDrawKind::Area);

    if (shape.kind == PDFMeasureKind::Distance && polygon.size() >= 2 && !isShapeDrawing)
    {
        // Review RS7CFIGMA L3: the label of a horizontal line lies beside it - above (471:6681), the one of the line
        // being drawn below (471:5731, 471:7363) - and of a vertical line at its right (471:6681); a slanted line has
        // its label on its middle (471:5975)
        const QLineF line(polygon.front(), polygon.back());
        position = line.center();
        if (qAbs(line.dy()) <= H::AXIS_TOLERANCE && qAbs(line.dx()) > H::AXIS_TOLERANCE)
        {
            position.ry() += isPreview ? H::LABEL_GAP_BELOW + box.height() * 0.5 : -(H::LABEL_GAP_ABOVE + box.height() * 0.5);
        }
        else if (qAbs(line.dx()) <= H::AXIS_TOLERANCE && qAbs(line.dy()) > H::AXIS_TOLERANCE)
        {
            position.rx() += H::LABEL_GAP_RIGHT + box.width() * 0.5;
        }
    }
    else if (isShapeDrawing && polygon.size() >= 2)
    {
        // Review RS7CFIGMA L4, 471:6681: the live label of a shape lies above the middle of its first edge
        const QLineF edge(polygon[0], polygon[1]);
        position = edge.center();
        position.ry() -= H::SHAPE_LABEL_GAP + box.height() * 0.5;
    }
    else if (shape.kind == PDFMeasureKind::Angle && polygon.size() == 3)
    {
        // On the bisector of the arc, beyond it
        const QLineF arm1(polygon[1], polygon[0]);
        const QLineF arm2(polygon[1], polygon[2]);
        qreal sweep = arm1.angleTo(arm2);
        if (sweep > 180.0)
        {
            sweep -= 360.0;
        }
        const qreal radius = qMin(H::ARC_RADIUS, qMin(arm1.length(), arm2.length()));
        QLineF bisector = arm1;
        bisector.setAngle(arm1.angle() + sweep * 0.5);
        bisector.setLength(radius + 16.0);
        position = bisector.p2();
    }

    box.moveCenter(position);
    return box;
}

QRectF PDFAppMeasureOverlay::getLabelBox(const QString& text, const QPointF& position, qreal pixelsPerPoint) const
{
    using H = PDFAppMeasureOverlayHelper;

    const QFontMetricsF metrics(getLabelFont(pixelsPerPoint));
    QRectF box(0.0, 0.0, metrics.horizontalAdvance(text) + 2.0 * H::LABEL_PADDING, metrics.height() + H::LABEL_PADDING);
    box.moveCenter(position);
    return box;
}

QFont PDFAppMeasureOverlay::getLabelFont(qreal pixelsPerPoint) const
{
    // The size is in points of the page at the zoom of the view (the matrix of the page), at least 6 pixels
    QFont font = m_style.font;
    const qreal pointSize = font.pointSizeF() > 0.0 ? font.pointSizeF() : 10.0;
    font.setPixelSize(qMax(6, qRound(pointSize * (std::isfinite(pixelsPerPoint) && pixelsPerPoint > 0.0 ? pixelsPerPoint : 1.0))));
    return font;
}

void PDFAppMeasureOverlay::drawLabel(QPainter* painter, const QString& text, const QPointF& position, qreal pixelsPerPoint, bool isAccent) const
{
    // The label of the panel Labels (its font, its colors); a label of the drawing is the accent box
    PDFPainterStateGuard guard(painter);
    painter->setFont(getLabelFont(pixelsPerPoint));
    const QRectF box = getLabelBox(text, position, pixelsPerPoint);

    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter->setPen(Qt::NoPen);
    painter->setBrush(isAccent ? manager->getColor(PDFThemeColors::BgAccent) : m_style.backgroundColor);
    painter->drawRoundedRect(box, 2.0, 2.0);
    painter->setPen(isAccent ? manager->getColor(PDFThemeColors::IconOnAccent) : m_style.textColor);
    painter->drawText(box, Qt::AlignCenter, text);
}

void PDFAppMeasureOverlay::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    // C toggles the big cross while measuring (the old pick tool); Esc stays the key of the shell
    if (m_input && event->key() == Qt::Key_C && event->modifiers() == Qt::NoModifier)
    {
        event->accept();
    }
}

void PDFAppMeasureOverlay::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (m_input && event->key() == Qt::Key_C && event->modifiers() == Qt::NoModifier)
    {
        m_isCrossShown = !m_isCrossShown;
        event->accept();
        updateCanvas();

        // The toolbar follows (queued by the receiver, the last call)
        Q_EMIT drawingChanged();
    }
}

void PDFAppMeasureOverlay::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFAppMeasureOverlay::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (!m_input || !m_proxy)
    {
        return;
    }

    if (event->button() == Qt::RightButton)
    {
        // The right button starts again (471:5490 "Right-click starts over")
        event->accept();
        restart();
        return;
    }

    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    m_mousePosition = event->position();
    m_hasMousePosition = true;
    m_snapper.updateSnappedPoint(m_mousePosition);
    const QPointF snappedPoint = m_snapper.getSnappedPoint();

    QPointF pagePoint;
    PDFInteger pageIndex = m_proxy->getPageUnderPoint(snappedPoint, &pagePoint);
    if (!m_points.empty())
    {
        // The next points belong to the page of the first one, also over its edge
        const std::optional<QPointF> point = mapToPage(m_page, snappedPoint);
        if (!point)
        {
            return;
        }
        pageIndex = m_page;
        pagePoint = *point;
    }
    if (pageIndex < 0)
    {
        return;
    }

    event->accept();
    addPoint(pageIndex, pagePoint);

    // The status text of the next point (queued by the receiver, the last call)
    Q_EMIT drawingChanged();
}

void PDFAppMeasureOverlay::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    // A double click while measuring is no selection of a word of the view
    if (m_input)
    {
        event->accept();
    }
}

void PDFAppMeasureOverlay::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (m_input && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton))
    {
        event->accept();
    }
}

void PDFAppMeasureOverlay::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (!m_input)
    {
        return;
    }

    m_mousePosition = event->position();
    m_hasMousePosition = true;
    m_snapper.updateSnappedPoint(m_mousePosition);
    updateCanvas();
    event->accept();

    if (!m_points.empty())
    {
        // The value of the status text follows the cursor (queued by the receiver, the last call)
        Q_EMIT drawingChanged();
    }
}

void PDFAppMeasureOverlay::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

QString PDFAppMeasureOverlay::getTooltip() const
{
    return QString();
}

const std::optional<QCursor>& PDFAppMeasureOverlay::getCursor() const
{
    return m_cursor;
}

int PDFAppMeasureOverlay::getInputPriority() const
{
    // Before the tools of the view (the selection of the text, the magnifier)
    return ToolPriority - 1;
}

void PDFAppMeasureOverlay::updateCanvas()
{
    if (m_drawWidget)
    {
        m_drawWidget->update();
    }
}

}   // namespace pdf

#include "moc_pdfappmeasureoverlay.cpp"
