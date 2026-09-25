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

#include "pdfscanpreparationdialog.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrjobcontroller.h"
#include "pdfdrawspacecontroller.h"
#include "pdfoptionalcontent.h"
#include "pdfwidgetutils.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdffont.h"
#include "pdfconstants.h"

#include <QMenu>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>
#include <QTableWidget>
#include <QProgressBar>
#include <QApplication>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QtConcurrent/QtConcurrent>

#include <cmath>

namespace pdfviewer
{

static constexpr double MM_TO_POINT = 72.0 / 25.4;
static constexpr double VIEW_DPI = 100.0;
static constexpr size_t IMAGE_CACHE_SIZE = 8;
static constexpr double HANDLE_SIZE = 8.0;

static QString getSettingsGroup()
{
    return QStringLiteral("ScanPreparationDialog");
}

// -------------------------------------------------------------------------
// PDFScanPreparationPageView
// -------------------------------------------------------------------------

PDFScanPreparationPageView::PDFScanPreparationPageView(QWidget* parent) :
    BaseClass(parent)
{
    setMouseTracking(true);
    setMinimumSize(300, 300);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Page with the plan of the preparation"));
}

void PDFScanPreparationPageView::setPage(QImage image, QSizeF visibleSize)
{
    m_image = std::move(image);
    m_visibleSize = visibleSize;
    m_message.clear();
    update();
}

void PDFScanPreparationPageView::clear(const QString& message)
{
    m_image = QImage();
    m_message = message;
    update();
}

void PDFScanPreparationPageView::setOverlay(const Overlay& overlay)
{
    m_overlay = overlay;
    update();
}

void PDFScanPreparationPageView::setResult(std::vector<QImage> images, const QString& message)
{
    m_resultImages = std::move(images);
    m_resultMessage = message;
    update();
}

void PDFScanPreparationPageView::setShowResult(bool showResult)
{
    m_showResult = showResult;
    update();
}

void PDFScanPreparationPageView::setZoom(double zoom)
{
    m_zoom = qBound(0.1, zoom, 16.0);
    if (qFuzzyCompare(m_zoom, 1.0))
    {
        m_offset = QPointF();
    }
    update();
}

QTransform PDFScanPreparationPageView::getPageToWidget() const
{
    if (!m_visibleSize.isValid() || m_visibleSize.isEmpty())
    {
        return QTransform();
    }

    const double fit = qMin((width() - 16.0) / m_visibleSize.width(), (height() - 16.0) / m_visibleSize.height());
    const double scale = qMax(0.01, fit * m_zoom);
    const QSizeF scaled = m_visibleSize * scale;
    const QPointF offset((width() - scaled.width()) * 0.5 + m_offset.x(), (height() - scaled.height()) * 0.5 + m_offset.y());
    return QTransform::fromScale(scale, scale) * QTransform::fromTranslate(offset.x(), offset.y());
}

std::vector<QPointF> PDFScanPreparationPageView::getHandles(const QRectF& rect) const
{
    const QRectF widgetRect = getPageToWidget().mapRect(rect);
    return { widgetRect.topLeft(), QPointF(widgetRect.center().x(), widgetRect.top()), widgetRect.topRight(),
             QPointF(widgetRect.right(), widgetRect.center().y()), widgetRect.bottomRight(),
             QPointF(widgetRect.center().x(), widgetRect.bottom()), widgetRect.bottomLeft(),
             QPointF(widgetRect.left(), widgetRect.center().y()) };
}

void PDFScanPreparationPageView::paintResult(QPainter& painter)
{
    if (m_resultImages.empty())
    {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_resultMessage.isEmpty() ? tr("The result is being prepared...") : m_resultMessage);
        return;
    }

    // Output pages side by side, scaled to fit
    constexpr double gap = 16.0;
    double totalWidth = 0.0;
    double maxHeight = 0.0;
    for (const QImage& image : m_resultImages)
    {
        totalWidth += image.width();
        maxHeight = qMax(maxHeight, double(image.height()));
    }
    totalWidth += gap * (m_resultImages.size() - 1);
    const double scale = qMin((width() - 16.0) / totalWidth, (height() - 40.0) / maxHeight) * m_zoom;

    double x = (width() - totalWidth * scale) * 0.5 + m_offset.x();
    int index = 0;
    for (const QImage& image : m_resultImages)
    {
        const QRectF target(x, 24.0 + (height() - 24.0 - image.height() * scale) * 0.5 + m_offset.y(), image.width() * scale, image.height() * scale);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, image);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.drawRect(target);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(target.left(), target.top() - 20, target.width(), 18), Qt::AlignCenter, tr("Output page %1").arg(++index));
        x += (image.width() + gap) * scale;
    }

    if (!m_resultMessage.isEmpty())
    {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(0, height() - 20, width(), 18), Qt::AlignCenter, m_resultMessage);
    }
}

void PDFScanPreparationPageView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Dark));

    if (m_showResult)
    {
        paintResult(painter);
        return;
    }

    if (m_image.isNull())
    {
        painter.setPen(palette().color(QPalette::BrightText));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message.isEmpty() ? tr("Rendering the page...") : m_message);
        return;
    }

    const QTransform pageToWidget = getPageToWidget();
    const QRectF pageRect(QPointF(0, 0), m_visibleSize);
    const QRectF pageWidgetRect = pageToWidget.mapRect(pageRect);
    painter.fillRect(pageWidgetRect, Qt::white);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Page image, each region rotated by its deskew around its center (live preview)
    const std::vector<QRectF> regions = m_overlay.regions.empty() ? std::vector<QRectF>{ pageRect } : m_overlay.regions;
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const double angle = i < m_overlay.angles.size() ? m_overlay.angles[i] : 0.0;
        const QRectF region = pageToWidget.mapRect(regions[i]);

        painter.save();
        painter.setClipRect(region);
        painter.translate(region.center());
        painter.rotate(-angle);
        painter.translate(-region.center());
        painter.drawImage(pageWidgetRect, m_image);
        painter.restore();
    }

    // Auxiliary grid (horizontal lines of the text must be parallel with it)
    if (m_overlay.showGrid && m_overlay.gridSpacing > 1.0)
    {
        painter.setPen(QPen(QColor(0, 150, 220, 140), 1.0, Qt::DashLine));
        for (double y = m_overlay.gridSpacing; y < m_visibleSize.height(); y += m_overlay.gridSpacing)
        {
            painter.drawLine(pageToWidget.map(QPointF(0, y)), pageToWidget.map(QPointF(m_visibleSize.width(), y)));
        }
        for (double x = m_overlay.gridSpacing; x < m_visibleSize.width(); x += m_overlay.gridSpacing)
        {
            painter.drawLine(pageToWidget.map(QPointF(x, 0)), pageToWidget.map(QPointF(x, m_visibleSize.height())));
        }
    }

    // Outside of the crops is darkened
    QPainterPath outside;
    outside.addRect(pageWidgetRect);
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const QRectF crop = (i < m_overlay.crops.size() && !m_overlay.crops[i].isEmpty()) ? m_overlay.crops[i] : regions[i];
        QPainterPath inside;
        inside.addRect(pageToWidget.mapRect(crop));
        outside = outside.subtracted(inside);
    }
    painter.fillPath(outside, QColor(0, 0, 0, 110));

    // Crop rectangles with their size
    painter.setBrush(Qt::NoBrush);
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const QRectF crop = (i < m_overlay.crops.size() && !m_overlay.crops[i].isEmpty()) ? m_overlay.crops[i] : regions[i];
        const QRectF widgetCrop = pageToWidget.mapRect(crop);
        painter.setPen(QPen(QColor(0, 160, 60), 2.0));
        painter.drawRect(widgetCrop);

        const QString size = tr("%1 x %2 mm").arg(crop.width() / MM_TO_POINT, 0, 'f', 0).arg(crop.height() / MM_TO_POINT, 0, 'f', 0);
        const QRectF textRect = painter.fontMetrics().boundingRect(size).adjusted(-4, -2, 4, 2);
        const QRectF labelRect(widgetCrop.left() + 4, widgetCrop.top() + 4, textRect.width(), textRect.height());
        painter.fillRect(labelRect, QColor(0, 0, 0, 150));
        painter.setPen(Qt::white);
        painter.drawText(labelRect, Qt::AlignCenter, size);
    }

    if (m_overlay.cropEditable && !m_overlay.crops.empty() && !m_overlay.crops.front().isEmpty())
    {
        painter.setPen(QPen(QColor(0, 160, 60), 1.0));
        painter.setBrush(Qt::white);
        for (const QPointF& handle : getHandles(m_overlay.crops.front()))
        {
            painter.drawRect(QRectF(handle - QPointF(HANDLE_SIZE * 0.5, HANDLE_SIZE * 0.5), QSizeF(HANDLE_SIZE, HANDLE_SIZE)));
        }
    }

    // Split line with the removed gutter
    if (m_overlay.splitPosition)
    {
        const double position = *m_overlay.splitPosition;
        const double halfGutter = m_overlay.gutterWidth * 0.5;
        const QRectF gutter = m_overlay.splitVertical ? QRectF(position - halfGutter, 0, m_overlay.gutterWidth, m_visibleSize.height())
                                                      : QRectF(0, position - halfGutter, m_visibleSize.width(), m_overlay.gutterWidth);
        painter.fillRect(pageToWidget.mapRect(gutter), QColor(220, 60, 0, 70));
        painter.setPen(QPen(QColor(220, 60, 0), 2.0, Qt::DashLine));
        const QLineF line = m_overlay.splitVertical ? QLineF(position, 0, position, m_visibleSize.height()) : QLineF(0, position, m_visibleSize.width(), position);
        painter.drawLine(pageToWidget.map(line));
    }
}

void PDFScanPreparationPageView::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePosition = event->position().toPoint();
    if (m_showResult || m_image.isNull())
    {
        if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
        {
            m_drag = Drag::Pan;
        }
        return;
    }

    const QPointF position = event->position();
    const QTransform pageToWidget = getPageToWidget();

    if (event->button() == Qt::LeftButton)
    {
        // Split line has the precedence, then the handles and the inside of the crop
        if (m_overlay.splitPosition)
        {
            const double position0 = *m_overlay.splitPosition;
            const QLineF line = pageToWidget.map(m_overlay.splitVertical ? QLineF(position0, 0, position0, m_visibleSize.height()) : QLineF(0, position0, m_visibleSize.width(), position0));
            const double distance = m_overlay.splitVertical ? std::abs(position.x() - line.x1()) : std::abs(position.y() - line.y1());
            if (distance < 6.0)
            {
                m_drag = Drag::Split;
                return;
            }
        }

        if (m_overlay.cropEditable && !m_overlay.crops.empty() && !m_overlay.crops.front().isEmpty())
        {
            const std::vector<QPointF> handles = getHandles(m_overlay.crops.front());
            for (size_t i = 0; i < handles.size(); ++i)
            {
                if (QLineF(handles[i], position).length() < HANDLE_SIZE)
                {
                    m_drag = Drag::Handle;
                    m_dragHandle = int(i);
                    m_dragStart = pageToWidget.inverted().map(position);
                    m_dragStartRect = m_overlay.crops.front();
                    return;
                }
            }

            if (pageToWidget.mapRect(m_overlay.crops.front()).contains(position))
            {
                m_drag = Drag::Move;
                m_dragStart = pageToWidget.inverted().map(position);
                m_dragStartRect = m_overlay.crops.front();
                return;
            }
        }
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        m_drag = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
    }
}

