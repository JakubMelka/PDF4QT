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

#include "pdfocrcompressionpreviewdialog.h"
#include "pdfocrjobcontroller.h"
#include "pdfdocument.h"
#include "pdfwidgetutils.h"

#include <QLabel>
#include <QTimer>
#include <QSpinBox>
#include <QPainter>
#include <QCheckBox>
#include <QComboBox>
#include <QBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>
#include <QDialogButtonBox>
#include <QtConcurrent/QtConcurrent>

namespace pdfviewer
{

// -------------------------------------------------------------------------
// PDFOCRComparisonView
// -------------------------------------------------------------------------

PDFOCRComparisonView::PDFOCRComparisonView(QWidget* parent) :
    BaseClass(parent)
{
    setMouseTracking(true);
    setMinimumSize(200, 200);
    setAccessibleName(tr("Comparison of the original and of the compressed image"));
}

void PDFOCRComparisonView::setImages(QImage original, QImage compressed, QString message)
{
    m_original = std::move(original);
    m_compressed = std::move(compressed);
    m_message = std::move(message);
    update();
}

void PDFOCRComparisonView::setZoom(double zoom)
{
    m_zoom = qMax(0.0, zoom);
    m_offset = QPointF();
    update();
}

void PDFOCRComparisonView::setDividerPosition(double position)
{
    m_divider = qBound(0.0, position, 1.0);
    update();
}

QSize PDFOCRComparisonView::sizeHint() const
{
    return QSize(800, 600);
}

QRectF PDFOCRComparisonView::getImageRect() const
{
    if (m_original.isNull())
    {
        return QRectF();
    }

    const QSizeF imageSize = m_original.size();
    double scale = m_zoom;
    if (scale <= 0.0)
    {
        scale = qMin((width() - 8.0) / imageSize.width(), (height() - 8.0) / imageSize.height());
    }

    const QSizeF scaledSize = imageSize * scale;
    const QPointF topLeft(((width() - scaledSize.width()) * 0.5) + m_offset.x(), ((height() - scaledSize.height()) * 0.5) + m_offset.y());
    return QRectF(topLeft, scaledSize);
}

void PDFOCRComparisonView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Dark));

    if (m_original.isNull())
    {
        painter.setPen(palette().color(QPalette::BrightText));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message.isEmpty() ? tr("No image on this page.") : m_message);
        return;
    }

    const QRectF imageRect = getImageRect();
    const double dividerX = width() * m_divider;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom <= 0.0 || m_zoom < 1.0);

    // Original on the left of the divider
    painter.save();
    painter.setClipRect(QRectF(0, 0, dividerX, height()));
    painter.drawImage(imageRect, m_original);
    painter.restore();

    // Compressed image on the right of the divider (the same geometry)
    painter.save();
    painter.setClipRect(QRectF(dividerX, 0, width() - dividerX, height()));
    if (!m_compressed.isNull())
    {
        painter.drawImage(imageRect, m_compressed);
    }
    else
    {
        painter.drawImage(imageRect, m_original);
        painter.fillRect(imageRect, QColor(0, 0, 0, 90));
    }
    painter.restore();

    // Divider with a handle
    painter.setPen(QPen(QColor(220, 60, 0), 2.0));
    painter.drawLine(QPointF(dividerX, 0), QPointF(dividerX, height()));
    painter.setBrush(QColor(220, 60, 0));
    painter.drawEllipse(QPointF(dividerX, height() * 0.5), 6, 6);

    // Captions
    const QString leftCaption = tr("Original");
    const QString rightCaption = m_compressed.isNull() ? (m_message.isEmpty() ? tr("Not compressed") : m_message) : tr("Compressed");
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    auto drawCaption = [&](const QString& text, Qt::Alignment alignment)
    {
        QRect textRect = metrics.boundingRect(QRect(0, 0, qMax(100, width() / 2 - 20), 200), Qt::TextWordWrap, text).adjusted(-6, -3, 6, 3);
        if (alignment & Qt::AlignRight)
        {
            textRect.moveTopRight(QPoint(width() - 8, 8));
        }
        else
        {
            textRect.moveTopLeft(QPoint(8, 8));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        painter.drawRoundedRect(textRect, 4, 4);
        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
    };
    drawCaption(leftCaption, Qt::AlignLeft);
    drawCaption(rightCaption, Qt::AlignRight);
}

