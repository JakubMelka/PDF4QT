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

#include "pdfoptimizeimagesdialog.h"
#include "ui_pdfoptimizeimagesdialog.h"

#include "pdfwidgetutils.h"
#include "pdfutils.h"

#include <QBuffer>
#include <QImageWriter>
#include <QLabel>
#include <QListWidget>
#include <QtConcurrent/QtConcurrent>

#include "pdfdbgheap.h"

namespace pdfviewer
{
namespace
{
template<typename Enum>
static void addItem(QComboBox* combo, const QString& text, Enum value)
{
    combo->addItem(text, QVariant::fromValue(static_cast<int>(value)));
}

template<typename Enum>
static Enum currentEnum(const QComboBox* combo, Enum fallback)
{
    QVariant value = combo->currentData();
    if (!value.isValid())
    {
        return fallback;
    }
    return static_cast<Enum>(value.toInt());
}

QString formatBytes(int bytes)
{
    if (bytes <= 0)
    {
        return QObject::tr("0 B");
    }
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
    {
        return QObject::tr("%1 KB").arg(QString::number(kb, 'f', 1));
    }
    const double mb = kb / 1024.0;
    return QObject::tr("%1 MB").arg(QString::number(mb, 'f', 2));
}
} // namespace

PDFOptimizeImagesDialog::PDFOptimizeImagesDialog(const pdf::PDFDocument* document,
                                                 pdf::PDFProgress* progress,
                                                 QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFOptimizeImagesDialog),
    m_document(document),
    m_progress(progress),
    m_optimizationInProgress(false),
    m_optimized(false),
    m_updatingUi(false),
    m_optimizeButton(nullptr),
    m_settings(pdf::PDFImageOptimizer::Settings::createDefault())
{
    ui->setupUi(this);

    m_settings.enabled = true;

    addItem(ui->modeComboBox, tr("Auto"), 0);
    addItem(ui->modeComboBox, tr("Custom"), 1);

    addItem(ui->colorModeComboBox, tr("Auto"), pdf::PDFImageOptimizer::ColorMode::Auto);
    addItem(ui->colorModeComboBox, tr("Preserve"), pdf::PDFImageOptimizer::ColorMode::Preserve);
    addItem(ui->colorModeComboBox, tr("Color (RGB)"), pdf::PDFImageOptimizer::ColorMode::Color);
    addItem(ui->colorModeComboBox, tr("Grayscale"), pdf::PDFImageOptimizer::ColorMode::Grayscale);
    addItem(ui->colorModeComboBox, tr("Bitonal"), pdf::PDFImageOptimizer::ColorMode::Bitonal);

    addItem(ui->goalComboBox, tr("Prefer quality"), pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    addItem(ui->goalComboBox, tr("Minimum size"), pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize);

    auto addAlgItems = [](QComboBox* combo, bool bitonal)
    {
        addItem(combo, QObject::tr("Auto"), pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
        addItem(combo, QObject::tr("Flate"), pdf::PDFImageOptimizer::CompressionAlgorithm::Flate);
        if (!bitonal)
        {
            addItem(combo, QObject::tr("JPEG"), pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG);
            addItem(combo, QObject::tr("JPEG2000"), pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG2000);
        }
        addItem(combo, QObject::tr("RunLength"), pdf::PDFImageOptimizer::CompressionAlgorithm::RunLength);
        if (bitonal)
        {
            addItem(combo, QObject::tr("CCITT Group 4"), pdf::PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4);
            addItem(combo, QObject::tr("JBIG2"), pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2);
        }
    };

    addAlgItems(ui->colorAlgComboBox, false);
    addAlgItems(ui->grayAlgComboBox, false);
    addAlgItems(ui->bitonalAlgComboBox, true);

    auto addResampleItems = [](QComboBox* combo)
    {
        addItem(combo, QObject::tr("Nearest"), pdf::PDFImage::ResampleFilter::Nearest);
        addItem(combo, QObject::tr("Bilinear"), pdf::PDFImage::ResampleFilter::Bilinear);
        addItem(combo, QObject::tr("Bicubic"), pdf::PDFImage::ResampleFilter::Bicubic);
        addItem(combo, QObject::tr("Lanczos"), pdf::PDFImage::ResampleFilter::Lanczos);
    };

    addResampleItems(ui->colorResampleComboBox);
    addResampleItems(ui->grayResampleComboBox);
    addResampleItems(ui->bitonalResampleComboBox);

    ui->bitonalThresholdAutoCheckBox->setChecked(true);

    m_optimizeButton = ui->buttonBox->addButton(tr("Optimize"), QDialogButtonBox::ActionRole);
    connect(m_optimizeButton, &QPushButton::clicked, this, &PDFOptimizeImagesDialog::onOptimizeButtonClicked);

    connect(ui->imageListWidget, &QListWidget::currentRowChanged, this, &PDFOptimizeImagesDialog::onSelectionChanged);
    connect(ui->imageOverrideCheckBox, &QCheckBox::toggled, this, &PDFOptimizeImagesDialog::onOverrideToggled);
    connect(ui->imageEnabledCheckBox, &QCheckBox::toggled, this, &PDFOptimizeImagesDialog::onImageEnabledToggled);
    connect(ui->bitonalThresholdAutoCheckBox, &QCheckBox::toggled, this, &PDFOptimizeImagesDialog::onBitonalThresholdAutoToggled);

    auto connectValueChanged = [this](QObject* object)
    {
        if (auto combo = qobject_cast<QComboBox*>(object))
        {
            connect(combo, &QComboBox::currentIndexChanged, this, &PDFOptimizeImagesDialog::onSettingsChanged);
        }
        else if (auto spin = qobject_cast<QSpinBox*>(object))
        {
            connect(spin, &QSpinBox::valueChanged, this, &PDFOptimizeImagesDialog::onSettingsChanged);
        }
        else if (auto dspin = qobject_cast<QDoubleSpinBox*>(object))
        {
            connect(dspin, &QDoubleSpinBox::valueChanged, this, &PDFOptimizeImagesDialog::onSettingsChanged);
        }
        else if (auto check = qobject_cast<QCheckBox*>(object))
        {
            if (check == ui->imageEnabledCheckBox ||
                check == ui->imageOverrideCheckBox ||
                check == ui->bitonalThresholdAutoCheckBox)
            {
                return;
            }
            connect(check, &QCheckBox::toggled, this, &PDFOptimizeImagesDialog::onSettingsChanged);
        }
    };

    for (QObject* widget : findChildren<QObject*>())
    {
        connectValueChanged(widget);
    }

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1200, 800));
    pdf::PDFWidgetUtils::style(this);

    loadImages();
    updateUi();

    if (ui->imageListWidget->count() > 0)
    {
        ui->imageListWidget->setCurrentRow(0);
    }
    updatePreview();
}

PDFOptimizeImagesDialog::~PDFOptimizeImagesDialog()
{
    Q_ASSERT(!m_optimizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFOptimizeImagesDialog::loadImages()
{
    ui->imageListWidget->clear();
    m_images.clear();

    if (!m_document)
    {
        return;
    }

    m_images.reserve(64);
    std::vector<pdf::PDFImageOptimizer::ImageInfo> infos = pdf::PDFImageOptimizer::collectImageInfos(m_document);

    QSize iconSize(128, 128);
    ui->imageListWidget->setIconSize(iconSize);
    QSize scaledIconSize = iconSize * ui->imageListWidget->devicePixelRatioF();

    int index = 1;
    for (auto& info : infos)
    {
        QListWidgetItem* item = new QListWidgetItem(ui->imageListWidget);
        QImage iconImage = info.image;
        if (!iconImage.isNull())
        {
            iconImage = iconImage.scaled(scaledIconSize, Qt::KeepAspectRatio, Qt::FastTransformation);
            item->setIcon(QIcon(QPixmap::fromImage(iconImage)));
        }

        QStringList lines;
        lines << tr("Image %1").arg(index++);
        lines << tr("%1 x %2 px").arg(info.pixelSize.width()).arg(info.pixelSize.height());
        if (info.minimalDpi.x() > 0.0 || info.minimalDpi.y() > 0.0)
        {
            lines << tr("Min DPI: %1 x %2").arg(info.minimalDpi.x(), 0, 'f', 1).arg(info.minimalDpi.y(), 0, 'f', 1);
        }
        if (!info.colorSpaceName.isEmpty())
        {
            lines << tr("ColorSpace: %1").arg(info.colorSpaceName);
        }
        if (!info.filterName.isEmpty())
        {
            lines << tr("Filter: %1").arg(info.filterName);
        }
        if (info.originalBytes > 0)
        {
            lines << tr("Size: %1").arg(formatBytes(info.originalBytes));
        }

        item->setText(lines.join("\n"));

        ImageEntry entry;
        entry.info = std::move(info);
        entry.enabled = true;
        entry.overrideEnabled = false;
        entry.overrideSettings = m_settings;
        m_images.push_back(std::move(entry));
    }
}

void PDFOptimizeImagesDialog::updateUi()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_optimized && !m_optimizationInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_optimizationInProgress);
    if (m_optimizeButton)
    {
        m_optimizeButton->setEnabled(!m_optimizationInProgress && !m_images.empty());
    }
}

void PDFOptimizeImagesDialog::updateSelectedImageUi()
{
    pdf::PDFTemporaryValueChange guard(&m_updatingUi, true);

    ImageEntry* entry = getSelectedEntry();
    if (!entry)
    {
        ui->imageEnabledCheckBox->setChecked(false);
        ui->imageOverrideCheckBox->setChecked(false);
        ui->imageInfoLabel->clear();
        return;
    }

    ui->imageEnabledCheckBox->setChecked(entry->enabled);
    ui->imageOverrideCheckBox->setChecked(entry->overrideEnabled);

    const pdf::PDFImageOptimizer::ImageInfo& info = entry->info;
    QStringList details;
    details << tr("Reference: %1 %2").arg(info.reference.objectNumber).arg(info.reference.generation);
    if (info.hasTransparency || info.hasSoftMask)
    {
        details << tr("Transparency: %1").arg(info.hasTransparency ? tr("Yes") : tr("No"));
        if (info.hasSoftMask)
        {
            details << tr("Soft mask: Yes");
        }
    }
    if (info.bitsPerComponent > 0)
    {
        details << tr("BPC: %1").arg(info.bitsPerComponent);
    }
    ui->imageInfoLabel->setText(details.join("\n"));

    loadSettingsToUi(activeSettings());
}

void PDFOptimizeImagesDialog::loadSettingsToUi(const pdf::PDFImageOptimizer::Settings& settings)
{
    pdf::PDFTemporaryValueChange guard(&m_updatingUi, true);

    ui->modeComboBox->setCurrentIndex(settings.autoMode ? 0 : 1);
    ui->colorModeComboBox->setCurrentIndex(ui->colorModeComboBox->findData(static_cast<int>(settings.colorMode)));
    ui->goalComboBox->setCurrentIndex(ui->goalComboBox->findData(static_cast<int>(settings.goal)));
    ui->keepOriginalCheckBox->setChecked(settings.keepOriginalIfLarger);
    ui->preserveAlphaCheckBox->setChecked(settings.preserveTransparency);

    ui->colorAlgComboBox->setCurrentIndex(ui->colorAlgComboBox->findData(static_cast<int>(settings.colorProfile.algorithm)));
    ui->colorDpiSpinBox->setValue(settings.colorProfile.targetDpi);
    ui->colorResampleComboBox->setCurrentIndex(ui->colorResampleComboBox->findData(static_cast<int>(settings.colorProfile.resampleFilter)));
    ui->colorJpegQualitySpinBox->setValue(settings.colorProfile.jpegQuality);
    ui->colorJpxRateSpinBox->setValue(settings.colorProfile.jpeg2000Rate);
    ui->colorPredictorCheckBox->setChecked(settings.colorProfile.enablePngPredictor);

    ui->grayAlgComboBox->setCurrentIndex(ui->grayAlgComboBox->findData(static_cast<int>(settings.grayProfile.algorithm)));
    ui->grayDpiSpinBox->setValue(settings.grayProfile.targetDpi);
    ui->grayResampleComboBox->setCurrentIndex(ui->grayResampleComboBox->findData(static_cast<int>(settings.grayProfile.resampleFilter)));
    ui->grayJpegQualitySpinBox->setValue(settings.grayProfile.jpegQuality);
    ui->grayJpxRateSpinBox->setValue(settings.grayProfile.jpeg2000Rate);
    ui->grayPredictorCheckBox->setChecked(settings.grayProfile.enablePngPredictor);

    ui->bitonalAlgComboBox->setCurrentIndex(ui->bitonalAlgComboBox->findData(static_cast<int>(settings.bitonalProfile.algorithm)));
    ui->bitonalDpiSpinBox->setValue(settings.bitonalProfile.targetDpi);
    ui->bitonalResampleComboBox->setCurrentIndex(ui->bitonalResampleComboBox->findData(static_cast<int>(settings.bitonalProfile.resampleFilter)));
    if (settings.bitonalProfile.monochromeThreshold < 0)
    {
        ui->bitonalThresholdAutoCheckBox->setChecked(true);
        ui->bitonalThresholdSpinBox->setEnabled(false);
    }
    else
    {
        ui->bitonalThresholdAutoCheckBox->setChecked(false);
        ui->bitonalThresholdSpinBox->setEnabled(true);
        ui->bitonalThresholdSpinBox->setValue(settings.bitonalProfile.monochromeThreshold);
    }
    ui->bitonalPredictorCheckBox->setChecked(settings.bitonalProfile.enablePngPredictor);
}

void PDFOptimizeImagesDialog::applyUiToSettings(pdf::PDFImageOptimizer::Settings& settings)
{
    settings.autoMode = ui->modeComboBox->currentIndex() == 0;
    settings.colorMode = currentEnum(ui->colorModeComboBox, pdf::PDFImageOptimizer::ColorMode::Auto);
    settings.goal = currentEnum(ui->goalComboBox, pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    settings.keepOriginalIfLarger = ui->keepOriginalCheckBox->isChecked();
    settings.preserveTransparency = ui->preserveAlphaCheckBox->isChecked();

    settings.colorProfile.algorithm = currentEnum(ui->colorAlgComboBox, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    settings.colorProfile.targetDpi = ui->colorDpiSpinBox->value();
    settings.colorProfile.resampleFilter = currentEnum(ui->colorResampleComboBox, pdf::PDFImage::ResampleFilter::Bicubic);
    settings.colorProfile.jpegQuality = ui->colorJpegQualitySpinBox->value();
    settings.colorProfile.jpeg2000Rate = static_cast<float>(ui->colorJpxRateSpinBox->value());
    settings.colorProfile.enablePngPredictor = ui->colorPredictorCheckBox->isChecked();

    settings.grayProfile.algorithm = currentEnum(ui->grayAlgComboBox, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    settings.grayProfile.targetDpi = ui->grayDpiSpinBox->value();
    settings.grayProfile.resampleFilter = currentEnum(ui->grayResampleComboBox, pdf::PDFImage::ResampleFilter::Bicubic);
    settings.grayProfile.jpegQuality = ui->grayJpegQualitySpinBox->value();
    settings.grayProfile.jpeg2000Rate = static_cast<float>(ui->grayJpxRateSpinBox->value());
    settings.grayProfile.enablePngPredictor = ui->grayPredictorCheckBox->isChecked();

    settings.bitonalProfile.algorithm = currentEnum(ui->bitonalAlgComboBox, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    settings.bitonalProfile.targetDpi = ui->bitonalDpiSpinBox->value();
    settings.bitonalProfile.resampleFilter = currentEnum(ui->bitonalResampleComboBox, pdf::PDFImage::ResampleFilter::Bicubic);
    settings.bitonalProfile.enablePngPredictor = ui->bitonalPredictorCheckBox->isChecked();

    if (ui->bitonalThresholdAutoCheckBox->isChecked())
    {
        settings.bitonalProfile.monochromeThreshold = -1;
    }
    else
    {
        settings.bitonalProfile.monochromeThreshold = ui->bitonalThresholdSpinBox->value();
    }
}

pdf::PDFImageOptimizer::Settings& PDFOptimizeImagesDialog::activeSettings()
{
    ImageEntry* entry = getSelectedEntry();
    if (entry && entry->overrideEnabled)
    {
        return entry->overrideSettings;
    }
    return m_settings;
}

PDFOptimizeImagesDialog::ImageEntry* PDFOptimizeImagesDialog::getSelectedEntry()
{
    int row = ui->imageListWidget->currentRow();
    if (row < 0 || row >= static_cast<int>(m_images.size()))
    {
        return nullptr;
    }
    return &m_images[static_cast<size_t>(row)];
}

const PDFOptimizeImagesDialog::ImageEntry* PDFOptimizeImagesDialog::getSelectedEntry() const
{
    int row = ui->imageListWidget->currentRow();
    if (row < 0 || row >= static_cast<int>(m_images.size()))
    {
        return nullptr;
    }
    return &m_images[static_cast<size_t>(row)];
}

void PDFOptimizeImagesDialog::onOptimizeButtonClicked()
{
    Q_ASSERT(!m_optimizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    applyUiToSettings(activeSettings());

    m_optimizationInProgress = true;

    pdf::PDFImageOptimizer::ImageOverrides overrides;
    overrides.clear();
    for (const ImageEntry& entry : m_images)
    {
        if (!entry.enabled)
        {
            pdf::PDFImageOptimizer::ImageOverride overrideInfo;
            overrideInfo.enabled = false;
            overrides.emplace(entry.info.reference, overrideInfo);
            continue;
        }

        if (entry.overrideEnabled)
        {
            pdf::PDFImageOptimizer::ImageOverride overrideInfo;
            overrideInfo.enabled = true;
            overrideInfo.useCustomSettings = true;
            overrideInfo.settings = entry.overrideSettings;
            overrides.emplace(entry.info.reference, overrideInfo);
        }
    }

    m_future = QtConcurrent::run([this, overrides]()
    {
        pdf::PDFImageOptimizer optimizer;
        m_optimizedDocument = optimizer.optimize(m_document, m_settings, overrides, m_progress);
    });

    m_futureWatcher.emplace();
    connect(&m_futureWatcher.value(), &QFutureWatcher<void>::finished, this, &PDFOptimizeImagesDialog::onOptimizationFinished);
    m_futureWatcher->setFuture(m_future);

    updateUi();
}

void PDFOptimizeImagesDialog::onOptimizationFinished()
{
    m_future.waitForFinished();
    m_optimizationInProgress = false;
    m_optimized = true;
    updateUi();
}

void PDFOptimizeImagesDialog::onSettingsChanged()
{
    if (m_updatingUi)
    {
        return;
    }

    applyUiToSettings(activeSettings());
    updatePreview();
}

void PDFOptimizeImagesDialog::onSelectionChanged()
{
    updateSelectedImageUi();
    updatePreview();
}

void PDFOptimizeImagesDialog::onOverrideToggled(bool checked)
{
    if (m_updatingUi)
    {
        return;
    }

    ImageEntry* entry = getSelectedEntry();
    if (!entry)
    {
        return;
    }

    entry->overrideEnabled = checked;
    if (checked)
    {
        entry->overrideSettings = m_settings;
    }

    updateSelectedImageUi();
    updatePreview();
}

void PDFOptimizeImagesDialog::onImageEnabledToggled(bool checked)
{
    if (m_updatingUi)
    {
        return;
    }

    ImageEntry* entry = getSelectedEntry();
    if (!entry)
    {
        return;
    }

    entry->enabled = checked;
    updatePreview();
}

void PDFOptimizeImagesDialog::onBitonalThresholdAutoToggled(bool checked)
{
    if (m_updatingUi)
    {
        return;
    }

    ui->bitonalThresholdSpinBox->setEnabled(!checked);
    onSettingsChanged();
}

void PDFOptimizeImagesDialog::updatePreview()
{
    const ImageEntry* entry = getSelectedEntry();
    if (!entry)
    {
        ui->previewBeforeImageLabel->clear();
        ui->previewAfterImageLabel->clear();
        ui->previewInfoLabel->clear();
        return;
    }

    const pdf::PDFImageOptimizer::Settings& settings = entry->overrideEnabled ? entry->overrideSettings : m_settings;
    pdf::PDFImageOptimizer::ResolvedPlan plan = pdf::PDFImageOptimizer::resolvePlan(entry->info, settings);

    QImage before = entry->info.image;
    QImage after = pdf::PDFImageOptimizer::createPreviewImage(entry->info, plan, true);

    const QSize previewSize(200, 200);
    if (!before.isNull())
    {
        QPixmap pix = QPixmap::fromImage(before.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ui->previewBeforeImageLabel->setPixmap(pix);
    }
    if (!after.isNull())
    {
        QPixmap pix = QPixmap::fromImage(after.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ui->previewAfterImageLabel->setPixmap(pix);
    }

    int newBytes = pdf::PDFImageOptimizer::estimateEncodedBytes(entry->info, plan, nullptr);
    QString infoText = tr("Estimated size: %1 -> %2").arg(formatBytes(entry->info.originalBytes),
                                                          formatBytes(newBytes));
    if (settings.keepOriginalIfLarger && entry->info.originalBytes > 0 && newBytes >= entry->info.originalBytes)
    {
        infoText += tr(" (will keep original)");
    }
    ui->previewInfoLabel->setText(infoText);
}

}   // namespace pdfviewer
