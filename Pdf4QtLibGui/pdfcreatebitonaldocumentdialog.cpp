// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfcreatebitonaldocumentdialog.h"
#include "ui_pdfcreatebitonaldocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdrawspacecontroller.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfrenderer.h"

#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QScopeGuard>
#include <QMouseEvent>
#include <QToolTip>

#include <iterator>
#include <map>
#include <numeric>

#include "pdfdbgheap.h"

namespace pdfviewer
{

/// Delay between a change of the settings and the start of the preview job
static constexpr int PREVIEW_UPDATE_DELAY_MSECS = 150;

/// Ratio between the size of the page rasterized for the preview and the size of the
/// preview pane. A value greater than one keeps the preview sharp when the image is
/// scaled down by a smooth transformation.
static constexpr qreal PREVIEW_QUALITY_FACTOR = 2.0;

/// Returns the number of the thumbnails scheduled into the execution policy at once.
/// The interactive preview uses the same thread pool, so the queue must stay short
/// enough for a preview request to get in between two batches of the thumbnails.
static size_t getThumbnailBatchSize()
{
    return size_t(qMax(1, pdf::PDFExecutionPolicy::getIdealThreadCount(pdf::PDFExecutionPolicy::Scope::Page)));
}

/// Creates an item of the list, whose thumbnail has not been generated yet. The size
/// of the item is fixed and it does not depend on the thumbnail, so the items do not
/// jump around in the list as the thumbnails are arriving one by one.
/// \param listWidget List widget
/// \param itemSize Size of the item
/// \param text Text displayed in the item
/// \param toolTip Tool tip of the item
static QListWidgetItem* createPendingListItem(QListWidget* listWidget,
                                              QSize itemSize,
                                              const QString& text,
                                              const QString& toolTip)
{
    QListWidgetItem* item = new QListWidgetItem(listWidget);
    item->setSizeHint(itemSize);
    item->setText(text);
    item->setToolTip(toolTip);

    Qt::ItemFlags flags = item->flags();
    flags.setFlag(Qt::ItemIsEditable, true);
    item->setFlags(flags);

    return item;
}

PDFCreateBitonalDocumentPreviewWidget::PDFCreateBitonalDocumentPreviewWidget(QWidget* parent) :
    QWidget(parent)
{

}

PDFCreateBitonalDocumentPreviewWidget::~PDFCreateBitonalDocumentPreviewWidget()
{

}

void PDFCreateBitonalDocumentPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    // Caption rect
    QRect captionRect = rect();
    captionRect.setHeight(painter.fontMetrics().lineSpacing() * 2);

    painter.fillRect(captionRect, QColor::fromRgb(0, 0, 128, 255));

    if (!m_caption.isEmpty())
    {
        painter.setPen(Qt::white);
        painter.drawText(captionRect, m_caption, QTextOption(Qt::AlignCenter));
    }

    QRect imageRect = rect();
    imageRect.setTop(captionRect.bottom());
    imageRect = imageRect.adjusted(16, 16, -32, -32);

    if (!imageRect.isValid())
    {
        return;
    }

    if (!m_image.isNull())
    {
        QRect imageDrawRect = imageRect;
        imageDrawRect.setSize(m_image.size().scaled(imageRect.size(), Qt::KeepAspectRatio));
        imageDrawRect.moveCenter(imageRect.center());
        painter.drawImage(imageDrawRect, m_image);
    }
    else if (m_generating)
    {
        // Image is being created in the background, so the user is informed
        // that something is happening instead of seeing an empty area.
        painter.setPen(Qt::darkGray);
        painter.drawText(imageRect, tr("Generating..."), QTextOption(Qt::AlignCenter));
    }
}

void PDFCreateBitonalDocumentPreviewWidget::setCaption(QString caption)
{
    if (m_caption != caption)
    {
        m_caption = caption;
        update();
    }
}

void PDFCreateBitonalDocumentPreviewWidget::setImage(QImage image)
{
    m_image = std::move(image);
    update();
}

void PDFCreateBitonalDocumentPreviewWidget::setGenerating(bool generating)
{
    if (m_generating != generating)
    {
        m_generating = generating;
        update();
    }
}

