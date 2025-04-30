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

#include "outputpreviewdialog.h"
#include "ui_outputpreviewdialog.h"

#include "pdfcms.h"
#include "pdfrenderer.h"
#include "pdfwidgetutils.h"
#include "pdfdrawspacecontroller.h"

#include <QCloseEvent>
#include <QColorDialog>
#include <QtConcurrent/QtConcurrent>

namespace pdfplugin
{

OutputPreviewDialog::OutputPreviewDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent) :
    QDialog(parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint),
    ui(new Ui::OutputPreviewDialog),
    m_inkMapper(widget->getCMSManager(), document),
    m_inkMapperForRendering(widget->getCMSManager(), document),
    m_document(document),
    m_widget(widget),
    m_needUpdateImage(false),
    m_outputPreviewWidget(new OutputPreviewWidget(this)),
    m_futureWatcher(nullptr)
{
    ui->setupUi(this);

    ui->pageIndexScrollBar->setMinimum(1);
    ui->pageIndexScrollBar->setValue(1);
    ui->pageIndexScrollBar->setMaximum(int(document->getCatalog()->getPageCount()));

    ui->frameViewLayout->insertWidget(0, m_outputPreviewWidget);

    ui->displayModeComboBox->addItem(tr("Separations"), OutputPreviewWidget::Separations);
    ui->displayModeComboBox->addItem(tr("Color Warnings | Ink Coverage"), OutputPreviewWidget::ColorWarningInkCoverage);
    ui->displayModeComboBox->addItem(tr("Color Warnings | Rich Black"), OutputPreviewWidget::ColorWarningRichBlack);
    ui->displayModeComboBox->addItem(tr("Ink Coverage"), OutputPreviewWidget::InkCoverage);
    ui->displayModeComboBox->addItem(tr("Shape Channel"), OutputPreviewWidget::ShapeChannel);
    ui->displayModeComboBox->addItem(tr("Opacity Channel"), OutputPreviewWidget::OpacityChannel);
    ui->displayModeComboBox->setCurrentIndex(0);

    m_outputPreviewWidget->setInkMapper(&m_inkMapper);
    ui->inksTreeWidget->setMinimumHeight(pdf::PDFWidgetUtils::scaleDPI_y(ui->inksTreeWidget, 150));

    m_inkMapper.createSpotColors(ui->simulateSeparationsCheckBox->isChecked());
    connect(ui->simulateSeparationsCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::onSimulateSeparationsChecked);
    connect(ui->simulatePaperColorCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::onSimulatePaperColorChecked);
    connect(ui->redPaperColorEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &OutputPreviewDialog::onPaperColorChanged);
    connect(ui->greenPaperColorEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &OutputPreviewDialog::onPaperColorChanged);
    connect(ui->bluePaperColorEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &OutputPreviewDialog::onPaperColorChanged);
    connect(ui->pageIndexScrollBar, &QScrollBar::valueChanged, this, &OutputPreviewDialog::updatePageImage);
    connect(ui->displayImagesCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::updatePageImage);
    connect(ui->displayShadingCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::updatePageImage);
    connect(ui->displayTextCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::updatePageImage);
    connect(ui->displayTilingPatternsCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::updatePageImage);
    connect(ui->displayVectorGraphicsCheckBox, &QCheckBox::clicked, this, &OutputPreviewDialog::updatePageImage);
    connect(ui->inksTreeWidget->model(), &QAbstractItemModel::dataChanged, this, &OutputPreviewDialog::onInksChanged);
    connect(ui->alarmColorButton, &QPushButton::clicked, this, &OutputPreviewDialog::onAlarmColorButtonClicked);
    connect(ui->displayModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OutputPreviewDialog::onDisplayModeChanged);
    connect(ui->inkCoverageLimitEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &OutputPreviewDialog::onInkCoverageLimitChanged);
    connect(ui->richBlackLimitEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &OutputPreviewDialog::onRichBlackLimtiChanged);

    updatePageImage();
    updateInks();
    updatePaperColorWidgets();
    updateAlarmColorButtonIcon();
    onDisplayModeChanged();
    onInkCoverageLimitChanged(ui->inkCoverageLimitEdit->value());
    onRichBlackLimtiChanged(ui->richBlackLimitEdit->value());
    pdf::PDFWidgetUtils::style(this);
}

