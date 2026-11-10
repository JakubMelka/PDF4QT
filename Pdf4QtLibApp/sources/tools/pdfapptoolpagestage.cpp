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

#include "pdfapptoolpagestage.h"

#include "pdfapppagelayout.h"
#include "pdfapppagerendercontext.h"

#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdffluentwidgets.h"
#include "pdfoperationcontrol.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfthreadannotations.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QScrollArea>
#include <QThreadPool>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <atomic>
#include <cmath>

namespace pdf
{

/// The way of a finished rendering back to the stage: cut by the destructor of the stage, a job
/// does not hold the stage (a late picture goes with the job)
class PDFAppToolPageStageLink
{
public:
    explicit PDFAppToolPageStageLink(PDFAppToolPageStage* stage) :
        m_stage(stage)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_stage = nullptr;
    }

    void finished(const std::shared_ptr<PDFAppToolPageStageJob>& job)
    {
        QMutexLocker locker(&m_mutex);
        if (m_stage)
        {
            PDFThreadAnnotations::happensBefore(job.get());
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_stage, &PDFAppToolPageStage::onRenderFinished, Qt::QueuedConnection, job);
        }
    }

private:
    QMutex m_mutex;
    PDFAppToolPageStage* m_stage;
};

/// One rendering of the page of the stage in the pool: plain values written before the start
/// and read after the end (the hand-offs are annotated), except of the cancellation
class PDFAppToolPageStageJob : public PDFOperationControl
{
public:
    std::shared_ptr<PDFAppToolPageStageLink> link;
    PDFDocumentPointer document;
    PDFInteger pageIndex = -1;
    PDFCMSSettings cmsSettings;
    PDFRenderer::Features features;
    QColor paperColor;
    QSize imageSize;
    QTransform pageToImage;
    QRectF pageRect;            ///< The rectangle of the page in the stage, for which the picture is
    QImage image;               ///< The result

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
            QImage result(imageSize, QImage::Format_ARGB32_Premultiplied);
            if (result.isNull())
            {
                return;
            }
            result.fill(paperColor);
            {
                QPainter painter(&result);
                PDFAppPageRenderContext context(document.data(), cmsSettings, features);
                context.draw(&painter, pageIndex, pageToImage, features.testFlag(PDFRenderer::DisplayAnnotations), this);
            }
            image = std::move(result);
        }
        catch (const PDFException&)
        {
            // A damaged page: the stage shows the paper
            image = QImage();
        }
    }

private:
    std::atomic_bool m_isCanceled = false;
};