PDFCreateBitonalDocumentDialog::PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document,
                                                               pdf::PDFDrawWidgetProxy* proxy,
                                                               const pdf::PDFCMS* cms,
                                                               pdf::PDFProgress* progress,
                                                               QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateBitonalDocumentDialog),
    m_document(document),
    m_proxy(proxy),
    m_cms(cms),
    m_createBitonalDocumentButton(nullptr),
    m_conversionInProgress(false),
    m_leftPreviewWidget(new PDFCreateBitonalDocumentPreviewWidget(this)),
    m_rightPreviewWidget(new PDFCreateBitonalDocumentPreviewWidget(this)),
    m_progress(progress),
    m_optionalContentActivity(nullptr),
    m_rasterizerPool(nullptr)
{
    ui->setupUi(this);

    m_leftPreviewWidget->setCaption(tr("ORIGINAL"));
    m_rightPreviewWidget->setCaption(tr("BITONAL"));

    ui->mainGridLayout->addWidget(m_leftPreviewWidget, 1, 1);
    ui->mainGridLayout->addWidget(m_rightPreviewWidget, 1, 2);

    m_optionalContentActivity = new pdf::PDFOptionalContentActivity(document, pdf::OCUsage::Export, this);
    m_rasterizerPool = new pdf::PDFRasterizerPool(m_document,
                                                  m_proxy->getFontCache(),
                                                  m_proxy->getCMSManager(),
                                                  m_optionalContentActivity,
                                                  pdf::PDFBitonalDocumentCreator::getPageRasterizationFeatures(),
                                                  m_proxy->getMeshQualitySettings(),
                                                  pdf::PDFRasterizerPool::getDefaultRasterizerCount(),
                                                  m_proxy->getRendererEngine(),
                                                  this);

    m_creator.emplace(m_document, m_rasterizerPool, m_progress);
    m_imageReferences = m_creator->getConvertibleImages();

    ui->conversionSourceComboBox->addItem(tr("Images"), static_cast<int>(ConversionSource::Images));
    ui->conversionSourceComboBox->addItem(tr("Whole pages"), static_cast<int>(ConversionSource::Pages));

    ui->conversionMethodComboBox->addItem(tr("Automatic (Otsu's 1D method)"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Automatic));
    ui->conversionMethodComboBox->addItem(tr("User-defined threshold"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Manual));
    ui->conversionMethodComboBox->addItem(tr("Adaptive thresholding"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Adaptive));
    ui->conversionMethodComboBox->addItem(tr("Dithering (Floyd-Steinberg)"), static_cast<int>(pdf::PDFImageConversion::ConversionMethod::Dither));

    // Rasterizing the page is expensive, so the preview is not updated
    // while the resolution is being typed. The range is taken from the core, which
    // clamps the resolution anyway - a page rasterized at an extreme resolution
    // would need gigabytes of memory.
    ui->resolutionEditBox->setKeyboardTracking(false);
    ui->resolutionEditBox->setRange(pdf::PDFBitonalDocumentCreator::MINIMUM_DPI_RESOLUTION,
                                    pdf::PDFBitonalDocumentCreator::MAXIMUM_DPI_RESOLUTION);
    ui->resolutionEditBox->setValue(m_creator->getEstimatedDpiResolution());

    // Creating the preview is expensive, so a series of quick changes of the settings
    // (holding the arrow of a spin box) starts a single job instead of one per change
    m_previewUpdateTimer.setSingleShot(true);
    m_previewUpdateTimer.setInterval(PREVIEW_UPDATE_DELAY_MSECS);
    connect(&m_previewUpdateTimer, &QTimer::timeout, this, &PDFCreateBitonalDocumentDialog::startPreviewGeneration);

    // The conversion itself is the accepting action of the dialog - a successfully
    // converted document is the result, so there is no separate confirmation button.
    m_createBitonalDocumentButton = ui->buttonBox->addButton(tr("Perform"), QDialogButtonBox::AcceptRole);
    m_createBitonalDocumentButton->setDefault(true);
    connect(m_createBitonalDocumentButton, &QPushButton::clicked, this, &PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked);
    connect(ui->conversionSourceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSourceChanged);
    connect(ui->conversionMethodComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSettingsChanged);
    connect(ui->imageListWidget, &QListWidget::currentItemChanged, this, &PDFCreateBitonalDocumentDialog::updatePreview);
    connect(ui->thresholdEditBox, qOverload<int>(&QSpinBox::valueChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSettingsChanged);
    connect(ui->resolutionEditBox, qOverload<int>(&QSpinBox::valueChanged), this, &PDFCreateBitonalDocumentDialog::onConversionSettingsChanged);

    // Conversion mode can be set to many items at once using the context menu, but the
    // preview still follows the current item, which exists in the extended selection
    // as well. Clicking a mark is consumed by the delegate, so it does not change
    // the selection.
    ui->imageListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->imageListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->imageListWidget, &QListWidget::customContextMenuRequested, this, &PDFCreateBitonalDocumentDialog::onItemListContextMenuRequested);

    // Results of the background jobs are emitted by the worker threads, so they must
    // be delivered into the GUI thread using a queued connection. Connections are
    // created before the first job is started.
    connect(this, &PDFCreateBitonalDocumentDialog::thumbnailReady, this, &PDFCreateBitonalDocumentDialog::onThumbnailReady, Qt::QueuedConnection);
    connect(this, &PDFCreateBitonalDocumentDialog::previewReady, this, &PDFCreateBitonalDocumentDialog::onPreviewReady, Qt::QueuedConnection);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1024, 768));
    updateUi();
    pdf::PDFWidgetUtils::style(this);

    ImagePreviewDelegate* delegate = new ImagePreviewDelegate(&m_itemsToBeConverted, this);
    connect(delegate, &ImagePreviewDelegate::conversionModeChanged, this, &PDFCreateBitonalDocumentDialog::onConversionModeChanged);
    ui->imageListWidget->setItemDelegate(delegate);

    setGeometry(parent->geometry());

    loadItems();
    updatePreview();
}

PDFCreateBitonalDocumentDialog::~PDFCreateBitonalDocumentDialog()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    // Workers use the document creator and the rasterizer pool, so none of them can be
    // running when the dialog is being destroyed. Both jobs are cancelled first and
    // joined afterwards, so they can be finishing their last uninterruptible step in
    // parallel.
    cancelJob(m_thumbnailJob);
    cancelJob(m_previewJob);
    finishJob(m_thumbnailJob);
    finishJob(m_previewJob);

    delete ui;
}

void PDFCreateBitonalDocumentDialog::done(int r)
{
    if (m_conversionInProgress)
    {
        // The conversion cannot be interrupted and its worker writes into the dialog,
        // so the dialog must not be closed while it is running. Buttons are disabled
        // at that time, but the dialog could still be closed by the escape key.
        return;
    }

    // Background jobs are cancelled as soon as the dialog is being closed, so the user
    // does not wait for a thumbnail or a preview, which nobody is going to see. The
    // workers are joined in the destructor.
    cancelJob(m_thumbnailJob);
    cancelJob(m_previewJob);

    QDialog::done(r);
}