void PDFScanPreparationPageView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint mousePosition = event->position().toPoint();
    const QTransform widgetToPage = getPageToWidget().inverted();
    const QPointF pagePoint = widgetToPage.map(event->position());
    const QRectF pageRect(QPointF(0, 0), m_visibleSize);

    switch (m_drag)
    {
        case Drag::Pan:
            m_offset += QPointF(mousePosition - m_lastMousePosition);
            update();
            break;

        case Drag::Split:
        {
            const double position = m_overlay.splitVertical ? qBound(1.0, pagePoint.x(), m_visibleSize.width() - 1.0) : qBound(1.0, pagePoint.y(), m_visibleSize.height() - 1.0);
            m_overlay.splitPosition = position;
            update();
            Q_EMIT splitPositionEdited(position);
            break;
        }

        case Drag::Handle:
        case Drag::Move:
        {
            QRectF rect = m_dragStartRect;
            const QPointF delta = pagePoint - m_dragStart;
            if (m_drag == Drag::Move)
            {
                rect.translate(delta);
                rect.moveLeft(qBound(0.0, rect.left(), m_visibleSize.width() - rect.width()));
                rect.moveTop(qBound(0.0, rect.top(), m_visibleSize.height() - rect.height()));
            }
            else
            {
                // Handles: 0 top-left, 1 top, 2 top-right, 3 right, 4 bottom-right, 5 bottom, 6 bottom-left, 7 left
                const int handle = m_dragHandle;
                if (handle == 0 || handle == 6 || handle == 7)
                {
                    rect.setLeft(qMin(rect.right() - 5.0, m_dragStartRect.left() + delta.x()));
                }
                if (handle == 2 || handle == 3 || handle == 4)
                {
                    rect.setRight(qMax(rect.left() + 5.0, m_dragStartRect.right() + delta.x()));
                }
                if (handle == 0 || handle == 1 || handle == 2)
                {
                    rect.setTop(qMin(rect.bottom() - 5.0, m_dragStartRect.top() + delta.y()));
                }
                if (handle == 4 || handle == 5 || handle == 6)
                {
                    rect.setBottom(qMax(rect.top() + 5.0, m_dragStartRect.bottom() + delta.y()));
                }
                rect = rect.intersected(pageRect);
            }

            if (!rect.isEmpty())
            {
                m_overlay.crops = { rect };
                update();
                Q_EMIT cropEdited(rect);
            }
            break;
        }

        case Drag::None:
        {
            // Cursor shows, what can be dragged
            Qt::CursorShape shape = Qt::ArrowCursor;
            if (!m_showResult && m_overlay.splitPosition)
            {
                const QTransform pageToWidget = getPageToWidget();
                const double position = *m_overlay.splitPosition;
                const QLineF line = pageToWidget.map(m_overlay.splitVertical ? QLineF(position, 0, position, m_visibleSize.height()) : QLineF(0, position, m_visibleSize.width(), position));
                const double distance = m_overlay.splitVertical ? std::abs(event->position().x() - line.x1()) : std::abs(event->position().y() - line.y1());
                if (distance < 6.0)
                {
                    shape = m_overlay.splitVertical ? Qt::SplitHCursor : Qt::SplitVCursor;
                }
            }
            if (shape == Qt::ArrowCursor && !m_showResult && m_overlay.cropEditable && !m_overlay.crops.empty() && !m_overlay.crops.front().isEmpty())
            {
                for (const QPointF& handle : getHandles(m_overlay.crops.front()))
                {
                    if (QLineF(handle, event->position()).length() < HANDLE_SIZE)
                    {
                        shape = Qt::SizeAllCursor;
                    }
                }
            }
            setCursor(shape);
            break;
        }
    }

    m_lastMousePosition = mousePosition;
}

void PDFScanPreparationPageView::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_drag = Drag::None;
    m_dragHandle = -1;
    setCursor(Qt::ArrowCursor);
}

void PDFScanPreparationPageView::wheelEvent(QWheelEvent* event)
{
    setZoom(m_zoom * (event->angleDelta().y() > 0 ? 1.25 : 0.8));
    event->accept();
}

// -------------------------------------------------------------------------
// PDFScanPreparationDialog
// -------------------------------------------------------------------------

PDFScanPreparationDialog::PDFScanPreparationDialog(const Context& context, QWidget* parent) :
    BaseClass(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMaximizeButtonHint),
    m_context(context)
{
    setWindowTitle(tr("Prepare Scanned Pages"));
    setObjectName(QStringLiteral("PDFScanPreparationDialog"));

    m_pageCount = m_context.document ? pdf::PDFInteger(m_context.document->getCatalog()->getPageCount()) : 0;
    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        m_settings[page] = PageSettings();
    }

    // The visible configuration of the optional content is taken from the view
    m_optionalContentActivity = new pdf::PDFOptionalContentActivity(m_context.document, pdf::OCUsage::View, this);
    if (m_context.proxy)
    {
        if (const pdf::PDFOptionalContentActivity* activity = m_context.proxy->getOptionalContentActivity())
        {
            for (const pdf::PDFObjectReference& ocg : m_context.document->getCatalog()->getOptionalContentProperties()->getAllOptionalContentGroups())
            {
                m_optionalContentActivity->setState(ocg, activity->getState(ocg), false);
            }
        }
        m_context.proxy->getFontCache()->setCacheShrinkEnabled(this, false);
    }

    createUi();
    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1300, 850));
    loadSettings();

    m_resultTimer.setSingleShot(true);
    m_resultTimer.setInterval(300);
    connect(&m_resultTimer, &QTimer::timeout, this, &PDFScanPreparationDialog::startResult);

    setCurrentPage(qBound<pdf::PDFInteger>(0, m_context.currentPage, qMax<pdf::PDFInteger>(0, m_pageCount - 1)));
    updateSummary();
}

PDFScanPreparationDialog::~PDFScanPreparationDialog()
{
    stopAnalysis();
    if (m_resultToken)
    {
        m_resultToken->cancel();
    }
    for (QFuture<void>& future : m_futures)
    {
        future.waitForFinished();
    }

    if (m_context.proxy)
    {
        m_context.proxy->getFontCache()->setCacheShrinkEnabled(this, true);
    }
}