/// The task of the pool: it holds the job, never the stage
class PDFAppToolPageStageTask
{
public:
    explicit PDFAppToolPageStageTask(std::shared_ptr<PDFAppToolPageStageJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()()
    {
        m_job->run();
        const std::shared_ptr<PDFAppToolPageStageLink> link = m_job->link;
        link->finished(m_job);
    }

private:
    std::shared_ptr<PDFAppToolPageStageJob> m_job;
};

/// The arrow between Now and After (391:23146: chevron-right 16, icon/muted)
class PDFAppToolPageArrow : public QWidget
{
public:
    explicit PDFAppToolPageArrow(QWidget* parent) :
        QWidget(parent)
    {
        setFixedWidth(PDFAppToolPageCanvas::COLUMN_GAP);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::icon(&painter, PDFStylePainter::centered(QRectF(rect()), QSizeF(16, 16)), QStringLiteral("chevron-right"), PDFThemeColors::IconMuted);
    }
};

// ---------------------------------------------------------------- PDFAppToolPageStage

PDFAppToolPageStage::PDFAppToolPageStage(QWidget* parent) :
    BaseClass(parent),
    m_link(std::make_shared<PDFAppToolPageStageLink>(this))
{
    setObjectName(QStringLiteral("toolPageStage"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(RENDER_DELAY);
    connect(&m_renderTimer, &QTimer::timeout, this, &PDFAppToolPageStage::startRender);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppToolPageStage::update));
}

PDFAppToolPageStage::~PDFAppToolPageStage()
{
    // Nobody waits: the job goes on with its own data, its late picture is dropped
    cancelRender();
    m_link->detach();
}

void PDFAppToolPageStage::setPage(PDFDocumentPointer document, const QString& documentId, PDFInteger pageIndex)
{
    const bool isSame = m_document == document && m_documentId == documentId && m_pageIndex == pageIndex && !isImageShown();
    m_sourcePicture = QImage();
    m_imagePixelSize = QSize();
    m_document = std::move(document);
    m_documentId = documentId;
    m_pageIndex = pageIndex;
    if (!getPage())
    {
        m_pageIndex = -1;
    }
    if (!isSame)
    {
        cancelRender();
        m_image = QImage();
    }
    onGeometryChanged();
}

void PDFAppToolPageStage::setImage(const QImage& picture, const QSize& pixelSize)
{
    // No page and no worker: the picture is a value of the stage
    cancelRender();
    m_image = QImage();
    m_document.reset();
    m_documentId.clear();
    m_pageIndex = -1;
    m_sourcePicture = picture;
    m_imagePixelSize = pixelSize.isValid() && !pixelSize.isEmpty() ? pixelSize : QSize();
    onGeometryChanged();
}

const PDFPage* PDFAppToolPageStage::getPage() const
{
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;
    if (!catalog || m_pageIndex < 0 || size_t(m_pageIndex) >= catalog->getPageCount())
    {
        return nullptr;
    }
    return catalog->getPage(size_t(m_pageIndex));
}

void PDFAppToolPageStage::setZoom(Zoom zoom)
{
    if (m_zoom != zoom)
    {
        m_zoom = zoom;
        onGeometryChanged();
    }
}

void PDFAppToolPageStage::setShownBox(PDFPageGeometry::Box box)
{
    if (m_shownBox != box)
    {
        m_shownBox = box;
        m_image = QImage();
        onGeometryChanged();
    }
}

QRectF PDFAppToolPageStage::getShownBoxRect() const
{
    const PDFPage* page = getPage();
    return page ? PDFPageGeometry::getEffectiveBox(page, m_shownBox) : QRectF();
}

QSizeF PDFAppToolPageStage::getPageSize() const
{
    if (isImageShown())
    {
        return QSizeF(m_imagePixelSize);
    }
    const PDFPage* page = getPage();
    return page ? PDFPage::getRotatedBox(getShownBoxRect(), page->getPageRotation()).size() : QSizeF();
}

void PDFAppToolPageStage::setSheet(const QSizeF& sheetSize, const QRectF& contentRect)
{
    if (m_sheetSize != sheetSize || m_contentRect != contentRect)
    {
        m_sheetSize = sheetSize;
        m_contentRect = contentRect;
        onGeometryChanged();
    }
}

void PDFAppToolPageStage::setProxy(PDFDrawWidgetProxy* proxy)
{
    m_proxy = proxy;
    m_image = QImage();
    scheduleRender();
}

QSizeF PDFAppToolPageStage::getSheetSizeOrPage() const
{
    return m_sheetSize.isValid() && !m_sheetSize.isEmpty() ? m_sheetSize : getPageSize();
}

double PDFAppToolPageStage::getScale() const
{
    const QSizeF sheet = getSheetSizeOrPage();
    if (sheet.isEmpty())
    {
        return 0.0;
    }

    if (m_zoom == Zoom::Actual)
    {
        // A unit of the page is /UserUnit points, a point is 1/72 inch; a pixel of an image is
        // a logical pixel
        if (isImageShown())
        {
            return 1.0;
        }
        const PDFPage* page = getPage();
        return ACTUAL_DPI / 72.0 * (page ? page->getEffectiveUserUnit() : 1.0);
    }

    const QRectF area = QRectF(rect()).adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
    if (area.isEmpty())
    {
        return 0.0;
    }
    const QSizeF fitted = m_fitExtent.isEmpty() ? sheet : m_fitExtent.expandedTo(sheet);
    return qMin(area.width() / fitted.width(), area.height() / fitted.height());
}

void PDFAppToolPageStage::setFitExtent(const QSizeF& extent)
{
    if (m_fitExtent != extent)
    {
        m_fitExtent = extent;
        onGeometryChanged();
    }
}

QSize PDFAppToolPageStage::getActualSize() const
{
    const QSizeF sheet = getSheetSizeOrPage();
    const PDFPage* page = getPage();
    const double scale = isImageShown() ? 1.0 : ACTUAL_DPI / 72.0 * (page ? page->getEffectiveUserUnit() : 1.0);
    return QSize(qCeil(sheet.width() * scale), qCeil(sheet.height() * scale));
}

QRectF PDFAppToolPageStage::getSheetRect() const
{
    const QSizeF sheet = getSheetSizeOrPage();
    const double scale = getScale();
    if (sheet.isEmpty() || scale <= 0.0)
    {
        return QRectF();
    }

    // Centered; 100 % keeps its margin at the top left in a larger stage
    const QSizeF size(sheet.width() * scale, sheet.height() * scale);
    const QPointF center = QRectF(rect()).center();
    return QRectF(QPointF(qMax(double(MARGIN), center.x() - size.width() * 0.5), qMax(double(MARGIN), center.y() - size.height() * 0.5)), size);
}

QRectF PDFAppToolPageStage::getPageRect() const
{
    const QRectF sheetRect = getSheetRect();
    if (sheetRect.isEmpty() || !m_sheetSize.isValid() || m_sheetSize.isEmpty())
    {
        return sheetRect;
    }
    return getSheetToStageTransform().mapRect(m_contentRect);
}

QTransform PDFAppToolPageStage::getSheetToStageTransform() const
{
    const QRectF sheetRect = getSheetRect();
    const double scale = getScale();
    if (sheetRect.isEmpty())
    {
        return QTransform();
    }
    return QTransform(scale, 0.0, 0.0, scale, sheetRect.left(), sheetRect.top());
}

QTransform PDFAppToolPageStage::getPageToStageTransform() const
{
    const QRectF pageRect = getPageRect();
    const QSizeF pageSize = getPageSize();
    if (pageRect.isEmpty() || pageSize.isEmpty())
    {
        return QTransform();
    }
    return QTransform(pageRect.width() / pageSize.width(), 0.0, 0.0, pageRect.height() / pageSize.height(), pageRect.left(), pageRect.top());
}

void PDFAppToolPageStage::addOverlay(QWidget* overlay)
{
    if (overlay)
    {
        overlay->setParent(this);
        overlay->setGeometry(rect());
        overlay->show();
        overlay->raise();
    }
}

bool PDFAppToolPageStage::hasPicture() const
{
    if (isImageShown())
    {
        return !m_sourcePicture.isNull();
    }
    return !m_image.isNull() && m_imagePageIndex == m_pageIndex && m_imageDocument == m_document && m_imagePageRect == getPageRect();
}

bool PDFAppToolPageStage::isRenderPending() const
{
    return m_renderTimer.isActive() || m_job != nullptr;
}

QSize PDFAppToolPageStage::sizeHint() const
{
    if (m_zoom == Zoom::Actual)
    {
        return getActualSize() + QSize(2 * MARGIN, 2 * MARGIN);
    }
    return QSize(320, 420);
}

QSize PDFAppToolPageStage::minimumSizeHint() const
{
    if (m_zoom == Zoom::Actual)
    {
        return sizeHint();
    }
    return QSize(2 * MARGIN + 16, 2 * MARGIN + 16);
}

void PDFAppToolPageStage::onGeometryChanged()
{
    for (QWidget* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        child->setGeometry(rect());
    }
    updateGeometry();
    update();
    scheduleRender();
    if (getPageRect() != m_lastPageRect)
    {
        m_lastPageRect = getPageRect();
        queueGeometryChanged();
    }
}

void PDFAppToolPageStage::queueGeometryChanged()
{
    // Never inside of a resize or a setter of the tool: a slot may delete the stage
    if (!m_isGeometryChangedQueued)
    {
        m_isGeometryChangedQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolPageStage::emitGeometryChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolPageStage::emitGeometryChanged()
{
    m_isGeometryChangedQueued = false;
    Q_EMIT pageGeometryChanged();
}

void PDFAppToolPageStage::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    onGeometryChanged();
}

void PDFAppToolPageStage::scheduleRender()
{
    // A picture of the page in this size is there, or is being rendered: nothing to do
    const QRectF pageRect = getPageRect();
    const bool isRendering = m_job && m_job->pageIndex == m_pageIndex && m_job->document == m_document && m_job->pageRect == pageRect;
    if (!getPage() || pageRect.isEmpty() || hasPicture() || isRendering)
    {
        return;
    }
    m_renderTimer.start();
}

void PDFAppToolPageStage::cancelRender()
{
    m_renderTimer.stop();
    if (m_job)
    {
        m_job->cancel();
        m_job.reset();
    }
}

void PDFAppToolPageStage::startRender()
{
    cancelRender();

    const PDFPage* page = getPage();
    const QRectF pageRect = getPageRect();
    if (!page || pageRect.isEmpty())
    {
        return;
    }

    // The pixels of the screen, smaller only over the limit of the memory
    double ratio = devicePixelRatioF();
    const double area = pageRect.width() * pageRect.height();
    if (area * ratio * ratio > double(MAXIMUM_PIXELS))
    {
        ratio = std::sqrt(double(MAXIMUM_PIXELS) / area);
    }

    std::shared_ptr<PDFAppToolPageStageJob> job = std::make_shared<PDFAppToolPageStageJob>();
    job->link = m_link;
    job->document = m_document;
    job->pageIndex = m_pageIndex;
    job->pageRect = pageRect;
    job->imageSize = QSize(qMax(1, qCeil(pageRect.width() * ratio)), qMax(1, qCeil(pageRect.height() * ratio)));
    job->features = PDFAppPageRenderContext::getDefaultFeatures();
    job->paperColor = Qt::white;
    if (m_proxy)
    {
        // The settings of the view are taken here, in the thread of the user interface
        job->cmsSettings = m_proxy->getCMSManager()->getSettings();
        job->features = m_proxy->getFeatures();
        job->paperColor = m_proxy->getPaperColor();
    }
    if (m_shownBox != PDFPageGeometry::Box::CropBox)
    {
        // The whole shown box is drawn, also outside of the crop box
        job->features.setFlag(PDFRenderer::ClipToCropBox, false);
    }

    const QRectF rotatedBox = PDFPage::getRotatedBox(getShownBoxRect(), page->getPageRotation());
    job->pageToImage = PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedBox, QRectF(QPointF(0.0, 0.0), QSizeF(job->imageSize)), page->getPageRotation());

    m_job = job;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppToolPageStageTask(std::move(job)));
}