void PDFCreateBitonalDocumentDialog::startJob(AsyncJob& job, std::function<void(int, const pdf::PDFOperationControl*)> worker)
{
    cancelJob(job);

    // Futures of the runs, which have already finished, are not needed anymore
    std::erase_if(job.futures, [](const QFuture<void>& future) { return future.isFinished(); });

    const int generation = job.generation;
    auto cancelToken = std::make_shared<PDFOperationCancelToken>();

    job.cancelToken = cancelToken;
    job.isRunning = true;

    QFuture<void> future = QtConcurrent::run([worker = std::move(worker), generation, cancelToken]()
    {
        try
        {
            // The run can be cancelled before the thread pool gets to it at all
            if (!cancelToken->isOperationCancelled())
            {
                worker(generation, cancelToken.get());
            }
        }
        catch (const pdf::PDFException&)
        {
            // A broken image just stays empty in the list or in the preview
        }
        catch (...)
        {
            // No exception is allowed to escape into the future. The future is waited
            // for in the destructor of the dialog, where a rethrown exception would
            // terminate the application.
        }
    });

    job.futures.push_back(future);
    job.futureWatcher.emplace();
    connect(&job.futureWatcher.value(), &QFutureWatcher<void>::finished, this, [this, &job, generation]() { onJobFinished(job, generation); });
    job.futureWatcher->setFuture(future);
}

void PDFCreateBitonalDocumentDialog::cancelJob(AsyncJob& job)
{
    // Generation of the job is increased, so the results, which the cancelled worker
    // still manages to emit, are recognized as obsolete and dropped by the slots.
    ++job.generation;

    if (job.cancelToken)
    {
        job.cancelToken->cancel();
        job.cancelToken.reset();
    }

    // The watcher of a cancelled run is not interesting anymore. Destroying it does
    // not touch the worker at all, it is just not watched anymore - the worker is
    // kept alive by its future stored in the job.
    job.futureWatcher.reset();
    job.isRunning = false;
}

void PDFCreateBitonalDocumentDialog::finishJob(AsyncJob& job)
{
    cancelJob(job);

    for (QFuture<void>& future : job.futures)
    {
        future.waitForFinished();
    }

    job.futures.clear();
}

void PDFCreateBitonalDocumentDialog::onJobFinished(AsyncJob& job, int generation)
{
    if (generation != job.generation)
    {
        // This run has been superseded by a newer one, the job is considered
        // running until its newest run finishes.
        return;
    }

    job.isRunning = false;

    if (&job == &m_thumbnailJob)
    {
        finishPendingThumbnails();
    }
    else if (&job == &m_previewJob && !m_isPreviewDelivered)
    {
        // The worker has ended without a result - it has been interrupted, or it has
        // thrown. The panes must not stay in the generating state forever.
        m_leftPreviewWidget->setGenerating(false);
        m_rightPreviewWidget->setGenerating(false);
    }

    updateUi();
}

void PDFCreateBitonalDocumentDialog::finishPendingThumbnails()
{
    // The thumbnail job has ended, so an item, whose thumbnail has not arrived, will
    // never get one. It must not stay pending, because the conversion is enabled now
    // and the user has to see, which items cannot be converted.
    for (int itemIndex = 0; itemIndex < int(m_itemsToBeConverted.size()); ++itemIndex)
    {
        ConversionItemInfo& info = m_itemsToBeConverted[size_t(itemIndex)];

        if (info.thumbnailState != ConversionItemInfo::ThumbnailState::Pending)
        {
            continue;
        }

        info.thumbnailState = ConversionItemInfo::ThumbnailState::Failed;

        if (!info.isModeAvailable(info.mode))
        {
            info.mode = ConversionItemInfo::Mode::Original;
        }

        if (QListWidgetItem* item = ui->imageListWidget->item(itemIndex))
        {
            item->setText(tr("Not available"));
        }
    }
}