OutputPreviewDialog::~OutputPreviewDialog()
{
    delete ui;
}

void OutputPreviewDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updatePageImage();
}

void OutputPreviewDialog::closeEvent(QCloseEvent* event)
{
    if (!isRenderingDone())
    {
        event->ignore();
    }
}

void OutputPreviewDialog::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);

    updatePageImage();
}

void OutputPreviewDialog::updateInks()
{
    ui->inksTreeWidget->setUpdatesEnabled(false);
    ui->inksTreeWidget->clear();

    QTreeWidgetItem* processColorsRoot = new QTreeWidgetItem(ui->inksTreeWidget, QStringList(tr("Process Inks")));
    QTreeWidgetItem* spotColorsRoot = new QTreeWidgetItem(ui->inksTreeWidget, QStringList(tr("Spot Inks")));

    processColorsRoot->setFlags(processColorsRoot->flags() | Qt::ItemIsUserCheckable);
    processColorsRoot->setCheckState(0, Qt::Checked);

    spotColorsRoot->setFlags(spotColorsRoot->flags() | Qt::ItemIsUserCheckable);
    spotColorsRoot->setCheckState(0, Qt::Checked);

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(ui->inksTreeWidget, QSize(16, 16));
    ui->inksTreeWidget->setIconSize(iconSize);
    ui->inksTreeWidget->setRootIsDecorated(true);

    int colorIndex = 0;
    std::vector<pdf::PDFInkMapper::ColorInfo> separations = m_inkMapper.getSeparations(4);
    for (const auto& colorInfo : separations)
    {
        QTreeWidgetItem* item = nullptr;
        if (!colorInfo.isSpot)
        {
            // Process color (ink)
            item = new QTreeWidgetItem(processColorsRoot, QStringList(colorInfo.textName));
        }
        else
        {
            // Spot color (ink)
            item = new QTreeWidgetItem(spotColorsRoot, QStringList(colorInfo.textName));
        }

        if (colorInfo.color.isValid())
        {
            QPixmap icon(iconSize);
            icon.fill(colorInfo.color);
            item->setIcon(0, QIcon(icon));
        }

        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, colorIndex++);
    }

    if (processColorsRoot->childCount() == 0)
    {
        delete processColorsRoot;
    }

    if (spotColorsRoot->childCount() == 0)
    {
        delete spotColorsRoot;
    }

    ui->inksTreeWidget->expandAll();
    ui->inksTreeWidget->setUpdatesEnabled(true);
}

void OutputPreviewDialog::updatePaperColorWidgets()
{
    const bool isPaperColorEnabled = ui->simulatePaperColorCheckBox->isChecked();

    ui->redPaperColorEdit->setEnabled(isPaperColorEnabled);
    ui->greenPaperColorEdit->setEnabled(isPaperColorEnabled);
    ui->bluePaperColorEdit->setEnabled(isPaperColorEnabled);

    if (!isPaperColorEnabled)
    {
        ui->redPaperColorEdit->setValue(1.0);
        ui->greenPaperColorEdit->setValue(1.0);
        ui->bluePaperColorEdit->setValue(1.0);
    }
}

void OutputPreviewDialog::updateAlarmColorButtonIcon()
{
    QSize iconSize = ui->alarmColorButton->iconSize();
    QPixmap pixmap(iconSize);
    pixmap.fill(m_outputPreviewWidget->getAlarmColor());
    ui->alarmColorButton->setIcon(QIcon(pixmap));
}

void OutputPreviewDialog::onPaperColorChanged()
{
    const bool isPaperColorEnabled = ui->simulatePaperColorCheckBox->isChecked();
    if (isPaperColorEnabled)
    {
        updatePageImage();
    }
}