void PDFAppToolPageStage::onRenderFinished(const std::shared_ptr<PDFAppToolPageStageJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    // A job of another page or size
    if (job != m_job || job->isOperationCancelled())
    {
        return;
    }
    m_job.reset();

    m_image = std::move(job->image);
    m_imagePageRect = job->pageRect;
    m_imagePageIndex = job->pageIndex;
    m_imageDocument = job->document;
    ++m_finishedRenderCount;
    update();
}

void PDFAppToolPageStage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const QRectF sheetRect = getSheetRect();
    const QRectF pageRect = getPageRect();
    if (sheetRect.isEmpty())
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The paper with its shadow (337:2138), the picture of the page on it
    PDFStylePainter::shadow(&painter, sheetRect, PDFStylePainter::Radii(0), QPointF(0, 2), 8, PDFStylePainter::color(PDFThemeColors::EffectShadow));
    if (isImageShown())
    {
        // An image (336:3159): its picture fills the rectangle of its pixels, no paper
        PDFStylePainter::fillRect(&painter, sheetRect, PDFThemeColors::ReadingPaperPage);
        if (!m_sourcePicture.isNull())
        {
            painter.drawImage(pageRect, m_sourcePicture);
        }
        return;
    }
    PDFStylePainter::fillRect(&painter, sheetRect, m_proxy ? m_proxy->getPaperColor() : QColor(Qt::white));
    if (!m_image.isNull() && m_imagePageIndex == m_pageIndex && m_imageDocument == m_document)
    {
        // An older picture is scaled until the new one is rendered
        painter.drawImage(pageRect, m_image);
    }
}