void PDFScanPreparationDialog::createUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    mainLayout->addWidget(splitter, 1);

    // Pages ----------------------------------------------------------------
    QWidget* pagesWidget = new QWidget(splitter);
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesWidget);
    pagesLayout->setContentsMargins(0, 0, 0, 0);
    m_pageListWidget = new QListWidget(pagesWidget);
    m_pageListWidget->setObjectName(QStringLiteral("pageListWidget"));
    m_pageListWidget->setAccessibleName(tr("Pages"));
    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        QListWidgetItem* item = new QListWidgetItem(m_pageListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, QVariant::fromValue(page));
    }
    QHBoxLayout* checkLayout = new QHBoxLayout();
    QPushButton* allButton = new QPushButton(tr("All"), pagesWidget);
    allButton->setObjectName(QStringLiteral("allButton"));
    QPushButton* noneButton = new QPushButton(tr("None"), pagesWidget);
    noneButton->setObjectName(QStringLiteral("noneButton"));
    QPushButton* invertButton = new QPushButton(tr("Invert"), pagesWidget);
    invertButton->setObjectName(QStringLiteral("invertButton"));
    checkLayout->addWidget(allButton);
    checkLayout->addWidget(noneButton);
    checkLayout->addWidget(invertButton);
    m_pageFilterComboBox = new QComboBox(pagesWidget);
    m_pageFilterComboBox->setObjectName(QStringLiteral("pageFilterComboBox"));
    m_pageFilterComboBox->setAccessibleName(tr("Filter of the pages"));
    m_pageFilterComboBox->addItem(tr("All pages"), 0);
    m_pageFilterComboBox->addItem(tr("Pages with changes"), 1);
    m_pageFilterComboBox->addItem(tr("Skew above..."), 2);
    m_pageFilterComboBox->addItem(tr("Pages with warnings"), 3);
    m_filterSkewSpinBox = new QDoubleSpinBox(pagesWidget);
    m_filterSkewSpinBox->setObjectName(QStringLiteral("filterSkewSpinBox"));
    m_filterSkewSpinBox->setAccessibleName(tr("Minimal skew of the filter"));
    m_filterSkewSpinBox->setRange(0.0, 10.0);
    m_filterSkewSpinBox->setSingleStep(0.1);
    m_filterSkewSpinBox->setValue(0.5);
    m_filterSkewSpinBox->setSuffix(QStringLiteral(" °"));
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(m_pageFilterComboBox, 1);
    filterLayout->addWidget(m_filterSkewSpinBox);
    pagesLayout->addWidget(m_pageListWidget, 1);
    pagesLayout->addLayout(checkLayout);
    pagesLayout->addLayout(filterLayout);
    splitter->addWidget(pagesWidget);

    // View and table --------------------------------------------------------
    QWidget* centerWidget = new QWidget(splitter);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout* viewToolLayout = new QHBoxLayout();
    m_resultViewButton = new QToolButton(centerWidget);
    m_resultViewButton->setObjectName(QStringLiteral("resultViewButton"));
    m_resultViewButton->setText(tr("Result"));
    m_resultViewButton->setCheckable(true);
    m_resultViewButton->setToolTip(tr("Show the output pages of the current page, rendered from the really prepared document"));
    m_tableViewButton = new QToolButton(centerWidget);
    m_tableViewButton->setObjectName(QStringLiteral("tableViewButton"));
    m_tableViewButton->setText(tr("Table"));
    m_tableViewButton->setCheckable(true);
    m_tableViewButton->setToolTip(tr("Show all pages in a table, sort them by the skew and edit them together"));
    m_gridCheckBox = new QCheckBox(tr("Grid"), centerWidget);
    m_gridCheckBox->setObjectName(QStringLiteral("gridCheckBox"));
    m_gridSpacingSpinBox = new QDoubleSpinBox(centerWidget);
    m_gridSpacingSpinBox->setObjectName(QStringLiteral("gridSpacingSpinBox"));
    m_gridSpacingSpinBox->setAccessibleName(tr("Spacing of the grid"));
    m_gridSpacingSpinBox->setRange(2.0, 100.0);
    m_gridSpacingSpinBox->setValue(10.0);
    m_gridSpacingSpinBox->setSuffix(tr(" mm"));
    QLabel* keysLabel = new QLabel(tr("Keys [ and ] rotate by 0.05°, with Shift by 0.5°"), centerWidget);
    viewToolLayout->addWidget(m_resultViewButton);
    viewToolLayout->addWidget(m_tableViewButton);
    viewToolLayout->addSpacing(12);
    viewToolLayout->addWidget(m_gridCheckBox);
    viewToolLayout->addWidget(m_gridSpacingSpinBox);
    viewToolLayout->addStretch(1);
    viewToolLayout->addWidget(keysLabel);
    centerLayout->addLayout(viewToolLayout);

    m_centerStack = new QStackedWidget(centerWidget);
    m_view = new PDFScanPreparationPageView(m_centerStack);
    m_view->setObjectName(QStringLiteral("preparationPageView"));
    m_tableWidget = new QTableWidget(m_centerStack);
    m_tableWidget->setObjectName(QStringLiteral("tableWidget"));
    m_tableWidget->setColumnCount(7);
    m_tableWidget->setHorizontalHeaderLabels({ tr("Page"), tr("Class"), tr("Detected skew"), tr("Applied skew"), tr("Split"), tr("Crop"), tr("Warnings") });
    m_tableWidget->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSortingEnabled(true);
    m_centerStack->addWidget(m_view);
    m_centerStack->addWidget(m_tableWidget);
    centerLayout->addWidget(m_centerStack, 1);
    splitter->addWidget(centerWidget);

    // Tools ------------------------------------------------------------------
    QWidget* toolsWidget = new QWidget(splitter);
    QVBoxLayout* toolsLayout = new QVBoxLayout(toolsWidget);
    toolsLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* detectionGroupBox = new QGroupBox(tr("Detection"), toolsWidget);
    QVBoxLayout* detectionLayout = new QVBoxLayout(detectionGroupBox);
    m_analyzeButton = new QPushButton(tr("&Detect on Checked Pages"), detectionGroupBox);
    m_analyzeButton->setObjectName(QStringLiteral("analyzeButton"));
    m_analyzeButton->setToolTip(tr("Detect the skew, the content and the spine of the checked pages. The detected values are proposals; edited values are not overwritten."));
    m_analysisProgressBar = new QProgressBar(detectionGroupBox);
    m_analysisProgressBar->setObjectName(QStringLiteral("analysisProgressBar"));
    m_analysisProgressBar->setValue(0);
    m_stopButton = new QPushButton(tr("&Stop"), detectionGroupBox);
    m_stopButton->setObjectName(QStringLiteral("stopButton"));
    m_stopButton->setEnabled(false);
    QHBoxLayout* analysisLayout = new QHBoxLayout();
    analysisLayout->addWidget(m_analysisProgressBar, 1);
    analysisLayout->addWidget(m_stopButton);
    detectionLayout->addWidget(m_analyzeButton);
    detectionLayout->addLayout(analysisLayout);
    toolsLayout->addWidget(detectionGroupBox);

    QGroupBox* deskewGroupBox = new QGroupBox(tr("Straighten"), toolsWidget);
    QFormLayout* deskewLayout = new QFormLayout(deskewGroupBox);
    m_deskewCheckBox = new QCheckBox(tr("Straighten the page"), deskewGroupBox);
    m_deskewCheckBox->setObjectName(QStringLiteral("deskewCheckBox"));
    m_deskewAngleSpinBox = new QDoubleSpinBox(deskewGroupBox);
    m_deskewAngleSpinBox->setObjectName(QStringLiteral("deskewAngleSpinBox"));
    m_deskewAngleSpinBox->setAccessibleName(tr("Skew of the page"));
    m_deskewAngleSpinBox->setRange(-pdf::PDFScanPreparation::MaximumDeskewAngle, pdf::PDFScanPreparation::MaximumDeskewAngle);
    m_deskewAngleSpinBox->setDecimals(2);
    m_deskewAngleSpinBox->setSingleStep(0.05);
    m_deskewAngleSpinBox->setSuffix(QStringLiteral(" °"));
    m_deskewAngleSpinBox->setToolTip(tr("Skew of the content (positive = the lines descend to the right); the page is rotated by the opposite angle"));
    m_deskewDetectedLabel = new QLabel(deskewGroupBox);
    m_deskewDetectedLabel->setObjectName(QStringLiteral("deskewDetectedLabel"));
    m_deskewDetectedLabel->setWordWrap(true);
    m_deskewResetButton = new QPushButton(tr("Use Detected"), deskewGroupBox);
    m_deskewResetButton->setObjectName(QStringLiteral("deskewResetButton"));
    m_deskewMinimumSpinBox = new QDoubleSpinBox(deskewGroupBox);
    m_deskewMinimumSpinBox->setObjectName(QStringLiteral("deskewMinimumSpinBox"));
    m_deskewMinimumSpinBox->setAccessibleName(tr("Minimal angle of the deskew"));
    m_deskewMinimumSpinBox->setRange(0.0, 2.0);
    m_deskewMinimumSpinBox->setDecimals(2);
    m_deskewMinimumSpinBox->setSingleStep(0.05);
    m_deskewMinimumSpinBox->setValue(0.1);
    m_deskewMinimumSpinBox->setSuffix(QStringLiteral(" °"));
    m_deskewMinimumSpinBox->setToolTip(tr("Smaller angles are not corrected (all pages)"));
    deskewLayout->addRow(m_deskewCheckBox);
    deskewLayout->addRow(tr("Angle:"), m_deskewAngleSpinBox);
    deskewLayout->addRow(m_deskewDetectedLabel);
    deskewLayout->addRow(m_deskewResetButton);
    deskewLayout->addRow(tr("Ignore angles below:"), m_deskewMinimumSpinBox);
    toolsLayout->addWidget(deskewGroupBox);

    QGroupBox* splitGroupBox = new QGroupBox(tr("Split"), toolsWidget);
    QFormLayout* splitLayout = new QFormLayout(splitGroupBox);
    m_splitModeComboBox = new QComboBox(splitGroupBox);
    m_splitModeComboBox->setObjectName(QStringLiteral("splitModeComboBox"));
    m_splitModeComboBox->setAccessibleName(tr("Split of the page"));
    m_splitModeComboBox->addItem(tr("None"), int(SplitMode::None));
    m_splitModeComboBox->addItem(tr("Two pages side by side"), int(SplitMode::SideBySide));
    m_splitModeComboBox->addItem(tr("Two pages one above another"), int(SplitMode::OneAboveAnother));
    m_splitPositionSpinBox = new QDoubleSpinBox(splitGroupBox);
    m_splitPositionSpinBox->setObjectName(QStringLiteral("splitPositionSpinBox"));
    m_splitPositionSpinBox->setAccessibleName(tr("Position of the split line"));
    m_splitPositionSpinBox->setRange(0.0, 10000.0);
    m_splitPositionSpinBox->setDecimals(1);
    m_splitPositionSpinBox->setSuffix(tr(" mm"));
    m_gutterWidthSpinBox = new QDoubleSpinBox(splitGroupBox);
    m_gutterWidthSpinBox->setObjectName(QStringLiteral("gutterWidthSpinBox"));
    m_gutterWidthSpinBox->setAccessibleName(tr("Width of the gutter removed around the split line"));
    m_gutterWidthSpinBox->setRange(0.0, 200.0);
    m_gutterWidthSpinBox->setDecimals(1);
    m_gutterWidthSpinBox->setSuffix(tr(" mm"));
    m_readingOrderComboBox = new QComboBox(splitGroupBox);
    m_readingOrderComboBox->setObjectName(QStringLiteral("readingOrderComboBox"));
    m_readingOrderComboBox->setAccessibleName(tr("Order of the halves"));
    m_readingOrderComboBox->addItem(tr("Left page first"), false);
    m_readingOrderComboBox->addItem(tr("Right page first"), true);
    m_splitDetectedLabel = new QLabel(splitGroupBox);
    m_splitDetectedLabel->setObjectName(QStringLiteral("splitDetectedLabel"));
    m_splitDetectedLabel->setWordWrap(true);
    splitLayout->addRow(tr("Mode:"), m_splitModeComboBox);
    splitLayout->addRow(tr("Position:"), m_splitPositionSpinBox);
    splitLayout->addRow(tr("Gutter:"), m_gutterWidthSpinBox);
    splitLayout->addRow(tr("Order:"), m_readingOrderComboBox);
    splitLayout->addRow(m_splitDetectedLabel);
    toolsLayout->addWidget(splitGroupBox);

    QGroupBox* cropGroupBox = new QGroupBox(tr("Crop"), toolsWidget);
    QFormLayout* cropLayout = new QFormLayout(cropGroupBox);
    m_cropModeComboBox = new QComboBox(cropGroupBox);
    m_cropModeComboBox->setObjectName(QStringLiteral("cropModeComboBox"));
    m_cropModeComboBox->setAccessibleName(tr("Crop of the page"));
    m_cropModeComboBox->addItem(tr("None"), int(CropMode::None));
    m_cropModeComboBox->addItem(tr("Automatic to content"), int(CropMode::Automatic));
    m_cropModeComboBox->addItem(tr("Manual"), int(CropMode::Manual));
    m_cropModeComboBox->addItem(tr("Same size on all pages"), int(CropMode::SameSize));
    m_cropMarginSpinBox = new QDoubleSpinBox(cropGroupBox);
    m_cropMarginSpinBox->setObjectName(QStringLiteral("cropMarginSpinBox"));
    m_cropMarginSpinBox->setAccessibleName(tr("Margin around the content"));
    m_cropMarginSpinBox->setRange(0.0, 100.0);
    m_cropMarginSpinBox->setDecimals(1);
    m_cropMarginSpinBox->setSuffix(tr(" mm"));
    m_cropOddEvenCheckBox = new QCheckBox(tr("Separate for odd and even pages"), cropGroupBox);
    m_cropOddEvenCheckBox->setObjectName(QStringLiteral("cropOddEvenCheckBox"));
    m_cropOddEvenCheckBox->setToolTip(tr("The same size is computed separately for the odd and the even output pages (all pages)"));
    m_cropSizeLabel = new QLabel(cropGroupBox);
    m_cropSizeLabel->setObjectName(QStringLiteral("cropSizeLabel"));
    m_cropSizeLabel->setWordWrap(true);
    cropLayout->addRow(tr("Mode:"), m_cropModeComboBox);
    cropLayout->addRow(tr("Margin:"), m_cropMarginSpinBox);
    cropLayout->addRow(m_cropOddEvenCheckBox);
    cropLayout->addRow(m_cropSizeLabel);
    toolsLayout->addWidget(cropGroupBox);

    QHBoxLayout* batchLayout = new QHBoxLayout();
    m_applyToCheckedButton = new QToolButton(toolsWidget);
    m_applyToCheckedButton->setObjectName(QStringLiteral("applyToCheckedButton"));
    m_applyToCheckedButton->setText(tr("Apply to Checked Pages"));
    m_applyToCheckedButton->setPopupMode(QToolButton::InstantPopup);
    m_applyToCheckedButton->setToolTip(tr("Copy the settings of the current page to the checked pages"));
    QMenu* applyMenu = new QMenu(m_applyToCheckedButton);
    connect(applyMenu->addAction(tr("Straightening")), &QAction::triggered, this, [this]() { applyToChecked(true, false, false); });
    connect(applyMenu->addAction(tr("Split")), &QAction::triggered, this, [this]() { applyToChecked(false, true, false); });
    connect(applyMenu->addAction(tr("Crop")), &QAction::triggered, this, [this]() { applyToChecked(false, false, true); });
    connect(applyMenu->addAction(tr("All")), &QAction::triggered, this, [this]() { applyToChecked(true, true, true); });
    m_applyToCheckedButton->setMenu(applyMenu);
    m_resetPageButton = new QPushButton(tr("Reset Page"), toolsWidget);
    m_resetPageButton->setObjectName(QStringLiteral("resetPageButton"));
    m_resetPageButton->setToolTip(tr("Reset the settings of the current page to the detected proposals"));
    batchLayout->addWidget(m_applyToCheckedButton);
    batchLayout->addWidget(m_resetPageButton);
    toolsLayout->addLayout(batchLayout);
    toolsLayout->addStretch(1);
    splitter->addWidget(toolsWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);

    // Summary and buttons ------------------------------------------------------
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("summaryLabel"));
    m_summaryLabel->setWordWrap(true);
    m_warningLabel = new QLabel(this);
    m_warningLabel->setObjectName(QStringLiteral("warningLabel"));
    m_warningLabel->setWordWrap(true);
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Apply)->setText(tr("&Apply"));
    m_buttonBox->button(QDialogButtonBox::Apply)->setObjectName(QStringLiteral("applyButton"));
    mainLayout->addWidget(m_summaryLabel);
    mainLayout->addWidget(m_warningLabel);
    mainLayout->addWidget(m_buttonBox);

    // Connections ---------------------------------------------------------------
    connect(m_pageListWidget, &QListWidget::currentRowChanged, this, [this](int row)
    {
        if (!m_updatingUi && row >= 0)
        {
            setCurrentPage(m_pageListWidget->item(row)->data(Qt::UserRole).toLongLong());
        }
    });
    connect(m_pageListWidget, &QListWidget::itemChanged, this, [this]()
    {
        if (!m_updatingUi)
        {
            m_sameSizeCacheValid = false;
            updateSummary();
            updateView();
        }
    });
    auto setChecked = [this](int mode)
    {
        m_updatingUi = true;
        for (int i = 0; i < m_pageListWidget->count(); ++i)
        {
            QListWidgetItem* item = m_pageListWidget->item(i);
            if (item->isHidden())
            {
                continue;
            }
            const bool checked = mode == 0 ? true : (mode == 1 ? false : item->checkState() != Qt::Checked);
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
        m_updatingUi = false;
        m_sameSizeCacheValid = false;
        updateSummary();
        updateView();
    };
    connect(allButton, &QPushButton::clicked, this, [setChecked]() { setChecked(0); });
    connect(noneButton, &QPushButton::clicked, this, [setChecked]() { setChecked(1); });
    connect(invertButton, &QPushButton::clicked, this, [setChecked]() { setChecked(2); });
    connect(m_pageFilterComboBox, &QComboBox::currentIndexChanged, this, &PDFScanPreparationDialog::updatePageFilter);
    connect(m_filterSkewSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFScanPreparationDialog::updatePageFilter);

    connect(m_analyzeButton, &QPushButton::clicked, this, &PDFScanPreparationDialog::startAnalysis);
    connect(m_stopButton, &QPushButton::clicked, this, &PDFScanPreparationDialog::stopAnalysis);

    for (QCheckBox* checkBox : { m_deskewCheckBox, m_cropOddEvenCheckBox })
    {
        connect(checkBox, &QCheckBox::toggled, this, &PDFScanPreparationDialog::onControlsChanged);
    }
    for (QDoubleSpinBox* spinBox : { m_deskewAngleSpinBox, m_deskewMinimumSpinBox, m_splitPositionSpinBox, m_gutterWidthSpinBox, m_cropMarginSpinBox })
    {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, &PDFScanPreparationDialog::onControlsChanged);
    }
    for (QComboBox* comboBox : { m_splitModeComboBox, m_readingOrderComboBox, m_cropModeComboBox })
    {
        connect(comboBox, &QComboBox::currentIndexChanged, this, &PDFScanPreparationDialog::onControlsChanged);
    }
    connect(m_deskewResetButton, &QPushButton::clicked, this, [this]()
    {
        auto it = m_analysis.find(m_currentPage);
        if (it != m_analysis.end())
        {
            PageSettings& settings = getEditableSettings(m_currentPage);
            settings.deskew = true;
            settings.deskewAngle = it->second.skewAngle;
            settings.deskewEdited = false;
            updateControls();
            updatePageItem(m_currentPage);
            updateView();
            updateSummary();
        }
    });
    connect(m_resetPageButton, &QPushButton::clicked, this, &PDFScanPreparationDialog::resetPage);
    connect(m_gridCheckBox, &QCheckBox::toggled, this, &PDFScanPreparationDialog::updateView);
    connect(m_gridSpacingSpinBox, &QDoubleSpinBox::valueChanged, this, &PDFScanPreparationDialog::updateView);
    connect(m_resultViewButton, &QToolButton::toggled, this, [this](bool checked)
    {
        m_view->setShowResult(checked);
        if (checked)
        {
            m_centerStack->setCurrentWidget(m_view);
            m_tableViewButton->setChecked(false);
            startResult();
        }
    });
    connect(m_tableViewButton, &QToolButton::toggled, this, [this](bool checked)
    {
        m_centerStack->setCurrentWidget(checked ? static_cast<QWidget*>(m_tableWidget) : static_cast<QWidget*>(m_view));
        if (checked)
        {
            m_resultViewButton->setChecked(false);
            updateTable();
        }
    });
    connect(m_tableWidget, &QTableWidget::cellChanged, this, &PDFScanPreparationDialog::onTableItemChanged);
    connect(m_tableWidget, &QTableWidget::currentCellChanged, this, [this](int row)
    {
        if (!m_updatingUi && row >= 0 && m_tableWidget->item(row, 0))
        {
            setCurrentPage(m_tableWidget->item(row, 0)->data(Qt::UserRole).toLongLong());
        }
    });
    connect(m_view, &PDFScanPreparationPageView::cropEdited, this, [this](QRectF crop)
    {
        PageSettings& settings = getEditableSettings(m_currentPage);
        settings.cropMode = CropMode::Manual;
        settings.manualCrop = crop;
        m_updatingUi = true;
        m_cropModeComboBox->setCurrentIndex(m_cropModeComboBox->findData(int(CropMode::Manual)));
        m_updatingUi = false;
        updatePageItem(m_currentPage);
        updateSummary();
        m_cropSizeLabel->setText(tr("%1 x %2 mm").arg(crop.width() / MM_TO_POINT, 0, 'f', 1).arg(crop.height() / MM_TO_POINT, 0, 'f', 1));
    });
    connect(m_view, &PDFScanPreparationPageView::splitPositionEdited, this, [this](double position)
    {
        PageSettings& settings = getEditableSettings(m_currentPage);
        settings.splitPosition = position;
        settings.splitEdited = true;
        m_updatingUi = true;
        m_splitPositionSpinBox->setValue(position / MM_TO_POINT);
        m_updatingUi = false;
        m_sameSizeCacheValid = false;
        updateSummary();
        updateView();
    });

    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PDFScanPreparationDialog::onApply);

    // Keys [ and ] rotate the current page (Shift = a larger step)
    new QShortcut(QKeySequence(Qt::Key_BracketLeft), this, [this]() { rotateCurrentPage(-0.05); });
    new QShortcut(QKeySequence(Qt::Key_BracketRight), this, [this]() { rotateCurrentPage(0.05); });
    new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), this, [this]() { rotateCurrentPage(-0.5); });
    new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), this, [this]() { rotateCurrentPage(0.5); });
    new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BraceLeft), this, [this]() { rotateCurrentPage(-0.5); });
    new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BraceRight), this, [this]() { rotateCurrentPage(0.5); });

    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        updatePageItem(page);
    }
}

