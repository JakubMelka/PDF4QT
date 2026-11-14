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

#include "pdfapptoolimagecomparison.h"

#include "pdfappdocumenttoolbar.h"
#include "pdfapppagelayout.h"

#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <utility>

namespace pdf
{

// ---------------------------------------------------------- PDFAppToolImageComparisonView

PDFAppToolImageComparisonView::PDFAppToolImageComparisonView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("imageComparisonView"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(2 * FIT_MARGIN + 1, 2 * FIT_MARGIN + 1);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::RADIUS, RADIUS);
}

PDFAppToolImageComparisonView::~PDFAppToolImageComparisonView() = default;

void PDFAppToolImageComparisonView::setImage(const QImage& image)
{
    m_image = image;
    m_scaledPixmap = QPixmap();
    m_scaledPixmapSize = QSize();
    m_center = boundCenter(m_center);
    updateCursor();
    update();
}

void PDFAppToolImageComparisonView::setPlaceholderText(const QString& text)
{
    if (m_placeholderText != text)
    {
        m_placeholderText = text;
        update();
    }
}

void PDFAppToolImageComparisonView::setFit(bool isFit)
{
    if (m_isFit != isFit)
    {
        m_isFit = isFit;
        updateCursor();
        update();
    }
}

void PDFAppToolImageComparisonView::setCenter(const QPointF& center)
{
    const QPointF newCenter(qBound(0.0, center.x(), 1.0), qBound(0.0, center.y(), 1.0));
    if (m_center != newCenter)
    {
        m_center = newCenter;
        update();
    }
}

QSizeF PDFAppToolImageComparisonView::getDrawnSize() const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0)
    {
        return QSizeF();
    }

    if (m_isFit)
    {
        // Fit: the whole image in the area without its margin, also a small one enlarged
        const QSizeF room(qMax(1.0, qreal(width() - 2 * FIT_MARGIN)), qMax(1.0, qreal(height() - 2 * FIT_MARGIN)));
        const qreal scale = qMin(room.width() / m_image.width(), room.height() / m_image.height());
        return QSizeF(m_image.width() * scale, m_image.height() * scale);
    }

    // 100 %: an image pixel is a device pixel
    const qreal ratio = qMax(qreal(1.0), devicePixelRatioF());
    return QSizeF(m_image.width() / ratio, m_image.height() / ratio);
}

QPointF PDFAppToolImageComparisonView::boundCenter(const QPointF& center) const
{
    const QSizeF drawnSize = getDrawnSize();
    if (m_isFit || drawnSize.isEmpty())
    {
        return center;
    }

    QPointF result = center;
    if (drawnSize.width() <= width())
    {
        result.setX(0.5);
    }
    else
    {
        const qreal half = width() * 0.5 / drawnSize.width();
        result.setX(qBound(half, center.x(), 1.0 - half));
    }
    if (drawnSize.height() <= height())
    {
        result.setY(0.5);
    }
    else
    {
        const qreal half = height() * 0.5 / drawnSize.height();
        result.setY(qBound(half, center.y(), 1.0 - half));
    }
    return result;
}

QRectF PDFAppToolImageComparisonView::getImageRect() const
{
    const QSizeF drawnSize = getDrawnSize();
    if (drawnSize.isEmpty())
    {
        return QRectF();
    }

    if (m_isFit)
    {
        return PDFStylePainter::centered(QRectF(rect()), drawnSize);
    }

    const QPointF center = boundCenter(m_center);
    const qreal left = width() * 0.5 - center.x() * drawnSize.width();
    const qreal top = height() * 0.5 - center.y() * drawnSize.height();
    return QRectF(QPointF(left, top), drawnSize);
}

bool PDFAppToolImageComparisonView::isMovable() const
{
    const QSizeF drawnSize = getDrawnSize();
    return !m_isFit && !drawnSize.isEmpty() && (drawnSize.width() > width() || drawnSize.height() > height());
}

void PDFAppToolImageComparisonView::moveBy(const QPointF& delta)
{
    const QSizeF drawnSize = getDrawnSize();
    if (!isMovable())
    {
        return;
    }

    const QPointF current = boundCenter(m_center);
    const QPointF moved = boundCenter(QPointF(current.x() + delta.x() / drawnSize.width(), current.y() + delta.y() / drawnSize.height()));
    if (moved == current)
    {
        return;
    }

    m_center = moved;
    update();
    Q_EMIT centerMoved(m_center);
}

