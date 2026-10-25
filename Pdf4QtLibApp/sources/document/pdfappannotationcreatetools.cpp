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

#include "pdfappannotationcreatetools.h"
#include "pdfappannotationcreator.h"

#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfpainterutils.h"

#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <limits>

namespace pdf
{

/// Drawing of the tools of package A over the page
class PDFAppAnnotationCreateToolsHelper
{
public:
    PDFAppAnnotationCreateToolsHelper() = delete;

    /// Fill of a marked area (link area, text box area, target page): bg/accent at 6 %
    /// (1168:26449, 1169:27256, 1170:27194, 1170:27667)
    static constexpr int AREA_FILL_ALPHA = 15;

    /// Outline of a marked area: 1.5 inside of the area, dashes 4 / 3; the target page
    /// 2 outside of the page (1170:27667); the dashed outline of a filled text box
    /// 4 outside of its preview (1168:26649)
    static constexpr qreal AREA_OUTLINE_WIDTH = 1.5;
    static constexpr qreal TARGET_OUTLINE_WIDTH = 2.0;
    static constexpr qreal PREVIEW_OUTLINE_OUTSET = 4.0;
    static constexpr qreal DASH_LENGTH = 4.0;
    static constexpr qreal DASH_GAP = 3.0;

    /// Size of the tip of the target (padding 9 / 6, Regular 11)
    static constexpr int TIP_PADDING_X = 9;
    static constexpr int TIP_PADDING_Y = 6;
    static constexpr int TIP_OFFSET = 18;

    static QColor getAccent()
    {
        return PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgAccent);
    }

    /// Light fill of the accent of an area
    static void fillArea(QPainter* painter, const QRectF& deviceRectangle)
    {
        QColor fill = getAccent();
        fill.setAlpha(AREA_FILL_ALPHA);
        painter->fillRect(deviceRectangle, fill);
    }

    /// Outline of the accent of an area. The outer edge of the stroke lies `outset`
    /// outside of the area (0 = the stroke inside of the area, `width` = outside).
    static void strokeArea(QPainter* painter, const QRectF& deviceRectangle, qreal width, qreal outset, bool isDashed)
    {
        QPen pen(getAccent(), width);
        pen.setJoinStyle(Qt::MiterJoin);
        if (isDashed)
        {
            // The pattern of a pen is in the units of its width
            pen.setDashPattern({ DASH_LENGTH / width, DASH_GAP / width });
        }

        const qreal inset = width / 2.0 - outset;
        PDFPainterStateGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(deviceRectangle.adjusted(inset, inset, -inset, -inset));
    }

    /// The fill and the outline 1.5 inside of a marked area (dashed while it is drawn)
    static void drawArea(QPainter* painter, const QRectF& deviceRectangle, bool isDashed)
    {
        PDFPainterStateGuard guard(painter);
        fillArea(painter, deviceRectangle);
        strokeArea(painter, deviceRectangle, AREA_OUTLINE_WIDTH, 0.0, isDashed);
    }

    /// The rectangle a pick tool is dragging on the page, in the device space. The pick
    /// tools of the application do not draw it themselves (setDrawSelectionRectangle):
    /// the engine fills it by an opaque color, the design draws it as an area being
    /// drawn (1169:27256: the fill and the dashed outline).
    static void drawDraggedArea(QPainter* painter, const PDFPickTool* pickTool, PDFInteger pageIndex, const QTransform& pagePointToDevicePointMatrix)
    {
        if (!painter || !pickTool || !pickTool->isActive() || pickTool->getPageIndex() != pageIndex || pickTool->getPickedPoints().empty())
        {
            return;
        }

        const QRectF rectangle = QRectF(pagePointToDevicePointMatrix.map(pickTool->getPickedPoints().back()), pickTool->getSnappedPoint()).normalized();
        if (rectangle.width() > 0.0 && rectangle.height() > 0.0)
        {
            drawArea(painter, rectangle, true);
        }
    }