void OutputPreviewDialog::onAlarmColorButtonClicked()
{
    QColorDialog colorDialog(m_outputPreviewWidget->getAlarmColor(), this);
    if (colorDialog.exec() == QColorDialog::Accepted)
    {
        m_outputPreviewWidget->setAlarmColor(colorDialog.currentColor());
        updateAlarmColorButtonIcon();
    }
}

void OutputPreviewDialog::onSimulateSeparationsChecked(bool checked)
{
    m_inkMapper.setSpotColorsActive(checked);
    updateInks();
    updatePageImage();
}

void OutputPreviewDialog::onSimulatePaperColorChecked(bool checked)
{
    Q_UNUSED(checked);

    updatePaperColorWidgets();
    updatePageImage();
}

void OutputPreviewDialog::onDisplayModeChanged()
{
    m_outputPreviewWidget->setDisplayMode(OutputPreviewWidget::DisplayMode(ui->displayModeComboBox->currentData().toInt()));
}

void OutputPreviewDialog::onInksChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles)
{
    Q_UNUSED(topLeft);
    Q_UNUSED(bottomRight);

    if (roles.contains(Qt::CheckStateRole))
    {
        updatePageImage();
    }
}

void OutputPreviewDialog::onInkCoverageLimitChanged(double value)
{
    m_outputPreviewWidget->setInkCoverageLimit(value / 100.0);
}

void OutputPreviewDialog::onRichBlackLimtiChanged(double value)
{
    m_outputPreviewWidget->setRichBlackLimit(value / 100.0);
}