QPixmap PDFAppToolImageComparisonView::getScaledPixmap(const QSizeF& drawnSize) const
{
    const qreal ratio = qMax(qreal(1.0), devicePixelRatioF());
    const QSize deviceSize(qMax(1, qRound(drawnSize.width() * ratio)), qMax(1, qRound(drawnSize.height() * ratio)));
    if (m_scaledPixmapSize != deviceSize || m_scaledPixmap.isNull())
    {
        QImage scaled;
        if (deviceSize == m_image.size())
        {
            scaled = m_image;
        }
        else
        {
            // An enlarged image shows its pixels (the artifacts of the compression), a smaller one is smoothed
            const bool isEnlarged = deviceSize.width() > m_image.width();
            scaled = m_image.scaled(deviceSize, Qt::IgnoreAspectRatio, isEnlarged ? Qt::FastTransformation : Qt::SmoothTransformation);
        }
        m_scaledPixmap = QPixmap::fromImage(scaled);
        m_scaledPixmap.setDevicePixelRatio(ratio);
        m_scaledPixmapSize = deviceSize;
    }
    return m_scaledPixmap;
}

void PDFAppToolImageComparisonView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // image-area 298:3307: bg/muted, radius 6
    const QRectF area(rect());
    const PDFStylePainter::Radii radii(RADIUS);
    PDFStylePainter::fill(&painter, area, radii, PDFThemeColors::BgMuted);

    const QRectF imageRect = getImageRect();
    if (imageRect.isEmpty())
    {
        const QString text = m_placeholderText.isEmpty() ? tr("Computing the preview…") : m_placeholderText;
        PDFStylePainter::text(&painter, area.adjusted(FIT_MARGIN, FIT_MARGIN, -FIT_MARGIN, -FIT_MARGIN), Qt::AlignCenter | Qt::TextWordWrap, text,
                              PDFThemeFonts::getFont(QFont::Normal, 12), PDFStylePainter::color(PDFThemeColors::TextTertiary));
    }
    else
    {
        PDFStylePainterGuard guard(&painter);
        painter.setClipPath(PDFStylePainter::roundedPath(area, radii));

        // The paper below the image: a transparent image lies on a white page
        PDFStylePainter::fillRect(&painter, imageRect, PDFThemeColors::SampleWhite);
        painter.drawPixmap(imageRect.topLeft(), getScaledPixmap(imageRect.size()));
    }

    if (hasFocus())
    {
        PDFStylePainter::focusRing(&painter, area, radii, false);
    }
}

void PDFAppToolImageComparisonView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isMovable())
    {
        m_isDragging = true;
        m_dragPosition = event->position();
        updateCursor();
        event->accept();
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppToolImageComparisonView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging)
    {
        // The image follows the pointer: the center moves against it; centerMoved is the last call
        const QPointF delta = m_dragPosition - event->position();
        m_dragPosition = event->position();
        event->accept();
        moveBy(delta);
        return;
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFAppToolImageComparisonView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isDragging && event->button() == Qt::LeftButton)
    {
        m_isDragging = false;
        updateCursor();
        event->accept();
        return;
    }
    BaseClass::mouseReleaseEvent(event);
}

void PDFAppToolImageComparisonView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_image.isNull())
    {
        m_isDragging = false;
        event->accept();
        Q_EMIT doubleClicked();
        return;
    }
    BaseClass::mouseDoubleClickEvent(event);
}

void PDFAppToolImageComparisonView::wheelEvent(QWheelEvent* event)
{
    if (!isMovable())
    {
        event->ignore();
        return;
    }

    const QPoint angle = event->angleDelta();
    qreal dx = -angle.x() / 120.0 * ARROW_STEP;
    qreal dy = -angle.y() / 120.0 * ARROW_STEP;
    if (event->modifiers().testFlag(Qt::ShiftModifier) && qFuzzyIsNull(dx))
    {
        std::swap(dx, dy);
    }
    event->accept();
    moveBy(QPointF(dx, dy));
}

void PDFAppToolImageComparisonView::keyPressEvent(QKeyEvent* event)
{
    QPointF delta;
    switch (event->key())
    {
        case Qt::Key_Left:
            delta = QPointF(-ARROW_STEP, 0);
            break;
        case Qt::Key_Right:
            delta = QPointF(ARROW_STEP, 0);
            break;
        case Qt::Key_Up:
            delta = QPointF(0, -ARROW_STEP);
            break;
        case Qt::Key_Down:
            delta = QPointF(0, ARROW_STEP);
            break;
        default:
            BaseClass::keyPressEvent(event);
            return;
    }

    if (!isMovable() || event->modifiers() != Qt::NoModifier)
    {
        BaseClass::keyPressEvent(event);
        return;
    }
    event->accept();
    moveBy(delta);
}