    /// Scale of the matrix (the length of a unit of the page in the device)
    static qreal getScale(const QTransform& matrix)
    {
        const qreal scale = std::sqrt(std::abs(matrix.determinant()));
        return std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
    }

    static const PDFPage* getPage(const PDFDocument* document, PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            return nullptr;
        }
        return catalog->getPage(size_t(pageIndex));
    }
};

// ------------------------------------------------------------ PDFAppTextBoxTool

PDFAppTextBoxTool::PDFAppTextBoxTool(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(proxy, parent)
{
    setObjectName(QStringLiteral("textBoxTool"));
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFAppTextBoxTool::onRectanglePicked);
}

PDFAppTextBoxTool::~PDFAppTextBoxTool() = default;

void PDFAppTextBoxTool::setArea(PDFInteger pageIndex, const QRectF& rectangle)
{
    const QRectF normalized = rectangle.normalized();
    if (!PDFAppAnnotationCreateToolsHelper::getPage(getDocument(), pageIndex) || !(normalized.width() > 0.0) || !(normalized.height() > 0.0))
    {
        return;
    }

    m_pageIndex = pageIndex;
    m_rectangle = normalized;
    m_previewText.clear();
    if (PDFDrawWidgetProxy* proxy = getProxy())
    {
        Q_EMIT proxy->repaintNeeded();
    }
}

void PDFAppTextBoxTool::clearArea()
{
    m_pageIndex = -1;
    m_rectangle = QRectF();
    m_previewText.clear();
    m_pickTool->resetTool();
    if (PDFDrawWidgetProxy* proxy = getProxy())
    {
        Q_EMIT proxy->repaintNeeded();
    }
}

void PDFAppTextBoxTool::setPreview(const QString& text, const PDFFreeTextStyle& style, const QColor& borderColor, PDFReal borderWidth)
{
    m_previewText = text.left(4096);
    m_previewStyle = style;
    m_previewBorderColor = borderColor;
    m_previewBorderWidth = std::isfinite(borderWidth) ? qBound(0.0, borderWidth, 100.0) : 0.0;
    if (hasArea())
    {
        if (PDFDrawWidgetProxy* proxy = getProxy())
        {
            Q_EMIT proxy->repaintNeeded();
        }
    }
}

void PDFAppTextBoxTool::drawPage(QPainter* painter,
                                 PDFInteger pageIndex,
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

    using Helper = PDFAppAnnotationCreateToolsHelper;
    Helper::drawDraggedArea(painter, m_pickTool, pageIndex, pagePointToDevicePointMatrix);
    if (pageIndex != m_pageIndex || !painter)
    {
        return;
    }

    const QRectF deviceRectangle = pagePointToDevicePointMatrix.mapRect(m_rectangle);
    const qreal scale = Helper::getScale(pagePointToDevicePointMatrix);

    // The border of the text box of the preview, then the text
    if (m_previewBorderColor.isValid() && m_previewBorderWidth > 0.0)
    {
        PDFPainterStateGuard guard(painter);
        painter->setPen(QPen(m_previewBorderColor, m_previewBorderWidth * scale));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(deviceRectangle);
    }

    if (!m_previewText.isEmpty())
    {
        PDFPainterStateGuard guard(painter);
        QFont font(m_previewStyle.fontFamily);
        const qreal fontSize = std::isfinite(m_previewStyle.fontSize) ? qBound(1.0, m_previewStyle.fontSize, 512.0) : 10.0;
        font.setPixelSize(qMax(1, qRound(fontSize * scale)));
        painter->setFont(font);
        painter->setPen(m_previewStyle.textColor.isValid() ? m_previewStyle.textColor : QColor(Qt::black));
        painter->setClipRect(deviceRectangle, Qt::IntersectClip);
        const int flags = int(m_previewStyle.textAlignment & Qt::AlignHorizontal_Mask) | Qt::AlignTop | Qt::TextWordWrap;
        const qreal padding = 2.0 * scale;
        painter->drawText(deviceRectangle.adjusted(padding, padding, -padding, -padding), flags, m_previewText);
    }

    if (m_previewText.isEmpty())
    {
        // The drawn area 1168:26449: the fill and the dashed outline inside
        Helper::drawArea(painter, deviceRectangle, true);
    }
    else
    {
        // The preview 1168:27129 with the dashed outline 4 px outside of it (1168:26649)
        Helper::strokeArea(painter, deviceRectangle, Helper::AREA_OUTLINE_WIDTH, Helper::PREVIEW_OUTLINE_OUTSET, true);
    }
}

void PDFAppTextBoxTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    // A drawn area waits for the popup (Add / Cancel), no new rectangle meanwhile
    if (hasArea())
    {
        event->accept();
        return;
    }
    BaseClass::mousePressEvent(widget, event);
}

void PDFAppTextBoxTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    if (hasArea())
    {
        event->accept();
        return;
    }
    BaseClass::mouseReleaseEvent(widget, event);
}

void PDFAppTextBoxTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    if (hasArea())
    {
        event->accept();
        return;
    }
    BaseClass::mouseMoveEvent(widget, event);
}

void PDFAppTextBoxTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);
    if (!active)
    {
        m_pageIndex = -1;
        m_rectangle = QRectF();
        m_previewText.clear();
        m_pickTool->resetTool();
    }
}

void PDFAppTextBoxTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    const QRectF normalized = pageRectangle.normalized();
    if (!(normalized.width() > 0.0) || !(normalized.height() > 0.0))
    {
        m_pickTool->resetTool();
        return;
    }

    setArea(pageIndex, normalized);
    if (hasArea())
    {
        Q_EMIT areaPicked(pageIndex, normalized);
    }
    else
    {
        m_pickTool->resetTool();
    }
}

// ----------------------------------------------------------- PDFAppPageLinkTool

PDFAppPageLinkTool::PDFAppPageLinkTool(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(proxy, parent)
{
    setObjectName(QStringLiteral("pageLinkTool"));

    // Step 1 draws the link area; step 2 picks a point or a rectangle of the target.
    // Only one pick tool is in the stack of the tool at a time.
    m_areaPickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_areaPickTool->setSnapToAnnotations(true);
    m_areaPickTool->setDrawSelectionRectangle(false);
    m_pointPickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_rectanglePickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_rectanglePickTool->setDrawSelectionRectangle(false);
    connect(m_areaPickTool, &PDFPickTool::rectanglePicked, this, &PDFAppPageLinkTool::onLinkRectanglePicked);
    connect(m_pointPickTool, &PDFPickTool::pointPicked, this, &PDFAppPageLinkTool::onTargetPointPicked);
    connect(m_rectanglePickTool, &PDFPickTool::rectanglePicked, this, &PDFAppPageLinkTool::onTargetRectanglePicked);

    m_currentPickTool = m_areaPickTool;
    addTool(m_currentPickTool);
}

PDFAppPageLinkTool::~PDFAppPageLinkTool() = default;

bool PDFAppPageLinkTool::isTarget(DestinationType type, bool targetOnly)
{
    if (PDFAppAnnotationTools::isPageLinkTarget(type))
    {
        return true;
    }
    return targetOnly && (type == DestinationType::FitB || type == DestinationType::FitBH || type == DestinationType::FitBV);
}

void PDFAppPageLinkTool::setTargetOnly(bool targetOnly)
{
    if (m_isTargetOnly == targetOnly)
    {
        return;
    }

    m_isTargetOnly = targetOnly;
    if (!targetOnly && !PDFAppAnnotationTools::isPageLinkTarget(m_destinationType))
    {
        m_destinationType = DestinationType::Fit;
    }
    goBack();
}

void PDFAppPageLinkTool::setDestinationType(DestinationType type)
{
    if (!isTarget(type, m_isTargetOnly) || type == m_destinationType)
    {
        return;
    }

    m_destinationType = type;
    updatePickTool();
    if (PDFDrawWidgetProxy* proxy = getProxy())
    {
        Q_EMIT proxy->repaintNeeded();
    }
}

void PDFAppPageLinkTool::setLinkArea(PDFInteger pageIndex, const QRectF& rectangle)
{
    onLinkRectanglePicked(pageIndex, rectangle);
}

void PDFAppPageLinkTool::pickTarget(PDFInteger pageIndex, const QPointF& point, const QRectF& rectangle)
{
    if (m_step != Step::Target)
    {
        return;
    }

    if (m_destinationType == DestinationType::FitR)
    {
        onTargetRectanglePicked(pageIndex, rectangle);
    }
    else
    {
        onTargetPointPicked(pageIndex, point);
    }
}

void PDFAppPageLinkTool::goBack()
{
    // The target-only mode has no link area: it stays in the step 2
    m_linkPageIndex = -1;
    m_linkRectangle = QRectF();
    setStep(m_isTargetOnly ? Step::Target : Step::LinkArea);
}

PDFDestination PDFAppPageLinkTool::createDestination(PDFInteger pageIndex, const QPointF& point, const QRectF& rectangle) const
{
    const PDFPage* page = PDFAppAnnotationCreateToolsHelper::getPage(getDocument(), pageIndex);
    if (!page)
    {
        return PDFDestination();
    }

    const PDFObjectReference reference = page->getPageReference();
    PDFDestination destination;
    switch (m_destinationType)
    {
        case DestinationType::Fit:
            destination = PDFDestination::createFit(reference);
            break;

        case DestinationType::FitH:
            // The page width from the clicked height
            destination = PDFDestination::createFitH(reference, point.y());
            break;

        case DestinationType::FitV:
            // The page height from the clicked edge
            destination = PDFDestination::createFitV(reference, point.x());
            break;

        case DestinationType::FitR:
        {
            const QRectF normalized = rectangle.normalized();
            if (!(normalized.width() > 0.0) || !(normalized.height() > 0.0))
            {
                return PDFDestination();
            }
            // QRectF of the page space: top() is the smaller y - the bottom of PDF
            destination = PDFDestination::createFitR(reference, normalized.left(), normalized.bottom(), normalized.right(), normalized.top());
            break;
        }

        case DestinationType::FitB:
            destination = PDFDestination::createFitB(reference);
            break;

        case DestinationType::FitBH:
            destination = PDFDestination::createFitBH(reference, point.y());
            break;

        case DestinationType::FitBV:
            destination = PDFDestination::createFitBV(reference, point.x());
            break;

        case DestinationType::XYZ:
        {
            const PDFDrawWidgetProxy* proxy = getProxy();
            const PDFReal zoom = (m_keepZoom || !proxy) ? std::numeric_limits<PDFReal>::quiet_NaN() : proxy->getZoom();
            destination = PDFDestination::createXYZ(reference, point.x(), point.y(), zoom);
            break;
        }

        default:
            return PDFDestination();
    }

    destination.setPageIndex(pageIndex);
    return destination;
}

void PDFAppPageLinkTool::drawPage(QPainter* painter,
                                  PDFInteger pageIndex,
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

    using Helper = PDFAppAnnotationCreateToolsHelper;
    if (!painter)
    {
        return;
    }

    // The area being drawn: the link area of the step 1, the rectangle of an Area target
    Helper::drawDraggedArea(painter, m_step == Step::Target ? m_rectanglePickTool : m_areaPickTool, pageIndex, pagePointToDevicePointMatrix);
    if (m_step != Step::Target)
    {
        return;
    }

    // The link area stays marked on its page during the step 2 (1170:27194, solid)
    if (pageIndex == m_linkPageIndex)
    {
        Helper::drawArea(painter, pagePointToDevicePointMatrix.mapRect(m_linkRectangle), false);
    }

    // The target under the pointer: the page, the height, the edge or the position
    if (pageIndex != m_hoverPageIndex)
    {
        return;
    }

    const PDFPage* page = Helper::getPage(getDocument(), pageIndex);
    if (!page)
    {
        return;
    }

    const QRectF pageRectangle = page->getCropBox().isValid() ? page->getCropBox() : page->getMediaBox();
    const QColor accent = Helper::getAccent();
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(accent, Helper::AREA_OUTLINE_WIDTH));
    switch (m_destinationType)
    {
        case DestinationType::Fit:
        case DestinationType::FitB:
        {
            // The target page 1170:27667: the fill and the outline 2 outside of the page
            const QRectF devicePage = pagePointToDevicePointMatrix.mapRect(pageRectangle);
            Helper::fillArea(painter, devicePage);
            Helper::strokeArea(painter, devicePage, Helper::TARGET_OUTLINE_WIDTH, Helper::TARGET_OUTLINE_WIDTH, false);
            break;
        }

        case DestinationType::FitH:
        case DestinationType::FitBH:
            painter->drawLine(pagePointToDevicePointMatrix.map(QLineF(pageRectangle.left(), m_hoverPagePoint.y(), pageRectangle.right(), m_hoverPagePoint.y())));
            break;

        case DestinationType::FitV:
        case DestinationType::FitBV:
            painter->drawLine(pagePointToDevicePointMatrix.map(QLineF(m_hoverPagePoint.x(), pageRectangle.top(), m_hoverPagePoint.x(), pageRectangle.bottom())));
            break;

        default:
            break;
    }
}