void PDFOCRComparisonView::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePosition = event->position().toPoint();
    if (event->button() == Qt::LeftButton)
    {
        m_draggingDivider = true;
        setDividerPosition(event->position().x() / qMax(1, width()));
        event->accept();
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void PDFOCRComparisonView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    if (m_draggingDivider)
    {
        setDividerPosition(event->position().x() / qMax(1, width()));
    }
    else if (m_panning)
    {
        m_offset += QPointF(position - m_lastMousePosition);
        update();
    }
    else
    {
        const double dividerX = width() * m_divider;
        setCursor(std::abs(event->position().x() - dividerX) < 8 ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
    m_lastMousePosition = position;
}

void PDFOCRComparisonView::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_draggingDivider = false;
    m_panning = false;
    setCursor(Qt::ArrowCursor);
}

void PDFOCRComparisonView::wheelEvent(QWheelEvent* event)
{
    if (m_original.isNull())
    {
        return;
    }

    // The zoom starts from the current (fitted) scale
    const QRectF imageRect = getImageRect();
    double scale = imageRect.width() / qMax(1, m_original.width());
    scale *= event->angleDelta().y() > 0 ? 1.25 : 0.8;
    m_zoom = qBound(0.02, scale, 16.0);
    update();
    event->accept();
}

// -------------------------------------------------------------------------
// PDFOCRCompressionPreviewDialog
// -------------------------------------------------------------------------

PDFOCRCompressionPreviewDialog::PDFOCRCompressionPreviewDialog(const pdf::PDFDocument* document,
                                                               std::vector<pdf::PDFInteger> pages,
                                                               pdf::PDFInteger currentPage,
                                                               pdf::PDFOCRCompressionSettings settings,
                                                               QWidget* parent) :
    BaseClass(parent),
    m_document(document),
    m_pages(std::move(pages)),
    m_settings(std::move(settings))
{
    setWindowTitle(tr("Preview of the Compression"));
    setObjectName(QStringLiteral("PDFOCRCompressionPreviewDialog"));

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Navigation: page, image and zoom
    QHBoxLayout* navigationLayout = new QHBoxLayout();
    m_previousPageButton = new QToolButton(this);
    m_previousPageButton->setObjectName(QStringLiteral("previousPageButton"));
    m_previousPageButton->setArrowType(Qt::LeftArrow);
    m_previousPageButton->setToolTip(tr("Previous page (Page Up)"));
    m_previousPageButton->setShortcut(QKeySequence(Qt::Key_PageUp));
    m_nextPageButton = new QToolButton(this);
    m_nextPageButton->setObjectName(QStringLiteral("nextPageButton"));
    m_nextPageButton->setArrowType(Qt::RightArrow);
    m_nextPageButton->setToolTip(tr("Next page (Page Down)"));
    m_nextPageButton->setShortcut(QKeySequence(Qt::Key_PageDown));
    m_pageComboBox = new QComboBox(this);
    m_pageComboBox->setObjectName(QStringLiteral("previewPageComboBox"));
    m_pageComboBox->setAccessibleName(tr("Page"));
    for (pdf::PDFInteger page : m_pages)
    {
        m_pageComboBox->addItem(tr("Page %1").arg(page + 1), QVariant::fromValue(page));
    }
    m_imageComboBox = new QComboBox(this);
    m_imageComboBox->setObjectName(QStringLiteral("previewImageComboBox"));
    m_imageComboBox->setAccessibleName(tr("Image of the page"));
    m_zoomComboBox = new QComboBox(this);
    m_zoomComboBox->setObjectName(QStringLiteral("previewZoomComboBox"));
    m_zoomComboBox->setAccessibleName(tr("Zoom"));
    m_zoomComboBox->addItem(tr("Fit"), 0.0);
    m_zoomComboBox->addItem(tr("50 %"), 0.5);
    m_zoomComboBox->addItem(tr("100 % (1:1)"), 1.0);
    m_zoomComboBox->addItem(tr("200 %"), 2.0);
    m_zoomComboBox->addItem(tr("400 %"), 4.0);

    navigationLayout->addWidget(m_previousPageButton);
    navigationLayout->addWidget(m_pageComboBox);
    navigationLayout->addWidget(m_nextPageButton);
    navigationLayout->addSpacing(12);
    navigationLayout->addWidget(new QLabel(tr("Image:"), this));
    navigationLayout->addWidget(m_imageComboBox, 1);
    navigationLayout->addSpacing(12);
    navigationLayout->addWidget(new QLabel(tr("Zoom:"), this));
    navigationLayout->addWidget(m_zoomComboBox);
    layout->addLayout(navigationLayout);

    m_view = new PDFOCRComparisonView(this);
    m_view->setObjectName(QStringLiteral("comparisonView"));
    layout->addWidget(m_view, 1);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName(QStringLiteral("previewInfoLabel"));
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_infoLabel);

    // Threshold of the lossy conversion and the exclusion of the image
    QHBoxLayout* settingsLayout = new QHBoxLayout();
    m_thresholdComboBox = new QComboBox(this);
    m_thresholdComboBox->setObjectName(QStringLiteral("previewThresholdComboBox"));
    m_thresholdComboBox->setAccessibleName(tr("Method of the conversion to black and white"));
    for (pdf::PDFOCRThresholdMethod method : { pdf::PDFOCRThresholdMethod::Automatic, pdf::PDFOCRThresholdMethod::Adaptive, pdf::PDFOCRThresholdMethod::Manual })
    {
        m_thresholdComboBox->addItem(pdf::PDFOCRCompressionSettings::getThresholdMethodName(method), int(method));
    }
    m_thresholdSpinBox = new QSpinBox(this);
    m_thresholdSpinBox->setObjectName(QStringLiteral("previewThresholdSpinBox"));
    m_thresholdSpinBox->setAccessibleName(tr("Manual threshold"));
    m_thresholdSpinBox->setRange(0, 255);
    m_excludeCheckBox = new QCheckBox(tr("Exclude this image from the compression"), this);
    m_excludeCheckBox->setObjectName(QStringLiteral("excludeImageCheckBox"));
    settingsLayout->addWidget(new QLabel(tr("Threshold:"), this));
    settingsLayout->addWidget(m_thresholdComboBox);
    settingsLayout->addWidget(m_thresholdSpinBox);
    settingsLayout->addStretch(1);
    settingsLayout->addWidget(m_excludeCheckBox);
    layout->addLayout(settingsLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("previewStatusLabel"));
    layout->addWidget(m_statusLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Confirm"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setToolTip(tr("Confirm the compression with these settings"));
    layout->addWidget(m_buttonBox);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(250);

    // Initial state
    m_updatingUi = true;
    m_thresholdComboBox->setCurrentIndex(qMax(0, m_thresholdComboBox->findData(int(m_settings.thresholdMethod))));
    m_thresholdSpinBox->setValue(m_settings.manualThreshold);
    const auto currentIt = std::find(m_pages.begin(), m_pages.end(), currentPage);
    m_pageComboBox->setCurrentIndex(currentIt != m_pages.end() ? int(std::distance(m_pages.begin(), currentIt)) : 0);
    m_updatingUi = false;
    updateThresholdControls();

    connect(m_previewTimer, &QTimer::timeout, this, &PDFOCRCompressionPreviewDialog::startPreview);
    connect(m_pageComboBox, &QComboBox::currentIndexChanged, this, [this]() { if (!m_updatingUi) { schedulePreview(); } });
    connect(m_previousPageButton, &QToolButton::clicked, this, [this]() { setPageIndex(m_pageComboBox->currentIndex() - 1); });
    connect(m_nextPageButton, &QToolButton::clicked, this, [this]() { setPageIndex(m_pageComboBox->currentIndex() + 1); });
    connect(m_imageComboBox, &QComboBox::currentIndexChanged, this, [this]() { if (!m_updatingUi) { updateImage(); } });
    connect(m_zoomComboBox, &QComboBox::currentIndexChanged, this, [this]() { m_view->setZoom(m_zoomComboBox->currentData().toDouble()); });
    connect(m_thresholdComboBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        if (!m_updatingUi)
        {
            m_settings.thresholdMethod = pdf::PDFOCRThresholdMethod(m_thresholdComboBox->currentData().toInt());
            updateThresholdControls();
            schedulePreview();
        }
    });
    connect(m_thresholdSpinBox, &QSpinBox::valueChanged, this, [this](int value)
    {
        if (!m_updatingUi)
        {
            m_settings.manualThreshold = value;
            schedulePreview();
        }
    });
    connect(m_excludeCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_updatingUi)
        {
            return;
        }

        const int index = m_imageComboBox->currentIndex();
        if (index < 0 || size_t(index) >= m_previews.size())
        {
            return;
        }

        const pdf::PDFObjectReference reference = m_previews[size_t(index)].result.reference;
        std::erase(m_settings.excludedImages, reference);
        if (checked)
        {
            m_settings.excludedImages.push_back(reference);
        }
        schedulePreview();
    });
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1000, 750));
    startPreview();
}