void PDFAppToolImageComparisonView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateCursor();
}

void PDFAppToolImageComparisonView::updateCursor()
{
    if (isMovable())
    {
        setCursor(m_isDragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    }
    else
    {
        unsetCursor();
    }
}

// ---------------------------------------------------------- PDFAppToolImageComparisonPane

PDFAppToolImageComparisonPane::PDFAppToolImageComparisonPane(QWidget* parent) :
    BaseClass(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING_TOP, PADDING, PADDING);
    layout->setSpacing(GAP);

    // caption 298:3306: Semi Bold 11 text/tertiary, letter spacing 6 %, centered
    m_caption = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, this);
    QFont captionFont = m_caption->font();
    captionFont.setLetterSpacing(QFont::PercentageSpacing, 106.0);
    m_caption->setFont(captionFont);
    m_caption->setAlignment(Qt::AlignCenter);
    m_caption->setMinimumWidth(1);
    m_caption->setObjectName(QStringLiteral("imageComparisonCaption"));
    layout->addWidget(m_caption);

    m_view = new PDFAppToolImageComparisonView(this);
    layout->addWidget(m_view, 1);

    // info 298:3316: Regular 11 text/secondary, centered
    m_info = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, this);
    m_info->setAlignment(Qt::AlignCenter);
    m_info->setMinimumWidth(1);
    m_info->setObjectName(QStringLiteral("imageComparisonInfo"));
    layout->addWidget(m_info);
}

PDFAppToolImageComparisonPane::~PDFAppToolImageComparisonPane() = default;

void PDFAppToolImageComparisonPane::setTexts(const QString& caption, const QString& info)
{
    m_caption->setText(caption);
    m_info->setText(info);
    m_info->setVisible(!info.isEmpty());
    if (m_view)
    {
        m_view->setAccessibleName(caption);
    }
}

void PDFAppToolImageComparisonPane::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // pane 298:3305: bg/surface, border/default 1, radius 10
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const PDFStylePainter::Radii radii(RADIUS);
    PDFStylePainter::fill(&painter, QRectF(rect()), radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, QRectF(rect()), radii, PDFThemeColors::BorderDefault);
}

// ---------------------------------------------------------- PDFAppToolImageComparison