void OutputPreviewDialog::updatePageImage()
{
    if (!isRenderingDone())
    {
        m_needUpdateImage = true;
        return;
    }

    m_needUpdateImage = false;

    const pdf::PDFPage* page = m_document->getCatalog()->getPage(ui->pageIndexScrollBar->value() - 1);
    if (!page)
    {
        m_outputPreviewWidget->clear();
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    pdf::PDFRGB paperColor = pdf::PDFRGB{ 1.0f, 1.0f, 1.0f };

    // Active color mask
    uint32_t activeColorMask = pdf::PDFPixelFormat::getAllColorsMask();

    const int itemCount = ui->inksTreeWidget->topLevelItemCount();
    for (int i = 0; i < itemCount; ++i)
    {
        QTreeWidgetItem* rootItem = ui->inksTreeWidget->topLevelItem(i);
        const bool isRootItemChecked = rootItem->data(0, Qt::CheckStateRole).toInt() == Qt::Checked;

        const int childCount = rootItem->childCount();
        for (int j = 0; j < childCount; ++j)
        {
            QTreeWidgetItem* childItem = rootItem->child(j);
            const bool isChecked = childItem->data(0, Qt::CheckStateRole).toInt() == Qt::Checked;
            const bool isEnabled = isRootItemChecked && isChecked;

            if (!isEnabled)
            {
                uint32_t colorIndex = childItem->data(0, Qt::UserRole).toInt();
                uint32_t colorFlags = 1 << colorIndex;
                activeColorMask = activeColorMask & ~colorFlags;
            }
        }
    }

    // Paper color
    if (ui->simulatePaperColorCheckBox)
    {
        paperColor[0] = ui->redPaperColorEdit->value();
        paperColor[1] = ui->greenPaperColorEdit->value();
        paperColor[2] = ui->bluePaperColorEdit->value();
    }

    pdf::PDFTransparencyRendererSettings::Flags flags = pdf::PDFTransparencyRendererSettings::None;
    flags.setFlag(pdf::PDFTransparencyRendererSettings::DisplayImages, ui->displayImagesCheckBox->isChecked());
    flags.setFlag(pdf::PDFTransparencyRendererSettings::DisplayText, ui->displayTextCheckBox->isChecked());
    flags.setFlag(pdf::PDFTransparencyRendererSettings::DisplayVectorGraphics, ui->displayVectorGraphicsCheckBox->isChecked());
    flags.setFlag(pdf::PDFTransparencyRendererSettings::DisplayShadings, ui->displayShadingCheckBox->isChecked());
    flags.setFlag(pdf::PDFTransparencyRendererSettings::DisplayTilingPatterns, ui->displayTilingPatternsCheckBox->isChecked());
    flags.setFlag(pdf::PDFTransparencyRendererSettings::SaveOriginalProcessImage, true);

    m_inkMapperForRendering = m_inkMapper;
    QSize renderSize = m_outputPreviewWidget->getPageImageSizeHint();
    auto renderImage = [this, page, renderSize, paperColor, activeColorMask, flags]() -> RenderedImage
    {
        return renderPage(page, renderSize, paperColor, activeColorMask, flags);
    };

    m_future = QtConcurrent::run(renderImage);
    m_futureWatcher = new QFutureWatcher<RenderedImage>();
    connect(m_futureWatcher, &QFutureWatcher<RenderedImage>::finished, this, &OutputPreviewDialog::onPageImageRendered);
    m_futureWatcher->setFuture(m_future);
}

OutputPreviewDialog::RenderedImage OutputPreviewDialog::renderPage(const pdf::PDFPage* page,
                                                                   QSize renderSize,
                                                                   pdf::PDFRGB paperColor,
                                                                   uint32_t activeColorMask,
                                                                   pdf::PDFTransparencyRendererSettings::Flags additionalFlags)
{
    RenderedImage result;

    QRectF pageRect = page->getRotatedMediaBox();
    QSizeF pageSize = pageRect.size();
    pageSize.scale(renderSize.width(), renderSize.height(), Qt::KeepAspectRatio);
    QSize imageSize = pageSize.toSize();

    if (!imageSize.isValid())
    {
        return result;
    }

    pdf::PDFTransparencyRendererSettings settings;
    settings.flags = additionalFlags;

    // Jakub Melka: debug is very slow, use multithreading
#ifdef QT_DEBUG
    settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::MultithreadedPathSampler, true);
#endif

    settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::ActiveColorMask, activeColorMask != pdf::PDFPixelFormat::getAllColorsMask());
    settings.flags.setFlag(pdf::PDFTransparencyRendererSettings::SeparationSimulation, m_inkMapperForRendering.getActiveSpotColorCount() > 0);
    settings.activeColorMask = activeColorMask;

    QTransform pagePointToDevicePoint = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, QRect(QPoint(0, 0), imageSize));
    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
    pdf::PDFTransparencyRenderer renderer(page, m_document, proxy->getFontCache(), cms.data(), proxy->getOptionalContentActivity(),
                                          &m_inkMapperForRendering, settings, pagePointToDevicePoint);

    renderer.beginPaint(imageSize);
    result.errors = renderer.processContents();
    renderer.endPaint();

    QImage image = renderer.toImage(false, true, paperColor);

    result.image = qMove(image);
    result.originalProcessImage = renderer.getOriginalProcessBitmap();
    result.pageSize = page->getRotatedMediaBoxMM().size();
    return result;
}

void OutputPreviewDialog::onPageImageRendered()
{
    QApplication::restoreOverrideCursor();

    if (m_future.isFinished())
    {
        RenderedImage result = m_future.result();
        m_future = QFuture<RenderedImage>();
        m_futureWatcher->deleteLater();
        m_futureWatcher = nullptr;

        m_outputPreviewWidget->setPageImage(qMove(result.image), qMove(result.originalProcessImage), result.pageSize);

        if (m_needUpdateImage)
        {
            updatePageImage();
        }
    }
}

bool OutputPreviewDialog::isRenderingDone() const
{
    return !(m_futureWatcher && m_futureWatcher->isRunning());
}

void OutputPreviewDialog::accept()
{
    if (!isRenderingDone())
    {
        return;
    }

    QDialog::accept();
}

void OutputPreviewDialog::reject()
{
    if (!isRenderingDone())
    {
        return;
    }

    QDialog::reject();
}

} // namespace pdfplugin