PDFOCRCompressionPreviewDialog::~PDFOCRCompressionPreviewDialog()
{
    if (m_token)
    {
        m_token->cancel();
    }
    m_future.waitForFinished();
}

void PDFOCRCompressionPreviewDialog::setPageIndex(int index)
{
    if (index >= 0 && index < m_pageComboBox->count())
    {
        m_pageComboBox->setCurrentIndex(index);
    }
}

void PDFOCRCompressionPreviewDialog::updateThresholdControls()
{
    // The threshold matters only for the conversion of the scans of a text
    const bool convertsScans = m_settings.mode == pdf::PDFOCRCompressionMode::BitonalTextScans;
    m_thresholdComboBox->setEnabled(convertsScans);
    m_thresholdSpinBox->setEnabled(convertsScans && m_settings.thresholdMethod == pdf::PDFOCRThresholdMethod::Manual);
}

void PDFOCRCompressionPreviewDialog::schedulePreview()
{
    m_previewTimer->start();
}

void PDFOCRCompressionPreviewDialog::startPreview()
{
    if (m_token)
    {
        m_token->cancel();
    }

    const int generation = ++m_generation;
    m_token = std::make_shared<pdf::PDFOCRCancelToken>();
    std::shared_ptr<pdf::PDFOCRCancelToken> token = m_token;

    const int pageIndex = m_pageComboBox->currentIndex();
    if (pageIndex < 0 || size_t(pageIndex) >= m_pages.size())
    {
        m_statusLabel->setText(tr("There is no page to preview."));
        return;
    }

    const pdf::PDFInteger page = m_pages[size_t(pageIndex)];
    m_previousPageButton->setEnabled(pageIndex > 0);
    m_nextPageButton->setEnabled(pageIndex + 1 < m_pageComboBox->count());
    m_statusLabel->setText(tr("Compressing the images of the page %1...").arg(page + 1));

    const pdf::PDFDocument* document = m_document;
    const std::vector<pdf::PDFInteger> pages = m_pages;
    const pdf::PDFOCRCompressionSettings settings = m_settings;

    // The previous computation is waited for, it was cancelled above
    m_future.waitForFinished();
    m_future = QtConcurrent::run([this, document, page, pages, settings, generation, token]()
    {
        std::vector<pdf::PDFOCRImageCompressor::Preview> previews;
        try
        {
            previews = pdf::PDFOCRImageCompressor::createPreview(document, page, pages, settings, token.get());
        }
        catch (...)
        {
            // The preview stays empty
        }

        if (!token->isOperationCancelled())
        {
            QMetaObject::invokeMethod(this, [this, generation, previews = std::move(previews)]() mutable { onPreviewReady(generation, std::move(previews)); }, Qt::QueuedConnection);
        }
    });
}