void PDFScanPreparationDialog::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());

    // The last used modes and margins are the defaults of the pages
    m_updatingUi = true;
    m_deskewMinimumSpinBox->setValue(settings.value(QStringLiteral("deskewMinimum"), 0.1).toDouble());
    m_gridCheckBox->setChecked(settings.value(QStringLiteral("grid"), false).toBool());
    m_gridSpacingSpinBox->setValue(settings.value(QStringLiteral("gridSpacing"), 10.0).toDouble());
    m_cropOddEvenCheckBox->setChecked(settings.value(QStringLiteral("cropOddEven"), false).toBool());
    PageSettings defaults;
    defaults.cropMode = CropMode(qBound(0, settings.value(QStringLiteral("cropMode"), int(CropMode::None)).toInt(), int(CropMode::SameSize)));
    if (defaults.cropMode == CropMode::Manual)
    {
        defaults.cropMode = CropMode::None;
    }
    defaults.cropMargin = settings.value(QStringLiteral("cropMargin"), 5.0).toDouble() * MM_TO_POINT;
    defaults.gutterWidth = settings.value(QStringLiteral("gutterWidth"), 0.0).toDouble() * MM_TO_POINT;
    defaults.rightToLeft = settings.value(QStringLiteral("rightToLeft"), false).toBool();
    settings.endGroup();
    m_updatingUi = false;

    for (auto& [page, pageSettings] : m_settings)
    {
        pageSettings.cropMode = defaults.cropMode;
        pageSettings.cropMargin = defaults.cropMargin;
        pageSettings.gutterWidth = defaults.gutterWidth;
        pageSettings.rightToLeft = defaults.rightToLeft;
    }
}

void PDFScanPreparationDialog::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("deskewMinimum"), m_deskewMinimumSpinBox->value());
    settings.setValue(QStringLiteral("grid"), m_gridCheckBox->isChecked());
    settings.setValue(QStringLiteral("gridSpacing"), m_gridSpacingSpinBox->value());
    settings.setValue(QStringLiteral("cropOddEven"), m_cropOddEvenCheckBox->isChecked());
    if (m_currentPage >= 0)
    {
        const PageSettings& current = getPageSettings(m_currentPage);
        settings.setValue(QStringLiteral("cropMode"), int(current.cropMode));
        settings.setValue(QStringLiteral("cropMargin"), current.cropMargin / MM_TO_POINT);
        settings.setValue(QStringLiteral("gutterWidth"), current.gutterWidth / MM_TO_POINT);
        settings.setValue(QStringLiteral("rightToLeft"), current.rightToLeft);
    }
    settings.endGroup();
}

void PDFScanPreparationDialog::done(int result)
{
    stopAnalysis();
    saveSettings();
    BaseClass::done(result);
}

// -------------------------------------------------------------------------
// Pages
// -------------------------------------------------------------------------

const PDFScanPreparationDialog::PageSettings& PDFScanPreparationDialog::getPageSettings(pdf::PDFInteger pageIndex) const
{
    static const PageSettings defaultSettings;
    auto it = m_settings.find(pageIndex);
    return it != m_settings.end() ? it->second : defaultSettings;
}

PDFScanPreparationDialog::PageSettings& PDFScanPreparationDialog::getEditableSettings(pdf::PDFInteger pageIndex)
{
    m_sameSizeCacheValid = false;
    return m_settings[pageIndex];
}

QSizeF PDFScanPreparationDialog::getVisibleSize(pdf::PDFInteger pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageCount)
    {
        return QSizeF();
    }
    return pdf::PDFScanPreparation::getVisibleSize(m_context.document->getCatalog()->getPage(pageIndex));
}

std::vector<pdf::PDFInteger> PDFScanPreparationDialog::getCheckedPages() const
{
    std::vector<pdf::PDFInteger> pages;
    for (int i = 0; i < m_pageListWidget->count(); ++i)
    {
        if (m_pageListWidget->item(i)->checkState() == Qt::Checked)
        {
            pages.push_back(m_pageListWidget->item(i)->data(Qt::UserRole).toLongLong());
        }
    }
    return pages;
}

