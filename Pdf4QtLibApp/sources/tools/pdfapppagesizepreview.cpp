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

#include "pdfapppagesizepreview.h"

#include "pdfapppagelayout.h"

#include "pdffluentwidgets.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Helpers of the preview of a page size
class PDFAppPageSizePreviewHelper
{
public:
    PDFAppPageSizePreviewHelper() = delete;

    /// Largest side of the paper (logical pixels): a page of 14 400 points has 19 200 at 100 %,
    /// a widget may be 16 777 215 at most
    static constexpr int MAXIMUM_PAPER_SIDE = 1 << 20;

    /// A side in logical pixels: whole, at least 1, at most MAXIMUM_PAPER_SIDE
    static int toPixels(double value)
    {
        if (!std::isfinite(value) || value < 1.0)
        {
            return 1;
        }
        return int(std::min<double>(std::round(value), MAXIMUM_PAPER_SIDE));
    }
};

// ------------------------------------------------------------------ PDFAppPageSizeStage

PDFAppPageSizeStage::PDFAppPageSizeStage(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPageSizeStage"));

    // The width and the height Medium 11, the caption Regular 11, all text/secondary (337:2136)
    m_widthLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 11, PDFThemeColors::TextSecondary, this);
    m_widthLabel->setObjectName(QStringLiteral("toolPageWidth"));
    m_heightLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 11, PDFThemeColors::TextSecondary, this);
    m_heightLabel->setObjectName(QStringLiteral("toolPageHeight"));
    m_captionLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, this);
    m_captionLabel->setObjectName(QStringLiteral("toolPageCaption"));
    m_captionLabel->setAlignment(Qt::AlignCenter);
    for (PDFLabel* label : { m_widthLabel.data(), m_heightLabel.data(), m_captionLabel.data() })
    {
        label->setLineHeight(TEXT_LINE);
    }
}

PDFAppPageSizeStage::~PDFAppPageSizeStage() = default;

void PDFAppPageSizeStage::setPageSize(const QSizeF& size)
{
    if (m_pageSize != size)
    {
        m_pageSize = size;
        updateGeometry();
        updatePaper();
    }
}

void PDFAppPageSizeStage::setTexts(const QString& widthText, const QString& heightText, const QString& caption)
{
    // The texts are data of the size: the screen readers read them from the stage too
    m_widthLabel->setText(widthText);
    m_heightLabel->setText(heightText);
    m_captionLabel->setText(caption);
    setAccessibleName(caption);
    updateGeometry();
    updatePaper();
}

void PDFAppPageSizeStage::setFit(bool isFit)
{
    if (m_isFit != isFit)
    {
        m_isFit = isFit;
        updateGeometry();
        updatePaper();
    }
}

QSize PDFAppPageSizeStage::getActualPaperSize() const
{
    // K12: 100 % is the size of the page at 96 dpi (a point is 1/72 inch)
    using Helper = PDFAppPageSizePreviewHelper;
    if (!m_pageSize.isValid() || m_pageSize.isEmpty())
    {
        return QSize(1, 1);
    }
    const double scale = ACTUAL_DPI / 72.0;
    return QSize(Helper::toPixels(m_pageSize.width() * scale), Helper::toPixels(m_pageSize.height() * scale));
}

QSize PDFAppPageSizeStage::computePaperSize(const QSize& stageSize) const
{
    using Helper = PDFAppPageSizePreviewHelper;

    const QSize actual = getActualPaperSize();
    if (!m_isFit)
    {
        return actual;
    }

    // Fit: the row of the paper and of its height inside of the side margins, the column of
    // the width, the paper and the caption inside of the vertical margins (337:2135)
    const int heightLabelWidth = m_heightLabel->sizeHint().width();
    const int textHeight = m_widthLabel->sizeHint().height() + m_captionLabel->sizeHint().height();
    const double maximumWidth = stageSize.width() - 2.0 * MARGIN_HORIZONTAL - GAP - heightLabelWidth;
    const double maximumHeight = stageSize.height() - 2.0 * MARGIN_VERTICAL - 2.0 * GAP - textHeight;
    if (maximumWidth < 1.0 || maximumHeight < 1.0)
    {
        return QSize(1, 1);
    }

    const double scale = std::min(maximumWidth / actual.width(), maximumHeight / actual.height());
    return QSize(Helper::toPixels(actual.width() * scale), Helper::toPixels(actual.height() * scale));
}