void PDFOCRCompressionPreviewDialog::onPreviewReady(int generation, std::vector<pdf::PDFOCRImageCompressor::Preview> previews)
{
    if (generation != m_generation)
    {
        return;
    }

    const int previousImage = m_imageComboBox->currentIndex();
    m_previews = std::move(previews);

    m_updatingUi = true;
    m_imageComboBox->clear();
    for (const pdf::PDFOCRImageCompressor::Preview& preview : m_previews)
    {
        m_imageComboBox->addItem(tr("%1 x %2 px, %3").arg(preview.result.pixelSize.width()).arg(preview.result.pixelSize.height())
                                 .arg(pdf::PDFOCRCompressionImageResult::getImageClassName(preview.result.imageClass)));
    }
    m_imageComboBox->setCurrentIndex(previousImage >= 0 && previousImage < m_imageComboBox->count() ? previousImage : 0);
    m_updatingUi = false;

    // Total of the page
    qint64 originalBytes = 0;
    qint64 newBytes = 0;
    for (const pdf::PDFOCRImageCompressor::Preview& preview : m_previews)
    {
        originalBytes += preview.result.originalBytes;
        newBytes += preview.result.action == pdf::PDFOCRCompressionImageResult::Action::Compressed ? preview.result.newBytes : preview.result.originalBytes;
    }
    m_statusLabel->setText(m_previews.empty() ? tr("The page has no image, which could be compressed.")
                                              : tr("Images of the page: %1 -> %2.").arg(pdf::PDFOCRCompressionReport::formatBytes(originalBytes), pdf::PDFOCRCompressionReport::formatBytes(newBytes)));
    updateImage();
}