void PDFScanPreparationDialog::updatePageItem(pdf::PDFInteger pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pageListWidget->count())
    {
        return;
    }

    // Badges: skew, split, crop, warnings
    const PageSettings& settings = getPageSettings(pageIndex);
    QStringList badges;
    const double angle = getEffectiveAngle(settings);
    if (!qFuzzyIsNull(angle))
    {
        badges << QStringLiteral("⟲ %1°").arg(angle, 0, 'f', 2);
    }
    if (settings.splitMode != SplitMode::None)
    {
        badges << QStringLiteral("▯▯");
    }
    if (settings.cropMode != CropMode::None)
    {
        badges << QStringLiteral("✂");
    }

    QStringList warnings;
    auto it = m_analysis.find(pageIndex);
    if (it != m_analysis.end())
    {
        const pdf::PDFScanPreparation::PageAnalysis& analysis = it->second;
        if (analysis.hasOwnOCRLayer)
        {
            warnings << tr("The page has an OCR layer of PDF4QT.");
        }
        if (analysis.isTagged && settings.splitMode != SplitMode::None)
        {
            warnings << tr("A tagged page cannot be split.");
        }
        if (analysis.hasUnbalancedContent && settings.deskew)
        {
            warnings << tr("The content of the page is not balanced, it cannot be straightened.");
        }
        if (analysis.hasText && (settings.cropMode != CropMode::None || settings.splitMode != SplitMode::None))
        {
            warnings << tr("Text outside of the visible area stays in the file and can be found by the search.");
        }
        if (analysis.hasAnnotations && !qFuzzyIsNull(angle))
        {
            warnings << tr("Annotations are not rotated.");
        }
    }
    if (!warnings.isEmpty())
    {
        badges << QStringLiteral("⚠");
    }

    QListWidgetItem* item = m_pageListWidget->item(int(pageIndex));
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;
    item->setText(tr("Page %1").arg(pageIndex + 1) + (badges.isEmpty() ? QString() : QStringLiteral("   ") + badges.join(QChar(' '))));
    item->setToolTip(warnings.join(QChar('\n')));
    item->setData(Qt::UserRole + 1, !warnings.isEmpty());
    m_updatingUi = wasUpdating;
}

void PDFScanPreparationDialog::updatePageFilter()
{
    const int filter = m_pageFilterComboBox->currentData().toInt();
    m_filterSkewSpinBox->setEnabled(filter == 2);
    for (int i = 0; i < m_pageListWidget->count(); ++i)
    {
        QListWidgetItem* item = m_pageListWidget->item(i);
        const pdf::PDFInteger page = item->data(Qt::UserRole).toLongLong();
        const PageSettings& settings = getPageSettings(page);
        bool visible = true;
        switch (filter)
        {
            case 1:
                visible = !getOutputPages(page).empty() && (getOutputPages(page).size() > 1 || !qFuzzyIsNull(getOutputPages(page).front().deskewAngle) || !getOutputPages(page).front().visibleRect.isEmpty());
                break;
            case 2:
            {
                auto it = m_analysis.find(page);
                const double skew = it != m_analysis.end() ? it->second.skewAngle : settings.deskewAngle;
                visible = std::abs(skew) >= m_filterSkewSpinBox->value();
                break;
            }
            case 3:
                visible = item->data(Qt::UserRole + 1).toBool();
                break;
            default:
                break;
        }
        item->setHidden(!visible);
    }
}

void PDFScanPreparationDialog::setCurrentPage(pdf::PDFInteger pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pageCount)
    {
        return;
    }

    m_currentPage = pageIndex;
    m_updatingUi = true;
    m_pageListWidget->setCurrentRow(int(pageIndex));
    m_updatingUi = false;

    updateControls();
    requestRender(pageIndex);
    updateView();
    if (m_resultViewButton->isChecked())
    {
        startResult();
    }
}

// -------------------------------------------------------------------------
// Background work
// -------------------------------------------------------------------------

void PDFScanPreparationDialog::startAnalysis()
{
    stopAnalysis();

    const std::vector<pdf::PDFInteger> pages = getCheckedPages();
    if (pages.empty())
    {
        return;
    }

    const int generation = ++m_analysisGeneration;
    m_analysisToken = std::make_shared<pdf::PDFOCRCancelToken>();
    std::shared_ptr<pdf::PDFOCRCancelToken> token = m_analysisToken;
    m_analysisProgressBar->setRange(0, int(pages.size()));
    m_analysisProgressBar->setValue(0);
    m_analyzeButton->setEnabled(false);
    m_stopButton->setEnabled(true);

    const pdf::PDFDocument* document = m_context.document;
    const pdf::PDFFontCache* fontCache = m_context.proxy->getFontCache();
    const pdf::PDFCMS* cms = m_context.cms;
    const pdf::PDFOptionalContentActivity* activity = m_optionalContentActivity;
    const pdf::PDFMeshQualitySettings meshQualitySettings = m_meshQualitySettings;
    const pdf::RendererEngine rendererEngine = m_context.proxy->getRendererEngine();

    // The current page is analyzed first
    std::vector<pdf::PDFInteger> order = pages;
    auto currentIt = std::find(order.begin(), order.end(), m_currentPage);
    if (currentIt != order.end())
    {
        std::rotate(order.begin(), currentIt, currentIt + 1);
    }

    m_futures.push_back(QtConcurrent::run([this, document, fontCache, cms, activity, meshQualitySettings, rendererEngine, order, generation, token]()
    {
        pdf::PDFOCRPagePreparer preparer(document, fontCache, cms, activity, meshQualitySettings, rendererEngine);
        for (pdf::PDFInteger page : order)
        {
            if (token->isOperationCancelled())
            {
                break;
            }

            pdf::PDFScanPreparation::PageAnalysis analysis;
            try
            {
                analysis = pdf::PDFScanPreparation::analyzePage(preparer, document, page, token.get());
            }
            catch (...)
            {
                analysis.pageIndex = page;
            }

            if (token->isOperationCancelled())
            {
                break;
            }
            QMetaObject::invokeMethod(this, [this, generation, page, analysis]() { onAnalysisReady(generation, page, analysis); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, generation]() { onAnalysisFinished(generation); }, Qt::QueuedConnection);
    }));
}

void PDFScanPreparationDialog::stopAnalysis()
{
    if (m_analysisToken)
    {
        m_analysisToken->cancel();
        m_analysisToken.reset();
    }
    ++m_analysisGeneration;
    if (m_analyzeButton)
    {
        m_analyzeButton->setEnabled(true);
        m_stopButton->setEnabled(false);
    }
}

void PDFScanPreparationDialog::onAnalysisReady(int generation, pdf::PDFInteger pageIndex, pdf::PDFScanPreparation::PageAnalysis analysis)
{
    if (generation != m_analysisGeneration)
    {
        return;
    }

    if (analysis.isValid())
    {
        m_analysis[pageIndex] = std::move(analysis);
        applyDetection(pageIndex);
        m_sameSizeCacheValid = false;
    }

    m_analysisProgressBar->setValue(m_analysisProgressBar->value() + 1);
    updatePageItem(pageIndex);
    if (pageIndex == m_currentPage)
    {
        updateControls();
        updateView();
    }
    if (m_tableViewButton->isChecked())
    {
        updateTable();
    }
    updateSummary();
}

void PDFScanPreparationDialog::onAnalysisFinished(int generation)
{
    if (generation != m_analysisGeneration)
    {
        return;
    }

    m_analysisToken.reset();
    m_analyzeButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    updatePageFilter();
    updateView();
}

void PDFScanPreparationDialog::applyDetection(pdf::PDFInteger pageIndex)
{
    auto it = m_analysis.find(pageIndex);
    if (it == m_analysis.end())
    {
        return;
    }

    const pdf::PDFScanPreparation::PageAnalysis& analysis = it->second;
    PageSettings& settings = getEditableSettings(pageIndex);

    // The skew is proposed only for a scan with a confident detection; an edited value is kept
    if (!settings.deskewEdited)
    {
        const bool propose = analysis.isScan && !analysis.hasUnbalancedContent && analysis.skewConfidence >= 30.0 && std::abs(analysis.skewAngle) >= m_deskewMinimumSpinBox->value();
        settings.deskew = propose;
        settings.deskewAngle = propose ? analysis.skewAngle : 0.0;
    }

    // The detected spine is used by the split, which was chosen but not placed by the user
    if (!settings.splitEdited && settings.splitMode != SplitMode::None)
    {
        const std::optional<pdf::PDFScanPreparation::GutterCandidate>& candidate = settings.splitMode == SplitMode::SideBySide ? analysis.gutterSideBySide : analysis.gutterOneAboveAnother;
        settings.splitPosition = (candidate && candidate->confidence >= 30.0) ? candidate->position : -1.0;
    }
}

void PDFScanPreparationDialog::requestRender(pdf::PDFInteger pageIndex)
{
    if (m_images.count(pageIndex) || m_pendingRenders.count(pageIndex) || !m_context.proxy)
    {
        return;
    }

    m_pendingRenders.insert(pageIndex);
    const pdf::PDFDocument* document = m_context.document;
    const pdf::PDFFontCache* fontCache = m_context.proxy->getFontCache();
    const pdf::PDFCMS* cms = m_context.cms;
    const pdf::PDFOptionalContentActivity* activity = m_optionalContentActivity;
    const pdf::PDFMeshQualitySettings meshQualitySettings = m_meshQualitySettings;
    const pdf::RendererEngine rendererEngine = m_context.proxy->getRendererEngine();

    m_futures.push_back(QtConcurrent::run([this, document, fontCache, cms, activity, meshQualitySettings, rendererEngine, pageIndex]()
    {
        QImage image;
        try
        {
            pdf::PDFOCRPagePreparer preparer(document, fontCache, cms, activity, meshQualitySettings, rendererEngine);
            image = preparer.rasterize(pageIndex, VIEW_DPI, { }, pdf::PDFOCRPagePreparer::DefaultMaximumPixels, nullptr).image;
        }
        catch (...)
        {
            // The view shows the message
        }
        QMetaObject::invokeMethod(this, [this, pageIndex, image]() { onRenderReady(pageIndex, image); }, Qt::QueuedConnection);
    }));
}

void PDFScanPreparationDialog::onRenderReady(pdf::PDFInteger pageIndex, QImage image)
{
    m_pendingRenders.erase(pageIndex);
    m_images[pageIndex] = std::move(image);
    m_imageOrder.push_back(pageIndex);

    // Only a few rendered pages are kept (memory)
    while (m_imageOrder.size() > IMAGE_CACHE_SIZE)
    {
        const pdf::PDFInteger oldest = m_imageOrder.front();
        m_imageOrder.erase(m_imageOrder.begin());
        if (oldest != m_currentPage)
        {
            m_images.erase(oldest);
        }
    }

    if (pageIndex == m_currentPage)
    {
        updateView();
    }
}

void PDFScanPreparationDialog::scheduleResult()
{
    if (m_resultViewButton->isChecked())
    {
        m_resultTimer.start();
    }
}

void PDFScanPreparationDialog::startResult()
{
    if (!m_resultViewButton->isChecked() || m_currentPage < 0 || !m_context.proxy)
    {
        return;
    }

    if (m_resultToken)
    {
        m_resultToken->cancel();
    }
    m_resultToken = std::make_shared<pdf::PDFOCRCancelToken>();
    std::shared_ptr<pdf::PDFOCRCancelToken> token = m_resultToken;
    const int generation = ++m_resultGeneration;

    // The result is the really prepared document, only the current page is changed
    pdf::PDFScanPreparation::Plan plan = pdf::PDFScanPreparation::createIdentityPlan(m_context.document);
    const std::vector<pdf::PDFScanPreparation::OutputPage> outputs = getOutputPages(m_currentPage);
    plan.pages.erase(plan.pages.begin() + m_currentPage);
    plan.pages.insert(plan.pages.begin() + m_currentPage, outputs.begin(), outputs.end());
    plan.layerActions[m_currentPage] = pdf::PDFScanPreparation::LayerAction::RemoveLayer;

    const pdf::PDFDocument* document = m_context.document;
    const pdf::PDFInteger currentPage = m_currentPage;
    const pdf::PDFFontCache* fontCache = m_context.proxy->getFontCache();
    const pdf::PDFCMS* cms = m_context.cms;
    const pdf::PDFMeshQualitySettings meshQualitySettings = m_meshQualitySettings;
    const pdf::RendererEngine rendererEngine = m_context.proxy->getRendererEngine();
    const size_t outputCount = outputs.size();

    m_view->setResult({ }, QString());
    m_futures.push_back(QtConcurrent::run([this, document, plan, currentPage, fontCache, cms, meshQualitySettings, rendererEngine, outputCount, generation, token]()
    {
        std::vector<QImage> images;
        QString message;
        try
        {
            const pdf::PDFScanPreparation::Result result = pdf::PDFScanPreparation::apply(document, plan, token.get());
            const pdf::PDFDocument* resultDocument = result.document ? result.document.data() : document;
            message = result.isSuccess() ? result.warnings.join(QChar(' ')) : result.errorMessage;

            // The prepared document has its own font cache (its objects differ)
            pdf::PDFFontCache resultFontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
            pdf::PDFOptionalContentActivity resultActivity(resultDocument, pdf::OCUsage::View, nullptr);
            pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(resultDocument), &resultActivity);
            resultFontCache.setDocument(modifiedDocument);
            resultFontCache.setCacheShrinkEnabled(nullptr, false);
            Q_UNUSED(fontCache);

            pdf::PDFOCRPagePreparer preparer(resultDocument, &resultFontCache, cms, &resultActivity, meshQualitySettings, rendererEngine);
            for (size_t i = 0; i < outputCount && !token->isOperationCancelled(); ++i)
            {
                images.push_back(preparer.rasterize(currentPage + pdf::PDFInteger(i), 72.0, { }, pdf::PDFOCRPagePreparer::DefaultMaximumPixels, token.get()).image);
            }
            resultFontCache.setCacheShrinkEnabled(nullptr, true);
        }
        catch (...)
        {
            message = tr("The result cannot be rendered.");
        }

        if (!token->isOperationCancelled())
        {
            QMetaObject::invokeMethod(this, [this, generation, images, message]()
            {
                if (generation == m_resultGeneration)
                {
                    m_view->setResult(images, message);
                }
            }, Qt::QueuedConnection);
        }
    }));
}

