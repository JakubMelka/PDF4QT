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

#include <QBrush>
#include <QDataStream>
#include <QIODevice>
#include <QLabel>
#include <QListWidget>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>

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

template<typename Enum>
static void setCurrentEnum(QComboBox* combo, Enum value, Enum fallback)
{
    int index = combo->findData(static_cast<int>(value));
    if (index < 0)
    {
        index = combo->findData(static_cast<int>(fallback));
    }
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

QString formatBytes(qint64 bytes)
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

QByteArray createEstimateCacheKey(const pdf::PDFImageOptimizer::Settings& settings,
                                  const pdf::PDFImageOptimizer::ResolvedPlan& plan)
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);

    const auto writeProfile = [&stream](const pdf::PDFImageOptimizer::CompressionProfile& profile)
    {
        stream << static_cast<qint32>(profile.algorithm);
        stream << static_cast<qint32>(profile.targetDpi);
        stream << static_cast<qint32>(profile.resampleFilter);
        stream << static_cast<qint32>(profile.jpegQuality);
        stream << profile.jpeg2000Rate;
        stream << static_cast<qint32>(profile.monochromeThreshold);
        stream << profile.enablePngPredictor;
    };

    stream << settings.enabled;
    stream << settings.autoMode;
    stream << static_cast<qint32>(settings.colorMode);
    stream << static_cast<qint32>(settings.goal);
    stream << settings.keepOriginalIfLarger;
    stream << settings.preserveTransparency;
    writeProfile(settings.colorProfile);
    writeProfile(settings.grayProfile);
    writeProfile(settings.bitonalProfile);
    stream << static_cast<qint32>(plan.algorithm);
    stream << static_cast<qint32>(plan.resolvedColorMode);
    stream << plan.targetSize;
    stream << plan.useSoftMask;
    stream << plan.hadUnsupportedCompression;

    return key;
}