void PDFOCRCompressionPreviewDialog::updateImage()
{
    const int index = m_imageComboBox->currentIndex();
    if (index < 0 || size_t(index) >= m_previews.size())
    {
        m_view->setImages(QImage(), QImage(), tr("No image on this page."));
        m_infoLabel->clear();
        m_excludeCheckBox->setEnabled(false);
        return;
    }

    const pdf::PDFOCRImageCompressor::Preview& preview = m_previews[size_t(index)];
    const pdf::PDFOCRCompressionImageResult& result = preview.result;

    QString message;
    if (result.action != pdf::PDFOCRCompressionImageResult::Action::Compressed)
    {
        message = pdf::PDFOCRCompressionImageResult::getActionName(result.action);
    }
    m_view->setImages(preview.original, preview.compressed, message);

    QStringList info;
    info << tr("Class: %1").arg(pdf::PDFOCRCompressionImageResult::getImageClassName(result.imageClass));
    info << tr("Original: %1, %2").arg(result.originalFilter.isEmpty() ? tr("uncompressed") : result.originalFilter, pdf::PDFOCRCompressionReport::formatBytes(result.originalBytes));
    if (result.action == pdf::PDFOCRCompressionImageResult::Action::Compressed || result.action == pdf::PDFOCRCompressionImageResult::Action::KeptLarger)
    {
        info << tr("New: %1, %2").arg(result.encoding, pdf::PDFOCRCompressionReport::formatBytes(result.newBytes));
    }
    info << tr("Result: %1").arg(pdf::PDFOCRCompressionImageResult::getActionName(result.action));
    if (!result.message.isEmpty())
    {
        info << result.message;
    }
    m_infoLabel->setText(info.join(QStringLiteral("; ")));

    m_updatingUi = true;
    m_excludeCheckBox->setEnabled(true);
    m_excludeCheckBox->setChecked(m_settings.isExcluded(result.reference));
    m_updatingUi = false;
}

}   // namespace pdfviewer