void PDFAppPageLinkTool::drawPostRendering(QPainter* painter, QRect rect) const
{
    if (!painter || m_step != Step::Target || m_hoverPageIndex < 0)
    {
        return;
    }

    // Tip of the target 1170:27670: "Page 309 · Whole page"
    using Helper = PDFAppAnnotationCreateToolsHelper;
    const QString text = getTipText();
    const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
    const qreal width = PDFStylePainter::textWidth(text, font) + 2 * Helper::TIP_PADDING_X;
    const qreal height = QFontMetricsF(font).height() + 2 * Helper::TIP_PADDING_Y;
    QRectF tip(m_hoverWidgetPoint + QPointF(Helper::TIP_OFFSET, Helper::TIP_OFFSET), QSizeF(width, height));
    if (tip.right() > rect.right())
    {
        tip.moveRight(m_hoverWidgetPoint.x() - Helper::TIP_OFFSET);
    }
    if (tip.bottom() > rect.bottom())
    {
        tip.moveBottom(m_hoverWidgetPoint.y() - Helper::TIP_OFFSET);
    }

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    const PDFStylePainter::Radii radii(6.0);
    // The shadow 0 / 8 / 24 effect/shadow of the tip (1170:27670)
    PDFStylePainter::shadow(painter, tip, radii, QPointF(0.0, 8.0), 24.0, PDFThemeManager::getInstance()->getColor(PDFThemeColors::EffectShadow));
    PDFStylePainter::fill(painter, tip, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(painter, tip, radii, PDFThemeColors::BorderDefault, 1.0);
    PDFStylePainter::text(painter, tip.adjusted(Helper::TIP_PADDING_X, 0, -Helper::TIP_PADDING_X, 0), Qt::AlignVCenter | Qt::AlignLeft, text, font,
                          PDFThemeManager::getInstance()->getColor(PDFThemeColors::TextSecondary));
}

QString PDFAppPageLinkTool::getTipText() const
{
    // The page label as the page field and the text box popup show it (the
    // number of the page without a label, PDFAppDocumentView::getPageLabel)
    const PDFDocument* document = getDocument();
    QString page = QString::number(m_hoverPageIndex + 1);
    if (document && PDFAppAnnotationCreateToolsHelper::getPage(document, m_hoverPageIndex))
    {
        const QString label = document->getCatalog()->getPageLabel(m_hoverPageIndex).left(64);
        if (!label.isEmpty())
        {
            page = label;
        }
    }

    //: Tip of the target of Link to page: the page and the target view
    return PDFAppAnnotationTools::tr("Page %1 · %2").arg(page, PDFAppAnnotationTools::getPageLinkTargetName(m_destinationType));
}

void PDFAppPageLinkTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    BaseClass::mouseMoveEvent(widget, event);

    if (m_step != Step::Target)
    {
        return;
    }

    PDFDrawWidgetProxy* proxy = getProxy();
    if (!proxy)
    {
        return;
    }

    QPointF pagePoint;
    m_hoverWidgetPoint = event->position().toPoint();
    m_hoverPageIndex = proxy->getPageUnderPoint(event->position(), &pagePoint);
    m_hoverPagePoint = pagePoint;
    Q_EMIT proxy->repaintNeeded();
}

void PDFAppPageLinkTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);
    if (active)
    {
        // A new activation starts with the link area
        if (m_step != Step::LinkArea)
        {
            goBack();
        }
    }
    else
    {
        m_hoverPageIndex = -1;
        m_currentPickTool->resetTool();
    }
}

void PDFAppPageLinkTool::onLinkRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    const QRectF normalized = pageRectangle.normalized();
    if (!PDFAppAnnotationCreateToolsHelper::getPage(getDocument(), pageIndex) || !(normalized.width() > 0.0) || !(normalized.height() > 0.0))
    {
        m_areaPickTool->resetTool();
        return;
    }

    m_linkPageIndex = pageIndex;
    m_linkRectangle = normalized;
    setStep(Step::Target);
}

void PDFAppPageLinkTool::onTargetPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    m_pointPickTool->resetTool();
    if (m_step != Step::Target || m_destinationType == DestinationType::FitR)
    {
        return;
    }

    const PDFDestination destination = createDestination(pageIndex, pagePoint, QRectF());
    if (!destination.isValid())
    {
        return;
    }
    if (m_isTargetOnly)
    {
        Q_EMIT targetPicked(destination);
        return;
    }

    const PDFInteger linkPageIndex = m_linkPageIndex;
    const QRectF linkRectangle = m_linkRectangle;
    QPointer<PDFAppPageLinkTool> guard(this);
    Q_EMIT linkRequested(linkPageIndex, linkRectangle, destination);
    if (guard)
    {
        goBack();
    }
}