QSize PDFAppPageSizeStage::getContentSize(const QSize& paperSize) const
{
    const QSize widthLabel = m_widthLabel->sizeHint();
    const QSize heightLabel = m_heightLabel->sizeHint();
    const QSize caption = m_captionLabel->sizeHint();
    const int rowWidth = paperSize.width() + GAP + heightLabel.width();
    const int columnWidth = std::max({ rowWidth, widthLabel.width(), caption.width() });
    const int columnHeight = widthLabel.height() + GAP + paperSize.height() + GAP + caption.height();
    return QSize(columnWidth + 2 * MARGIN_HORIZONTAL, columnHeight + 2 * MARGIN_VERTICAL);
}

QSize PDFAppPageSizeStage::sizeHint() const
{
    return getContentSize(getActualPaperSize());
}

QSize PDFAppPageSizeStage::minimumSizeHint() const
{
    // Fit takes any size; 100 % is as large as its content (the scroll area scrolls it)
    return m_isFit ? QSize(0, 0) : getContentSize(getActualPaperSize());
}

QRect PDFAppPageSizeStage::getPaperRect() const
{
    return m_paper ? m_paper->geometry() : QRect();
}

void PDFAppPageSizeStage::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updatePaper();
}

void PDFAppPageSizeStage::updatePaper()
{
    if (m_isUpdating)
    {
        return;
    }
    m_isUpdating = true;

    if (!m_paper)
    {
        // The place of the paper: no painting of its own, the stage draws the paper and its shadow
        m_paper = new QWidget(this);
        m_paper->setObjectName(QStringLiteral("toolPagePaper"));
        m_paper->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    const bool hasPage = m_pageSize.isValid() && !m_pageSize.isEmpty();
    const QSize paperSize = computePaperSize(size());
    const QSize widthLabel = m_widthLabel->sizeHint();
    const QSize heightLabel = m_heightLabel->sizeHint();
    const QSize caption = m_captionLabel->sizeHint();

    // The column is centered: the width, the row of the paper and its height, the caption
    const int columnHeight = widthLabel.height() + GAP + paperSize.height() + GAP + caption.height();
    const int top = std::max(MARGIN_VERTICAL, (height() - columnHeight) / 2);
    const int centerX = width() / 2;
    const int rowWidth = paperSize.width() + GAP + heightLabel.width();
    const int rowLeft = std::max(MARGIN_HORIZONTAL, (width() - rowWidth) / 2);
    const int paperTop = top + widthLabel.height() + GAP;
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;

    // The height lies at the trailing side of the paper (the left one in right-to-left)
    const QRect paperRect(isRightToLeft ? rowLeft + heightLabel.width() + GAP : rowLeft, paperTop, paperSize.width(), paperSize.height());
    const QRect heightRect(isRightToLeft ? rowLeft : rowLeft + paperSize.width() + GAP, paperTop + (paperSize.height() - heightLabel.height()) / 2, heightLabel.width(), heightLabel.height());
    m_paper->setGeometry(paperRect);
    m_widthLabel->setGeometry(QRect(centerX - widthLabel.width() / 2, top, widthLabel.width(), widthLabel.height()));
    m_heightLabel->setGeometry(heightRect);
    m_captionLabel->setGeometry(QRect(centerX - caption.width() / 2, paperRect.bottom() + 1 + GAP, caption.width(), caption.height()));
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_paper.data(), m_widthLabel.data(), m_heightLabel.data(), m_captionLabel.data() })
    {
        widget->setVisible(hasPage);
    }

    const QSize actual = getActualPaperSize();
    m_zoom = hasPage ? double(paperSize.width()) / double(actual.width()) : 0.0;
    if (hasPage && actual.height() > actual.width())
    {
        // The longer side has the finer step of the rounding
        m_zoom = double(paperSize.height()) / double(actual.height());
    }
    m_isUpdating = false;
    update();
    queueZoomChanged();
}

void PDFAppPageSizeStage::queueZoomChanged()
{
    // A resize may not run a slot of the application: Qt uses the widget after its resize
    if (m_zoom != m_emittedZoom && !m_isZoomQueued)
    {
        m_isZoomQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppPageSizeStage::emitZoomChanged, Qt::QueuedConnection);
    }
}