void PDFCreateBitonalDocumentDialog::onPerformFinished()
{
    const ConversionResult result = m_future.result();

    // The flag must be cleared before the dialog is closed - closing is refused while
    // the conversion is running, because its worker writes into the dialog.
    m_conversionInProgress = false;

    if (!result.isConverted)
    {
        m_bitonalDocument = pdf::PDFDocument();
        updateUi();
        QMessageBox::warning(this, tr("Create Bitonal Document"), tr("The bitonal document has not been created. No item of the document could be converted."));
        return;
    }

    if (result.failedItemCount > 0)
    {
        // The document has been created, but a part of it is left in the original form.
        // A successful thumbnail does not rule this out - the final conversion runs at
        // a different resolution and it can fail because of its demands. The user has
        // asked for the conversion of these items, so the partial result is accepted
        // only when the user agrees with it.
        const QString message = tr("%1 of %2 items could not be converted and they are left unchanged. Do you want to use the partially converted document?")
                                    .arg(result.failedItemCount)
                                    .arg(result.failedItemCount + result.convertedItemCount);

        if (QMessageBox::question(this, tr("Create Bitonal Document"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            m_bitonalDocument = pdf::PDFDocument();
            updateUi();
            return;
        }
    }

    // The converted document is the result of the dialog, so there is nothing else
    // to be confirmed here - the caller takes the document and the dialog is closed.
    accept();
}

PDFCreateBitonalDocumentDialog::ConversionResult PDFCreateBitonalDocumentDialog::createBitonalDocument(const ConversionSettings& settings)
{
    ConversionResult result;
    result.isConverted = m_creator->createBitonalDocument(settings);
    result.convertedItemCount = m_creator->getConvertedItemCount();
    result.failedItemCount = m_creator->getFailedItemCount();

    if (!result.isConverted)
    {
        m_bitonalDocument = pdf::PDFDocument();
        return result;
    }

    m_bitonalDocument = m_creator->takeBitonalDocument();
    return result;
}

void PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    // All inputs of the conversion are snapshotted here, in the GUI thread, and the
    // worker gets its own copy of them. The worker must not read the state of the
    // dialog, because the user could change it while the conversion is running.
    ConversionSettings settings = getConversionSettings();

    m_bitonalDocument = pdf::PDFDocument();
    m_conversionInProgress = true;
    m_future = QtConcurrent::run([this, settings = std::move(settings)]() { return createBitonalDocument(settings); });
    m_futureWatcher.emplace();
    connect(&m_futureWatcher.value(), &QFutureWatcher<ConversionResult>::finished, this, &PDFCreateBitonalDocumentDialog::onPerformFinished);
    m_futureWatcher->setFuture(m_future);
    updateUi();
}

void PDFCreateBitonalDocumentDialog::onConversionSourceChanged()
{
    m_conversionSource = getSelectedConversionSource();

    loadItems();
    updateUi();
    updatePreview();
}

void PDFCreateBitonalDocumentDialog::onConversionSettingsChanged()
{
    updateUi();
    updatePreview();
}

void PDFCreateBitonalDocumentDialog::onConversionModeChanged()
{
    ui->imageListWidget->viewport()->update();

    // The right pane shows, what the mode does with the current item
    updatePreview();
}

void PDFCreateBitonalDocumentDialog::onItemListContextMenuRequested(const QPoint& pos)
{
    if (m_itemsToBeConverted.empty() || m_conversionInProgress)
    {
        return;
    }

    const qsizetype selectedCount = ui->imageListWidget->selectionModel()->selectedIndexes().count();

    QMenu menu(ui->imageListWidget);
    menu.addSection(selectedCount > 0 ? tr("Selected items (%1)").arg(selectedCount) : tr("All items"));

    auto addModeAction = [this, &menu](const QString& text, ConversionItemInfo::Mode mode)
    {
        QAction* action = menu.addAction(text);
        connect(action, &QAction::triggered, this, [this, mode]() { setConversionModeToItems(mode); });
    };

    addModeAction(tr("Convert using the selected method"), ConversionItemInfo::Mode::Algorithm);
    addModeAction(tr("Leave unchanged"), ConversionItemInfo::Mode::Original);
    addModeAction(tr("Fill with black"), ConversionItemInfo::Mode::FillBlack);
    addModeAction(tr("Fill with white"), ConversionItemInfo::Mode::FillWhite);

    menu.exec(ui->imageListWidget->mapToGlobal(pos));
}

void PDFCreateBitonalDocumentDialog::setConversionModeToItems(ConversionItemInfo::Mode mode)
{
    std::vector<int> rows;

    for (const QModelIndex& index : ui->imageListWidget->selectionModel()->selectedIndexes())
    {
        rows.push_back(index.row());
    }

    if (rows.empty())
    {
        // Nothing is selected, so the mode is applied to the whole document
        rows.resize(m_itemsToBeConverted.size());
        std::iota(rows.begin(), rows.end(), 0);
    }

    bool isChanged = false;

    for (const int row : rows)
    {
        if (row < 0 || row >= int(m_itemsToBeConverted.size()))
        {
            continue;
        }

        ConversionItemInfo& info = m_itemsToBeConverted[size_t(row)];

        if (info.mode != mode && info.isModeAvailable(mode))
        {
            info.mode = mode;
            isChanged = true;
        }
    }

    if (isChanged)
    {
        onConversionModeChanged();
    }
}

PDFCreateBitonalDocumentDialog::ConversionSettings PDFCreateBitonalDocumentDialog::getConversionSettings() const
{
    ConversionSettings settings;
    settings.conversionSource = getSelectedConversionSource();
    settings.conversionMethod = getSelectedConversionMethod();
    settings.manualThreshold = ui->thresholdEditBox->value();
    settings.dpiResolution = ui->resolutionEditBox->value();

    // Only the part of the item, which the conversion needs, is copied - the state
    // of the thumbnail is a matter of the list, not of the conversion.
    settings.items.assign(m_itemsToBeConverted.cbegin(), m_itemsToBeConverted.cend());

    return settings;
}

void PDFCreateBitonalDocumentDialog::loadItems()
{
    // The list is being rebuilt, so the results of the running jobs do not belong
    // to anything anymore.
    cancelJob(m_thumbnailJob);
    cancelJob(m_previewJob);

    QSignalBlocker blocker(ui->imageListWidget);

    ui->imageListWidget->clear();
    m_itemsToBeConverted.clear();

    m_cachedPageImage = QImage();
    m_cachedPageIndex = -1;
    m_cachedPageDpiResolution = 0;

    const QSize iconSize(QSize(256, 256));
    ui->imageListWidget->setIconSize(iconSize);
    const QSize thumbnailSize = iconSize * ui->imageListWidget->devicePixelRatioF();

    // Size of the items is fixed - it must not depend on the thumbnail, which is not
    // available yet, otherwise the items would be jumping around in the list as the
    // thumbnails are arriving.
    const int textHeight = ui->imageListWidget->fontMetrics().lineSpacing();
    const QSize itemSize(iconSize.width() + 8, iconSize.height() + textHeight + 12);

    switch (m_conversionSource)
    {
        case ConversionSource::Images:
            createImageItems(itemSize);
            break;

        case ConversionSource::Pages:
            createPageItems(itemSize);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (ui->imageListWidget->count() > 0)
    {
        ui->imageListWidget->setCurrentRow(0);
    }

    startThumbnailGeneration(thumbnailSize);
    updateUi();
}

void PDFCreateBitonalDocumentDialog::createImageItems(QSize itemSize)
{
    for (pdf::PDFObjectReference reference : m_imageReferences)
    {
        ConversionItemInfo conversionItemInfo;
        conversionItemInfo.imageReference = reference;
        m_itemsToBeConverted.push_back(conversionItemInfo);

        const int itemIndex = int(m_itemsToBeConverted.size());
        createPendingListItem(ui->imageListWidget, itemSize, tr("Generating..."), tr("Image %1").arg(itemIndex));
    }
}

void PDFCreateBitonalDocumentDialog::createPageItems(QSize itemSize)
{
    const pdf::PDFCatalog* catalog = m_document->getCatalog();

    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        ConversionItemInfo conversionItemInfo;
        conversionItemInfo.pageIndex = pdf::PDFInteger(pageIndex);
        m_itemsToBeConverted.push_back(conversionItemInfo);

        createPendingListItem(ui->imageListWidget, itemSize, tr("Generating..."), tr("Page %1").arg(pageIndex + 1));
    }
}

void PDFCreateBitonalDocumentDialog::startThumbnailGeneration(QSize thumbnailSize)
{
    if (m_itemsToBeConverted.empty())
    {
        return;
    }

    ThumbnailRequest request;
    request.conversionSource = m_conversionSource;
    request.thumbnailSize = thumbnailSize;

    for (const ConversionItemInfo& info : m_itemsToBeConverted)
    {
        switch (m_conversionSource)
        {
            case ConversionSource::Images:
                request.imageReferences.push_back(info.imageReference);
                break;

            case ConversionSource::Pages:
                request.pageIndices.push_back(info.pageIndex);
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }

    startJob(m_thumbnailJob, [this, request](int generation, const pdf::PDFOperationControl* operationControl)
    {
        generateThumbnails(generation, request, operationControl);
    });
}

void PDFCreateBitonalDocumentDialog::generateThumbnails(int generation,
                                                        const ThumbnailRequest& request,
                                                        const pdf::PDFOperationControl* operationControl)
{
    switch (request.conversionSource)
    {
        case ConversionSource::Images:
        {
            std::vector<size_t> indices(request.imageReferences.size(), 0);
            std::iota(indices.begin(), indices.end(), size_t(0));

            auto processImage = [this, generation, operationControl, &request](size_t index)
            {
                if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
                {
                    return;
                }

                QImage image = m_creator->getDecodedImage(request.imageReferences[index], operationControl);

                if (!image.isNull())
                {
                    image = image.scaled(request.thumbnailSize.width(), request.thumbnailSize.height(), Qt::KeepAspectRatio, Qt::FastTransformation);
                }

                if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
                {
                    // Image can be incomplete, because its decoding has been interrupted
                    return;
                }

                Q_EMIT thumbnailReady(generation, int(index), image);
            };

            // Thumbnails are scheduled in small batches. Enqueuing the whole document
            // at once would push an interactive preview request, which uses the same
            // pool, behind every single thumbnail of the document.
            const size_t batchSize = getThumbnailBatchSize();

            for (size_t first = 0; first < indices.size(); first += batchSize)
            {
                if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
                {
                    return;
                }

                const size_t last = qMin(first + batchSize, indices.size());
                pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, indices.cbegin() + first, indices.cbegin() + last, processImage);
            }
            break;
        }

        case ConversionSource::Pages:
        {
            // Pages are rendered in parallel and they can be finished in any order,
            // so the item, which a rendered page belongs to, is looked up by its index.
            std::map<pdf::PDFInteger, int> itemIndices;
            for (size_t index = 0; index < request.pageIndices.size(); ++index)
            {
                itemIndices[request.pageIndices[index]] = int(index);
            }

            const QSize thumbnailSize = request.thumbnailSize;
            auto pageSizeGetter = [thumbnailSize](const pdf::PDFPage* page) -> QSize
            {
                QSizeF size = page->getMediaBox().size();
                size.scale(thumbnailSize.width(), thumbnailSize.height(), Qt::KeepAspectRatio);
                return size.toSize().expandedTo(QSize(1, 1));
            };

            auto pageImageProcessor = [this, generation, operationControl, &itemIndices](pdf::PDFInteger pageIndex, QImage image)
            {
                if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
                {
                    return;
                }

                auto it = itemIndices.find(pageIndex);
                if (it != itemIndices.cend())
                {
                    Q_EMIT thumbnailReady(generation, it->second, image);
                }
            };

            // Pages are rendered in small batches for the same reason as the images
            const size_t batchSize = getThumbnailBatchSize();

            for (size_t first = 0; first < request.pageIndices.size(); first += batchSize)
            {
                if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
                {
                    return;
                }

                const size_t last = qMin(first + batchSize, request.pageIndices.size());
                const std::vector<pdf::PDFInteger> batch(request.pageIndices.cbegin() + first, request.pageIndices.cbegin() + last);
                m_creator->renderPages(batch, pageSizeGetter, pageImageProcessor, operationControl);
            }
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFCreateBitonalDocumentDialog::generatePreview(int generation,
                                                     const PreviewRequest& request,
                                                     const pdf::PDFOperationControl* operationControl)
{
    QImage image;

    switch (request.conversionSource)
    {
        case ConversionSource::Images:
            image = m_creator->getDecodedImage(request.item.imageReference, operationControl);
            break;

        case ConversionSource::Pages:
        {
            if (!request.cachedPageImage.isNull())
            {
                // Page has already been rasterized for the previous preview and only
                // the conversion settings have changed since then.
                image = request.cachedPageImage;
            }
            else if (request.item.pageIndex >= 0)
            {
                const pdf::PDFCatalog* catalog = m_document->getCatalog();
                const pdf::PDFPage* page = size_t(request.item.pageIndex) < catalog->getPageCount() ? catalog->getPage(size_t(request.item.pageIndex)) : nullptr;

                if (page)
                {
                    const QSize size = pdf::PDFBitonalDocumentCreator::getPageImageSize(page, request.dpiResolution);
                    image = m_creator->renderPage(request.item.pageIndex, size, operationControl);
                }
            }
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    QImage bitonalImage;

    switch (request.item.mode)
    {
        case ConversionItemInfo::Mode::Algorithm:
            bitonalImage = pdf::PDFBitonalDocumentCreator::convertImageToBitonal(image, request.conversionMethod, request.manualThreshold, nullptr, operationControl);
            break;

        case ConversionItemInfo::Mode::Original:
            // Nothing is going to change, so both panes show the same image
            bitonalImage = image;
            break;

        case ConversionItemInfo::Mode::FillBlack:
        case ConversionItemInfo::Mode::FillWhite:
            bitonalImage = pdf::PDFBitonalDocumentCreator::createFillPreviewImage(image, request.item.isFilledByBlack());
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
    {
        return;
    }

    if (bitonalImage.isNull())
    {
        // Conversion has failed, so the original image is not displayed either
        image = QImage();
    }

    Q_EMIT previewReady(generation, image, bitonalImage);
}

void PDFCreateBitonalDocumentDialog::onThumbnailReady(int generation, int itemIndex, QImage thumbnail)
{
    if (generation != m_thumbnailJob.generation)
    {
        // Thumbnail belongs to an obsolete run. The list of the items has been
        // rebuilt in the meantime, so the index does not mean anything anymore.
        return;
    }

    if (itemIndex < 0 || itemIndex >= int(m_itemsToBeConverted.size()) || itemIndex >= ui->imageListWidget->count())
    {
        Q_ASSERT(false);
        return;
    }

    ConversionItemInfo& info = m_itemsToBeConverted[itemIndex];
    QListWidgetItem* item = ui->imageListWidget->item(itemIndex);

    if (thumbnail.isNull())
    {
        info.thumbnailState = ConversionItemInfo::ThumbnailState::Failed;

        if (!info.isModeAvailable(info.mode))
        {
            // The image cannot be decoded or the page cannot be rendered, so the
            // algorithm has nothing to work with. A page can still be replaced by a
            // solid fill, an image cannot - see isModeAvailable.
            info.mode = ConversionItemInfo::Mode::Original;
        }

        item->setText(tr("Not available"));
    }
    else
    {
        info.thumbnailState = ConversionItemInfo::ThumbnailState::Ready;
        item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
        item->setText(getItemCaption(itemIndex));
    }
}

void PDFCreateBitonalDocumentDialog::onPreviewReady(int generation, QImage originalImage, QImage bitonalImage)
{
    if (generation != m_previewJob.generation)
    {
        // Preview belongs to an obsolete run, the settings have changed since then
        return;
    }

    if (m_conversionSource == ConversionSource::Pages && !originalImage.isNull())
    {
        // Rasterized page is cached, so changing the conversion method or the
        // threshold does not rasterize the same page again.
        m_cachedPageImage = originalImage;
        m_cachedPageIndex = m_previewPageIndex;
        m_cachedPageDpiResolution = m_previewDpiResolution;
    }

    m_previewImageLeft = std::move(originalImage);
    m_previewImageRight = std::move(bitonalImage);
    m_isPreviewDelivered = true;

    m_leftPreviewWidget->setGenerating(false);
    m_rightPreviewWidget->setGenerating(false);
    m_leftPreviewWidget->setImage(m_previewImageLeft);
    m_rightPreviewWidget->setImage(m_previewImageRight);
}

QString PDFCreateBitonalDocumentDialog::getItemCaption(int itemIndex) const
{
    if (itemIndex < 0 || itemIndex >= int(m_itemsToBeConverted.size()))
    {
        return QString();
    }

    switch (m_conversionSource)
    {
        case ConversionSource::Images:
            return tr("Image %1").arg(itemIndex + 1);

        case ConversionSource::Pages:
            return tr("Page %1").arg(m_itemsToBeConverted[itemIndex].pageIndex + 1);

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFCreateBitonalDocumentDialog::getPreviewDpiResolution(pdf::PDFInteger pageIndex, int dpiResolution) const
{
    if (m_conversionSource != ConversionSource::Pages || pageIndex < 0)
    {
        // Images are converted as they are decoded, no resolution is involved
        return dpiResolution;
    }

    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    const pdf::PDFPage* page = size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(size_t(pageIndex)) : nullptr;

    if (!page)
    {
        return dpiResolution;
    }

    const QSize outputSize = pdf::PDFBitonalDocumentCreator::getPageImageSize(page, dpiResolution);
    const QSize paneSize = m_rightPreviewWidget->size() * m_rightPreviewWidget->devicePixelRatioF() * PREVIEW_QUALITY_FACTOR;

    if (outputSize.isEmpty() || paneSize.isEmpty())
    {
        // The panes have no size yet, so there is nothing to derive the resolution from
        return dpiResolution;
    }

    if (outputSize.width() <= paneSize.width() && outputSize.height() <= paneSize.height())
    {
        // The page at the output resolution is small enough to be displayed as it is,
        // so the preview shows exactly, what the conversion is going to produce
        return dpiResolution;
    }

    const double scale = qMin(double(paneSize.width()) / double(outputSize.width()),
                              double(paneSize.height()) / double(outputSize.height()));

    return qBound(pdf::PDFBitonalDocumentCreator::MINIMUM_DPI_RESOLUTION, qRound(dpiResolution * scale), dpiResolution);
}

void PDFCreateBitonalDocumentDialog::updateUi()
{
    m_conversionSource = getSelectedConversionSource();

    const pdf::PDFImageConversion::ConversionMethod conversionMethod = getSelectedConversionMethod();
    const bool usesThreshold = conversionMethod == pdf::PDFImageConversion::ConversionMethod::Manual ||
                               conversionMethod == pdf::PDFImageConversion::ConversionMethod::Dither;
    const bool usesResolution = m_conversionSource == ConversionSource::Pages;

    // Conversion cannot be started while the thumbnails are still being generated -
    // the user does not see, what is going to be converted, yet.
    const bool isBusy = m_conversionInProgress || m_thumbnailJob.isRunning;

    // While the conversion is running, no input can be changed - the worker is using
    // a snapshot of them and a modified input would not match the produced document.
    ui->thresholdLabel->setEnabled(usesThreshold && !m_conversionInProgress);
    ui->thresholdEditBox->setEnabled(usesThreshold && !m_conversionInProgress);
    ui->resolutionLabel->setEnabled(usesResolution && !m_conversionInProgress);
    ui->resolutionEditBox->setEnabled(usesResolution && !m_conversionInProgress);
    ui->conversionSourceLabel->setEnabled(!m_conversionInProgress);
    ui->conversionSourceComboBox->setEnabled(!m_conversionInProgress);
    ui->conversionMethodLabel->setEnabled(!m_conversionInProgress);
    ui->conversionMethodComboBox->setEnabled(!m_conversionInProgress);
    ui->imageListWidget->setEnabled(!m_conversionInProgress);

    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_conversionInProgress);
    m_createBitonalDocumentButton->setEnabled(!isBusy);
}

void PDFCreateBitonalDocumentDialog::updatePreview()
{
    // Preview of the previous settings is not interesting anymore. The job is cancelled
    // immediately, so it stops as soon as it can, but the new one is started by the
    // timer - a user dragging a spin box would otherwise start a job per each step.
    cancelJob(m_previewJob);
    m_previewUpdateTimer.stop();

    m_previewImageLeft = QImage();
    m_previewImageRight = QImage();
    m_leftPreviewWidget->setImage(QImage());
    m_rightPreviewWidget->setImage(QImage());

    const QModelIndex index = ui->imageListWidget->currentIndex();

    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_itemsToBeConverted.size()))
    {
        m_rightPreviewWidget->setCaption(tr("BITONAL"));
        m_leftPreviewWidget->setGenerating(false);
        m_rightPreviewWidget->setGenerating(false);
        return;
    }

    m_leftPreviewWidget->setGenerating(true);
    m_rightPreviewWidget->setGenerating(true);
    m_previewUpdateTimer.start();
}

void PDFCreateBitonalDocumentDialog::startPreviewGeneration()
{
    const QModelIndex index = ui->imageListWidget->currentIndex();

    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_itemsToBeConverted.size()))
    {
        m_leftPreviewWidget->setGenerating(false);
        m_rightPreviewWidget->setGenerating(false);
        return;
    }

    const int outputDpiResolution = ui->resolutionEditBox->value();

    PreviewRequest request;
    request.conversionSource = m_conversionSource;
    request.item = m_itemsToBeConverted.at(index.row());
    request.conversionMethod = getSelectedConversionMethod();
    request.manualThreshold = ui->thresholdEditBox->value();

    // Only the resolution needed by the panes is rasterized. The cache of the page is
    // keyed by this resolution as well, so a resized dialog rasterizes the page again.
    request.dpiResolution = getPreviewDpiResolution(request.item.pageIndex, outputDpiResolution);

    if (request.conversionSource == ConversionSource::Pages &&
        m_cachedPageIndex == request.item.pageIndex &&
        m_cachedPageDpiResolution == request.dpiResolution)
    {
        request.cachedPageImage = m_cachedPageImage;
    }

    // Page, which the started job is going to rasterize. It is remembered here, so the
    // rasterized page returned by the job can be stored into the cache - only one run
    // of the job is not obsolete, so a single value is enough.
    m_previewPageIndex = request.item.pageIndex;
    m_previewDpiResolution = request.dpiResolution;

    // The right pane does not always show a converted image, so its caption says,
    // what the selected mode is going to do with the item.
    auto getRightCaption = [](ConversionItemInfo::Mode mode) -> QString
    {
        switch (mode)
        {
            case ConversionItemInfo::Mode::Algorithm:
                return tr("BITONAL");

            case ConversionItemInfo::Mode::Original:
                return tr("UNCHANGED");

            case ConversionItemInfo::Mode::FillBlack:
                return tr("FILLED BLACK");

            case ConversionItemInfo::Mode::FillWhite:
                return tr("FILLED WHITE");

            default:
                Q_ASSERT(false);
                break;
        }

        return QString();
    };

    QString rightCaption = getRightCaption(request.item.mode);

    if (request.dpiResolution < outputDpiResolution)
    {
        // The page is rasterized at a lower resolution than the converted document, so
        // the thresholding can differ in the details. The user has to know about it.
        rightCaption = tr("%1 - PREVIEW AT %2 DPI").arg(rightCaption).arg(request.dpiResolution);
    }

    m_rightPreviewWidget->setCaption(rightCaption);
    m_leftPreviewWidget->setGenerating(true);
    m_rightPreviewWidget->setGenerating(true);
    m_isPreviewDelivered = false;

    startJob(m_previewJob, [this, request](int generation, const pdf::PDFOperationControl* operationControl)
    {
        generatePreview(generation, request, operationControl);
    });
}

pdf::PDFImageConversion::ConversionMethod PDFCreateBitonalDocumentDialog::getSelectedConversionMethod() const
{
    return static_cast<pdf::PDFImageConversion::ConversionMethod>(ui->conversionMethodComboBox->currentData().toInt());
}

PDFCreateBitonalDocumentDialog::ConversionSource PDFCreateBitonalDocumentDialog::getSelectedConversionSource() const
{
    return static_cast<ConversionSource>(ui->conversionSourceComboBox->currentData().toInt());
}

ImagePreviewDelegate::ImagePreviewDelegate(std::vector<PDFCreateBitonalDocumentDialog::ConversionItemInfo>* conversionItemInfos, QObject *parent) :
    QStyledItemDelegate(parent),
    m_conversionItemInfos(conversionItemInfos)
{

}

void ImagePreviewDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    if (!index.isValid() || index.row() >= int(m_conversionItemInfos->size()))
    {
        return;
    }

    const PDFCreateBitonalDocumentDialog::ConversionItemInfo& info = m_conversionItemInfos->at(index.row());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Marks are painted over the thumbnail, so they need a background of their own,
    // which keeps them readable even over a dark scan.
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 190));
    painter->drawRoundedRect(getMarksRect(option).adjusted(-2, -2, 2, 2), 4, 4);

    for (size_t modeIndex = 0; modeIndex < std::size(s_modes); ++modeIndex)
    {
        const Mode mode = s_modes[modeIndex];
        paintMark(painter, getMarkRect(option, modeIndex), mode, info.mode == mode, info.isModeAvailable(mode));
    }

    painter->restore();
}

void ImagePreviewDelegate::paintMark(QPainter* painter, QRect rect, Mode mode, bool isActive, bool isEnabled) const
{
    painter->save();

    if (isActive)
    {
        // The active mode is ringed, so the state of the item is readable at a glance.
        // A filled highlight is not used - it would fight with the colors of the mark.
        painter->setPen(QPen(QColor(0, 0, 128), 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0), 3.0, 3.0);
    }
    else
    {
        painter->setOpacity(isEnabled ? 0.45 : 0.15);
    }

    const QRect glyphRect = rect.adjusted(3, 3, -3, -3);
    const QPen outlinePen(QColor(64, 64, 64), 1.0);

    painter->setPen(Qt::NoPen);

    switch (mode)
    {
        case Mode::Algorithm:
            // A circle split into the black and the white half - the conversion turns
            // the colors of the item into exactly these two.
            painter->setBrush(Qt::black);
            painter->drawPie(glyphRect, 90 * 16, 180 * 16);
            painter->setBrush(Qt::white);
            painter->drawPie(glyphRect, -90 * 16, 180 * 16);
            break;

        case Mode::Original:
            // A circle divided into three colors - the item keeps its colors
            painter->setBrush(QColor(220, 50, 50));
            painter->drawPie(glyphRect, 90 * 16, 120 * 16);
            painter->setBrush(QColor(60, 160, 60));
            painter->drawPie(glyphRect, 210 * 16, 120 * 16);
            painter->setBrush(QColor(60, 90, 210));
            painter->drawPie(glyphRect, 330 * 16, 120 * 16);
            break;

        case Mode::FillBlack:
            painter->setBrush(Qt::black);
            painter->drawEllipse(glyphRect);
            break;

        case Mode::FillWhite:
            painter->setBrush(Qt::white);
            painter->drawEllipse(glyphRect);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    painter->setPen(outlinePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(glyphRect);

    painter->restore();
}

QString ImagePreviewDelegate::getModeToolTip(Mode mode) const
{
    switch (mode)
    {
        case Mode::Algorithm:
            return tr("Convert this item to the bitonal format using the selected conversion method.");

        case Mode::Original:
            return tr("Leave this item unchanged.");

        case Mode::FillBlack:
            return tr("Replace this item with a black area.");

        case Mode::FillWhite:
            return tr("Replace this item with a white area.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

bool ImagePreviewDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    Q_UNUSED(model);

    if (event->type() == QEvent::MouseButtonPress && index.isValid() && index.row() < int(m_conversionItemInfos->size()))
    {
        QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
        {
            PDFCreateBitonalDocumentDialog::ConversionItemInfo& info = m_conversionItemInfos->at(index.row());

            // Do we click on one of the mode marks?
            for (size_t modeIndex = 0; modeIndex < std::size(s_modes); ++modeIndex)
            {
                if (!QRectF(getMarkRect(option, modeIndex)).contains(mouseEvent->position()))
                {
                    continue;
                }

                const Mode mode = s_modes[modeIndex];

                if (info.mode != mode && info.isModeAvailable(mode))
                {
                    info.mode = mode;
                    Q_EMIT conversionModeChanged();
                }

                // The click is consumed even when nothing has changed, so clicking
                // a mark never modifies the selection of the list.
                return true;
            }
        }
    }

    return false;
}

bool ImagePreviewDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    Q_UNUSED(index);

    if (!event || !view)
    {
        return false;
    }

    if (event->type() == QEvent::ToolTip)
    {
        // Are we hovering over one of the mode marks?
        for (size_t modeIndex = 0; modeIndex < std::size(s_modes); ++modeIndex)
        {
            if (getMarkRect(option, modeIndex).contains(event->pos()))
            {
                event->accept();
                QToolTip::showText(event->globalPos(), getModeToolTip(s_modes[modeIndex]), view);
                return true;
            }
        }
    }

    return false;
}

QRect ImagePreviewDelegate::getMarkRect(const QStyleOptionViewItem& option, size_t modeIndex) const
{
    const QSize markSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, s_iconSize);
    const int spacing = qMax(2, markSize.width() / 8);
    const int left = option.rect.left() + int(modeIndex) * (markSize.width() + spacing);
    return QRect(left, option.rect.top(), markSize.width(), markSize.height());
}

QRect ImagePreviewDelegate::getMarksRect(const QStyleOptionViewItem& option) const
{
    return getMarkRect(option, 0).united(getMarkRect(option, std::size(s_modes) - 1));
}

}   // namespace pdfviewer