QPointF getEffectiveMinimalDpi(const pdf::PDFImageOptimizer::ImageInfo& info,
                               const pdf::PDFImageOptimizer::ResolvedPlan& plan)
{
    QPointF effectiveDpi = info.minimalDpi;
    if (!plan.targetSize.isValid() || plan.targetSize.isEmpty() || info.image.isNull())
    {
        return effectiveDpi;
    }

    const QSize sourceSize = info.image.size();
    if (sourceSize.width() > 0 && std::isfinite(effectiveDpi.x()) && effectiveDpi.x() > 0.0)
    {
        effectiveDpi.setX(effectiveDpi.x() * static_cast<double>(plan.targetSize.width()) / static_cast<double>(sourceSize.width()));
    }
    if (sourceSize.height() > 0 && std::isfinite(effectiveDpi.y()) && effectiveDpi.y() > 0.0)
    {
        effectiveDpi.setY(effectiveDpi.y() * static_cast<double>(plan.targetSize.height()) / static_cast<double>(sourceSize.height()));
    }

    return effectiveDpi;
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
    m_previewUiReady(false),
    m_previewRow(-1),
    m_optimizeButton(nullptr),
    m_settings(pdf::PDFImageOptimizer::Settings::createDefault())
{
    ui->setupUi(this);
    m_previewUiReady = true;
    ui->gridLayout->setColumnStretch(0, 0);
    ui->gridLayout->setColumnStretch(1, 0);
    ui->gridLayout->setColumnStretch(2, 1);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setSizeGripEnabled(true);

    m_settings.enabled = true;
    updateSettingsEditorContextUi();

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
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto setTip = [](QWidget* widget, const QString& text)
    {
        if (widget)
        {
            widget->setToolTip(text);
        }
    };

    setTip(ui->modeComboBox, tr("<b>Mode</b><br/>Auto uses image analysis to pick color mode and compression.<br/>Custom respects the selected color mode and profiles."));
    setTip(ui->colorModeComboBox, tr("<b>Color mode</b><br/>Choose output color space or let the optimizer decide."));
    setTip(ui->goalComboBox, tr("<b>Goal</b><br/><b>Prefer quality</b> keeps more detail.<br/><b>Minimum size</b> prefers smaller output."));
    setTip(ui->keepOriginalCheckBox, tr("<b>Keep original if larger</b><br/>Leaves the original image if re-encoding does not shrink it."));
    setTip(ui->preserveAlphaCheckBox, tr("<b>Preserve transparency</b><br/>Stores alpha as a soft mask when possible."));

    setTip(ui->colorAlgComboBox, tr("<b>Algorithm</b><br/>Compression for color images (Auto picks based on content)."));
    setTip(ui->grayAlgComboBox, tr("<b>Algorithm</b><br/>Compression for grayscale images (Auto picks based on content)."));
    setTip(ui->bitonalAlgComboBox, tr("<b>Algorithm</b><br/>Compression for bitonal images (Auto picks JBIG2, which is lossless and usually the smallest)."));

    setTip(ui->colorDpiSpinBox, tr("<b>Target DPI</b><br/>Downsample color images to this DPI (0 keeps original)."));
    setTip(ui->grayDpiSpinBox, tr("<b>Target DPI</b><br/>Downsample grayscale images to this DPI (0 keeps original)."));
    setTip(ui->bitonalDpiSpinBox, tr("<b>Target DPI</b><br/>Downsample bitonal images to this DPI (0 keeps original)."));

    setTip(ui->colorResampleComboBox, tr("<b>Resample</b><br/>Scaling filter used when resizing images."));
    setTip(ui->grayResampleComboBox, tr("<b>Resample</b><br/>Scaling filter used when resizing images."));
    setTip(ui->bitonalResampleComboBox, tr("<b>Resample</b><br/>Scaling filter used when resizing images."));

    setTip(ui->colorJpegQualitySpinBox, tr("<b>JPEG quality</b><br/>Higher values preserve detail but increase size."));
    setTip(ui->grayJpegQualitySpinBox, tr("<b>JPEG quality</b><br/>Higher values preserve detail but increase size."));

    setTip(ui->colorJpxRateSpinBox, tr("<b>JPEG2000 rate</b><br/>0 = lossless, higher values increase compression."));
    setTip(ui->grayJpxRateSpinBox, tr("<b>JPEG2000 rate</b><br/>0 = lossless, higher values increase compression."));

    setTip(ui->colorPredictorCheckBox, tr("<b>PNG predictor</b><br/>Improves Flate compression for continuous-tone images."));
    setTip(ui->grayPredictorCheckBox, tr("<b>PNG predictor</b><br/>Improves Flate compression for continuous-tone images."));
    setTip(ui->bitonalPredictorCheckBox, tr("<b>PNG predictor</b><br/>Improves Flate compression for 1-bit images."));

    setTip(ui->bitonalThresholdSpinBox, tr("<b>Threshold</b><br/>Manual threshold for bitonal conversion (0-255)."));
    setTip(ui->bitonalThresholdAutoCheckBox, tr("<b>Auto threshold</b><br/>Let the optimizer pick an automatic threshold."));

    setTip(ui->imageEnabledCheckBox, tr("<b>Enable compression</b><br/>Exclude the selected images from optimization when unchecked."));
    setTip(ui->imageOverrideCheckBox, tr("<b>Override settings</b><br/>Use custom settings for the selected images."));
    setTip(ui->imageListWidget, tr("<b>Images</b><br/>Hold Ctrl or Shift while clicking to select multiple images, or press Ctrl+A to select all of them. "
                                   "The settings editor and the check boxes below then apply to the whole selection."));
    setTip(m_optimizeButton, tr("<b>Optimize</b><br/>Run image optimization with the current settings."));

    // Both signals are needed: Ctrl+click on an already selected item changes
    // the selection without changing the current row, and Ctrl+arrow moves the
    // current row without changing the selection.
    connect(ui->imageListWidget, &QListWidget::currentRowChanged, this, &PDFOptimizeImagesDialog::onSelectionChanged);
    connect(ui->imageListWidget, &QListWidget::itemSelectionChanged, this, &PDFOptimizeImagesDialog::onSelectionChanged);

    // Use clicked() instead of toggled() - it is emitted only for user
    // interaction, so programmatic tri-state updates do not re-enter the slots.
    connect(ui->imageOverrideCheckBox, &QCheckBox::clicked, this, &PDFOptimizeImagesDialog::onOverrideClicked);
    connect(ui->imageEnabledCheckBox, &QCheckBox::clicked, this, &PDFOptimizeImagesDialog::onImageEnabledClicked);
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

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1400, 800));
    pdf::PDFWidgetUtils::style(this);

    loadImages();
    updateSummaryInfo();
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

void PDFOptimizeImagesDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_previewUiReady)
    {
        updatePreview();
    }
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

        ImageEntry entry;
        entry.info = std::move(info);
        entry.enabled = true;
        entry.overrideEnabled = false;
        entry.overrideSettings = m_settings;
        m_images.push_back(std::move(entry));
        updateImageListItem(index - 1);
        ++index;
    }
}

void PDFOptimizeImagesDialog::updateImageListItem(int row)
{
    if (row < 0 || row >= static_cast<int>(m_images.size()))
    {
        return;
    }

    QListWidgetItem* item = ui->imageListWidget->item(row);
    if (!item)
    {
        return;
    }

    ImageEntry& entry = m_images[static_cast<size_t>(row)];
    const pdf::PDFImageOptimizer::ImageInfo& info = entry.info;
    QPointF effectiveMinimalDpi = info.minimalDpi;

    QStringList lines;
    lines << tr("Image %1").arg(row + 1);
    lines << tr("%1 x %2 px").arg(info.pixelSize.width()).arg(info.pixelSize.height());

    int newBytes = 0;
    bool willKeepOriginal = false;

    QStringList status;
    if (!entry.enabled)
    {
        status << tr("disabled");
        willKeepOriginal = true;
    }
    if (entry.overrideEnabled)
    {
        status << tr("override");
    }

    if (entry.enabled)
    {
        const pdf::PDFImageOptimizer::Settings& settings = entry.overrideEnabled ? entry.overrideSettings : m_settings;
        const pdf::PDFImageOptimizer::ResolvedPlan plan = pdf::PDFImageOptimizer::resolvePlan(info, settings);
        effectiveMinimalDpi = getEffectiveMinimalDpi(info, plan);
        newBytes = getEstimatedBytes(entry, settings, plan);
        willKeepOriginal = info.isImageMask || (settings.keepOriginalIfLarger && info.originalBytes > 0 && newBytes >= info.originalBytes);
        if (willKeepOriginal)
        {
            status << tr("will keep original");
        }
    }

    if (!status.isEmpty())
    {
        lines << tr("Status: %1").arg(status.join(", "));
    }

    if (effectiveMinimalDpi.x() > 0.0 || effectiveMinimalDpi.y() > 0.0)
    {
        lines << tr("Min DPI: %1 x %2").arg(effectiveMinimalDpi.x(), 0, 'f', 1).arg(effectiveMinimalDpi.y(), 0, 'f', 1);
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
        if (willKeepOriginal || newBytes <= 0)
        {
            lines << tr("Size: %1").arg(formatBytes(info.originalBytes));
        }
        else
        {
            lines << tr("Size: %1 -> %2").arg(formatBytes(info.originalBytes), formatBytes(newBytes));
        }
    }

    item->setText(lines.join("\n"));
    item->setForeground(entry.enabled ? QBrush() : QBrush(Qt::gray));
}

void PDFOptimizeImagesDialog::updateImageListItems()
{
    for (int row = 0; row < static_cast<int>(m_images.size()); ++row)
    {
        updateImageListItem(row);
    }
}

void PDFOptimizeImagesDialog::updateSummaryInfo()
{
    qint64 originalBytes = 0;
    qint64 optimizedBytes = 0;

    for (ImageEntry& entry : m_images)
    {
        const pdf::PDFImageOptimizer::ImageInfo& info = entry.info;
        originalBytes += info.originalBytes;

        if (!entry.enabled)
        {
            optimizedBytes += info.originalBytes;
            continue;
        }

        const pdf::PDFImageOptimizer::Settings& settings = entry.overrideEnabled ? entry.overrideSettings : m_settings;
        const pdf::PDFImageOptimizer::ResolvedPlan plan = pdf::PDFImageOptimizer::resolvePlan(info, settings);
        const int newBytes = getEstimatedBytes(entry, settings, plan);
        const bool keepOriginal = info.isImageMask || newBytes <= 0 ||
                                  (settings.keepOriginalIfLarger && info.originalBytes > 0 && newBytes >= info.originalBytes);

        optimizedBytes += keepOriginal ? info.originalBytes : newBytes;
    }

    QStringList lines;
    lines << tr("Original images size: %1").arg(formatBytes(originalBytes));
    lines << tr("Estimated optimized size: %1").arg(formatBytes(optimizedBytes));

    if (originalBytes > 0)
    {
        const double ratio = 100.0 * static_cast<double>(optimizedBytes) / static_cast<double>(originalBytes);
        lines << tr("Compression ratio: %1%").arg(QString::number(ratio, 'f', 1));
    }
    else
    {
        lines << tr("Compression ratio: n/a");
    }

    ui->optimizationSummaryLabel->setText(lines.join("\n"));
}