// -------------------------------------------------------------------------
// Plan
// -------------------------------------------------------------------------

double PDFScanPreparationDialog::getEffectiveAngle(const PageSettings& settings) const
{
    if (!settings.deskew || std::abs(settings.deskewAngle) < m_deskewMinimumSpinBox->value())
    {
        return 0.0;
    }
    return qBound(-pdf::PDFScanPreparation::MaximumDeskewAngle, settings.deskewAngle, pdf::PDFScanPreparation::MaximumDeskewAngle);
}

std::vector<QRectF> PDFScanPreparationDialog::getRegions(pdf::PDFInteger pageIndex, const PageSettings& settings) const
{
    const QSizeF visibleSize = getVisibleSize(pageIndex);
    if (settings.splitMode == SplitMode::None)
    {
        return { QRectF(QPointF(0, 0), visibleSize) };
    }

    const bool sideBySide = settings.splitMode == SplitMode::SideBySide;
    const double length = sideBySide ? visibleSize.width() : visibleSize.height();
    const double position = settings.splitPosition >= 0.0 ? settings.splitPosition : length * 0.5;
    const std::array<QRectF, 2> halves = pdf::PDFScanPreparation::computeSplit(visibleSize, sideBySide ? pdf::PDFScanPreparation::SplitOrientation::SideBySide
                                                                                                      : pdf::PDFScanPreparation::SplitOrientation::OneAboveAnother,
                                                                              position, settings.gutterWidth);
    if (settings.rightToLeft && sideBySide)
    {
        return { halves[1], halves[0] };
    }
    return { halves[0], halves[1] };
}

QSizeF PDFScanPreparationDialog::getSameSize(pdf::PDFInteger pageIndex) const
{
    if (!m_sameSizeCacheValid)
    {
        // Sizes of the content of the checked pages (after the deskew), per output page
        std::vector<std::pair<int, QSizeF>> sizes;
        int outputIndex = 0;
        for (pdf::PDFInteger page : getCheckedPages())
        {
            const PageSettings& settings = getPageSettings(page);
            auto it = m_analysis.find(page);
            for (const QRectF& region : getRegions(page, settings))
            {
                if (it != m_analysis.end())
                {
                    const QRectF content = pdf::PDFScanPreparation::computeContentCrop(it->second, region, getEffectiveAngle(settings), settings.cropMargin);
                    sizes.emplace_back(outputIndex % 2, content.size());
                }
                ++outputIndex;
            }
        }

        auto median = [&sizes](std::optional<int> parity)
        {
            std::vector<double> widths;
            std::vector<double> heights;
            for (const auto& [itemParity, size] : sizes)
            {
                if (!parity || *parity == itemParity)
                {
                    widths.push_back(size.width());
                    heights.push_back(size.height());
                }
            }
            if (widths.empty())
            {
                return QSizeF();
            }
            std::nth_element(widths.begin(), widths.begin() + widths.size() / 2, widths.end());
            std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
            return QSizeF(widths[widths.size() / 2], heights[heights.size() / 2]);
        };

        m_sameSizeCache.clear();
        if (m_cropOddEvenCheckBox->isChecked())
        {
            m_sameSizeCache[0] = median(0);
            m_sameSizeCache[1] = median(1);
        }
        else
        {
            m_sameSizeCache[0] = median(std::nullopt);
            m_sameSizeCache[1] = m_sameSizeCache[0];
        }
        m_sameSizeCacheValid = true;
    }

    auto it = m_sameSizeCache.find(int(pageIndex % 2));
    return it != m_sameSizeCache.end() ? it->second : QSizeF();
}

std::vector<pdf::PDFScanPreparation::OutputPage> PDFScanPreparationDialog::getOutputPages(pdf::PDFInteger pageIndex) const
{
    std::vector<pdf::PDFScanPreparation::OutputPage> outputs;
    const PageSettings& settings = getPageSettings(pageIndex);
    const std::vector<QRectF> regions = getRegions(pageIndex, settings);
    const bool isSplit = regions.size() > 1;
    const double angle = getEffectiveAngle(settings);
    auto analysisIt = m_analysis.find(pageIndex);

    // Index of the output page among the output pages of the checked pages (odd / even)
    int outputBase = 0;
    for (pdf::PDFInteger page : getCheckedPages())
    {
        if (page == pageIndex)
        {
            break;
        }
        outputBase += int(getRegions(page, getPageSettings(page)).size());
    }

    for (size_t i = 0; i < regions.size(); ++i)
    {
        const QRectF& region = regions[i];
        pdf::PDFScanPreparation::OutputPage output;
        output.sourcePageIndex = pageIndex;
        output.deskewAngle = angle;
        output.regionRect = isSplit ? region : QRectF();

        QRectF crop = isSplit ? region : QRectF();
        switch (settings.cropMode)
        {
            case CropMode::None:
                break;

            case CropMode::Automatic:
                if (analysisIt != m_analysis.end())
                {
                    crop = pdf::PDFScanPreparation::computeContentCrop(analysisIt->second, region, angle, settings.cropMargin);
                }
                break;

            case CropMode::Manual:
                if (!isSplit && settings.manualCrop.isValid())
                {
                    crop = settings.manualCrop.intersected(region);
                }
                break;

            case CropMode::SameSize:
                if (analysisIt != m_analysis.end())
                {
                    const QRectF content = pdf::PDFScanPreparation::computeContentCrop(analysisIt->second, region, angle, settings.cropMargin);
                    const QSizeF size = getSameSize(outputBase + pdf::PDFInteger(i));
                    if (size.isValid())
                    {
                        // The same size, centered on the content, inside of the region
                        QRectF rect(QPointF(), size.boundedTo(region.size()));
                        rect.moveCenter(content.center());
                        rect.moveLeft(qBound(region.left(), rect.left(), region.right() - rect.width()));
                        rect.moveTop(qBound(region.top(), rect.top(), region.bottom() - rect.height()));
                        crop = rect;
                    }
                }
                break;
        }

        output.visibleRect = crop;
        outputs.push_back(output);
    }

    return outputs;
}

pdf::PDFScanPreparation::Plan PDFScanPreparationDialog::createPlan() const
{
    pdf::PDFScanPreparation::Plan plan;
    const std::vector<pdf::PDFInteger> checked = getCheckedPages();
    const std::set<pdf::PDFInteger> checkedSet(checked.begin(), checked.end());
    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        if (checkedSet.count(page))
        {
            const std::vector<pdf::PDFScanPreparation::OutputPage> outputs = getOutputPages(page);
            plan.pages.insert(plan.pages.end(), outputs.begin(), outputs.end());
        }
        else
        {
            plan.pages.push_back(pdf::PDFScanPreparation::OutputPage{ page, QRectF(), 0.0, QRectF() });
        }
    }
    return plan;
}

// -------------------------------------------------------------------------
// Controls
// -------------------------------------------------------------------------