void PDFAppPageSizeStage::emitZoomChanged()
{
    m_isZoomQueued = false;
    if (m_zoom != m_emittedZoom)
    {
        m_emittedZoom = m_zoom;
        Q_EMIT zoomChanged();
    }
}

void PDFAppPageSizeStage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const QRect paper = getPaperRect();
    if (!m_paper || !m_paper->isVisible() || paper.isEmpty())
    {
        return;
    }

    // page 337:2138: reading/paper/page with the shadow 0 / 8 / 24 of effect/shadow
    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::shadow(&painter, QRectF(paper), PDFStylePainter::Radii(0.0), QPointF(0.0, SHADOW_OFFSET), SHADOW_BLUR, PDFStylePainter::color(PDFThemeColors::EffectShadow));
    PDFStylePainter::fillRect(&painter, QRectF(paper), PDFThemeColors::ReadingPaperPage);
}

// ---------------------------------------------------------------- PDFAppPageSizePreview

PDFAppPageSizePreview::PDFAppPageSizePreview(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPageSizePreview"));

    // document-canvas 337:2009: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 337:2118: the title Semi Bold 13 text/primary, gap 8, the segments Fit / 100 %
    QWidget* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("toolPreviewBar"));
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("toolPreviewTitle"));
    m_title->setMinimumWidth(1);
    barLayout->addWidget(m_title, 1);
    m_zoom = new PDFSegmentedControl(bar);
    m_zoom->setObjectName(QStringLiteral("toolPreviewZoom"));
    m_zoom->addSegment(QString());
    m_zoom->addSegment(QString());
    m_zoom->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    connect(m_zoom.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppPageSizePreview::onZoomIndexChanged);
    barLayout->addWidget(m_zoom);
    layout->addWidget(bar);

    // stage 337:2135: the rest of the canvas; 100 % scrolls
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("toolPageSizeArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setProperty("overlayScrollBars", true);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_stage = new PDFAppPageSizeStage(m_scrollArea);
    m_scrollArea->setWidget(m_stage);
    connect(m_stage.data(), &PDFAppPageSizeStage::zoomChanged, this, &PDFAppPageSizePreview::onStageZoomChanged);
    layout->addWidget(m_scrollArea, 1);

    updateTexts();
}

PDFAppPageSizePreview::~PDFAppPageSizePreview() = default;

void PDFAppPageSizePreview::setTitle(const QString& title)
{
    m_title->setText(title);
}

void PDFAppPageSizePreview::setPage(const QSizeF& size, const QString& widthText, const QString& heightText, const QString& caption)
{
    m_stage->setTexts(widthText, heightText, caption);
    m_stage->setPageSize(size);
}

void PDFAppPageSizePreview::setFit(bool isFit)
{
    // The segments write the mode (onZoomIndexChanged)
    m_zoom->setCurrentIndex(isFit ? 0 : 1);
    updateStageMode();
}

bool PDFAppPageSizePreview::isFit() const
{
    return m_stage->isFit();
}

double PDFAppPageSizePreview::getZoom() const
{
    return m_stage->getZoom();
}

void PDFAppPageSizePreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 337:2009: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

void PDFAppPageSizePreview::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppPageSizePreview::onZoomIndexChanged(int index)
{
    Q_UNUSED(index);
    updateStageMode();
}

void PDFAppPageSizePreview::updateStageMode()
{
    const bool isFit = m_zoom->getCurrentIndex() == 0;
    m_stage->setFit(isFit);

    // Fit fills the area, 100 % has the size of its content and scrolls
    m_scrollArea->setHorizontalScrollBarPolicy(isFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(isFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
}

void PDFAppPageSizePreview::onStageZoomChanged()
{
    // From the event loop already (the stage queues it): emitted last
    Q_EMIT zoomChanged();
}

void PDFAppPageSizePreview::updateTexts()
{
    // The segments of the page itself follow the language (871:135793)
    const QStringList segments = { tr("Fit"), tr("100 %") };
    const QList<QToolButton*> buttons = m_zoom->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (qsizetype i = 0; i < buttons.size() && i < segments.size(); ++i)
    {
        buttons[i]->setText(segments[i]);
    }
    m_zoom->setAccessibleName(tr("Size of the page"));
}

}   // namespace pdf

#include "moc_pdfapppagesizepreview.cpp"