void PDFAppPageLinkTool::onTargetRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    m_rectanglePickTool->resetTool();
    if (m_step != Step::Target || m_destinationType != DestinationType::FitR)
    {
        return;
    }

    const PDFDestination destination = createDestination(pageIndex, QPointF(), pageRectangle);
    if (!destination.isValid())
    {
        // An empty area is no target - the user draws it again
        return;
    }
    if (m_isTargetOnly)
    {
        Q_EMIT targetPicked(destination);
        return;
    }

    const PDFInteger linkPageIndex = m_linkPageIndex;
    const QRectF linkRectangle = m_linkRectangle;
    QPointer<PDFAppPageLinkTool> guard(this);
    Q_EMIT linkRequested(linkPageIndex, linkRectangle, destination);
    if (guard)
    {
        goBack();
    }
}

void PDFAppPageLinkTool::setStep(Step step)
{
    if (m_step == step)
    {
        updatePickTool();
        return;
    }

    m_step = step;
    m_hoverPageIndex = -1;
    updatePickTool();
    if (PDFDrawWidgetProxy* proxy = getProxy())
    {
        Q_EMIT proxy->repaintNeeded();
    }
    Q_EMIT stepChanged();
}

void PDFAppPageLinkTool::updatePickTool()
{
    PDFPickTool* pickTool = m_areaPickTool;
    if (m_step == Step::Target)
    {
        pickTool = m_destinationType == DestinationType::FitR ? m_rectanglePickTool : m_pointPickTool;
    }

    if (pickTool == m_currentPickTool)
    {
        return;
    }

    // The pick tool of the stack is replaced (the active one is turned off)
    m_currentPickTool->resetTool();
    removeTool();
    m_currentPickTool = pickTool;
    m_currentPickTool->resetTool();
    addTool(m_currentPickTool);
}

}   // namespace pdf