void PDFScanPreparationDialog::updateControls()
{
    if (m_currentPage < 0)
    {
        return;
    }

    const PageSettings& settings = getPageSettings(m_currentPage);
    const QSizeF visibleSize = getVisibleSize(m_currentPage);
    auto analysisIt = m_analysis.find(m_currentPage);
    const pdf::PDFScanPreparation::PageAnalysis* analysis = analysisIt != m_analysis.end() ? &analysisIt->second : nullptr;

    m_updatingUi = true;
    m_deskewCheckBox->setChecked(settings.deskew);
    m_deskewAngleSpinBox->setValue(settings.deskewAngle);
    m_deskewAngleSpinBox->setEnabled(settings.deskew);
    m_splitModeComboBox->setCurrentIndex(m_splitModeComboBox->findData(int(settings.splitMode)));
    const bool sideBySide = settings.splitMode == SplitMode::SideBySide;
    const double length = sideBySide ? visibleSize.width() : visibleSize.height();
    m_splitPositionSpinBox->setMaximum(length / MM_TO_POINT);
    m_splitPositionSpinBox->setValue((settings.splitPosition >= 0.0 ? settings.splitPosition : length * 0.5) / MM_TO_POINT);
    m_gutterWidthSpinBox->setValue(settings.gutterWidth / MM_TO_POINT);
    m_readingOrderComboBox->setCurrentIndex(settings.rightToLeft ? 1 : 0);
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_splitPositionSpinBox, m_gutterWidthSpinBox, m_readingOrderComboBox })
    {
        widget->setEnabled(settings.splitMode != SplitMode::None);
    }
    m_readingOrderComboBox->setEnabled(sideBySide);
    m_cropModeComboBox->setCurrentIndex(m_cropModeComboBox->findData(int(settings.cropMode)));
    m_cropMarginSpinBox->setValue(settings.cropMargin / MM_TO_POINT);
    m_cropMarginSpinBox->setEnabled(settings.cropMode == CropMode::Automatic || settings.cropMode == CropMode::SameSize);
    m_updatingUi = false;

    // Detected values with their confidence
    if (analysis)
    {
        const bool lowConfidence = analysis->skewConfidence < 30.0;
        QString text = tr("Detected %1°, confidence %2").arg(analysis->skewAngle, 0, 'f', 2).arg(qRound(analysis->skewConfidence));
        if (!analysis->isScan)
        {
            text += QChar('\n') + tr("The page is not a scan; it is not straightened by default.");
        }
        if (analysis->hasUnbalancedContent)
        {
            text += QChar('\n') + tr("The content of the page is not balanced, it cannot be straightened.");
        }
        m_deskewDetectedLabel->setText(text);
        m_deskewDetectedLabel->setStyleSheet(lowConfidence ? QStringLiteral("color: #c86400;") : QString());
        m_deskewResetButton->setEnabled(true);

        const std::optional<pdf::PDFScanPreparation::GutterCandidate>& candidate = sideBySide ? analysis->gutterSideBySide : analysis->gutterOneAboveAnother;
        if (settings.splitMode == SplitMode::None)
        {
            m_splitDetectedLabel->setText(analysis->gutterSideBySide ? tr("Spine side by side detected at %1 mm (confidence %2)").arg(analysis->gutterSideBySide->position / MM_TO_POINT, 0, 'f', 1).arg(qRound(analysis->gutterSideBySide->confidence))
                                                                     : tr("No spine was detected."));
        }
        else if (candidate)
        {
            m_splitDetectedLabel->setText(tr("Detected at %1 mm (%2, confidence %3)").arg(candidate->position / MM_TO_POINT, 0, 'f', 1)
                                          .arg(candidate->isShadow ? tr("shadow of the spine") : tr("white gap")).arg(qRound(candidate->confidence)));
        }
        else
        {
            m_splitDetectedLabel->setText(tr("No spine was detected, the split line is in the middle."));
        }
    }
    else
    {
        m_deskewDetectedLabel->setText(tr("Not detected yet (button Detect on Checked Pages)."));
        m_deskewDetectedLabel->setStyleSheet(QString());
        m_deskewResetButton->setEnabled(false);
        m_splitDetectedLabel->clear();
    }

    // Size of the crop
    QStringList cropSizes;
    for (const pdf::PDFScanPreparation::OutputPage& output : getOutputPages(m_currentPage))
    {
        const QRectF rect = output.visibleRect.isEmpty() ? QRectF(QPointF(0, 0), visibleSize) : output.visibleRect;
        cropSizes << tr("%1 x %2 mm").arg(rect.width() / MM_TO_POINT, 0, 'f', 1).arg(rect.height() / MM_TO_POINT, 0, 'f', 1);
    }
    QString cropText = cropSizes.join(QStringLiteral(", "));
    if ((settings.cropMode == CropMode::Automatic || settings.cropMode == CropMode::SameSize) && !analysis)
    {
        cropText += QChar('\n') + tr("The automatic crop needs the detection.");
    }
    if (settings.cropMode == CropMode::Manual && settings.splitMode != SplitMode::None)
    {
        cropText += QChar('\n') + tr("The manual crop is used only for pages, which are not split.");
    }
    m_cropSizeLabel->setText(cropText);
}

void PDFScanPreparationDialog::onControlsChanged()
{
    if (m_updatingUi || m_currentPage < 0)
    {
        return;
    }

    PageSettings& settings = getEditableSettings(m_currentPage);
    const PageSettings previous = settings;

    settings.deskew = m_deskewCheckBox->isChecked();
    if (!qFuzzyCompare(previous.deskewAngle + 100.0, m_deskewAngleSpinBox->value() + 100.0))
    {
        settings.deskewAngle = m_deskewAngleSpinBox->value();
        settings.deskewEdited = true;
    }
    if (settings.deskew != previous.deskew)
    {
        settings.deskewEdited = true;
    }

    settings.splitMode = SplitMode(m_splitModeComboBox->currentData().toInt());
    if (settings.splitMode != previous.splitMode)
    {
        // A new split mode takes the detected spine
        settings.splitEdited = false;
        settings.splitPosition = -1.0;
        applyDetection(m_currentPage);
    }
    else if (settings.splitMode != SplitMode::None)
    {
        const QSizeF visibleSize = getVisibleSize(m_currentPage);
        const double length = settings.splitMode == SplitMode::SideBySide ? visibleSize.width() : visibleSize.height();
        const double previousPosition = previous.splitPosition >= 0.0 ? previous.splitPosition : length * 0.5;
        const double position = m_splitPositionSpinBox->value() * MM_TO_POINT;
        if (std::abs(position - previousPosition) > 0.05)
        {
            settings.splitPosition = position;
            settings.splitEdited = true;
        }
    }
    settings.gutterWidth = m_gutterWidthSpinBox->value() * MM_TO_POINT;
    settings.rightToLeft = m_readingOrderComboBox->currentData().toBool();

    settings.cropMode = CropMode(m_cropModeComboBox->currentData().toInt());
    if (settings.cropMode == CropMode::Manual && !settings.manualCrop.isValid())
    {
        // The manual crop starts from the automatic one (or from the page)
        auto it = m_analysis.find(m_currentPage);
        settings.manualCrop = it != m_analysis.end() ? pdf::PDFScanPreparation::computeContentCrop(it->second, QRectF(), getEffectiveAngle(settings), settings.cropMargin)
                                                     : QRectF(QPointF(0, 0), getVisibleSize(m_currentPage)).adjusted(20, 20, -20, -20);
    }
    settings.cropMargin = m_cropMarginSpinBox->value() * MM_TO_POINT;
    m_sameSizeCacheValid = false;

    updateControls();
    updatePageItem(m_currentPage);
    updateView();
    updateSummary();
    if (m_tableViewButton->isChecked())
    {
        updateTable();
    }
}

void PDFScanPreparationDialog::rotateCurrentPage(double delta)
{
    if (m_currentPage < 0)
    {
        return;
    }

    PageSettings& settings = getEditableSettings(m_currentPage);
    settings.deskew = true;
    settings.deskewAngle = qBound(-pdf::PDFScanPreparation::MaximumDeskewAngle, settings.deskewAngle + delta, pdf::PDFScanPreparation::MaximumDeskewAngle);
    settings.deskewEdited = true;
    updateControls();
    updatePageItem(m_currentPage);
    updateView();
    updateSummary();
}

void PDFScanPreparationDialog::applyToChecked(bool angle, bool split, bool crop)
{
    if (m_currentPage < 0)
    {
        return;
    }

    const PageSettings source = getPageSettings(m_currentPage);
    for (pdf::PDFInteger page : getCheckedPages())
    {
        if (page == m_currentPage)
        {
            continue;
        }

        PageSettings& settings = getEditableSettings(page);
        if (angle)
        {
            // An edited angle is copied, otherwise every page keeps its own detected skew
            settings.deskew = source.deskew;
            if (source.deskewEdited)
            {
                settings.deskewAngle = source.deskewAngle;
                settings.deskewEdited = true;
            }
            else
            {
                settings.deskewEdited = false;
                applyDetection(page);
                settings.deskew = source.deskew && settings.deskew;
            }
        }
        if (split)
        {
            settings.splitMode = source.splitMode;
            settings.gutterWidth = source.gutterWidth;
            settings.rightToLeft = source.rightToLeft;
            settings.splitEdited = source.splitEdited;
            settings.splitPosition = source.splitEdited ? source.splitPosition : -1.0;
            if (!source.splitEdited)
            {
                const bool deskew = settings.deskew;
                const double deskewAngle = settings.deskewAngle;
                applyDetection(page);
                settings.deskew = deskew;
                settings.deskewAngle = deskewAngle;
            }
        }
        if (crop)
        {
            settings.cropMode = source.cropMode;
            settings.cropMargin = source.cropMargin;
            settings.manualCrop = source.manualCrop;
        }
        updatePageItem(page);
    }

    m_sameSizeCacheValid = false;
    updateView();
    updateSummary();
    if (m_tableViewButton->isChecked())
    {
        updateTable();
    }
}

void PDFScanPreparationDialog::resetPage()
{
    if (m_currentPage < 0)
    {
        return;
    }

    PageSettings& settings = getEditableSettings(m_currentPage);
    const PageSettings defaults;
    settings.deskew = defaults.deskew;
    settings.deskewAngle = defaults.deskewAngle;
    settings.deskewEdited = false;
    settings.splitMode = SplitMode::None;
    settings.splitPosition = -1.0;
    settings.splitEdited = false;
    settings.cropMode = CropMode::None;
    settings.manualCrop = QRectF();
    applyDetection(m_currentPage);
    updateControls();
    updatePageItem(m_currentPage);
    updateView();
    updateSummary();
}

// -------------------------------------------------------------------------
// View, table, summary
// -------------------------------------------------------------------------

void PDFScanPreparationDialog::updateView()
{
    if (m_currentPage < 0)
    {
        m_view->clear(tr("The document has no page."));
        return;
    }

    auto imageIt = m_images.find(m_currentPage);
    if (imageIt == m_images.end())
    {
        m_view->clear(tr("Rendering the page..."));
    }
    else if (imageIt->second.isNull())
    {
        m_view->clear(tr("The page cannot be rendered."));
    }
    else
    {
        m_view->setPage(imageIt->second, getVisibleSize(m_currentPage));
    }

    const PageSettings& settings = getPageSettings(m_currentPage);
    const bool isChecked = m_pageListWidget->item(int(m_currentPage))->checkState() == Qt::Checked;

    PDFScanPreparationPageView::Overlay overlay;
    overlay.showGrid = m_gridCheckBox->isChecked();
    overlay.gridSpacing = m_gridSpacingSpinBox->value() * MM_TO_POINT;
    if (isChecked)
    {
        for (const pdf::PDFScanPreparation::OutputPage& output : getOutputPages(m_currentPage))
        {
            overlay.regions.push_back(output.regionRect.isEmpty() ? QRectF(QPointF(0, 0), getVisibleSize(m_currentPage)) : output.regionRect);
            overlay.crops.push_back(output.visibleRect);
            overlay.angles.push_back(output.deskewAngle);
        }

        if (settings.splitMode != SplitMode::None)
        {
            const QSizeF visibleSize = getVisibleSize(m_currentPage);
            overlay.splitVertical = settings.splitMode == SplitMode::SideBySide;
            const double length = overlay.splitVertical ? visibleSize.width() : visibleSize.height();
            overlay.splitPosition = settings.splitPosition >= 0.0 ? settings.splitPosition : length * 0.5;
            overlay.gutterWidth = settings.gutterWidth;
        }
        overlay.cropEditable = settings.cropMode == CropMode::Manual && settings.splitMode == SplitMode::None;
    }
    m_view->setOverlay(overlay);
    scheduleResult();
}