PDFAppToolImageComparison::PDFAppToolImageComparison(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("imageComparison"));

    // document-canvas 297:3201: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 298:3274: the title, Fit / 100 %, Sync zoom, Show on page (gap 8)
    QWidget* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("imageComparisonBar"));
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(BAR_GAP);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("imageComparisonTitle"));
    m_title->setMinimumWidth(1);
    barLayout->addWidget(m_title, 1);

    m_zoomControl = new PDFSegmentedControl(bar);
    m_zoomControl->setObjectName(QStringLiteral("imageComparisonZoom"));
    m_zoomControl->addSegment(QString());
    m_zoomControl->addSegment(QString());
    m_zoomControl->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    connect(m_zoomControl.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolImageComparison::onZoomIndexChanged);
    barLayout->addWidget(m_zoomControl);

    // preview-Sync zoom 304:5219: Filter chip 114 x 32, the check mark when it is on
    m_syncZoomChip = new QPushButton(bar);
    m_syncZoomChip->setObjectName(QStringLiteral("imageComparisonSyncZoom"));
    m_syncZoomChip->setCheckable(true);
    m_syncZoomChip->setChecked(m_isSyncZoom);
    m_syncZoomChip->setAutoDefault(false);
    m_syncZoomChip->setIconSize(QSize(14, 14));
    m_syncZoomChip->setFixedSize(SYNC_ZOOM_WIDTH, SEGMENT_HEIGHT);
    PDFWidgetStyleProperties::set(m_syncZoomChip, PDFWidgetStyleProperties::CHIP);
    PDFWidgetStyleProperties::set(m_syncZoomChip, PDFWidgetStyleProperties::OPTION);
    m_syncZoomChip->hide();
    connect(m_syncZoomChip.data(), &QPushButton::toggled, this, &PDFAppToolImageComparison::onSyncZoomToggled);
    barLayout->addWidget(m_syncZoomChip);

    // tb-show-on-page 298:3299: Toolbar button 36 x 36 with icon/locate
    m_showOnPageButton = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("imageComparisonShowOnPage"), QStringLiteral("locate"), bar);
    m_showOnPageButton->setFixedSize(BAR_HEIGHT, BAR_HEIGHT);
    connect(m_showOnPageButton.data(), &QToolButton::clicked, this, &PDFAppToolImageComparison::onShowOnPageClicked);
    barLayout->addWidget(m_showOnPageButton);
    layout->addWidget(bar);

    // panes 298:3304: BEFORE at the left, AFTER at the right also for right-to-left
    QWidget* panes = new QWidget(this);
    panes->setObjectName(QStringLiteral("imageComparisonPanes"));
    panes->setLayoutDirection(Qt::LeftToRight);
    QHBoxLayout* panesLayout = new QHBoxLayout(panes);
    panesLayout->setContentsMargins(0, 0, 0, 0);
    panesLayout->setSpacing(GAP);
    m_beforePane = new PDFAppToolImageComparisonPane(panes);
    m_beforePane->setObjectName(QStringLiteral("imageComparisonBefore"));
    m_afterPane = new PDFAppToolImageComparisonPane(panes);
    m_afterPane->setObjectName(QStringLiteral("imageComparisonAfter"));
    panesLayout->addWidget(m_beforePane, 1);
    panesLayout->addWidget(m_afterPane, 1);
    layout->addWidget(panes, 1);

    connect(m_beforePane->getView(), &PDFAppToolImageComparisonView::centerMoved, this, &PDFAppToolImageComparison::onBeforeCenterMoved);
    connect(m_afterPane->getView(), &PDFAppToolImageComparisonView::centerMoved, this, &PDFAppToolImageComparison::onAfterCenterMoved);
    connect(m_beforePane->getView(), &PDFAppToolImageComparisonView::doubleClicked, this, &PDFAppToolImageComparison::onViewDoubleClicked);
    connect(m_afterPane->getView(), &PDFAppToolImageComparisonView::doubleClicked, this, &PDFAppToolImageComparison::onViewDoubleClicked);

    // estimate 298:3329: Semi Bold 13 text/primary and the detail Regular 12 text/secondary, gap 8
    QWidget* estimate = new QWidget(this);
    estimate->setObjectName(QStringLiteral("imageComparisonEstimate"));
    QHBoxLayout* estimateLayout = new QHBoxLayout(estimate);
    estimateLayout->setContentsMargins(0, 0, 0, 0);
    estimateLayout->setSpacing(ESTIMATE_GAP);
    m_footer = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, estimate);
    m_footer->setObjectName(QStringLiteral("imageComparisonFooter"));
    m_footerDetail = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, estimate);
    m_footerDetail->setObjectName(QStringLiteral("imageComparisonFooterDetail"));
    m_footerDetail->setMinimumWidth(1);
    PDFAppPageWidgets::addBaselineLabels(estimateLayout, { { m_footer, 0 }, { m_footerDetail, 1 } });
    layout->addWidget(estimate);

    updateTexts();
    updateViews();
    setFooterTexts(QString(), QString());
}

PDFAppToolImageComparison::~PDFAppToolImageComparison() = default;

void PDFAppToolImageComparison::setImages(const QImage& before, const QImage& after)
{
    m_before = before;
    m_after = after;
    m_beforePane->getView()->setImage(before);
    m_afterPane->getView()->setImage(after);
}

void PDFAppToolImageComparison::setCaptions(const QString& beforeTitle, const QString& beforeDetail, const QString& afterTitle, const QString& afterDetail)
{
    m_beforePane->setTexts(beforeTitle, beforeDetail);
    m_afterPane->setTexts(afterTitle, afterDetail);
}

void PDFAppToolImageComparison::setFooterText(const QString& text)
{
    setFooterTexts(text, QString());
}

void PDFAppToolImageComparison::setFooterTexts(const QString& text, const QString& detail)
{
    m_footer->setText(text);
    m_footerDetail->setText(detail);
    m_footer->setVisible(!text.isEmpty());
    m_footerDetail->setVisible(!detail.isEmpty());
}

void PDFAppToolImageComparison::setTitle(const QString& title)
{
    m_title->setText(title);
}

void PDFAppToolImageComparison::setPlaceholderTexts(const QString& before, const QString& after)
{
    m_beforePlaceholder = before;
    m_afterPlaceholder = after;
    m_beforePane->getView()->setPlaceholderText(before);
    m_afterPane->getView()->setPlaceholderText(after);
}