void PDFOptimizeImagesDialog::updateUi()
{
    ui->imageListWidget->setEnabled(!m_optimizationInProgress);
    ui->globalGroupBox->setEnabled(!m_optimizationInProgress);
    ui->profilesTabWidget->setEnabled(!m_optimizationInProgress);
    ui->selectedGroupBox->setEnabled(!m_optimizationInProgress && !m_images.empty());
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_optimized && !m_optimizationInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_optimizationInProgress);
    if (m_optimizeButton)
    {
        m_optimizeButton->setEnabled(!m_optimizationInProgress && !m_images.empty());
    }
}

void PDFOptimizeImagesDialog::markOptimizationDirty()
{
    if (!m_optimized)
    {
        return;
    }

    m_optimized = false;
    m_optimizedDocument = pdf::PDFDocument();
    updateUi();
}

void PDFOptimizeImagesDialog::updateSelectedImageUi()
{
    pdf::PDFTemporaryValueChange guard(&m_updatingUi, true);

    const std::vector<int> selectedRows = getSelectedRows();
    ImageEntry* entry = getCurrentEntry();

    if (selectedRows.empty() || !entry)
    {
        setCheckBoxSelectionState(ui->imageEnabledCheckBox, 0, 0);
        setCheckBoxSelectionState(ui->imageOverrideCheckBox, 0, 0);
        ui->settingsScopeLabel->clear();
        ui->imageInfoLabel->clear();
        updateSettingsEditorContextUi();
        return;
    }

    int enabledCount = 0;
    int overrideCount = 0;
    qint64 selectedOriginalBytes = 0;
    for (const int row : selectedRows)
    {
        const ImageEntry& selectedEntry = m_images[static_cast<size_t>(row)];
        enabledCount += selectedEntry.enabled ? 1 : 0;
        overrideCount += selectedEntry.overrideEnabled ? 1 : 0;
        selectedOriginalBytes += selectedEntry.info.originalBytes;
    }

    const int selectedCount = static_cast<int>(selectedRows.size());
    setCheckBoxSelectionState(ui->imageEnabledCheckBox, enabledCount, selectedCount);
    setCheckBoxSelectionState(ui->imageOverrideCheckBox, overrideCount, selectedCount);

    if (entry->overrideEnabled)
    {
        if (overrideCount > 1)
        {
            ui->settingsScopeLabel->setText(tr("Edits on the right apply to the overrides of %1 selected images. "
                                               "Each override started as a copy of the global settings.").arg(overrideCount));
        }
        else
        {
            ui->settingsScopeLabel->setText(tr("Edits on the right currently apply only to this image. "
                                               "The override started as a copy of the global settings."));
        }
    }
    else
    {
        QString scopeText = tr("Edits on the right currently change the global settings used by images without an override.");
        if (overrideCount > 0)
        {
            scopeText += QChar(' ');
            scopeText += tr("%1 of the selected images use an override and are not affected.").arg(overrideCount);
        }
        ui->settingsScopeLabel->setText(scopeText);
    }

    QStringList details;
    if (selectedCount > 1)
    {
        details << tr("Selected images: %1").arg(selectedCount);
        details << tr("Enabled for optimization: %1").arg(enabledCount);
        details << tr("Original size of selection: %1").arg(formatBytes(selectedOriginalBytes));
    }
    else
    {
        const pdf::PDFImageOptimizer::ImageInfo& info = entry->info;
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
    }
    ui->imageInfoLabel->setText(details.join("\n"));

    updateSettingsEditorContextUi();
    loadSettingsToUi(activeSettings());
}