// --------------------------------------------------------------- PDFAppToolPageCanvas

PDFAppToolPageCanvas::PDFAppToolPageCanvas(int stageCount, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPageCanvas"));

    // document-canvas 389:22382: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar: the title Semi Bold 13 text/primary, the segments Fit / 100 %
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
    m_zoomControl = new PDFSegmentedControl(bar);
    m_zoomControl->setObjectName(QStringLiteral("toolPreviewZoom"));
    m_zoomControl->addSegment(QString());
    m_zoomControl->addSegment(QString());
    m_zoomControl->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    connect(m_zoomControl.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolPageCanvas::onZoomIndexChanged);
    barLayout->addWidget(m_zoomControl);
    layout->addWidget(bar);

    // The pages: 100 % scrolls
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("toolPageArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setProperty("overlayScrollBars", true);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    QWidget* content = new QWidget(m_scrollArea);
    content->setObjectName(QStringLiteral("toolPageAreaContent"));
    content->setAutoFillBackground(false);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(GAP);

    QHBoxLayout* columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);
    const int count = qBound(1, stageCount, 2);
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            columns->addWidget(new PDFAppToolPageArrow(content));
        }

        QVBoxLayout* column = new QVBoxLayout();
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        PDFLabel* stageTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, content);
        stageTitle->setObjectName(QStringLiteral("toolStageTitle%1").arg(i));
        stageTitle->setAlignment(Qt::AlignHCenter);
        stageTitle->setVisible(false);
        column->addWidget(stageTitle);
        PDFAppToolPageStage* stage = new PDFAppToolPageStage(content);
        stage->setObjectName(QStringLiteral("toolPageStage%1").arg(i));
        column->addWidget(stage, 1);
        PDFLabel* caption = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, content);
        caption->setObjectName(QStringLiteral("toolStageCaption%1").arg(i));
        caption->setAlignment(Qt::AlignHCenter);
        caption->setWordWrap(true);
        column->addWidget(caption);
        columns->addLayout(column, 1);

        m_stages.emplace_back(stage);
        m_stageTitles.emplace_back(stageTitle);
        m_captions.emplace_back(caption);
    }
    contentLayout->addLayout(columns, 1);

    m_bottomHolder = new QWidget(content);
    m_bottomHolder->setObjectName(QStringLiteral("toolStageBottom"));
    QVBoxLayout* bottomLayout = new QVBoxLayout(m_bottomHolder);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_bottomHolder->setVisible(false);
    contentLayout->addWidget(m_bottomHolder);

    m_scrollArea->setWidget(content);
    layout->addWidget(m_scrollArea, 1);
    updateTexts();
}