void PDFAppToolImageComparison::setZoom(Zoom zoom)
{
    if (m_zoom == zoom)
    {
        return;
    }

    m_zoom = zoom;
    m_isUpdatingControls = true;
    m_zoomControl->setCurrentIndex(zoom == Zoom::Fit ? 0 : 1);
    m_isUpdatingControls = false;
    updateViews();
}

void PDFAppToolImageComparison::setSyncZoom(bool isSyncZoom)
{
    if (m_isSyncZoom == isSyncZoom)
    {
        return;
    }

    m_isSyncZoom = isSyncZoom;
    m_isUpdatingControls = true;
    m_syncZoomChip->setChecked(isSyncZoom);
    m_isUpdatingControls = false;
    if (isSyncZoom)
    {
        // The AFTER card follows the BEFORE card
        m_afterPane->getView()->setCenter(m_beforePane->getView()->getCenter());
    }
}

void PDFAppToolImageComparison::setSyncZoomVisible(bool isVisible)
{
    m_syncZoomChip->setVisible(isVisible);
}

void PDFAppToolImageComparison::setShowOnPageVisible(bool isVisible)
{
    m_showOnPageButton->setVisible(isVisible);
}

void PDFAppToolImageComparison::updateViews()
{
    const bool isFit = m_zoom == Zoom::Fit;
    m_beforePane->getView()->setFit(isFit);
    m_afterPane->getView()->setFit(isFit);
}

void PDFAppToolImageComparison::updateTexts()
{
    const QStringList segments = { tr("Fit"), tr("100 %") };
    const QList<QToolButton*> buttons = m_zoomControl->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (qsizetype i = 0; i < buttons.size() && i < segments.size(); ++i)
    {
        buttons[i]->setText(segments[i]);
    }
    m_zoomControl->setAccessibleName(tr("Zoom of the preview"));
    m_syncZoomChip->setText(tr("Sync zoom"));
    m_syncZoomChip->setAccessibleName(tr("Sync zoom"));
    m_showOnPageButton->setToolTip(tr("Show on page"));
    m_showOnPageButton->setAccessibleName(tr("Show on page"));
}

void PDFAppToolImageComparison::onZoomIndexChanged(int index)
{
    if (m_isUpdatingControls)
    {
        return;
    }

    m_zoom = index == 1 ? Zoom::ActualSize : Zoom::Fit;
    updateViews();
    postZoomChanged();
}

void PDFAppToolImageComparison::postZoomChanged()
{
    // The segment, the chip or the view is still inside of its event: a slot, which deletes the
    // comparison, gets the signal from the event loop, once for more changes
    if (!m_isZoomChangePending)
    {
        m_isZoomChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppToolImageComparison::emitZoomChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolImageComparison::emitZoomChanged()
{
    m_isZoomChangePending = false;
    Q_EMIT zoomChanged();
}

void PDFAppToolImageComparison::onSyncZoomToggled(bool isChecked)
{
    if (m_isUpdatingControls)
    {
        return;
    }

    m_isSyncZoom = isChecked;
    if (isChecked)
    {
        m_afterPane->getView()->setCenter(m_beforePane->getView()->getCenter());
    }
    postZoomChanged();
}

void PDFAppToolImageComparison::onShowOnPageClicked()
{
    Q_EMIT showOnPageRequested();
}

void PDFAppToolImageComparison::onBeforeCenterMoved(QPointF center)
{
    if (m_isSyncZoom)
    {
        m_afterPane->getView()->setCenter(center);
    }
}

void PDFAppToolImageComparison::onAfterCenterMoved(QPointF center)
{
    if (m_isSyncZoom)
    {
        m_beforePane->getView()->setCenter(center);
    }
}

void PDFAppToolImageComparison::onViewDoubleClicked()
{
    // A double click switches Fit / 100 % (a change of the user)
    m_zoom = m_zoom == Zoom::Fit ? Zoom::ActualSize : Zoom::Fit;
    m_isUpdatingControls = true;
    m_zoomControl->setCurrentIndex(m_zoom == Zoom::Fit ? 0 : 1);
    m_isUpdatingControls = false;
    updateViews();
    postZoomChanged();
}

void PDFAppToolImageComparison::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 297:3201: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

void PDFAppToolImageComparison::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolimagecomparison.cpp"