void PDFOptimizeImagesDialog::setCheckBoxSelectionState(QCheckBox* checkBox, int trueCount, int totalCount)
{
    // The partially checked state is allowed only while the selection is mixed.
    // A single click on it then yields the checked state (Qt cycles unchecked ->
    // partially checked -> checked), which unifies the whole selection.
    const bool isMixed = trueCount > 0 && trueCount < totalCount;
    checkBox->setTristate(isMixed);

    if (isMixed)
    {
        checkBox->setCheckState(Qt::PartiallyChecked);
    }
    else
    {
        checkBox->setCheckState(totalCount > 0 && trueCount == totalCount ? Qt::Checked : Qt::Unchecked);
    }
}

void PDFOptimizeImagesDialog::updateSettingsEditorContextUi()
{
    ImageEntry* entry = getCurrentEntry();
    const bool editingOverride = entry && entry->overrideEnabled;

    int overrideCount = 0;
    if (editingOverride)
    {
        for (const int row : getSelectedRows())
        {
            overrideCount += m_images[static_cast<size_t>(row)].overrideEnabled ? 1 : 0;
        }
    }

    if (!editingOverride)
    {
        ui->globalGroupBox->setTitle(tr("Settings Editor - Global Defaults"));
        ui->profilesTabWidget->setToolTip(tr("These settings currently modify the global defaults used by images without an override."));
    }
    else if (overrideCount > 1)
    {
        ui->globalGroupBox->setTitle(tr("Settings Editor - Selected Image Overrides"));
        ui->profilesTabWidget->setToolTip(tr("These settings currently modify the overrides of all selected images."));
    }
    else
    {
        ui->globalGroupBox->setTitle(tr("Settings Editor - Selected Image Override"));
        ui->profilesTabWidget->setToolTip(tr("These settings currently modify only the selected image override."));
    }
}