PDFAppToolPageCanvas::~PDFAppToolPageCanvas() = default;

void PDFAppToolPageCanvas::setTitle(const QString& title)
{
    m_title->setText(title);
}

void PDFAppToolPageCanvas::setStageTitle(int index, const QString& title)
{
    if (PDFLabel* label = getStageTitleLabel(index))
    {
        label->setText(title);
        label->setVisible(!title.isEmpty());
    }
}

void PDFAppToolPageCanvas::setCaption(int index, const QString& caption, bool isError)
{
    if (PDFLabel* label = getCaptionLabel(index))
    {
        label->setText(caption);
        label->setTextColor(isError ? PDFThemeColors::TextDanger : PDFThemeColors::TextSecondary);
    }
}

void PDFAppToolPageCanvas::setBottomWidget(QWidget* widget)
{
    if (!m_bottomHolder || !widget)
    {
        return;
    }
    widget->setParent(m_bottomHolder);
    m_bottomHolder->layout()->addWidget(widget);
    m_bottomHolder->setVisible(true);
}

void PDFAppToolPageCanvas::setZoom(PDFAppToolPageStage::Zoom zoom)
{
    // The segments write the mode (onZoomIndexChanged)
    m_zoomControl->setCurrentIndex(zoom == PDFAppToolPageStage::Zoom::Fit ? 0 : 1);
    onZoomIndexChanged(m_zoomControl->getCurrentIndex());
}

PDFAppToolPageStage::Zoom PDFAppToolPageCanvas::getZoom() const
{
    return m_zoomControl && m_zoomControl->getCurrentIndex() == 1 ? PDFAppToolPageStage::Zoom::Actual : PDFAppToolPageStage::Zoom::Fit;
}

PDFAppToolPageStage* PDFAppToolPageCanvas::getStage(int index) const
{
    return index >= 0 && size_t(index) < m_stages.size() ? m_stages[size_t(index)].data() : nullptr;
}

PDFLabel* PDFAppToolPageCanvas::getStageTitleLabel(int index) const
{
    return index >= 0 && size_t(index) < m_stageTitles.size() ? m_stageTitles[size_t(index)].data() : nullptr;
}

PDFLabel* PDFAppToolPageCanvas::getCaptionLabel(int index) const
{
    return index >= 0 && size_t(index) < m_captions.size() ? m_captions[size_t(index)].data() : nullptr;
}

void PDFAppToolPageCanvas::onZoomIndexChanged(int index)
{
    const PDFAppToolPageStage::Zoom zoom = index == 1 ? PDFAppToolPageStage::Zoom::Actual : PDFAppToolPageStage::Zoom::Fit;
    for (const QPointer<PDFAppToolPageStage>& stage : m_stages)
    {
        if (stage)
        {
            stage->setZoom(zoom);
        }
    }
}

void PDFAppToolPageCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

void PDFAppToolPageCanvas::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppToolPageCanvas::updateTexts()
{
    const QStringList segments = { tr("Fit"), tr("100 %") };
    const QList<QToolButton*> buttons = m_zoomControl->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (qsizetype i = 0; i < buttons.size() && i < segments.size(); ++i)
    {
        buttons[i]->setText(segments[i]);
    }
    m_zoomControl->setAccessibleName(tr("Zoom of the page"));
}

}   // namespace pdf

#include "moc_pdfapptoolpagestage.cpp"