void PDFScanPreparationDialog::updateTable()
{
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;
    m_tableWidget->setSortingEnabled(false);
    m_tableWidget->setRowCount(int(m_pageCount));
    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        updateTableRow(int(page), page);
    }
    m_tableWidget->setSortingEnabled(true);
    m_updatingUi = wasUpdating;
}

void PDFScanPreparationDialog::updateTableRow(int row, pdf::PDFInteger pageIndex)
{
    auto createItem = [](const QVariant& value, bool editable)
    {
        QTableWidgetItem* item = new QTableWidgetItem();
        item->setData(Qt::DisplayRole, value);
        if (!editable)
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
        return item;
    };

    const PageSettings& settings = getPageSettings(pageIndex);
    auto it = m_analysis.find(pageIndex);
    const pdf::PDFScanPreparation::PageAnalysis* analysis = it != m_analysis.end() ? &it->second : nullptr;

    QTableWidgetItem* pageItem = createItem(qlonglong(pageIndex + 1), false);
    pageItem->setData(Qt::UserRole, qlonglong(pageIndex));
    m_tableWidget->setItem(row, 0, pageItem);

    QString pageClass = tr("not detected");
    if (analysis)
    {
        pageClass = analysis->isScan ? tr("scan") : (analysis->hasText ? tr("text") : tr("other"));
    }
    m_tableWidget->setItem(row, 1, createItem(pageClass, false));
    m_tableWidget->setItem(row, 2, analysis ? createItem(QString::number(analysis->skewAngle, 'f', 2).toDouble(), false) : createItem(QString(), false));
    m_tableWidget->setItem(row, 3, createItem(QString::number(getEffectiveAngle(settings), 'f', 2).toDouble(), true));

    QString split = tr("no");
    if (settings.splitMode == SplitMode::SideBySide)
    {
        split = settings.rightToLeft ? tr("side by side, right first") : tr("side by side");
    }
    else if (settings.splitMode == SplitMode::OneAboveAnother)
    {
        split = tr("one above another");
    }
    m_tableWidget->setItem(row, 4, createItem(split, false));
    m_tableWidget->setItem(row, 5, createItem(m_cropModeComboBox->itemText(m_cropModeComboBox->findData(int(settings.cropMode))), false));
    m_tableWidget->setItem(row, 6, createItem(m_pageListWidget->item(int(pageIndex))->toolTip().replace(QChar('\n'), QStringLiteral("; ")), false));
}

void PDFScanPreparationDialog::onTableItemChanged(int row, int column)
{
    if (m_updatingUi || column != 3)
    {
        return;
    }

    QTableWidgetItem* pageItem = m_tableWidget->item(row, 0);
    QTableWidgetItem* angleItem = m_tableWidget->item(row, 3);
    if (!pageItem || !angleItem)
    {
        return;
    }

    // The applied skew is edited directly in the table
    bool ok = false;
    const double angle = angleItem->data(Qt::DisplayRole).toString().replace(QChar(','), QChar('.')).toDouble(&ok);
    const pdf::PDFInteger page = pageItem->data(Qt::UserRole).toLongLong();
    if (ok)
    {
        PageSettings& settings = getEditableSettings(page);
        settings.deskew = !qFuzzyIsNull(angle);
        settings.deskewAngle = qBound(-pdf::PDFScanPreparation::MaximumDeskewAngle, angle, pdf::PDFScanPreparation::MaximumDeskewAngle);
        settings.deskewEdited = true;
        updatePageItem(page);
        if (page == m_currentPage)
        {
            updateControls();
            updateView();
        }
        updateSummary();
    }

    m_updatingUi = true;
    updateTableRow(row, page);
    m_updatingUi = false;
}

void PDFScanPreparationDialog::updateSummary()
{
    const pdf::PDFScanPreparation::Plan plan = createPlan();
    int straightened = 0;
    int split = 0;
    int cropped = 0;
    std::map<pdf::PDFInteger, int> outputs;
    for (const pdf::PDFScanPreparation::OutputPage& output : plan.pages)
    {
        ++outputs[output.sourcePageIndex];
        straightened += qFuzzyIsNull(output.deskewAngle) ? 0 : 1;
        cropped += output.visibleRect.isEmpty() ? 0 : 1;
    }
    for (const auto& [page, count] : outputs)
    {
        split += count > 1 ? 1 : 0;
    }

    m_summaryLabel->setText(tr("%1 page(s) straightened, %2 split (the document will have %3 pages), %4 cropped. The content is not re-encoded.")
                                .arg(straightened).arg(split).arg(plan.pages.size()).arg(cropped));

    QStringList warnings;
    const std::vector<pdf::PDFInteger> layers = pdf::PDFScanPreparation::getChangedPagesWithOCRLayer(m_context.document, plan);
    if (!layers.empty())
    {
        warnings << tr("%n changed page(s) have an OCR layer of PDF4QT; you will be asked what to do with them.", nullptr, int(layers.size()));
    }
    if (m_context.hasSignatures && (straightened + split + cropped) > 0)
    {
        warnings << tr("The document is signed; the change of the pages invalidates the signatures.");
    }
    m_warningLabel->setText(warnings.join(QChar(' ')));
    m_warningLabel->setVisible(!warnings.isEmpty());
    m_buttonBox->button(QDialogButtonBox::Apply)->setEnabled(straightened + split + cropped > 0);
}

// -------------------------------------------------------------------------
// Application
// -------------------------------------------------------------------------

void PDFScanPreparationDialog::onApply()
{
    pdf::PDFScanPreparation::Plan plan = createPlan();

    // Pages with the own OCR layer: the user decides (the layer would not fit the changed page)
    const std::vector<pdf::PDFInteger> layerPages = pdf::PDFScanPreparation::getChangedPagesWithOCRLayer(m_context.document, plan);
    if (!layerPages.empty())
    {
        QStringList pageNumbers;
        for (pdf::PDFInteger page : layerPages)
        {
            pageNumbers << QString::number(page + 1);
        }

        QMessageBox messageBox(QMessageBox::Question, windowTitle(),
                               tr("The following changed pages have a text layer created by the OCR of PDF4QT: %1.").arg(pageNumbers.join(QStringLiteral(", "))),
                               QMessageBox::NoButton, this);
        messageBox.setInformativeText(tr("The layer would not fit the changed page (text outside of the visible area, a duplicated layer of the split pages). "
                                         "Skip these pages, or remove their OCR layer and recognize them again after the preparation. Corrections saved in an OCR project are not loaded for the changed pages."));
        QPushButton* skipButton = messageBox.addButton(tr("Skip These Pages"), QMessageBox::AcceptRole);
        QPushButton* removeButton = messageBox.addButton(tr("Remove OCR Layer and Continue"), QMessageBox::DestructiveRole);
        messageBox.addButton(QMessageBox::Cancel);
        messageBox.setDefaultButton(skipButton);
        messageBox.exec();

        const pdf::PDFScanPreparation::LayerAction action = messageBox.clickedButton() == removeButton ? pdf::PDFScanPreparation::LayerAction::RemoveLayer
                                                                                                       : pdf::PDFScanPreparation::LayerAction::Skip;
        if (messageBox.clickedButton() != skipButton && messageBox.clickedButton() != removeButton)
        {
            return;
        }
        for (pdf::PDFInteger page : layerPages)
        {
            plan.layerActions[page] = action;
        }
    }

    // Confirmation with the summary and the warnings
    int straightened = 0;
    int cropped = 0;
    std::set<pdf::PDFInteger> annotatedStraightened;
    std::set<pdf::PDFInteger> textCropped;
    std::map<pdf::PDFInteger, int> outputCounts;
    for (const pdf::PDFScanPreparation::OutputPage& output : plan.pages)
    {
        ++outputCounts[output.sourcePageIndex];
        auto it = m_analysis.find(output.sourcePageIndex);
        if (!qFuzzyIsNull(output.deskewAngle))
        {
            ++straightened;
            if (!m_context.document->getCatalog()->getPage(output.sourcePageIndex)->getAnnotations().empty())
            {
                annotatedStraightened.insert(output.sourcePageIndex);
            }
        }
        if (!output.visibleRect.isEmpty())
        {
            ++cropped;
            if (it == m_analysis.end() || it->second.hasText)
            {
                textCropped.insert(output.sourcePageIndex);
            }
        }
    }
    const int split = int(std::count_if(outputCounts.begin(), outputCounts.end(), [](const auto& item) { return item.second > 1; }));

    QStringList summary;
    summary << tr("Pages straightened: %1").arg(straightened);
    summary << tr("Pages split: %1 (the document will have %2 pages)").arg(split).arg(plan.pages.size());
    summary << tr("Pages cropped: %1").arg(cropped);
    summary << tr("The content of the pages is not re-encoded; the change is one step of the undo history.");

    QStringList warnings;
    if (!annotatedStraightened.empty())
    {
        warnings << tr("Annotations of the straightened pages are not rotated.");
    }
    if (!textCropped.empty())
    {
        warnings << tr("Text outside of the visible area of the cropped pages stays in the file and can be found by the search.");
    }
    if (m_context.hasSignatures)
    {
        warnings << tr("The document is signed; the change of the pages invalidates the signatures.");
    }
    for (const auto& [page, action] : plan.layerActions)
    {
        if (action == pdf::PDFScanPreparation::LayerAction::RemoveLayer)
        {
            warnings << tr("The OCR layer is removed from %n page(s); recognize them again after the preparation.", nullptr, int(plan.layerActions.size()));
            break;
        }
    }

    QMessageBox confirmation(QMessageBox::Question, tr("Prepare Scanned Pages"), summary.join(QChar('\n')), QMessageBox::NoButton, this);
    confirmation.setInformativeText(warnings.join(QStringLiteral("\n\n")));
    QPushButton* applyButton = confirmation.addButton(tr("Apply"), QMessageBox::AcceptRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != applyButton)
    {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const pdf::PDFScanPreparation::Result result = pdf::PDFScanPreparation::apply(m_context.document, plan, nullptr);
    QApplication::restoreOverrideCursor();

    if (!result.isSuccess())
    {
        QMessageBox::critical(this, windowTitle(), tr("The pages were not prepared, the document was not changed.\n\n%1").arg(result.errorMessage));
        return;
    }

    if (!result.document)
    {
        QMessageBox::information(this, windowTitle(), tr("Nothing was changed.\n\n%1").arg(result.warnings.join(QChar('\n'))));
        return;
    }

    if (!result.warnings.isEmpty())
    {
        QMessageBox messageBox(QMessageBox::Information, windowTitle(), result.getSummary(), QMessageBox::Ok, this);
        messageBox.setDetailedText(result.warnings.join(QChar('\n')));
        messageBox.exec();
    }

    m_resultDocument = result.document;
    accept();
}

}   // namespace pdfviewer