void PDFOptimizeImagesDialog::loadSettingsToUi(const pdf::PDFImageOptimizer::Settings& settings)
{
    pdf::PDFTemporaryValueChange guard(&m_updatingUi, true);

    ui->modeComboBox->setCurrentIndex(settings.autoMode ? 0 : 1);
    setCurrentEnum(ui->colorModeComboBox, settings.colorMode, pdf::PDFImageOptimizer::ColorMode::Auto);
    setCurrentEnum(ui->goalComboBox, settings.goal, pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    ui->keepOriginalCheckBox->setChecked(settings.keepOriginalIfLarger);
    ui->preserveAlphaCheckBox->setChecked(settings.preserveTransparency);

    setCurrentEnum(ui->colorAlgComboBox, settings.colorProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    ui->colorDpiSpinBox->setValue(settings.colorProfile.targetDpi);
    setCurrentEnum(ui->colorResampleComboBox, settings.colorProfile.resampleFilter, pdf::PDFImage::ResampleFilter::Bicubic);
    ui->colorJpegQualitySpinBox->setValue(settings.colorProfile.jpegQuality);
    ui->colorJpxRateSpinBox->setValue(settings.colorProfile.jpeg2000Rate);
    ui->colorPredictorCheckBox->setChecked(settings.colorProfile.enablePngPredictor);

    setCurrentEnum(ui->grayAlgComboBox, settings.grayProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    ui->grayDpiSpinBox->setValue(settings.grayProfile.targetDpi);
    setCurrentEnum(ui->grayResampleComboBox, settings.grayProfile.resampleFilter, pdf::PDFImage::ResampleFilter::Bicubic);
    ui->grayJpegQualitySpinBox->setValue(settings.grayProfile.jpegQuality);
    ui->grayJpxRateSpinBox->setValue(settings.grayProfile.jpeg2000Rate);
    ui->grayPredictorCheckBox->setChecked(settings.grayProfile.enablePngPredictor);

    setCurrentEnum(ui->bitonalAlgComboBox, settings.bitonalProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    ui->bitonalDpiSpinBox->setValue(settings.bitonalProfile.targetDpi);
    setCurrentEnum(ui->bitonalResampleComboBox, settings.bitonalProfile.resampleFilter, pdf::PDFImage::ResampleFilter::Bicubic);
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
    ImageEntry* entry = getCurrentEntry();
    if (entry && entry->overrideEnabled)
    {
        return entry->overrideSettings;
    }
    return m_settings;
}

void PDFOptimizeImagesDialog::applyUiToActiveSettings()
{
    const ImageEntry* currentEntry = getCurrentEntry();
    if (!currentEntry || !currentEntry->overrideEnabled)
    {
        applyUiToSettings(m_settings);
        return;
    }

    // The editor shows the override of the current image. Write the values into
    // every selected image which also has an override, so a whole group of
    // images can be tuned at once.
    for (const int row : getSelectedRows())
    {
        ImageEntry& entry = m_images[static_cast<size_t>(row)];
        if (entry.overrideEnabled)
        {
            applyUiToSettings(entry.overrideSettings);
        }
    }
}

int PDFOptimizeImagesDialog::getEstimatedBytes(ImageEntry& entry,
                                               const pdf::PDFImageOptimizer::Settings& settings,
                                               const pdf::PDFImageOptimizer::ResolvedPlan& plan)
{
    const QByteArray cacheKey = createEstimateCacheKey(settings, plan);
    if (!entry.estimateCacheValid || entry.estimateCacheKey != cacheKey)
    {
        entry.estimateCacheBytes = pdf::PDFImageOptimizer::estimateEncodedBytes(entry.info, plan, nullptr);
        entry.estimateCacheKey = cacheKey;
        entry.estimateCacheValid = true;
    }

    return entry.estimateCacheBytes;
}

PDFOptimizeImagesDialog::ImageEntry* PDFOptimizeImagesDialog::getCurrentEntry()
{
    int row = ui->imageListWidget->currentRow();
    if (row < 0 || row >= static_cast<int>(m_images.size()))
    {
        return nullptr;
    }
    return &m_images[static_cast<size_t>(row)];
}

const PDFOptimizeImagesDialog::ImageEntry* PDFOptimizeImagesDialog::getCurrentEntry() const
{
    int row = ui->imageListWidget->currentRow();
    if (row < 0 || row >= static_cast<int>(m_images.size()))
    {
        return nullptr;
    }
    return &m_images[static_cast<size_t>(row)];
}

std::vector<int> PDFOptimizeImagesDialog::getSelectedRows() const
{
    std::vector<int> rows;

    const QList<QListWidgetItem*> selectedItems = ui->imageListWidget->selectedItems();
    rows.reserve(selectedItems.size());

    for (QListWidgetItem* item : selectedItems)
    {
        const int row = ui->imageListWidget->row(item);
        if (row >= 0 && row < static_cast<int>(m_images.size()))
        {
            rows.push_back(row);
        }
    }

    if (rows.empty())
    {
        const int currentRow = ui->imageListWidget->currentRow();
        if (currentRow >= 0 && currentRow < static_cast<int>(m_images.size()))
        {
            rows.push_back(currentRow);
        }
    }

    std::sort(rows.begin(), rows.end());
    return rows;
}

void PDFOptimizeImagesDialog::onOptimizeButtonClicked()
{
    Q_ASSERT(!m_optimizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    applyUiToActiveSettings();

    m_optimizationInProgress = true;
    m_optimized = false;
    m_optimizedDocument = pdf::PDFDocument();

    pdf::PDFImageOptimizer::ImageOverrides overrides;
    overrides.clear();
    for (const ImageEntry& entry : m_images)
    {
        if (!entry.enabled)
        {
            pdf::PDFImageOptimizer::ImageOverride overrideInfo;
            overrideInfo.enabled = false;
            overrides[entry.info.reference] = overrideInfo;
            continue;
        }

        if (entry.overrideEnabled)
        {
            pdf::PDFImageOptimizer::ImageOverride overrideInfo;
            overrideInfo.enabled = true;
            overrideInfo.useCustomSettings = true;
            overrideInfo.settings = entry.overrideSettings;
            overrides[entry.info.reference] = overrideInfo;
        }
    }

    const pdf::PDFImageOptimizer::Settings settings = m_settings;
    const pdf::PDFDocument* document = m_document;
    pdf::PDFProgress* progress = m_progress;

    // Run against a snapshot of the dialog state. The UI is disabled while the
    // task runs, so the returned document always matches the currently accepted
    // settings.
    m_future = QtConcurrent::run([document, settings, overrides, progress]() -> pdf::PDFDocument
    {
        pdf::PDFImageOptimizer optimizer;
        return optimizer.optimize(document, settings, overrides, progress);
    });

    m_futureWatcher.emplace();
    connect(&m_futureWatcher.value(), &QFutureWatcher<void>::finished, this, &PDFOptimizeImagesDialog::onOptimizationFinished);
    m_futureWatcher->setFuture(m_future);

    updateUi();
}

void PDFOptimizeImagesDialog::onOptimizationFinished()
{
    m_future.waitForFinished();
    m_optimizedDocument = m_future.result();
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

    applyUiToActiveSettings();
    markOptimizationDirty();
    updateImageListItems();
    updateSummaryInfo();
    updatePreview();
}

void PDFOptimizeImagesDialog::onSelectionChanged()
{
    updateSelectedImageUi();

    // Building a preview resamples and re-encodes the image, so rebuild it only
    // when the current image really changed. Both selection signals are wired to
    // this slot and can fire for a single click.
    const int currentRow = ui->imageListWidget->currentRow();
    if (currentRow != m_previewRow)
    {
        m_previewRow = currentRow;
        updatePreview();
    }
}

void PDFOptimizeImagesDialog::onOverrideClicked()
{
    const Qt::CheckState state = ui->imageOverrideCheckBox->checkState();
    if (state == Qt::PartiallyChecked)
    {
        return;
    }

    const bool checked = state == Qt::Checked;
    for (const int row : getSelectedRows())
    {
        ImageEntry& entry = m_images[static_cast<size_t>(row)];
        entry.overrideEnabled = checked;
        if (checked)
        {
            entry.overrideSettings = m_settings;
        }
    }

    markOptimizationDirty();
    updateSelectedImageUi();
    updateImageListItems();
    updateSummaryInfo();
    updatePreview();
}

void PDFOptimizeImagesDialog::onImageEnabledClicked()
{
    const Qt::CheckState state = ui->imageEnabledCheckBox->checkState();
    if (state == Qt::PartiallyChecked)
    {
        return;
    }

    const bool checked = state == Qt::Checked;
    for (const int row : getSelectedRows())
    {
        m_images[static_cast<size_t>(row)].enabled = checked;
    }

    markOptimizationDirty();
    updateSelectedImageUi();
    updateImageListItems();
    updateSummaryInfo();
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
    ui->previewBeforeImageLabel->clear();
    ui->previewAfterImageLabel->clear();
    ui->previewInfoLabel->clear();

    auto setPreviewPixmap = [](QLabel* label, const QImage& image)
    {
        if (!label || image.isNull())
        {
            return;
        }

        QSize previewSize = label->contentsRect().size();
        if (previewSize.width() <= 0 || previewSize.height() <= 0)
        {
            previewSize = QSize(320, 320);
        }

        const qreal devicePixelRatio = label->devicePixelRatioF();
        const QSize physicalPreviewSize = previewSize * devicePixelRatio;
        QPixmap pix = QPixmap::fromImage(image.scaled(physicalPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pix.setDevicePixelRatio(devicePixelRatio);
        label->setPixmap(pix);
    };

    ImageEntry* entry = getCurrentEntry();
    if (!entry)
    {
        return;
    }

    QImage before = entry->info.image;
    setPreviewPixmap(ui->previewBeforeImageLabel, before);

    if (!entry->enabled)
    {
        setPreviewPixmap(ui->previewAfterImageLabel, before);
        ui->previewInfoLabel->setText(tr("Optimization is disabled for the selected image. The original image will be kept."));
        return;
    }

    const pdf::PDFImageOptimizer::Settings& settings = entry->overrideEnabled ? entry->overrideSettings : m_settings;
    pdf::PDFImageOptimizer::ResolvedPlan plan = pdf::PDFImageOptimizer::resolvePlan(entry->info, settings);
    QImage after = pdf::PDFImageOptimizer::createPreviewImage(entry->info, plan, true);

    if (!after.isNull())
    {
        setPreviewPixmap(ui->previewAfterImageLabel, after);
    }

    int newBytes = getEstimatedBytes(*entry, settings, plan);
    QString infoText = tr("Estimated size: %1 -> %2").arg(formatBytes(entry->info.originalBytes),
                                                          formatBytes(newBytes));
    if (settings.keepOriginalIfLarger && entry->info.originalBytes > 0 && newBytes >= entry->info.originalBytes)
    {
        infoText += tr(" (will keep original)");
    }
    ui->previewInfoLabel->setText(infoText);
}

}   // namespace pdfviewer
